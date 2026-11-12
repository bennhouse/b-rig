#pragma once
// Riverbero algoritmico "mix ready" per chitarra elettrica (metal moderno).
//
// Struttura: pre-delay -> filtri di ingresso (passa-alto + passa-basso, come il
// ritorno filtrato di un aux) -> riflessioni precoci -> 4 diffusori allpass ->
// rete di ritardi retroazionata a 8 linee (FDN, matrice di Householder) con
// smorzamento delle alte per linea e leggera modulazione. Uscita stereo
// decorrelata. Nessuna allocazione nel thread audio, latenza zero.
//
// Tre timbri:
//   room  - ambiente corto e stretto: dà aria alle ritmiche senza impastarle
//   plate - lastra brillante e densa: il classico per lead e parti melodiche
//   hall  - sala ampia con pre-delay lungo e "sparkle": un alone all'ottava sopra (shimmer
//           leggero, filtrato sopra 1,2 kHz) che cresce nella coda, più aria e una modulazione
//           più ricca. Nessuna saturazione: solo vivacità, con un carattere ancora neutro.

#include <array>
#include <cmath>
#include <vector>
#include "Biquad.h"

namespace sp
{

struct ReverbTimbre
{
    double size;         // scala delle lunghezze delle linee
    double rt60;         // tempo di decadimento percepito (s), misurato
    double loopRt;       // valore usato nella rete (compensa lo smorzamento delle alte)
    double predelayMs;
    double hpfHz, lpfHz; // filtri del segnale che entra nel riverbero
    double dampHz;       // smorzamento delle alte dentro la coda
    double diffusion;    // 0..0.8: densità iniziale
    double erLevel;      // livello delle riflessioni precoci
    double modDepthMs, modRateHz;
    double outGain;      // normalizzazione: stesso volume percepito tra i timbri
    double shimmer;      // quota dell'ottava sopra rimandata nella coda (0 = nessuno)
};

class Reverb
{
public:
    enum Type { Room = 0, Plate = 1, Hall = 2 };
    static constexpr int kN = 8;

    static const ReverbTimbre& timbre (int t)
    {
        static const ReverbTimbre table[3] = {
            //  size  rt60  loop   pre   hpf    lpf     damp    diff  er    mod   rate  out    shimmer
            { 0.38, 0.65, 0.78,  8.0, 300.0, 8000.0, 6000.0, 0.60, 0.70, 0.00, 0.0, 1.454, 0.0 },    // room
            { 0.62, 1.80, 2.09, 25.0, 250.0, 7500.0, 9000.0, 0.75, 0.00, 0.12, 0.9, 1.356, 0.0 },    // plate
            { 1.00, 3.20, 3.82, 70.0, 220.0, 7500.0, 5600.0, 0.70, 0.25, 0.55, 0.7, 1.409, 0.50 },   // hall
        };
        return table[t < 0 ? 0 : (t > 2 ? 2 : t)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        auto alloc = [this] (Line& l, double maxMs)
        {
            int size = 1;
            while (size < (int) (maxMs * 0.001 * sr) + 8) size <<= 1;
            l.buf.assign ((size_t) size, 0.0f);
            l.mask = size - 1;
            l.pos = 0;
        };
        alloc (pre, 100.0 + 60.0);                         // pre-delay + riflessioni precoci
        for (auto& l : lines) alloc (l, kLineMs[kN - 1] * 1.0 + 2.0);
        for (int i = 0; i < 4; ++i) alloc (diffusers[(size_t) i], kDiffMs[i] + 1.0);
        alloc (shimBuf, kShimWindowMs + 2.0);
        shimWindow = kShimWindowMs * 0.001 * sr;
        shimStep = 1.0 / shimWindow;
        for (int i = 0; i < kHannSize; ++i)   // finestra sin^2: due teste sfasate di 1/2 sommano a 1
        {
            const double v = std::sin (M_PI * i / kHannSize);
            hann[(size_t) i] = (float) (v * v);
        }
        configured = -1;
        setTimbre (Plate);
        reset();
    }

    void reset()
    {
        for (auto* l : { &pre }) std::fill (l->buf.begin(), l->buf.end(), 0.0f);
        for (auto& l : lines) std::fill (l.buf.begin(), l.buf.end(), 0.0f);
        for (auto& d : diffusers) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
        damp.fill (0.0f);
        hpf.reset(); lpf.reset();
        std::fill (shimBuf.buf.begin(), shimBuf.buf.end(), 0.0f);
        shimHp.reset(); shimPhase = 0.0; shimOut = 0.0f; shimAmt = shimTarget > 0.0f ? shimTarget : 0.0f;
    }

    // Ricalcola i parametri del timbro (nessuna allocazione: si può chiamare dal thread audio).
    void setTimbre (int t)
    {
        if (t == configured) return;
        configured = t;
        const auto& p = timbre (t);
        hpf.c = BiquadCoeffs::make (FilterType::LowCut, sr, p.hpfHz, 0.707, 0.0);
        lpf.c = BiquadCoeffs::make (FilterType::HighCut, sr, p.lpfHz, 0.707, 0.0);
        predelay = p.predelayMs * 0.001 * sr;

        for (int i = 0; i < kN; ++i)
        {
            const double lenS = kLineMs[i] * p.size * 0.001 * sr;
            lineLen[(size_t) i] = lenS;
            gain[(size_t) i] = (float) std::pow (10.0, -3.0 * lenS / (p.loopRt * sr));   // -60 dB in rt60
        }
        dampCoef = (float) std::exp (-2.0 * M_PI * p.dampHz / sr);
        for (int i = 0; i < 4; ++i) diffLen[(size_t) i] = (int) (kDiffMs[i] * (0.6 + 0.4 * p.size) * 0.001 * sr);
        diffGain = (float) p.diffusion;
        // pre-delay e riflessioni precoci: ritardi interi (la frazione di campione non si sente)
        predelayInt = (int) std::lround (predelay);
        for (int i = 0; i < 6; ++i)
        {
            erL[(size_t) i] = (int) std::lround (predelay + kErMsL[i] * (0.5 + 0.5 * p.size) * 0.001 * sr);
            erR[(size_t) i] = (int) std::lround (predelay + kErMsR[i] * (0.5 + 0.5 * p.size) * 0.001 * sr);
        }
        erLevel = (float) p.erLevel;
        modDepth = p.modDepthMs * 0.001 * sr;
        const double w = 2.0 * M_PI * p.modRateHz / sr * kModStep;   // gli LFO avanzano ogni kModStep campioni
        rotC = std::cos (w); rotS = std::sin (w);
        for (int i = 0; i < kN; ++i)
        {
            const double ph = 2.0 * M_PI * i / kN;
            lfoC[(size_t) i] = std::cos (ph); lfoS[(size_t) i] = std::sin (ph);
        }
        outGain = (float) p.outGain;
        for (int i = 0; i < kN; ++i) curLen[(size_t) i] = (float) lineLen[(size_t) i];
        modCount = 0;
        shimGain = (float) p.shimmer;
        shimTarget = -1.0f;   // -1 = shimmer fisso del timbro (setShimmerAmount lo rende regolabile)
        shimHp.c = BiquadCoeffs::make (FilterType::LowCut, sr, 1200.0, 0.707, 0.0);
    }

    // Shimmer verb (hall nel delverb): 0 = hall pieno senza ottava, 1 = shimmer pieno. L'ottava sopra rientra
    // nella coda (fino a 0,6) e se ne sente anche uno strato diretto; il volume totale resta costante.
    void setShimmerAmount (float a)
    {
        a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        shimTarget = a;
    }

    // Elabora n campioni: in = segnale mono (già equalizzato), outL/outR = solo riverbero.
    void process (const float* in, float* outL, float* outR, int n)
    {
        const bool modulate = modDepth > 0.0;
        for (int s = 0; s < n; ++s)
        {
            // modulazione (sotto 1 Hz): lunghezze ricalcolate ogni kModStep campioni, non a ogni campione
            if (modulate && modCount-- <= 0)
            {
                modCount = kModStep - 1;
                for (int i = 0; i < kN; ++i)
                {
                    curLen[(size_t) i] = (float) (lineLen[(size_t) i] + modDepth * lfoS[(size_t) i]);
                    const double c = lfoC[(size_t) i], sn = lfoS[(size_t) i];   // LFO in quadratura: una rotazione
                    lfoC[(size_t) i] = c * rotC - sn * rotS;
                    lfoS[(size_t) i] = sn * rotC + c * rotS;
                }
            }

            // filtri d'ingresso (ritorno "pulito": niente fango sotto, niente fizz sopra)
            const float x = lpf.process (hpf.process (in[s]));
            pre.write (x);

            // pre-delay + riflessioni precoci
            float d = pre.read (predelayInt);
            float eL = 0.0f, eR = 0.0f;
            if (erLevel > 0.0f)
                for (int i = 0; i < 6; ++i)
                {
                    eL += kErGain[i] * pre.read (erL[(size_t) i]);
                    eR += kErGain[i] * pre.read (erR[(size_t) i]);
                }

            // diffusione: 4 allpass in serie
            for (int i = 0; i < 4; ++i)
            {
                auto& ap = diffusers[(size_t) i];
                const float z = ap.read (diffLen[(size_t) i]);
                const float v = d + diffGain * z;
                ap.write (v);
                d = z - diffGain * v;
            }

            // sparkle: l'ottava sopra dell'uscita della coda (campione precedente) rientra nella rete
            if (shimTarget >= 0.0f)   // shimmer verb: quantità con rampa (nessuno scatto girando il balance)
            {
                shimAmt += 0.0005f * (shimTarget - shimAmt);
                shimGain = 0.6f * shimAmt;
            }
            if (shimGain > 0.0f) d += shimGain * shimOut;

            // FDN a 8 linee
            std::array<float, kN> y {};
            float sum = 0.0f;
            for (int i = 0; i < kN; ++i)
            {
                float v = lines[(size_t) i].readFrac (curLen[(size_t) i]);
                damp[(size_t) i] = (1.0f - dampCoef) * v + dampCoef * damp[(size_t) i];   // smorzamento alte
                v = damp[(size_t) i] * gain[(size_t) i];
                y[(size_t) i] = v;
                sum += v;
            }
            const float hh = sum * (2.0f / kN);                                   // Householder
            float wl = 0.0f, wr = 0.0f;
            const float inj = d * 0.35f;
            for (int i = 0; i < kN; ++i)
            {
                wl += kSignL[i] * y[(size_t) i];
                wr += kSignR[i] * y[(size_t) i];
                lines[(size_t) i].write (y[(size_t) i] - hh + kSignIn[i] * inj);
            }

            if (shimGain > 0.0f) shimOut = pitchUpOctave (shimHp.process (0.25f * (wl + wr)));

            float sl = 0.0f, comp = 1.0f;
            if (shimTarget >= 0.0f) { sl = 0.9f * shimAmt * shimOut; comp = 1.0f / std::sqrt (1.0f + kShimEnergy * shimAmt); }
            outL[s] = comp * outGain * (0.5f * wl + erLevel * 0.5f * eL + sl);
            outR[s] = comp * outGain * (0.5f * wr + erLevel * 0.5f * eR - sl);
        }
        // rinormalizza gli LFO (evita la deriva numerica dell'ampiezza)
        if (modulate)
            for (int i = 0; i < kN; ++i)
            {
                const double m = std::sqrt (lfoC[(size_t) i] * lfoC[(size_t) i] + lfoS[(size_t) i] * lfoS[(size_t) i]);
                lfoC[(size_t) i] /= m; lfoS[(size_t) i] /= m;
            }
        for (auto& v : damp) if (std::abs (v) < 1.0e-20f) v = 0.0f;
    }

private:
    // Trasposizione di un'ottava sopra: due teste di lettura che scorrono a velocità doppia
    // su una finestra di 40 ms, incrociate con finestre sin^2 (somma costante, nessun click).
    float pitchUpOctave (float x)
    {
        shimBuf.write (x);
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            double ph = shimPhase + 0.5 * k;
            if (ph >= 1.0) ph -= 1.0;
            const double delay = 1.0 + (1.0 - ph) * shimWindow;   // scende di 1 campione per campione: velocità 2x
            y += hann[(size_t) (ph * kHannSize) & (kHannSize - 1)] * shimBuf.readFrac (delay);
        }
        shimPhase += shimStep;
        if (shimPhase >= 1.0) shimPhase -= 1.0;
        return y;
    }

    struct Line
    {
        std::vector<float> buf;
        int mask = 0, pos = 0;
        inline void write (float v) { buf[(size_t) pos] = v; pos = (pos + 1) & mask; }
        inline float read (int delay) const { return buf[(size_t) ((pos - delay) & mask)]; }
        inline float readFrac (double delay) const
        {
            const int di = (int) delay;
            const float t = (float) (delay - di);
            const float a = buf[(size_t) ((pos - di) & mask)], b = buf[(size_t) ((pos - di - 1) & mask)];
            return a + t * (b - a);
        }
        inline float readFrac (float delay) const
        {
            const int di = (int) delay;
            const float t = delay - (float) di;
            const float a = buf[(size_t) ((pos - di) & mask)], b = buf[(size_t) ((pos - di - 1) & mask)];
            return a + t * (b - a);
        }
    };

    // lunghezze base delle linee (ms, mutuamente "prime") per size = 1
    static constexpr double kLineMs[kN] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    static constexpr double kDiffMs[4] = { 4.77, 3.59, 12.73, 9.31 };
    static constexpr double kErMsL[6] = { 7.1, 11.3, 17.9, 23.5, 31.3, 41.7 };
    static constexpr double kErMsR[6] = { 8.9, 13.1, 19.7, 27.1, 35.9, 44.3 };
    static constexpr float kErGain[6] = { 0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.3f };
    static constexpr float kSignL[kN]  = { 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr float kSignR[kN]  = { 1, 1, -1, -1, 1, 1, -1, -1 };
    static constexpr float kSignIn[kN] = { 1, -1, 1, 1, -1, 1, -1, -1 };

    double sr = 48000.0;
    int configured = -1;
    Line pre;
    std::array<Line, kN> lines;
    std::array<Line, 4> diffusers;
    std::array<double, kN> lineLen {};
    std::array<float, kN> gain {}, damp {};
    std::array<int, 4> diffLen {};
    std::array<int, 6> erL {}, erR {};
    static constexpr int kModStep = 16;
    std::array<float, kN> curLen {};
    int predelayInt = 0, modCount = 0;
    std::array<double, kN> lfoC {}, lfoS {};
    double predelay = 0.0, modDepth = 0.0, rotC = 1.0, rotS = 0.0;
    float dampCoef = 0.0f, diffGain = 0.0f, erLevel = 0.0f, outGain = 1.0f;
    Biquad hpf, lpf;

    static constexpr double kShimWindowMs = 40.0;
    static constexpr int kHannSize = 1024;
    Line shimBuf;
    Biquad shimHp;
    std::array<float, kHannSize> hann {};
    double shimWindow = 1920.0, shimStep = 1.0 / 1920.0, shimPhase = 0.0;
    float shimGain = 0.0f, shimOut = 0.0f, shimTarget = -1.0f, shimAmt = 0.0f;
public:
    static inline float kShimEnergy = 0.2f;    // energia aggiunta dallo shimmer pieno (misurata nei test)
private:
};

} // namespace sp

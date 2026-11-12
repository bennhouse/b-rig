#pragma once
// Delverb: delay + riverbero in un unico blocco, con un solo mix.
//
//   ingresso ──┬───────────────────────────────► riverbero ─┐
//              └► delay (sync al tempo) ───────────┬─► send ─┘      (prima il delay, poi il riverbero:
//                                                  └──────────────► + uscita    le ripetizioni finiscono nello spazio)
//
// Delay e riverbero stanno sempre insieme sugli stessi canali, mai uno a sinistra e uno a destra:
//   - ingresso mono (o spread spento): tutto mono, identico su L e R;
//   - ingresso stereo o spread acceso: un delay per canale (stessi tempi, segue l'immagine del
//     segnale) e riverbero stereo. Il passaggio tra i due è sfumato.
// Il delay è "da lead metal": a tempo con il progetto, filtrato (ripetizioni più scure
// ad ogni giro) e con "ducking" (si abbassa mentre suoni e riemerge nelle pause).
//
//   room  - solo riverbero corto (ritmiche): nessun delay
//   plate - lead e parti melodiche: 1/8 puntato, poche ripetizioni, ducking marcato
//   hall  - lead lunghi, puliti, atmosfere: nessun delay; il balance va da hall pieno a shimmer verb (ottava sopra)

#include <array>
#include <cmath>
#include <vector>
#include "Biquad.h"
#include "Reverb.h"

namespace sp
{

struct DelaySettings
{
    double note;          // durata in quarti (0,75 = 1/8 puntato, 1,5 = 1/4 puntato)
    double feedback;      // 0..1
    double hpfHz, lpfHz;  // filtri su ingresso e retroazione del delay
    double level;         // livello del delay rispetto al riverbero
    double send;          // quanto delay entra nel riverbero
    double duck;          // profondità del ducking (0 = niente)
    double modMs, modHz;  // leggera modulazione "a nastro"
    double powerRatio;    // energia delay / riverbero al bilanciamento di progetto (misurata)
};

class Delverb
{
public:
    static const DelaySettings& delaySettings (int t)
    {
        static const DelaySettings table[3] = {
            //  note  fb    hpf    lpf     level  send  duck  mod   rate  ratio
            { 0.00, 0.00,   0.0,     0.0, 0.00, 0.00, 0.0, 0.00, 0.0, 0.0 },     // room: niente delay
            { 0.75, 0.28, 400.0, 4500.0, 1.405, 0.35, 3.3, 0.00, 0.0, 0.631 },  // plate (delay -2 dB)
            { 0.00, 0.00,   0.0,     0.0, 0.00, 0.00, 0.0, 0.00, 0.0, 0.0 },     // hall: niente delay (hall + shimmer)
        };
        return table[t < 0 ? 0 : (t > 2 ? 2 : t)];
    }

    // Normalizzazione finale: delay + riverbero allo stesso volume del riverbero da solo.
    static double outGain (int t)
    {
        static const double g[3] = { 1.0, 0.749, 1.0 };   // misurati: stesso volume wet della sola room
        return g[t < 0 ? 0 : (t > 2 ? 2 : t)];
    }

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        reverb.prepare (sampleRate);
        int size = 1;
        while (size < (int) (2.2 * sr)) size <<= 1;   // fino a 1/4 puntato a 40 BPM
        dl.assign ((size_t) size, 0.0f);
        dr.assign ((size_t) size, 0.0f);
        mask = size - 1;
        maxDelay = (double) size - 8.0;
        revIn.assign ((size_t) maxBlock, 0.0f);
        dOutL.assign ((size_t) maxBlock, 0.0f);
        dOutR.assign ((size_t) maxBlock, 0.0f);
        envAtk = (float) std::exp (-1.0 / (0.005 * sr));
        envRel = (float) std::exp (-1.0 / (0.250 * sr));
        timeCoef = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        balCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
        widthCoef = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        configured = -1;
        setTimbre (1);
        reset();
    }


    void reset()
    {
        reverb.reset();
        std::fill (dl.begin(), dl.end(), 0.0f);
        std::fill (dr.begin(), dr.end(), 0.0f);
        for (auto* f : { &inHpL, &inLpL, &inHpR, &inLpR, &fbHpL, &fbLpL, &fbHpR, &fbLpR }) f->reset();
        width = targetWidth;
        env = 0.0f;
        curTime = targetTime;
        lfoC = 1.0; lfoS = 0.0;
        revG = targetRevG; dlyG = targetDlyG;
    }

    void setTimbre (int t)
    {
        reverb.setTimbre (t);
        if (t == configured) return;
        configured = t;
        ds = delaySettings (t);
        delayOn = ds.level > 0.0;
        if (delayOn)
        {
            const auto hp = BiquadCoeffs::make (FilterType::LowCut, sr, ds.hpfHz, 0.707, 0.0);
            const auto lp = BiquadCoeffs::make (FilterType::HighCut, sr, ds.lpfHz, 0.707, 0.0);
            inHpL.c = inHpR.c = fbHpL.c = fbHpR.c = hp;
            inLpL.c = inLpR.c = fbLpL.c = fbLpR.c = lp;
        }
        const double w = 2.0 * M_PI * ds.modHz / sr;
        rotC = std::cos (w); rotS = std::sin (w);
        modDepth = ds.modMs * 0.001 * sr;
        gain = (float) outGain (t);
        setTempo (bpm);
        curTime = targetTime;
    }

    void setTempo (double newBpm)
    {
        bpm = newBpm > 20.0 && newBpm < 400.0 ? newBpm : 120.0;
        targetTime = std::fmin (maxDelay - modDepth - 2.0, std::fmax (1.0, 60.0 / bpm * ds.note * sr));
    }

    // Preset di bilanciamento, scelti per lo scopo di ciascun timbro (quota di energia del delay):
    //  plate (lead)      0,61 -> delay +2 dB sopra il riverbero: nei lead metal è il delay a
    //                    "parlare" (ripetizioni ritmiche nelle pause, grazie al ducking), mentre
    //                    la plate resta un alone che lega le note senza impastarle.
    //  hall (atmosfere)  0,28 -> riverbero +4 dB sopra il delay: comanda lo spazio, le
    //                    ripetizioni scure e modulate restano una scia dentro la coda.
    static double presetDelayShare (int t)
    {
        static const double p[3] = { 0.0, 0.61, 0.35 };   // hall: quota dello shimmer
        return p[t < 0 ? 0 : (t > 2 ? 2 : t)];
    }

    // Bilanciamento: d = quota di energia del delay (0 = solo riverbero, 1 = solo delay).
    // Il volume totale resta costante: si sposta solo la quota tra le due parti.
    // (powerRatio = rapporto delay/riverbero misurato con i guadagni a 1, punto di taratura.)
    void setDelayShare (double d)
    {
        d = std::fmax (0.0, std::fmin (1.0, d));
        if (configured == 2) { reverb.setShimmerAmount ((float) d); targetRevG = 1.0f; targetDlyG = 1.0f; return; }   // hall: shim <-> verb
        if (! delayOn) { targetRevG = 1.0f; targetDlyG = 1.0f; return; }
        const double s0 = 1.0 / (1.0 + ds.powerRatio);   // quota del riverbero al punto di taratura
        const double s = 1.0 - d;
        targetRevG = (float) std::sqrt (s / s0);
        targetDlyG = (float) std::sqrt ((1.0 - s) / (1.0 - s0));
    }

    // Solo per test/calibrazione: escludere una parte o il ducking.
    void setParts (bool rev, bool dly, bool duck) { useRev = rev; useDly = dly; useDuck = duck; }

    // Mono (true) o stereo (false): vedi in cima. Il cambio è sfumato in ~50 ms.
    void setStereo (bool stereo) { targetWidth = stereo ? 1.0f : 0.0f; }

    // Ingresso mono (test e calibrazione): come la versione stereo con L = R.
    void process (const float* in, float* outL, float* outR, int n) { process (in, in, outL, outR, n); }

    // inL/inR: ingresso (uguali in mono); outL/outR: solo effetto (wet). n <= il maxBlock di prepare().
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (delayOn && useDly)
        {
            for (int i = 0; i < n; ++i)
            {
                const float xL = inL[i], xR = inR[i];
                const float a = std::max (std::abs (xL), std::abs (xR));
                env = a > env ? envAtk * (env - a) + a : envRel * (env - a) + a;

                curTime += timeCoef * (targetTime - curTime);          // cambi di tempo senza click
                double t = curTime;
                if (modDepth > 0.0)
                {
                    t += modDepth * lfoS;
                    const double c = lfoC;
                    lfoC = c * rotC - lfoS * rotS;
                    lfoS = lfoS * rotC + c * rotS;
                }
                // un delay per canale, stessi tempi: le ripetizioni restano dove suona il segnale
                const float yL = readFrac (dl, t), yR = readFrac (dr, t);
                const float fb = (float) ds.feedback;
                dl[(size_t) pos] = inLpL.process (inHpL.process (xL)) + fb * fbLpL.process (fbHpL.process (yL));
                dr[(size_t) pos] = inLpR.process (inHpR.process (xR)) + fb * fbLpR.process (fbHpR.process (yR));
                pos = (pos + 1) & mask;

                const float duckG = useDuck ? 1.0f / (1.0f + (float) ds.duck * env) : 1.0f;
                dOutL[(size_t) i] = yL * duckG;
                dOutR[(size_t) i] = yR * duckG;
                // prima il delay, poi il riverbero: le ripetizioni entrano nello spazio
                revIn[(size_t) i] = 0.5f * (xL + xR) + (float) ds.send * 0.5f * (dOutL[(size_t) i] + dOutR[(size_t) i]);
            }
            const double m = std::sqrt (lfoC * lfoC + lfoS * lfoS);
            lfoC /= m; lfoS /= m;
        }
        else
        {
            for (int i = 0; i < n; ++i) revIn[(size_t) i] = 0.5f * (inL[i] + inR[i]);
            std::fill (dOutL.begin(), dOutL.begin() + n, 0.0f);
            std::fill (dOutR.begin(), dOutR.begin() + n, 0.0f);
        }

        if (useRev) reverb.process (revIn.data(), outL, outR, n);
        else { std::fill (outL, outL + n, 0.0f); std::fill (outR, outR + n, 0.0f); }

        // ogni canale ha tutte le ripetizioni (non più alternate): -3 dB rispetto al ping-pong
        const float lvl = (float) ds.level * 0.70710678f;
        for (int i = 0; i < n; ++i)
        {
            revG += balCoef * (targetRevG - revG);    // bilanciamento senza scatti
            dlyG += balCoef * (targetDlyG - dlyG);
            width += widthCoef * (targetWidth - width);
            const float l = revG * outL[i] + dlyG * lvl * dOutL[(size_t) i];
            const float r = revG * outR[i] + dlyG * lvl * dOutR[(size_t) i];
            // mono: (L + R) / sqrt(2) su entrambi i canali (stessa potenza); stereo: L e R
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            const float midG = 1.41421356f + width * (1.0f - 1.41421356f);
            outL[i] = gain * (midG * mid + width * side);
            outR[i] = gain * (midG * mid - width * side);
        }
    }

private:
    inline float readFrac (const std::vector<float>& b, double delay) const
    {
        const int di = (int) delay;
        const float t = (float) (delay - di);
        const float x0 = b[(size_t) ((pos - di) & mask)], x1 = b[(size_t) ((pos - di - 1) & mask)];
        return x0 + t * (x1 - x0);
    }

    Reverb reverb;
    DelaySettings ds {};
    double sr = 48000.0, bpm = 120.0, targetTime = 1.0, curTime = 1.0, maxDelay = 1.0, modDepth = 0.0;
    double lfoC = 1.0, lfoS = 0.0, rotC = 1.0, rotS = 0.0;
    std::vector<float> dl, dr, revIn, dOutL, dOutR;
    int mask = 0, pos = 0, configured = -1;
    bool delayOn = false, useRev = true, useDly = true, useDuck = true;
    float revG = 1.0f, dlyG = 1.0f, targetRevG = 1.0f, targetDlyG = 1.0f, balCoef = 0.0f;
    float env = 0.0f, envAtk = 0.0f, envRel = 0.0f, timeCoef = 0.0f, gain = 1.0f;
    float width = 1.0f, targetWidth = 1.0f, widthCoef = 0.0f;
    Biquad inHpL, inLpL, inHpR, inLpR, fbHpL, fbLpL, fbHpR, fbLpR;
};

} // namespace sp

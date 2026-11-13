#pragma once
// b-amp: motore dell'amplificatore a 3 canali. Solo matematica, nessuna allocazione, noexcept.
//
//   ingresso  -> tap del tuner -> HP 35 Hz -> notch 50/100 Hz (Q 40, solo con gate > -85 dB)
//             -> gate A (VCA pre-gain, dal rilevatore sulla DI)
//             -> [canale: boost (lead) / bright (clean)] -> stadi ADAA (2 / 4 / 5) a 2x o 4x
//             -> gate B (VCA + passa-basso dinamico, stesso rilevatore) -> tone stack passivo (bilineare)
//             -> master -> finale (sag + soft clip ADAA simmetrico) -> cassa (risonanza 96 Hz, presence, LP 11,5 kHz)
//             -> level (manuale o auto)
//
// Stadio di guadagno (Stage): HP SVF pre-stadio, inviluppo 1,5/30 ms -> conduzione di griglia (compressione
// morbida del 15-20 % sui picchi positivi) e spostamento del bias, waveshaper asimmetrico con ADAA del 1° ordine
// (primitiva log cosh stabile, fallback analitico per |dx| < 1e-5), DC blocker a 15 Hz, passa-basso TPT
// "Miller" la cui frequenza scende da 10,5 a 4,2 kHz con la quantità di clipping.
#include <cmath>
#include <algorithm>
#include <array>
#include "smp/Filters.h"

namespace amp
{
constexpr float kPi = 3.14159265358979f;

// log(cosh(x)) numericamente stabile anche per |x| grande
inline float logCosh (float x) noexcept { const float a = std::abs (x); return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f; }
inline double logCoshD (double x) noexcept { const double a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }

// ADAA del 1° ordine in doppia precisione. In float la differenza F(x) - F(xPrev) perde quasi tutte le cifre
// quando x cambia poco tra due campioni (sovracampionamento, note che sostengono): l'errore diviso per una
// differenza piccola diventa un fruscio che segue il livello della chitarra. Anche il caso limite usa la
// funzione esatta (tanh), così non c'è salto tra i due rami.
template <typename Fn, typename Prim>
inline float adaaD (float x, float xPrev, Fn&& f, Prim&& F) noexcept
{
    const double a = x, b = xPrev, d = a - b;
    return (float) (std::abs (d) < 1.0e-7 ? f (0.5 * (a + b)) : (F (a) - F (b)) / d);
}

// Waveshaper asimmetrico: semionda negativa tanh(x) (netta), positiva più morbida k·tanh(x/k).
struct Shaper
{
    float k = 1.6f;   // morbidezza della semionda positiva (1 = simmetrico)
    inline float f (float x) const noexcept { return x < 0.0f ? sp::fastTanh (x) : k * sp::fastTanh (x / k); }
    inline float F (float x) const noexcept { return x < 0.0f ? logCosh (x) : k * k * logCosh (x / k); }
    inline float adaa (float x, float& xPrev) const noexcept
    {
        const double kd = k;
        const float y = adaaD (x, xPrev,
                               [kd] (double v) { return v < 0.0 ? std::tanh (v) : kd * std::tanh (v / kd); },
                               [kd] (double v) { return v < 0.0 ? logCoshD (v) : kd * kd * logCoshD (v / kd); });
        xPrev = x;
        return y;
    }
};

struct DcBlock
{
    float a = 0.998f, x1 = 0, y1 = 0;
    void set (double fs, double fc) noexcept { a = (float) std::exp (-2.0 * kPi * fc / fs); }
    inline float process (float x) noexcept { const float y = x - x1 + a * y1; x1 = x; y1 = y; return y; }
    void reset() noexcept { x1 = y1 = 0; }
};

struct Tpt1
{
    float G = 0, z = 0;
    void set (double fs, double fc) noexcept { const double g = std::tan (kPi * fc / fs); G = (float) (g / (1.0 + g)); }
    inline float lp (float x) noexcept { const float v = G * (x - z), y = v + z; z = y + v; return y; }
    void reset() noexcept { z = 0; }
};

struct Stage
{
    Tpt1 hp;               // condensatore di accoppiamento tra gli stadi: passa-alto del 1° ordine (6 dB/ott, come nel circuito)
    Shaper sh;
    float drive = 1, bias = 0, kBias = 0.3f, env = 0, eA = 0, eR = 0, xPrev = 0, clip = 0, clipC = 0;
    float lpLo = 4200, lpHi = 10500; Tpt1 miller; DcBlock dc;
    double fs = 48000;
    int ctl = 0;
    void prepare (double rate, double hpHz) noexcept
    {
        fs = rate; hp.set (fs, hpHz); miller.set (fs, lpHi); dc.set (fs, 15.0);
        eA = sp::coefMs (1.5, fs); eR = sp::coefMs (30.0, fs); clipC = 1.0f - sp::coefMs (5.0, fs);
        reset();
    }
    void setHp (double hz) noexcept { hp.set (fs, hz); }
    void reset() noexcept { hp.reset(); miller.reset(); dc.reset(); env = xPrev = clip = 0; ctl = 0; }
    inline float process (float x) noexcept
    {
        x -= hp.lp (x);   // passa-alto del 1° ordine
        float in = drive * x;
        // conduzione di griglia: i picchi positivi oltre ~0,8 fanno condurre la griglia, che carica il condensatore di
        // accoppiamento -> il punto di lavoro si sposta verso l'interdizione (bias più negativo) e torna in ~30 ms.
        // È il "blocking" dei veri preamp: dopo un colpo forte la nota si sporca e si comprime, poi si riapre.
        const float pos = in > 0.8f ? in - 0.8f : 0.0f;
        env = pos > env ? eA * (env - pos) + pos : eR * (env - pos) + pos;
        if (in > 1.0f) in = 1.0f + 0.82f * (in - 1.0f);                       // compressione morbida del picco positivo
        in += bias - kBias * env;
        const float y = sh.adaa (in, xPrev);
        const float c = std::min (1.0f, std::max (0.0f, std::abs (in) - 0.6f) * 0.5f);
        clip += clipC * (c - clip);
        if ((ctl++ & 15) == 0) miller.set (fs, lpHi + (lpLo - lpHi) * clip);  // capacità Miller: meno alte sotto clipping
        return -miller.lp (dc.process (y));                                    // uno stadio a catodo comune inverte la fase
    }
};

// Tone stack passivo a 3 bande (circuito TMB, Yeh & Smith 2006), trasformata bilineare, coefficienti ogni 16 campioni.
struct ToneStack
{
    double R1 = 250e3, R2 = 1e6, R3 = 25e3, R4 = 56e3, C1 = 470e-12, C2 = 22e-9, C3 = 22e-9;
    float b0 = 1, b1 = 0, b2 = 0, b3 = 0, a1 = 0, a2 = 0, a3 = 0, z1 = 0, z2 = 0, z3 = 0;
    void set (double fs, double t, double m, double l) noexcept
    {
        // taper: bass e mid logaritmici (come i potenziometri audio), treble lineare
        m = std::max (1.0e-3, m * m); l = std::max (1.0e-3, l * l); t = std::max (1.0e-3, std::min (1.0, t));
        const double n1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
        const double n2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
        const double n3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                        + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) + t * C1 * C2 * C3 * R1 * R3 * R4
                        - t * m * C1 * C2 * C3 * R1 * R3 * R4 + t * l * C1 * C2 * C3 * R1 * R2 * R4;
        const double d0 = 1.0;
        const double d1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
        const double d2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                        + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
        const double d3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                        + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4) + l * C1 * C2 * C3 * R1 * R2 * R4
                        + l * C1 * C2 * C3 * R2 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
        const double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
        const double B0 = n1 * c + n2 * c2 + n3 * c3, B1 = n1 * c - n2 * c2 - 3 * n3 * c3, B2 = -n1 * c - n2 * c2 + 3 * n3 * c3, B3 = -n1 * c + n2 * c2 - n3 * c3;
        const double A0 = d0 + d1 * c + d2 * c2 + d3 * c3, A1 = 3 * d0 + d1 * c - d2 * c2 - 3 * d3 * c3, A2 = 3 * d0 - d1 * c - d2 * c2 + 3 * d3 * c3, A3 = d0 - d1 * c + d2 * c2 - d3 * c3;
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); b3 = (float) (B3 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); a3 = (float) (A3 / A0);
    }
    inline float process (float x) noexcept   // forma diretta II trasposta, 3° ordine
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y + z3;
        z3 = b3 * x - a3 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = z3 = 0; }
    void flush() noexcept { if (std::abs (z1) < 1e-20f) z1 = 0; if (std::abs (z2) < 1e-20f) z2 = 0; if (std::abs (z3) < 1e-20f) z3 = 0; }
    // |H| a f (per il grafico / i test)
    double magnitude (double f, double fs) const noexcept
    {
        const double w = 2.0 * kPi * f / fs;
        const double nr = b0 + b1 * std::cos (w) + b2 * std::cos (2 * w) + b3 * std::cos (3 * w), ni = -(b1 * std::sin (w) + b2 * std::sin (2 * w) + b3 * std::sin (3 * w));
        const double dr = 1 + a1 * std::cos (w) + a2 * std::cos (2 * w) + a3 * std::cos (3 * w), di = -(a1 * std::sin (w) + a2 * std::sin (2 * w) + a3 * std::sin (3 * w));
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// Rilevatore del gate sulla DI pulita: isteresi 4 dB, rilascio musicale finché le fondamentali suonano,
// 8 ms quando l'inviluppo crolla (palm-mute / stop).
struct GateDetector
{
    sp::Bq lfHp, lfLp, hfHp;
    float env = 0, eLf = 0, eHf = 0, eA = 0, eR = 0, bA = 0, bR = 0, gain = 0, openThr = 0, closeThr = 0, lastEnv = 0;
    float relSlow = 0, relFast = 0, relMid = 0, relCur = 0, atk = 0, fast = 0, fR = 0; int holdLeft = 0, holdSamples = 0; bool open = false, collapsed = false, stopped = false; float reopen = 0;
    float floorDb = -120.0f, chunkMin = 1.0e9f; int chunkPos = 0, chunkLen = 2400, warm = 0;
    // pavimento del rumore sulla DI già ripulita dal ronzio (dopo il notch): minimo dell'inviluppo a blocchi da 50 ms,
    // solo a gate chiuso (cioè solo rumore). Serve al filtro anti-fruscio sotto le code delle note.
    float cleanEnv = 0;
    // soglia automatica: il fruscio della DI (scheda con molto guadagno) può stare sopra la soglia scelta e tenere il
    // gate sempre aperto; l'ampli lo alza di 30-50 dB e quando si suona lo comprime ("ducking"). Il fruscio si riconosce
    // perché ha più energia sopra i 2,5 kHz che tra 80 e 600 Hz (la chitarra è il contrario): sui blocchi da 50 ms
    // fatti solo di fruscio si misura il suo livello e la soglia effettiva resta 8 dB sopra.
    float autoFloorDb = -120.0f, chunkFast = 0; int chunkNoisy = 0, chunkN = 0, autoPos = 0;
    float autoOpen = 0, autoClose = 0;
    inline void trackAuto (int chunk) noexcept
    {
        chunkFast += fast; ++chunkN;
        if (eHf > 0.7f * eLf) ++chunkNoisy;
        if (++autoPos < chunk) return;
        const float mean = chunkFast / (float) juce::jmax (1, chunkN);
        if (chunkNoisy > (chunkN * 9) / 10 && mean > 1.0e-7f)
        {
            const float m = 20.0f * std::log10 (mean);
            // scende subito, sale piano (0,5 dB ogni 50 ms): una pennata sporca non sposta la soglia
            autoFloorDb = autoFloorDb < -119.0f ? m : (m < autoFloorDb ? m : std::min (m, autoFloorDb + 0.5f));
            autoOpen = sp::dbToGain (autoFloorDb + 8.0f); autoClose = sp::dbToGain (autoFloorDb + 4.0f);
        }
        chunkFast = 0; chunkN = chunkNoisy = autoPos = 0;
    }
    inline void trackClean (float x) noexcept
    {
        const float a = std::abs (x);
        cleanEnv = a > cleanEnv ? eA * (cleanEnv - a) + a : eR * (cleanEnv - a) + a;
        chunkMin = std::min (chunkMin, cleanEnv);
        if (++chunkPos >= chunkLen)
        {
            if (! open && gain < 0.05f && chunkMin > 1.0e-7f && ++warm > 2)   // i primi blocchi (avvio) non contano
            {
                const float m = 20.0f * std::log10 (chunkMin);
                floorDb = floorDb < -119.0f ? m : (m < floorDb ? 0.5f * (m + floorDb) : std::min (m, floorDb + 1.0f));
            }
            chunkMin = 1.0e9f; chunkPos = 0;
        }
    }
    float snrDb() const noexcept { return 20.0f * std::log10 (cleanEnv + 1.0e-9f) - floorDb; }
    // stop delle corde: picco dell'inviluppo veloce sugli ultimi 25 ms contro quello dei 25 ms prima (ogni 16 campioni).
    // Su una finestra più lunga del periodo delle note basse (82 Hz = 12 ms, 41 Hz = 24 ms) un'onda che suona non
    // sembra mai uno stop; prima il confronto su 8 ms scambiava il calo tra due picchi di una nota bassa per uno stop.
    std::array<float, 256> hist {}; int hpos = 0, histSpan = 75;
    bool lowest = false;   // soglia al minimo: solo il gate a soglia, niente stop e niente soglia automatica
    double fs = 48000;
    void prepare (double rate) noexcept
    {
        fs = rate;
        lfHp = {}; lfHp.set (sp::Bq::HP, fs, 80.0, 0.7071); lfLp = {}; lfLp.set (sp::Bq::LP, fs, 600.0, 0.7071);
        hfHp = {}; hfHp.set (sp::Bq::HP, fs, 2500.0, 0.7071);
        eA = sp::coefMs (0.5, fs); eR = sp::coefMs (20.0, fs); bA = sp::coefMs (5.0, fs); bR = sp::coefMs (60.0, fs);
        atk = 1.0f - sp::coefMs (0.3, fs); relSlow = 1.0f - sp::coefMs (300.0, fs); relFast = 1.0f - sp::coefMs (8.0, fs); relMid = 1.0f - sp::coefMs (60.0, fs); relCur = relSlow;
        fR = sp::coefMs (3.0, fs);
        holdSamples = (int) (0.03 * fs); chunkLen = (int) (0.05 * fs);
        histSpan = juce::jlimit (8, 127, (int) (0.025 * fs / 16.0));
        reset();
    }
    void reset() noexcept { autoFloorDb = -120.0f; chunkFast = 0; chunkN = chunkNoisy = autoPos = 0; autoOpen = autoClose = 0; floorDb = -120.0f; chunkMin = 1.0e9f; chunkPos = 0; warm = 0; cleanEnv = 0; hist.fill (0.0f); hpos = 0; fast = 0; collapsed = stopped = false; env = eLf = eHf = lastEnv = 0; gain = 0; open = false; holdLeft = 0; lfHp.reset(); lfLp.reset(); hfHp.reset(); }
    void setThreshold (float db) noexcept { openThr = sp::dbToGain (db); closeThr = sp::dbToGain (db - 6.0f); lowest = db <= -109.5f; }
    // x = DI; restituisce il guadagno del gate (0..1), aggiornando il rilascio ogni 16 campioni
    inline float process (float x, int i) noexcept
    {
        const float a = std::abs (x);
        env = a > env ? eA * (env - a) + a : eR * (env - a) + a;
        fast = a > fast ? eA * (fast - a) + a : fR * (fast - a) + a;
        const float lf = std::abs (lfLp.process (lfHp.process (x))), hf = std::abs (hfHp.process (x));
        eLf = lf > eLf ? bA * (eLf - lf) + lf : bR * (eLf - lf) + lf;
        eHf = hf > eHf ? bA * (eHf - hf) + hf : bR * (eHf - hf) + hf;
        trackAuto (chunkLen);
        const float oThr = lowest ? openThr : std::max (openThr, autoOpen), cThr = lowest ? closeThr : std::max (closeThr, autoClose);
        // isteresi 4 dB sull'inviluppo veloce, con 20 ms di tenuta (copre il periodo delle note più basse)
        if (open) { if (fast < cThr) { if (holdLeft > 0) --holdLeft; else open = false; } else holdLeft = holdSamples; }
        else if (fast > std::max (oThr, reopen)) { open = true; holdLeft = holdSamples; stopped = false; reopen = 0; }   // nuova plettrata
        if (stopped) reopen = std::min (reopen, fast * 2.0f);   // dopo uno stop riapre solo un inviluppo che risale
        if ((i & 15) == 0)
        {
            // stop delle corde (palm-mute, staccato, mano sulle corde): il picco degli ultimi 25 ms è oltre 20 dB sotto
            // quello dei 25 ms prima
            hist[(size_t) hpos] = fast; hpos = (hpos + 1) & 255;
            float recent = 0.0f, before = 0.0f;
            for (int k = 1; k <= histSpan; ++k) recent = std::max (recent, hist[(size_t) ((hpos - k) & 255)]);
            for (int k = histSpan + 1; k <= 2 * histSpan; ++k) before = std::max (before, hist[(size_t) ((hpos - k) & 255)]);
            collapsed = ! lowest && before > closeThr * 2.0f && recent < before * 0.1f;
            if (collapsed && ! stopped) { stopped = true; open = false; reopen = fast * 2.0f; }   // stop: chiude subito, resta agganciato fino alla plettrata successiva
            const bool periodic = ! stopped && eLf > 2.0f * eHf && eLf > closeThr;   // fondamentali che suonano: coda musicale
            relCur = stopped ? relFast : (periodic ? relSlow : relMid);
        }
        const float target = open ? 1.0f : 0.0f;
        gain += (target > gain ? atk : relCur) * (target - gain);
        return gain;
    }
};

// Soft clipper simmetrico ADAA del finale (headroom che cala con il sag)
struct PowerClip
{
    float xPrev = 0;
    static inline float f (float x) noexcept { return sp::fastTanh (x); }
    static inline float F (float x) noexcept { return logCosh (x); }
    inline float process (float x, float head) noexcept
    {
        const float xs = x / head;
        const float y = adaaD (xs, xPrev, [] (double v) { return std::tanh (v); }, [] (double v) { return logCoshD (v); });
        xPrev = xs;
        return y * head;
    }
    void reset() noexcept { xPrev = 0; }
    // uscita statica per un ingresso costante (per togliere l'offset dello sbilanciamento del push-pull)
    static inline float offset (float x, float head) noexcept { return std::tanh (x / head) * head; }   // come il ramo esatto dell'ADAA
};

// McLeod Pitch Method sul segnale decimato: NSDF, primo massimo sopra 0,8 × il massimo globale, parabola.
inline float mpmFrequency (const float* x, int n, double fsDec, int maxLag, float& clarity) noexcept
{
    float best = 0; int bestLag = 0;
    // NSDF per i lag 16..maxLag (il minimo lag = 1318 Hz a fs/8)
    static thread_local std::array<float, 1024> nsdf;
    maxLag = std::min (maxLag, (int) nsdf.size() - 1);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double acf = 0, m = 0;
        for (int i = 0; i + lag < n; ++i) { acf += (double) x[i] * x[i + lag]; m += (double) x[i] * x[i] + (double) x[i + lag] * x[i + lag]; }
        nsdf[(size_t) lag] = m > 1e-12 ? (float) (2.0 * acf / m) : 0.0f;
    }
    // picchi: primo massimo locale positivo dopo il primo attraversamento negativo
    float globalMax = 0; int lag = 2;
    while (lag < maxLag && nsdf[(size_t) lag] > 0) ++lag;   // esce dal lobo centrale
    std::array<std::pair<int, float>, 64> peaks; int np = 0;
    for (; lag < maxLag && np < 64; ++lag)
    {
        if (nsdf[(size_t) lag] > 0 && nsdf[(size_t) lag] >= nsdf[(size_t) lag - 1] && nsdf[(size_t) lag] > nsdf[(size_t) lag + 1])
        {
            peaks[(size_t) np++] = { lag, nsdf[(size_t) lag] };
            globalMax = std::max (globalMax, nsdf[(size_t) lag]);
        }
    }
    clarity = 0;
    if (np == 0 || globalMax < 0.3f) return 0.0f;
    for (int k = 0; k < np; ++k) if (peaks[(size_t) k].second >= 0.8f * globalMax) { bestLag = peaks[(size_t) k].first; best = peaks[(size_t) k].second; break; }
    const float y0 = nsdf[(size_t) bestLag - 1], y1 = nsdf[(size_t) bestLag], y2 = nsdf[(size_t) bestLag + 1];
    const float den = y0 - 2 * y1 + y2;
    const float lagF = (float) bestLag + (std::abs (den) > 1e-12f ? 0.5f * (y0 - y2) / den : 0.0f);
    clarity = best;
    return (float) (fsDec / lagF);
}

} // namespace amp

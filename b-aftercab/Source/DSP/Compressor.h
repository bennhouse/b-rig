#pragma once
// Compressore per chitarra "dopo il cab": feed-forward, ginocchio morbido,
// rilevatore di picco collegato (stessa riduzione su tutti i canali, l'immagine
// stereo non si sposta), autogain statico (make-up fisso da preset e soglia). Nessuna latenza (niente look-ahead).
//
// Tre preset, perché tempi e rapporto cambiano con lo scopo:
//   rhythm - incollare: 2,5:1, attacco rapido (3 ms), rilascio breve (80 ms).
//            La distorsione comprime già: qui si livellano palm mute e accenti
//            senza togliere il "chug".
//   lead   - sustain: 4:1, attacco 12 ms (passa la plettrata), rilascio 150 ms.
//            Le note lunghe restano in piedi e i bending non saltano fuori.
//   clean  - controllo: 5:1, attacco 8 ms, rilascio 220 ms, ginocchio più morbido.
//            I puliti hanno la dinamica più ampia e ne hanno più bisogno.

#include <cmath>
#include <algorithm>

namespace sp
{

struct CompPreset
{
    double ratio, attackMs, releaseMs, kneeDb, thresholdDb;
};

class Compressor
{
public:
    static const CompPreset& preset (int m)
    {
        static const CompPreset table[3] = {
            //  ratio  att   rel    knee  thr
            { 2.5,  3.0,  80.0, 6.0, -12.0 },   // rhythm
            { 4.0, 12.0, 150.0, 6.0, -18.0 },   // lead
            { 5.0,  8.0, 220.0, 8.0, -24.0 },   // clean
        };
        return table[m < 0 ? 0 : (m > 2 ? 2 : m)];
    }

    static constexpr double kMakeupRefDb = -10.0;   // livello tipico di una chitarra dopo il cab
    static constexpr int kStep = 8;                  // il guadagno si calcola ogni 8 campioni (0,17 ms a 48 kHz)
                                                     // e si interpola linearmente: niente log/pow per campione

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        mode = -1; setMode (0);
        makeupCoef = (float) std::exp (-(double) kStep / (0.05 * sr));   // cambi di preset/soglia senza scatti
        reset();
    }

    void reset()
    {
        grSmoothed = 0.0f; env = 0.0f; gPrev = -1.0f;
        makeupLin = autoGain ? staticMakeupLin() : 1.0f;
    }

    void setMode (int m)
    {
        if (m == mode) return;
        mode = m;
        const auto& p = preset (m);
        ratio = p.ratio;
        knee = p.kneeDb;
        attCoef = (float) std::exp (-(double) kStep / (p.attackMs * 0.001 * sr));
        relCoef = (float) std::exp (-(double) kStep / (p.releaseMs * 0.001 * sr));
        detCoef = (float) std::exp (-1.0 / (0.025 * sr));   // inviluppo di picco: niente ondulazione a frequenza audio
        updateMakeup();
    }

    void setThreshold (double db)
    {
        if (std::abs (db - threshold) < 1.0e-6) return;
        threshold = db;
        updateMakeup();
    }

    // Autogain (non dinamico): un guadagno fisso calcolato da preset e soglia, che riporta un
    // segnale tipico (-10 dBFS) al volume che aveva prima della compressione. Spento: nessun make-up.
    void setAutoGain (bool on) noexcept { autoGain = on; }

    // Riduzione statica (dB, positiva) per un livello d'ingresso in dB.
    double staticGr (double x) const
    {
        const double over = x - threshold;
        double y;
        if (2.0 * over < -knee)             y = x;
        else if (2.0 * std::abs (over) <= knee) y = x + (1.0 / ratio - 1.0) * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
        else                                y = threshold + over / ratio;
        return x - y;
    }

    // Applica la compressione (con dissolvenza "gate" 0..1 per on/off senza click) a tutti i canali.
    // Restituisce la riduzione massima del blocco (dB).
    float process (float* const* ch, int numCh, int n, const float* gate)
    {
        float maxGr = 0.0f;
        for (int start = 0; start < n; start += kStep)
        {
            const int m = std::min (kStep, n - start);

            // 1) rilevatore di picco per campione
            for (int i = start; i < start + m; ++i)
            {
                float peak = 0.0f;
                for (int c = 0; c < numCh; ++c) peak = std::max (peak, std::abs (ch[c][i]));
                env = std::max (peak, env * detCoef);   // segue i picchi, scende piano tra un ciclo e l'altro
            }

            // 2) riduzione: zero (e nessun logaritmo) finché l'inviluppo resta sotto il ginocchio
            float gr = 0.0f;
            if (env > kneeStartLin) gr = (float) staticGr (20.0 * std::log10 ((double) env));
            const float a = gr > grSmoothed ? attCoef : relCoef;
            grSmoothed = a * grSmoothed + (1.0f - a) * gr;
            if (grSmoothed < 1.0e-6f) grSmoothed = 0.0f;
            const float gComp = grSmoothed > 0.0f ? std::exp2 (-grSmoothed * kLog2Of10Over20) : 1.0f;

            // 3) make-up fisso (autogain) o nessuno
            const float target = autoGain ? staticMakeupLin() : 1.0f;
            makeupLin = makeupCoef * makeupLin + (1.0f - makeupCoef) * target;

            // 4) guadagno interpolato linearmente dal tratto precedente
            const float g = gComp * makeupLin;
            if (gPrev < 0.0f) gPrev = g;
            const float dg = (g - gPrev) / (float) m;
            float gi = gPrev;
            for (int i = start; i < start + m; ++i)
            {
                gi += dg;
                const float applied = 1.0f + gate[i] * (gi - 1.0f);
                for (int c = 0; c < numCh; ++c) ch[c][i] *= applied;
            }
            gPrev = g;
            maxGr = std::max (maxGr, grSmoothed * gate[start + m - 1]);
        }
        if (env < 1.0e-9f) env = 0.0f;
        return maxGr;
    }

private:
    static constexpr float kLog2Of10Over20 = 0.16609640474f;   // log2(10) / 20: dB -> esponente in base 2
    float staticMakeupLin() const { return std::exp2 (makeup * kLog2Of10Over20); }
    void updateMakeup()
    {
        makeup = (float) staticGr (kMakeupRefDb);                               // stesso volume a -10 dBFS
        kneeStartLin = (float) std::pow (10.0, (threshold - knee * 0.5) / 20.0); // sotto: riduzione nulla
    }

    double sr = 48000.0, ratio = 2.5, knee = 6.0, threshold = -12.0;
    int mode = -1;
    bool autoGain = false;
    float attCoef = 0.0f, relCoef = 0.0f, detCoef = 0.0f, grSmoothed = 0.0f, makeup = 0.0f, env = 0.0f;
    float kneeStartLin = 0.0f, gPrev = -1.0f, makeupLin = 1.0f, makeupCoef = 0.0f;
};

} // namespace sp

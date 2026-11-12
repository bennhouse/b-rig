#pragma once
// b-focus: de-noiser per tracce DI di chitarra e basso.
// Latenza zero, nessuna FFT, nessuna allocazione; uscita identica all'ingresso (bit per bit) quando è aperto.
//
// PERCORSO AUDIO — 4 bande ottenute con 3 shelf TPT in cascata (passa-basso a 2 poli, Q 0,5: due TPT del
// 1° ordine) più un guadagno finale:
//     lp = TPT(TPT(x));   y = lp + m * (x - lp)     (m = 1 -> y = x esattamente)
//     shelf A 2800 Hz (mA), shelf B 900 Hz (mB), shelf C 300 Hz (mC), guadagno D (mD, tutto lo spettro)
// guadagno di ogni banda = prodotto dei moltiplicatori sopra di essa:
//     banda 1  fizz / air        > 2,8 kHz      g1 = mA·mB·mC·mD   fino a -42 dB
//     banda 2  upper-mid buzz    0,9-2,8 kHz    g2 = mB·mC·mD      fino a -42 dB
//     banda 3  midrange hash     300-900 Hz     g3 = mC·mD         fino a -36 dB (85 % della riduzione)
//     banda 4  low / fondamentali < 300 Hz      g4 = mD            fino a -48 dB, solo quando le corde tacciono
// (bande 1-3: una banda alta non è mai più aperta di una più bassa, g1 <= g2 <= g3; se una banda alta ha segnale
//  quelle sotto si aprono almeno altrettanto e le armoniche vere non si perdono; la banda 4 chiude da sola)
//
// PERCORSO DI CONTROLLO (dall'ingresso, collegato su tutti i canali, aggiornato ogni 8 campioni):
//   - crossover a 2 poli (SVF) 2800 / 900 / 300 Hz sul sidechain: un inviluppo di picco per banda,
//     confrontato con la soglia della banda thr[k] = threshold + offset[k] (offset dal noise profile del learn);
//   - "shutup": finché il corpo (bande 3+4) suona sopra la sua soglia la banda 4 resta aperta e le bande 1-3
//     si aprono e chiudono ognuna con la propria energia (fruscio tolto anche SOTTO la nota); quando le corde
//     tacciono (o vengono stoppate) chiudono tutte e 4, su tutto lo spettro, con il rilascio adattivo (15 ms);
//   - expander 1:10 con ginocchio di 6 dB per banda (isteresi 4 dB, tenuta 20 ms), riduzione limitata da Strength;
//   - guardia dei transienti: inviluppo veloce contro lento sulle alte; oltre 4 dB di differenza la riduzione
//     viene tolta in proporzione (ghost note, legati, tapping anche sotto soglia);
//   - i moltiplicatori hanno uno smoothing per campione (apertura 0,1 ms, chiusura 1 ms).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace sp
{

// Filtro TPT (zero-delay feedback) del 1° ordine.
struct Tpt1
{
    float G = 0.0f, z = 0.0f;
    void setup (double fc, double fs) noexcept
    {
        const double g = std::tan (3.14159265358979323846 * fc / fs);
        G = (float) (g / (1.0 + g));
    }
    void reset() noexcept { z = 0.0f; }
    inline float lp (float x) noexcept
    {
        const float v = G * (x - z);
        const float y = v + z;
        z = y + v;
        return y;
    }
    inline float hp (float x) noexcept { return x - lp (x); }
};

// Logaritmo ed esponenziale veloci per il percorso di controllo (per campione).
// Errore: log2 < 0,0002 (0,001 dB), exp2 < 2e-6 relativo. Il percorso audio non li usa:
// a filtro aperto l'uscita resta identica all'ingresso bit per bit.
inline float fastLog2 (float x) noexcept
{
    std::uint32_t bits;
    std::memcpy (&bits, &x, 4);
    const float e = (float) ((int) ((bits >> 23) & 255u) - 127);
    bits = (bits & 0x007fffffu) | 0x3f800000u;   // mantissa in [1, 2)
    float m;
    std::memcpy (&m, &bits, 4);
    const float t = (m - 1.0f) / (m + 1.0f);    // log2(m) = 2/ln2 * atanh(t)
    const float t2 = t * t;
    return e + t * (2.8853900f + t2 * (0.9617967f + t2 * (0.5770780f + t2 * 0.4121520f)));
}

inline float fastExp2 (float x) noexcept
{
    x = std::max (-126.0f, std::min (126.0f, x));
    const float fi = std::floor (x);
    const float f = x - fi;                      // [0, 1)
    const float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    const std::uint32_t bits = (std::uint32_t) ((int) fi + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, 4);
    return p * scale;
}

class FocusEngine
{
public:
    static constexpr int kMaxChannels = 2, kBands = 4, kCtl = 8;
    static constexpr double kSplitHz[3] = { 2800.0, 900.0, 300.0 };             // shelf A, B, C
    static constexpr float kBandFloorDb[kBands] = { 42.0f, 42.0f, 36.0f, 48.0f }; // riduzione massima (strength 100 %)
    static constexpr float kBandShare[kBands] = { 1.0f, 1.0f, 0.85f, 1.0f };
    static constexpr float kDefaultOffset[kBands] = { 0.0f, -8.0f, -10.0f, -6.0f };   // DI tipica, senza learn
    static constexpr float kMinReleaseMs = 2.0f, kFastReleaseMs = 15.0f, kHystDb = 4.0f;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        for (auto& ch : stages)
            for (int k = 0; k < 3; ++k) for (auto& t : ch[(size_t) k]) t.setup (kSplitHz[k], fs);
        for (int k = 0; k < 3; ++k) xo[(size_t) k].set (fs, kSplitHz[k]);
        auto coef = [this] (double ms) { return (float) std::exp (-1.0 / (ms * 0.001 * fs)); };
        hfAtk = coef (0.2);  hfRel = coef (hfRelMs);
        fastAtk = coef (0.1); fastRel = coef (12.0);
        slowAtk = coef (20.0); slowRel = coef (120.0);
        lfRel = coef (8.0);
        holdSamples[2] = (int) (0.015 * fs);   // banda 3: un periodo a 70 Hz
        holdSamples[3] = (int) (0.035 * fs);   // banda 4: un periodo intero anche per il Si grave del basso (31 Hz)
        mUp = 1.0f - coef (0.1);
        mDown = 1.0f - coef (1.0);
        learnFrameLen = std::max (1, (int) (kLearnFrameSec * fs));
        toggleDecay = coef (1500.0);
        toggleDecay8 = std::pow (toggleDecay, (float) kCtl);
        stutterHoldSamples = (int) (3.0 * fs);
        holdCtl = (int) (0.020 * fs / kCtl);   // 20 ms in passi di controllo
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : stages) for (auto& st : ch) for (auto& t : st) t.reset();
        for (auto& f : xo) f.reset();
        env.fill (0.0f); hold.fill (0);
        eFast = eSlow = 0.0f;
        gr.fill (0.0f);
        m.fill (1.0f); mT.fill (1.0f);
        bandOpen.fill (false); bandHold.fill (0);
        relCoef = 0.0f;
        ctl = 0;
        learnState.store (LearnIdle);
    }

    // ------------------------------------------------------------------ Auto-Learn (Dual-Cluster, 4 bande)
    // 4,5 s in frame da 50 ms: per ogni frame il picco di ciascuna banda, il livello del corpo e l'attività
    // dei transienti (array fisso, nessuna allocazione). Cluster A = corde ferme (solo rumore), B = note.
    // Dal cluster A il 95° percentile del rumore di ogni banda -> noise profile e offset delle soglie per banda
    // (un picco EMI sulle medie alza solo la soglia di quella banda); threshold tra rumore e nota più leggera.
    enum LearnState { LearnIdle = 0, LearnListening, LearnDone, LearnFailed };
    static constexpr float kLearnSeconds = 4.5f, kLearnFrameSec = 0.050f;

    void requestLearn (bool start) noexcept { learnCmd.store (start ? 1 : 2); }
    bool isLearnPending() const noexcept { return learnCmd.load() == 1; }
    int getLearnState() const noexcept { return learnState.load(); }
    enum LearnFail { FailNone = 0, FailNoPause, FailNoSignal };
    int getLearnFailReason() const noexcept { return learnFail.load(); }
    float getLearnRemaining() const noexcept { return learnRemaining.load(); }
    void acknowledgeLearn() noexcept { learnState.store (LearnIdle); }
    struct LearnResult { float threshold, strength, release, lfGate, noisePeakHf, quietPlayHf, decayRate; bool played;
                         std::array<float, kBands> noiseDb, offsets; };
    LearnResult getLearnResult() const noexcept
    {
        LearnResult r { resThreshold.load(), resStrength.load(), resRelease.load(), resLfGate.load(),
                        resNoisePeak.load(), resQuiet.load(), resDecay.load(), resPlayed.load(), {}, {} };
        for (int k = 0; k < kBands; ++k) { r.noiseDb[(size_t) k] = resNoise[(size_t) k].load(); r.offsets[(size_t) k] = resOffset[(size_t) k].load(); }
        return r;
    }

    // Soglia del corpo (bande 3+4) per il rilascio adattivo e la banda 4 (dB). NaN = automatica.
    void setLfGate (float db) noexcept { lfGate = db; }
    float getLfGate() const noexcept { return lfGate; }
    // Offset delle soglie per banda (dB, rispetto a threshold) e noise profile da mostrare (dBFS, NaN = nessuno).
    void setBandOffsets (const std::array<float, kBands>& o) noexcept { for (int k = 0; k < kBands; ++k) offset[(size_t) k].store (o[(size_t) k]); }
    float getBandOffset (int k) const noexcept { return offset[(size_t) k].load(); }

    void setParameters (float thresholdDb, float strength01, float releaseMs) noexcept
    {
        threshold = thresholdDb;
        strength = std::clamp (strength01, 0.0f, 1.0f);
        userReleaseMs = std::clamp (releaseMs, kMinReleaseMs, 1500.0f);
        holdCtl = (int) (std::min (20.0f, userReleaseMs) * 0.001f * (float) fs / (float) kCtl);   // tenuta: 20 ms (o il rilascio, se più breve)
        const float detMs = std::min (10.0f, userReleaseMs);
        if (std::abs (detMs - hfRelMs) > 1.0e-3f)
        {
            hfRelMs = detMs;
            hfRel = (float) std::exp (-1.0 / ((double) detMs * 0.001 * fs));
        }
    }

    // Elabora in place n campioni su numCh canali (1 o 2). Rilevamento collegato tra i canali.
    void process (float* const* ch, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, kMaxChannels);
        if (const int cmd = learnCmd.exchange (0); cmd == 1) startLearn();
        else if (cmd == 2 && learnState.load() == LearnListening) learnState.store (LearnIdle);
        const bool learning = learnState.load() == LearnListening;
        std::array<float, kBands> thr {};
        for (int k = 0; k < kBands; ++k) thr[(size_t) k] = threshold + offset[(size_t) k].load (std::memory_order_relaxed);
        const float bodyGate = std::isnan (lfGate) ? thr[3] : lfGate;
        float blockMin = 0.0f, blockPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // --- sidechain: crossover a 2 poli, un inviluppo per banda
            float sc = ch[0][i];
            blockPeak = std::max (blockPeak, std::abs (sc));
            if (numCh > 1) sc = 0.5f * (sc + ch[1][i]);
            xo[0].tick (sc);           const float b1 = xo[0].hp;   // > 2800
            xo[1].tick (xo[0].lp);     const float b2 = xo[1].hp;   // 900-2800
            xo[2].tick (xo[1].lp);     const float b3 = xo[2].hp, b4 = xo[2].lp;   // 300-900, < 300
            const float a1 = std::abs (b1), a2 = std::abs (b2), a3 = std::abs (b3), a4 = std::abs (b4);
            env[0] = follow (env[0], a1, hfAtk, hfRel);
            env[1] = follow (env[1], a2, hfAtk, hfRel);
            followHold (2, a3);
            followHold (3, a4);
            const float hfg = a1 + a2;   // guardia dei transienti sulle alte
            eFast = follow (eFast, hfg, fastAtk, fastRel);
            eSlow = follow (eSlow, hfg, slowAtk, slowRel);

            float deltaT = 0.0f;
            if (learning)
            {
                fB[0] = std::max (fB[0], a1); fB[1] = std::max (fB[1], a2); fB[2] = std::max (fB[2], a3); fB[3] = std::max (fB[3], a4);
                if (eSlow > 0.0f) deltaT = std::max (0.0f, fastDb (eFast / eSlow));
                fDt += deltaT;
                if (++learnPos >= learnFrameLen) endLearnFrame();
            }

            // --- control rate: soglie, "shutup", expander, rilascio adattivo -> moltiplicatori bersaglio
            if (ctl++ % kCtl == 0) control (thr, bodyGate);

            // moltiplicatori con smoothing per campione; 1.0 esatto quando aperto
            for (int k = 0; k < kBands; ++k)
            {
                const float tgt = mT[(size_t) k];
                float& mk = m[(size_t) k];
                if (! (mk < tgt) && ! (mk > tgt)) continue;   // già al bersaglio
                const bool opening = std::abs (tgt - 1.0f) < std::abs (mk - 1.0f);   // verso 1 = si apre (subito)
                mk += (opening ? mUp : mDown) * (tgt - mk);
                if (! (tgt < 1.0f) && ! (tgt > 1.0f) && std::abs (mk - 1.0f) < 0.001f) mk = 1.0f;   // aggancio a 1.0 esatto: bit per bit
            }

            // --- percorso audio: 3 shelf complementari in cascata + guadagno della banda 4
            for (int c = 0; c < numCh; ++c)
            {
                float x = ch[c][i];
                for (int k = 0; k < 3; ++k)
                {
                    auto& st = stages[(size_t) c][(size_t) k];
                    const float lp = st[1].lp (st[0].lp (x));   // passa-basso a 2 poli (Q 0,5): shelf a 12 dB/ott
                    const float mk = m[(size_t) k];
                    if (mk < 1.0f || mk > 1.0f) x = lp + mk * (x - lp);   // con mk == 1 x resta identico (bit per bit)
                }
                if (m[3] < 1.0f) x *= m[3];
                ch[c][i] = x;
            }
            blockMin = std::min (blockMin, std::min (std::min (gr[0], gr[1]), std::min (gr[2], gr[3])));
        }

        for (auto& chs : stages) for (auto& st : chs) for (auto& t : st) if (std::abs (t.z) < 1.0e-20f) t.z = 0.0f;
        for (auto& f : xo) f.flush();
        const bool silent = blockPeak < 1.0e-5f;   // a riposo i meter mostrano nessuna azione
        lastBlockGr.store (silent ? 0.0f : blockMin, std::memory_order_relaxed);
        for (int k = 0; k < kBands; ++k) lastM[(size_t) k].store (silent ? 1.0f : m[(size_t) k], std::memory_order_relaxed);
        stuttering.store (stutterHold > 0, std::memory_order_relaxed);
    }

    // Per l'interfaccia (thread-safe).
    bool isStuttering() const noexcept { return stuttering.load (std::memory_order_relaxed); }
    float getGainReductionDb() const noexcept { return lastBlockGr.load (std::memory_order_relaxed); }
    float getMultiplier (int k) const noexcept { return lastM[(size_t) k].load (std::memory_order_relaxed); }   // 0..2 shelf A-C, 3 = banda 4
    float getCurrentGr() const noexcept { return gr[0]; }        // solo per i test (stesso thread): banda 1

    // Riduzione statica dell'expander per un livello (dB) rispetto alla sua soglia: 1:10, ginocchio 6 dB
    // (quasi un gate: il rumore appena 4-5 dB sotto soglia va già al pavimento della banda; con un ampli ad alto
    // guadagno dopo, un rumore "mezzo ridotto" verrebbe rialzato di 50-60 dB e si sentirebbe).
    static float expanderOver (float over) noexcept
    {
        constexpr float ratio = 10.0f, knee = 6.0f;
        if (over >= knee * 0.5f) return 0.0f;
        if (over <= -knee * 0.5f) return (ratio - 1.0f) * over;
        const float t = over - knee * 0.5f;
        return -(ratio - 1.0f) * t * t / (2.0f * knee);
    }

private:
    void control (const std::array<float, kBands>& thr, float bodyGate) noexcept
    {
        const float bodyDb = fastDb (std::max (env[2], env[3]));
        const bool playing = bodyDb > bodyGate;
        // guardia dei transienti: plettrate e legati aprono anche sotto soglia (in proporzione al livello delle alte)
        float boost = 0.0f;
        if (eFast > 1.5848932f * eSlow && eSlow > 0.0f)
        {
            const float deltaT = fastDb (eFast / eSlow);
            const float weight = std::clamp ((fastDb (eFast) - (thr[0] - 24.0f)) / 24.0f, 0.0f, 1.0f);
            boost = 12.0f * (deltaT - 4.0f) * weight;
        }
        // rilascio adattivo: corde ferme -> chiusura rapida (anche di tutto lo spettro)
        const float sustain = std::clamp ((bodyDb - bodyGate) / 12.0f, 0.0f, 1.0f);
        lfSustain = sustain;
        const float fast = std::min (kFastReleaseMs, userReleaseMs);
        const float relMs = fast + sustain * (userReleaseMs - fast);
        relCoef = fastExp2 (-1.442695f * (float) kCtl / (relMs * 0.001f * (float) fs));

        for (int k = 0; k < kBands; ++k)
        {
            // isteresi (4 dB) e tenuta (20 ms) per banda: una coda che oscilla attorno alla soglia non fa aprire e chiudere
            // la banda di continuo (con un ampli ad alto guadagno dopo, quello "sfarfallio" si sente moltissimo)
            float over = k < 3 ? fastDb (env[(size_t) k]) - thr[(size_t) k] : bodyDb - bodyGate;
            auto& op = bandOpen[(size_t) k]; auto& hl = bandHold[(size_t) k];
            if (over > 0.0f) { op = true; hl = holdCtl; }
            else if (op && over < -kHystDb) { if (hl > 0) --hl; else op = false; }
            if (op) over = std::max (over + kHystDb, 0.0f);   // aperta: chiude solo 4 dB sotto la soglia
            float t = 0.0f;
            if (k < 3) t = kBandShare[k] * expanderOver (over);
            else if (! playing && ! op) t = expanderOver (over);   // banda 4: solo quando il corpo tace
            t = std::min (0.0f, t + boost);
            t = std::max (t, -kBandFloorDb[k] * strength);
            float& g = gr[(size_t) k];
            g = t >= g ? t : relCoef * g + (1.0f - relCoef) * t;   // si apre subito, si chiude col rilascio
        }

        // stutter: con le corde che suonano il filtro continua ad aprirsi e chiudersi
        {
            const bool wasOpen = gateOpen;
            if (gr[0] > -3.0f) gateOpen = true; else if (gr[0] < -12.0f) gateOpen = false;
            if (gateOpen != wasOpen && lfSustain > 0.5f) toggles += 1.0f;
            toggles *= toggleDecay8;
            if (toggles > 4.0f) stutterHold = stutterHoldSamples;
            else stutterHold = std::max (0, stutterHold - kCtl);
        }

        // guadagni per banda: le bande 1-3 monotone dall'alto (una banda alta non è mai più aperta di una più bassa);
        // la banda 4 è libera di chiudere più delle altre a corde ferme (lo shelf C allora rialza le bande sopra i
        // 300 Hz al loro livello). Poi i moltiplicatori della cascata: rapporti tra bande vicine.
        auto lin = [] (float db) { return db >= 0.0f ? 1.0f : fastExp2 (db * 0.16609640f); };
        const float e0 = lin (gr[0]);
        const float e1 = std::max (lin (gr[1]), e0), e2 = std::max (lin (gr[2]), e1), e3 = lin (gr[3]);
        mT[3] = e3;
        mT[2] = e2 >= e3 ? std::min (16.0f, e2 / e3) : e2 / e3;
        if (! (mT[2] < 1.0f) && ! (mT[2] > 1.0f)) mT[2] = 1.0f;
        mT[1] = e1 >= e2 ? 1.0f : e1 / e2;
        mT[0] = e0 >= e1 ? 1.0f : e0 / e1;
    }

    static inline float follow (float e, float x, float atk, float rel) noexcept
    {
        return x > e ? atk * (e - x) + x : rel * (e - x) + x;
    }
    // inviluppo con "peak hold" per le bande basse: niente ondulazione durante la nota, discesa rapida allo stop
    inline void followHold (int k, float a) noexcept
    {
        auto& e = env[(size_t) k]; auto& h = hold[(size_t) k];
        if (a >= e) { e = a; h = holdSamples[(size_t) k]; }
        else if (h > 0) --h;
        else e *= lfRel;
    }
    void startLearn() noexcept
    {
        learnPos = 0; learnFrames = 0;
        fB.fill (0.0f); fDt = 0.0f;
        learnFail.store (FailNone);
        learnRemaining.store (kLearnSeconds);
        learnState.store (LearnListening);
    }

    void endLearnFrame() noexcept
    {
        if (learnFrames < kMaxLearnFrames)
        {
            auto& f = frames[(size_t) learnFrames];
            for (int k = 0; k < kBands; ++k) f.b[(size_t) k] = toDb (fB[(size_t) k]);
            f.lf = std::max (f.b[2], f.b[3]);   // corpo: bande 3 + 4
            f.hf = f.b[0];
            f.dt = fDt / (float) learnFrameLen;
        }
        fB.fill (0.0f); fDt = 0.0f;
        learnPos = 0;
        ++learnFrames;
        learnRemaining.store (std::max (0.0f, kLearnSeconds - (float) learnFrames * kLearnFrameSec));
        if ((float) learnFrames * kLearnFrameSec >= kLearnSeconds - 1.0e-4f) finishLearn();
    }

    void finishLearn() noexcept
    {
        const int n = std::min (learnFrames, kMaxLearnFrames);
        float minLf = 1.0e9f, maxLf = -1.0e9f, maxHf = -1.0e9f;
        bool anyAbsA = false;
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frames[(size_t) i];
            minLf = std::min (minLf, f.lf); maxLf = std::max (maxLf, f.lf); maxHf = std::max (maxHf, f.hf);
            anyAbsA = anyAbsA || f.lf < -58.0f;
        }
        if (n == 0 || maxHf < -130.0f) { fail (FailNoSignal); return; }

        auto inA = [&] (float lf) { return anyAbsA ? lf < -58.0f : lf <= minLf + 6.0f; };
        auto inB = [&] (float lf) { return anyAbsA ? lf >= -52.0f : lf >= minLf + 12.0f; };
        if (! anyAbsA && maxLf - minLf < 20.0f && minLf >= -52.0f) { fail (FailNoPause); return; }

        // cluster A: 95° percentile del rumore per ciascuna banda (noise profile) e pavimento del corpo (media)
        std::array<float, kMaxLearnFrames> tmp {};
        std::array<float, kBands> noise {};
        int nA = 0;
        float lfSum = 0.0f;
        for (int i = 0; i < n; ++i) if (inA (frames[(size_t) i].lf)) { ++nA; lfSum += frames[(size_t) i].lf; }
        if (nA == 0) { fail (FailNoPause); return; }
        const int k95 = std::min (nA - 1, (int) std::floor (0.95f * (float) nA));
        for (int k = 0; k < kBands; ++k)
        {
            int c = 0;
            for (int i = 0; i < n; ++i) if (inA (frames[(size_t) i].lf)) tmp[(size_t) c++] = frames[(size_t) i].b[(size_t) k];
            std::nth_element (tmp.begin(), tmp.begin() + k95, tmp.begin() + nA);
            noise[(size_t) k] = tmp[(size_t) k95];
        }
        const float noisePeak = noise[0];
        const float noiseFloorLf = lfSum / (float) nA;

        // cluster B: la nota più leggera (alte) e la velocità di decadimento del sustain
        float quietPlay = std::numeric_limits<float>::infinity(), decaySum = 0.0f;
        int nDecay = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frames[(size_t) i];
            if (! inB (f.lf)) continue;
            if (f.lf > noiseFloorLf + 10.0f) quietPlay = std::min (quietPlay, f.hf);
            if (i > 0 && inB (frames[(size_t) i - 1].lf) && f.lf < frames[(size_t) i - 1].lf && f.dt < 2.0f)
            {
                decaySum += (frames[(size_t) i - 1].lf - f.lf) / kLearnFrameSec;
                ++nDecay;
            }
        }
        const bool played = std::isfinite (quietPlay);
        float thr = noisePeak + 6.0f;
        if (played)
        {
            thr = std::min (noisePeak + 6.0f, quietPlay - 3.0f);
            thr = std::max (thr, noisePeak + 2.0f);
        }
        const float needed = std::max (0.0f, noisePeak + 74.0f);
        const float decay = nDecay > 0 ? std::clamp (decaySum / (float) nDecay, 6.0f, 60.0f) : 20.0f;
        resThreshold.store (std::clamp (thr, -100.0f, -10.0f));
        resStrength.store (std::clamp (needed / kBandFloorDb[0] * 100.0f, 25.0f, 100.0f));
        resRelease.store (std::clamp (4.5f / decay * 1000.0f, 80.0f, 450.0f));
        resLfGate.store (std::clamp (std::max (noise[2], noise[3]) + 6.0f, -100.0f, -35.0f));
        for (int k = 0; k < kBands; ++k)
        {
            resNoise[(size_t) k].store (noise[(size_t) k]);
            resOffset[(size_t) k].store (k == 0 ? 0.0f : std::clamp (noise[(size_t) k] - noisePeak, -40.0f, 30.0f));
        }
        resNoisePeak.store (noisePeak);
        resQuiet.store (played ? quietPlay : 0.0f);
        resDecay.store (decay);
        resPlayed.store (played);
        learnState.store (LearnDone);
    }

    void fail (LearnFail why) noexcept
    {
        learnFail.store (why);
        learnState.store (LearnFailed);
    }

    static inline float toDb (float v) noexcept { return 20.0f * std::log10 (v + 1.0e-9f); }
    static inline float fastDb (float v) noexcept { return 6.0205999f * fastLog2 (v + 1.0e-9f); }

    // crossover del sidechain: SVF TPT a 2 poli (Q 0,707)
    struct Xo
    {
        float a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f, ic1 = 0, ic2 = 0, lp = 0, hp = 0;
        void set (double rate, double f) noexcept
        {
            const float g = (float) std::tan (3.14159265358979323846 * f / rate);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        inline void tick (float x) noexcept
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            lp = v2; hp = x - k * v1 - v2;
        }
        void reset() noexcept { ic1 = ic2 = lp = hp = 0.0f; }
        void flush() noexcept { if (std::abs (ic1) < 1.0e-20f) ic1 = 0.0f; if (std::abs (ic2) < 1.0e-20f) ic2 = 0.0f; }
    };

    double fs = 48000.0;
    std::array<std::array<std::array<Tpt1, 2>, 3>, kMaxChannels> stages {};
    std::array<Xo, 3> xo {};
    float hfAtk = 0, hfRel = 0, fastAtk = 0, fastRel = 0, slowAtk = 0, slowRel = 0, lfRel = 0;
    std::array<float, kBands> env {};
    std::array<int, kBands> hold {}, holdSamples {};
    float mUp = 1, mDown = 1, relCoef = 0;
    float eFast = 0, eSlow = 0;
    std::array<float, kBands> gr {}, m { 1.0f, 1.0f, 1.0f, 1.0f }, mT { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<bool, kBands> bandOpen {}; std::array<int, kBands> bandHold {}; int holdCtl = 120;
    std::array<std::atomic<float>, kBands> offset { kDefaultOffset[0], kDefaultOffset[1], kDefaultOffset[2], kDefaultOffset[3] };
    float threshold = -85.0f, strength = 1.0f, userReleaseMs = 180.0f, hfRelMs = 10.0f;
    unsigned ctl = 0;
    float lfGate = std::numeric_limits<float>::quiet_NaN();

    // Auto-Learn
    struct LearnFrame { float lf = -180.0f, hf = -180.0f, dt = 0.0f; std::array<float, kBands> b { -180.0f, -180.0f, -180.0f, -180.0f }; };
    static constexpr int kMaxLearnFrames = 96;   // 4,5 s / 50 ms = 90 frame
    std::array<LearnFrame, kMaxLearnFrames> frames {};
    std::atomic<int> learnCmd { 0 }, learnState { LearnIdle }, learnFail { FailNone };
    std::atomic<float> learnRemaining { 0.0f };
    int learnFrameLen = 2400, learnPos = 0, learnFrames = 0;
    std::array<float, kBands> fB {};
    float fDt = 0;
    std::atomic<float> resThreshold { 0 }, resStrength { 0 }, resRelease { 0 }, resLfGate { 0 },
                       resNoisePeak { 0 }, resQuiet { 0 }, resDecay { 0 };
    std::array<std::atomic<float>, kBands> resNoise {}, resOffset {};
    std::atomic<bool> resPlayed { false };
    std::atomic<float> lastBlockGr { 0.0f };
    std::atomic<bool> stuttering { false };
    bool gateOpen = true;
    float toggles = 0.0f, toggleDecay = 0.9999f, lfSustain = 0.0f;
    float toggleDecay8 = 0.999f;
    int stutterHold = 0, stutterHoldSamples = 144000;
    std::array<std::atomic<float>, kBands> lastM { 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace sp

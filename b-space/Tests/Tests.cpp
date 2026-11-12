// Test di b-space: latenza zero (dist comp), ritardi dalla geometria, direzione (ITD/livello), direttività,
// pareti e coda, stabilità, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
template <typename F> juce::AudioBuffer<float> runWith (std::initializer_list<std::pair<const char*, float>> params, int len, F sig)
{
    auto p = make<BSpaceProcessor> (1, 2);
    for (auto& [id, v] : params) setP (p->apvts, id, v);
    p->prepareToPlay (sr, block);
    return run (*p, len, sig);
}
int firstAbove (const juce::AudioBuffer<float>& b, int ch, float thr, int from = 0)
{
    for (int i = from; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (ch, i)) > thr) return i;
    return -1;
}
BSpaceProcessor::Params defaults()
{
    BSpaceProcessor p; return p.currentParams();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto impulse = [] (int i) { return i == 0 ? 1.0f : 0.0f; };
    // 1. dist comp acceso: il diretto esce al campione 0 (latenza zero)
    {
        auto y = runWith ({}, 4800, impulse);
        check (std::abs (y.getSample (0, 0)) > 0.3f && std::abs (y.getSample (1, 0)) > 0.3f, "dist comp: il suono diretto esce senza ritardo", y.getSample (0, 0));
    }
    // 2. dist comp spento: il diretto arriva dopo d0 / 343 m/s; la prima riflessione dove dice la geometria
    {
        auto prm = defaults(); prm.distComp = false;
        const auto g = BSpaceProcessor::geometry (prm);
        auto y = runWith ({ { BSpaceIDs::distComp, 0.0f } }, 9600, impulse);
        const int got = firstAbove (y, 0, 0.05f), want = (int) std::round (g.paths[0].delayS * sr);
        std::printf ("       diretto a %.2f m: atteso campione %d, misurato %d\n", g.paths[0].dist, want, got);
        check (std::abs (got - want) <= 2, "dist comp spento: ritardo del diretto = distanza / velocità del suono", got - want);
        prm.distComp = true;
        const auto h = BSpaceProcessor::geometry (prm);
        float firstRefl = 1.0f; int which = 1;
        for (int k = 1; k < BSpaceProcessor::kTaps; ++k) if (h.paths[(size_t) k].delayS < firstRefl) { firstRefl = h.paths[(size_t) k].delayS; which = k; }
        auto z = runWith ({ { BSpaceIDs::walls, 3.0f } }, 9600, impulse);
        const int at = (int) std::round (firstRefl * sr);
        // dopo il diretto (che si spegne nel passa-basso entro pochi campioni) la prima riflessione
        const int seen = firstAbove (z, 0, 0.02f, 40);
        std::printf ("       prima riflessione (percorso %d, 5 = soffitto): attesa +%d campioni, vista a %d\n", which, at, seen);
        check (std::abs (seen - at) <= 30, "prima riflessione al ritardo relativo (d - d0) della geometria", seen - at);
    }
    // 3. ampli a destra dell'ascoltatore: il destro arriva prima ed è più forte
    {
        auto y = runWith ({ { BSpaceIDs::srcX, 0.95f }, { BSpaceIDs::srcY, 0.6f }, { BSpaceIDs::lisX, 0.3f }, { BSpaceIDs::lisY, 0.6f } }, 4800, impulse);
        const int l = firstAbove (y, 0, 0.05f), r = firstAbove (y, 1, 0.05f);
        const double lv = rmsDb (y, 0, 0, 200), rv = rmsDb (y, 1, 0, 200);
        std::printf ("       ampli a destra: sinistro al campione %d (%.1f dB), destro al %d (%.1f dB)\n", l, lv, r, rv);
        check (r < l && rv > lv + 1.0, "direzione: ITD e livello verso l'orecchio vicino", l - r);
    }
    // 4. direttività: ampli girato di spalle (180°), le alte del diretto scendono
    {
        auto hf = [&] (float angle)   // solo il suono diretto: risposta all'impulso nei primi 128 campioni (prima di ogni riflessione)
        {
            auto y = runWith ({ { BSpaceIDs::angle, angle } }, 4800, impulse);
            return toneDb (y, 0, 0, 128, 8000.0);
        };
        const double on = hf (0.0f), back = hf (180.0f);
        std::printf ("       8 kHz: in asse %.1f dB, di spalle %.1f dB\n", on, back);
        check (on - back > 6.0, "direttività del cono: di spalle l'ampli perde le alte", on - back);
    }
    // 5. pareti: cemento lascia una coda molto più lunga del trattato; tutto si spegne (FDN stabile)
    {
        auto tail = [&] (float walls, float space)
        {
            auto p = make<BSpaceProcessor> (1, 2);
            setP (p->apvts, BSpaceIDs::walls, walls);
            if (space > 0) p->applySpace ((int) space);
            p->prepareToPlay (sr, block);
            return run (*p, 6 * 48000, [] (int i) { return i < 480 ? (float) (0.5 * std::sin (tp * 440.0 * i / sr)) : 0.0f; });
        };
        auto t = tail (0.0f, 0.0f), c = tail (3.0f, 0.0f), h = tail (3.0f, 3.0f);
        const double tr = rmsDb (t, 0, 9600, 28800), co = rmsDb (c, 0, 9600, 28800), end = rmsDb (h, 0, 5 * 48000, 6 * 48000);
        std::printf ("       coda 200-600 ms: treated %.1f dB, concrete %.1f dB; hall in cemento dopo 5 s %.1f dB\n", tr, co, end);
        check (co - tr > 6.0, "pareti: il cemento suona molto più del trattato", co - tr);
        check (end < -70.0, "la coda si spegne (rete di ritardi stabile)", end);
    }
    // 6. livello: un suono lungo esce a un volume simile all'ingresso (entro ±6 dB) in studio
    {
        auto y = runWith ({}, 48000, [] (int i) { return pluck (i, 110.0, 0.5); });
        const double in = 20 * std::log10 (0.3 / std::sqrt (2.0)) - 3.0, out = rmsDb (y, 0, 24000, 48000);
        std::printf ("       livello d'uscita %.1f dB (ingresso circa %.1f dB)\n", out, in);
        check (std::abs (out - in) < 6.0, "volume vicino a quello d'ingresso", out - in);
    }
    // 7. CPU
    {
        auto p = make<BSpaceProcessor> (1, 2);
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU: %.3f %% di un core\n", cpu);
        check (cpu < 1.5, "CPU sotto l'1,5 % di un core", cpu);
    }
    // 8. interfaccia
    {
        auto p = make<BSpaceProcessor> (1, 2);
        setP (p->apvts, BSpaceIDs::angle, 20.0f);
        snapshot (*p, "b-space.png");
    }
    return finish();
}

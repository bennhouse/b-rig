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
        prm.room = 2;
        const auto hg = BSpaceProcessor::geometry (prm);
        float firstRefl = 1.0f; int which = 1;
        for (int k = 1; k < BSpaceProcessor::kTaps; ++k) if (hg.paths[(size_t) k].delayS < firstRefl) { firstRefl = hg.paths[(size_t) k].delayS; which = k; }
        juce::ignoreUnused (h);
        auto z = runWith ({ { BSpaceIDs::room, 2.0f } }, 9600, impulse);   // garage: cemento
        const int at = (int) std::round (firstRefl * sr);
        // dopo il diretto (che si spegne nel passa-basso entro pochi campioni) la prima riflessione
        const int seen = firstAbove (z, 0, 0.02f, 40);
        std::printf ("       prima riflessione (percorso %d, 5 = soffitto, 6 = pavimento): attesa +%d campioni, vista a %d\n", which, at, seen);
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
    // 5. stanze: il garage in cemento lascia una coda molto più lunga dello studio trattato; tutto si spegne (FDN stabile)
    {
        auto tail = [&] (int room)
        {
            auto p = make<BSpaceProcessor> (1, 2);
            setP (p->apvts, BSpaceIDs::room, (float) room);
            p->applyRoom (room);
            p->prepareToPlay (sr, block);
            return run (*p, 6 * 48000, [] (int i) { return i < 480 ? (float) (0.5 * std::sin (tp * 440.0 * i / sr)) : 0.0f; });
        };
        auto t = tail (0), c = tail (2), h = tail (5);
        const double tr = rmsDb (t, 0, 9600, 28800), co = rmsDb (c, 0, 9600, 28800), end = rmsDb (h, 0, 5 * 48000, 6 * 48000);
        std::printf ("       coda 200-600 ms: studio %.1f dB, garage %.1f dB; chiesa dopo 5 s %.1f dB\n", tr, co, end);
        check (co - tr > 6.0, "stanze: il garage in cemento suona molto più dello studio trattato", co - tr);
        check (end < -70.0, "la coda si spegne (rete di ritardi stabile)", end);
    }
    // 5b. materiali: le prime riflessioni dello studio (pareti trattate) sono più scure di quelle del garage
    {
        auto hfRatio = [&] (int room)
        {
            auto p = make<BSpaceProcessor> (1, 2);
            setP (p->apvts, BSpaceIDs::room, (float) room);
            p->prepareToPlay (sr, block);
            auto y = run (*p, 4800, impulse);
            // energia sopra i 4 kHz rispetto a quella sotto 1 kHz, nei rimbalzi (dopo il diretto, prima della coda piena)
            juce::IIRFilter hp, lp;
            hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 4000.0)); lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 1000.0));
            double eh = 1e-20, el = 1e-20;
            for (int i = 0; i < 1500; ++i)
            {
                const float v = i < 60 ? 0.0f : y.getSample (0, i);
                const float h = hp.processSingleSampleRaw (v), l = lp.processSingleSampleRaw (v);
                eh += (double) h * h; el += (double) l * l;
            }
            return 10.0 * std::log10 (eh / el);
        };
        const double studio = hfRatio (0), garage = hfRatio (2);
        std::printf ("       rimbalzi, energia sopra 4 kHz rispetto a sotto 1 kHz: studio %.1f dB, garage %.1f dB\n", studio, garage);
        check (garage - studio > 3.0, "materiali: il trattato si mangia le alte dei rimbalzi, il cemento no", garage - studio);
    }
    // 5c. ampli contro una parete: più bassi (80 Hz) rispetto al centro della stanza
    {
        auto low = [&] (float sy)
        {
            auto p = make<BSpaceProcessor> (1, 2);
            setP (p->apvts, BSpaceIDs::room, 2.0f); setP (p->apvts, BSpaceIDs::srcY, sy); setP (p->apvts, BSpaceIDs::distComp, 1.0f);
            p->prepareToPlay (sr, block);
            auto y = run (*p, 24000, [] (int i) { return (float) (0.2 * std::sin (tp * 80.0 * i / sr) + 0.2 * std::sin (tp * 2000.0 * i / sr)); });
            return toneDb (y, 0, 12000, 24000, 80.0) - toneDb (y, 0, 12000, 24000, 2000.0);
        };
        const double wall = low (0.03f), mid = low (0.45f);
        std::printf ("       80 Hz rispetto a 2 kHz: contro la parete %.1f dB, a metà stanza %.1f dB\n", wall, mid);
        check (wall - mid > 2.0, "ampli contro la parete: più bassi", wall - mid);
    }
    // 5d. mix: a 0 esce il segnale che entra, identico
    {
        juce::Random r (3);
        std::vector<float> in (9600); for (auto& v : in) v = 0.3f * (r.nextFloat() - 0.5f);
        auto y = runWith ({ { BSpaceIDs::mix, 0.0f } }, 9600, [&] (int i) { return in[(size_t) i]; });
        float d = 0; for (int i = 0; i < 9600; ++i) d = juce::jmax (d, std::abs (y.getSample (0, i) - in[(size_t) i]), std::abs (y.getSample (1, i) - in[(size_t) i]));
        check (d < 1.0e-6f, "mix 0: esce il segnale d'ingresso", d);
    }
    // 5e. progetti di prima: "space" garage diventa la stanza garage
    {
        auto p = make<BSpaceProcessor> (1, 2);
        juce::ValueTree t ("BSpace");
        juce::ValueTree a ("PARAM"); a.setProperty ("id", "space", nullptr); a.setProperty ("value", 1.0f, nullptr); t.appendChild (a, nullptr);
        juce::ValueTree b ("PARAM"); b.setProperty ("id", "walls", nullptr); b.setProperty ("value", 3.0f, nullptr); t.appendChild (b, nullptr);
        juce::MemoryBlock mb; if (auto x = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*x, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        check ((int) p->apvts.getRawParameterValue (BSpaceIDs::room)->load() == 2, "progetto di prima: space garage -> stanza garage");
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

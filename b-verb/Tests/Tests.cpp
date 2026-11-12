// Test di b-verb: RT60 misurato, predelay, mix 0 = dry, freeze, stabilità di tutti i tipi, shimmer, duck, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
std::unique_ptr<BVerbProcessor> mk (std::initializer_list<std::pair<const char*, float>> params)
{
    auto p = make<BVerbProcessor> (2, 2);
    setP (p->apvts, BVerbIDs::mix, 100.0f);
    for (auto& [id, v] : params) setP (p->apvts, id, v);
    p->prepareToPlay (sr, block);
    return p;
}
// RT60 dalla pendenza (integrale di Schroeder, da -5 a -25 dB) nella banda attorno a hz
double rt60 (const juce::AudioBuffer<float>& y, double hz = 500.0)
{
    sp::Bq bp; bp.set (sp::Bq::BP, sr, hz, 2.0);
    std::vector<double> e ((size_t) y.getNumSamples());
    for (int i = 0; i < y.getNumSamples(); ++i) { const double v = bp.process (y.getSample (0, i)); e[(size_t) i] = v * v; }
    for (int i = (int) e.size() - 2; i >= 0; --i) e[(size_t) i] += e[(size_t) i + 1];
    const double e0 = e[0];
    int i5 = -1, i25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10 * std::log10 (e[i] / e0 + 1e-30);
        if (i5 < 0 && db < -5) i5 = (int) i;
        if (i25 < 0 && db < -25) { i25 = (int) i; break; }
    }
    return i5 < 0 || i25 < 0 ? 0.0 : 3.0 * (i25 - i5) / sr;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto impulse = [] (int i) { return i == 0 ? 0.5f : 0.0f; };
    // 1. RT60 misurato contro "decay" (hall, senza modulazione)
    for (float d : { 0.8f, 2.4f, 6.0f })
    {
        auto p = mk ({ { BVerbIDs::decay, d }, { BVerbIDs::mod, 0.0f }, { BVerbIDs::early, 0.0f }, { BVerbIDs::predelay, 0.0f } });
        auto y = run (*p, (int) (sr * (d * 1.6 + 0.5)), impulse);
        const double t = rt60 (y);
        std::printf ("       decay %.1f s: RT60 misurato %.2f s\n", d, t);
        check (std::abs (t - d) < 0.05 * d, "RT60 entro il 5 % di decay", t / d);
    }
    // 1b. le alte durano quanto dice il grafico (stessa formula del loop), più corte del corpo
    {
        auto p = mk ({ { BVerbIDs::decay, 3.0f }, { BVerbIDs::mod, 0.0f }, { BVerbIDs::early, 0.0f }, { BVerbIDs::predelay, 0.0f },
                       { BVerbIDs::damp, 3000.0f }, { BVerbIDs::hicut, 20000.0f } });
        auto y = run (*p, (int) (sr * 5), impulse);
        const double hi = rt60 (y, 6000.0), want = BVerbProcessor::rt60At (6000.0, 2, 0.65f, 3.0, 3000.0, sr);
        std::printf ("       damp 3 kHz: RT60 a 6 kHz misurato %.2f s, previsto %.2f s (corpo 3.00 s)\n", hi, want);
        check (std::abs (hi - want) < 0.12 * want && hi < 2.2, "damp: le alte decadono come previsto, prima del corpo", hi / want);
    }
    // 2. predelay: niente wet prima del predelay
    {
        auto p = mk ({ { BVerbIDs::predelay, 50.0f } });
        auto y = run (*p, 9600, impulse);
        int first = -1; for (int i = 0; i < 9600; ++i) if (std::abs (y.getSample (0, i)) > 1e-4f) { first = i; break; }
        std::printf ("       predelay 50 ms: primo suono al campione %d (2400 atteso)\n", first);
        check (first >= 2390 && first < 2400 + 480, "predelay: il wet parte dopo 50 ms", first);
    }
    // 3. mix 0: il dry passa identico
    {
        auto p = mk ({ { BVerbIDs::mix, 0.0f } });
        auto y = run (*p, 24000, [] (int i) { return pluck (i, 110.0); });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - pluck (i, 110.0)));
        check (d < 1e-6f, "mix 0: uscita identica all'ingresso", d);
    }
    // 4. freeze: la coda resta (5 s dopo, entro 3 dB)
    {
        auto p = mk ({ { BVerbIDs::freeze, 1.0f }, { BVerbIDs::early, 0.0f } });
        setP (p->apvts, BVerbIDs::freeze, 0.0f);
        p->prepareToPlay (sr, block);
        auto y0 = run (*p, 4800, [] (int i) { return pluck (i, 220.0); });
        setP (p->apvts, BVerbIDs::freeze, 1.0f);
        auto y = run (*p, (int) (sr * 6), [] (int) { return 0.0f; });
        const double a = rmsDb (y, 0, (int) (sr * 0.5), (int) sr), b = rmsDb (y, 0, (int) (sr * 5), (int) (sr * 6));
        std::printf ("       freeze: %.1f dB a 0,5 s, %.1f dB a 5 s\n", a, b);
        check (a > -60.0 && std::abs (a - b) < 3.0, "freeze: la coda resta sospesa", b - a);
    }
    // 5. tutti i tipi a decay 20 s, mod al massimo: stabile, finito
    for (int t = 0; t < 5; ++t)
    {
        auto p = mk ({ { BVerbIDs::type, (float) t }, { BVerbIDs::decay, 20.0f }, { BVerbIDs::mod, 100.0f } });
        auto y = run (*p, (int) (sr * 4), [] (int i) { return i < 48000 ? pluck (i, 110.0) : 0.0f; });
        float pk = 0; bool fin = true;
        for (int i = 0; i < y.getNumSamples(); ++i) { fin &= std::isfinite (y.getSample (0, i)); pk = std::max (pk, std::abs (y.getSample (0, i))); }
        const double late = rmsDb (y, 0, (int) (sr * 3), (int) (sr * 4)), mid = rmsDb (y, 0, (int) (sr * 1.5), (int) (sr * 2.5));
        std::printf ("       tipo %d: picco %.2f, 1,5-2,5 s %.1f dB, 3-4 s %.1f dB\n", t, pk, mid, late);
        check (fin && pk < 4.0f && late < mid + 0.5, "decay 20 s: stabile, non cresce", late - mid);
    }
    // 6. shimmer: nella coda compare l'ottava sopra
    {
        auto shim = mk ({ { BVerbIDs::type, 4.0f }, { BVerbIDs::early, 0.0f } });
        auto hall = mk ({ { BVerbIDs::type, 2.0f }, { BVerbIDs::early, 0.0f } });
        auto sine = [] (int i) { return i < 24000 ? (float) (0.3 * std::sin (tp * 330.0 * i / sr)) : 0.0f; };
        auto a = run (*shim, (int) (sr * 2), sine), b = run (*hall, (int) (sr * 2), sine);
        const double sa = toneDb (a, 0, 48000, 96000, 660.0) - toneDb (a, 0, 48000, 96000, 330.0);
        const double sb = toneDb (b, 0, 48000, 96000, 660.0) - toneDb (b, 0, 48000, 96000, 330.0);
        std::printf ("       ottava nella coda: shimmer %+.1f dB, hall %+.1f dB (rispetto alla nota)\n", sa, sb);
        check (sa > sb + 15.0, "shimmer: +1 ottava nella coda", sa - sb);
    }
    // 7. duck: con il dry forte il wet scende
    {
        auto lvl = [&] (float duck)
        {
            auto p = mk ({ { BVerbIDs::duck, duck }, { BVerbIDs::mix, 50.0f } });
            auto y = run (*p, (int) sr, [] (int i) { return (float) (0.5 * std::sin (tp * 220.0 * i / sr)); });
            return rmsDb (y, 0, 36000, 48000);
        };
        const double off = lvl (0.0f), on = lvl (100.0f);
        std::printf ("       mentre suoni: duck 0 %% %.1f dB, 100 %% %.1f dB\n", off, on);
        check (off - on > 0.5, "duck: il riverbero scende mentre il dry è forte", off - on);
    }
    // 8. CPU
    {
        auto p = mk ({ { BVerbIDs::type, 4.0f } });
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU (shimmer): %.3f %% di un core\n", cpu);
        check (cpu < 2.5, "CPU sotto il 2,5 % di un core", cpu);
    }
    // 9. interfaccia
    {
        auto p = mk ({ { BVerbIDs::mix, 25.0f } });
        run (*p, 48000, [] (int i) { return pluck (i % 24000, 220.0); });
        snapshot (*p, "b-verb.png");
    }
    return finish();
}

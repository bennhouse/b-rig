// Test di b-scenery: silenzio senza play né chitarra, texture con la chitarra, react, balance, lo-fi, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        auto p = make<BSceneryProcessor> (2, 2);
        auto y = run (*p, 48000, [] (int) { return 0.0f; });
        check (rmsDb (y, 0, 0, 48000) < -200.0, "nessun play e nessuna chitarra: silenzio assoluto", rmsDb (y, 0, 0, 48000));
    }
    {
        auto texDb = [] (float react, float amp)
        {
            auto p = make<BSceneryProcessor> (2, 2);
            setP (p->apvts, BSceneryIDs::react, react); setP (p->apvts, BSceneryIDs::mix, 0.0f);
            auto y = run (*p, 48000, [amp] (int i) { return amp * (float) std::sin (tp * 110.0 * i / sr); });
            // la texture è quello che resta togliendo la chitarra
            double e = 0; for (int i = 24000; i < 48000; ++i) { const double d = y.getSample (1, i) - amp * std::sin (tp * 110.0 * i / sr); e += d * d; }
            return 10 * std::log10 (e / 24000 + 1e-30);
        };
        const double quietNoReact = texDb (0, 0.01f), loudReact = texDb (100, 0.3f), quietReact = texDb (100, 0.01f);
        std::printf ("       texture: chitarra piano %.1f dBFS; con react, chitarra forte %.1f / piano %.1f dBFS\n", quietNoReact, loudReact, quietReact);
        check (quietNoReact > -60.0, "con la chitarra viva le texture suonano", quietNoReact);
        check (loudReact < quietReact - 10.0, "react: le texture si abbassano quando suoni forte", quietReact - loudReact);
    }
    {
        auto p = make<BSceneryProcessor> (2, 2);
        for (int l = 0; l < 3; ++l) setP (p->apvts, BSceneryIDs::on (l).toRawUTF8(), 1);
        setP (p->apvts, BSceneryIDs::react, 0);
        p->toggleBalance();
        auto sig = [] (int i) { return 0.2f * (float) std::sin (tp * 196.0 * i / sr); };
        run (*p, (int) (3.0 * sr), sig);
        p->applyBalanceIfReady();
        auto y = run (*p, 48000, sig);
        double e = 0, eg = 0;
        for (int i = 12000; i < 48000; ++i) { const double s = sig (i); const double d = y.getSample (1, i) - s; e += d * d; eg += s * s; }
        const double rel = 10 * std::log10 (e / eg);
        std::printf ("       balance: texture %.1f dB rispetto alla chitarra (voluto -16)\n", rel);
        check (std::abs (rel + 16.0) < 2.0, "balance: texture a -16 dB sotto la chitarra", rel);
    }
    {
        auto p = make<BSceneryProcessor> (2, 2);
        setP (p->apvts, BSceneryIDs::on (0).toRawUTF8(), 0); setP (p->apvts, BSceneryIDs::on (3).toRawUTF8(), 1); setP (p->apvts, BSceneryIDs::level (3).toRawUTF8(), 100);
        auto y = run (*p, 48000, [] (int i) { return 0.2f * (float) std::sin (tp * 8000.0 * i / sr); });
        check (rmsDb (y, 0, 24000, 48000) < 10 * std::log10 (0.02) - 8.0, "lo-fi: le alte (8 kHz) vengono tagliate (con il ripiegamento del decimatore)", rmsDb (y, 0, 24000, 48000));
    }
    auto p = make<BSceneryProcessor> (2, 2);
    for (int l = 0; l < 4; ++l) setP (p->apvts, BSceneryIDs::on (l).toRawUTF8(), 1);
    const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
    check (cpu < 3.0, "CPU % di un core (4 scenari)", cpu);
    setP (p->apvts, BSceneryIDs::on (3).toRawUTF8(), 0);
    snapshot (*p, "gui_bscenery.png", [&] { run (*p, (int) (4 * sr), [] (int i) { return pluck (i, 110.0, 1.0); }); });
    return finish();
}

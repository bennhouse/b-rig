// Test di b-comp: curva statica (soglia, ratio, ginocchio), sotto soglia identico, attack e release, tutti gli stili,
// multibanda piatto quando non comprime, parallel, auto gain, CPU, snapshot (con larghezze fisse).
#include "PluginEditor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
std::unique_ptr<BCompProcessor> mk (std::initializer_list<std::pair<const char*, float>> params)
{
    auto p = make<BCompProcessor> (1, 1);
    setP (p->apvts, BCompIDs::autoGain, 0.0f);
    for (auto& [id, v] : params) setP (p->apvts, id, v);
    p->prepareToPlay (sr, block);
    return p;
}
auto sine (double f, double a) { return [f, a] (int i) { return (float) (a * std::sin (tp * f * i / sr)); }; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. curva statica: soglia -20, ratio 4, ginocchio 0; un seno a -6 dBFS di picco perde (14 dB sopra) × 3/4 = 10,5 dB
    {
        auto p = mk ({ { BCompIDs::threshold, -20.0f }, { BCompIDs::ratio, 4.0f }, { BCompIDs::knee, 0.0f }, { BCompIDs::scHp, 20.0f } });
        const double a = std::pow (10.0, -6.0 / 20.0);
        auto y = run (*p, 48000, sine (1000.0, a));
        const double d = rmsDb (y, 0, 24000, 48000) - 20.0 * std::log10 (a / std::sqrt (2.0));
        std::printf ("       soglia -20, ratio 4, seno a -6 dBFS: %.2f dB (attesi -10.5)\n", d);
        check (std::abs (d + 10.5) < 0.6, "curva statica: riduzione di 10,5 dB", d);
        check (std::abs (BCompProcessor::gainDb (-6.0f, -20.0f, 4.0f, 0.0f) + 10.5f) < 1.0e-4f, "gain computer: 10,5 dB");
        check (std::abs (BCompProcessor::gainDb (-20.0f, -20.0f, 4.0f, 6.0f) + 0.5625f) < 1.0e-3f, "ginocchio morbido: a metà del ginocchio un quarto della pendenza");
    }
    // 2. sotto la soglia il segnale passa identico
    {
        auto p = mk ({ { BCompIDs::threshold, -20.0f }, { BCompIDs::knee, 0.0f } });
        auto y = run (*p, 24000, sine (440.0, 0.03));
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - sine (440.0, 0.03) (i)));
        check (d < 1.0e-5f, "sotto soglia: uscita identica", d);
    }
    // 3. attack e release: un gradino da -40 a -6 dBFS, la riduzione arriva al 63 % in circa il tempo di attack
    {
        auto p = mk ({ { BCompIDs::threshold, -20.0f }, { BCompIDs::ratio, 10.0f }, { BCompIDs::knee, 0.0f }, { BCompIDs::attack, 10.0f },
                       { BCompIDs::release, 100.0f }, { BCompIDs::scHp, 20.0f } });
        const int on = 24000, off = 72000;
        juce::AudioBuffer<float> y = run (*p, 120000, [&] (int i) { const double a = (i >= on && i < off) ? 0.5 : 0.01; return (float) (a * std::sin (tp * 1000.0 * i / sr)); });
        auto grAt = [&] (int i) { return 20.0 * std::log10 (std::abs (y.getMagnitude (0, i, 96)) / (i >= on && i < off ? 0.5 : 0.01) + 1e-9); };
        const double full = grAt (off - 200);
        int atk = -1; for (int i = on; i < off; i += 24) if (grAt (i) < 0.63 * full) { atk = i - on; break; }
        int rel = -1; for (int i = off + 96; i < 120000; i += 24) if (grAt (i) > 0.37 * full) { rel = i - off; break; }
        std::printf ("       riduzione a regime %.1f dB, 63 %% dopo %.1f ms, rilascio al 63 %% dopo %.1f ms\n", full, atk * 1000.0 / sr, rel * 1000.0 / sr);
        check (atk > 0 && atk * 1000.0 / sr > 4.0 && atk * 1000.0 / sr < 20.0, "attack: circa 10 ms", atk * 1000.0 / sr);
        check (rel > 0 && rel * 1000.0 / sr > 50.0 && rel * 1000.0 / sr < 200.0, "release: circa 100 ms", rel * 1000.0 / sr);
    }
    // 4. tutti gli stili comprimono una plettrata forte, senza NaN né picchi oltre l'ingresso
    for (int st = 0; st < BCompProcessor::kStyles; ++st)
    {
        auto p = mk ({ { BCompIDs::style, (float) st } });
        for (const char* id : { BCompIDs::threshold, BCompIDs::ratio, BCompIDs::attack, BCompIDs::release, BCompIDs::knee })
            setP (p->apvts, id, BCompProcessor::styleDefault (id, st));
        auto y = run (*p, 96000, [] (int i) { return pluck (i, 110.0, 0.5, 0.5); });
        bool finite = true; for (int i = 0; i < 96000; ++i) finite &= std::isfinite (y.getSample (0, i));
        const double d = rmsDb (y, 0, 0, 96000) - [] { juce::AudioBuffer<float> b (1, 96000); for (int i = 0; i < 96000; ++i) b.setSample (0, i, pluck (i, 110.0, 0.5, 0.5)); return rmsDb (b, 0, 0, 96000); }();
        const char* names[] = { "clean", "opto", "fet", "squash", "multi" };
        std::printf ("       %s: %.1f dB rispetto all'ingresso (senza make-up)\n", names[st], d);
        check (finite && d < -2.0, (juce::String ("stile ") + names[st] + ": comprime").toRawUTF8(), d);
    }
    // 5. multibanda: senza compressione (soglia a 0 dB) le tre bande sommate restano piatte (entro 0,5 dB)
    {
        double worst = 0;
        for (double f : { 60.0, 250.0, 800.0, 2500.0, 6000.0, 12000.0 })
        {
            auto p = mk ({ { BCompIDs::style, 4.0f }, { BCompIDs::threshold, 0.0f }, { BCompIDs::knee, 0.0f } });
            auto y = run (*p, 48000, sine (f, 0.1));
            worst = std::max (worst, std::abs (rmsDb (y, 0, 24000, 48000) - 20.0 * std::log10 (0.1 / std::sqrt (2.0))));
        }
        std::printf ("       multibanda senza compressione: scarto massimo %.2f dB (60 Hz - 12 kHz)\n", worst);
        check (worst < 0.5, "multibanda: le bande sommate sono piatte", worst);
    }
    // 6. parallel: con mix al 50 % la riduzione è circa metà (in ampiezza)
    {
        auto full = mk ({ { BCompIDs::threshold, -30.0f }, { BCompIDs::ratio, 10.0f }, { BCompIDs::knee, 0.0f }, { BCompIDs::scHp, 20.0f } });
        auto half = mk ({ { BCompIDs::threshold, -30.0f }, { BCompIDs::ratio, 10.0f }, { BCompIDs::knee, 0.0f }, { BCompIDs::scHp, 20.0f }, { BCompIDs::mixOn, 1.0f }, { BCompIDs::mix, 50.0f } });
        auto a = run (*full, 48000, sine (1000.0, 0.5)), b = run (*half, 48000, sine (1000.0, 0.5));
        const double la = std::pow (10.0, rmsDb (a, 0, 24000, 48000) / 20.0), lb = std::pow (10.0, rmsDb (b, 0, 24000, 48000) / 20.0), dry = 0.5 / std::sqrt (2.0);
        check (std::abs (lb - 0.5 * (la + dry)) < 0.02 * dry, "parallel: 50 % di asciutto e 50 % di compresso", lb / dry);
    }
    // 7. auto gain: una chitarra con picchi a -10 dBFS esce a un volume vicino all'ingresso (entro 3 dB)
    {
        auto p = make<BCompProcessor> (1, 1);
        p->prepareToPlay (sr, block);
        auto y = run (*p, 96000, [] (int i) { return pluck (i % 24000, 110.0, 0.5, 0.316); });
        juce::AudioBuffer<float> x (1, 96000); for (int i = 0; i < 96000; ++i) x.setSample (0, i, pluck (i % 24000, 110.0, 0.5, 0.316));
        const double d = rmsDb (y, 0, 24000, 96000) - rmsDb (x, 0, 24000, 96000);
        std::printf ("       auto gain (clean): uscita %+.1f dB rispetto all'ingresso, make-up %.1f dB\n", d, p->autoMakeupDb());
        check (std::abs (d) < 3.0, "auto gain: volume vicino a quello d'ingresso", d);
    }
    // 8. CPU (multi, lo stile più pesante)
    {
        auto p = make<BCompProcessor> (2, 2);
        setP (p->apvts, BCompIDs::style, 4.0f);
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU (multi, stereo): %.3f %% di un core\n", cpu);
        check (cpu < 2.0, "CPU sotto il 2 % di un core", cpu);
    }
    // 9. interfaccia: clean e multi
    {
        auto p = make<BCompProcessor> (1, 1);
        run (*p, 48000, [] (int i) { return pluck (i % 12000, 110.0, 0.5, 0.5); });
        snapshot (*p, "gui_bcomp.png");
        auto q = make<BCompProcessor> (1, 1);
        setP (q->apvts, BCompIDs::style, 4.0f);
        run (*q, 48000, [] (int i) { return pluck (i % 12000, 110.0, 0.5, 0.5); });
        snapshot (*q, "gui_bcomp_multi.png");
    }
    return finish();
}

// Test di b-starve: tutto spento = identico, effetti attivi senza NaN, mosh senza click, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto sig = [] (int i) { return pluck (i, 110.0, 0.4); };
    {
        auto p = make<BStarveProcessor> (2, 2);
        auto y = run (*p, 24000, sig);   // di default tutti e 4 gli effetti sono spenti
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - sig (i)));
        check (d == 0.0f, "tutto spento: uscita identica all'ingresso", d);
    }
    for (int e = 0; e < 4; ++e)
    {
        auto p = make<BStarveProcessor> (2, 2);
        for (int k = 0; k < 4; ++k) setP (p->apvts, BStarveIDs::on (k).toRawUTF8(), k == e ? 1.0f : 0.0f);
        setP (p->apvts, BStarveIDs::intensity (e).toRawUTF8(), 80); p->prepareToPlay (sr, block);
        auto y = run (*p, 96000, sig);
        bool finite = true; double diff = 0;
        for (int i = 0; i < 96000; ++i) { finite &= std::isfinite (y.getSample (0, i)); diff += std::abs (y.getSample (0, i) - sig (i)); }
        check (finite && diff > 1.0, (juce::String ("effetto ") + BStarveIDs::fx[e] + ": agisce, uscita valida").toRawUTF8(), diff);
        if (e == 1)   // mosh: nessun salto più grande di una sinusoide pulita (fade coseno di 64 campioni)
        {
            auto q = make<BStarveProcessor> (2, 2);
            for (int k = 0; k < 4; ++k) setP (q->apvts, BStarveIDs::on (k).toRawUTF8(), k == 1 ? 1.0f : 0.0f);
            setP (q->apvts, BStarveIDs::intensity (1).toRawUTF8(), 100); q->prepareToPlay (sr, block);
            auto z = run (*q, 96000, [] (int i) { return 0.3f * (float) std::sin (tp * 220.0 * i / sr); });
            float jump = 0; for (int i = 1; i < 96000; ++i) jump = std::max (jump, std::abs (z.getSample (0, i) - z.getSample (0, i - 1)));
            std::printf ("       mosh, salto massimo tra due campioni: %.4f (sinusoide pulita: 0.0086)\n", jump);
            check (jump < 0.05f, "mosh: nessun click (salto massimo tra campioni)", jump);
        }
    }
    // prima accensione: il volume dell'effetto si calibra da solo (RMS di uscita = RMS di ingresso), una volta sola
    {
        auto q = make<BStarveProcessor> (2, 2);
        run (*q, 4800, sig);
        setP (q->apvts, BStarveIDs::on (0).toRawUTF8(), 1); setP (q->apvts, BStarveIDs::intensity (0).toRawUTF8(), 60);
        auto lvl = [] (int i) { return 0.25f * (float) std::sin (tp * 110.0 * i / sr) + 0.1f * (float) std::sin (tp * 330.0 * i / sr); };
        run (*q, 48000, lvl);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        auto* v = q->apvts.getParameter (BStarveIDs::volume (0).toRawUTF8());
        const float volDb = v->convertFrom0to1 (v->getValue());
        auto y = run (*q, 48000, lvl);
        const double d = rmsDb (y, 0, 24000, 48000) - rmsDb (run (*make<BStarveProcessor> (2, 2), 48000, lvl), 0, 24000, 48000);
        std::printf ("       starve alla prima accensione: volume %.1f dB, uscita - ingresso %.2f dB\n", volDb, d);
        check (std::abs (volDb) > 0.5f && std::abs (d) < 2.0, "prima accensione: volume calibrato, livello pareggiato (dB)", d);
        setP (q->apvts, BStarveIDs::volume (0).toRawUTF8(), 0); setP (q->apvts, BStarveIDs::on (0).toRawUTF8(), 0);
        run (*q, 4800, lvl);
        setP (q->apvts, BStarveIDs::on (0).toRawUTF8(), 1);
        run (*q, 48000, lvl);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (std::abs (v->convertFrom0to1 (v->getValue())) < 0.01f, "riaccensione: nessuna nuova calibrazione (fino al ricaricamento)");
    }
    auto p = make<BStarveProcessor> (2, 2);
    for (int k = 0; k < 4; ++k) setP (p->apvts, BStarveIDs::on (k).toRawUTF8(), 1);
    const double cpu = cpuPercent (*p, sig);
    check (cpu < 3.0, "CPU % di un core (tutti attivi, stereo)", cpu);
    snapshot (*p, "gui_bstarve.png", [&] { run (*p, 24000, sig); });
    return finish();
}

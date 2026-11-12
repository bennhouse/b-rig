// Test di b-synthesia: audio identico bit per bit, descrittori (palm-mute, plettrata, larghezza), analisi dei picchi,
// export di un fotogramma PNG con alfa, CPU del thread audio, larghezze fisse e snapshot dei 4 modi e dei 4 sfondi.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "smp/TestKit.h"
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. pass-through bit per bit
    {
        auto p = make<BSynthesiaProcessor> (2, 2);
        auto y = run (*p, 48000, [] (int i) { return pluck (i, 110.0); });
        float d = 0; for (int i = 0; i < 48000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - pluck (i, 110.0)));
        check (d == 0.0f, "audio inalterato, bit per bit", d);
    }
    // 2. descrittori
    {
        auto p = make<BSynthesiaProcessor> (2, 2);
        run (*p, 24000, [] (int i) { return 0.4f * (float) std::sin (tp * 90.0 * i / sr); });
        const float subLow = p->subEnergy.load();
        run (*p, 24000, [] (int i) { return 0.4f * (float) std::sin (tp * 2000.0 * i / sr); });
        const float subHigh = p->subEnergy.load();
        check (subLow > 10.0f * subHigh, "sub_energy: reagisce ai bassi (palm-mute), non alle alte", subLow / (subHigh + 1e-9f));
        float hit = 0;
        auto q = make<BSynthesiaProcessor> (2, 2);
        juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < block; ++i) { const float v = pluck (k * block + i, 220.0, 0.5, 0.3); b.setSample (0, i, v); b.setSample (1, i, v); }
            q->processBlock (b, m); hit = std::max (hit, q->transientHit.load());
        }
        check (hit > 0.3f, "transient_hit: scatta sulle plettrate", hit);
        check (q->stereoWidth.load() > 0.99f, "stereo_width: mono = correlazione +1", q->stereoWidth.load());
    }
    // 3. picchi FFT, export PNG con alfa, snapshot
    auto p = make<BSynthesiaProcessor> (2, 2);
    const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
    check (cpu < 0.5, "CPU % di un core (thread audio)", cpu);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
        auto* se = dynamic_cast<BSynthesiaEditor*> (ed.get());
        run (*p, 8192, [] (int i) { return 0.3f * (float) (std::sin (tp * 220.0 * i / sr) + 0.5 * std::sin (tp * 660.0 * i / sr)); });
        se->frameForTest();
        bool f220 = false, f660 = false;
        for (int k = 0; k < se->state().numPeaks; ++k) { const float hz = se->state().peaks[(size_t) k].hz; f220 |= std::abs (hz - 220.0f) < 12.0f; f660 |= std::abs (hz - 660.0f) < 12.0f; }
        check (f220 && f660, "analisi: i picchi a 220 e 660 Hz", se->state().numPeaks);
        std::printf ("       nota dominante: %.1f Hz\n", se->state().domHz);
        p->editorBeingDeleted (ed.get());
    }
    const char* modes[5] = { "tunnel", "flow", "terrain", "prism", "attractor" };
    for (int mo = 0; mo < 5; ++mo)
    {
        setP (p->apvts, BSynIDs::mode, (float) mo);
        snapshot (*p, (juce::String ("gui_bsyn_") + modes[mo] + ".png").toRawUTF8(), [&] { run (*p, 24000, [] (int i) { return pluck (i, 82.4, 0.25, 0.4); }); });
    }
    setP (p->apvts, BSynIDs::mode, 2); setP (p->apvts, BSynIDs::bg, 1); setP (p->apvts, BSynIDs::aspect, 2);
    snapshot (*p, "gui_bsyn_green_916.png", [&] { run (*p, 24000, [] (int i) { return pluck (i, 82.4, 0.25, 0.4); }); });
    // export: un fotogramma renderizzato fuori schermo con sfondo alfa ha pixel trasparenti
    {
        setP (p->apvts, BSynIDs::bg, 3); setP (p->apvts, BSynIDs::mode, 1);
        VisualState vs; Canvas c (*p, vs);
        juce::Image img (juce::Image::ARGB, 320, 180, true);
        { juce::Graphics g (img); c.render (g, { 0, 0, 320, 180 }, true); }
        check (img.getPixelAt (160, 90).getAlpha() == 0 || img.getPixelAt (5, 90).getAlpha() == 0, "export con bg alpha: sfondo trasparente nel PNG");
    }
    return finish();
}

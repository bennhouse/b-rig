// Test di b-meter: trasparenza a 0 dB, loudness EBU (1 kHz a -20 dBFS = -20 LUFS), auto gain, viste, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        auto p = make<BMeterProcessor> (2, 2);
        auto y = run (*p, 24000, [] (int i) { return pluck (i, 110.0); });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - pluck (i, 110.0)));
        check (d == 0.0f, "gain 0 dB: uscita identica, latenza zero", d);
    }
    {
        auto p = make<BMeterProcessor> (2, 2);
        const float a = std::pow (10.0f, -20.0f / 20.0f);
        run (*p, (int) (4 * sr), [a] (int i) { return a * (float) std::sin (tp * 1000.0 * i / sr); });
        std::printf ("       1 kHz a -20 dBFS (stereo): momentary %.2f, short-term %.2f LUFS\n", p->momentary.load(), p->shortTerm.load());
        check (std::abs (p->momentary.load() + 20.0f) < 0.2f && std::abs (p->shortTerm.load() + 20.0f) < 0.2f, "loudness EBU R128: -20 LUFS (±0.2)", p->momentary.load());
        // auto: target social (-14 LUFS, TP -1): +6 dB
        p->toggleAuto();
        run (*p, (int) (6 * sr), [a] (int i) { return a * (float) std::sin (tp * 1000.0 * i / sr); });
        p->applyAutoIfReady();
        auto* gp = p->apvts.getParameter (BMeterIDs::gain);
        const float gdb = gp->convertFrom0to1 (gp->getValue());
        std::printf ("       auto (social): gain %.2f dB\n", gdb);
        check (std::abs (gdb - 6.0f) < 0.3f, "auto: gain che porta al target (-14 LUFS)", gdb);
        run (*p, (int) (4 * sr), [a] (int i) { return a * (float) std::sin (tp * 1000.0 * i / sr); });
        check (std::abs (p->shortTerm.load() + 14.0f) < 0.3f, "dopo auto: short-term sul target", p->shortTerm.load());
    }
    auto p = make<BMeterProcessor> (2, 2);
    const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
    check (cpu < 1.0, "CPU % di un core (thread audio)", cpu);
    for (int v = 0; v < 4; ++v)
    {
        auto q = make<BMeterProcessor> (2, 2);
        setP (q->apvts, BMeterIDs::view, (float) v);
        const char* files[4] = { "gui_bmeter_osc.png", "gui_bmeter_vec.png", "gui_bmeter_wave.png", "gui_bmeter_spec.png" };
        std::printf ("       vista %d\n", v); std::fflush (stdout);
        snapshot (*q, files[v], [&] { run (*q, (int) (3 * sr), [] (int i) { return pluck (i, 110.0); }); });
    }
    // riapertura della finestra sullo stesso processore (chiudi e riapri in DAW), anche cambiando vista
    {
        auto q = make<BMeterProcessor> (2, 2);
        for (int k = 0; k < 4; ++k)
        {
            setP (q->apvts, BMeterIDs::view, (float) (k % 4));
            std::printf ("       riapertura %d\n", k); std::fflush (stdout);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q->createEditorAndMakeActive());
            run (*q, 24000, [] (int i) { return pluck (i, 110.0); });
            for (int f = 0; f < 5; ++f) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            juce::String rep; ui::checkFixedValueBoxes (*ed, rep);
            q->editorBeingDeleted (ed.get());
        }
        check (true, "finestra chiusa e riaperta 4 volte sullo stesso processore");
    }
    return finish();
}

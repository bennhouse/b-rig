// Test di b-dynaverb: riff basso = trasparente, assolo alto = boost + delay, palm-mute chiude in fretta,
// la coda dell'assolo non viene tagliata, sync, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
float riff (int i) { return pluck (i, 82.4, 0.25, 0.4); }   // palm-mute sul Mi basso
float solo (int i) { return pluck (i, 659.3, 0.25, 0.3); }  // nota alta (Mi al 12° tasto del cantino)
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. riff sulle corde basse: uscita identica all'ingresso (canale lead spento)
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        auto y = run (*p, 96000, riff);
        float d = 0; for (int i = 0; i < 96000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - riff (i)));
        std::printf ("       riff: alpha %.3f, differenza massima %.2g\n", p->alphaNow.load(), d);
        check (d < 1.0e-6f && p->alphaNow.load() < 0.01f, "riff basso: il segnale passa identico", d);
    }
    // 2. assolo: alpha sale a 1, le medie a 1,1 kHz salgono e arriva l'eco
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        auto y = run (*p, 96000, solo);
        const double a = p->alphaNow.load();
        check (a > 0.95, "assolo alto: il canale lead si accende", a);
        auto q = make<BDynaverbProcessor> (1, 2);
        setP (q->apvts, BDynaverbIDs::space, 0.0f);
        auto z = run (*q, 96000, [] (int i) { return (float) (0.2 * std::sin (tp * 1100.0 * i / sr)) + (float) (0.1 * std::sin (tp * 660.0 * i / sr)); });
        const double g = toneDb (z, 0, 48000, 96000, 1100.0) - 20 * std::log10 (0.2);
        std::printf ("       1,1 kHz in modalità lead: %+.1f dB (boost 3,5 + uscita 1,5)\n", g);
        check (g > 4.0 && g < 5.5, "lead focus: +3,5 dB a 1,1 kHz e +1,5 dB d'uscita", g);
    }
    // 3. tempi: lead in ~25 ms, ritorno al riff più rapido (palm-mute subito dopo l'assolo)
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        int up = -1, down = -1;
        juce::AudioBuffer<float> b (2, 32); juce::MidiBuffer m;
        for (int pos = 0; pos < 4 * 48000; pos += 32)
        {
            for (int i = 0; i < 32; ++i) { const int k = pos + i; const float v = k < 48000 ? riff (k) : k < 2 * 48000 ? solo (k - 48000) : riff (k - 2 * 48000); b.setSample (0, i, v); b.setSample (1, i, v); }
            p->processBlock (b, m);
            if (up < 0 && pos >= 48000 && p->alphaNow.load() > 0.63f) up = pos - 48000;
            if (down < 0 && pos >= 2 * 48000 && p->alphaNow.load() < 0.37f) down = pos - 2 * 48000;
        }
        const double upMs = up * 1000.0 / sr, downMs = down * 1000.0 / sr;
        std::printf ("       verso il lead (63 %%) in %.1f ms, ritorno al riff (37 %%) in %.1f ms\n", upMs, downMs);
        check (up > 0 && down > 0 && upMs < 60.0 && downMs < upMs, "cambio rapido, ritorno al riff più veloce dell'apertura", downMs);
    }
    // 4. la coda dell'assolo non si taglia: dopo l'ultima nota alta, le ripetizioni continuano
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        setP (p->apvts, BDynaverbIDs::duck, 0.0f);
        auto y = run (*p, 2 * 48000, [] (int i) { return i < 48000 ? solo (i) : 0.0f; });
        const double tail = rmsDb (y, 0, 48000 + 9600, 48000 + 28800);
        std::printf ("       eco dopo la fine dell'assolo (200-600 ms): %.1f dB\n", tail);
        check (tail > -45.0, "la coda del delay sfuma da sola dopo l'assolo", tail);
    }
    // 5. sync: 380 ms a 120 bpm -> 3/8 (1/8 puntato) = 375 ms
    {
        int note = -1;
        const float ms = BDynaverbProcessor::syncedMs (380.0f, 120.0, &note);
        check (std::abs (ms - 375.0f) < 0.5f && note == 2, "sync: 380 ms a 120 bpm diventa 1/8 puntato (375 ms)", ms);
    }
    // 6. CPU
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        const double cpu = cpuPercent (*p, [] (int i) { return (i / 24000) % 2 ? solo (i) : riff (i); });
        std::printf ("       CPU: %.3f %% di un core\n", cpu);
        check (cpu < 1.0, "CPU sotto l'1 % di un core", cpu);
    }
    // 7. interfaccia
    {
        auto p = make<BDynaverbProcessor> (1, 2);
        run (*p, 48000, [] (int i) { return i < 30000 ? riff (i) : solo (i); });
        snapshot (*p, "b-dynaverb.png");
    }
    return finish();
}

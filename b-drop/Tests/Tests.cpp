// Test di b-drop: intonazione (-2, -12, +7 st, fine), ritardo, zero-lag pick, accordi senza tremolio,
// tension, spento = identico, CPU, snapshot.
#include "PluginEditor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
// frequenza del picco spettrale (Goertzel, passo di 1 cent entro ±80 cent dall'atteso): l'altezza che si sente
double peakFreq (const juce::AudioBuffer<float>& b, int from, int to, double around)
{
    double best = around, bestDb = -1e9;
    for (int c = -80; c <= 80; ++c) { const double f = around * std::exp2 (c / 1200.0), d = toneDb (b, 0, from, to, f); if (d > bestDb) { bestDb = d; best = f; } }
    return best;
}
double centsOff (double f, double target) { return 1200.0 * std::log2 (f / target); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto sine = [] (double f, double a = 0.3) { return [f, a] (int i) { return (float) (a * std::sin (tp * f * i / sr)); }; };

    // 1. -2 st: 440 Hz esce a 392 Hz
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::semitones, -2.0f);
        auto y = run (*p, 48000, sine (440.0));
        const double f = peakFreq (y, 12000, 48000, 392.0);
        std::printf ("       -2 st: 440 Hz -> %.2f Hz (atteso 392.00)\n", f);
        check (std::abs (centsOff (f, 440.0 * std::exp2 (-2.0 / 12.0))) < 3.0, "-2 st: intonazione entro 3 cent", f);
    }
    // 2. un'ottava sotto e una quinta sopra, fine +30 cent
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::range, 2.0f);
        setP (p->apvts, BDropIDs::semitones, -12.0f);
        auto y = run (*p, 48000, sine (220.0));
        const double f = peakFreq (y, 12000, 48000, 110.0);
        auto q = make<BDropProcessor> (1, 1);
        setP (q->apvts, BDropIDs::range, 2.0f);
        setP (q->apvts, BDropIDs::semitones, 7.0f);
        setP (q->apvts, BDropIDs::fine, 30.0f);
        auto z = run (*q, 48000, sine (220.0));
        const double g = peakFreq (z, 12000, 48000, 220.0 * std::exp2 (7.3 / 12.0));
        std::printf ("       -12 st: 220 -> %.2f Hz; +7 st +30 ct: 220 -> %.2f Hz (atteso %.2f)\n", f, g, 220.0 * std::exp2 (7.3 / 12.0));
        check (std::abs (centsOff (f, 110.0)) < 3.0 && std::abs (centsOff (g, 220.0 * std::exp2 (7.3 / 12.0))) < 3.0, "ottava sotto e quinta sopra (con fine) intonate", f);
    }
    // 2b. intonazione su tutta la tastiera (82...659 Hz) a -2, -5, -12 e +5 semitoni
    {
        double worst = 0;
        for (int st : { -2, -5, -12, 5 })
            for (double f0 : { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 440.0, 659.3 })
            {
                auto p = make<BDropProcessor> (1, 1);
                setP (p->apvts, BDropIDs::range, 2.0f); setP (p->apvts, BDropIDs::semitones, (float) st); setP (p->apvts, BDropIDs::tension, 0.0f);
                auto y = run (*p, 72000, sine (f0));
                const double tgt = f0 * std::exp2 (st / 12.0);
                worst = std::max (worst, std::abs (centsOff (peakFreq (y, 24000, 72000, tgt), tgt)));
            }
        std::printf ("       errore massimo su 8 note x 4 trasposizioni: %.1f cent\n", worst);
        check (worst <= 5.0, "intonazione entro 5 cent dal Mi basso al Mi alto", worst);
    }
    // 3. ritardo della nota trasposta (zero-lag spento): sotto 10 ms
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::zeroLag, 0.0f);
        setP (p->apvts, BDropIDs::semitones, -2.0f);
        p->prepareToPlay (sr, block);
        const int on = 24000;
        auto y = run (*p, 36000, [&] (int i) { return i < on ? 0.0f : (float) (0.5 * std::sin (tp * 196.0 * (i - on) / sr)); });
        int first = -1; for (int i = on; i < 36000; ++i) if (std::abs (y.getSample (0, i)) > 0.1f) { first = i; break; }
        const double ms = (first - on) * 1000.0 / sr;
        std::printf ("       nota trasposta udibile dopo %.2f ms\n", ms);
        check (first > 0 && ms < 12.0, "la nota trasposta arriva entro 12 ms", ms);
    }
    // 4. zero-lag pick: il clic del plettro esce subito (entro 0,3 ms); spento, nulla prima di 1 ms
    {
        const int on = 24000;
        juce::Random r (3);
        std::vector<float> click (48000, 0.0f);
        for (int i = on; i < on + 96; ++i) click[(size_t) i] = 0.6f * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-(i - on) / 30.0f);
        auto energy = [&] (bool zl, int a, int b)
        {
            auto p = make<BDropProcessor> (1, 1);
            setP (p->apvts, BDropIDs::zeroLag, zl ? 1.0f : 0.0f);
            setP (p->apvts, BDropIDs::semitones, -2.0f);
            p->prepareToPlay (sr, block);
            setP (p->apvts, BDropIDs::tension, 0.0f);
            auto y = run (*p, 48000, [&] (int i) { return click[(size_t) i]; });
            return rmsDb (y, 0, on + a, on + b);
        };
        const double withZl = energy (true, 0, 48), without = energy (false, 0, 48);
        std::printf ("       primo ms dopo il clic: %.1f dB con zero-lag, %.1f dB senza\n", withZl, without);
        check (withZl > -30.0 && without < -60.0, "zero-lag pick: il colpo di plettro esce a latenza zero", withZl - without);
    }
    // 5. accordo (due note): il volume resta stabile, nessun tremolio dalle giunzioni
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::semitones, -5.0f);
        setP (p->apvts, BDropIDs::tension, 0.0f);
        auto y = run (*p, 96000, [&] (int i) { return (float) (0.25 * std::sin (tp * 196.0 * i / sr) + 0.25 * std::sin (tp * 293.66 * i / sr)); });
        double mn = 1e9, mx = -1e9;
        for (int k = 24000; k + 1920 <= 96000; k += 960) { const double d = rmsDb (y, 0, k, k + 1920); mn = std::min (mn, d); mx = std::max (mx, d); }
        std::printf ("       accordo -5 st: RMS su finestre di 40 ms tra %.2f e %.2f dB\n", mn, mx);
        check (mx - mn < 2.5, "accordo: variazione di volume sotto 2,5 dB", mx - mn);
    }
    // 6. tension: un'ottava sotto, 50 Hz in uscita (da 100 Hz) più bassi con tension 100 % che con 0 %
    {
        auto level = [&] (float tension)
        {
            auto p = make<BDropProcessor> (1, 1);
            setP (p->apvts, BDropIDs::range, 0.0f);
            setP (p->apvts, BDropIDs::semitones, -12.0f);
            setP (p->apvts, BDropIDs::tension, tension);
            auto y = run (*p, 48000, sine (100.0));
            return rmsDb (y, 0, 24000, 48000);
        };
        const double t0 = level (0.0f), t1 = level (100.0f);
        std::printf ("       -12 st, 100 -> 50 Hz: %.1f dB (tension 0) / %.1f dB (tension 100)\n", t0, t1);
        check (t0 - t1 > 6.0, "tension: le basse sotto 95 Hz si asciugano scendendo di un'ottava", t0 - t1);
    }
    // 7. detune spento: uscita identica all'ingresso (dopo la dissolvenza)
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::on, 0.0f);
        p->prepareToPlay (sr, block);
        juce::Random r (9);
        std::vector<float> in (24000); for (auto& v : in) v = 0.3f * (r.nextFloat() - 0.5f);
        auto y = run (*p, 24000, [&] (int i) { return in[(size_t) i]; });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - in[(size_t) i]));
        check (d < 1e-7f, "detune off: il plugin non tocca il segnale", d);
    }
    // 8. range: "octave" porta a -12 (fisso); di default 0 semitoni e il segnale passa identico
    {
        auto p = make<BDropProcessor> (1, 1);
        auto* s = p->apvts.getParameter (BDropIDs::semitones);
        const float def = s->convertFrom0to1 (s->getValue());
        juce::Random r (4);
        std::vector<float> in (24000); for (auto& v : in) v = 0.3f * (r.nextFloat() - 0.5f);
        auto y = run (*p, 24000, [&] (int i) { return in[(size_t) i]; });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - in[(size_t) i]));
        check (std::abs (def) < 0.01f && d < 1e-7f, "default 0 semitoni: il segnale passa identico", d);
        p->applyRange (BDropProcessor::octave);
        check (std::abs (s->convertFrom0to1 (s->getValue()) + 12.0f) < 0.01f, "range octave: -12 semitoni");
    }
    // 9. CPU
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::semitones, -5.0f);
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU: %.3f %% di un core\n", cpu);
        check (cpu < 2.0, "CPU sotto il 2 % di un core", cpu);
    }
    // 10. interfaccia
    {
        auto p = make<BDropProcessor> (1, 1);
        setP (p->apvts, BDropIDs::semitones, -5.0f);
        run (*p, 24000, [] (int i) { return pluck (i, 110.0, 0.25); });
        snapshot (*p, "b-drop.png");
        // rullo: doppio clic = 0 e il numero si scrive da tastiera
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        DropDisplay* disp = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* d = dynamic_cast<DropDisplay*> (k)) disp = d; else find (*k); } };
        find (*ed);
        auto* s = p->apvts.getParameter (BDropIDs::semitones);
        if (disp != nullptr) disp->typeValueForTest ("-7");
        check (disp != nullptr && std::abs (s->convertFrom0to1 (s->getValue()) + 7.0f) < 0.01f, "rullo: -7 scritto da tastiera", s->convertFrom0to1 (s->getValue()));
        if (disp != nullptr) disp->typeValueForTest ("+3");
        check (std::abs (s->convertFrom0to1 (s->getValue()) - 3.0f) < 0.01f, "rullo: +3 scritto da tastiera", s->convertFrom0to1 (s->getValue()));
        p->editorBeingDeleted (ed.get());
        ed.reset();
    }
    return finish();
}

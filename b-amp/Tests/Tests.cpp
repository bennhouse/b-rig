// Test di b-amp: livelli dei canali, gate (rumore chiuso, nota passa, stop rapido), tone stack, tuner MPM,
// cambio canale senza click, tone del boost, oversampling spento, controlli per testata salvati col progetto,
// CPU, larghezze fisse e snapshot (3 canali + tuner).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "smp/TestKit.h"
#include <map>
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto di = [] (int i) { return pluck (i, 110.0, 0.5, 0.25); };
    // 1. i tre canali: tutti producono segnale, il lead distorce più del clean (crest factor più basso)
    double crest[3];
    for (int ch = 0; ch < 3; ++ch)
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, ch == 0 ? 40.0f : 70.0f);
        auto y = run (*p, 96000, di);
        bool finite = true; float pk = 0;
        for (int i = 0; i < 96000; ++i) { finite &= std::isfinite (y.getSample (0, i)); pk = std::max (pk, std::abs (y.getSample (0, i))); }
        const double r = rmsDb (y, 0, 48000, 96000);
        crest[ch] = 20 * std::log10 (pk) - r;
        std::printf ("       canale %d: rms %.1f dBFS, picco %.1f dBFS, crest %.1f dB\n", ch, r, 20 * std::log10 (pk), crest[ch]);
        check (finite && r > -40.0 && pk < 1.5f, (juce::String ("canale ") + juce::String (ch) + ": uscita valida e sana").toRawUTF8(), r);
    }
    check (crest[2] < crest[0] - 2.0, "lead: più compresso / saturo del clean (crest factor)", crest[0] - crest[2]);
    // 1b. quanta saturazione: escursione d'uscita per 30 dB di dinamica in ingresso (palm-mute sul Mi basso a -50 e a
    //     -20 dBFS di picco) e rapporto armoniche/fondamentale su una nota suonata piano (-40 dBFS)
    auto satSpan = [&] (int ch, float gain, double& span, double& thd)
    {
        double lv[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = make<BAmpProcessor> (1, 1);
            setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, gain); setP (p->apvts, BAmpIDs::gateOn, 0.0f);
            const double a = k == 0 ? std::pow (10.0, -50.0 / 20.0) : std::pow (10.0, -20.0 / 20.0);
            auto y = run (*p, 48000, [a] (int i) { return (float) (a * std::sin (tp * 82.4 * i / sr)); });
            lv[k] = rmsDb (y, 0, 24000, 48000);
        }
        span = lv[1] - lv[0];
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, gain); setP (p->apvts, BAmpIDs::gateOn, 0.0f);
        auto y = run (*p, 48000, [] (int i) { return (float) (0.01 * std::sin (tp * 110.0 * i / sr)); });
        double h = 0; for (int k = 2; k <= 9; ++k) h += std::pow (10.0, toneDb (y, 0, 24000, 48000, 110.0 * k) / 10.0);
        thd = 10 * std::log10 (h + 1e-30) - toneDb (y, 0, 24000, 48000, 110.0);
    };
    std::map<int, double> spans;
    for (auto [ch, g] : { std::pair<int, float> { 1, 50.0f }, { 1, 100.0f }, { 2, 30.0f }, { 2, 50.0f }, { 2, 100.0f } })
    {
        double span, thd; satSpan (ch, g, span, thd);
        spans[ch * 1000 + (int) g] = span;
        std::printf ("       %s gain %3.0f %%: 30 dB in ingresso -> %.1f dB in uscita; nota a -40 dBFS: armoniche %+.1f dB\n", ch == 1 ? "crunch" : "lead  ", g, span, thd);
    }
    check (spans[2030] < 8.0 && spans[2050] < 3.0, "lead: muro di gain già al 30 %, palm-mute leggeri saturi quanto quelli forti (dB)", spans[2050]);
    check (spans[1050] > 10.0 && spans[1100] < 3.0, "crunch: dinamico a metà, abbastanza gain per il djent al massimo (dB)", spans[1100]);
    // 2. gate: rumore a -70 dBFS chiuso (uscita < -80 dBFS); con la nota passa; stop rapido
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, 2); setP (p->apvts, BAmpIDs::gain, 80); setP (p->apvts, BAmpIDs::gate, -55); setP (p->apvts, BAmpIDs::gateOn, 1);
        juce::Random r (3);
        auto noise = run (*p, 96000, [&] (int) { return 0.0003f * (2.0f * r.nextFloat() - 1.0f); });
        const double nz = rmsDb (noise, 0, 48000, 96000);
        auto q = make<BAmpProcessor> (1, 1);
        setP (q->apvts, BAmpIDs::channel, 2); setP (q->apvts, BAmpIDs::gain, 80); setP (q->apvts, BAmpIDs::gateOn, 0);   // gate spento: riferimento
        auto open = run (*q, 96000, [&] (int) { return 0.0003f * (2.0f * r.nextFloat() - 1.0f); });
        const double oz = rmsDb (open, 0, 48000, 96000);
        std::printf ("       rumore DI a -70 dBFS nel lead: gate spento -> %.1f dBFS, gate -55 -> %.1f dBFS\n", oz, nz);
        check (nz < oz - 30.0 && nz < -80.0, "gate: il rumore amplificato sparisce (dBFS)", nz);
        auto note = run (*p, 48000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
        check (rmsDb (note, 0, 24000, 48000) > -30.0, "gate: la nota passa", rmsDb (note, 0, 24000, 48000));
        // stop: nota poi silenzio; quanto ci mette a scendere sotto -60 dBFS
        auto stop = run (*p, 48000, [] (int i) { return i < 24000 ? 0.2f * (float) std::sin (tp * 110.0 * i / sr) : 0.0f; });
        int closeAt = -1; for (int i = 24000; i + 480 < 48000; i += 48) if (rmsDb (stop, 0, i, i + 480) < -60.0) { closeAt = i; break; }
        std::printf ("       chiusura dopo lo stop: %.0f ms\n", (closeAt - 24000) / sr * 1000.0);
        check (closeAt > 0 && closeAt - 24000 < (int) (0.12 * sr), "gate: dopo lo stop chiude in meno di 120 ms (ms)", (closeAt - 24000) / sr * 1000.0);
    }
    // 2b. studio del rumore di fondo: DI con fruscio (-90 / -80 / -70 dBFS rms) + ronzio 50 Hz a -66 dBFS, gate di default,
    //     lead al 80 %: (a) uscita nelle pause, (b) aperture spurie del gate sul solo rumore, (c) fruscio sopra 5 kHz
    //     nella coda di una nota rispetto alla stessa nota senza rumore
    for (double nzDb : { -90.0, -80.0, -70.0 })
    {
        auto mk = [] { auto q = make<BAmpProcessor> (1, 1); setP (q->apvts, BAmpIDs::channel, 2); setP (q->apvts, BAmpIDs::gain, 80); setP (q->apvts, BAmpIDs::gateOn, 1); return q; };
        const float na = (float) (std::pow (10.0, nzDb / 20.0) * 1.7320508);
        juce::Random r (11);
        auto noisy = [&] (int i, float note) { return note + na * (2.0f * r.nextFloat() - 1.0f) + 0.0005f * (float) std::sin (tp * 50.0 * i / sr); };
        auto note = [] (int i) { const double t = std::fmod (i / sr, 2.0); return t < 1.2 ? (float) (0.25 * std::exp (-t * 2.5) * std::sin (tp * 110.0 * i / sr)) : 0.0f; };
        auto a = mk(); auto pause = run (*a, 4 * 48000, [&] (int i) { return noisy (i, 0.0f); });
        int opens = 0; { float prev = 0; for (int i = 48000; i < 4 * 48000; ++i) { const float v = std::abs (pause.getSample (0, i)); if (v > 1e-4f && prev <= 1e-4f) ++opens; prev = v; } }
        auto b = mk(); auto y = run (*b, 4 * 48000, [&] (int i) { return noisy (i, note (i)); });
        auto c = mk(); auto z = run (*c, 4 * 48000, [&] (int i) { return note (i) + 0.0005f * (float) std::sin (tp * 50.0 * i / sr); });
        // fruscio nella coda: differenza tra le due uscite, 0,6-1,2 s dopo l'attacco della seconda nota, sopra 5 kHz
        juce::AudioBuffer<float> d (1, 28800);
        for (int i = 0; i < 28800; ++i) d.setSample (0, i, y.getSample (0, 2 * 48000 + 28800 + i) - z.getSample (0, 2 * 48000 + 28800 + i));
        sp::Bq hp; hp.set (sp::Bq::HP, sr, 5000.0, 0.7071); for (int i = 0; i < 28800; ++i) d.setSample (0, i, hp.process (d.getSample (0, i)));
        const double tail = rmsDb (d, 0, 4800, 28800), sig = rmsDb (z, 0, 2 * 48000 + 28800, 2 * 48000 + 57600);
        const double rest = rmsDb (pause, 0, 48000, 4 * 48000), between = rmsDb (y, 0, 3 * 48000 + 14400, 4 * 48000 - 4800);
        double hy = 0, hz = 0; { sp::Bq h1, h2; h1.set (sp::Bq::HP, sr, 5000.0, 0.7071); h2 = h1;
            for (int i = 2 * 48000 + 28800; i < 2 * 48000 + 57600; ++i) { const double a1 = h1.process (y.getSample (0, i)), a2 = h2.process (z.getSample (0, i)); hy += a1 * a1; hz += a2 * a2; } }
        std::printf ("       rumore DI %.0f dBFS: pavimento stimato %.1f dBFS, alte >5k nella coda rispetto alla nota senza rumore %+.1f dB\n", nzDb, b->noiseFloorDb(), 10 * std::log10 (hy / (hz + 1e-20)));
        std::printf ("       rumore DI %.0f dBFS: pause %.1f dBFS (aperture spurie %d), tra le note %.1f dBFS, fruscio >5k nella coda %.1f dB sotto la nota\n",
                     nzDb, rest, opens, between, sig - tail);
        check (rest < -80.0 && between < -70.0 && opens < 3, (juce::String ("rumore DI ") + juce::String ((int) nzDb) + " dBFS: pause pulite, nessun chatter").toRawUTF8(), rest);
    }
    // 2c. scheda di realismo: per ogni canale e gain, THD e armoniche pari/dispari a vari livelli d'ingresso (seno 220 Hz),
    //     compressione (dB di uscita per 20 dB di ingresso), aliasing (energia fuori dalle armoniche, seno 3,1 kHz) e sag
    auto measure = [] (int ch, float gain, float inDb, double f0, double& thd, double& h2h3, double& outDb, double& alias)
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, gain); setP (p->apvts, BAmpIDs::gate, -90.0f);
        setP (p->apvts, BAmpIDs::bass, 50); setP (p->apvts, BAmpIDs::mid, 50); setP (p->apvts, BAmpIDs::treble, 50);
        setP (p->apvts, BAmpIDs::presence, 0); setP (p->apvts, BAmpIDs::resonance, 0);
        setP (p->apvts, BAmpIDs::gateOn, 0.0f);   // misura dello stadio, senza il gate (che chiude su un 3,1 kHz senza basse)
        p->prepareToPlay (sr, block);
        const float a = (float) std::pow (10.0, inDb / 20.0);
        auto y = run (*p, 3 * 16384, [&] (int i) { return a * (float) std::sin (tp * f0 * i / sr); });
        const int from = 2 * 16384, to = 3 * 16384;
        const double h1 = toneDb (y, 0, from, to, f0);
        double hp = 0; for (int h = 2; h <= 12 && f0 * h < 20000; ++h) hp += std::pow (10.0, toneDb (y, 0, from, to, f0 * h) / 10.0);
        thd = 100.0 * std::sqrt (hp) / std::pow (10.0, h1 / 20.0);
        h2h3 = toneDb (y, 0, from, to, f0 * 2) - toneDb (y, 0, from, to, f0 * 3);
        outDb = rmsDb (y, 0, from, to);
        // aliasing: spettro (FFT 16384, Hann), energia lontana dalle armoniche rispetto a quella sulle armoniche
        std::vector<float> d (2 * 16384, 0.0f);
        for (int i = 0; i < 16384; ++i) d[(size_t) i] = y.getSample (0, from + i) * (float) (0.5 - 0.5 * std::cos (tp * i / 16384.0));
        juce::dsp::FFT (14).performFrequencyOnlyForwardTransform (d.data(), true);
        double on = 0, off = 0;
        for (int k = 3; k < 8192; ++k)
        {
            const double hz = k * sr / 16384.0, hr = hz / f0, dist = std::abs (hr - std::round (hr)) * f0;
            const double e = (double) d[(size_t) k] * d[(size_t) k];
            if (dist < 30.0 && std::round (hr) >= 1) on += e; else if (hz > 40.0) off += e;
        }
        alias = 10 * std::log10 (off / (on + 1e-30) + 1e-30);
    };
    {
        const char* names[3] = { "clean", "crunch", "lead" };
        for (int ch = 0; ch < 3; ++ch)
            for (float gain : { 30.0f, 60.0f, 90.0f })
            {
                double thd[3], h23[3], out[3], al;
                const float lv[3] = { -30.0f, -20.0f, -10.0f };
                for (int k = 0; k < 3; ++k) measure (ch, gain, lv[k], 220.0, thd[k], h23[k], out[k], al);
                double t2, h2, o2;
                measure (ch, gain, -14.0f, 3100.0, t2, h2, o2, al);
                std::printf ("       %-6s gain %2.0f%%: THD %6.1f / %6.1f / %6.1f %% (in -30/-20/-10 dBFS), H2-H3 %+5.1f / %+5.1f / %+5.1f dB, "
                             "out %5.1f / %5.1f / %5.1f dBFS (compr. %4.1f dB su 20), alias %5.1f dB\n",
                             names[ch], gain, thd[0], thd[1], thd[2], h23[0], h23[1], h23[2], out[0], out[1], out[2], 20.0 - (out[2] - out[0]), al);
            }
    }
    // 2e. voce di ogni canale (piccolo segnale, -50 dBFS, controlli a ore 12): risposta in frequenza relativa a 1 kHz
    {
        const double fr[9] = { 80, 150, 300, 500, 700, 1000, 2000, 4000, 8000 };
        for (int ch = 0; ch < 3; ++ch)
        {
            double db[9];
            for (int k = 0; k < 9; ++k)
            {
                auto p = make<BAmpProcessor> (1, 1);
                setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, 20); setP (p->apvts, BAmpIDs::gate, -90);
                setP (p->apvts, BAmpIDs::treble, 50); setP (p->apvts, BAmpIDs::presence, 0); setP (p->apvts, BAmpIDs::resonance, 0); setP (p->apvts, BAmpIDs::tight, 50);
                p->prepareToPlay (sr, block);
                auto y = run (*p, 3 * 16384, [&] (int i) { return 0.003f * (float) std::sin (tp * fr[k] * i / sr); });
                db[k] = toneDb (y, 0, 2 * 16384, 3 * 16384, fr[k]);
            }
            std::printf ("       voce canale %d (rel. 1 kHz):", ch);
            for (int k = 0; k < 9; ++k) std::printf (" %.0f:%+.1f", fr[k], db[k] - db[5]);
            std::printf ("\n");
        }
    }
    // 2d. sag: accordo tenuto, master alto; quanto scende l'uscita tra i primi 20 ms e 0,5 s, e quanto si riprende dopo
    for (int ch = 0; ch < 3; ++ch)
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, (float) ch); setP (p->apvts, BAmpIDs::gain, 60); setP (p->apvts, BAmpIDs::master, 90); setP (p->apvts, BAmpIDs::gate, -90);
        p->prepareToPlay (sr, block);   // parametri già fermi: niente rampe dello smoothing nella misura
        auto chord = [] (int i) { double s = 0; for (double f : { 82.4, 123.5, 164.8 }) for (int h = 1; h <= 4; ++h) s += std::sin (tp * f * h * i / sr) / h; return (float) (0.12 * s); };
        auto y = run (*p, 48000, chord);
        const double a = rmsDb (y, 0, 96, 480), b = rmsDb (y, 0, 19200, 24000);
        std::printf ("       sag canale %d (master 90%%): uscita %.1f dBFS a 2-10 ms, %.1f dBFS a 0,4-0,5 s (%+.1f dB)\n", ch, a, b, b - a);
        check (b - a < 1.0 && b - a > -4.0, "sag: l'accordo tenuto si comprime un po' dopo l'attacco (mai di più)", b - a);
    }
    // 2f. gate disattivabile: spento, il rumore amplificato passa (come senza gate); acceso, silenzio
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, 2); setP (p->apvts, BAmpIDs::gain, 80); setP (p->apvts, BAmpIDs::gate, -55); setP (p->apvts, BAmpIDs::gateOn, 0);
        juce::Random r (5);
        auto y = run (*p, 96000, [&] (int) { return 0.0003f * (2.0f * r.nextFloat() - 1.0f); });
        const double off = rmsDb (y, 0, 48000, 96000);
        setP (p->apvts, BAmpIDs::gateOn, 1);
        auto z = run (*p, 96000, [&] (int) { return 0.0003f * (2.0f * r.nextFloat() - 1.0f); });
        const double on = rmsDb (z, 0, 48000, 96000);
        std::printf ("       gate spento %.1f dBFS, acceso %.1f dBFS\n", off, on);
        check (off > -60.0 && on < -100.0, "noise gate disattivabile", off - on);
    }
    // 3. tone stack: stabile, treble alza le alte, bass alza le basse
    {
        amp::ToneStack t;
        t.set (sr, 1.0, 0.5, 0.5); const double hiUp = 20 * std::log10 (t.magnitude (5000.0, sr));
        t.set (sr, 0.0, 0.5, 0.5); const double hiDn = 20 * std::log10 (t.magnitude (5000.0, sr));
        t.set (sr, 0.5, 0.5, 1.0); const double loUp = 20 * std::log10 (t.magnitude (80.0, sr));
        t.set (sr, 0.5, 0.5, 0.0); const double loDn = 20 * std::log10 (t.magnitude (80.0, sr));
        t.set (sr, 0.5, 0.5, 0.5); const double midC = 20 * std::log10 (t.magnitude (700.0, sr));
        std::printf ("       tone stack: treble 5 kHz %.1f / %.1f dB, bass 80 Hz %.1f / %.1f dB, centro 700 Hz %.1f dB\n", hiUp, hiDn, loUp, loDn, midC);
        check (hiUp > hiDn + 6.0 && loUp > loDn + 6.0 && std::isfinite (midC), "tone stack passivo: treble e bass agiscono, filtro stabile", hiUp - hiDn);
        float z = 0; bool ok = true; for (int i = 0; i < 48000; ++i) { z = t.process (i == 0 ? 1.0f : 0.0f); ok &= std::isfinite (z) && std::abs (z) < 10.0f; }
        check (ok && std::abs (z) < 1e-4f, "tone stack: risposta all'impulso che si spegne (stabile)", z);
    }
    // 4. tuner MPM: E2 82,41 Hz e A4 440 Hz con armoniche, errore in cents
    {
        for (double f0 : { 82.41, 196.0, 440.0 })
        {
            auto p = make<BAmpProcessor> (1, 1);
            run (*p, 48000, [f0] (int i) { double s = 0; for (int h = 1; h <= 5; ++h) s += std::sin (tp * f0 * h * i / sr) / h; return (float) (0.2 * s); });
            TunerOverlay t (*p); t.setVisible (true); t.setSize (100, 100);
            for (int k = 0; k < 8; ++k) t.analyse();
            const double ct = 1200.0 * std::log2 (t.hz / f0);
            std::printf ("       tuner: %.2f Hz -> %.2f Hz (%.2f ct)\n", f0, t.hz, ct);
            check (std::abs (ct) < 1.0, "tuner MPM: entro 1 cent", ct);
        }
    }
    // 4b. gate e note basse lunghe: un mi basso (82 Hz) e un la in drop (55 Hz) che si spengono piano non vengono
    //     tagliati dal gate di default (-60 dB); stessa uscita con il gate spento finché la nota è sopra la soglia
    for (double f0 : { 82.41, 55.0 })
    {
        auto note = [f0] (int i) { const double t = i / sr; double v = 0; for (int h = 1; h <= 5; ++h) v += std::sin (tp * f0 * h * t) / h; return (float) (0.25 * std::exp (-t * 1.2) * v); };
        auto mk = [] (float on) { auto q = make<BAmpProcessor> (1, 1); setP (q->apvts, BAmpIDs::channel, 2); setP (q->apvts, BAmpIDs::gain, 60); setP (q->apvts, BAmpIDs::gateOn, on); return q; };
        auto g1 = mk (1.0f), g0 = mk (0.0f);
        auto y1 = run (*g1, 2 * 48000, note), y0 = run (*g0, 2 * 48000, note);
        const double d = rmsDb (y1, 0, 72000, 96000) - rmsDb (y0, 0, 72000, 96000);
        std::printf ("       gate con una nota a %.0f Hz che si spegne (1,5-2 s): %.2f dB rispetto al gate spento\n", f0, d);
        check (std::abs (d) < 1.0, "gate: le note basse lunghe non vengono tagliate", d);
    }
    // 5. noise gate spento di default; tone del boost vol + eq; oversampling spento; controlli per testata salvati
    {
        auto p = make<BAmpProcessor> (1, 1);
        check (p->apvts.getRawParameterValue (BAmpIDs::gateOn)->load() < 0.5f, "noise gate spento di default");
        // tone: 0 = scuro, 100 = brillante (alte sopra 5 kHz rispetto a 1 kHz, a parità del resto)
        auto bright = [] (float tone)
        {
            auto q = make<BAmpProcessor> (1, 1);
            setP (q->apvts, BAmpIDs::channel, 0); setP (q->apvts, BAmpIDs::gain, 20); setP (q->apvts, BAmpIDs::boost, 2); setP (q->apvts, BAmpIDs::boostTone, tone);
            auto y = run (*q, 3 * 16384, [] (int i) { return 0.003f * (float) (std::sin (tp * 1000.0 * i / sr) + std::sin (tp * 7000.0 * i / sr)); });
            return toneDb (y, 0, 16384, 3 * 16384, 7000.0) - toneDb (y, 0, 16384, 3 * 16384, 1000.0);
        };
        const double dark = bright (0.0f), mid = bright (50.0f), hi = bright (100.0f);
        std::printf ("       boost tone: 7k - 1k = %.1f dB (0 %%), %.1f dB (50 %%), %.1f dB (100 %%)\n", dark, mid, hi);
        check (dark < mid - 3.0 && hi > mid + 3.0, "boost tone: scuro a 0, brillante a 100", hi - dark);
        // oversampling spento: latenza 0, uscita pulita e vicina al 2x
        auto lvl = [] (int os, int& lat)
        {
            auto q = make<BAmpProcessor> (1, 1);
            setP (q->apvts, BAmpIDs::channel, 1); setP (q->apvts, BAmpIDs::os, (float) os);
            auto y = run (*q, 48000, [] (int i) { return pluck (i, 110.0, 0.25, 0.3); });
            lat = q->getLatencySamples();
            bool finite = true; for (int i = 0; i < 48000; ++i) finite &= std::isfinite (y.getSample (0, i));
            return finite ? rmsDb (y, 0, 12000, 48000) : -999.0;
        };
        int lat2 = 0, lat1 = 0;
        const double l2 = lvl (1, lat2), l1 = lvl (0, lat1);
        std::printf ("       oversampling: 2x %.1f dB (latenza %d), spento %.1f dB (latenza %d)\n", l2, lat2, l1, lat1);
        check (lat1 == 0 && std::abs (l1 - l2) < 1.5, "oversampling spento: latenza 0, stesso livello del 2x (dB)", l1 - l2);
        // ogni testata ha tutti i suoi controlli, anche dopo aver salvato e riaperto il progetto
        setP (p->apvts, BAmpIDs::channel, 0);
        setP (p->apvts, BAmpIDs::level, -6.0f); setP (p->apvts, BAmpIDs::gateOn, 1); setP (p->apvts, BAmpIDs::boost, 2);
        p->headMemory.switchPreset (0, 2); setP (p->apvts, BAmpIDs::channel, 2);
        const bool fresh = std::abs (p->apvts.getRawParameterValue (BAmpIDs::level)->load()) < 0.01f && p->apvts.getRawParameterValue (BAmpIDs::boost)->load() < 0.5f;
        setP (p->apvts, BAmpIDs::level, 3.0f);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto r = make<BAmpProcessor> (1, 1);
        r->setStateInformation (mb.getData(), (int) mb.getSize());
        r->headMemory.switchPreset (2, 0); setP (r->apvts, BAmpIDs::channel, 0);
        const float lv0 = r->apvts.getRawParameterValue (BAmpIDs::level)->load(), gt0 = r->apvts.getRawParameterValue (BAmpIDs::gateOn)->load();
        r->headMemory.switchPreset (0, 2); setP (r->apvts, BAmpIDs::channel, 2);
        const float lv2 = r->apvts.getRawParameterValue (BAmpIDs::level)->load();
        // progetto di prima: oversampling 4x salvato come 1 -> resta 4x
        {
            juce::ValueTree old ("BAmp");
            juce::ValueTree o ("PARAM"); o.setProperty ("id", BAmpIDs::os, nullptr); o.setProperty ("value", 1.0f, nullptr); old.appendChild (o, nullptr);
            juce::MemoryBlock ob; if (auto x = old.createXml()) juce::AudioProcessor::copyXmlToBinary (*x, ob);
            auto q = make<BAmpProcessor> (1, 1);
            q->setStateInformation (ob.getData(), (int) ob.getSize());
            check ((int) q->apvts.getRawParameterValue (BAmpIDs::os)->load() == 2, "progetto di prima: 4x resta 4x (off, 2x, 4x)");
        }
        check (fresh && std::abs (lv0 + 6.0f) < 0.05f && gt0 > 0.5f && std::abs (lv2 - 3.0f) < 0.05f, "testate indipendenti (level, gate, boost), salvate col progetto", lv0);
    }
    // gain a 0 = silenzio (come il potenziometro di un ampli vero); da 10 in su il suono pieno
    {
        double lv[2];
        for (int t = 0; t < 2; ++t)
        {
            auto p = make<BAmpProcessor> (1, 1);
            setP (p->apvts, BAmpIDs::channel, 2); setP (p->apvts, BAmpIDs::gain, t == 0 ? 0.0f : 10.0f);
            auto y = run (*p, 48000, [] (int i) { return pluck (i, 110.0, 0.25, 0.4); });
            lv[t] = rmsDb (y, 0, 12000, 48000);
        }
        std::printf ("       lead: gain 0 -> %.1f dBFS, gain 10 -> %.1f dBFS\n", lv[0], lv[1]);
        check (lv[0] < -120.0 && lv[1] > -40.0, "gain a 0: nessun suono", lv[0]);
    }
    // gate automatico: fruscio della DI a -55 dBFS (sopra la soglia di -60): nelle pause si chiude lo stesso
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, 2); setP (p->apvts, BAmpIDs::gateOn, 1);
        juce::Random r (9);
        auto y = run (*p, 4 * 48000, [&] (int i)
        {
            const int c = i % 96000;
            const float nz = 0.0018f * 1.732f * (2.0f * r.nextFloat() - 1.0f);
            return nz + (c < 28800 ? 0.25f * (float) std::sin (tp * 110.0 * i / sr) * (float) std::exp (-c / 12000.0) : 0.0f);
        });
        const double pause = rmsDb (y, 0, 96000 + 48000, 96000 + 90000), note = rmsDb (y, 0, 96000, 96000 + 9600);
        std::printf ("       fruscio DI -55 dBFS nel lead: nota %.1f dBFS, pausa %.1f dBFS\n", note, pause);
        check (pause < -100.0 && note > -20.0, "gate automatico: il fruscio della DI sopra la soglia si chiude nelle pause", pause);
    }
    // 6. cambio canale: nessun click (salto massimo)
    {
        auto p = make<BAmpProcessor> (1, 1);
        setP (p->apvts, BAmpIDs::channel, 0);
        juce::AudioBuffer<float> b (1, block); juce::MidiBuffer m; float jump = 0, steady = 0, steadyClean = 0, prev = 0;
        for (int k = 0; k < 400; ++k)
        {
            if (k == 200) setP (p->apvts, BAmpIDs::channel, 2);
            for (int i = 0; i < block; ++i) b.setSample (0, i, 0.2f * (float) std::sin (tp * 110.0 * (k * block + i) / sr));
            p->processBlock (b, m);
            for (int i = 0; i < block; ++i)
            {
                const float d = std::abs (b.getSample (0, i) - prev); prev = b.getSample (0, i);
                if (k >= 195 && k < 210) jump = std::max (jump, d);       // durante il cambio
                if (k >= 300) steady = std::max (steady, d);              // il lead a regime (onda quasi quadra: fronti ripidi)
                if (k >= 100 && k < 195) steadyClean = std::max (steadyClean, d);   // il clean a regime
            }
        }
        std::printf ("       cambio canale: salto massimo durante il cambio %.3f, a regime nel clean %.3f e nel lead %.3f\n", jump, steadyClean, steady);
        // l'attacco del lead da fermo (stessa sinusoide): il cambio non deve fare salti più grandi del suono stesso
        float onset = 0;
        {
            auto q = make<BAmpProcessor> (1, 1);
            setP (q->apvts, BAmpIDs::channel, 2);
            float pv = 0;
            for (int k = 0; k < 15; ++k)
            {
                for (int i = 0; i < block; ++i) b.setSample (0, i, 0.2f * (float) std::sin (tp * 110.0 * (k * block + i) / sr));
                q->processBlock (b, m);
                for (int i = 0; i < block; ++i) { onset = std::max (onset, std::abs (b.getSample (0, i) - pv)); pv = b.getSample (0, i); }
            }
        }
        std::printf ("       attacco del lead da fermo: salto massimo %.3f\n", onset);
        steady = std::max ({ steady, steadyClean, onset });
        check (jump <= steady * 1.1f, "cambio canale: rampa, nessun click (mai oltre i fronti del suono a regime)", jump);
    }
    // 7. boost: "vol" alza di ~6 dB l'ingresso (clean poco saturo); "vol + eq" spinge la zona dell'articolazione (1,6 kHz)
    //    rispetto ai bassi e non alza il fizz sopra i 6 kHz più del resto
    {
        auto lvl = [] (int boost, double hz)
        {
            auto p = make<BAmpProcessor> (1, 1);
            setP (p->apvts, BAmpIDs::channel, 0); setP (p->apvts, BAmpIDs::gain, 20); setP (p->apvts, BAmpIDs::gateOn, 0); setP (p->apvts, BAmpIDs::boost, (float) boost);
            auto y = run (*p, 48000, [hz] (int i) { return 0.01f * (float) std::sin (tp * hz * i / sr); });
            return rmsDb (y, 0, 24000, 48000);
        };
        const double off = lvl (0, 1000), vol = lvl (1, 1000);
        std::printf ("       boost vol: %+.1f dB a 1 kHz\n", vol - off);
        check (vol - off > 4.5 && vol - off < 7.5, "boost vol: circa +6 dB (dB)", vol - off);
        const double e100 = lvl (2, 100) - lvl (1, 100), e1600 = lvl (2, 1600) - lvl (1, 1600), e7k = lvl (2, 7000) - lvl (1, 7000);
        std::printf ("       boost vol+eq rispetto a vol: 100 Hz %+.1f, 1,6 kHz %+.1f, 7 kHz %+.1f dB\n", e100, e1600, e7k);
        check (e1600 > 2.5 && e100 < 0.0 && e7k < e1600 - 2.0, "boost vol + eq: articolazione su, bassi stretti, niente fizz", e1600);
    }
    // 8. ogni testata ricorda i suoi controlli; i default di partenza: gain 50 % sul crunch, 30 % sul lead
    {
        auto p = make<BAmpProcessor> (1, 1);
        auto get = [&] (const char* id) { return p->apvts.getRawParameterValue (id)->load(); };
        setP (p->apvts, BAmpIDs::gain, 77);
        p->headMemory.switchPreset (1, 2); setP (p->apvts, BAmpIDs::channel, 2);
        const float leadGain = get (BAmpIDs::gain);
        setP (p->apvts, BAmpIDs::bass, 12);
        p->headMemory.switchPreset (2, 1); setP (p->apvts, BAmpIDs::channel, 1);
        const float crunchGain = get (BAmpIDs::gain), crunchBass = get (BAmpIDs::bass);
        p->headMemory.switchPreset (1, 2);
        check (std::abs (leadGain - 30.0f) < 0.5f && std::abs (crunchGain - 77.0f) < 0.5f && std::abs (crunchBass - 50.0f) < 0.5f && std::abs (get (BAmpIDs::bass) - 12.0f) < 0.5f,
               "testate indipendenti: lead parte da gain 30 %, il crunch ritrova i suoi valori", leadGain);
    }
    auto p = make<BAmpProcessor> (1, 1);
    setP (p->apvts, BAmpIDs::channel, 2);
    const double cpu = cpuPercent (*p, di);
    check (cpu < 4.0, "CPU % di un core (lead, 2x)", cpu);
    setP (p->apvts, BAmpIDs::os, 2);
    const double cpu4 = cpuPercent (*p, di);
    check (cpu4 < 7.0, "CPU % di un core (lead, 4x)", cpu4);
    setP (p->apvts, BAmpIDs::os, 0);
    const double cpu1 = cpuPercent (*p, di);
    std::printf ("       CPU lead: 2x %.2f %%, 4x %.2f %%, oversampling spento %.2f %%\n", cpu, cpu4, cpu1);
    check (cpu1 < cpu, "oversampling spento: meno CPU del 2x", cpu1);
    setP (p->apvts, BAmpIDs::os, 1);
    for (int ch = 0; ch < 3; ++ch)
    {
        setP (p->apvts, BAmpIDs::channel, (float) ch);
        setP (p->apvts, BAmpIDs::boost, ch == 2 ? 2.0f : 0.0f);   // il lead con il boost vol + eq: si vede anche tone
        snapshot (*p, (juce::String ("gui_bamp_") + juce::String (ch) + ".png").toRawUTF8(), [&] { run (*p, 24000, di); });
    }
    snapshot (*p, "gui_bamp_tuner.png", [&] { run (*p, 24000, [] (int i) { return 0.2f * (float) std::sin (tp * 82.41 * i / sr); });
                                              auto* ed = dynamic_cast<BAmpEditor*> (p->getActiveEditor());
                                              if (ed != nullptr) for (auto* c : ed->getChildren()) if (auto* t = dynamic_cast<TunerOverlay*> (c)) t->setVisible (true); });
    // retro della testata (clic sulla spia): valvole dalla fessura
    for (int ch : { 0, 1, 2 })
    {
        setP (p->apvts, BAmpIDs::channel, (float) ch);
        snapshot (*p, (juce::String ("gui_bamp_back_") + juce::String (ch) + ".png").toRawUTF8(), [&] { run (*p, 24000, di);
                                              juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                                              auto* ed = dynamic_cast<BAmpEditor*> (p->getActiveEditor());
                                              if (ed != nullptr) for (auto* c : ed->getChildren()) if (auto* h = dynamic_cast<AmpHead*> (c)) h->showBack(); });
    }
    return finish();
}

// Test numerici del DSP di b-aftercab: si eseguono da terminale, senza DAW.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include "UI/EqDisplay.h"
#include "smp/Widgets.h"
#include "DSP/Delverb.h"
#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what, double value = 0.0)
{
    std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, value);
    if (! ok) ++failures;
}

void setParam (BAftercabProcessor& p, const char* id, float v)
{
    auto* par = p.apvts.getParameter (id);
    par->setValueNotifyingHost (par->convertTo0to1 (v));
}

constexpr double sr = 48000.0;
constexpr int block = 512;
const double twoPi = juce::MathConstants<double>::twoPi;

std::unique_ptr<BAftercabProcessor> make (juce::AudioChannelSet in, juce::AudioChannelSet out)
{
    auto p = std::make_unique<BAftercabProcessor>();
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in);
    l.outputBuses.add (out);
    if (! p->setBusesLayout (l)) { std::printf ("layout non supportato\n"); ++failures; }
    p->setRateAndBufferSizeDetails (sr, block);
    // i test di spread e tempo partono da spread acceso e tempo "default" (i default del plugin sono diversi)
    setParam (*p, ParamIDs::spreadOn, 1.0f);
    setParam (*p, ParamIDs::revTimeMode, 0.0f);
    setParam (*p, ParamIDs::outAuto, 0.0f);   // uscita a 0 dB: i test misurano i singoli blocchi
    p->prepareToPlay (sr, block);
    return p;
}

// Processa un segnale (funzione del tempo in campioni) e restituisce l'uscita.
template <typename Fn>
juce::AudioBuffer<float> run (BAftercabProcessor& p, int numIn, int total, Fn signal)
{
    const int numOut = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> out (numOut, total);
    juce::AudioBuffer<float> buf (juce::jmax (numIn, numOut), block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        buf.clear();
        for (int ch = 0; ch < numIn; ++ch)
            for (int i = 0; i < n; ++i) buf.setSample (ch, i, signal (ch, pos + i));
        p.processBlock (buf, midi);
        for (int ch = 0; ch < numOut; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}
} // namespace

// Benchmark per funzione: % di un core a 48 kHz, buffer 128, minimo di 5 ripetizioni da 2 s.

// Accendere il comp con OUT auto su una chitarra a circa -24 dBFS: il volume non deve mai superare quello finale
// (prima la stima a -10 dBFS faceva un picco di diversi dB per 0,4 s)
static void compSwitchNoPeak()
{
    auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
    setParam (*p, ParamIDs::outAuto, 1.0f);
    setParam (*p, ParamIDs::revOn, 0.0f);
    setParam (*p, ParamIDs::eqOn, 0.0f);
    setParam (*p, ParamIDs::compMode, 1.0f);
    setParam (*p, ParamIDs::compThr, -30.0f);
    p->refreshNonRealtimeState();
    auto guitar = [] (int, int i) { const double t = std::fmod (i / sr, 0.25); double v = 0; for (int h = 1; h <= 6; ++h) v += std::sin (juce::MathConstants<double>::twoPi * 110.0 * h * t) / h;
                                    return (float) (0.08 * v * std::exp (-t * 4.0)); };
    const int win = (int) (0.05 * sr);
    std::vector<double> levels;
    int pos = 0;
    auto chunk = [&] (int len)
    {
        for (int done = 0; done < len; done += win)
        {
            auto y = run (*p, 1, win, [&] (int c, int i) { return guitar (c, pos + i); });
            pos += win;
            double e = 0; for (int i = 0; i < win; ++i) e += (double) y.getSample (0, i) * y.getSample (0, i);
            levels.push_back (10.0 * std::log10 (e / win + 1e-30));
            p->refreshNonRealtimeState();   // come il timer a 20 Hz dell'app
        }
    };
    chunk ((int) sr);
    const size_t before = levels.size();
    setParam (*p, ParamIDs::compOn, 1.0f);
    p->refreshNonRealtimeState();
    chunk ((int) (5 * sr));
    // la chitarra si ripete ogni 0,25 s = 5 finestre: ogni finestra si confronta con la stessa fase a regime
    std::array<double, 5> ref {}; std::array<int, 5> cnt {};
    for (size_t k = levels.size() - 20; k < levels.size(); ++k) { ref[k % 5] += levels[k]; ++cnt[k % 5]; }
    for (int ph = 0; ph < 5; ++ph) ref[(size_t) ph] /= juce::jmax (1, cnt[(size_t) ph]);
    double worst = -1e9;
    for (size_t k = before; k < levels.size(); ++k) worst = std::max (worst, levels[k] - ref[k % 5]);
    std::printf ("       comp acceso con OUT auto: al massimo %+.2f dB sopra il volume a regime\n", worst);
    check (worst < 1.0, "OUT auto: accendendo il comp nessun picco di volume", worst);
}

static void benchmark()
{
    struct Cfg { const char* name; bool stereoIn; std::vector<std::pair<const char*, float>> params; };
    std::vector<std::pair<const char*, float>> flat = { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 0.0f } };
    const std::vector<Cfg> cfgs = {
        { "tutto spento (mono>stereo)",          false, flat },
        { "spread",                              false, { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 1.0f } } },
        { "EQ zero latency, piatto",             false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 0.0f } } },
        { "EQ zero latency, 5 bande +3 dB",      false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 0.0f }, { "b1_gain", 3 }, { "b2_gain", 3 }, { "b3_gain", 3 }, { "b4_gain", 3 }, { "b5_gain", 3 } } },
        { "EQ linear low",                       false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::eqMode, 1 }, { ParamIDs::eqQuality, 0 }, { "b3_gain", 3 } } },
        { "EQ linear mid",                       false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::eqMode, 1 }, { ParamIDs::eqQuality, 1 }, { "b3_gain", 3 } } },
        { "EQ linear high",                      false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::eqMode, 1 }, { ParamIDs::eqQuality, 2 }, { "b3_gain", 3 } } },
        { "comp",                                false, { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::compOn, 1 } } },
        { "delverb room",                        false, { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::revOn, 1 }, { ParamIDs::revType, 0 } } },
        { "delverb plate",                       false, { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::revOn, 1 }, { ParamIDs::revType, 1 } } },
        { "delverb hall",                        false, { { ParamIDs::eqOn, 0.0f }, { ParamIDs::spreadOn, 0.0f }, { ParamIDs::revOn, 1 }, { ParamIDs::revType, 2 } } },
        { "tutto (linear high, comp, spread, hall)", false, { { ParamIDs::eqOn, 1.0f }, { ParamIDs::spreadOn, 1.0f }, { ParamIDs::eqMode, 1 }, { ParamIDs::eqQuality, 2 }, { "b3_gain", 3 }, { ParamIDs::compOn, 1 }, { ParamIDs::revOn, 1 }, { ParamIDs::revType, 2 } } },
        { "stereo: EQ zl 5 bande + comp + hall", true,  { { ParamIDs::eqOn, 1.0f }, { "b1_gain", 3 }, { "b2_gain", 3 }, { "b3_gain", 3 }, { "b4_gain", 3 }, { "b5_gain", 3 }, { ParamIDs::compOn, 1 }, { ParamIDs::revOn, 1 }, { ParamIDs::revType, 2 } } },
        { "stereo: EQ linear high",              true,  { { ParamIDs::eqOn, 1.0f }, { ParamIDs::eqMode, 1 }, { ParamIDs::eqQuality, 2 }, { "b3_gain", 3 } } },
    };
    constexpr int bs = 128;
    for (const auto& c : cfgs)
    {
        auto p = std::make_unique<BAftercabProcessor>();
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (c.stereoIn ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        p->setBusesLayout (l);
        p->setRateAndBufferSizeDetails (sr, bs);
        for (const auto& [id, v] : c.params) setParam (*p, id, v);
        p->prepareToPlay (sr, bs);
        p->waitForLinearIr (5000);
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        juce::Random rnd (7);
        for (int i = 0; i < 200; ++i) { buf.clear(); p->processBlock (buf, midi); }
        double best = 1e9;
        for (int rep = 0; rep < 5; ++rep)
        {
            const int blocks = (int) (2.0 * sr / bs);
            double t = 0.0;
            for (int k = 0; k < blocks; ++k)
            {
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < bs; ++i) buf.setSample (ch, i, 0.3f * (rnd.nextFloat() - 0.5f));
                const auto t0 = juce::Time::getHighResolutionTicks();
                p->processBlock (buf, midi);
                t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            }
            best = std::min (best, 100.0 * t / 2.0);
        }
        std::printf ("  %-42s %6.3f %%\n", c.name, best);
    }

    // interfaccia: costo di un fotogramma completo (finestra intera, spettro attivo), a 30 fps
    {
        BAftercabProcessor p;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::mono());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        p.setBusesLayout (l);
        p.setRateAndBufferSizeDetails (sr, 512);
        p.prepareToPlay (sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        juce::Random rnd (3);
        for (int k = 0; k < 100; ++k) { for (int i = 0; i < 512; ++i) buf.setSample (0, i, 0.3f * (rnd.nextFloat() - 0.5f)); p.processBlock (buf, midi); }
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        double best = 1e9;
        for (int rep = 0; rep < 60; ++rep)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();
            juce::Graphics g (img);
            ed->paintEntireComponent (g, false);
            best = std::min (best, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
        }
        std::printf ("  %-42s %6.3f ms (%.2f %% di un core a 30 fps)\n", "interfaccia: fotogramma intero", best * 1000.0, best * 30.0 * 100.0);
        ed.reset();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--bench") { benchmark(); return 0; }
    compSwitchNoPeak();

    // ---------------------------------------------------------------- 0. default di un'istanza nuova
    {
        BAftercabProcessor p;
        auto v = [&] (const char* id) { auto* a = p.apvts.getParameter (id); return a->convertFrom0to1 (a->getValue()); };
        bool flat = true;
        for (int b = 0; b < BAftercabProcessor::kNumBands; ++b) flat &= std::abs (v (ParamIDs::band (b, "gain").toRawUTF8())) < 1e-6f;
        check (v (ParamIDs::revOn) < 0.5f && v (ParamIDs::compOn) < 0.5f && v (ParamIDs::spreadOn) < 0.5f
                   && v (ParamIDs::eqOn) > 0.5f && flat && (int) v (ParamIDs::revTimeMode) == 1,
               "default: delverb off, comp off, spread off, EQ acceso e piatto, tempo in sync con la DAW");
    }

    // ---------------------------------------------------------------- 1. layout
    {
        BAftercabProcessor p;
        auto ok = [&] (juce::AudioChannelSet i, juce::AudioChannelSet o)
        {
            juce::AudioProcessor::BusesLayout l; l.inputBuses.add (i); l.outputBuses.add (o);
            return p.checkBusesLayoutSupported (l);
        };
        using CS = juce::AudioChannelSet;
        check (ok (CS::mono(), CS::mono()) && ok (CS::mono(), CS::stereo()) && ok (CS::stereo(), CS::stereo())
               && ! ok (CS::stereo(), CS::mono()), "layout supportati: 1->1, 1->2, 2->2 (non 2->1)");
    }

    // ---------------------------------------------------------------- 2. mono->stereo, impulso
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        check (p->getMode() == BAftercabProcessor::Mode::MonoToStereo, "modalita' Mono -> Stereo rilevata");
        setParam (*p, ParamIDs::delayMs, 10.0f);
        p->prepareToPlay (sr, block);                       // nessuna rampa iniziale del delay
        auto out = run (*p, 1, 4096, [] (int, int n) { return n == 100 ? 1.0f : 0.0f; });
        check (juce::exactlyEqual (out.getSample (0, 100), 1.0f) && juce::exactlyEqual (out.getSample (0, 99), 0.0f), "canale diretto (L): latenza zero, campione identico");
        check (std::abs (out.getSample (1, 580) - 1.0f) < 1e-4, "canale ritardato (R): impulso a +480 campioni (10 ms)", out.getSample (1, 580));
        check (p->getLatencySamples() == 0, "latenza dichiarata alla DAW = 0");
    }

    // ---------------------------------------------------------------- 3. delay frazionario
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::delayMs, 10.0123f);
        p->prepareToPlay (sr, block);
        const double f = 1000.0, D = 10.0123 * 0.001 * sr;  // 480.59 campioni
        auto out = run (*p, 1, 8192, [&] (int, int n) { return (float) std::sin (twoPi * f * n / sr); });
        double err = 0.0;
        for (int n = 2000; n < 8192; ++n)
            err = std::max (err, std::abs (out.getSample (1, n) - std::sin (twoPi * f * (n - D) / sr)));
        check (err < 1e-3, "delay frazionario 480.59 campioni: errore max su sinusoide 1 kHz", err);

        double errHi = 0.0;
        auto out2 = run (*p, 1, 8192, [&] (int, int n) { return (float) std::sin (twoPi * 16000.0 * n / sr); });
        for (int n = 2000; n < 8192; ++n)
            errHi = std::max (errHi, std::abs (out2.getSample (1, n) - std::sin (twoPi * 16000.0 * (n - D) / sr)));
        check (errHi < 2e-2, "delay frazionario: errore max a 16 kHz", errHi);
    }

    // ---------------------------------------------------------------- 4. stereo, EQ spento = bit-exact
    {
        auto p = make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        juce::Random rnd (1);
        std::vector<float> noise (2 * 4096);
        for (auto& v : noise) v = rnd.nextFloat() * 2 - 1;
        auto out = run (*p, 2, 4096, [&] (int ch, int n) { return noise[(size_t) (ch * 4096 + n)]; });
        bool same = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 4096; ++n) same &= juce::exactlyEqual (out.getSample (ch, n), noise[(size_t) (ch * 4096 + n)]);
        check (same, "Stereo -> Stereo con EQ spento: uscita identica all'ingresso");
    }

    // ---------------------------------------------------------------- 5. EQ zero latency
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono());
        setParam (*p, ParamIDs::eqOn, 1.0f);
        setParam (*p, ParamIDs::band (2, "freq").toRawUTF8(), 1000.0f);
        setParam (*p, ParamIDs::band (2, "gain").toRawUTF8(), 6.0f);
        auto out = run (*p, 1, 16384, [] (int, int n) { return (float) (0.25 * std::sin (twoPi * 1000.0 * n / sr)); });
        float peak = 0; for (int n = 8000; n < 16384; ++n) peak = std::max (peak, std::abs (out.getSample (0, n)));
        check (std::abs (20 * std::log10 (peak / 0.25) - 6.0) < 0.05, "EQ fase minima: campana +6 dB a 1 kHz (dB misurati)", 20 * std::log10 (peak / 0.25));
        check (p->getLatencySamples() == 0, "EQ fase minima: latenza 0");
    }

    // ---------------------------------------------------------------- 6. EQ fase lineare
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::delayMs, 5.0f);
        setParam (*p, ParamIDs::eqOn, 1.0f);
        setParam (*p, ParamIDs::eqMode, 1.0f);
        setParam (*p, ParamIDs::eqQuality, 0.0f);
        setParam (*p, ParamIDs::band (2, "freq").toRawUTF8(), 1000.0f);
        setParam (*p, ParamIDs::band (2, "gain").toRawUTF8(), 6.0f);
        p->prepareToPlay (sr, block);
        const int L = p->getLatencySamples();
        check (L == 1024, "fase lineare (Low, 48 kHz): latenza dichiarata in campioni", L);

        // lascia al convolutore il tempo di caricare la risposta all'impulso
        p->waitForLinearIr (5000);
        for (int i = 0; i < 40; ++i) { run (*p, 1, block, [] (int, int) { return 0.0f; }); juce::Thread::sleep (10); }

        const double f = 300.0;   // fuori centro: un EQ IIR qui sposterebbe la fase
        const double g = sp::BiquadCoeffs::make (sp::FilterType::Bell, sr, 1000.0, 1.0, 6.0).magnitude (twoPi * f / sr);
        auto out = run (*p, 1, 24000, [&] (int, int n) { return (float) (0.25 * std::sin (twoPi * f * n / sr)); });
        double err = 0.0;
        for (int n = 12000; n < 24000; ++n)
            err = std::max (err, std::abs (out.getSample (0, n) - 0.25 * g * std::sin (twoPi * f * (n - L) / sr)));
        check (err < 2e-3, "fase lineare: uscita = guadagno x ingresso ritardato di L, nessuno sfasamento (errore max)", err);

        setParam (*p, ParamIDs::eqOn, 0.0f);
        p->refreshNonRealtimeState();
        check (p->getLatencySamples() == 0, "EQ spento: latenza torna a 0");
    }

    // ---------------------------------------------------------------- 7. analisi di allineamento
    {
        // Segnale periodico a 220 Hz (periodo 4.5454 ms). Intorno a 10 ms +/- 2 il
        // miglior allineamento e' 2 periodi = 9.0909 ms.
        const int n = (int) (3.0 * sr);
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (0.3 * std::sin (twoPi * 220 * i / sr) + 0.2 * std::sin (twoPi * 440 * i / sr + 1.0)
                                   + 0.1 * std::sin (twoPi * 660 * i / sr + 2.0));
        auto r = sp::findBestDelay (x.data(), n, sr, 10.0, 2.0, 1.0, 30.0);
        check (r.ok && std::abs (r.delayMs - 2000.0 / 220.0) < 0.005, "allineamento: trova 9.0909 ms (2 periodi a 220 Hz)", r.delayMs);
        check (r.corrAfter > 0.99 && r.corrAfter > r.corrBefore, "allineamento: correlazione dopo (prima = corrBefore)", r.corrAfter);

        std::vector<float> silence ((size_t) n, 0.0f);
        check (! sp::findBestDelay (silence.data(), n, sr, 10.0, 2.0, 1.0, 30.0).ok, "allineamento: rifiuta il silenzio");

        // Flusso completo nel plugin: ascolto -> thread di analisi -> risultato
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::delayMs, 10.0f);
        p->startAlignment();
        run (*p, 1, n + 2 * block, [&] (int, int i) { return i < n ? x[(size_t) i] : x[(size_t) (i - n)]; });
        for (int i = 0; i < 200 && p->getAlignState() != BAftercabProcessor::AlignState::Done; ++i) juce::Thread::sleep (10);
        const auto res = p->getLastAlignResult();
        check (p->getAlignState() == BAftercabProcessor::AlignState::Done && std::abs (res.delayMs - 2000.0 / 220.0) < 0.005,
               "plugin: analisi in background completata", res.delayMs);
    }

    // ---------------------------------------------------------------- 8. spread spento
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::spreadOn, 0.0f);
        p->prepareToPlay (sr, block);
        juce::Random rnd (3);
        auto out = run (*p, 1, 4096, [&] (int, int) { return rnd.nextFloat() - 0.5f; });
        bool same = true;
        for (int n = 0; n < 4096; ++n) same &= juce::exactlyEqual (out.getSample (0, n), out.getSample (1, n));
        check (same, "spread spento: L e R identici");
    }

    // ---------------------------------------------------------------- 8b. banda spenta = filtro neutro
    {
        auto p = make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::eqOn, 1.0f);
        setParam (*p, ParamIDs::band (2, "gain").toRawUTF8(), 8.0f);
        setParam (*p, ParamIDs::band (2, "on").toRawUTF8(), 0.0f);
        const auto c = p->computeEqCoeffs (sr);
        check (c[2] == sp::BiquadCoeffs {}, "banda spenta: coefficienti neutri (la curva la ignora)");
    }

    // ---------------------------------------------------------------- 8d. la curva disegnata = cosa succede all'audio
    // Si fa passare un impulso nel plugin, si misura la risposta in frequenza reale (FFT)
    // e la si confronta, frequenza per frequenza, con il valore che la curva disegna.
    {
        auto measure = [&] (int eqMode, int quality, double fMin, const char* name, double maxErrDb)
        {
            auto p = make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
            setParam (*p, ParamIDs::eqOn, 1.0f);
            setParam (*p, ParamIDs::eqMode, (float) eqMode);
            setParam (*p, ParamIDs::eqQuality, (float) quality);
            setParam (*p, ParamIDs::band (0, "type").toRawUTF8(), 0.0f);    // low cut 60 Hz
            setParam (*p, ParamIDs::band (0, "freq").toRawUTF8(), 60.0f);
            setParam (*p, ParamIDs::band (1, "freq").toRawUTF8(), 250.0f);  // banda 2 spenta con +8 dB: non deve agire
            setParam (*p, ParamIDs::band (1, "gain").toRawUTF8(), 8.0f);
            setParam (*p, ParamIDs::band (1, "on").toRawUTF8(), 0.0f);
            setParam (*p, ParamIDs::band (2, "freq").toRawUTF8(), 1000.0f); // +2,5 dB, Q 2
            setParam (*p, ParamIDs::band (2, "gain").toRawUTF8(), 2.5f);
            setParam (*p, ParamIDs::band (2, "q").toRawUTF8(), 2.0f);
            setParam (*p, ParamIDs::band (3, "freq").toRawUTF8(), 4000.0f); // +6 dB, Q 0,7
            setParam (*p, ParamIDs::band (3, "gain").toRawUTF8(), 6.0f);
            setParam (*p, ParamIDs::band (3, "q").toRawUTF8(), 0.7f);
            setParam (*p, ParamIDs::band (4, "freq").toRawUTF8(), 10000.0f); // high shelf -2 dB
            setParam (*p, ParamIDs::band (4, "gain").toRawUTF8(), -2.0f);
            p->prepareToPlay (sr, block);
            p->waitForLinearIr (5000);
            for (int i = 0; i < 60; ++i) { run (*p, 2, block, [] (int, int) { return 0.0f; }); juce::Thread::sleep (10); }

            constexpr int order = 16, N = 1 << order;          // 65536 punti: 0,73 Hz di risoluzione
            auto out = run (*p, 2, N, [] (int, int n) { return n == 0 ? 1.0f : 0.0f; });
            std::vector<float> fftData (2 * N, 0.0f);
            std::copy (out.getReadPointer (0), out.getReadPointer (0) + N, fftData.begin());
            juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (fftData.data(), true);

            ui::EqDisplay display (*p);                       // la curva usa queste stesse funzioni
            display.setBounds (0, 0, 680, 240);
            double maxErr = 0.0, worstHz = 0.0;
            for (double f = fMin; f <= 20000.0; f *= 1.02)
            {
                const int k = (int) std::lround (f * N / sr);
                const double fk = (double) k * sr / N;
                const double measured = 20.0 * std::log10 (juce::jmax (1.0e-9f, fftData[(size_t) k]));
                const double drawn = display.responseDb (fk);
                if (std::abs (measured - drawn) > maxErr) { maxErr = std::abs (measured - drawn); worstHz = fk; }
            }
            std::printf ("       (peggior punto a %.0f Hz)\n", worstHz);
            check (maxErr < maxErrDb, name, maxErr);
        };
        measure (0, 0, 20.0,  "curva = audio, zero latency: errore max in dB (20 Hz - 20 kHz)", 0.01);
        measure (1, 2, 20.0,  "curva = audio, linear phase high: errore max in dB (20 Hz - 20 kHz)", 0.1);
        measure (1, 1, 20.0,  "curva = audio, linear phase mid: errore max in dB (20 Hz - 20 kHz)", 0.1);
        measure (1, 0, 20.0,  "curva = audio, linear phase low: errore max in dB (20 Hz - 20 kHz)", 0.1);

        // la posizione orizzontale dei punti e la curva usano la stessa scala, andata e ritorno esatte
        auto p = make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        ui::EqDisplay display (*p);
        display.setBounds (0, 0, 680, 240);
        double maxRel = 0.0;
        for (double f = 20.0; f <= 20000.0; f *= 1.01)
            maxRel = std::max (maxRel, std::abs (display.freqForX (display.xForFreq (f)) - f) / f);
        check (maxRel < 1.0e-5, "scala frequenze del grafico: frequenza -> x -> frequenza (errore relativo)", maxRel);
    }

    // ---------------------------------------------------------------- 8e'. hall: sparkle senza distorsione
    {
        // nota di 1,5 kHz per 0,5 s, poi la coda: quanto c'è all'ottava sopra (3 kHz) e
        // quanto a una armonica dispari (4,5 kHz, segno di distorsione)
        auto measure = [&] (int t, double& octave, double& odd)
        {
            sp::Reverb rv; rv.prepare (sr); rv.setTimbre (t); rv.reset();
            const int n = (int) (3.0 * sr);
            std::vector<float> x ((size_t) n, 0.0f), l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < (int) (0.5 * sr); ++i) x[(size_t) i] = 0.3f * (float) std::sin (twoPi * 1500.0 * i / sr);
            rv.process (x.data(), l.data(), r.data(), n);
            auto goertzel = [&] (double f)
            {
                double s1 = 0, s2 = 0; const double c = 2.0 * std::cos (twoPi * f / sr);
                for (int i = (int) (1.0 * sr); i < n; ++i) { const double s0 = l[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return s1 * s1 + s2 * s2 - c * s1 * s2;
            };
            const double base = goertzel (1500.0);
            octave = 10.0 * std::log10 (goertzel (3000.0) / base);
            odd = 10.0 * std::log10 (goertzel (4500.0) / base);
        };
        double plOct, plOdd, haOct, haOdd;
        measure (sp::Reverb::Plate, plOct, plOdd);
        measure (sp::Reverb::Hall, haOct, haOdd);
        std::printf ("       coda di una nota a 1,5 kHz: ottava sopra plate %.1f dB, hall %.1f dB; 3a armonica hall %.1f dB\n", plOct, haOct, haOdd);
        check (haOct > -30.0 && haOct - plOct > 20.0, "hall: sparkle all'ottava sopra nella coda (dB rispetto alla nota)", haOct);
        check (haOdd < -60.0, "hall: nessuna distorsione (3a armonica nella coda, dB)", haOdd);
    }

    // ---------------------------------------------------------------- 8e. riverbero
    {
        const char* names[] = { "room", "plate", "hall" };
        for (int t = 0; t < 3; ++t)
        {
            sp::Reverb rv;
            rv.prepare (sr);
            rv.setTimbre (t);
            rv.reset();

            // rumore a banda limitata (200 Hz - 5 kHz, simile a una chitarra distorta dopo il cab)
            const int n = (int) (6.0 * sr);
            std::vector<float> x ((size_t) n), l ((size_t) n), r ((size_t) n);
            sp::Biquad bp1, bp2;
            bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 200, 0.707, 0);
            bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 5000, 0.707, 0);
            juce::Random rnd (11);
            for (auto& v : x) v = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 0.5f));
            rv.process (x.data(), l.data(), r.data(), n);
            double ein = 0, el = 0, er = 0, elr = 0;
            for (int i = n / 2; i < n; ++i) { ein += x[(size_t) i] * x[(size_t) i]; el += l[(size_t) i] * l[(size_t) i]; er += r[(size_t) i] * r[(size_t) i]; elr += l[(size_t) i] * r[(size_t) i]; }
            const double wetDb = 10.0 * std::log10 ((el + er) * 0.5 / ein);
            const double corr = elr / std::sqrt (el * er);
            std::printf ("       %s: livello wet/dry %.2f dB, correlazione L/R %.2f\n", names[t], wetDb, corr);

            // tempo di decadimento misurato: impulso, energia da -5 a -35 dB (T30)
            sp::Reverb rv2; rv2.prepare (sr); rv2.setTimbre (t); rv2.reset();
            const int m = (int) (8.0 * sr);
            std::vector<float> imp ((size_t) m, 0.0f), il ((size_t) m), ir ((size_t) m);
            imp[0] = 1.0f;
            rv2.process (imp.data(), il.data(), ir.data(), m);
            std::vector<double> edc ((size_t) m);
            double acc = 0;
            for (int i = m - 1; i >= 0; --i) { acc += il[(size_t) i] * il[(size_t) i] + ir[(size_t) i] * ir[(size_t) i]; edc[(size_t) i] = acc; }
            int i5 = 0, i35 = 0;
            for (int i = 0; i < m; ++i) { const double db = 10 * std::log10 (edc[(size_t) i] / edc[0] + 1e-30); if (! i5 && db < -5) i5 = i; if (! i35 && db < -35) { i35 = i; break; } }
            const double rt = 2.0 * (i35 - i5) / sr;
            std::printf ("       %s: RT60 misurato %.2f s (impostato %.2f s)\n", names[t], rt, sp::Reverb::timbre (t).rt60);
            const double tailEnd = std::abs (il[(size_t) m - 1]) + std::abs (ir[(size_t) m - 1]);
            check (std::abs (rt - sp::Reverb::timbre (t).rt60) / sp::Reverb::timbre (t).rt60 < 0.25 && tailEnd < 1e-4,
                   (juce::String ("riverbero ") + names[t] + ": decadimento corretto e coda che si spegne").toRawUTF8(), rt);
            check (std::abs (wetDb) < 1.5, (juce::String ("riverbero ") + names[t] + ": livello allineato agli altri timbri (dB)").toRawUTF8(), wetDb);
            check (std::abs (corr) < 0.3, (juce::String ("riverbero ") + names[t] + ": stereo decorrelato (|corr| L/R)").toRawUTF8(), corr);
        }

        // ---- delverb: bilanciamento delay / riverbero e volume totale (a 120 BPM, ducking escluso)
        const double targetDelayVsRev[3] = { 0.0, -2.0, -4.0 };   // plate: ripetizioni ben presenti (la hall non ha delay)
        for (int t = 1; t < 2; ++t)
        {
            auto levelDb = [&] (bool rev, bool dly)
            {
                sp::Delverb dv;
                dv.prepare (sr, 512);
                dv.setTempo (120.0);
                dv.setTimbre (t);
                dv.reset();
                dv.setParts (rev, dly, false);
                const int n = (int) (8.0 * sr);
                sp::Biquad bp1, bp2;
                bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 200, 0.707, 0);
                bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 5000, 0.707, 0);
                juce::Random rnd (5);
                std::vector<float> x (512), l (512), r (512);
                double ein = 0, eout = 0;
                for (int pos = 0; pos < n; pos += 512)
                {
                    for (auto& v : x) v = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 0.5f));
                    dv.process (x.data(), l.data(), r.data(), 512);
                    if (pos > n / 2)
                        for (int i = 0; i < 512; ++i) { ein += x[(size_t) i] * x[(size_t) i]; eout += 0.5 * (l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]); }
                }
                return 10.0 * std::log10 (eout / ein);
            };
            const double rv = levelDb (true, false), dl = levelDb (false, true), all = levelDb (true, true);
            std::printf ("       %s delverb: riverbero %.2f dB, delay %.2f dB (delay-riv %.2f), totale %.2f dB\n",
                         names[t], rv, dl, dl - rv, all);
            check (std::abs ((dl - rv) - targetDelayVsRev[t]) < 1.0,
                   (juce::String ("delverb ") + names[t] + ": equilibrio delay/riverbero (dB)").toRawUTF8(), dl - rv);
            check (std::abs (all) < 1.0, (juce::String ("delverb ") + names[t] + ": volume wet allineato (dB)").toRawUTF8(), all);
        }

        // hall = shimmer verb: niente delay; il balance va da hall pieno a shimmer a volume costante, più ottava sopra
        {
            auto run = [&] (double share, double& octDb)
            {
                sp::Delverb dv; dv.prepare (sr, 512); dv.setTempo (120.0); dv.setTimbre (2); dv.setDelayShare (share); dv.reset();
                sp::Biquad bp1, bp2;
                bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 200, 0.707, 0);
                bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 5000, 0.707, 0);
                juce::Random rnd (5);
                std::vector<float> x (512), l (512), r (512);
                double ein = 0, eout = 0;
                const int n = (int) (8.0 * sr);
                for (int pos = 0; pos < n; pos += 512)
                {
                    for (auto& v : x) v = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 0.5f));
                    dv.process (x.data(), l.data(), r.data(), 512);
                    if (pos > n / 2)
                        for (int i = 0; i < 512; ++i) { ein += x[(size_t) i] * x[(size_t) i]; eout += 0.5 * (l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]); }
                }
                // ottava sopra nella coda di una nota a 1,5 kHz
                sp::Delverb dn; dn.prepare (sr, 512); dn.setTempo (120.0); dn.setTimbre (2); dn.setDelayShare (share); dn.reset();
                const int m = (int) (3.0 * sr); std::vector<float> nl ((size_t) m), xin (512), ol (512), orr (512);
                for (int pos = 0; pos < m; pos += 512)
                {
                    for (int i = 0; i < 512; ++i) xin[(size_t) i] = pos + i < (int) (0.5 * sr) ? 0.3f * (float) std::sin (twoPi * 1500.0 * (pos + i) / sr) : 0.0f;
                    dn.process (xin.data(), ol.data(), orr.data(), 512);
                    for (int i = 0; i < 512 && pos + i < m; ++i) nl[(size_t) (pos + i)] = ol[(size_t) i];
                }
                auto gz = [&] (double f) { double s1 = 0, s2 = 0; const double c = 2.0 * std::cos (twoPi * f / sr);
                    for (int i = (int) (1.0 * sr); i < m; ++i) { const double s0 = nl[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; } return s1 * s1 + s2 * s2 - c * s1 * s2; };
                octDb = 10.0 * std::log10 (gz (3000.0) / gz (1500.0));
                return 10.0 * std::log10 (eout / ein);
            };
            double o0, o1, oP;
            const double l0 = run (0.0, o0), lP = run (sp::Delverb::presetDelayShare (2), oP), l1 = run (1.0, o1);
            std::printf ("       hall/shimmer: hall pieno %.2f dB (ottava %.1f), preset %.2f dB (ottava %.1f), shimmer pieno %.2f dB (ottava %.1f)\n", l0, o0, lP, oP, l1, o1);
            check (std::abs (l0) < 1.5 && std::abs (l1 - l0) < 1.5 && std::abs (lP - l0) < 1.5, "delverb hall: shim <-> verb a volume costante (dB)", l1 - l0);
            check (o1 - o0 > 15.0 && o1 > -15.0, "delverb hall: lo shimmer porta l'ottava sopra nella coda (dB)", o1 - o0);
        }

        // balance: il volume totale resta costante da "solo delay" a "solo riverbero"
        for (int t = 1; t < 2; ++t)
        {
            double lv[3];
            const double bal[3] = { 1.0, sp::Delverb::presetDelayShare (t), 0.0 };   // solo delay, preset, solo riverbero
            for (int k = 0; k < 3; ++k)
            {
                sp::Delverb dv; dv.prepare (sr, 512); dv.setTempo (120.0); dv.setTimbre (t); dv.setDelayShare (bal[k]); dv.reset();
                dv.setParts (true, true, false);
                sp::Biquad bp1, bp2;
                bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 200, 0.707, 0);
                bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 5000, 0.707, 0);
                juce::Random rnd (5);
                std::vector<float> x (512), l (512), r (512);
                double ein = 0, eout = 0;
                const int n = (int) (8.0 * sr);
                for (int pos = 0; pos < n; pos += 512)
                {
                    for (auto& v : x) v = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 0.5f));
                    dv.process (x.data(), l.data(), r.data(), 512);
                    if (pos > n / 2)
                        for (int i = 0; i < 512; ++i) { ein += x[(size_t) i] * x[(size_t) i]; eout += 0.5 * (l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]); }
                }
                lv[k] = 10.0 * std::log10 (eout / ein);
            }
            std::printf ("       %s balance: solo delay %.2f dB, preset %.2f dB, solo riverbero %.2f dB\n", names[t], lv[0], lv[1], lv[2]);

            // al preset: differenza delay - riverbero voluta per lo scopo del timbro
            auto partDb = [&] (bool rev, bool dly)
            {
                sp::Delverb dv; dv.prepare (sr, 512); dv.setTempo (120.0); dv.setTimbre (t);
                dv.setDelayShare (sp::Delverb::presetDelayShare (t)); dv.reset(); dv.setParts (rev, dly, false);
                sp::Biquad bp1, bp2;
                bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 200, 0.707, 0);
                bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 5000, 0.707, 0);
                juce::Random rnd (5);
                std::vector<float> x (512), l (512), r (512);
                double ein = 0, eout = 0;
                const int n = (int) (8.0 * sr);
                for (int pos = 0; pos < n; pos += 512)
                {
                    for (auto& v : x) v = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 0.5f));
                    dv.process (x.data(), l.data(), r.data(), 512);
                    if (pos > n / 2)
                        for (int i = 0; i < 512; ++i) { ein += x[(size_t) i] * x[(size_t) i]; eout += 0.5 * (l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]); }
                }
                return 10.0 * std::log10 (eout / ein);
            };
            const double diff = partDb (false, true) - partDb (true, false);
            const double want = t == 1 ? 2.0 : -4.0;
            std::printf ("       %s preset: delay - riverbero = %.2f dB (voluto %.1f)\n", names[t], diff, want);
            check (std::abs (diff - want) < 1.0, (juce::String ("delverb ") + names[t] + ": preset di bilanciamento per lo scopo").toRawUTF8(), diff);
            check (std::abs (lv[0] - lv[1]) < 1.5 && std::abs (lv[2] - lv[1]) < 1.5,
                   (juce::String ("delverb ") + names[t] + ": balance a volume costante").toRawUTF8(), std::max (std::abs (lv[0] - lv[1]), std::abs (lv[2] - lv[1])));
        }

        // mono / stereo: delay e riverbero sempre insieme sugli stessi canali
        {
            sp::Delverb dv; dv.prepare (sr, 4096); dv.setTempo (120.0); dv.setTimbre (1);
            dv.setDelayShare (0.61); dv.reset();
            std::vector<float> x (4096, 0.0f), l (4096), r (4096);
            double diffMono = 0.0, eL = 0.0, eR = 0.0;
            dv.setStereo (false); dv.reset();
            x[0] = 1.0f;
            for (int b = 0; b < 12; ++b)
            {
                dv.process (x.data(), l.data(), r.data(), 4096);
                for (int i = 0; i < 4096; ++i) diffMono = std::max (diffMono, (double) std::abs (l[(size_t) i] - r[(size_t) i]));
                x[0] = 0.0f;
            }
            // stereo con segnale solo a sinistra: tutto (delay e riverbero) resta prevalente a sinistra
            sp::Delverb ds2; ds2.prepare (sr, 4096); ds2.setTempo (120.0); ds2.setTimbre (1); ds2.setDelayShare (0.61);
            ds2.setStereo (true); ds2.reset();
            std::vector<float> xl (4096, 0.0f), xr (4096, 0.0f);
            xl[0] = 1.0f;
            ds2.setParts (false, true, false);   // solo delay
            for (int b = 0; b < 12; ++b)
            {
                ds2.process (xl.data(), xr.data(), l.data(), r.data(), 4096);
                for (int i = 0; i < 4096; ++i) { eL += l[(size_t) i] * l[(size_t) i]; eR += r[(size_t) i] * r[(size_t) i]; }
                xl[0] = 0.0f;
            }
            check (diffMono < 1e-6, "delverb mono: delay e riverbero identici su L e R", diffMono);
            check (eR < 1e-9 * eL, "delverb stereo: il delay resta sul canale del segnale (niente ping-pong)", eR);
        }

        // sync e tap separati: tap solo dal 4° tocco entro 3 s, sync on/off indipendente
        {
            auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
            p->setSync (true);  bool ok = p->isSync();
            p->setSync (false); ok &= ! p->isSync();
            for (int i = 0; i < 3; ++i) p->tapAt (10000.0 + 500.0 * i);   ok &= std::abs (p->getTapBpm() - 120.0f) < 0.05f;   // 3 tocchi: niente
            p->tapAt (10000.0 + 461.5 * 0 + 1500.0);                       // 4° tocco a 1,5 s: (1500 / 3) = 120 BPM
            for (int i = 0; i < 4; ++i) p->tapAt (20000.0 + 461.5 * i);   ok &= std::abs (p->getTapBpm() - 130.0f) < 0.1f;    // nuova serie: 130 BPM
            for (int i = 0; i < 4; ++i) p->tapAt (30000.0 + 1500.0 * i);  ok &= std::abs (p->getTapBpm() - 130.0f) < 0.1f;    // troppo lenti: invariato
            check (ok, "sync e tap separati: tap dal 4° tocco entro 3 s", p->getTapBpm());
        }

        // ducking: mentre suoni il delay si abbassa, nelle pause torna su
        {
            auto run2 = [&] (bool duck, double& during, double& after)
            {
                sp::Delverb dv; dv.prepare (sr, 512); dv.setTempo (120.0); dv.setTimbre (1); dv.reset();
                dv.setParts (false, true, duck);
                const int n = (int) (2.0 * sr);
                juce::Random rnd (9);
                std::vector<float> x ((size_t) n), l ((size_t) n), r ((size_t) n);
                for (int i = 0; i < n; ++i) x[(size_t) i] = i < n / 2 ? 0.3f * (rnd.nextFloat() - 0.5f) : 0.0f;
                for (int pos = 0; pos < n; pos += 512) dv.process (x.data() + pos, l.data() + pos, r.data() + pos, juce::jmin (512, n - pos));
                auto rms = [&] (int a, int b) { double e = 0; for (int i = a; i < b; ++i) e += l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]; return e; };
                during = rms (n / 2 - (int) (0.3 * sr), n / 2);
                after = rms (n / 2 + (int) (0.6 * sr), n / 2 + (int) (0.9 * sr));
            };
            double d1, a1, d0, a0;
            run2 (true, d1, a1); run2 (false, d0, a0);
            const double duckDuring = 10 * std::log10 (d1 / d0), duckAfter = 10 * std::log10 (a1 / a0);
            std::printf ("       ducking plate: mentre suoni %.1f dB, nella pausa %.1f dB\n", duckDuring, duckAfter);
            check (duckDuring < -2.0 && duckAfter > -1.0, "delverb: il delay si abbassa mentre suoni e torna nelle pause", duckDuring);
        }

        // tempo: il delay della plate (1/8 puntato) a 100 BPM cade a 450 ms
        {
            sp::Delverb dv;
            dv.prepare (sr, 512);
            dv.setTimbre (1);
            dv.setTempo (100.0);
            dv.reset();
            dv.setParts (false, true, false);
            const int n = (int) (1.0 * sr);
            std::vector<float> x ((size_t) n, 0.0f), l ((size_t) n), r ((size_t) n);
            x[0] = 1.0f;
            for (int pos = 0; pos < n; pos += 512)
                dv.process (x.data() + pos, l.data() + pos, r.data() + pos, juce::jmin (512, n - pos));
            int peak = 0;
            for (int i = 1; i < n; ++i) if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) peak])) peak = i;
            check (std::abs (peak / sr * 1000.0 - 450.0) < 3.0, "delverb plate: prima ripetizione a tempo (ms, 1/8 puntato a 100 BPM)", peak / sr * 1000.0);
        }

        // nel plugin: spento = bit-exact, acceso = latenza zero (il dry non si sposta)
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::revOn, 1.0f);
        setParam (*p, ParamIDs::revMix, 30.0f);
        setParam (*p, ParamIDs::spreadOn, 0.0f);
        p->prepareToPlay (sr, block);
        run (*p, 1, 4 * block, [] (int, int) { return 0.0f; });   // apertura del riverbero
        auto out = run (*p, 1, 4096, [] (int, int n) { return n == 100 ? 1.0f : 0.0f; });
        const float dryGain = std::cos (0.3f * juce::MathConstants<float>::halfPi);
        check (std::abs (out.getSample (0, 100) - dryGain) < 1e-3 && std::abs (out.getSample (0, 99)) < 1e-6,
               "riverbero acceso: impulso diretto nello stesso campione (latenza 0)", out.getSample (0, 100));
        check (p->getLatencySamples() == 0, "riverbero acceso: latenza dichiarata 0");
    }

    // ---------------------------------------------------------------- 8i. precision: punto oltre ±4 dB trascinabile subito
    {
        BAftercabProcessor p;
        p.setRateAndBufferSizeDetails (sr, block); p.prepareToPlay (sr, block);
        setParam (p, ParamIDs::eqOn, 1.0f);
        setParam (p, ParamIDs::band (2, "gain").toRawUTF8(), 8.0f);
        p.setUiFlag (ui::EqDisplay::precisionId, true);
        ui::EqDisplay d (p);
        d.setBounds (0, 0, 700, 300);
        auto* f = p.apvts.getParameter (ParamIDs::band (2, "freq"));
        const float fx = d.xForFreq (f->convertFrom0to1 (f->getValue()));
        // il punto è disegnato sul bordo alto (+4): lo si prende lì e lo si trascina in giù di 40 px
        float yTop = 0; for (int y = 0; y < 300; ++y) if (std::abs (d.getGridYRange().getStart() - (float) y) < 0.5f) yTop = (float) y;
        const juce::Point<float> start (fx, d.getGridYRange().getStart());
        auto ev = [&] (juce::Point<float> pos, juce::Point<float> down)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, {}, juce::MouseInputSource::defaultPressure,
                                     0, 0, 0, 0, &d, &d, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
        };
        d.mouseDown (ev (start, start));
        d.mouseDrag (ev (start.translated (0, 40), start));
        d.mouseUp (ev (start.translated (0, 40), start));
        auto* g = p.apvts.getParameter (ParamIDs::band (2, "gain"));
        const float gain = g->convertFrom0to1 (g->getValue());
        std::printf ("       precision, punto a +8 dB preso dal bordo e trascinato giù di 40 px: %.2f dB\n", gain);
        check (gain < 3.0f, "precision: un punto fuori scala si trascina subito anche in verticale", gain);
        juce::ignoreUnused (yTop);
    }

    // ---------------------------------------------------------------- 8j. clic, doppio clic e trascinamento sui punti EQ
    {
        BAftercabProcessor p;
        p.setRateAndBufferSizeDetails (sr, block); p.prepareToPlay (sr, block);
        setParam (p, ParamIDs::eqOn, 1.0f);
        setParam (p, ParamIDs::band (2, "gain").toRawUTF8(), 5.0f);
        ui::EqDisplay d (p);
        d.setBounds (0, 0, 700, 300);
        auto val = [&] (const char* what) { auto* a = p.apvts.getParameter (ParamIDs::band (2, what)); return a->convertFrom0to1 (a->getValue()); };
        auto pointPos = [&]
        {
            const auto r = d.getGridYRange();
            const float g = val ("gain"), range = BAftercabProcessor::kMaxEqGainDb;
            return juce::Point<float> (d.xForFreq (val ("freq")), juce::jmap (g, range, -range, r.getStart(), r.getEnd()));
        };
        auto ev = [&] (juce::Point<float> pos, juce::Point<float> down, int clicks)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, {}, juce::MouseInputSource::defaultPressure,
                                     0, 0, 0, 0, &d, &d, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, false);
        };
        auto click = [&] (int clicks, juce::Point<float> move = {})
        {
            const auto at = pointPos();
            d.mouseDown (ev (at, at, clicks));
            if (! move.isOrigin()) d.mouseDrag (ev (at + move, at, clicks));
            d.mouseUp (ev (at + move, at, clicks));
        };
        auto settle = [&] { d.flushClickForTest(); };
        click (1);                       const bool waits = val ("on") > 0.5f;   // il clic singolo aspetta un possibile secondo clic
        settle();                        const bool offAfterClick = val ("on") < 0.5f;
        click (1); settle();             const bool onAgain = val ("on") > 0.5f;
        click (1); click (2); settle();  const bool dbl = val ("on") > 0.5f && std::abs (val ("gain")) < 0.01f;   // doppio clic: default, resta acceso
        setParam (p, ParamIDs::band (2, "gain").toRawUTF8(), 5.0f);
        click (1, { 2.0f, 1.0f }); settle(); const bool tremor = val ("on") < 0.5f && std::abs (val ("gain") - 5.0f) < 0.01f;   // 2 px: è un clic
        setParam (p, ParamIDs::band (2, "on").toRawUTF8(), 1.0f);
        click (1, { 0.0f, 30.0f }); settle(); const bool drag = val ("on") > 0.5f && val ("gain") < 4.0f;   // trascinamento: niente on/off
        check (waits && offAfterClick && onAgain && dbl && tremor && drag,
               "punti EQ: clic = on/off dopo il tempo del doppio clic, doppio clic = default senza on/off, trascinamento = solo valori");
    }

    // ---------------------------------------------------------------- 8h. OUT auto e memoria per preset
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::eqOn, 1.0f);
        for (int b = 0; b < 5; ++b) setParam (*p, ParamIDs::band (b, "gain").toRawUTF8(), 4.0f);
        const float autoDb = p->computeAutoOutDb();
        setParam (*p, ParamIDs::eqOn, 1.0f);
        for (int b = 0; b < 5; ++b) setParam (*p, ParamIDs::band (b, "gain").toRawUTF8(), 0.0f);
        const float flatDb = p->computeAutoOutDb();
        std::printf ("       OUT auto: EQ +4 dB su tutte le bande -> %.2f dB, EQ piatto -> %.2f dB\n", autoDb, flatDb);
        check (autoDb < -3.0f && autoDb > -9.0f && std::abs (flatDb) < 0.01f, "OUT auto: compensa il volume aggiunto dall'EQ (fisso, non dinamico)", autoDb);
        setParam (*p, ParamIDs::compOn, 1.0f);
        setParam (*p, ParamIDs::compMode, 1.0f);
        setParam (*p, ParamIDs::compThr, -18.0f);
        const float compAuto = p->computeAutoOutDb();
        check (std::abs (compAuto) < 0.01f, "OUT auto: prima di misurare il comp nessun recupero (niente stime troppo alte)", compAuto);
        setParam (*p, ParamIDs::compOn, 0.0f);

        auto v = [&] (const char* id) { auto* a = p->apvts.getParameter (id); return a->convertFrom0to1 (a->getValue()); };
        setParam (*p, ParamIDs::compThr, -20.0f);                 // rhythm modificato
        p->compMemory.switchPreset (0, 1); setParam (*p, ParamIDs::compMode, 1.0f);
        const float leadThr = v (ParamIDs::compThr);
        p->compMemory.switchPreset (1, 0); setParam (*p, ParamIDs::compMode, 0.0f);
        const float backThr = v (ParamIDs::compThr);
        p->compMemory.resetPreset (0);
        const float resetThr = v (ParamIDs::compThr);
        p->revMemory.switchPreset (1, 0);
        const float roomMix = v (ParamIDs::revMix);
        check (std::abs (leadThr + 18.0f) < 0.01f && std::abs (backThr + 20.0f) < 0.01f && std::abs (resetThr + 12.0f) < 0.01f
                   && std::abs (roomMix - 10.0f) < 0.01f,
               "memoria per preset: valori ricordati, doppio clic = default del preset, room mix 10 %");
    }

    // ---------------------------------------------------------------- 8h'. OUT auto con il comp: uscita = ingresso a ogni livello
    for (float lvl : { 0.05f, 0.25f })   // circa -32 e -18 dBFS rms
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono());
        setParam (*p, ParamIDs::compOn, 1.0f); setParam (*p, ParamIDs::compMode, 0.0f); setParam (*p, ParamIDs::compThr, -30.0f);
        setParam (*p, ParamIDs::outAuto, 1.0f);
        sp::Biquad bp1, bp2;
        bp1.c = sp::BiquadCoeffs::make (sp::FilterType::LowCut, sr, 120, 0.707, 0);
        bp2.c = sp::BiquadCoeffs::make (sp::FilterType::HighCut, sr, 4000, 0.707, 0);
        juce::Random rnd (9);
        std::vector<float> x ((size_t) (8 * sr));
        for (size_t i = 0; i < x.size(); ++i)   // plettrate: inviluppo che si ripete ogni 250 ms
        {
            const double t = std::fmod ((double) i / sr, 0.25);
            x[i] = bp2.process (bp1.process ((rnd.nextFloat() - 0.5f) * 2.0f)) * lvl * (float) (0.4 + 0.6 * std::exp (-t * 12.0));
        }
        run (*p, 1, (int) (4 * sr), [&] (int, int i) { return x[(size_t) i]; });
        p->refreshNonRealtimeState();   // come fa il timer del plugin
        auto y = run (*p, 1, (int) (4 * sr), [&] (int, int i) { return x[(size_t) i + (size_t) (4 * sr)]; });
        double ei = 0, eo = 0;
        for (int i = (int) (2 * sr); i < (int) (4 * sr); ++i) { ei += (double) x[(size_t) i + (size_t) (4 * sr)] * x[(size_t) i + (size_t) (4 * sr)]; eo += (double) y.getSample (0, i) * y.getSample (0, i); }
        const double diff = 10.0 * std::log10 (eo / ei);
        std::printf ("       OUT auto con comp (ingresso %.0f dBFS rms): uscita - ingresso %+.2f dB\n", 10.0 * std::log10 (ei / (2 * sr)), diff);
        check (std::abs (diff) < 1.0, "OUT auto: col comp l'uscita resta al volume d'ingresso (dB)", diff);
    }

    // ---------------------------------------------------------------- 8g'. autogain spento: nessun make-up
    {
        const char* modes[] = { "rhythm", "lead", "clean" };
        for (int m = 0; m < 3; ++m)
        {
            auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
            setParam (*p, ParamIDs::compOn, 1.0f);
            setParam (*p, ParamIDs::compMode, (float) m);
            setParam (*p, ParamIDs::compThr, (float) sp::Compressor::preset (m).thresholdDb);
            setParam (*p, ParamIDs::spreadOn, 0.0f);
            p->prepareToPlay (sr, block);
            auto out = run (*p, 1, (int) (2.0 * sr), [&] (int, int n) { return 0.5f * (float) std::sin (twoPi * 1000.0 * n / sr); });
            float pk = 0.0f;
            for (int n = (int) (1.5 * sr); n < (int) (2.0 * sr); ++n) pk = std::max (pk, std::abs (out.getSample (0, n)));
            sp::Compressor c; c.prepare (sr); c.setMode (m); c.setThreshold (sp::Compressor::preset (m).thresholdDb);
            const double expected = -6.02 - c.staticGr (-6.02);
            const double measured = 20.0 * std::log10 (pk);
            check (std::abs (measured - expected) < 0.3, (juce::String ("compressore ") + modes[m] + ", autogain spento: solo riduzione").toRawUTF8(), measured - expected);
        }
    }

    // ---------------------------------------------------------------- 8g. compressore
    {
        // latenza zero: un impulso piccolo (sotto soglia) esce nello stesso campione
        auto p = make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        setParam (*p, ParamIDs::compOn, 1.0f);
        p->prepareToPlay (sr, block);
        run (*p, 2, 4 * block, [] (int, int) { return 0.0f; });
        auto out = run (*p, 2, 2048, [] (int, int n) { return n == 100 ? 0.01f : 0.0f; });
        check (std::abs (out.getSample (0, 100)) > 0.005f && std::abs (out.getSample (0, 99)) < 1e-9f && p->getLatencySamples() == 0,
               "compressore: latenza zero", out.getSample (0, 100));
    }

    // ---------------------------------------------------------------- 8c. prestazioni (CPU per istanza)
    {
        auto bench = [&] (const char* name, bool eq, int eqMode, int quality, double maxPercent, int revType = -1)
        {
            constexpr int bs = 128;   // buffer piccolo, come in registrazione a bassa latenza
            auto p = std::make_unique<BAftercabProcessor>();
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (juce::AudioChannelSet::mono());
            l.outputBuses.add (juce::AudioChannelSet::stereo());
            p->setBusesLayout (l);
            p->setRateAndBufferSizeDetails (sr, bs);
            setParam (*p, ParamIDs::spreadOn, 1.0f);   // caso peggiore: spread acceso
            setParam (*p, ParamIDs::eqOn, eq ? 1.0f : 0.0f);
            setParam (*p, ParamIDs::eqMode, (float) eqMode);
            setParam (*p, ParamIDs::eqQuality, (float) quality);
            for (int b = 0; b < 5; ++b) setParam (*p, ParamIDs::band (b, "gain").toRawUTF8(), 3.0f);
            if (revType >= 0) { setParam (*p, ParamIDs::revOn, 1.0f); setParam (*p, ParamIDs::revType, (float) revType); }
            if (eqMode == 1 && revType >= 0) setParam (*p, ParamIDs::compOn, 1.0f);
            p->prepareToPlay (sr, bs);
            p->waitForLinearIr (5000);
            for (int i = 0; i < 30; ++i) { juce::AudioBuffer<float> w (2, bs); w.clear(); juce::MidiBuffer m; p->processBlock (w, m); juce::Thread::sleep (5); }

            juce::AudioBuffer<float> buf (2, bs);
            juce::MidiBuffer midi;
            juce::Random rnd (7);
            const int blocks = (int) (10.0 * sr / bs);   // 10 secondi di audio
            double t = 0.0;
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < bs; ++i) buf.setSample (0, i, rnd.nextFloat() - 0.5f);
                if (k % 200 == 0) setParam (*p, ParamIDs::delayMs, 5.0f + (float) (k % 2000) / 100.0f);  // muove il delay (rampa)
                const auto t0 = juce::Time::getHighResolutionTicks();
                p->processBlock (buf, midi);
                t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            }
            const double percent = 100.0 * t / 10.0;
            check (percent < maxPercent, name, percent);
        };
        bench ("CPU % di un core: mono>stereo, EQ spento", false, 0, 0, 2.0);
        bench ("CPU % di un core: mono>stereo, EQ zero latency 5 bande", true, 0, 0, 2.0);
        bench ("CPU % di un core: mono>stereo, EQ linear phase high", true, 1, 2, 10.0);
        bench ("CPU % di un core: mono>stereo, EQ zero latency + riverbero hall", true, 0, 0, 5.0, 2);
        bench ("CPU % di un core: tutto attivo (linear high + comp + hall)", true, 1, 2, 10.0, 2);
    }

    // ---------------------------------------------------------------- 8f. delay manuale: sempre ms interi
    // Si simulano clic e trascinamenti veri sul box del delay, partendo da un valore
    // frazionario (come dopo l'allineamento "="): i decimali devono tornare a ,00.
    {
        auto p = make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
        auto* delayParam = p->apvts.getParameter (ParamIDs::delayMs);
        ui::ValueBox* box = nullptr;
        for (auto* c : ed->getChildren())
            if (auto* vb = dynamic_cast<ui::ValueBox*> (c))
                if (vb->getParameter() == delayParam) box = vb;

        auto value = [&] { return delayParam->convertFrom0to1 (delayParam->getValue()); };
        auto mouse = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, box, box, juce::Time::getCurrentTime(),
                                     down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const float cy = (float) box->getHeight() * 0.5f, cx = (float) box->getWidth() * 0.5f;
        bool ok = box != nullptr;
        juce::String log;

        setParam (*p, ParamIDs::delayMs, 15.42f);
        box->mouseDown (mouse ({ (float) box->getWidth() - 2.0f, cy }, { (float) box->getWidth() - 2.0f, cy }, false));
        log << "freccia > da 15,42: " << value();  ok &= std::abs (value() - 16.0f) < 1e-4f;

        setParam (*p, ParamIDs::delayMs, 15.42f);
        box->mouseDown (mouse ({ 2.0f, cy }, { 2.0f, cy }, false));
        log << " | freccia < da 15,42: " << value();  ok &= std::abs (value() - 15.0f) < 1e-4f;

        setParam (*p, ParamIDs::delayMs, 15.42f);
        box->mouseDown (mouse ({ cx, cy }, { cx, cy }, false));
        box->mouseDrag (mouse ({ cx + 21.0f, cy }, { cx, cy }, true));
        box->mouseUp (mouse ({ cx + 21.0f, cy }, { cx, cy }, true));
        log << " | trascinamento da 15,42: " << value();  ok &= std::abs (value() - std::round (value())) < 1e-4f;

        setParam (*p, ParamIDs::delayMs, 15.42f);
        juce::MouseWheelDetails w {}; w.deltaY = 0.2f;
        box->mouseWheelMove (mouse ({ cx, cy }, { cx, cy }, false), w);
        log << " | rotella da 15,42: " << value();  ok &= std::abs (value() - 16.0f) < 1e-4f;

        std::printf ("       %s\n", log.toRawUTF8());
        check (ok, "delay manuale (frecce, trascinamento, rotella): sempre ms interi dopo l'allineamento");
        ed.reset();
    }

    // ---------------------------------------------------------------- 9. interfaccia (snapshot PNG)
    {
        using CS = juce::AudioChannelSet;
        const std::pair<const char*, std::pair<CS, CS>> modes[] = {
            { "gui_mono_stereo.png", { CS::mono(), CS::stereo() } },
            { "gui_mono_mono.png",   { CS::mono(), CS::mono() } },
            { "gui_stereo.png",      { CS::stereo(), CS::stereo() } },
            { "gui_precision.png",   { CS::mono(), CS::stereo() } } };
        for (const auto& [file, io] : modes)
        {
            auto p = make (io.first, io.second);
            if (juce::String (file) != "gui_mono_mono.png")
            {
                // un'impostazione d'esempio per vedere curva e punti
                setParam (*p, ParamIDs::eqOn, 1.0f);
                setParam (*p, ParamIDs::band (0, "type").toRawUTF8(), 0.0f);   // low cut
                setParam (*p, ParamIDs::band (0, "freq").toRawUTF8(), 60.0f);
                setParam (*p, ParamIDs::band (1, "gain").toRawUTF8(), -3.0f);
                setParam (*p, ParamIDs::band (2, "gain").toRawUTF8(), 2.5f);
                setParam (*p, ParamIDs::band (3, "gain").toRawUTF8(), 6.0f);
                setParam (*p, ParamIDs::band (4, "gain").toRawUTF8(), -2.0f);
                setParam (*p, ParamIDs::delayMs, 15.42f);
                setParam (*p, ParamIDs::revOn, 1.0f);
                if (juce::String (file) == "gui_mono_stereo.png")
                {
                    setParam (*p, ParamIDs::revTimeMode, 2.0f);   // tap
                    setParam (*p, ParamIDs::revTapBpm, 128.0f);
                    setParam (*p, ParamIDs::compOn, 1.0f);
                }
                setParam (*p, ParamIDs::band (1, "on").toRawUTF8(), 0.0f);   // banda 2 spenta: solo bordo, curva senza di lei
            }
            if (juce::String (file) == "gui_stereo.png")
            {
                // casi limite: punti a 20 Hz / 20 kHz e a +10 dB, curva oltre il massimo
                setParam (*p, ParamIDs::band (0, "type").toRawUTF8(), 1.0f);
                setParam (*p, ParamIDs::band (0, "freq").toRawUTF8(), 20.0f);
                setParam (*p, ParamIDs::band (0, "gain").toRawUTF8(), 10.0f);
                setParam (*p, ParamIDs::band (4, "freq").toRawUTF8(), 20000.0f);
                setParam (*p, ParamIDs::band (4, "gain").toRawUTF8(), 10.0f);
                setParam (*p, ParamIDs::band (3, "gain").toRawUTF8(), 10.0f);
                setParam (*p, ParamIDs::band (3, "q").toRawUTF8(), 0.5f);
            }
            p->setUiFlag ("precision", juce::String (file) == "gui_precision.png");
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
            { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile (file);
            f.deleteFile();
            juce::FileOutputStream os (f);
            const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
            check (ok && ed->getWidth() > 0, file, ed->getHeight());
            ed.reset();
        }
    }

    // ---------------------------------------------------------------- 10. checkbox del comp come meter (snapshot ingrandito)
    {
        juce::Component row;
        row.setSize (4 * 30, 30);
        ui::PixelCheckbox boxes[4];
        const float levels[4] = { -1.0f, 0.0f, 0.5f, 1.0f };   // spenta, 0 dB, 6 dB, 12 dB di riduzione
        for (int i = 0; i < 4; ++i)
        {
            const bool on = levels[i] >= 0.0f;
            const float lv = levels[i];
            boxes[i].getState = [on] { return on; };
            boxes[i].meter = [lv] { return lv; };
            boxes[i].setBounds (i * 30, 0, 30, 30);
            row.addAndMakeVisible (boxes[i]);
        }
        auto img = row.createComponentSnapshot (row.getLocalBounds(), true, 6.0f);
        juce::Image bg (juce::Image::ARGB, img.getWidth(), img.getHeight(), true);
        { juce::Graphics gb (bg); gb.fillAll (ui::background); gb.drawImageAt (img, 0, 0); }
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("comp_meter.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (bg, os), "comp_meter.png (spenta, 0, 6, 12 dB)");
    }

    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

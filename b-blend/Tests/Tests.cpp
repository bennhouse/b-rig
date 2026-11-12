// Test di b-blend: allineamento sub-campione, angoli = IR singoli, livello del blend, thump, stato, CPU, snapshot.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "smp/TestUtil.h"
#include <cstdio>
namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0) { std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++failures; }
constexpr double sr = 48000.0; constexpr int block = 256;
void setP (juce::AudioProcessorValueTreeState& s, const char* id, float v) { auto* a = s.getParameter (id); a->setValueNotifyingHost (a->convertTo0to1 (v)); }
// IR sintetico di cabinet: impulso ritardato di "delay" campioni (anche frazionario) filtrato da una risonanza
juce::AudioBuffer<float> cab (double delay, double resHz, double decay)
{
    juce::AudioBuffer<float> b (1, 4800);
    for (int i = 0; i < 4800; ++i)
    {
        const double t = i - delay;
        b.setSample (0, i, t < 0 ? 0.0f : (float) (std::exp (-t / decay) * std::cos (juce::MathConstants<double>::twoPi * resHz * t / sr)));
    }
    return b;
}
std::unique_ptr<BBlendProcessor> make()
{
    auto p = std::make_unique<BBlendProcessor>();
    juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::mono()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    p->setBusesLayout (l); p->setRateAndBufferSizeDetails (sr, block); p->prepareToPlay (sr, block); return p;
}
template <typename F> juce::AudioBuffer<float> run (BBlendProcessor& p, int len, F sig)
{
    juce::AudioBuffer<float> out (2, len), b (2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += block) { const int n = std::min (block, len - pos); b.setSize (2, n, false, false, true); b.clear();
        for (int i = 0; i < n; ++i) b.setSample (0, i, sig (pos + i)); p.processBlock (b, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n); }
    return out;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = make();
    p->loadIr (0, cab (10.0, 2500.0, 60.0), sr, "cab A");
    p->loadIr (1, cab (17.3, 2500.0, 90.0), sr, "cab B");    // stessa forma, 7,3 campioni più tardi
    p->loadIr (2, cab (10.0, 900.0, 120.0), sr, "cab C");
    p->loadIr (3, cab (40.6, 2500.0, 45.0), sr, "cab D");    // stessa forma, 30,6 campioni più tardi
    p->waitForBuild (5000);
    std::printf ("       spostamenti trovati: B %.2f, C %.2f, D %.2f campioni\n", p->getLastAlignmentShift (1), p->getLastAlignmentShift (2), p->getLastAlignmentShift (3));
    check (std::abs (p->getLastAlignmentShift (1) - 7.3f) < 0.3f && std::abs (p->getLastAlignmentShift (3) - 30.6f) < 0.6f,
           "allineamento: ritardi degli IR trovati sotto il campione", p->getLastAlignmentShift (1));

    // angolo A (X 0, Y 1): l'uscita è l'IR A (normalizzato) con il suo ritardo; angolo B: allineato ad A
    auto peakAt = [&] (float x, float y)
    {
        setP (p->apvts, BBlendIDs::x, x); setP (p->apvts, BBlendIDs::y, y);
        p->prepareToPlay (sr, block); p->waitForBuild (5000); juce::Thread::sleep (50);
        run (*p, 4 * block, [] (int) { return 0.0f; });
        auto o = run (*p, 4096, [] (int i) { return i == 1000 ? 1.0f : 0.0f; });
        int best = 0; for (int i = 0; i < 4096; ++i) if (std::abs (o.getSample (0, i)) > std::abs (o.getSample (0, best))) best = i;
        return best - 1000;
    };
    const int la = peakAt (0.0f, 1.0f), lb = peakAt (1.0f, 1.0f);
    std::printf ("       picco dell'uscita: angolo A %d, angolo B %d campioni\n", la, lb);
    check (la == 10 && std::abs (lb - la) <= 1, "angoli: ogni IR esce in fase con A (latenza 0)", lb - la);

    // livello del blend: al centro (4 IR) e in un angolo stessa energia (±1,5 dB) con rumore
    auto energy = [&] (float x, float y)
    {
        setP (p->apvts, BBlendIDs::x, x); setP (p->apvts, BBlendIDs::y, y);
        p->prepareToPlay (sr, block); p->waitForBuild (5000); juce::Thread::sleep (50);
        juce::Random r (2);
        auto o = run (*p, 48000, [&] (int) { return 0.1f * (r.nextFloat() - 0.5f); });
        double e = 0; for (int i = 12000; i < 48000; ++i) e += (double) o.getSample (0, i) * o.getSample (0, i);
        return 10 * std::log10 (e);
    };
    const double eC = energy (0.5f, 0.5f), eA = energy (0.0f, 1.0f);
    check (std::abs (eC - eA) < 1.5, "blend: il centro suona forte quanto un angolo (dB)", eC - eA);

    // thump: un palm-mute (110 Hz forte) alza la campana a 98 Hz
    {
        setP (p->apvts, BBlendIDs::thump, 100.0f);
        run (*p, 24000, [] (int i) { return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 98.0 * i / sr); });
        std::printf ("       thump su un palm-mute: +%.1f dB a 98 Hz\n", p->thumpDb.load());
        check (p->thumpDb.load() > 6.0f && p->thumpDb.load() <= 12.0f, "thump: risonanza dinamica del cono (max 12 dB)", p->thumpDb.load());
        setP (p->apvts, BBlendIDs::thump, 0.0f);
    }

    // 1, 2 e 3 IR: ogni vertice della forma dà proprio quell'IR (ritardi diversi per riconoscerli)
    {
        auto q = make();
        auto lagOf = [&] (BBlendProcessor& pr, float x, float y)
        {
            setP (pr.apvts, BBlendIDs::x, x); setP (pr.apvts, BBlendIDs::y, y);
            pr.prepareToPlay (sr, block); pr.waitForBuild (5000); juce::Thread::sleep (50);
            run (pr, 4 * block, [] (int) { return 0.0f; });
            auto o = run (pr, 4096, [] (int i) { return i == 1000 ? 1.0f : 0.0f; });
            int best = 0; for (int i = 0; i < 4096; ++i) if (std::abs (o.getSample (0, i)) > std::abs (o.getSample (0, best))) best = i;
            return best - 1000;
        };
        q->loadIr (1, cab (20.0, 2500.0, 60.0), sr, "solo B");
        const int one = lagOf (*q, 0.2f, 0.9f);
        q->loadIr (3, cab (20.0, 900.0, 60.0), sr, "D");
        const int twoA = lagOf (*q, 0.0f, 0.5f), twoB = lagOf (*q, 1.0f, 0.5f);
        q->loadIr (0, cab (20.0, 1500.0, 60.0), sr, "A");
        const int three = lagOf (*q, 0.5f, 1.0f);
        std::printf ("       1 IR: %d, 2 IR: %d / %d, 3 IR: %d campioni\n", one, twoA, twoB, three);
        check (one == 20 && twoA == 20 && twoB == 20 && three == 20, "1, 2 e 3 IR: applicati e in fase con qualsiasi forma");
        q->removeIr (0); q->removeIr (1); q->removeIr (3); q->waitForBuild (5000);
        auto dry = run (*q, 2048, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
        check (dry.getMagnitude (0, 0, 2048) == 0.0f, "nessun IR: uscita muta (il plugin resta acceso)");
    }

    // stato: gli IR viaggiano col progetto
    {
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (q->getSlotName (3) == "cab D", "stato: gli IR si salvano e si ricaricano col progetto");
    }

    // ingresso: -7 dB fissi (nessun pareggio dinamico): stesso IR, il livello d'uscita non cambia nel tempo
    {
        auto q = make();
        q->loadIr (0, cab (10.0, 2500.0, 60.0), sr, "cab A");
        q->prepareToPlay (sr, block); q->waitForBuild (5000); juce::Thread::sleep (50);
        auto y = run (*q, 3 * 48000, [] (int i) { return 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr); });
        double e1 = 0, e2 = 0; for (int i = 12000; i < 36000; ++i) e1 += (double) y.getSample (0, i) * y.getSample (0, i);
        for (int i = 120000; i < 144000; ++i) e2 += (double) y.getSample (0, i) * y.getSample (0, i);
        std::printf ("       livello a 0,25 s e a 2,5 s: differenza %.3f dB\n", 10 * std::log10 (e2 / e1));
        check (std::abs (10 * std::log10 (e2 / e1)) < 0.05 && std::abs (20 * std::log10 (BBlendProcessor::kInputTrim) + 7.0) < 0.01,
               "ingresso -7 dB fissi, nessun guadagno dinamico", 20 * std::log10 (BBlendProcessor::kInputTrim));
    }
    // hicut mosso di continuo su un seno a 150 Hz: nessun "sparkle" (energia sopra 4 kHz trascurabile)
    {
        auto q = make();
        q->loadIr (0, cab (10.0, 2500.0, 60.0), sr, "cab A");
        q->prepareToPlay (sr, block); q->waitForBuild (5000); juce::Thread::sleep (50);
        juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m;
        double hf = 0, all = 0; float z1 = 0;
        for (int k = 0; k < 400; ++k)
        {
            setP (q->apvts, BBlendIDs::hicut, k % 2 == 0 ? 2000.0f : 19000.0f);   // salto enorme a ogni blocco
            b.clear();
            for (int i = 0; i < block; ++i) b.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 150.0 * (k * block + i) / sr));
            q->processBlock (b, m);
            if (k > 40)
                for (int i = 0; i < block; ++i) { const float x = b.getSample (0, i), d = x - z1; z1 = x; hf += (double) d * d; all += (double) x * x; }
        }
        const double ratioDb = 10.0 * std::log10 (hf / (all + 1e-20) + 1e-20);
        std::printf ("       hicut a scatti: energia della differenza prima %.1f dB rispetto al segnale\n", ratioDb);
        check (ratioDb < -26.0, "hicut: nessun click o sparkle mentre si muove (dB)", ratioDb);
    }
    // CPU e snapshot
    {
        juce::AudioBuffer<float> b (2, 128); juce::MidiBuffer m; juce::Random r (3); double t = 0;
        p->prepareToPlay (sr, 128); p->waitForBuild (5000);
        for (int k = 0; k < (int) (10 * sr / 128); ++k) { b.clear(); for (int i = 0; i < 128; ++i) b.setSample (0, i, r.nextFloat() - 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks(); p->processBlock (b, m); t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0); }
        check (t * 10 < 3.0, "CPU % di un core (mono > stereo, IR da 0,1 s)", t * 10);
        setP (p->apvts, BBlendIDs::x, 0.3f); setP (p->apvts, BBlendIDs::y, 0.65f); setP (p->apvts, BBlendIDs::air, 50.0f);
        p->prepareToPlay (sr, block); p->waitForBuild (5000);
        p->removeIr (2);   // tre IR: forma a triangolo
        p->loadIr (1, cab (17.3, 2500.0, 90.0), 44100.0, "cab B 44k");   // frequenza diversa: loader in rosso
        p->loadIr (3, cab (40.6, 2500.0, 45.0), sr, "Mesa OS 4x12 V30 SM57 cap edge 1in");   // nome lungo: si accorcia prima della x
        p->waitForBuild (5000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        for (int k = 0; k < 12; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_bblend.png"); f.deleteFile();
        juce::FileOutputStream os (f); check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_bblend.png");
        // browser di una cartella di IR: analisi in background, grafici di tutti i file
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bblend_ir_test");
            dir.deleteRecursively(); dir.getChildFile ("V30").createDirectory();
            for (int i = 0; i < 14; ++i)
            {
                auto irf = (i < 7 ? dir : dir.getChildFile ("V30")).getChildFile ("cab " + juce::String (i + 1) + " SM57 " + juce::String (800 + 300 * i) + " Hz.wav");
                auto ir = cab (10.0 + i, 800.0 + 300.0 * i, 40.0 + 4.0 * i);
                std::unique_ptr<juce::OutputStream> out (irf.createOutputStream());
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.release(), sr, 1, 24, {}, 0));
                if (w) w->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
            }
            auto* be = dynamic_cast<BBlendEditor*> (ed.get());
            be->getBrowser().setBounds (be->getLocalBounds());
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            be->getBrowser().open (0, dir);
            while (be->getBrowser().numAnalysed() < be->getBrowser().numEntries() && juce::Time::getMillisecondCounterHiRes() - t0 < 5000)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("       browser: %d IR analizzati in %.0f ms\n", be->getBrowser().numAnalysed(), ms);
            check (be->getBrowser().numEntries() == 14 && be->getBrowser().numAnalysed() == 14, "browser: tutti gli IR della cartella (anche nelle sottocartelle), con il grafico", be->getBrowser().numAnalysed());
            be->getBrowser().mouseForTest = { be->getWidth() - 24 - 8 - 372 + 150, 24 + 62 + 70 };   // tra la prima e la seconda riga: l'etichetta segue il mouse
            for (int k = 0; k < 6; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
            auto bi = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto bf = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_bblend_browser.png"); bf.deleteFile();
            juce::FileOutputStream bos (bf); check (bos.openedOk() && juce::PNGImageFormat().writeImageToStream (bi, bos), "gui_bblend_browser.png");
            be->getBrowser().close();
            check (p->getSoloSlot() == -1, "browser chiuso: torna il blend di tutti gli slot");

            // lo stesso file non entra in due slot; il percorso resta nello stato (il browser si riapre lì)
            auto f0 = dir.getChildFile ("cab 1 SM57 800 Hz.wav");
            auto q = std::make_unique<BBlendProcessor>();
            q->setRateAndBufferSizeDetails (sr, block); q->prepareToPlay (sr, block);
            q->setBrowseFolder (0, dir);
            q->setBrowseFolder (2, dir);
            q->loadIrFileAsync (2, dir.getChildFile ("V30").getChildFile ("cab 8 SM57 2900 Hz.wav"));
            const bool first = q->loadIrFileAsync (0, f0);
            const bool second = q->loadIrFileAsync (1, f0);
            for (int k = 0; k < 100 && (q->isLoading (0) || q->isLoading (2)); ++k) juce::Thread::sleep (10);
            check (first && ! second && q->slotUsing (f0) == 0, "un IR già caricato in A non si può caricare anche in B", second);
            juce::MemoryBlock st; q->getStateInformation (st);
            auto r = std::make_unique<BBlendProcessor>();
            r->setStateInformation (st.getData(), (int) st.getSize());
            check (r->getSlotFile (0) == f0, "il file dell'IR resta nel progetto (il browser si riapre nella sua cartella)");
            // il clic su uno slot caricato apre il browser del plugin nella cartella scelta (anche se l'IR è in una
            // sottocartella), senza il selettore di sistema; il file sparito: l'ultima cartella aperta
            check (r->getBrowseFolder (2) == dir && r->getBrowseFolder (0) == dir, "slot con un IR: il browser si apre nella cartella scelta, anche dopo aver riaperto il progetto");
            dir.getChildFile ("V30").deleteRecursively();
            check (r->getBrowseFolder (2) == dir && r->getLastFolder() == dir, "IR sparito: il browser si apre nell'ultima cartella");
            dir.deleteRecursively();
        }
        ed.reset();
    }
    // mentre si sceglie dalla cartella si sente solo l'IR scelto: uguale a quell'IR da solo
    {
        auto impulseResponse = [] (BBlendProcessor& p)
        {
            juce::AudioBuffer<float> b (2, 4096); b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            juce::MidiBuffer m; p.processBlock (b, m); return b;
        };
        auto both = std::make_unique<BBlendProcessor>();
        both->setRateAndBufferSizeDetails (sr, 4096); both->prepareToPlay (sr, 4096);
        both->loadIr (0, cab (12.0, 1500.0, 50.0), sr, "A");
        both->loadIr (1, cab (30.0, 4000.0, 70.0), sr, "B");
        both->setSoloSlot (1);
        both->waitForBuild (5000);
        auto one = std::make_unique<BBlendProcessor>();
        one->setRateAndBufferSizeDetails (sr, 4096); one->prepareToPlay (sr, 4096);
        one->loadIr (1, cab (30.0, 4000.0, 70.0), sr, "B");
        one->waitForBuild (5000);
        for (int k = 0; k < 40; ++k) { impulseResponse (*both); impulseResponse (*one); juce::Thread::sleep (5); }   // la convoluzione cambia IR in background
        const auto a = impulseResponse (*both), b = impulseResponse (*one);
        // stesso timbro (l'allineamento di fase può spostare l'IR di qualche campione): confronto in ampiezza
        auto mag = [] (const juce::AudioBuffer<float>& x, double hz)
        {
            double re = 0, im = 0;
            for (int i = 0; i < x.getNumSamples(); ++i) { const double ph = juce::MathConstants<double>::twoPi * hz * i / sr; re += x.getSample (0, i) * std::cos (ph); im -= x.getSample (0, i) * std::sin (ph); }
            return 10.0 * std::log10 (re * re + im * im + 1e-30);
        };
        double worst = 0;
        for (double hz : { 200.0, 800.0, 1500.0, 2500.0, 4000.0, 7000.0 }) worst = std::max (worst, std::abs (mag (a, hz) - mag (b, hz)));
        std::printf ("       solo B contro B da solo: differenza massima %.2f dB (200 Hz...7 kHz)\n", worst);
        check (worst < 0.5, "scelta dalla cartella: si sente solo l'IR scelto, senza il blend", worst);
    }
    // quattro nomi lunghi (quadrato): i nomi sopra restano dentro l'interfaccia
    {
        auto p = std::make_unique<BBlendProcessor>();
        p->setRateAndBufferSizeDetails (sr, block); p->prepareToPlay (sr, block);
        p->loadIr (0, cab (12.0, 1500.0, 50.0), sr, "British 4x12 Greenback SM57 off axis 2in");
        p->loadIr (1, cab (14.0, 2500.0, 60.0), sr, "American 2x12 Celestion V30 R121 edge");
        p->loadIr (2, cab (16.0, 3000.0, 70.0), sr, "Closed back 4x12 V30 MD421 cap centre");
        p->loadIr (3, cab (18.0, 3500.0, 80.0), sr, "Open back 1x12 Alnico Blue ribbon far");
        p->waitForBuild (5000);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
        for (int k = 0; k < 12; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_bblend_square.png"); f.deleteFile();
        juce::FileOutputStream os (f); check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_bblend_square.png");
    }
    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

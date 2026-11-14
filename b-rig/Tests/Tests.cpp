// Test di b-rig: catena di default, protezione di b-amp, bypass a 0 % CPU con crossfade senza click,
// fade-in di 80 ms all'armo, limiter a -0,5 dBFS, metronomo, tuner (spento di default), preset del rig,
// trascinamento delle card, riapertura degli editor, tutti i moduli (processo + editor) e snapshot dell'interfaccia.
#include "UI/RigComponent.h"
#include "Plugin/RigPlugin.h"
#include "smp/TestKit.h"
using namespace tk;
void rigBlendLoadTestIr (juce::AudioProcessor&);

namespace
{
constexpr int kBlock = 128;

// fa girare il motore su un segnale mono (DI) e restituisce l'uscita stereo
juce::AudioBuffer<float> runEngine (rig::RigEngine& e, int samples, const std::function<float (int)>& sig, int startIndex = 0)
{
    juce::AudioBuffer<float> out (2, samples);
    std::vector<float> in ((size_t) kBlock);
    for (int off = 0; off < samples; off += kBlock)
    {
        const int n = juce::jmin (kBlock, samples - off);
        for (int i = 0; i < n; ++i) in[(size_t) i] = sig (startIndex + off + i);
        const float* ins[1] = { in.data() };
        float* outs[2] = { out.getWritePointer (0, off), out.getWritePointer (1, off) };
        e.process (ins, 1, outs, 2, n);
    }
    return out;
}

float maxJump (const juce::AudioBuffer<float>& b, int from, int to)
{
    float m = 0;
    for (int i = juce::jmax (1, from); i < to; ++i) m = juce::jmax (m, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
    return m;
}

int indexOfId (rig::RigEngine& e, const char* id)
{
    for (int i = 0; i < e.numSlots(); ++i) if (e.slot (i).info->id == id) return i;
    return -1;
}

void savePng (juce::Component& c, const char* file)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile (file);
    f.deleteFile();
    juce::FileOutputStream os (f);
    check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), file);
}

void pump (int ms) { for (int k = 0; k < ms / 20; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // 0. engine e plugin piccoli: i buffer grandi stanno nell'heap. Questi test ne creano decine sullo stack, che su
    //    Windows è di 1 MB (8 MB su macOS); nel plugin lo stack è quello della DAW.
    check (sizeof (rig::RigEngine) < 8192, "RigEngine sta sullo stack (buffer grandi nell'heap), byte", (double) sizeof (rig::RigEngine));
    check (sizeof (rig::RigPluginProcessor) < 8192, "RigPluginProcessor sta sullo stack, byte", (double) sizeof (rig::RigPluginProcessor));

    // 1. catena di default
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        e.resetDefault();
        const bool order = e.numSlots() == 4 && e.slot (0).info->id == "drop" && e.slot (1).info->id == "amp"
                        && e.slot (2).info->id == "blend" && e.slot (3).info->id == "aftercab";
        check (order, "catena di default: drop -> amp -> blend -> aftercab", e.numSlots());
        check (! e.slot (0).enabled && ! e.slot (1).enabled && e.slot (2).enabled && e.slot (3).enabled,
               "di default: drop OFF, amp OFF, blend ON, aftercab ON");
        check (e.slot (0).proc != nullptr && e.slot (0).processedBlocks.load() == 0, "b-drop: modulo vero, spento di default (non processato)");
        auto* dist = e.addModule ("distort");
        check (dist != nullptr && e.indexOf (dist) == indexOfId (e, "amp") - 1, "b-distort entra subito prima di b-amp", e.indexOf (dist));
        e.removeSlot (e.indexOf (dist));

        // b-amp spento: il suo processBlock non viene mai chiamato (0 % CPU)
        runEngine (e, (int) sr, [] (int i) { return pluck (i, 82.4); });
        check (e.slot (1).processedBlocks.load() == 0, "b-amp spento: processBlock saltato", e.slot (1).processedBlocks.load());
        check (e.slot (2).processedBlocks.load() > 0 && e.slot (3).processedBlocks.load() > 0, "blend e aftercab accesi: processati");

        // b-amp aggiunto con un b-blend dopo di sé: si arma subito (b-blend senza IR blocca l'uscita da solo)
        auto* amp2 = e.addModule ("amp");
        check (amp2 != nullptr && amp2->enabled.load() && ! amp2->safeMuted.load(), "b-amp aggiunto con b-blend dopo: acceso");
        { rig::RigEngine q; auto* lone = q.addModule ("amp"); check (lone != nullptr && ! lone->enabled.load() && lone->safeMuted.load(), "b-amp senza b-blend dopo: safe muted"); }
        auto* tight = e.addModule ("comp");
        check (tight != nullptr && tight->enabled.load() && e.indexOf (tight) == indexOfId (e, "amp") - 1, "b-comp entra prima di b-amp (posizione logica)", e.indexOf (tight));
        auto* meter = e.addModule ("meter");
        check (e.indexOf (meter) == e.numSlots() - 1, "b-meter entra in fondo", e.indexOf (meter));
        e.moveSlot (e.indexOf (meter), -1);
        check (e.slot (e.numSlots() - 2).info->id == "meter", "freccia <: b-meter si sposta indietro");
        e.removeSlot (e.indexOf (amp2));
        e.removeSlot (e.indexOf (tight));
        e.removeSlot (e.indexOf (meter));
        runEngine (e, 4096, [] (int) { return 0.0f; });
        e.collectGarbage();
        check (e.numSlots() == 4, "rimozione con la x", e.numSlots());
    }

    // 2. crossfade di 10 ms senza click (b-distort acceso/spento su una sinusoide) e fade-in di 80 ms di b-amp
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        auto* d = e.addModule ("distort");
        auto sine = [] (int i) { return 0.3f * (float) std::sin (tp * 220.0 * i / sr); };
        runEngine (e, 24000, sine);
        const auto steadyOn = runEngine (e, 4800, sine, 24000);
        const float jOn = maxJump (steadyOn, 0, 4800);
        e.setEnabled (0, false);
        const auto sw = runEngine (e, 4800, sine, 28800);
        const float jSw = maxJump (sw, 0, 4800);
        const int before = d->processedBlocks.load();
        runEngine (e, 4800, sine, 33600);
        check (d->processedBlocks.load() == before, "dopo il crossfade il modulo spento non lavora più");
        const float jOff = maxJump (runEngine (e, 4800, sine, 38400), 0, 4800);
        check (jSw <= 1.05f * juce::jmax (jOn, jOff), "spegnimento senza click (salto max / salto a regime)", jSw / juce::jmax (jOn, jOff));
        e.setEnabled (0, true);
        const auto sw2 = runEngine (e, 4800, sine, 43200);
        check (maxJump (sw2, 0, 4800) <= 1.05f * juce::jmax (jOn, jOff), "accensione senza click", maxJump (sw2, 0, 4800) / juce::jmax (jOn, jOff));

        rig::RigEngine a;
        a.prepare (sr, 64);
        auto* amp = a.addModule ("amp");
        runEngine (a, 9600, sine);
        a.setEnabled (0, true);
        const auto arm = runEngine (a, 9600, sine, 9600);
        // differenza tra uscita e DI: cresce in ~80 ms (fade a coseno) invece che in un colpo
        auto diffRms = [&] (int from, int to) { double s = 0; for (int i = from; i < to; ++i) { const double x = arm.getSample (0, i) - sine (9600 + i); s += x * x; } return std::sqrt (s / (to - from)); };
        const double early = diffRms (0, 480), mid = diffRms (1680, 2160), late = diffRms (4800, 5280);
        check (early < 0.35 * late && mid > 0.3 * late && mid < 0.85 * late, "b-amp armato: fade-in morbido di 80 ms", early / late);
        check (amp->fade >= 1.0f, "dopo 80 ms b-amp è completamente acceso", amp->fade);
    }

    // 3. limiter di sicurezza: nulla supera -0,5 dBFS, nemmeno con un ingresso a +12 dB
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        const auto y = runEngine (e, 48000, [] (int i) { return 4.0f * (float) std::sin (tp * 110.0 * i / sr); });
        float pk = 0; for (int c = 0; c < 2; ++c) pk = juce::jmax (pk, y.getMagnitude (c, 0, 48000));
        check (pk <= rig::RigEngine::kCeiling + 1.0e-6f, "limiter: picco in uscita (dBFS)", juce::Decibels::gainToDecibels (pk));
        const auto q = runEngine (e, 48000, [] (int i) { return 0.25f * (float) std::sin (tp * 110.0 * i / sr); }, 48000);
        check (std::abs (q.getMagnitude (0, 24000, 24000) - 0.25f) < 1.0e-4f, "limiter trasparente sotto la soglia", q.getMagnitude (0, 24000, 24000));
        float nan = std::numeric_limits<float>::quiet_NaN();
        const auto z = runEngine (e, 256, [nan] (int) { return nan; });
        check (std::isfinite (z.getSample (0, 100)) && std::isfinite (z.getSample (1, 100)), "un NaN non arriva alle casse");
    }

    // 4. metronomo
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        e.bpm = 120.0f; e.sigIndex = 2; e.metroOn = true;
        const auto y = runEngine (e, (int) (sr * 2.1), [] (int) { return 0.0f; });
        std::vector<int> onsets;
        for (int i = 1; i < y.getNumSamples(); ++i) if (std::abs (y.getSample (0, i)) > 0.05f && std::abs (y.getSample (0, i - 1)) <= 0.05f && (onsets.empty() || i - onsets.back() > 4800)) onsets.push_back (i);
        check (onsets.size() == 5 && std::abs (onsets[1] - onsets[0] - 24000) < 8, "metronomo: un click ogni 0,5 s a 120 bpm", onsets.size() > 1 ? onsets[1] - onsets[0] : 0);
        e.metroOn = false;
        runEngine (e, 256, [] (int) { return 0.0f; });
        e.sigIndex = 4; e.metroOn = true;   // 6/8: click ogni croma
        const auto y8 = runEngine (e, (int) (sr * 1.05), [] (int) { return 0.0f; });
        int n8 = 0; for (int i = 1; i < y8.getNumSamples(); ++i) if (std::abs (y8.getSample (0, i)) > 0.05f && std::abs (y8.getSample (0, i - 1)) <= 0.05f) { ++n8; i += 4800; }
        check (n8 == 5, "metronomo in 6/8 a 120 bpm: un click ogni croma (0,25 s)", n8);
        // suoni e volume del metronomo
        float pk[3] = {};
        for (int snd = 0; snd < 3; ++snd)
        {
            e.metroOn = false; runEngine (e, 256, [] (int) { return 0.0f; });
            e.metroSound = snd; e.metroVolume = 1.0f; e.sigIndex = 2; e.metroOn = true;
            const auto c = runEngine (e, 4800, [] (int) { return 0.0f; });
            pk[snd] = c.getMagnitude (0, 0, 4800);
        }
        check (pk[0] > 0.3f && pk[1] > 0.1f && pk[2] > 0.3f, "metronomo: beep, hat e wood suonano", pk[1]);
        { rig::RigEngine d; check (std::abs (d.metroVolume.load() - 0.5f) < 1e-6f, "metronomo: volume di partenza 50 %"); }
        // volume: fino al 50 % lineare; al 100 % il click è molto più forte (più pieno e più lungo), sempre sotto 0 dBFS
        auto clickAt = [&] (float vol)
        {
            e.metroOn = false; runEngine (e, 256, [] (int) { return 0.0f; });
            e.metroSound = 0; e.metroVolume = vol; e.metroOn = true;
            return runEngine (e, 4800, [] (int) { return 0.0f; });
        };
        const auto half = clickAt (0.5f), quiet = clickAt (0.25f), full = clickAt (1.0f);
        check (std::abs (quiet.getMagnitude (0, 0, 4800) / half.getMagnitude (0, 0, 4800) - 0.5f) < 0.02f, "metronomo: al 25 % metà del 50 %", quiet.getMagnitude (0, 0, 4800) / half.getMagnitude (0, 0, 4800));
        const double loud = rmsDb (full, 0, 0, 4800) - rmsDb (half, 0, 0, 4800);
        std::printf ("       metronomo al 100 %%: %+.1f dB rispetto al 50 %%, picco %.3f\n", loud, full.getMagnitude (0, 0, 4800));
        check (loud > 9.0 && full.getMagnitude (0, 0, 4800) < 1.0f, "metronomo al 100 %: molto più forte del 50 %, senza superare 0 dBFS", loud);
        auto* ph = e.addModule ("aftercab");
        check (ph != nullptr, "modulo con playhead del metronomo");
    }

    // 4b. metronomo in parallelo: il limiter della chitarra non lo schiaccia (il click è uguale con o senza chitarra forte)
    {
        auto clickOf = [&] (bool metro)
        {
            rig::RigEngine e;
            e.prepare (sr, kBlock);
            e.resetDefault();
            e.setEnabled (2, false); e.setEnabled (3, false);   // niente cassa e aftercab: la DI forte va dritta al limiter
            e.metroOn = metro; e.bpm = 120.0f;
            return runEngine (e, 24000, [] (int i) { return 0.95f * (float) std::sin (tp * 110.0 * i / sr); });
        };
        auto with = clickOf (true), without = clickOf (false);
        float clickPk = 0; for (int i = 0; i < 2400; ++i) clickPk = std::max (clickPk, std::abs (with.getSample (0, i) - without.getSample (0, i)));
        rig::RigEngine q; q.prepare (sr, kBlock); q.clear(); q.metroOn = true; q.bpm = 120.0f;
        auto alone = runEngine (q, 2400, [] (int) { return 0.0f; });
        const float alonePk = alone.getMagnitude (0, 0, 2400);
        std::printf ("       click del metronomo: %.3f con la chitarra al limiter, %.3f da solo\n", clickPk, alonePk);
        check (std::abs (clickPk - alonePk) < 0.02f * alonePk + 1e-4f, "metronomo separato dalla catena: il limiter non lo tocca", clickPk / alonePk);
    }

    // 5. tuner sulla DI pulita (anche con b-amp e la catena davanti spenti)
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        e.resetDefault();
        rig::TunerAnalyser t;
        runEngine (e, 4800, [] (int i) { return 0.3f * (float) std::sin (tp * 110.0 * i / sr); });
        check (e.tunerFifo.getNumReady() == 0, "tuner spento: nessun calcolo sulla DI");
        e.tunerOn = true;
        for (int k = 0; k < 40; ++k)
        {
            runEngine (e, 1600, [] (int i) { return 0.3f * (float) std::sin (tp * 110.0 * 1.0011 * i / sr); }, k * 1600);
            t.update (e);
        }
        check (t.midi == 45, "tuner: La2 (A2) riconosciuto", t.midi);
        check (std::abs (t.cents - 1.9f) < 1.0f, "tuner: +1,9 cents", t.cents);
        // soglia: il rumore di fondo (-46 dBFS) non accende nessuna nota
        rig::TunerAnalyser q;
        juce::Random rnd (7);
        for (int k = 0; k < 40; ++k)
        {
            runEngine (e, 1600, [&rnd] (int) { return 0.005f * (rnd.nextFloat() * 2.0f - 1.0f) * 1.7f; });
            q.update (e);
        }
        check (q.midi < 0, "tuner: il rumore di fondo sotto la soglia non mostra note", q.midi);
        for (double hz : { 659.26, 82.41 })
        {
            rig::TunerAnalyser w;
            for (int k = 0; k < 12; ++k)
            {
                runEngine (e, 800, [hz] (int i) { return 0.006f * (float) std::sin (tp * hz * i / sr); }, k * 800);   // -44 dBFS: plettrata leggera
                w.update (e);
            }
            const int want = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
            check (w.midi == want, (juce::String ("tuner: nota suonata piano (-44 dBFS) riconosciuta in 200 ms, ") + juce::String (hz, 0) + " Hz").toRawUTF8(), w.midi);
        }
    }

    // 6. preset del rig: salva e ricarica (b-amp torna sempre spento)
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        e.resetDefault();
        e.setEnabled (1, true);
        e.addModule ("meter");
        auto tree = e.saveRig();
        rig::RigEngine f;
        f.prepare (sr, kBlock);
        f.loadRig (tree);
        check (f.numSlots() == 5 && f.slot (3).info->id == "aftercab" && f.slot (4).info->id == "meter", "preset del rig: stessa catena", f.numSlots());
        check (f.slot (1).enabled.load(), "preset del rig: b-amp acceso con b-blend dopo (anche senza IR)");
        check (f.slot (2).enabled.load() && f.slot (4).enabled.load(), "preset del rig: gli altri ricordano lo stato");

        // con un b-blend dopo l'ampli che ha un IR caricato, b-amp riparte già acceso dal preset
        rigBlendLoadTestIr (*e.slot (2).proc);
        rig::RigEngine g;
        g.prepare (sr, kBlock);
        g.loadRig (e.saveRig());
        check (g.slot (1).enabled.load(), "preset: b-amp con la cassa caricata dopo di sé parte acceso");
        // togliere b-blend da dopo l'ampli lo rimette muto
        g.removeSlot (2);
        check (! g.slot (1).enabled.load(), "b-blend tolto: b-amp torna safe muted");
        rig::RigEngine h;
        h.prepare (sr, kBlock);
        h.loadRig (e.saveRig());
        h.moveSlotTo (2, 0);   // b-blend spostato prima dell'ampli
        check (h.slot (0).info->id == "blend" && ! h.slot (2).enabled.load(), "b-blend spostato prima dell'ampli: b-amp torna safe muted");
        // amp acceso, cassa altrove nella catena (anche lontana) dopo di lui: resta acceso se si sposta altro
        rig::RigEngine m;
        m.prepare (sr, kBlock);
        m.loadRig (e.saveRig());
        m.moveSlotTo (3, 4);
        check (m.slot (1).enabled.load(), "spostando un altro modulo b-amp resta acceso (la cassa è ancora dopo)");
    }

    // 7. tutti i moduli: processo, editor (dimensioni) e CPU della catena
    {
        rig::RigEngine e;
        e.prepare (sr, kBlock);
        int ok = 0, total = 0;
        for (auto& m : rig::modules())
        {
            if (! m.available()) continue;
            ++total;
            auto p = m.make();
            p->setRateAndBufferSizeDetails (sr, kBlock);
            p->prepareToPlay (sr, kBlock);
            juce::AudioBuffer<float> b (juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), kBlock);
            juce::MidiBuffer mb;
            bool finite = true;
            for (int k = 0; k < 40; ++k)
            {
                for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < kBlock; ++i) b.setSample (c, i, pluck (k * kBlock + i, 110.0));
                p->processBlock (b, mb);
                for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < kBlock; ++i) finite &= std::isfinite (b.getSample (c, i));
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
            std::printf ("       %-12s %d in / %d out, editor %d x %d\n", m.name.toRawUTF8(), p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(),
                         ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
            ed.reset();
            if (finite) ++ok;
        }
        check (ok == total && total == 16, "i 16 moduli della suite compilati dentro b-rig, uscita finita", ok);

        e.resetDefault();
        e.setEnabled (1, true);
        const auto t0 = juce::Time::getHighResolutionTicks();
        runEngine (e, (int) sr * 4, [] (int i) { return pluck (i, 82.4); });
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        check (secs < 4.0, "CPU % di un core: catena di default con b-amp armato", 100.0 * secs / 4.0);
    }

    // 8. interfaccia (nell'ordine dell'app: prima la catena, poi il dispositivo audio)
    {
        rig::RigEngine e;
        e.resetDefault();
        e.prepare (sr, kBlock);
        rig::RigComponent ui (e, nullptr);
        ui.setSize (1180, 760);
        check (ui.getSelected() != nullptr && ui.getSelected()->info->id == "amp", "all'avvio il modulo al centro è b-amp");
        check (ui.getEditor() != nullptr, "editor di b-amp nel workspace");
        { juce::String rep; check (ui::checkFixedValueBoxes (ui, rep), ("riquadri a larghezza fissa " + rep).toRawUTF8()); }
        check (! ui.isTunerActive() && ! e.globalMute.load(), "tuner spento di default, uscita libera");
        // ogni modulo si riapre dopo essere passati ad altri (prima restava "coming soon")
        bool reopen = true;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 1; i < e.numSlots(); ++i) { ui.select (&e.slot (i)); reopen &= ui.getEditor() != nullptr; }
        check (reopen, "gli editor della catena di default si riaprono ogni volta");
        ui.select (&e.slot (1));
        pump (300);
        savePng (ui, "gui_brig_default.png");

        // b-blend dopo un ampli (a qualunque distanza): non si spegne, si può solo togliere o spostare
        const int bi = indexOfId (e, "blend");
        ui.requestEnabled (bi, false);
        check (e.slot (bi).enabled.load(), "b-blend dopo b-amp: non si spegne");
        e.moveSlotTo (bi, 0);
        ui.requestEnabled (0, false);
        check (! e.slot (0).enabled.load(), "b-blend prima dell'ampli: si spegne");
        ui.requestEnabled (0, true);
        e.moveSlotTo (0, bi);

        ui.setTunerActive (true);
        check (ui.isTunerActive() && e.globalMute.load(), "tuner premuto: si accende col mute");
        for (int k = 0; k < 20; ++k)
        {
            runEngine (e, 1600, [] (int i) { return 0.3f * (float) std::sin (tp * 82.0 * i / sr); }, k * 1600);
            ui.tickForTest();
        }
        pump (200);
        savePng (ui, "gui_brig_tuner.png");
        ui.setTunerActive (false);
        check (! ui.isTunerActive() && ! e.globalMute.load(), "tuner chiuso: niente mute");

        ui.select (&e.slot (2));
        pump (300);
        savePng (ui, "gui_brig_blend.png");
        ui.addFromLibrary ("comp");
        ui.addFromLibrary ("focus");
        ui.addFromLibrary ("meter");
        check (e.numSlots() == 7, "aggiunta dalla libreria", e.numSlots());
        // trascinamento: b-meter (ultimo) al primo posto
        ui.dragCardForTest (e.numSlots() - 1, 0);
        check (e.slot (0).info->id == "meter", "trascinamento: b-meter in testa alla catena");
        ui.dragCardForTest (0, e.numSlots());
        check (e.slot (e.numSlots() - 1).info->id == "meter", "trascinamento: e di nuovo in fondo");
        pump (300);
        savePng (ui, "gui_brig_long_chain.png");

        // preset: scrittura e lettura di un file (in una cartella temporanea, non tra i preset dell'utente)
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("brig_test.brig");
        check (ui.writeRig (tmp), "preset: esportazione su file");
        rig::RigEngine f; f.prepare (sr, kBlock);
        check (rig::RigComponent::loadPresetFile (f, tmp) && f.numSlots() == 7 && f.slot (6).info->id == "meter", "preset: importazione dal file", f.numSlots());
        tmp.deleteFile();

        // stato "unsaved": una modifica fatta dall'utente fa comparire save; save crea "User N" e apre il nome
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("brig_presets_test");
            dir.deleteRecursively(); dir.createDirectory();
            rig::RigComponent::presetDirOverride() = dir;
            check (rig::RigComponent::startupPreset() == "clean slate", "prima installazione: si parte da clean slate, nessun preset");
            check (rig::RigComponent::listPresets().size() == 1, "prima installazione: solo clean slate nell'elenco", (int) rig::RigComponent::listPresets().size());
            ui.loadPreset ("clean slate");
            e.resetDefault();
            ui.clearDirty();
            check (! ui.dirty, "preset appena caricato: niente save");
            check (! ui.saveEnabledForTest(), "preset non modificato: save e new spenti");
            juce::Thread::sleep (900);   // passato il tempo in cui il caricamento non conta come modifica
            ui.paramTouchedForTest();
            check (ui.dirty, "un parametro cambiato dall'utente: stato unsaved");
            pump (100);
            savePng (ui, "gui_brig_unsaved.png");
            ui.saveFromTopBar();
            check (! ui.dirty && ui.currentPreset == "User 1" && dir.getChildFile ("User 1.brig").existsAsFile(), "save: nuovo preset User 1", ui.dirty);
            check (ui.isPresetRenaming(), "save: il nome del nuovo preset è già selezionato");
            pump (100);
            savePng (ui, "gui_brig_presets.png");
            ui.renamePresetForTest ("crunch rig");
            check (ui.currentPreset == "crunch rig" && dir.getChildFile ("crunch rig.brig").existsAsFile() && ! dir.getChildFile ("User 1.brig").exists(), "rinomina dalla schermata dei preset");
            juce::Thread::sleep (900);
            ui.paramTouchedForTest(); ui.saveFromTopBar();
            check (ui.currentPreset == "User 1", "il numero libero successivo: User 1 di nuovo (rinominato)", 0);
            juce::Thread::sleep (20);
            juce::Thread::sleep (900); ui.paramTouchedForTest(); ui.saveFromTopBar();   // User 2
            juce::Thread::sleep (900); ui.paramTouchedForTest(); ui.saveFromTopBar();   // User 3
            juce::Thread::sleep (900); ui.paramTouchedForTest(); ui.saveFromTopBar();   // User 4
            auto list = ui.presetEntriesForTest();
            juce::StringArray names; for (auto& e : list) names.add (e.name);
            std::printf ("       preset: %s\n", names.joinIntoString (", ").toRawUTF8());
            check (names[0] == "clean slate" && list[0].colour == juce::Colours::white, "clean slate in cima, bianco");
            check (names[1] == "User 4" && names[2] == "User 3" && names[3] == "User 2", "preset in ordine di creazione, l'ultimo in cima");
            bool spaced = true;
            for (size_t k = 1; k < list.size(); ++k)
                for (size_t d = 1; d <= 2 && k + d < list.size(); ++d) spaced &= list[k].colour != list[k + d].colour;
            check (spaced, "colori: mai uguali a meno di due preset di distanza");
            // un clic seleziona soltanto; il doppio clic carica
            const int u2 = names.indexOf ("User 2");
            ui.selectPresetForTest (u2);
            check (ui.currentPreset == "User 4", "un clic: il preset è selezionato ma non caricato");
            ui.loadPresetRowForTest (0);
            check (ui.currentPreset == "clean slate" && e.numSlots() == 0, "clean slate: catena vuota", e.numSlots());
            ui.undoPresetForTest();
            check (ui.currentPreset == "User 4" && e.numSlots() > 0, "undo: torna il rig di prima", e.numSlots());
            // elimina + undo
            ui.deletePresetForTest (u2);
            check (! dir.getChildFile ("User 2.brig").exists(), "x: preset eliminato");
            ui.undoPresetForTest();
            check (dir.getChildFile ("User 2.brig").existsAsFile(), "undo: preset ripristinato");
            // save as new = copia del preset selezionato
            ui.selectPresetForTest (ui.presetNamesForTest().indexOf ("User 2"));
            ui.duplicatePresetForTest();
            check (dir.getChildFile ("User 5.brig").existsAsFile() && ui.presetNamesForTest()[1] == "User 5", "save as new: copia del selezionato, in cima");
            // preset d'avvio: si sceglie dalla lista, segue la rinomina; eliminato, si parte da clean slate (undo lo riporta)
            check (rig::RigComponent::startupPreset() == "clean slate", "all'inizio si carica clean slate (la catena vuota)");
            ui.setStartupForTest (ui.presetNamesForTest().indexOf ("User 3"));
            check (rig::RigComponent::startupPreset() == "User 3", "load at start-up: User 3 si caricherà all'avvio");
            ui.selectPresetForTest (ui.presetNamesForTest().indexOf ("User 3"));
            ui.showPresets();
            pump (60);
            {
                rig::RigEngine boot; boot.prepare (sr, kBlock);
                rig::RigComponent::loadStartup (boot);
                rig::RigEngine ref; ref.prepare (sr, kBlock);
                rig::RigComponent::loadPresetFile (ref, dir.getChildFile ("User 3.brig"));
                check (boot.numSlots() == ref.numSlots(), "all'avvio si carica il preset scelto", boot.numSlots());
            }
            ui.deletePresetForTest (ui.presetNamesForTest().indexOf ("User 3"));
            check (rig::RigComponent::startupPreset() == "clean slate", "preset d'avvio eliminato: si parte da clean slate");
            ui.undoPresetForTest();
            check (rig::RigComponent::startupPreset() == "User 3", "undo: torna anche il preset d'avvio");
            // anche "default" e i preset già salvati si rinominano e si eliminano
            check (rig::RigComponent::renamePreset ("User 3", "lead"), "rinomina di un preset salvato");
            check (rig::RigComponent::startupPreset() == "lead", "il preset d'avvio segue il nome nuovo");
            ui.showPresets();
            pump (100);
            savePng (ui, "gui_brig_presets_list.png");
            dir.deleteRecursively();
            rig::RigComponent::presetDirOverride() = juce::File();
        }


        // un solo modulo per tipo: il secondo clic su + non aggiunge nulla
        {
            ui.addFromLibrary ("meter");
            ui.addFromLibrary ("meter");
            int meters = 0; for (int i = 0; i < e.numSlots(); ++i) meters += e.slot (i).info->id == "meter" ? 1 : 0;
            check (meters == 1, "un solo modulo per tipo nella catena", meters);
        }
        // undo / redo del rig: togliere un modulo, undo lo rimette, redo lo ritoglie
        {
            ui.commitHistory();
            const int n0 = e.numSlots();
            e.removeSlot (e.numSlots() - 1);
            ui.commitHistory();
            ui.undoRig();
            const int afterUndo = e.numSlots();
            ui.redoRig();
            check (afterUndo == n0 && e.numSlots() == n0 - 1, "undo / redo: il modulo tolto torna e poi va via di nuovo", afterUndo);
            ui.undoRig();
        }
        // b-blend: senza IR resta acceso per forza dopo l'ampli; con un IR che funziona si può spegnere, e b-amp in
        // safe mute si riattiva da solo appena l'IR è caricato
        {
            ui.loadPreset ("clean slate");
            e.addModule ("amp"); e.addModule ("blend");
            const int ia = indexOfId (e, "amp"), ib = indexOfId (e, "blend");
            ui.checkCabs();
            const bool lockedEmpty = rig::RigComponent::cabProtects (e, ib);
            check (e.slot (ia).enabled.load() && lockedEmpty, "b-blend senza IR dopo l'ampli: bloccato acceso, b-amp armato");
            rigBlendLoadTestIr (*e.slot (ib).proc);
            ui.checkCabs();
            check (e.slot (ia).enabled.load(), "IR caricato in b-blend: b-amp si riattiva da solo");
            check (rig::RigComponent::cabProtects (e, ib), "b-blend dopo l'ampli resta bloccato acceso anche con un IR");
            // la x chiede conferma: annullando resta, confermando si toglie
            const int n0 = e.numSlots();
            ui.requestRemove (ib);
            check (ui.confirmVisibleForTest() && e.numSlots() == n0, "togliere b-blend dopo l'ampli chiede conferma");
            ui.closeScreensForTest();
            pump (60);
            savePng (ui, "gui_brig_confirm_remove.png");
            ui.confirmForTest();
            check (e.numSlots() == n0 - 1 && indexOfId (e, "blend") < 0, "confermato: b-blend tolto", e.numSlots());
        }
        // tuner: se l'utente sceglie di non mutare, la volta dopo il tuner parte senza mute
        {
            ui.setTunerActive (true);
            const bool mutedByDefault = e.globalMute.load();
            ui.setTunerActive (false);
            e.tunerMutes = false;
            ui.setTunerActive (true);
            check (mutedByDefault && ! e.globalMute.load(), "tuner: la scelta del mute resta", e.globalMute.load());
            ui.setTunerActive (false);
            e.tunerMutes = true;
        }
        // clean slate e poi un modulo: l'interfaccia non deve toccare i moduli tolti e già liberati (prima si chiudeva)
        {
            ui.loadPreset ("clean slate");
            e.addModule ("amp"); e.addModule ("delay"); e.addModule ("verb");
            ui.tickForTest();
            ui.loadPreset ("clean slate");
            for (int k = 0; k < 3; ++k) { ui.tickForTest(); e.collectGarbage(); pump (20); }
            ui.addFromLibrary ("comp");
            ui.addFromLibrary ("delay");
            ui.tickForTest();
            check (e.numSlots() == 2, "clean slate, poi due moduli dalla libreria: nessun crash", e.numSlots());
        }
        // b-amp spento dall'utente: solo "off". Muto per protezione (cassa tolta): "safe muted"
        {
            ui.loadPreset ("clean slate");
            e.addModule ("amp");
            check (e.slot (indexOfId (e, "amp")).safeMuted.load(), "b-amp appena aggiunto, senza cassa: safe muted");
            e.addModule ("blend");
            const int ia = indexOfId (e, "amp");
            check (e.slot (ia).enabled.load() && ! e.slot (ia).safeMuted.load(), "b-blend messo dopo l'ampli: b-amp si arma");
            ui.requestEnabled (ia, false);
            ui.requestEnabled (ia, true);
            check (e.slot (ia).enabled.load() && ! e.slot (ia).safeMuted.load(), "armato a mano: acceso");
            ui.requestEnabled (ia, false);
            check (! e.slot (ia).enabled.load() && ! e.slot (ia).safeMuted.load(), "spento dall'utente: solo off, niente safe muted");
            rigBlendLoadTestIr (*e.slot (indexOfId (e, "blend")).proc);
            ui.checkCabs(); ui.checkCabs();
            check (! e.slot (ia).enabled.load(), "spento dall'utente: un IR caricato non lo riaccende");
            ui.requestEnabled (ia, true);
            e.removeSlot (indexOfId (e, "blend"));
            check (! e.slot (ia).enabled.load() && e.slot (ia).safeMuted.load(), "b-blend tolto dopo l'ampli acceso: safe muted");
            pump (60);
            savePng (ui, "gui_brig_safe_muted.png");
        }
        // libreria: un modulo già nella catena ha un - che lo toglie
        {
            ui.loadPreset ("clean slate");
            ui.addFromLibrary ("comp");
            check (ui.inChain ("comp"), "tight nella catena");
            ui.requestRemove (ui.viewIndexOf ("comp"));
            check (! ui.inChain ("comp") && e.numSlots() == 0, "il - della libreria lo toglie");
        }
        // split: due rami paralleli. Si divide dalla DI con B copia di A (il suono non cambia); il punto di split si
        // sposta; ogni ramo ha i suoi moduli (uno per tipo per ramo); wide = A a sinistra, B a destra
        {
            ui.loadPreset ("clean slate");
            ui.addFromLibrary ("comp"); ui.addFromLibrary ("distort");
            ui.toggleSplit();
            check (e.isSplit() && e.laneSize (0) == 0 && e.laneSize (1) == 2 && e.laneSize (2) == 2, "split: A = la catena, B = una copia", e.laneSize (2));
            check (e.wide.load() && ! e.widen.load(), "split: stereo acceso di default, widen spento");
            ui.addFromLibrary ("delay");
            check (e.laneHas (1, "delay") && ! e.laneHas (2, "delay"), "dalla libreria: nel ramo che si sta guardando");
            ui.showLane (2);
            check (! ui.inChain ("delay") && ui.inChain ("distort"), "ramo B: b-delay non c'è, b-distort sì");
            ui.addFromLibrary ("verb");
            check (e.laneHas (2, "verb") && ! e.laneHas (1, "verb"), "ramo B: b-verb solo qui");
            e.moveSplit (2, 1);
            check (e.laneSize (0) == 1 && e.slot (0).info->id == "comp" && ! e.laneHas (1, "comp") && ! e.laneHas (2, "comp"), "punto di split spostato dopo b-comp: in comune");
            check (! e.canPlace ("comp", 1), "b-comp in comune: non può stare anche in un ramo");
            e.moveSplit (2, 0);
            check (e.laneSize (0) == 0 && e.laneHas (1, "comp") && e.laneHas (2, "comp"), "punto di split di nuovo alla DI: b-comp in tutti e due i rami");
            e.moveSplit (2, 1);   // b-comp in comune, poi i due rami
            pump (60);
            savePng (ui, "gui_brig_split.png");   // ramo b
            const auto saved = e.saveRig();
            rig::RigEngine copy; copy.loadRig (saved);
            check (copy.isSplit() && copy.laneSize (1) == e.laneSize (1) && copy.laneSize (2) == e.laneSize (2), "preset con lo split: rami uguali");
            ui.showLane (1);
            pump (60);
            savePng (ui, "gui_brig_split_a.png");
            ui.toggleSplit();
            check (ui.confirmVisibleForTest(), "riunire due rami diversi chiede conferma");
            ui.confirmForTest();
            check (! e.isSplit() && e.laneHas (0, "delay") && ! e.laneHas (0, "verb"), "riuniti: resta il ramo A");
        }
        // trascinamento con lo split: la catena espansa mostra i due rami uno sopra l'altro e un modulo passa da un
        // ramo all'altro; ogni ramo ha i suoi moduli e la libreria segue il ramo che si vede; un modulo nella parte
        // comune non entra in un ramo (e viceversa). Il tasto copia mette un modulo anche nell'altro ramo.
        {
            ui.loadPreset ("clean slate");
            ui.addFromLibrary ("comp"); ui.addFromLibrary ("distort");
            ui.toggleSplit();
            e.moveSplit (1, 1);   // b-comp in comune, b-distort in tutti e due i rami
            ui.showLane (1);
            ui.addFromLibrary ("delay");
            check (ui.inChain ("delay") && e.laneHas (1, "delay"), "ramo a: b-delay");
            check (ui.canCopy (indexOfId (e, "delay")), "b-delay solo nel ramo a: si può copiare nel ramo b");
            check (! ui.canCopy (indexOfId (e, "comp")), "b-comp nella parte comune: niente tasto copia");
            check (! ui.canCopy (e.laneStart (1)), "b-distort già nell'altro ramo: niente tasto copia");
            const auto stereoAt = ui.stereoBoundsForTest();
            ui.setExpanded (true);
            check (ui.expanded, "catena espansa con lo split");
            ui.showLane (2); ui.showLane (1);
            check (ui.stereoBoundsForTest() == stereoAt, "catena espansa, cambiando ramo: stereo e balance restano dove sono");
            pump (60);
            savePng (ui, "gui_brig_expanded.png");
            // a metà del trascinamento: b-delay dalla riga del ramo a verso la riga del ramo b
            ui.previewDragForTest (indexOfId (e, "delay"), ui.chainXForTest (600), ui.rowYForTest (1));
            pump (40); savePng (ui, "gui_brig_expanded_drag.png");
            ui.cancelDragForTest();
            const bool moved = ui.dropToForTest (indexOfId (e, "delay"), 2, 0);
            check (moved && e.laneHas (2, "delay") && ! e.laneHas (1, "delay") && ui.viewLane == 2, "catena espansa: b-delay passa nel ramo b");
            check (ui.collapseOnClick (ui.workspaceForTest().getCentre().translated (0, 200)) && ! ui.expanded, "catena espansa: un clic sul modulo sotto la richiude");
            check (ui.stereoBoundsForTest() == stereoAt, "richiusa: stereo e balance dove erano");
            ui.setExpanded (false);
            ui.showLane (1);
            check (! ui.inChain ("delay") && ui.inChain ("distort") && ui.inChain ("comp"), "ramo a: b-delay di nuovo libero in libreria, b-comp in comune");
            ui.showLane (2);
            check (ui.inChain ("delay"), "ramo b: b-delay già presente");
            // copia nel ramo a, con il suo stato
            {
                auto& d = e.slot (indexOfId (e, "delay"));
                d.proc->getParameters()[0]->setValueNotifyingHost (0.37f);
                const int n0 = e.numSlots();
                ui.copyToOtherLane (e.indexOf (&d));
                int copyIdx = -1; for (int i = e.laneStart (1); i < e.laneStart (1) + e.laneSize (1); ++i) if (e.slot (i).info->id == "delay") copyIdx = i;
                const float want = d.proc->getParameters()[0]->getValue();   // il valore com'è dopo l'arrotondamento del parametro
                check (e.numSlots() == n0 + 1 && copyIdx >= 0 && std::abs (e.slot (copyIdx).proc->getParameters()[0]->getValue() - want) < 1.0e-4f
                       && std::abs (want - juce::jlimit (0.0f, 1.0f, want)) < 1.0e-6f && std::abs (want - d.proc->getParameters()[0]->getDefaultValue()) > 1.0e-6f,
                       "copia: b-delay anche nel ramo a, con gli stessi valori");
                check (! ui.canCopy (copyIdx), "dopo la copia: il tasto copia sparisce (l'altro ramo ce l'ha)");
                e.removeSlot (copyIdx);
            }
            // b-distort (in tutti e due i rami) non può andare nella parte comune
            const int n0 = e.numSlots();
            const auto vb = ui.viewSlots();
            int dist = -1; for (int d = 0; d < (int) vb.size(); ++d) if (e.slot (vb[(size_t) d]).info->id == "distort") dist = d;
            ui.dragCardForTest (dist, 0);
            check (e.numSlots() == n0 && ! e.laneHas (0, "distort") && e.laneHas (1, "distort") && e.laneHas (2, "distort"), "b-distort nei due rami: non entra nella parte comune");
            // b-delay (solo nel ramo b) nella parte comune: va bene
            const auto vc = ui.viewSlots();
            int del = -1; for (int d = 0; d < (int) vc.size(); ++d) if (e.slot (vc[(size_t) d]).info->id == "delay") del = d;
            ui.dragCardForTest (del, 1);
            check (e.laneHas (0, "delay") && ! e.laneHas (2, "delay"), "b-delay solo in un ramo: entra nella parte comune");
            ui.showLane (1);
            check (ui.inChain ("delay"), "in comune: si vede anche nel ramo a");
            ui.showLane (2);
            pump (60);
            savePng (ui, "gui_brig_split_drag.png");
            // a metà del trascinamento: nel ramo (b-distort verso la fine del ramo b) e nella parte comune (non può)
            ui.previewDragForTest (indexOfId (e, "distort") >= 0 ? e.laneStart (2) : 0, ui.chainXForTest (900), ui.rowYForTest (0));
            pump (40); savePng (ui, "gui_brig_drag_lane.png");
            ui.previewDragForTest (e.laneStart (2), ui.chainXForTest (5), ui.rowYForTest (0));
            pump (40); savePng (ui, "gui_brig_drag_shared.png");
            ui.cancelDragForTest();
            // il + della libreria: passandoci sopra si vede dove entra il modulo
            const auto tgt = ui.libraryTarget ("verb");
            check (tgt.lane == 2, "il + con lo split: nel ramo che si vede", tgt.lane);
            ui.hoverForTest (ui.libAddCentreForTest ("verb"));
            pump (40); savePng (ui, "gui_brig_library_hover.png");
            ui.hoverForTest ({ -1, -1 });
            // balance mentre misura: il tasto si riempie da sinistra quanto la registrazione è avanti
            e.startMeasure (rig::RigEngine::kBalance);
            runEngine (e, 12000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
            const float prog = e.measureProgress();
            check (prog > 0.2f && prog < 0.5f, "balance: avanzamento della misura", prog);
            pump (40); savePng (ui, "gui_brig_balance_progress.png");
            e.cancelMeasure();
            ui.setExpanded (true);
            ui.toggleSplit();
            if (ui.confirmVisibleForTest()) ui.confirmForTest();
            check (! e.isSplit() && ! ui.expanded, "riuniti i rami: la catena torna a una riga");
        }
        // il suono: rami uguali = catena unica; wide = A a sinistra e B a destra; ampli prima dello split coperto
        // solo con una cassa in entrambi i rami
        {
            auto build = [] (rig::RigEngine& x) { x.prepare (sr, kBlock); x.addModule ("comp"); x.addModule ("distort"); };
            auto sig = [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr) * (float) std::exp (-(i % 24000) / 9000.0); };
            rig::RigEngine one; build (one);
            rig::RigEngine two; build (two); two.splitChain();
            const auto a = runEngine (one, 24000, sig), b = runEngine (two, 24000, sig);
            float diff = 0; for (int i = 0; i < 24000; ++i) diff = juce::jmax (diff, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            check (diff < 1.0e-4f, "split con B copia di A: stesso suono della catena unica", diff);
            two.removeSlot (two.laneStart (2) + 1);   // b-distort solo nel ramo A
            two.wide = true;
            const auto w = runEngine (two, 24000, sig);
            rig::RigEngine onlyComp; onlyComp.prepare (sr, kBlock); onlyComp.addModule ("comp");
            const auto t = runEngine (onlyComp, 24000, sig);
            float dl = 0, dr = 0;
            for (int i = 4800; i < 24000; ++i) { dl = juce::jmax (dl, std::abs (w.getSample (0, i) - a.getSample (0, i))); dr = juce::jmax (dr, std::abs (w.getSample (1, i) - t.getSample (1, i))); }
            check (dl < 1.0e-3f && dr < 1.0e-3f, "wide: A (con b-distort) a sinistra, B (senza) a destra", juce::jmax (dl, dr));
            rig::RigEngine c; c.prepare (sr, kBlock); c.addModule ("amp"); c.splitChain(); c.moveSplit (1, 1);
            c.addModuleAt ("blend", 1, 0);
            check (! c.cabAfter (0, false), "ampli prima dello split, cassa solo in A: non coperto");
            c.addModuleAt ("blend", 2, 0);
            check (c.cabAfter (0, false), "cassa in entrambi i rami: coperto");
        }
        // widen: il ramo b in ritardo del tempo misurato. Con due rami uguali e una nota a 110 Hz il tempo migliore è un
        // multiplo del periodo (9,09 ms): sommati in mono non si cancellano
        {
            rig::RigEngine w; w.prepare (sr, kBlock); w.addModule ("comp"); w.splitChain();
            auto note = [] (int i) { double v = 0; for (int h = 1; h <= 6; ++h) v += std::sin (tp * 110.0 * h * i / sr) / h; return (float) (0.15 * v); };
            w.startWidenMeasure();
            int k = 0;
            while (! w.pollWiden() && k < 40) { runEngine (w, 4800, note, k * 4800); ++k; }
            const float ms = w.widenMs.load();
            const double period = 1000.0 / 110.0, frac = std::fmod ((double) ms, period) / period;
            std::printf ("       widen: %.2f ms (periodo 9.09 ms), misura in %d blocchi da 100 ms\n", ms, k);
            check (w.widen.load() && ms >= 5.0f && ms <= 25.0f && (frac < 0.03 || frac > 0.97), "widen: ritardo in un multiplo del periodo (nessuna cancellazione)", ms);
            auto y = runEngine (w, 48000, note, 0);
            w.wide = false;   // somma mono
            auto m = runEngine (w, 48000, note, 48000);
            rig::RigEngine one; one.prepare (sr, kBlock); one.addModule ("comp");
            auto ref = runEngine (one, 96000, note, 0);
            const double lm = rmsDb (m, 0, 24000, 48000), lr = rmsDb (ref, 0, 72000, 96000);
            std::printf ("       widen in mono: %.2f dB rispetto alla catena unica\n", lm - lr);
            check (std::abs (lm - lr) < 1.0, "widen sommato in mono: stesso livello della catena unica", lm - lr);
            juce::ignoreUnused (y);
            const auto t = w.saveRig();
            rig::RigEngine r2; r2.loadRig (t);
            check (r2.widen.load() && std::abs (r2.widenMs.load() - ms) < 0.01f, "widen e il suo tempo si salvano nel preset");
        }
        // save aggiorna il preset caricato (nessun preset nuovo); la sessione si riapre com'era, anche non salvata
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("brig_session_test");
            dir.deleteRecursively(); dir.createDirectory();
            rig::RigComponent::presetDirOverride() = dir.getChildFile ("Presets");
            ui.loadPreset ("clean slate");
            ui.addFromLibrary ("delay");
            juce::Thread::sleep (900); ui.paramTouchedForTest();
            check (ui.dirty && ui.saveEnabledForTest(), "modificato: save e new accesi");
            ui.saveFromTopBar();   // new: "User 1"
            ui.closeScreensForTest();
            const auto before = rig::RigComponent::listPresets().size();
            ui.addFromLibrary ("verb");
            juce::Thread::sleep (900); ui.paramTouchedForTest();
            ui.saveCurrent();
            check (rig::RigComponent::listPresets().size() == before && ui.currentPreset == "User 1" && ! ui.dirty, "save: aggiorna User 1, nessun preset nuovo");
            rig::RigEngine chk; rig::RigComponent::loadPresetFile (chk, rig::RigComponent::presetFile ("User 1"));
            check (chk.numSlots() == 2, "User 1 salvato con b-verb", chk.numSlots());
            ui.addFromLibrary ("space");
            juce::Thread::sleep (900); ui.paramTouchedForTest();
            ui.saveSession();
            rig::RigEngine boot; rig::RigComponent::loadStartup (boot);
            check (boot.numSlots() == 3 && rig::RigComponent::bootPreset() == "User 1" && rig::RigComponent::bootDirty(), "sessione: si riapre com'era (User 1, con b-space non salvato)", boot.numSlots());
            rig::RigComponent::bootPreset() = {}; rig::RigComponent::bootDirty() = false;
            dir.deleteRecursively();
        }
        // balance: due rami con livelli diversi e b in controfase si pareggiano e si allineano
        {
            rig::RigEngine b; b.prepare (sr, kBlock); b.addModule ("comp"); b.splitChain();
            juce::Random rnd (4);
            std::vector<float> nz (96000); for (auto& v : nz) v = 0.2f * (rnd.nextFloat() - 0.5f);
            std::vector<float> a ((size_t) (0.75 * sr)), bb ((size_t) (0.75 * sr) + (size_t) (0.026 * sr));
            const int lead = (int) (0.026 * sr), lag = 48;   // b 1 ms in ritardo, metà volume, invertito
            for (size_t i = 0; i < bb.size(); ++i) bb[i] = -0.5f * nz[i + 1000 - (size_t) lag];
            for (size_t i = 0; i < a.size(); ++i) a[i] = nz[i + 1000 + (size_t) lead];
            const auto r = rig::RigEngine::measureBalance (a.data(), bb.data(), (int) a.size(), sr);
            std::printf ("       balance: ritardo %.2f ms, a %.2f, b %.2f\n", r.alignMs, r.gainA, r.gainB);
            check (std::abs (r.alignMs + 1.0f) < 0.05f && r.gainB < 0.0f && std::abs (std::abs (r.gainB) / r.gainA - 2.0f) < 0.05f, "balance: b arriva 1 ms dopo, invertito e più piano: a ritardato di 1 ms, b invertito, volumi pari");
        }
        // tasti dell'app: m e k accendono e spengono il metronomo; l batte il tempo, che cambia dal quarto tocco
        {
            const bool m0 = e.metroOn.load();
            ui.keyPressed (juce::KeyPress ('m', 0, 'm'));
            check (e.metroOn.load() != m0, "m: metronomo acceso o spento");
            ui.keyPressed (juce::KeyPress ('k', 0, 'k'));
            check (e.metroOn.load() == m0, "k: anche");
            const float bpm0 = ui.controls.get ("bpm");
            for (int k = 0; k < 3; ++k) { ui.keyPressed (juce::KeyPress ('l', 0, 'l')); juce::Thread::sleep (500); }
            check (std::abs (ui.controls.get ("bpm") - bpm0) < 0.01f, "l tre volte: il tempo non cambia ancora");
            ui.keyPressed (juce::KeyPress ('l', 0, 'l'));
            const float bpm1 = ui.controls.get ("bpm");
            std::printf ("       tap con l, 500 ms tra i tocchi: %.0f bpm\n", bpm1);
            check (std::abs (bpm1 - 120.0f) <= 3.0f, "l al quarto tocco: 120 bpm", bpm1);
        }
        // preset modificato: il nome resta (in giallo), non diventa "unsaved"
        {
            ui.setPresetState ("crunch rig", true);
            check (ui.currentPreset == "crunch rig" && ui.dirty, "modificato: resta il nome del preset");
            pump (60);
            savePng (ui, "gui_brig_dirty_name.png");
            ui.setPresetState ("clean slate", false);
        }
        // primo avvio
        ui.showWelcome();
        pump (100);
        check (ui.welcomeVisibleForTest(), "primo avvio: la finestra sugli IR");
        savePng (ui, "gui_brig_welcome.png");
        ui.closeScreensForTest();
        ui.showInfo ("comp");
        pump (100);
        savePng (ui, "gui_brig_info.png");
        ui.setSize (1600, 1000);
        ui.select (&e.slot (e.numSlots() - 1));
        pump (300);
        savePng (ui, "gui_brig_large.png");
    }

    // 9. b-rig come plugin: mono -> stereo, il tempo della DAW ai moduli, la catena e il preset nel progetto
    auto plugDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("brig_plugin_test");
    plugDir.deleteRecursively(); plugDir.createDirectory();
    rig::RigComponent::presetDirOverride() = plugDir.getChildFile ("Presets");   // mai i file veri dell'utente
    {
        struct Head final : juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm (87.0); p.setIsPlaying (true); return p; }
        } head;
        rig::RigPluginProcessor p;
        check (p.getTotalNumInputChannels() == 1 && p.getTotalNumOutputChannels() == 2, "plugin: mono in ingresso, stereo in uscita");
        juce::AudioProcessor::BusesLayout st; st.inputBuses.add (juce::AudioChannelSet::stereo()); st.outputBuses.add (juce::AudioChannelSet::stereo());
        juce::AudioProcessor::BusesLayout mm; mm.inputBuses.add (juce::AudioChannelSet::mono()); mm.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.checkBusesLayoutSupported (st) && ! p.checkBusesLayoutSupported (mm), "plugin: stereo in ingresso accettato (sommato), uscita sempre stereo");
        p.setPlayHead (&head);
        p.prepareToPlay (sr, 256);
        p.engine.addModule ("delay");
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double energy = 0;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i) buf.setSample (0, i, 0.3f * (float) std::sin (tp * 220.0 * (b * 256 + i) / sr));
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i) energy += (double) buf.getSample (1, i) * buf.getSample (1, i);
        }
        check (energy > 1.0, "plugin: la chitarra esce anche a destra", energy);
        auto* mp = p.engine.slot (0).proc->getPlayHead();
        const auto pos = mp != nullptr ? mp->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
        check (pos.hasValue() && pos->getBpm().hasValue() && std::abs (*pos->getBpm() - 87.0) < 1.0e-6, "plugin: i moduli vedono il tempo della DAW (87 bpm)");
        p.presetName = "crunch rig"; p.presetDirty = true;
        juce::MemoryBlock mb; p.getStateInformation (mb);
        rig::RigPluginProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.engine.numSlots() == 1 && q.engine.slot (0).info->id == "delay" && q.presetName == "crunch rig" && q.presetDirty, "plugin: catena e preset si riaprono dal progetto");
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto* re = dynamic_cast<rig::RigPluginEditor*> (ed.get());
            check (re != nullptr && re->rig().currentPreset == "crunch rig" && re->rig().dirty && re->rig().plugin, "plugin: l'interfaccia mostra il preset (modificato)");
            pump (200);
            savePng (*ed, "gui_brig_plugin.png");
            q.editorBeingDeleted (ed.get());
        }
        p.releaseResources();
    }
    // salvataggio fantasma del plugin: un'istanza nuova ritrova lo stato dell'ultima chiusa (anche non salvato come
    // preset), senza creare preset; un progetto riaperto e richiuso senza modifiche non lo sovrascrive
    {
        rig::RigComponent::pluginSessionFile().deleteFile();
        {
            rig::RigPluginProcessor a;
            check (a.engine.numSlots() == 0 && a.presetName == "clean slate", "plugin, prima volta: clean slate");
            a.engine.addModule ("comp"); a.engine.addModule ("verb");
            a.presetName = "crunch rig"; a.presetDirty = true;
        }
        rig::RigPluginProcessor b;
        check (b.engine.numSlots() == 2 && b.engine.slot (1).info->id == "verb" && b.presetName == "crunch rig" && b.presetDirty,
               "plugin riaperto: la catena e il preset modificato di prima", b.engine.numSlots());
        check (rig::RigComponent::listPresets().size() == 1, "nessun preset creato in automatico", (int) rig::RigComponent::listPresets().size());
        juce::MemoryBlock old;
        { rig::RigPluginProcessor x; x.engine.clear(); x.engine.addModule ("delay"); x.getStateInformation (old); }   // ora l'ultimo stato è b-delay
        { rig::RigPluginProcessor proj; proj.setStateInformation (old.getData(), (int) old.getSize()); }
        rig::RigPluginProcessor c;
        check (c.engine.numSlots() == 1 && c.engine.slot (0).info->id == "delay", "un progetto aperto e chiuso senza modifiche non cambia l'ultimo stato");
    }
    rig::RigComponent::presetDirOverride() = juce::File();
    plugDir.deleteRecursively();
    return finish();
}

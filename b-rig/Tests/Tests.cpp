// Test di b-rig: catena di default, protezione di b-amp, bypass a 0 % CPU con crossfade senza click,
// fade-in di 80 ms all'armo, limiter a -0,5 dBFS, metronomo, tuner (spento di default), preset del rig,
// trascinamento delle card, riapertura degli editor, tutti i moduli (processo + editor) e snapshot dell'interfaccia.
#include "UI/RigComponent.h"
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

        // b-amp aggiunto dalla libreria: parte SEMPRE spento; posizione logica
        auto* amp2 = e.addModule ("amp");
        check (amp2 != nullptr && ! amp2->enabled.load(), "b-amp aggiunto dalla libreria: safe muted");
        auto* tight = e.addModule ("tight");
        check (tight != nullptr && tight->enabled.load() && e.indexOf (tight) == indexOfId (e, "amp") - 1, "b-tight entra prima di b-amp (posizione logica)", e.indexOf (tight));
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
        e.metroOn = false; runEngine (e, 256, [] (int) { return 0.0f; });
        e.metroSound = 0; e.metroVolume = 0.25f; e.metroOn = true;
        const auto quiet = runEngine (e, 4800, [] (int) { return 0.0f; });
        check (std::abs (quiet.getMagnitude (0, 0, 4800) / pk[0] - 0.25f) < 0.02f, "metronomo: volume al 25 %", quiet.getMagnitude (0, 0, 4800) / pk[0]);
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
        check (! f.slot (1).enabled.load(), "preset del rig: b-amp torna safe muted");
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
        ui.addFromLibrary ("tight");
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
            ui.loadPreset ("default");
            check (! ui.dirty, "preset appena caricato: niente save");
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
            check (rig::RigComponent::startupPreset() == "default", "all'inizio si carica \"default\" (la catena di fabbrica)");
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
            check (! e.slot (ia).enabled.load() && lockedEmpty, "b-blend senza IR dopo l'ampli: bloccato acceso, b-amp muto");
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
        ui.showInfo ("tight");
        pump (100);
        savePng (ui, "gui_brig_info.png");
        ui.setSize (1600, 1000);
        ui.select (&e.slot (e.numSlots() - 1));
        pump (300);
        savePng (ui, "gui_brig_large.png");
    }
    return finish();
}

// Test di b-adapt: true peak <= -1 dBTP, bassi mono, thump, de-harsh, monitor, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include <cstdio>
namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0) { std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++failures; }
constexpr double sr = 48000.0; constexpr int block = 512;
const double tp = juce::MathConstants<double>::twoPi;
void setP (juce::AudioProcessorValueTreeState& s, const char* id, float v) { auto* a = s.getParameter (id); a->setValueNotifyingHost (a->convertTo0to1 (v)); }
std::unique_ptr<BAdaptProcessor> make() { auto p = std::make_unique<BAdaptProcessor>(); p->setRateAndBufferSizeDetails (sr, block); p->prepareToPlay (sr, block); return p; }
template <typename F> juce::AudioBuffer<float> run (BAdaptProcessor& p, int len, F sig)
{
    juce::AudioBuffer<float> out (2, len), b (2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += block) { const int n = std::min (block, len - pos); b.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) { auto [l, r] = sig (pos + i); b.setSample (0, i, l); b.setSample (1, i, r); }
        p.processBlock (b, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n); }
    return out;
}
double rms (const juce::AudioBuffer<float>& b, int c, int a, int e) { double s = 0; for (int i = a; i < e; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i); return std::sqrt (s / (e - a)); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. true peak: rumore e sinusoidi forti (+6 dBFS), picco misurato in oversampling 8x
    {
        auto p = make();
        juce::Random r (1);
        auto y = run (*p, 96000, [&] (int i) { const float v = 2.0f * (float) std::sin (tp * 11025.0 * i / sr + 0.3) + (r.nextFloat() - 0.5f);
                                                 return std::pair<float, float> { v, -v }; });
        juce::dsp::Oversampling<float> o8 (2, 3, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        o8.initProcessing ((size_t) y.getNumSamples());
        juce::dsp::AudioBlock<float> blk (y);
        auto up = o8.processSamplesUp (blk);
        float pk = 0; for (size_t c = 0; c < 2; ++c) for (size_t i = up.getNumSamples() / 4; i < up.getNumSamples(); ++i) pk = std::max (pk, std::abs (up.getSample ((int) c, (int) i)));
        const double tpDb = 20 * std::log10 (pk);
        std::printf ("       true peak con +6 dBFS in ingresso: %.2f dBTP\n", tpDb);
        check (tpDb <= -1.0, "codec shield: picchi veri <= -1 dBTP", tpDb);
    }
    // 2. mono guard: il side sotto 250 Hz sparisce (bassi mono), sopra resta
    {
        auto p = make();
        auto y = run (*p, 48000, [&] (int i) { const float v = 0.1f * (float) std::sin (tp * 60.0 * i / sr); return std::pair<float, float> { v, -v }; });
        auto q = make();
        auto z = run (*q, 48000, [&] (int i) { const float v = 0.1f * (float) std::sin (tp * 2000.0 * i / sr); return std::pair<float, float> { v, 0.3f * v }; });
        const double low = 20 * std::log10 (rms (y, 0, 24000, 48000) / (0.1 / std::sqrt (2.0)));
        const double hi = 20 * std::log10 (rms (z, 0, 24000, 48000) / (0.1 / std::sqrt (2.0)));
        std::printf ("       side a 60 Hz %.1f dB, segnale a 2 kHz %.2f dB\n", low, hi);
        check (low < -12.0 && std::abs (hi) < 0.5, "mono guard: bassi del side tolti, alte intatte", low);
    }
    // 2b. safe width: segnale in controfase (correlazione -1) -> side abbassato, visibile nel meter
    {
        auto p = make(); setP (p->apvts, BAdaptIDs::safeWidth, 100.0f);
        run (*p, 48000, [&] (int i) { const float v = 0.2f * (float) std::sin (tp * 1000.0 * i / sr); return std::pair<float, float> { v, -v }; });
        std::printf ("       controfase: side -%.1f dB (safe width 100 %%)\n", p->sideCutDb.load());
        check (p->sideCutDb.load() > 10.0f, "safe width: con la correlazione negativa il side scende (fino a 12 dB)", p->sideCutDb.load());
    }
    // 3. thump: con 100 % compare energia nuova tra 170 e 420 Hz partendo da 110 Hz
    {
        auto energyAround = [&] (float thump)
        {
            auto p = make(); setP (p->apvts, BAdaptIDs::thump, thump);
            auto y = run (*p, 48000, [&] (int i) { const float v = 0.2f * (float) std::sin (tp * 110.0 * i / sr); return std::pair<float, float> { v, v }; });
            double s1 = 0, s2 = 0; const double c = 2 * std::cos (tp * 220.0 / sr);
            for (int i = 24000; i < 48000; ++i) { const double s0 = y.getSample (0, i) + c * s1 - s2; s2 = s1; s1 = s0; }
            return 10 * std::log10 (s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-20);
        };
        const double off = energyAround (0.0f), on = energyAround (100.0f);
        std::printf ("       2a armonica (220 Hz): thump 0 %% %.1f dB, 100 %% %.1f dB\n", off, on);
        check (on > off + 20.0, "phantom thump: armoniche dei bassi aggiunte", on - off);
    }
    // 4. de-harsh: per il 70 % un EQ fisso sui punti caldi, per il 30 % tagli stretti solo sulle risonanze
    {
        juce::Random r (4);
        std::vector<float> noise (96000); for (auto& v : noise) v = 0.2f * (r.nextFloat() - 0.5f);
        auto bandDb = [&] (const juce::AudioBuffer<float>& y, double f)   // energia in una banda stretta attorno a f
        {
            sp::Bq a; a.set (sp::Bq::BP, sr, f, 8.0); double e = 0;
            for (int i = 0; i < 96000; ++i) { const float v = a.process (y.getSample (0, i)); if (i >= 48000) e += (double) v * v; }
            return 10 * std::log10 (e + 1e-30);
        };
        auto runNoise = [&] (float amt, float toneHz, BAdaptProcessor** keep = nullptr)
        {
            static std::unique_ptr<BAdaptProcessor> held;
            held = make(); setP (held->apvts, BAdaptIDs::deharsh, amt);
            auto y = run (*held, 96000, [&] (int i) { const float v = noise[(size_t) i] + (toneHz > 0 ? 0.25f * (float) std::sin (tp * toneHz * i / sr) : 0.0f);
                                                        return std::pair<float, float> { v, v }; });
            if (keep != nullptr) *keep = held.get();
            return y;
        };
        // rumore senza risonanze: lavora solo l'EQ fisso (circa -5 dB a 3 kHz, nessun taglio dinamico)
        BAdaptProcessor* p1 = nullptr;
        const auto off = runNoise (0.0f, 0.0f), on = runNoise (100.0f, 0.0f, &p1);
        const double eq3k = bandDb (on, 3000.0) - bandDb (off, 3000.0), eq1k = bandDb (on, 1000.0) - bandDb (off, 1000.0);
        float dynNoise = 0; for (auto& d : p1->dynCutDb) dynNoise = std::max (dynNoise, d.load());
        std::printf ("       de-harsh 100 %% su rumore: 3 kHz %+.1f dB, 1 kHz %+.1f dB, taglio dinamico %.2f dB\n", eq3k, eq1k, dynNoise);
        check (eq3k < -4.0 && eq3k > -6.5 && std::abs (eq1k) < 0.6 && dynNoise < 0.3f, "de-harsh: EQ fisso sui punti caldi, il resto non cambia", eq3k);
        // una risonanza a 4,8 kHz che sporge: solo quella banda scende (stretta), le altre no
        BAdaptProcessor* p2 = nullptr;
        runNoise (100.0f, 4800.0f, &p2);
        const float c48 = p2->dynCutDb[2].load(), c25 = p2->dynCutDb[0].load();
        std::printf ("       risonanza a 4,8 kHz: taglio dinamico %.2f dB (2,5 kHz: %.2f dB)\n", c48, c25);
        check (c48 > 1.5f && c48 <= 2.01f && c25 < 0.1f, "de-harsh dinamico: solo la banda che risuona, al massimo 2 dB (30 %)", c48);
    }
    // 5. monitor phone: mono e niente sotto i 200 Hz
    {
        auto p = make(); setP (p->apvts, BAdaptIDs::monitor, 1.0f);
        auto y = run (*p, 48000, [&] (int i) { const float v = 0.1f * (float) std::sin (tp * 100.0 * i / sr); return std::pair<float, float> { v, 0.0f }; });
        const double lowDb = 20 * std::log10 (rms (y, 0, 24000, 48000) / (0.1 / std::sqrt (2.0)) + 1e-9);
        check (lowDb < -20.0 && std::abs (rms (y, 0, 24000, 48000) - rms (y, 1, 24000, 48000)) < 1e-6, "monitor phone: mono e taglio dei bassi", lowDb);
    }
    // 6. CPU e snapshot (monitor acceso: avviso visibile; poi spento, per le immagini del repository)
    {
        auto p = std::make_unique<BAdaptProcessor>(); p->setRateAndBufferSizeDetails (sr, 128); p->prepareToPlay (sr, 128);
        setP (p->apvts, BAdaptIDs::thump, 50); setP (p->apvts, BAdaptIDs::deharsh, 50);
        juce::AudioBuffer<float> b (2, 128); juce::MidiBuffer m; juce::Random r (3); double t = 0;
        for (int k = 0; k < (int) (10 * sr / 128); ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) b.setSample (c, i, r.nextFloat() - 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks(); p->processBlock (b, m); t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0); }
        check (t * 10 < 2.0, "CPU % di un core (stereo)", t * 10);
        BAdaptProcessor q; q.setRateAndBufferSizeDetails (sr, block); q.prepareToPlay (sr, block);
        setP (q.apvts, BAdaptIDs::thump, 60); setP (q.apvts, BAdaptIDs::deharsh, 70); setP (q.apvts, BAdaptIDs::monitor, 1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        run (q, 48000, [&] (int i) { const float v = 0.3f * (float) std::sin (tp * 3400.0 * i / sr); return std::pair<float, float> { v, 0.5f * v }; });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_badapt.png"); f.deleteFile();
        juce::FileOutputStream os (f); check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_badapt.png");
        // la stessa schermata con il monitor spento (quella delle immagini del repository)
        setP (q.apvts, BAdaptIDs::monitor, 0);
        run (q, 48000, [&] (int i) { const float v = 0.3f * (float) std::sin (tp * 3400.0 * i / sr); return std::pair<float, float> { v, 0.5f * v }; });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f2 = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_badapt_off.png"); f2.deleteFile();
        juce::FileOutputStream os2 (f2); check (os2.openedOk() && juce::PNGImageFormat().writeImageToStream (img2, os2), "gui_badapt_off.png");
        ed.reset();
    }
    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

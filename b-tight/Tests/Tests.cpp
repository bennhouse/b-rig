// Test di b-tight: trasparenza a controlli neutri, clamp sui palm-mute, bite, push, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include <cstdio>
#include <juce_dsp/juce_dsp.h>
namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0) { std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++failures; }
constexpr double sr = 48000.0; constexpr int block = 256;
void setP (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, const char* id, float v) { juce::ignoreUnused (p); auto* a = s.getParameter (id); a->setValueNotifyingHost (a->convertTo0to1 (v)); }
template <typename F> std::vector<float> run (BTightProcessor& p, int len, F sig)
{
    std::vector<float> y ((size_t) len); juce::AudioBuffer<float> b (1, block); juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += block) { const int n = std::min (block, len - pos); b.setSize (1, n, false, false, true);
        for (int i = 0; i < n; ++i) b.setSample (0, i, sig (pos + i)); p.processBlock (b, m); for (int i = 0; i < n; ++i) y[(size_t) (pos + i)] = b.getSample (0, i); }
    return y;
}
double rmsDb (const std::vector<float>& y, int a, int b) { double e = 0; for (int i = a; i < b; ++i) e += (double) y[(size_t) i] * y[(size_t) i]; return 10 * std::log10 (e / (b - a) + 1e-30); }
std::unique_ptr<BTightProcessor> make() { auto p = std::make_unique<BTightProcessor>(); p->setRateAndBufferSizeDetails (sr, block); p->prepareToPlay (sr, block); return p; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double tp = juce::MathConstants<double>::twoPi;
    // 0. default: tuning spento e nessuna azione (uscita identica)
    {
        auto p = make();
        juce::Random r (5);
        std::vector<float> in (24000); for (auto& v : in) v = 0.3f * (r.nextFloat() - 0.5f);
        auto y = run (*p, 24000, [&] (int i) { return in[(size_t) i]; });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y[(size_t) i] - in[(size_t) i]));
        check (d < 1e-6f, "default: tuning off, il plugin non fa nulla", d);
        p->applyTuningPreset (2);
        auto* c = p->apvts.getParameter (BTightIDs::clamp);
        check (std::abs (c->convertFrom0to1 (c->getValue()) + 26.0f) < 0.01f, "scegliendo un tuning parte il suo preset (drop: clamp -26 dB)");
    }
    // 1. controlli neutri, segnale sotto la soglia: 1 kHz passa invariato, 30 Hz tagliato dal mud cut
    {
        auto p = make();
        setP (*p, p->apvts, BTightIDs::tuning, 1.0f);
        setP (*p, p->apvts, BTightIDs::clamp, 0.0f);
        auto y = run (*p, 48000, [&] (int i) { return 0.1f * (float) std::sin (tp * 1000.0 * i / sr); });
        const double g1k = rmsDb (y, 24000, 48000) - 10 * std::log10 (0.005);
        auto q = make();
        setP (*q, q->apvts, BTightIDs::tuning, 1.0f);
        setP (*q, q->apvts, BTightIDs::clamp, 0.0f);
        auto z = run (*q, 48000, [&] (int i) { return 0.1f * (float) std::sin (tp * 30.0 * i / sr); });
        const double g30 = rmsDb (z, 24000, 48000) - 10 * std::log10 (0.005);
        std::printf ("       1 kHz %.2f dB, 30 Hz %.1f dB (standard, 80 Hz)\n", g1k, g30);
        check (std::abs (g1k) < 0.2 && g30 < -12.0, "mud cut: 30 Hz tagliato, medie invariate", g1k);
    }
    // 2. chug clamp: un palm-mute forte a 110 Hz viene ridotto, le alte no
    {
        auto p = make();
        setP (*p, p->apvts, BTightIDs::clamp, -36.0f);
        setP (*p, p->apvts, BTightIDs::autoMu, 0.0f);
        auto y = run (*p, 48000, [&] (int i) { return 0.5f * (float) std::sin (tp * 110.0 * i / sr); });
        const double gLow = rmsDb (y, 24000, 48000) - 10 * std::log10 (0.125);
        auto q = make();
        setP (*q, q->apvts, BTightIDs::clamp, -36.0f);
        setP (*q, q->apvts, BTightIDs::autoMu, 0.0f);
        auto z = run (*q, 48000, [&] (int i) { return 0.5f * (float) std::sin (tp * 2000.0 * i / sr); });
        const double gHigh = rmsDb (z, 24000, 48000) - 10 * std::log10 (0.125);
        std::printf ("       clamp -36 dB: 110 Hz %.1f dB, 2 kHz %.2f dB\n", gLow, gHigh);
        check (gLow < -8.0 && gLow > -12.5 && std::abs (gHigh) < 0.3, "chug clamp: riduce solo le basse (max 12 dB)", gLow);
    }
    // 2b. auto: con il make-up automatico un palm-mute lungo torna al volume d'ingresso
    {
        auto p = make();
        setP (*p, p->apvts, BTightIDs::clamp, -36.0f);
        setP (*p, p->apvts, BTightIDs::autoMu, 1.0f);
        auto y = run (*p, 4 * 48000, [&] (int i) { return 0.5f * (float) std::sin (tp * 110.0 * i / sr); });
        const double g = rmsDb (y, 3 * 48000, 4 * 48000) - 10 * std::log10 (0.125);
        std::printf ("       clamp -36 dB con auto: 110 Hz %.1f dB dopo 3 s (senza auto circa -10.6 dB)\n", g);
        check (std::abs (g) < 1.5, "auto: il make-up restituisce alle basse il volume tolto dal clamp", g);
    }
    // 3. bite: la plettrata (burst a 2 kHz) esce più forte nei primi ms con bite +100 %
    {
        auto attackGain = [&] (float bite)
        {
            auto p = make(); setP (*p, p->apvts, BTightIDs::clamp, 0.0f); setP (*p, p->apvts, BTightIDs::bite, bite);
            auto y = run (*p, 24000, [&] (int i) { return i >= 12000 ? 0.2f * (float) std::sin (tp * 2000.0 * i / sr) : 0.0f; });
            float pk = 0; for (int i = 12000; i < 12000 + 240; ++i) pk = std::max (pk, std::abs (y[(size_t) i]));
            return 20 * std::log10 (pk / 0.2);
        };
        const double up = attackGain (100.0f), flat = attackGain (0.0f), down = attackGain (-100.0f);
        std::printf ("       picco nei primi 5 ms: bite +100 %% %.1f dB, 0 %% %.1f dB, -100 %% %.1f dB\n", up, flat, down);
        check (up > flat + 3.0 && down < flat - 3.0, "pick attack: bite esalta o smorza l'attacco", up - flat);
    }
    // 3b. bite tarato uguale nei tre tuning: corda a vuoto più bassa di ogni tuning, stesso clic di plettro
    {
        const double lowE[4] = { 0, 82.4, 73.4, 61.7 };
        double boost[4] = {};
        for (int t = 1; t <= 3; ++t)
        {
            auto pk = [&] (float bite)
            {
                auto p = make(); setP (*p, p->apvts, BTightIDs::tuning, (float) t); setP (*p, p->apvts, BTightIDs::clamp, 0.0f);
                setP (*p, p->apvts, BTightIDs::bite, bite); setP (*p, p->apvts, BTightIDs::autoMu, 0.0f);
                juce::Random r (11);
                auto y = run (*p, 24000, [&] (int i)
                {
                    if (i < 12000) return 0.0f;
                    const double tt = (i - 12000) / sr;
                    double s = 0; for (int h = 1; h <= 10; ++h) s += std::sin (tp * lowE[t] * h * tt) / h;
                    return (float) (0.25 * s * std::exp (-tt * 2) + (tt < 0.002 ? 0.3 * (r.nextFloat() - 0.5) : 0.0));
                });
                float m = 0; for (int i = 12000; i < 12000 + 240; ++i) m = std::max (m, std::abs (y[(size_t) i]));
                return 20 * std::log10 (m);
            };
            boost[t] = pk (100.0f) - pk (0.0f);
        }
        std::printf ("       bite +100 %%, primi 5 ms: standard %+.1f dB, drop %+.1f dB, baritone %+.1f dB\n", boost[1], boost[2], boost[3]);
        check (boost[1] > 3.0 && std::abs (boost[2] - boost[1]) < 2.0 && std::abs (boost[3] - boost[1]) < 2.0, "pick bite: stessa resa con ogni tuning", boost[1]);
    }
    // 3c. nessun artefatto: sinusoide stabile con bite al massimo resta pulita (nessuna modulazione)
    {
        auto p = make(); setP (*p, p->apvts, BTightIDs::tuning, 1.0f); setP (*p, p->apvts, BTightIDs::bite, 100.0f);
        setP (*p, p->apvts, BTightIDs::clamp, -30.0f);
        const int N = 1 << 15;
        auto y = run (*p, N + 24000, [&] (int i) { return 0.3f * (float) std::sin (tp * 1000.0 * i / sr) + 0.3f * (float) std::sin (tp * 110.0 * i / sr); });
        std::vector<float> d ((size_t) (2 * N), 0.0f);
        for (int i = 0; i < N; ++i) d[(size_t) i] = y[(size_t) (24000 + i)] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));
        juce::dsp::FFT (15).performFrequencyOnlyForwardTransform (d.data(), true);
        float sig = 0, other = 0;
        for (int b = 2; b < N / 2; ++b)
        {
            const double hz = b * sr / N;
            const bool tone = std::abs (hz - 1000.0) < 8.0 || std::abs (hz - 110.0) < 8.0;
            (tone ? sig : other) = std::max (tone ? sig : other, d[(size_t) b]);
        }
        const double spur = 20 * std::log10 (other / sig);
        std::printf ("       due toni costanti, clamp e bite attivi: spurie a %.1f dB\n", spur);
        check (spur < -50.0, "nessun artefatto: niente modulazioni o distorsioni sui suoni tenuti", spur);
    }
    // 4. push: +12 dB a 1,4 kHz, con trim di -6 dB
    {
        auto p = make(); setP (*p, p->apvts, BTightIDs::clamp, 0.0f); setP (*p, p->apvts, BTightIDs::push, 12.0f);
        auto y = run (*p, 48000, [&] (int i) { return 0.05f * (float) std::sin (tp * 1400.0 * i / sr); });
        const double g = rmsDb (y, 24000, 48000) - 10 * std::log10 (0.00125);
        check (std::abs (g - 6.0) < 0.5, "focus tilt: +12 dB a 1,4 kHz meno 6 dB di trim", g);
    }
    // 5. CPU e snapshot
    {
        auto p = std::make_unique<BTightProcessor>(); p->setRateAndBufferSizeDetails (sr, 128); p->prepareToPlay (sr, 128);
        juce::AudioBuffer<float> b (1, 128); juce::MidiBuffer m; juce::Random r (3); double t = 0;
        for (int k = 0; k < (int) (10 * sr / 128); ++k) { for (int i = 0; i < 128; ++i) b.setSample (0, i, r.nextFloat() - 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks(); p->processBlock (b, m); t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0); }
        check (t * 10 < 1.0, "CPU % di un core (mono)", t * 10);
        // gain d'uscita (dopo l'effetto) e autogain (RMS di uscita = RMS della DI)
        {
            BTightProcessor g; g.setRateAndBufferSizeDetails (sr, block); g.prepareToPlay (sr, block);
            auto sine = [&] (int i) { return (float) (0.5 * std::sin (tp * 110.0 * i / sr)); };
            setP (g, g.apvts, BTightIDs::gain, -6.0f);
            auto y = run (g, 48000, sine);
            const double dg = rmsDb (y, 24000, 48000) - 10 * std::log10 (0.125);
            check (std::abs (dg + 6.0) < 0.1, "gain -6 dB in uscita (anche col resto spento)", dg);
            setP (g, g.apvts, BTightIDs::gain, 0.0f);
            setP (g, g.apvts, BTightIDs::tuning, 1.0f); g.applyTuningPreset (1); setP (g, g.apvts, BTightIDs::push, 12.0f);
            auto raw = run (g, 96000, sine);
            const double before = rmsDb (raw, 48000, 96000) - 10 * std::log10 (0.125);
            setP (g, g.apvts, BTightIDs::autoGain, 1.0f);
            auto z = run (g, 5 * 48000, sine);
            const double after = rmsDb (z, 4 * 48000, 5 * 48000) - 10 * std::log10 (0.125);
            std::printf ("       autogain: uscita - ingresso %.2f dB senza, %.2f dB con\n", before, after);
            check (std::abs (after) < 0.5, "autogain: RMS di uscita pari a quello d'ingresso (dB)", after);
        }
        BTightProcessor q; q.setRateAndBufferSizeDetails (sr, block); q.prepareToPlay (sr, block);
        setP (q, q.apvts, BTightIDs::bite, 60.0f); setP (q, q.apvts, BTightIDs::clamp, -30.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        run (q, 96000, [&] (int i) { const double t2 = std::fmod (i / sr, 0.25); return (float) (0.4 * std::exp (-t2 * 18) * std::sin (tp * 82.4 * i / sr) + 0.15 * std::exp (-t2 * 60) * std::sin (tp * 2400.0 * i / sr)); });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_btight.png"); f.deleteFile();
        juce::FileOutputStream os (f); check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_btight.png");
        ed.reset();
    }
    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

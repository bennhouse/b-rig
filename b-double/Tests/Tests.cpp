// Test di b-double: dry a 2 ms in hard-pan, ritardo della doppia nel range, mono-safe, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0) { std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++failures; }
constexpr double sr = 48000.0; constexpr int block = 512;
void setP (juce::AudioProcessorValueTreeState& s, const char* id, float v) { auto* a = s.getParameter (id); a->setValueNotifyingHost (a->convertTo0to1 (v)); }
std::unique_ptr<BDoubleProcessor> make()
{
    auto p = std::make_unique<BDoubleProcessor>();
    juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::mono()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    p->setBusesLayout (l); p->setRateAndBufferSizeDetails (sr, block); p->prepareToPlay (sr, block); return p;
}
template <typename F> juce::AudioBuffer<float> run (BDoubleProcessor& p, int len, F sig)
{
    juce::AudioBuffer<float> out (2, len), b (2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += block) { const int n = std::min (block, len - pos); b.setSize (2, n, false, false, true); b.clear();
        for (int i = 0; i < n; ++i) b.setSample (0, i, sig (pos + i)); p.processBlock (b, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n); }
    return out;
}
// "chitarra": plettrate ogni 250 ms di rumore filtrato che decade
float guitar (int i) { static juce::Random r (9); const double t = std::fmod (i / sr, 0.25); return (float) ((r.nextFloat() - 0.5f) * 0.6 * std::exp (-t * 10)); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. hard-pan: L = ingresso ritardato di 2 ms esatti
    {
        auto p = make();
        auto y = run (*p, 4096, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
        check (std::abs (y.getSample (0, 196) - 1.0f) < 1e-6, "hard-pan: dry a sinistra ritardato di 2 ms");
    }
    // 2. ritardo della doppia: con tightness 100 % il picco di correlazione L/R sta tra 12 e 20 ms
    {
        auto p = make(); setP (p->apvts, BDoubleIDs::tightness, 100.0f); setP (p->apvts, BDoubleIDs::dynamic, 1.0f);
        const int len = (int) (4 * sr);
        auto y = run (*p, len, guitar);
        double best = -1; int bestLag = 0;
        for (int lag = 0; lag < (int) (0.04 * sr); lag += 4)
        {
            double c = 0; for (int i = len / 2; i < len - 2000; i += 2) c += (double) y.getSample (0, i) * y.getSample (1, i + lag);
            if (c > best) { best = c; bestLag = lag; }
        }
        const double ms = bestLag / sr * 1000.0 + 2.0;   // + i 2 ms del dry
        std::printf ("       ritardo medio della doppia: %.1f ms\n", ms);
        check (ms > 13.0 && ms < 19.0, "dynamic, tightness 100 %: ritardo attorno a delay (16 ms ± jitter ± allineamento)", ms);
    }
    // 2b. on/off: spento = ingresso identico su L e R
    {
        auto p = make(); setP (p->apvts, BDoubleIDs::active, 0.0f); p->prepareToPlay (sr, block);
        auto y = run (*p, 4096, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
        check (std::abs (y.getSample (0, 100) - 1.0f) < 1e-6 && std::abs (y.getSample (1, 100) - 1.0f) < 1e-6, "on/off: spento = segnale originale su entrambi i lati");
    }
    // 3. mono-safe: in mono (L+R) il pettine di cancellazioni si attenua (meno scarto tra bande vicine)
    {
        auto ripple = [&] (float safe)
        {
            auto p = make(); setP (p->apvts, BDoubleIDs::monoSafe, safe);
            const int seg = 1 << 12, segs = 48, len = seg * segs;
            juce::Random r (4);
            auto y = run (*p, len + 8192, [&] (int) { return 0.2f * (r.nextFloat() - 0.5f); });
            // spettro medio (Welch) di L+R: il rumore si media, resta il pettine delle cancellazioni
            std::vector<double> pw ((size_t) seg / 2, 0.0);
            std::vector<float> d ((size_t) (2 * seg), 0.0f);
            for (int sgi = 0; sgi < segs; ++sgi)
            {
                std::fill (d.begin(), d.end(), 0.0f);
                for (int i = 0; i < seg; ++i)
                    d[(size_t) i] = (y.getSample (0, 8192 + sgi * seg + i) + y.getSample (1, 8192 + sgi * seg + i))
                                  * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) seg));
                juce::dsp::FFT (12).performFrequencyOnlyForwardTransform (d.data(), true);
                for (int k = 0; k < seg / 2; ++k) pw[(size_t) k] += (double) d[(size_t) k] * d[(size_t) k];
            }
            double acc = 0; int cnt = 0;
            for (int k = (int) (300.0 * seg / sr); k < (int) (3000.0 * seg / sr); ++k)
            {
                double avg = 0; for (int j = -6; j <= 6; ++j) avg += pw[(size_t) (k + j)];
                acc += std::abs (10 * std::log10 (pw[(size_t) k] / (avg / 13.0))); ++cnt;
            }
            return acc / cnt;
        };
        const double off = ripple (0.0f), on = ripple (100.0f);
        std::printf ("       mono L+R, scarto medio tra bin vicini: mono-safe 0 %% %.2f dB, 100 %% %.2f dB\n", off, on);
        check (on < off, "mono-safe: meno cancellazioni ascoltando in mono", off - on);
    }
    // 3a. mono-safe sulle basse: una nota a 110 Hz in mono (L+R) non deve "sparire" quando dry e doppia vanno in controfase
    {
        auto dip = [&] (float safe)
        {
            auto p = make(); setP (p->apvts, BDoubleIDs::monoSafe, safe); p->prepareToPlay (sr, block);
            auto y = run (*p, (int) (6 * sr), [] (int i) { return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / sr)); });
            double lo = 1e9, hi = 0; const int w = (int) (0.05 * sr);
            for (int s0 = (int) sr; s0 + w < (int) (6 * sr); s0 += w)
            {
                double e = 0; for (int i = s0; i < s0 + w; ++i) { const double m = y.getSample (0, i) + y.getSample (1, i); e += m * m; }
                lo = std::min (lo, e); hi = std::max (hi, e);
            }
            return 10.0 * std::log10 (hi / (lo + 1e-20));
        };
        const double off = dip (0.0f), on = dip (100.0f);
        std::printf ("       mono L+R a 110 Hz, escursione di livello: mono-safe 0 %% %.1f dB, 100 %% %.1f dB\n", off, on);
        check (on < off - 6.0 && on < 6.0, "mono-safe: in mono le basse non spariscono più (escursione dB)", on);
    }
    // 3b. nessun click / burst: all'avvio, a ogni plettrata (dynamic, tightness 0) e cambiando delay e detune
    {
        auto p = make(); setP (p->apvts, BDoubleIDs::dynamic, 1.0f); setP (p->apvts, BDoubleIDs::tightness, 0.0f); p->prepareToPlay (sr, block);
        // seno a 220 Hz con un "attacco" (gradino d'inviluppo) ogni 250 ms: la doppia deve restare liscia
        auto sig = [] (int i) { const double t = std::fmod (i / sr, 0.25); return (float) (0.3 * (t < 0.002 ? t / 0.002 : 1.0) * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr)); };
        auto y = run (*p, (int) (3 * sr), sig);
        float jump = 0; for (int i = 1; i < (int) (3 * sr); ++i) jump = std::max (jump, std::abs (y.getSample (1, i) - y.getSample (1, i - 1)));
        const float clean = (float) (0.3 * juce::MathConstants<double>::twoPi * 220.0 / sr);
        std::printf ("       dynamic, salto massimo tra campioni nella doppia: %.4f (seno pulito %.4f)\n", jump, clean);
        check (jump < clean * 2.5f, "dynamic: nessun burst o click agli attacchi (prese in crossfade)", jump);
        auto q = make(); setP (q->apvts, BDoubleIDs::detune, 0.0f);
        auto z0 = run (*q, 24000, [] (int i) { return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr)); });
        float first = 0; for (int i = 1; i < 24000; ++i) first = std::max (first, std::abs (z0.getSample (1, i) - z0.getSample (1, i - 1)));
        setP (q->apvts, BDoubleIDs::delay, 31.0f); setP (q->apvts, BDoubleIDs::detune, 12.0f);
        auto z = run (*q, 24000, [] (int i) { return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * (i + 24000) / sr)); });
        float jz = 0; for (int i = 1; i < 24000; ++i) jz = std::max (jz, std::abs (z.getSample (1, i) - z.getSample (1, i - 1)));
        std::printf ("       avvio %.4f, cambio di delay e detune %.4f (seno pulito %.4f)\n", first, jz, clean);
        check (first < clean * 1.5f && jz < clean * 2.5f, "avvio e cambio di delay/detune: nessun click", jz);
    }
    // 3c. tightness: con 0 % le prese della doppia variano molto più che con 100 %
    {
        auto spread = [&] (float t)
        {
            auto p = make(); setP (p->apvts, BDoubleIDs::dynamic, 1.0f); setP (p->apvts, BDoubleIDs::tightness, t); p->prepareToPlay (sr, block);
            juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m; float lo = 1e9f, hi = -1e9f;
            for (int k = 0; k < (int) (6 * sr / block); ++k)
            {
                b.clear(); for (int i = 0; i < block; ++i) b.setSample (0, i, guitar (k * block + i));
                p->processBlock (b, m);
                if (k * block > sr) { lo = std::min (lo, p->shownDelayMs.load()); hi = std::max (hi, p->shownDelayMs.load()); }
            }
            return hi - lo;
        };
        const float loose = spread (0.0f), tight = spread (100.0f);
        std::printf ("       escursione del ritardo: tightness 0 %% %.1f ms, 100 %% %.1f ms\n", loose, tight);
        check (loose > 3.0f * tight && loose > 4.0f, "tightness: controlla davvero il jitter temporale", loose - tight);
    }
    // 4. CPU e snapshot
    {
        auto p = make(); setP (p->apvts, BDoubleIDs::mode, 1.0f); setP (p->apvts, BDoubleIDs::monoSafe, 50.0f);
        juce::AudioBuffer<float> b (2, 128); juce::MidiBuffer m; double t = 0;
        p->prepareToPlay (sr, 128);
        for (int k = 0; k < (int) (10 * sr / 128); ++k) { for (int i = 0; i < 128; ++i) b.setSample (0, i, guitar (k * 128 + i));
            const auto t0 = juce::Time::getHighResolutionTicks(); p->processBlock (b, m); t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0); }
        check (t * 10 < 2.0, "CPU % di un core (center-wide)", t * 10);
        auto q = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (q->createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        run (*q, 48000, guitar);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build/gui_bdouble.png"); f.deleteFile();
        juce::FileOutputStream os (f); check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_bdouble.png");
        ed.reset();
    }
    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

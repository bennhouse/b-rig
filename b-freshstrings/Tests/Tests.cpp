// Test di b-freshstrings: silenzio, rumore di fondo, brillantezza sull'attacco, plettri, pickup, gauge, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
// energia sopra fc (passa-alto RBJ 4° ordine) tra due campioni, in dB
double hfDb (const juce::AudioBuffer<float>& b, int from, int to, double fc)
{
    sp::Bq a, c; a.set (sp::Bq::HP, sr, fc, 0.7071); c = a;
    double e = 0;
    for (int i = 0; i < to; ++i) { const float y = c.process (a.process (b.getSample (0, i))); if (i >= from) e += (double) y * y; }
    return 10 * std::log10 (e / (to - from) + 1e-30);
}
template <typename F> juce::AudioBuffer<float> runWith (std::initializer_list<std::pair<const char*, float>> params, int len, F sig)
{
    auto p = make<BFreshstringsProcessor> (1, 1);
    for (auto& [id, v] : params) setP (p->apvts, id, v);
    p->prepareToPlay (sr, block);
    return run (*p, len, sig);
}
// una plettrata: clic di plettro (rumore 1 ms) e nota a 196 Hz con armoniche che decadono
float strum (int i, int at = 4800)
{
    if (i < at) return 0.0f;
    const int k = i - at;
    static juce::Random r (11);
    const float click = k < 48 ? 0.4f * (r.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) k / 48.0f) : 0.0f;
    return click + pluck (k, 196.0, 10.0, 0.25);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. silenzio in, silenzio out
    {
        auto y = runWith ({}, 24000, [] (int) { return 0.0f; });
        check (y.getMagnitude (0, 0, 24000) == 0.0f, "silenzio: nessun rumore generato");
    }
    // 2. rumore di fondo della DI (-70 dBFS): le alte non salgono con freshness al 100 %
    {
        juce::Random r (2);
        std::vector<float> hiss (48000); for (auto& v : hiss) v = 0.0006f * (r.nextFloat() * 2.0f - 1.0f);
        auto a = runWith ({ { BFreshIDs::freshness, 0.0f } }, 48000, [&] (int i) { return hiss[(size_t) i]; });
        auto b = runWith ({ { BFreshIDs::freshness, 100.0f } }, 48000, [&] (int i) { return hiss[(size_t) i]; });
        const double d = hfDb (b, 12000, 48000, 3000.0) - hfDb (a, 12000, 48000, 3000.0);
        std::printf ("       fruscio a -70 dBFS: alte %+.2f dB con freshness 100 %%\n", d);
        check (std::abs (d) < 0.5, "freshness non alza il rumore di fondo", d);
    }
    // 3. sull'attacco la banda alta sale con freshness, sul decadimento quasi nulla
    {
        auto a = runWith ({ { BFreshIDs::freshness, 0.0f } }, 48000, [] (int i) { return strum (i); });
        auto b = runWith ({ { BFreshIDs::freshness, 100.0f } }, 48000, [] (int i) { return strum (i); });
        const double atk = hfDb (b, 4800, 4800 + 480, 3000.0) - hfDb (a, 4800, 4800 + 480, 3000.0);
        const double tail = hfDb (b, 4800 + 19200, 4800 + 28800, 3000.0) - hfDb (a, 4800 + 19200, 4800 + 28800, 3000.0);
        std::printf ("       sopra 3 kHz: attacco %+.1f dB, coda (400-600 ms) %+.1f dB\n", atk, tail);
        check (atk > 2.5 && tail < atk - 1.5, "freshness: brillantezza sull'attacco, non sul decadimento", atk);
    }
    // 4. plettri: metal più brillante di nylon nei primi 3 ms
    {
        auto a = runWith ({ { BFreshIDs::pick, 0.0f }, { BFreshIDs::freshness, 0.0f } }, 24000, [] (int i) { return strum (i); });
        auto b = runWith ({ { BFreshIDs::pick, 3.0f }, { BFreshIDs::freshness, 0.0f } }, 24000, [] (int i) { return strum (i); });
        const double d = hfDb (b, 4800, 4800 + 144, 4500.0) - hfDb (a, 4800, 4800 + 144, 4500.0);
        std::printf ("       primi 3 ms sopra 4,5 kHz: metal %+.1f dB rispetto a nylon\n", d);
        check (d > 4.0, "pick: metal più brillante di nylon sull'attacco", d);
    }
    // 5. pickup: neck scava 1,1 kHz rispetto a bridge, bridge spinge 3,4 kHz
    {
        auto tone = [&] (float pu, double f)
        {
            auto y = runWith ({ { BFreshIDs::pickup, pu }, { BFreshIDs::freshness, 0.0f }, { BFreshIDs::attack, 0.0f } }, 48000,
                              [f] (int i) { return (float) (0.1 * std::sin (tp * f * i / sr)); });
            return toneDb (y, 0, 24000, 48000, f) - 20 * std::log10 (0.1);
        };
        const double b11 = tone (0.0f, 1110.0), n11 = tone (2.0f, 1110.0), b34 = tone (0.0f, 3400.0);
        std::printf ("       1,1 kHz: bridge %+.1f dB, neck %+.1f dB; 3,4 kHz bridge %+.1f dB\n", b11, n11, b34);
        check (n11 < b11 - 3.0 && b34 > 2.0, "pickup: neck scavato a 1,1 kHz, bridge con la risonanza a 3,4 kHz", b11 - n11);
    }
    // 6. gauge: heavy ha più basse di .009 (a 80 Hz, nota forte)
    {
        auto tone = [&] (float gg)
        {
            auto y = runWith ({ { BFreshIDs::gauge, gg }, { BFreshIDs::freshness, 0.0f } }, 48000,
                              [] (int i) { return (float) (0.4 * std::sin (tp * 80.0 * i / sr)); });
            return toneDb (y, 0, 24000, 48000, 80.0);
        };
        const double thin = tone (0.0f), heavy = tone (3.0f);
        std::printf ("       80 Hz: .009 %.1f dB, heavy %.1f dB\n", thin, heavy);
        check (heavy - thin > 3.0, "gauge: le mute grosse hanno più peso sotto 110 Hz", heavy - thin);
    }
    // 7. CPU
    {
        auto p = make<BFreshstringsProcessor> (1, 1);
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU: %.3f %% di un core\n", cpu);
        check (cpu < 1.0, "CPU sotto l'1 % di un core", cpu);
    }
    // 8. interfaccia
    {
        auto p = make<BFreshstringsProcessor> (1, 1);
        run (*p, 24000, [] (int i) { return strum (i % 12000, 0); });
        snapshot (*p, "b-freshstrings.png");
        checkUpdates (*p, "b-freshstrings");
    }
    return finish();
}

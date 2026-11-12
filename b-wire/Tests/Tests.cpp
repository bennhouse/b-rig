// Test di b-wire: aggancio del PLL, sub-ottava, gate a riposo, CPU, larghezze fisse e snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"
using namespace tk;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // 1. aggancio: nota tenuta a 110 Hz
    {
        auto p = make<BWireProcessor> (1, 1);
        run (*p, 24000, [] (int i) { double s = 0; for (int h = 1; h <= 6; ++h) s += std::sin (tp * 110.0 * h * i / sr) / h; return (float) (0.2 * s); });
        std::printf ("       PLL su 110 Hz: %.2f Hz\n", p->pllHz.load());
        check (std::abs (p->pllHz.load() - 110.0f) < 1.5f, "PLL: aggancia la fondamentale (Hz)", p->pllHz.load());
    }
    // 2. sub -1: solo sub 1 al 100 % -> energia a 55 Hz
    {
        auto p = make<BWireProcessor> (1, 1);
        setP (p->apvts, BWireIDs::fuzz, 0); setP (p->apvts, BWireIDs::sub1, 100);
        auto y = run (*p, 48000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
        const double s55 = toneDb (y, 0, 24000, 48000, 55.0), s110 = toneDb (y, 0, 24000, 48000, 110.0);
        std::printf ("       sub 1: 55 Hz %.1f dB, 110 Hz %.1f dB\n", s55, s110);
        check (s55 > s110 + 20.0, "sub -1: un'ottava sotto la nota", s55 - s110);
    }
    // 3. silenzio: uscita nulla (gate)
    {
        auto p = make<BWireProcessor> (1, 1);
        run (*p, 24000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
        auto y = run (*p, 48000, [] (int) { return 0.0f; });   // dopo la coda (rilascio ~0,3 s)
        check (rmsDb (y, 0, 36000, 48000) < -90.0, "gate: in silenzio gli oscillatori tacciono (dBFS)", rmsDb (y, 0, 36000, 48000));
    }
    // 4. level auto: l'uscita pareggia il livello dell'ingresso (±3 dB)
    {
        auto p = make<BWireProcessor> (1, 1);
        setP (p->apvts, BWireIDs::levelAuto, 1); setP (p->apvts, BWireIDs::sub1, 100);
        auto y = run (*p, 96000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
        const double d = rmsDb (y, 0, 72000, 96000) - 20.0 * std::log10 (0.2 / std::sqrt (2.0));
        std::printf ("       auto level: uscita - ingresso %.2f dB\n", d);
        check (std::abs (d) < 3.0, "level auto: stesso RMS di ingresso e uscita (dB)", d);
    }
    // 5. ring: con il ring modulator solo, energia sulla seconda armonica (f + f) e niente fondamentale pulita
    {
        auto p = make<BWireProcessor> (1, 1);
        setP (p->apvts, BWireIDs::fuzz, 0); setP (p->apvts, BWireIDs::sub1, 0); setP (p->apvts, BWireIDs::ring, 100);
        auto y = run (*p, 48000, [] (int i) { return 0.2f * (float) std::sin (tp * 110.0 * i / sr); });
        const double h2 = toneDb (y, 0, 24000, 48000, 220.0), h1 = toneDb (y, 0, 24000, 48000, 110.0);
        std::printf ("       ring: 220 Hz %.1f dB, 110 Hz %.1f dB\n", h2, h1);
        check (h2 > h1 + 6.0, "ring adattivo: portante agganciata alla nota", h2 - h1);
    }
    auto p = make<BWireProcessor> (1, 1);
    setP (p->apvts, BWireIDs::sub2, 60); setP (p->apvts, BWireIDs::glitch, 30);
    const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
    check (cpu < 2.0, "CPU % di un core", cpu);
    snapshot (*p, "gui_bwire.png", [&] { run (*p, 12000, [] (int i) { return pluck (i, 82.4); }); });
    return finish();
}

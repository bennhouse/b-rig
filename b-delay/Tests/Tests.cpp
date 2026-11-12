// Test di b-delay: tempo dell'eco, feedback, offset e ping-pong, sync, tap, tipi stabili a 110 %, repitch senza
// click, trails in bypass, mix 0 = dry, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestKit.h"

namespace
{
using namespace tk;
std::unique_ptr<BDelayProcessor> mk (std::initializer_list<std::pair<const char*, float>> params)
{
    auto p = make<BDelayProcessor> (2, 2);
    setP (p->apvts, BDelayIDs::mix, 100.0f);
    setP (p->apvts, BDelayIDs::mod, 0.0f);
    setP (p->apvts, BDelayIDs::locut, 20.0f);
    setP (p->apvts, BDelayIDs::hicut, 20000.0f);
    for (auto& [id, v] : params) setP (p->apvts, id, v);
    p->prepareToPlay (sr, block);
    return p;
}
int peakAt (const juce::AudioBuffer<float>& y, int ch, int from, int to)
{
    int best = from; float m = 0;
    for (int i = from; i < to; ++i) if (std::abs (y.getSample (ch, i)) > m) { m = std::abs (y.getSample (ch, i)); best = i; }
    return best;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto impulse = [] (int i) { return i == 0 ? 0.5f : 0.0f; };
    // 1. digital, 250 ms: eco a 12000 campioni, secondo eco più basso del feedback
    {
        auto p = mk ({ { BDelayIDs::time, 250.0f }, { BDelayIDs::feedback, 50.0f } });
        auto y = run (*p, 30000, impulse);
        const int e1 = peakAt (y, 0, 6000, 18000), e2 = peakAt (y, 0, 18001, 29000);
        const double r = 20 * std::log10 (std::abs (y.getSample (0, e2)) / std::abs (y.getSample (0, e1)));
        std::printf ("       echi a %d e %d campioni, il secondo %.1f dB sotto il primo\n", e1, e2, r);
        check (std::abs (e1 - 12000) <= 2 && std::abs (e2 - 24000) <= 3 && std::abs (r + 6.0) < 1.0, "time e feedback: echi a 250 ms, -6 dB col 50 %", r);
    }
    // 2. offset +20 %: il destro a 300 ms; ping-pong: primo eco a sinistra, secondo a destra
    {
        auto p = mk ({ { BDelayIDs::time, 250.0f }, { BDelayIDs::offset, 20.0f } });
        auto y = run (*p, 20000, impulse);
        const int r = peakAt (y, 1, 6000, 19000);
        check (std::abs (r - 14400) <= 2, "offset +20 %: il destro a 300 ms", r);
        auto q = mk ({ { BDelayIDs::time, 250.0f }, { BDelayIDs::pingpong, 1.0f }, { BDelayIDs::feedback, 60.0f } });
        auto z = run (*q, 30000, impulse);
        const float l1 = std::abs (z.getSample (0, peakAt (z, 0, 6000, 18000))), r1 = std::abs (z.getSample (1, peakAt (z, 1, 6000, 18000)));
        const int r2 = peakAt (z, 1, 18001, 29000);
        check (l1 > 0.1f && r1 < 0.01f && std::abs (r2 - 24000) <= 3, "ping-pong: primo eco a sinistra, poi a destra", r2);
    }
    // 3. sync: 1/8 puntato a 120 bpm = 375 ms
    {
        const float ms = BDelayProcessor::leftMs (380.0f, true, 10, 120.0);
        check (std::abs (ms - 375.0f) < 0.01f, "sync: 1/8D a 120 bpm = 375 ms", ms);
    }
    // 4. tap: tre click a 500 ms di distanza -> 500 ms (e sync spento)
    {
        auto p = mk ({ { BDelayIDs::sync, 1.0f } });
        for (int k = 0; k < 3; ++k) { p->tap(); if (k < 2) juce::Thread::sleep (500); }
        auto* t = p->apvts.getParameter (BDelayIDs::time);
        const float ms = t->convertFrom0to1 (t->getValue());
        std::printf ("       tap a 500 ms: %.1f ms\n", ms);
        check (std::abs (ms - 500.0f) < 15.0f && p->apvts.getRawParameterValue (BDelayIDs::sync)->load() < 0.5f, "tap: media degli intervalli, sync spento", ms);
    }
    // 5. tutti i tipi con feedback 110 %: auto-oscillazione limitata, nessun NaN
    for (int t = 0; t < 5; ++t)
    {
        auto p = mk ({ { BDelayIDs::type, (float) t }, { BDelayIDs::feedback, 110.0f }, { BDelayIDs::time, 120.0f }, { BDelayIDs::smear, 50.0f } });
        auto y = run (*p, (int) (sr * 5), [] (int i) { return i < 4800 ? pluck (i, 220.0) : 0.0f; });
        float pk = 0; bool fin = true;
        for (int i = 0; i < y.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) { fin &= std::isfinite (y.getSample (c, i)); pk = std::max (pk, std::abs (y.getSample (c, i))); }
        std::printf ("       tipo %d a feedback 110 %%: picco %.2f\n", t, pk);
        check (fin && pk < 2.0f, "feedback 110 %: oscilla ma resta limitato", pk);
    }
    // 6. cambio di tempo mentre suona: la nota cambia altezza (come un nastro) ma senza discontinuità:
    //    la differenza seconda resta piccola rispetto alla prima (un click la farebbe grande quanto lei)
    {
        auto p = mk ({ { BDelayIDs::time, 300.0f }, { BDelayIDs::feedback, 40.0f } });
        auto sine = [] (int i) { return (float) (0.3 * std::sin (tp * 220.0 * i / sr)); };
        run (*p, 48000, sine);
        setP (p->apvts, BDelayIDs::time, 120.0f);
        auto b = run (*p, 24000, [&] (int i) { return sine (48000 + i); });
        float d1 = 0, d2 = 0;
        for (int i = 2; i < 24000; ++i)
        {
            const float a0 = b.getSample (0, i), a1 = b.getSample (0, i - 1), a2 = b.getSample (0, i - 2);
            d1 = std::max (d1, std::abs (a0 - a1)); d2 = std::max (d2, std::abs (a0 - 2 * a1 + a2));
        }
        std::printf ("       cambio di tempo: differenza seconda / prima = %.3f\n", d2 / d1);
        check (d2 < 0.3f * d1, "cambio di tempo: scivola come un nastro, niente click", d2 / d1);
    }
    // 7. bypass: con trails la coda continua, senza si spegne
    for (bool trails : { true, false })
    {
        auto p = mk ({ { BDelayIDs::trails, trails ? 1.0f : 0.0f }, { BDelayIDs::feedback, 60.0f }, { BDelayIDs::time, 200.0f } });
        run (*p, 12000, [] (int i) { return i < 4800 ? pluck (i, 220.0) : 0.0f; });
        setP (p->apvts, BDelayIDs::bypass, 1.0f);
        auto y = run (*p, 24000, [] (int) { return 0.0f; });
        const double tail = rmsDb (y, 0, 4800, 24000);
        std::printf ("       bypass con trails %s: coda %.1f dB\n", trails ? "on" : "off", tail);
        check (trails ? tail > -50.0 : tail < -90.0, trails ? "trails: in bypass la coda sfuma da sola" : "trails spento: in bypass la coda si ferma", tail);
    }
    // 7b. realismo: in un bbd e su un nastro le ripetizioni lunghe sono più scure delle corte
    for (int type : { (int) BDelayProcessor::Bbd, (int) BDelayProcessor::Tape })
    {
        auto tilt = [&] (float ms)
        {
            auto p = mk ({ { BDelayIDs::type, (float) type }, { BDelayIDs::time, ms }, { BDelayIDs::feedback, 0.0f } });
            juce::Random rnd (7);
            const int at = (int) (ms * 0.001f * sr);
            auto y = run (*p, at + 9600, [&] (int i) { return i < 4800 ? 0.2f * (rnd.nextFloat() - 0.5f) : 0.0f; });
            auto band = [&] (double hz)   // energia in banda (un tono singolo su un rumore oscilla troppo)
            {
                sp::Bq bp; bp.set (sp::Bq::BP, sr, hz, 1.4);
                double e = 0;
                for (int i = 0; i < at + 4800; ++i) { const double v = bp.process (y.getSample (0, i)); if (i >= at) e += v * v; }
                return 10.0 * std::log10 (e + 1e-30);
            };
            return band (4000.0) - band (500.0);
        };
        const double shortT = tilt (100.0f), longT = tilt (800.0f);
        std::printf ("       %s: 4 kHz rispetto a 500 Hz, eco a 100 ms %.1f dB, a 800 ms %.1f dB\n", type == BDelayProcessor::Bbd ? "bbd" : "tape", shortT, longT);
        check (shortT - longT > (type == BDelayProcessor::Bbd ? 15.0 : 3.0), "ritardo lungo = ripetizioni più scure (clock / velocità del nastro)", shortT - longT);
    }
    // 8. mix 0: il dry passa identico
    {
        auto p = mk ({ { BDelayIDs::mix, 0.0f } });
        auto y = run (*p, 24000, [] (int i) { return pluck (i, 110.0); });
        float d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, std::abs (y.getSample (0, i) - pluck (i, 110.0)));
        check (d < 1e-6f, "mix 0: uscita identica all'ingresso", d);
    }
    // 9. CPU
    {
        auto p = mk ({ { BDelayIDs::type, 1.0f }, { BDelayIDs::smear, 50.0f }, { BDelayIDs::mod, 50.0f } });
        const double cpu = cpuPercent (*p, [] (int i) { return pluck (i, 110.0); });
        std::printf ("       CPU (tape, smear, mod): %.3f %% di un core\n", cpu);
        check (cpu < 2.0, "CPU sotto il 2 % di un core", cpu);
    }
    // 10. interfaccia
    {
        auto p = mk ({ { BDelayIDs::mix, 30.0f }, { BDelayIDs::offset, 25.0f } });
        run (*p, 30000, [] (int i) { return i < 4800 ? pluck (i, 220.0) : 0.0f; });
        snapshot (*p, "b-delay.png");
        // slapback corto in bbd con ping-pong: la finestra si stringe (250 ms) e il grafico dice fin dove arrivano gli acuti
        auto q = mk ({ { BDelayIDs::mix, 30.0f }, { BDelayIDs::type, 2.0f }, { BDelayIDs::time, 60.0f }, { BDelayIDs::pingpong, 1.0f },
                       { BDelayIDs::feedback, 55.0f } });
        run (*q, 12000, [] (int i) { return i < 2400 ? pluck (i, 220.0) : 0.0f; });
        snapshot (*q, "b-delay_slap.png");
    }
    return finish();
}

// Test di b-focus: trasparenza bit per bit, riduzione del rumore, guardia dei transienti,
// rilascio adattivo, latenza, CPU e snapshot dell'interfaccia.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0.0)
{
    std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v);
    if (! ok) ++failures;
}

constexpr double sr = 48000.0;
constexpr int block = 256;
const double twoPi = juce::MathConstants<double>::twoPi;

void setP (BFocusProcessor& p, const char* id, float v)
{
    auto* par = p.apvts.getParameter (id);
    par->setValueNotifyingHost (par->convertTo0to1 (v));
}

std::unique_ptr<BFocusProcessor> make (float thr, float strength, float release)
{
    auto p = std::make_unique<BFocusProcessor>();
    p->setRateAndBufferSizeDetails (sr, block);
    setP (*p, BFocusIDs::threshold, thr);
    setP (*p, BFocusIDs::strength, strength);
    setP (*p, BFocusIDs::release, release);
    p->prepareToPlay (sr, block);
    return p;
}

// Elabora un segnale mono (su L e R); restituisce L. Opzionale: registra la riduzione per campione di blocco.
std::vector<float> run (BFocusProcessor& p, const std::vector<float>& x, std::vector<float>* grPerBlock = nullptr)
{
    std::vector<float> y (x.size());
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        const int n = (int) std::min ((size_t) block, x.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) { buf.setSample (0, i, x[pos + (size_t) i]); buf.setSample (1, i, x[pos + (size_t) i]); }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i) y[pos + (size_t) i] = buf.getSample (0, i);
        if (grPerBlock) grPerBlock->push_back (p.getEngine().getCurrentGr());
    }
    return y;
}

std::vector<float> noise (int n, float rmsDb, int seed)
{
    juce::Random r (seed);
    const float a = std::pow (10.0f, rmsDb / 20.0f) * 1.7320508f;   // rumore uniforme: rms = a / sqrt(3)
    std::vector<float> v ((size_t) n);
    for (auto& s : v) s = a * (2.0f * r.nextFloat() - 1.0f);
    return v;
}

// "Nota" DI sintetica: fondamentale + armoniche che decadono, attacco netto.
float note (int i, int start, double f0, float amp, double decaySec)
{
    if (i < start) return 0.0f;
    const double t = (i - start) / sr;
    double s = 0.0;
    for (int h = 1; h <= 12; ++h) s += std::sin (twoPi * f0 * h * t) / h * std::exp (-t * h * 0.8 / decaySec);
    return (float) (amp * s * std::exp (-t / decaySec));
}

double bandEnergyDb (const std::vector<float>& x, size_t from, double loHz, double hiHz)
{
    constexpr int order = 14, N = 1 << order;
    std::vector<float> d (2 * N, 0.0f);
    double e = 0.0;
    for (size_t off = from; off + N <= x.size(); off += N)
    {
        std::fill (d.begin(), d.end(), 0.0f);
        for (int i = 0; i < N; ++i) d[(size_t) i] = x[off + (size_t) i] * (0.5f - 0.5f * std::cos ((float) twoPi * i / N));
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (d.data(), true);
        for (int b = (int) (loHz * N / sr); b <= (int) (hiHz * N / sr) && b < N / 2; ++b) e += d[(size_t) b] * d[(size_t) b];
    }
    return 10.0 * std::log10 (e + 1e-30);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---------------------------------------------------------------- 1. strength 0% = bypass bit per bit
    {
        auto p = make (-42.0f, 0.0f, 180.0f);
        auto x = noise ((int) sr, -40.0f, 1);
        auto y = run (*p, x);
        check (std::memcmp (x.data(), y.data(), x.size() * sizeof (float)) == 0, "strength 0%: uscita identica bit per bit");
    }

    // ---------------------------------------------------------------- 2. aperto = trasparente bit per bit, latenza 0
    {
        auto p = make (-42.0f, 70.0f, 180.0f);
        std::vector<float> x ((size_t) sr);
        for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] = 0.3f * (float) (std::sin (twoPi * 110.0 * i / sr) + 0.3 * std::sin (twoPi * 3300.0 * i / sr));
        auto y = run (*p, x);
        int diff = 0;
        for (size_t i = (size_t) (0.05 * sr); i < x.size(); ++i) diff += ! juce::exactlyEqual (y[i], x[i]);
        check (diff == 0 && p->getLatencySamples() == 0, "segnale sopra soglia: uscita identica bit per bit, latenza 0 (campioni diversi)", diff);
    }

    // ---------------------------------------------------------------- 3. solo rumore (corde ferme): chiudono tutte e 4 le bande
    for (float strength : { 70.0f, 100.0f })
    {
        auto p = make (-42.0f, strength, 180.0f);
        auto x = noise ((int) (3 * sr), -60.0f, 2);
        auto y = run (*p, x);
        const size_t from = (size_t) sr;
        const double hf = bandEnergyDb (y, from, 5000, 15000) - bandEnergyDb (x, from, 5000, 15000);
        const double mid = bandEnergyDb (y, from, 350, 800) - bandEnergyDb (x, from, 350, 800);
        const double lf = bandEnergyDb (y, from, 40, 250) - bandEnergyDb (x, from, 40, 250);
        std::printf ("       strength %.0f%%: alte %.1f dB, medie (350-800 Hz) %.1f dB, basse (40-250 Hz) %.1f dB\n", strength, hf, mid, lf);
        check (hf < (strength > 90 ? -40.0 : -28.0) && mid < (strength > 90 ? -30.0 : -20.0) && lf < (strength > 90 ? -36.0 : -25.0),
               (juce::String ("solo rumore, strength ") + juce::String ((int) strength) + "%: tutto lo spettro ridotto (anche medie e basse)").toRawUTF8(), hf);
    }

    // ---------------------------------------------------------------- 3b. nota bassa tenuta senza armoniche alte: fruscio sotto la nota
    {
        auto p = make (-42.0f, 100.0f, 180.0f);
        auto x = noise ((int) (3 * sr), -62.0f, 12);
        for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] += 0.2f * (float) std::sin (twoPi * 82.4 * i / sr);
        auto y = run (*p, x);
        const size_t from = (size_t) sr;
        const double hf = bandEnergyDb (y, from, 5000, 15000) - bandEnergyDb (x, from, 5000, 15000);
        const double body = bandEnergyDb (y, from, 60, 110) - bandEnergyDb (x, from, 60, 110);
        std::printf ("       nota tenuta a 82 Hz: fruscio 5-15 kHz %.1f dB, fondamentale %.2f dB\n", hf, body);
        check (hf < -30.0 && std::abs (body) < 1.0, "nota bassa tenuta: fizz sotto la nota chiuso, corpo intatto", hf);
    }

    // ---------------------------------------------------------------- 4. attacco: da chiuso a trasparente
    {
        auto p = make (-42.0f, 100.0f, 180.0f);
        const int start = (int) (0.5 * sr);
        auto x = noise ((int) sr, -70.0f, 3);
        for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] += note (i, start, 330.0, 0.25f, 0.6);   // armoniche fino a 4 kHz
        auto y = run (*p, x);
        int firstExact = -1;
        for (int i = start; i < (int) x.size(); ++i)
        {
            bool allSame = true;
            for (int k = 0; k < 64 && i + k < (int) x.size(); ++k) allSame &= juce::exactlyEqual (y[(size_t) (i + k)], x[(size_t) (i + k)]);
            if (allSame) { firstExact = i; break; }
        }
        const double ms = (firstExact - start) / sr * 1000.0;
        check (firstExact >= 0 && ms < 1.0, "plettrata da stato chiuso: trasparente bit per bit dopo (ms)", ms);
    }

    // ---------------------------------------------------------------- 5. guardia dei transienti (ghost note sotto soglia)
    {
        auto runGuard = [&] (bool pluck)
        {
            auto p = make (-42.0f, 100.0f, 180.0f);
            const int start = (int) (1.0 * sr);
            auto x = noise ((int) (1.5 * sr), -70.0f, 4);
            if (pluck) for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] += note (i, start, 220.0, 0.012f, 0.3);
            std::vector<float> grs;
            run (*p, x, &grs);
            float best = -100.0f;   // riduzione minima nei primi 10 ms dopo l'attacco
            for (int b = start / block; b < (start + (int) (0.01 * sr)) / block; ++b) best = std::max (best, grs[(size_t) b]);
            return best;
        };
        const float idle = runGuard (false), ghost = runGuard (true);
        std::printf ("       riduzione nei primi 10 ms: rumore %.1f dB, ghost note sotto soglia %.1f dB\n", idle, ghost);
        check (ghost > idle + 10.0f, "ghost note sotto soglia: il filtro si apre comunque (dB di riduzione in meno)", ghost - idle);
    }

    // ---------------------------------------------------------------- 6. rilascio adattivo: pausa staccata contro sustain
    {
        auto timeToClose = [&] (bool staccato, float releaseMs = 1000.0f)
        {
            auto p = make (-42.0f, 100.0f, releaseMs);
            const int stop = (int) (0.4 * sr);
            auto x = noise ((int) (2.0 * sr), -70.0f, 5);
            for (int i = 0; i < (int) x.size(); ++i)
            {
                const double t = i / sr;
                const bool on = i < stop;
                float s = 0.0f;
                if (on || ! staccato) s += 0.2f * (float) std::sin (twoPi * 82.4 * t);        // fondamentale: continua solo in sustain
                if (on) s += 0.05f * (float) std::sin (twoPi * 2500.0 * t);                   // armoniche alte: si fermano
                x[(size_t) i] += s;
            }
            std::vector<float> grs;
            run (*p, x, &grs);
            for (size_t b = (size_t) (stop / block); b < grs.size(); ++b)
                if (grs[b] <= -20.0f) return (double) ((int) b * block - stop) / sr * 1000.0;
            return 9999.0;
        };
        const double sustain = timeToClose (false), stac = timeToClose (true);
        std::printf ("       chiusura a -20 dB dopo lo stop delle alte: in sustain %.0f ms, staccato %.0f ms (release 1000 ms)\n", sustain, stac);
        check (stac < 120.0 && sustain > 3.0 * stac, "staccato: il ronzio si chiude subito; in sustain resta il rilascio impostato", stac);

        // release minimo 2 ms: anche in sustain la chiusura è quasi immediata
        auto pr = make (-42.0f, 100.0f, 2.0f);
        auto* relP = pr->apvts.getParameter (BFocusIDs::release);
        const float relMin = relP->convertFrom0to1 (relP->getValue());
        const double fastClose = timeToClose (false, 2.0f);
        std::printf ("       release 2 ms: chiusura a -20 dB in sustain %.0f ms\n", fastClose);
        check (std::abs (relMin - 2.0f) < 0.01f && fastClose < 20.0, "release minimo 2 ms (parametro e chiusura in sustain, ms)", fastClose);
    }

    // ---------------------------------------------------------------- 6b. Auto-Learn (Dual-Cluster)
    {
        // 1,5 s di corde ferme (solo rumore), poi note: una leggera e due normali in sustain
        auto makeTake = [&]
        {
            auto x = noise ((int) (5.0 * sr), -66.0f, 8);
            for (int i = 0; i < (int) x.size(); ++i)
                x[(size_t) i] += note (i, (int) (1.5 * sr), 110.0, 0.03f, 0.8)     // tocco leggero
                               + note (i, (int) (2.5 * sr), 82.4, 0.25f, 0.8)
                               + note (i, (int) (3.6 * sr), 98.0, 0.25f, 0.8);
            return x;
        };
        auto p = make (-42.0f, 70.0f, 180.0f);
        p->toggleLearn();
        auto take = makeTake();
        run (*p, take);
        const bool applied = p->applyLearnResultIfReady();
        const auto r = p->getEngine().getLearnResult();
        auto val = [&] (const char* id) { auto* q = p->apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
        std::printf ("       learn: rumore HF %.1f dB, nota leggera HF %.1f dB, decadimento %.1f dB/s\n", r.noisePeakHf, r.quietPlayHf, r.decayRate);
        std::printf ("       -> threshold %.1f dB, strength %.0f %%, release %.0f ms, soglia LF interna %.1f dB\n",
                     val (BFocusIDs::threshold), val (BFocusIDs::strength), val (BFocusIDs::release), p->getEngine().getLfGate());
        const float thr = val (BFocusIDs::threshold);
        const float expStr = std::clamp ((r.noisePeakHf + 74.0f) / 36.0f * 100.0f, 25.0f, 100.0f);
        check (applied && r.played, "learn: entrambi i cluster rilevati e parametri applicati");
        check (thr >= r.noisePeakHf + 2.0f - 0.1f && thr <= r.quietPlayHf - 3.0f + 0.1f + 100.0f * (r.noisePeakHf + 2.0f > r.quietPlayHf - 3.0f),
               "learn: soglia tra il rumore (+2 dB) e la nota più leggera (-3 dB)", thr);
        juce::ignoreUnused (expStr);
        check (std::abs (val (BFocusIDs::strength) - 70.0f) < 0.01f && std::abs (val (BFocusIDs::release) - 180.0f) < 0.5f,
               "learn: regola solo il threshold (strength e release invariati)", val (BFocusIDs::strength));
        check (p->getEngine().getLfGate() >= -100.0f && p->getEngine().getLfGate() <= -35.0f, "learn: soglia interna del rilascio adattivo aggiornata", p->getEngine().getLfGate());
        std::printf ("       noise profile: %.1f / %.1f / %.1f / %.1f dB, offset %.1f / %.1f / %.1f\n", r.noiseDb[0], r.noiseDb[1], r.noiseDb[2], r.noiseDb[3],
                     r.offsets[1], r.offsets[2], r.offsets[3]);
        check (std::abs (p->getEngine().getBandOffset (2) - r.offsets[2]) < 0.01f && r.noiseDb[0] > -120.0f, "learn: noise profile a 4 bande e offset per banda applicati", r.offsets[2]);

        // annullamento: secondo clic durante l'ascolto -> nessun parametro cambia
        auto q = make (-42.0f, 70.0f, 180.0f);
        q->toggleLearn();
        run (*q, noise ((int) (1.0 * sr), -66.0f, 9));
        q->toggleLearn();
        run (*q, makeTake());
        auto qv = [&] (const char* id) { auto* a = q->apvts.getParameter (id); return a->convertFrom0to1 (a->getValue()); };
        check (! q->applyLearnResultIfReady() && ! q->isLearning() && std::abs (qv (BFocusIDs::threshold) + 42.0f) < 0.01f,
               "learn annullato: parametri invariati");

        // DI con ronzio (60 Hz a -45 dBFS): nessun frame sotto -58 dB LF -> cluster relativi
        {
            auto h = make (-42.0f, 70.0f, 180.0f);
            h->toggleLearn();
            auto hum = makeTake();
            for (int i = 0; i < (int) hum.size(); ++i) hum[(size_t) i] += 0.0056f * (float) std::sin (twoPi * 60.0 * i / sr);
            run (*h, hum);
            const bool ok = h->applyLearnResultIfReady();
            const auto hr = h->getEngine().getLearnResult();
            auto hv = [&] (const char* id) { auto* a = h->apvts.getParameter (id); return a->convertFrom0to1 (a->getValue()); };
            std::printf ("       learn con ronzio: rumore HF %.1f dB -> threshold %.1f dB, strength %.0f %%  [%s]\n",
                         hr.noisePeakHf, hv (BFocusIDs::threshold), hv (BFocusIDs::strength), h->getLearnStatus().toRawUTF8());
            check (ok && hr.played && hv (BFocusIDs::threshold) > hr.noisePeakHf + 1.9f && std::abs (hv (BFocusIDs::threshold) + 42.0f) > 0.5f,
                   "learn con ronzio di rete: parametri appresi comunque", hv (BFocusIDs::threshold));
        }
        // solo note, nessuna pausa -> fallisce con un messaggio, parametri invariati
        {
            auto f = make (-42.0f, 70.0f, 180.0f);
            f->toggleLearn();
            std::vector<float> x ((size_t) (5.0 * sr));
            for (int i = 0; i < (int) x.size(); ++i)
                x[(size_t) i] = note (i, 0, 82.4, 0.25f, 3.0) + note (i, (int) (2.0 * sr), 110.0, 0.25f, 3.0) + 0.0005f * std::sin ((float) i);
            run (*f, x);
            const bool ok = f->applyLearnResultIfReady();
            const auto msg = f->getLearnStatus();
            std::printf ("       learn senza pause: [%s]\n", msg.toRawUTF8());
            check (! ok && f->lastLearnFailed() && msg.contains ("no pause"), "learn senza pause: errore spiegato, parametri invariati");
        }
        // nessun audio (traccia ferma: arrivano solo zeri) -> errore "no audio"
        {
            auto z = make (-42.0f, 70.0f, 180.0f);
            z->toggleLearn();
            run (*z, std::vector<float> ((size_t) (5.0 * sr), 0.0f));
            z->applyLearnResultIfReady();
            check (z->getLearnStatus().contains ("no audio"), "learn senza audio: errore spiegato");
        }
    }

    // ---------------------------------------------------------------- 6c. chitarra molto distorta: DI con rumore -> b-focus -> +50 dB di
    // distorsione. Nota che decade in 3 s fino al rumore, poi pausa. Si misura (a) il rumore distorto nelle pause,
    // (b) il fruscio sopra 4 kHz nella coda, (c) le aperture/chiusure ripetute della banda alta sulla coda (chatter)
    {
        auto highGain = [] (float v) { return std::tanh (316.0f * v) * 0.5f; };   // +50 dB e clip
        auto take = [&] (bool withFocus, int& toggles, double& pause, double& tailHf)
        {
            auto p = make (-42.0f, 100.0f, 120.0f);
            juce::Random r (21);
            std::vector<float> x ((size_t) (8 * sr));
            for (int i = 0; i < (int) x.size(); ++i)
            {
                const double t = std::fmod (i / sr, 4.0);
                double s = 0;
                if (t < 3.0) for (int h = 1; h <= 10; ++h) s += std::sin (twoPi * 98.0 * h * i / sr) / h * std::exp (-t * h * 0.35);
                x[(size_t) i] = (float) (0.3 * s * std::exp (-t * 2.2)) + 0.00017f * (2.0f * r.nextFloat() - 1.0f);   // rumore ~ -80 dBFS
            }
            if (withFocus)   // come farebbe l'utente: learn sulla ripresa (corde ferme e note), poi si suona
            {
                p->toggleLearn();
                std::vector<float> lx (x.begin() + (long) (2.9 * sr), x.begin() + (long) (7.5 * sr));
                run (*p, lx); p->applyLearnResultIfReady();
            }
            std::vector<float> grs;
            auto y = withFocus ? run (*p, x, &grs) : x;
            toggles = 0; bool open = true;
            for (size_t b = (size_t) (4 * sr) / block; b < (size_t) (7 * sr) / block && b < grs.size(); ++b)
            { const bool o = grs[b] > -6.0f; if (o != open) { ++toggles; open = o; } }
            std::vector<float> z (y.size()); for (size_t i = 0; i < y.size(); ++i) z[i] = highGain (y[i]);
            double e = 0; for (int i = (int) (7.3 * sr); i < (int) (7.9 * sr); ++i) e += (double) z[(size_t) i] * z[(size_t) i];
            pause = 10 * std::log10 (e / (0.6 * sr) + 1e-30);
            tailHf = bandEnergyDb (z, (size_t) (6.0 * sr), 4000, 16000) - bandEnergyDb (z, (size_t) (6.0 * sr), 60, 1000);
        };
        int tOff, tOn; double pOff, pOn, hOff, hOn;
        take (false, tOff, pOff, hOff); take (true, tOn, pOn, hOn);
        { auto q = make (-42.0f, 100.0f, 120.0f); juce::ignoreUnused (q); }
        std::printf ("       distorsione +50 dB: pausa %.1f -> %.1f dBFS, alte nella coda (4-16k vs 60-1k) %.1f -> %.1f dB, chatter %d\n", pOff, pOn, hOff, hOn, tOn);
        check (pOn < pOff - 30.0 && hOn < hOff - 6.0 && tOn <= 2, "chitarra molto distorta: pause e coda pulite, nessun chatter", tOn);
    }

    // ---------------------------------------------------------------- 7. CPU (stereo, buffer 128)
    {
        auto p = std::make_unique<BFocusProcessor>();
        p->setRateAndBufferSizeDetails (sr, 128);
        p->prepareToPlay (sr, 128);
        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        juce::Random r (6);
        double secs = 0;
        for (int k = 0; k < (int) (10.0 * sr / 128); ++k)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buf.setSample (c, i, 0.01f * (r.nextFloat() - 0.5f));
            const auto t0 = juce::Time::getHighResolutionTicks();
            p->processBlock (buf, midi);
            secs += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        }
        check (secs * 10.0 < 1.0, "CPU % di un core (stereo)", secs * 10.0);
    }

    // ---------------------------------------------------------------- 8. snapshot (dopo un po' di rumore: filtro chiuso)
    {
        auto p = make (-42.0f, 70.0f, 180.0f);
        run (*p, noise ((int) (0.5 * sr), -66.0f, 7));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("gui_bfocus.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_bfocus.png");
        ed.reset();
    }

    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

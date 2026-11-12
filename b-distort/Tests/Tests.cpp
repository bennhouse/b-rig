// Test di b-distort: carattere armonico dei tipi, livelli, DC, aliasing, latenza/allineamento, CPU, snapshot.
#include "PluginProcessor.h"
#include "smp/TestUtil.h"
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const char* what, double v = 0.0)
{
    std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v);
    if (! ok) ++failures;
}

constexpr double sr = 48000.0;
constexpr int block = 512;
const char* names[] = { "tube", "drive", "rat", "fuzz", "fold" };
constexpr int kTypes = BDistortProcessor::NumTypes;

void setP (BDistortProcessor& p, const char* id, float v)
{
    auto* par = p.apvts.getParameter (id);
    par->setValueNotifyingHost (par->convertTo0to1 (v));
}

std::unique_ptr<BDistortProcessor> make (int type, float driveDb, float mix = 100.0f, float tone = 20000.0f)
{
    auto p = std::make_unique<BDistortProcessor>();
    p->setRateAndBufferSizeDetails (sr, block);
    setP (*p, BDistortIDs::type, (float) type);
    setP (*p, BDistortIDs::drive, driveDb);
    setP (*p, BDistortIDs::mix, mix);
    setP (*p, BDistortIDs::tone, tone);       // di solito aperto: si misura il circuito, non il controllo
    setP (*p, BDistortIDs::output, 0.0f);
    p->prepareToPlay (sr, block);
    return p;
}

// Processa un segnale mono (duplicato su L/R) e restituisce il canale sinistro.
template <typename Fn>
std::vector<float> run (BDistortProcessor& p, int total, Fn sig)
{
    std::vector<float> out ((size_t) total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) { const float x = sig (pos + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
    }
    return out;
}

// Spettro di ampiezza (finestra di Hann) di N campioni a partire da 'from'.
std::vector<float> spectrum (const std::vector<float>& x, size_t from, int order)
{
    const int N = 1 << order;
    std::vector<float> d ((size_t) (2 * N), 0.0f);
    for (int i = 0; i < N; ++i)
        d[(size_t) i] = x[from + (size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));
    juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (d.data(), true);
    d.resize ((size_t) (N / 2));
    return d;
}

double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// DI di chitarra simulata: note pizzicate (82-330 Hz, 12 armoniche che decadono), picchi a -12 dBFS.
float guitarDI (int i)
{
    static const double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6 };
    const double t = i / sr;
    const int k = (int) (t / 0.5);
    const double tt = t - 0.5 * k, f = notes[k % 6];
    double y = 0.0;
    for (int h = 1; h <= 12; ++h)
        y += std::sin (juce::MathConstants<double>::twoPi * f * h * tt) * std::exp (-tt * (1.5 + 0.4 * h)) / h;
    return (float) (0.25 * y / 1.6);
}

// Loudness pesata K (approssimata: passa-alto 38 Hz + shelf +4 dB sopra 1,5 kHz), in dB.
double loudness (const std::vector<float>& x, size_t from)
{
    auto hs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 1500.0, 0.707f, juce::Decibels::decibelsToGain (4.0f));
    auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 38.0, 0.5);
    juce::dsp::IIR::Filter<float> f1 (hs), f2 (hp);
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float y = f2.processSample (f1.processSample (x[i]));
        if (i >= from) e += (double) y * y;
    }
    return 10.0 * std::log10 (e / (double) (x.size() - from) + 1e-30);
}

// Differenza di loudness uscita - ingresso (dB) con la DI simulata.
double levelDb (int type, float driveDb)
{
    auto p = make (type, driveDb, 100.0f, 12000.0f);
    const int len = (int) (6.0 * sr);
    auto y = run (*p, len, guitarDI);
    std::vector<float> x ((size_t) len);
    for (int i = 0; i < len; ++i) x[(size_t) i] = guitarDI (i);
    return loudness (y, (size_t) sr) - loudness (x, (size_t) sr);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // benchmark: costo dell'oversampling da solo (alta qualità e normale) per confronto con i tipi
    if (argc > 1 && juce::String (argv[1]) == "--bench")
    {
        for (bool maxQ : { true, false })
        {
            juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, maxQ, true);
            os.initProcessing (128);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random r (3);
            double best = 1e9;
            for (int rep = 0; rep < 5; ++rep)
            {
                double secs = 0;
                for (int k = 0; k < (int) (2.0 * sr / 128); ++k)
                {
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buf.setSample (c, i, r.nextFloat() - 0.5f);
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    juce::dsp::AudioBlock<float> b (buf);
                    os.processSamplesUp (b);
                    os.processSamplesDown (b);
                    secs += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                }
                best = std::min (best, secs / 2.0 * 100.0);
            }
            std::printf ("  oversampling 4x IIR %s: %.3f %%, latenza %.1f campioni\n", maxQ ? "alta qualità" : "normale    ", best, os.getLatencyInSamples());
        }
        return 0;
    }

    // modalità calibrazione: stampa la tabella dei volumi (da incollare in PluginProcessor.cpp)
    if (argc > 1 && juce::String (argv[1]) == "--calibrate")
    {
        BDistortProcessor::calibrating = true;
        for (int t = 0; t < kTypes; ++t)
        {
            std::printf ("        {");
            for (int k = 0; k < 13; ++k) std::printf (" %.2ff%s", -levelDb (t, 3.0f * (float) k), k < 12 ? "," : "");
            std::printf (" },   // %s\n", names[t]);
        }
        return 0;
    }

    // ---------------------------------------------------------------- 1. carattere armonico, livello, DC
    // Sinusoide a 140,6 Hz (centrata su un bin FFT) a -6 dBFS, drive 12 dB.
    std::printf ("\n tipo   rms(dB)  H2(dB)  H3(dB)  DC\n");
    double rmsDb[kTypes];
    constexpr int order = 15, N = 1 << order;
    const double f0 = 96.0 * sr / N;
    for (int t = 0; t < kTypes; ++t)
    {
        auto p = make (t, 12.0f);
        auto y = run (*p, N + 8192, [&] (int i) { return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * f0 * i / sr); });
        double e = 0, sum = 0; bool finite = true;
        for (size_t i = 8192; i < y.size(); ++i) { e += y[i] * y[i]; sum += y[i]; finite &= std::isfinite (y[i]); }
        rmsDb[t] = db (std::sqrt (e / N));
        const auto s = spectrum (y, 8192, order);
        const double h1 = s[96], h2 = s[192], h3 = s[288];
        const double dc = sum / N;
        std::printf (" %-5s  %6.1f   %6.1f  %6.1f  %.5f\n", names[t], rmsDb[t], db (h2 / h1), db (h3 / h1), dc);
        check (finite && std::abs (dc) < 2e-3, (juce::String (names[t]) + ": uscita valida, senza DC").toRawUTF8(), dc);
        // simmetria: drive/rat/fold solo armoniche dispari; tube/fuzz anche pari
        const bool asym = t == BDistortProcessor::Tube || t == BDistortProcessor::Fuzz;
        check (asym ? db (h2 / h1) > -40.0 : db (h2 / h1) < -60.0,
               (juce::String (names[t]) + (asym ? ": armoniche pari presenti (asimmetrico)" : ": solo armoniche dispari (simmetrico)")).toRawUTF8(), db (h2 / h1));
    }
    juce::ignoreUnused (rmsDb);

    // ---------------------------------------------------------------- 1b. volumi bilanciati (DI di chitarra, loudness K)
    {
        double worst = 0.0;
        std::printf ("\n drive   tube   drive    rat   fuzz   fold   (uscita - ingresso, dB)\n");
        for (float d : { 0.0f, 4.5f, 10.5f, 16.5f, 22.5f, 28.5f, 36.0f })   // anche tra i punti della tabella
        {
            std::printf (" %4.0f ", d);
            for (int t = 0; t < kTypes; ++t)
            {
                const double l = levelDb (t, d);
                worst = std::max (worst, std::abs (l));
                std::printf (" %6.2f", l);
            }
            std::printf ("\n");
        }
        check (worst < 1.0, "volumi: tutti i tipi e tutti i drive suonano forti quanto l'ingresso (scarto max dB)", worst);
    }

    // ---------------------------------------------------------------- 1c. caratteri distinti
    {
        // equilibrio delle medie (800-1250 Hz) rispetto a bassi (200-400) e alte (2,5-4 kHz) con la DI
        auto midBalance = [&] (int t)
        {
            auto p = make (t, 18.0f);
            const int len = (int) (3.0 * sr);
            auto y = run (*p, len, guitarDI);
            std::vector<float> seg (y.begin() + 8192, y.begin() + 8192 + (1 << 16));
            const auto s = spectrum (seg, 0, 16);
            auto band = [&] (double lo, double hi)
            {
                double e = 0; for (int b = (int) (lo * 65536 / sr); b <= (int) (hi * 65536 / sr); ++b) e += s[(size_t) b] * s[(size_t) b];
                return e / (hi - lo);
            };
            return 10.0 * std::log10 (band (800, 1250) / (0.5 * (band (200, 400) + band (2500, 4000))));
        };
        double mb[kTypes];
        for (int t = 0; t < kTypes; ++t) mb[t] = midBalance (t);
        std::printf ("       medie rispetto a bassi+alte: tube %.1f, drive %.1f, rat %.1f, fuzz %.1f, fold %.1f dB\n", mb[0], mb[1], mb[2], mb[3], mb[4]);
        check (mb[BDistortProcessor::Fuzz] < mb[BDistortProcessor::Rat] - 3.0 && mb[BDistortProcessor::Fuzz] < mb[BDistortProcessor::Drive] - 3.0,
               "fuzz: medie scavate rispetto a drive e rat (dB)", mb[BDistortProcessor::Fuzz] - mb[BDistortProcessor::Rat]);

        // drive: distorce le medio-alte e lascia pulite le basse (THD a 100 Hz contro 1,6 kHz)
        constexpr int order2 = 15, N2 = 1 << order2;
        auto thd = [&] (int t, double f, float driveDb, float amp)
        {
            const double fb = std::round (f * N2 / sr) * sr / N2;
            auto p = make (t, driveDb);
            auto y = run (*p, N2 + 8192, [&] (int i) { return amp * (float) std::sin (juce::MathConstants<double>::twoPi * fb * i / sr); });
            const auto s = spectrum (y, 8192, order2);
            const int k = (int) std::round (fb * N2 / sr);
            double h = 0; for (int m = 2; m <= 7 && m * k < N2 / 2; ++m) h += s[(size_t) (m * k)] * s[(size_t) (m * k)];
            return db (std::sqrt (h) / s[(size_t) k]);
        };
        const double dLow = thd (BDistortProcessor::Drive, 100.0, 18.0f, 0.25f), dHigh = thd (BDistortProcessor::Drive, 1600.0, 18.0f, 0.25f);
        std::printf ("       drive THD: 100 Hz %.1f dB, 1,6 kHz %.1f dB\n", dLow, dHigh);
        check (dHigh > dLow + 10.0, "drive: bassi puliti, distorsione sulle medio-alte (dB)", dHigh - dLow);
    }

    // ---------------------------------------------------------------- 2. RAT: più distorsione sulle medio-alte
    {
        auto thd = [&] (double f)
        {
            const double fb = std::round (f * N / sr) * sr / N;
            auto p = make (BDistortProcessor::Rat, 6.0f);
            auto y = run (*p, N + 8192, [&] (int i) { return 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * fb * i / sr); });
            const auto s = spectrum (y, 8192, order);
            const int k = (int) std::round (fb * N / sr);
            double h = 0; for (int m = 2; m <= 7 && m * k < N / 2; ++m) h += s[(size_t) (m * k)] * s[(size_t) (m * k)];
            return db (std::sqrt (h) / s[(size_t) k]);
        };
        const double low = thd (200.0), mid = thd (1600.0);
        std::printf ("       rat THD: 200 Hz %.1f dB, 1,6 kHz %.1f dB\n", low, mid);
        check (mid > low + 3.0, "rat: distorce di più le medio-alte (rete di guadagno dell'op-amp)", mid - low);
    }

    // ---------------------------------------------------------------- 3. aliasing: oversampling contro curva senza oversampling
    {
        const double fa = std::round (4987.0 * N / sr) * sr / N;   // ~5 kHz, centrata su un bin
        const int ka = (int) std::round (fa * N / sr);
        auto aliasDb = [&] (const std::vector<float>& y)
        {
            const auto s = spectrum (y, 8192, order);
            double harm = 0, other = 0;
            for (int b = 4; b < N / 2; ++b)
            {
                bool isHarm = false;
                for (int m = 1; m * ka < N / 2 + 3; ++m) if (std::abs (b - m * ka) <= 3) isHarm = true;
                (isHarm ? harm : other) += s[(size_t) b] * s[(size_t) b];
            }
            return 10.0 * std::log10 (other / harm);
        };
        for (int t : { (int) BDistortProcessor::Drive, (int) BDistortProcessor::Rat, (int) BDistortProcessor::Fuzz, (int) BDistortProcessor::Fold })
        {
            auto p = make (t, 30.0f);
            auto sig = [&] (int i) { return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * fa * i / sr); };
            const double withOs = aliasDb (run (*p, N + 8192, sig));
            std::vector<float> naive ((size_t) (N + 8192));
            const float d = std::pow (10.0f, 30.0f / 20.0f);
            for (int i = 0; i < N + 8192; ++i) naive[(size_t) i] = BDistortProcessor::shape (t, sig (i), d);
            const double without = aliasDb (naive);
            std::printf ("       %s a 5 kHz, drive 30 dB: aliasing %.1f dB (senza oversampling %.1f dB)\n", names[t], withOs, without);
            check (withOs < without - 10.0, (juce::String (names[t]) + ": l'oversampling riduce l'aliasing (dB in meno)").toRawUTF8(), without - withOs);
        }
    }

    // ---------------------------------------------------------------- 4. latenza dichiarata e dry/wet allineati
    {
        auto p = make (BDistortProcessor::Tube, 0.0f, 0.0f);   // solo dry
        const int L = p->getLatencySamples();
        auto y = run (*p, 4096, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
        int peak = 0; for (int i = 0; i < 4096; ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) peak])) peak = i;
        check (peak - 100 == L, "latenza dichiarata = ritardo misurato del dry (campioni)", L);
        auto q = make (BDistortProcessor::Fold, 0.0f, 100.0f);    // solo wet, drive 0, nessun filtro: quasi lineare
        auto z = run (*q, 8192, [] (int i) { return 0.01f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / sr); });
        double best = -1e9; int bestLag = 0;
        for (int lag = -8; lag <= 8; ++lag)
        {
            double c = 0; for (int i = 4000; i < 8000; ++i) c += z[(size_t) i] * 0.01 * std::sin (juce::MathConstants<double>::twoPi * 200.0 * (i - L - lag) / sr);
            if (c > best) { best = c; bestLag = lag; }
        }
        check (bestLag == 0, "wet allineato al dry (nessuno sfasamento nel mix)", bestLag);
    }

    // ---------------------------------------------------------------- 4b. low cut: curva disegnata = audio
    {
        auto p = make (BDistortProcessor::Tube, 12.0f, 0.0f);   // solo dry: il percorso lineare
        setP (*p, BDistortIDs::locut, 200.0f);
        double worst = 0.0;
        for (double f : { 50.0, 100.0, 200.0, 400.0, 2000.0 })
        {
            auto y = run (*p, 24000, [&] (int i) { return 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / sr); });
            float pk = 0.0f; for (int i = 16000; i < 24000; ++i) pk = std::max (pk, std::abs (y[(size_t) i]));
            worst = std::max (worst, std::abs (db (pk / 0.1) - p->responseDb (f)));
        }
        check (worst < 0.2, "low cut: la curva del grafico coincide con l'audio (errore max dB)", worst);
        auto q = make (BDistortProcessor::Tube, 12.0f, 0.0f);
        auto y = run (*q, 4096, [] (int i) { return i == 100 ? 1.0f : 0.0f; });
        check (std::abs (y[(size_t) (100 + q->getLatencySamples())] - 1.0f) < 1e-6, "low cut a 0 Hz: spento (dry identico)");
    }

    // ---------------------------------------------------------------- 5. CPU per tipo (stereo, buffer 128)
    for (int t = 0; t < kTypes; ++t)
    {
        auto p = std::make_unique<BDistortProcessor>();
        p->setRateAndBufferSizeDetails (sr, 128);
        setP (*p, BDistortIDs::type, (float) t);
        p->prepareToPlay (sr, 128);
        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        juce::Random r (3);
        double secs = 0;
        const int blocks = (int) (10.0 * sr / 128);
        for (int k = 0; k < blocks; ++k)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buf.setSample (c, i, r.nextFloat() - 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks();
            p->processBlock (buf, midi);
            secs += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        }
        check (secs / 10.0 * 100.0 < 3.0, (juce::String ("CPU % di un core: ") + names[t]).toRawUTF8(), secs / 10.0 * 100.0);
    }

    // ---------------------------------------------------------------- 6. snapshot dell'interfaccia
    {
        BDistortProcessor p;
        p.setRateAndBufferSizeDetails (sr, block);
        setP (p, BDistortIDs::type, (float) BDistortProcessor::Rat);
        setP (p, BDistortIDs::locut, 120.0f);
        setP (p, BDistortIDs::tone, 6000.0f);
        p.prepareToPlay (sr, block);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
        { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
        // un po' di DI di chitarra, così lo spettro dal vivo compare nello snapshot
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < (int) sr; pos += block)
        {
            for (int i = 0; i < block; ++i) { const float x = guitarDI (pos + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);   // qualche giro del timer dell'interfaccia
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("gui_bdistort.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), "gui_bdistort.png");
        ed.reset();
    }

    // tone (bilanciamento): 0 % scuro, 100 % brillante, 50 % piatto (risposta dei filtri d'uscita)
    {
        auto p = make (BDistortProcessor::Tube, 12.0f);
        auto at = [&] (float tone, double hz) { setP (*p, BDistortIDs::tilt, tone); return (double) p->responseDb (hz); };
        const double flat200 = at (50.0f, 200.0), flat4k = at (50.0f, 4000.0);
        const double dark = at (0.0f, 200.0) - at (0.0f, 4000.0), bright = at (100.0f, 200.0) - at (100.0f, 4000.0);
        std::printf ("       tone: basse - alte (200 Hz - 4 kHz) a 0 %% %+.1f dB, 50 %% %+.1f dB, 100 %% %+.1f dB\n", dark, flat200 - flat4k, bright);
        check (dark > 8.0 && bright < -8.0 && std::abs (flat200 - flat4k) < 0.5, "tone: da scuro a brillante, piatto a metà", dark - bright);
        // in audio: a 50 % il suono è lo stesso di prima (le due metà si risommano identiche)
        auto q = make (BDistortProcessor::Drive, 18.0f);
        auto y50 = run (*q, 24000, [] (int i) { return 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * i / sr); });
        auto r = make (BDistortProcessor::Drive, 18.0f);
        setP (*r, BDistortIDs::tilt, 100.0f);
        auto y100 = run (*r, 24000, [] (int i) { return 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * i / sr); });
        double e50 = 0, e100 = 0;
        for (size_t i = 12000; i < 24000; ++i) { e50 += (double) y50[i] * y50[i]; e100 += (double) y100[i] * y100[i]; }
        check (std::isfinite (e100) && std::abs (e100 - e50) > 1.0e-3 * e50, "tone in audio: cambia il suono, resta stabile", 10 * std::log10 (e100 / e50));
    }
    std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures);
    return failures == 0 ? 0 : 1;
}

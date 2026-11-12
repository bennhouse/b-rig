#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <complex>

namespace
{
    inline float dB2lin (float db) { return std::pow (10.0f, db / 20.0f); }
    inline float lpCoef (double hz, double fs) { return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / fs)); }
}

// ----------------------------------------------------------------------------- voicing
const BDistortProcessor::Voicing& BDistortProcessor::voicing (int t)
{
    static const Voicing table[NumTypes] = {
        //  preHp  emph     postLp   gain   (tube e fuzz tolgono anche la continua: vedi runNonLinear)
        {  30.0f,    0.0f, 9000.0f, 2.0f },   // tube: accoppiamento 30 Hz, Miller 9 kHz
        {   0.0f,  720.0f, 5000.0f, 2.0f },   // drive: distorce sopra 720 Hz, tono a 5 kHz
        {  60.0f, 1500.0f,    0.0f, 6.0f },   // rat (banda limitata dall'op-amp, vedi runNonLinear)
        {  80.0f,    0.0f, 7000.0f, 4.0f },   // fuzz
        {   0.0f,    0.0f,    0.0f, 1.0f },   // fold
    };
    return table[juce::jlimit (0, NumTypes - 1, t)];
}

// Allineamento di volume misurato con una DI di chitarra simulata (picchi a -12 dBFS),
// loudness pesata K, drive da 0 a 36 dB a passi di 3 dB. Rigenerare con: BDistortTests --calibrate
namespace
{
    constexpr int kTrimPoints = 13;
    constexpr float kTrimTable[BDistortProcessor::NumTypes][kTrimPoints] = {
        { -4.48f, -7.34f, -10.08f, -12.62f, -14.87f, -16.73f, -18.16f, -19.17f, -19.82f, -20.23f, -20.50f, -20.68f, -20.81f },   // tube
        { -1.85f, -2.63f, -3.60f, -4.72f, -5.90f, -7.07f, -8.20f, -9.28f, -10.31f, -11.25f, -12.07f, -12.74f, -13.22f },   // drive
        { -14.91f, -16.54f, -17.86f, -18.84f, -19.51f, -19.94f, -20.23f, -20.42f, -20.58f, -20.72f, -20.81f, -20.87f, -20.91f },   // rat
        { -12.66f, -14.11f, -15.18f, -15.88f, -16.33f, -16.62f, -16.81f, -16.97f, -17.09f, -17.19f, -17.28f, -17.35f, -17.41f },   // fuzz
        { -3.79f, -6.74f, -9.62f, -12.36f, -14.84f, -16.85f, -18.13f, -18.56f, -18.49f, -18.36f, -18.69f, -19.06f, -19.36f },   // fold
    };

    // tanh veloce (Padé 3/2, raccordo liscio a ±1 per |x| >= 3): una divisione invece di un'esponenziale
    inline float fastTanh (float x) noexcept
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // seno veloce di (pi/2 * u), periodico: per il wavefolder (errore < 0,1 %)
    inline float foldSin (float u) noexcept
    {
        float w = u * 0.25f;
        w -= std::floor (w + 0.5f);                     // fase in [-0,5, 0,5)
        const float y = 8.0f * w * (1.0f - 2.0f * std::abs (w));
        return 0.225f * (y * std::abs (y) - y) + y;
    }

    constexpr float kTubeBias1 = 0.30f, kTubeBias2 = 0.25f;
    const float kTanhB1 = std::tanh (kTubeBias1), kTanhB2 = std::tanh (kTubeBias2);

    inline float triode1 (float u) noexcept { return fastTanh (u + kTubeBias1) - kTanhB1; }
    // il secondo stadio inverte la fase: la polarizzazione agisce sull'altra semionda
    inline float triode2 (float u) noexcept { return fastTanh (u - kTubeBias2) + kTanhB2; }
    inline float ratClip (float u) noexcept { const float u2 = u * u; return u / std::sqrt (std::sqrt (1.0f + u2 * u2)); }
    // transistor saturati: la semionda negativa satura prima (armoniche pari)
    inline float fuzzClip (float u) noexcept { return u >= 0.0f ? fastTanh (u) : 0.6f * fastTanh (u * (1.0f / 0.6f)); }
}

float BDistortProcessor::trimDb (int type, float driveDb) noexcept
{
    if (calibrating) return 0.0f;
    const float pos = juce::jlimit (0.0f, (float) (kTrimPoints - 1), driveDb / 3.0f);
    const int i = juce::jmin ((int) pos, kTrimPoints - 2);
    const float t = pos - (float) i;
    const auto& row = kTrimTable[juce::jlimit (0, NumTypes - 1, type)];
    return row[i] + t * (row[i + 1] - row[i]);
}

// Curva statica (senza filtri) di un tipo con drive lineare d: la stessa matematica del motore audio.
float BDistortProcessor::shape (int type, float x, float d) noexcept
{
    const float g = d * voicing (type).gainMul;
    switch (type)
    {
        case Tube:  { const float gs = std::sqrt (g); return triode2 (gs * triode1 (gs * x)); }
        case Drive: return x + 0.5f * fastTanh (g * x);
        case Rat:   return ratClip (g * x);
        case Fuzz:  { const float gs = 1.5f * std::sqrt (g); return fuzzClip (gs * fuzzClip (gs * x)); }
        case Fold:  return foldSin (g * x);
        default:    return x;
    }
}

// ----------------------------------------------------------------------------- setup
BDistortProcessor::BDistortProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BDistort", createLayout())
{
    pType   = apvts.getRawParameterValue (BDistortIDs::type);
    pDrive  = apvts.getRawParameterValue (BDistortIDs::drive);
    pTone   = apvts.getRawParameterValue (BDistortIDs::tone);
    pTilt   = apvts.getRawParameterValue (BDistortIDs::tilt);
    pMix    = apvts.getRawParameterValue (BDistortIDs::mix);
    pOutput = apvts.getRawParameterValue (BDistortIDs::output);
    pLocut  = apvts.getRawParameterValue (BDistortIDs::locut);
    // ogni tipo ricorda drive, tono, mix e low cut (tutto tranne output) finché il plugin resta caricato
    typeMemory.add (apvts.getParameter (BDistortIDs::drive), [] (int) { return 12.0f; });
    typeMemory.add (apvts.getParameter (BDistortIDs::tone), [] (int) { return 12000.0f; });
    typeMemory.add (apvts.getParameter (BDistortIDs::tilt), [] (int) { return 50.0f; });
    typeMemory.add (apvts.getParameter (BDistortIDs::mix), [] (int) { return 100.0f; });
    typeMemory.add (apvts.getParameter (BDistortIDs::locut), [] (int) { return 0.0f; });
}

juce::AudioProcessorValueTreeState::ParameterLayout BDistortProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { BDistortIDs::type, 1 }, "Type",
        StringArray { "Tube", "Drive", "Rat", "Fuzz", "Fold" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f, AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    NormalisableRange<float> toneRange (500.0f, 20000.0f, 1.0f);
    toneRange.setSkewForCentre (3162.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::tone, 1 }, "Hi cut", toneRange, 12000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::tilt, 1 }, "Tone",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    NormalisableRange<float> lcRange (0.0f, 800.0f, 1.0f);
    lcRange.setSkewForCentre (150.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BDistortIDs::locut, 1 }, "Low cut", lcRange, 0.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    return layout;
}

bool BDistortProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BDistortProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    srOs = sr * (1 << kOsLog2);

    // oversampling 4x con filtri half-band IIR polifase: latenza minima (pochi campioni, intera)
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, kOsLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlock);
    osLatency = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (osLatency);

    driveArr.assign ((size_t) maxBlock, 1.0f);
    dryLine.setSize (2, juce::nextPowerOfTwo (osLatency + maxBlock + 1));   // potenza di 2: indice con maschera, niente modulo
    smoothCoef = std::exp (-1.0f / (0.02f * (float) sr));   // ~20 ms di smoothing sui parametri
    coeffType = -1;
    reset();
}

void BDistortProcessor::reset()
{
    st = {};
    dryLine.clear();
    dryPos = 0;
    if (oversampler) oversampler->reset();
    sDrive = dB2lin (pDrive->load());
    sMix = pMix->load() / 100.0f;
    tiltGains (pTilt->load(), sLowG, sHighG);
    sOut = dB2lin (pOutput->load());
    sTrim = dB2lin (trimDb ((int) pType->load(), pDrive->load()));
}

float BDistortProcessor::transfer (float x) const
{
    // forma della curva, normalizzata al suo massimo tra -1 e +1 (il volume reale è allineato a parte)
    const int t = juce::jlimit (0, NumTypes - 1, (int) pType->load());
    const float d = dB2lin (pDrive->load());
    if (t != curveType || ! juce::exactlyEqual (d, curveDrive))   // il massimo si ricalcola solo se cambia la curva
    {
        float pk = 1.0e-6f;
        for (int i = 0; i <= 64; ++i) pk = std::max (pk, std::abs (shape (t, -1.0f + (float) i / 32.0f, d)));
        curveType = t; curveDrive = d; curvePeak = pk;
    }
    const float peak = curvePeak;
    const float m = pMix->load() / 100.0f;
    return (x * (1.0f - m) + shape (t, x, d) / peak * m) * dB2lin (pOutput->load());
}

float BDistortProcessor::responseDb (double hz) const
{
    const double fs = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    // tono: passa-basso del 1° ordine (a una sola uscita) sul distorto, sommato al dry secondo il mix
    const double fc = juce::jlimit (20.0, 0.45 * fs, (double) pTone->load());
    const double c = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / fs);
    const std::complex<double> z = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / fs);
    const std::complex<double> tone = c / (1.0 - (1.0 - c) * z);
    // tone: passa-basso del 1° ordine a 800 Hz e il suo complemento, pesati (a 50 % somma piatta)
    float gl, gh; tiltGains (pTilt->load(), gl, gh);
    const double ct = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * kTiltHz / fs);
    const std::complex<double> lpT = ct / (1.0 - (1.0 - ct) * z);
    const std::complex<double> tilt = lpT * (double) gl + (1.0 - lpT) * (double) gh;
    const double m = pMix->load() / 100.0;
    double mag = std::abs ((1.0 - m) + m * tone * tilt);
    // low cut: Butterworth del 2° ordine
    const double lc = pLocut->load();
    if (lc >= 1.0) { const double r = hz / lc; mag *= r * r / std::sqrt (1.0 + r * r * r * r); }
    return (float) (20.0 * std::log10 (juce::jmax (mag, 1.0e-6)));
}

// ----------------------------------------------------------------------------- DSP
// Parte non lineare, a frequenza di oversampling. Il tipo è un parametro di template:
// nessun "switch" dentro il ciclo per campione.
template <int T>
void BDistortProcessor::runNonLinear (float* x, int numOs, int ch) noexcept
{
    const auto& v = voicing (T);
    auto& s = st[(size_t) ch];
    constexpr int os = 1 << kOsLog2;
    float cGbw = 1.0f, g = 1.0f, gs = 1.0f;

    for (int j = 0; j < numOs; ++j)
    {
        if (j % os == 0)   // guadagni e banda dell'op-amp: una volta per campione base
        {
            g = driveArr[(size_t) (j / os)] * v.gainMul;
            if constexpr (T == Tube) gs = std::sqrt (g);
            if constexpr (T == Fuzz) gs = 1.5f * std::sqrt (g);
            if constexpr (T == Rat)
                if (j % (8 * os) == 0)   // gain-bandwidth: più guadagno = banda più stretta
                    cGbw = lpCoef (juce::jlimit (1500.0, 20000.0, 250000.0 / (double) g), srOs);
        }
        float in = x[j];

        if constexpr (T == Tube || T == Rat || T == Fuzz)   // passa-alto d'ingresso
        {
            s.preLp += cPreHp * (in - s.preLp);
            in -= s.preLp;
        }

        float y;
        if constexpr (T == Tube)
        {
            float a = triode1 (gs * in);
            s.stage += cPost * (a - s.stage);          // Miller tra i due stadi
            s.couple += cCouple * (s.stage - s.couple); // condensatore di accoppiamento
            y = triode2 (gs * (s.stage - s.couple));
        }
        else if constexpr (T == Drive)
        {
            // overdrive verde: le basse passano pulite, solo la parte sopra 720 Hz va nei diodi
            s.emphLp += cEmph * (in - s.emphLp);
            y = in + 0.5f * fastTanh (g * (in - s.emphLp));
        }
        else if constexpr (T == Rat)
        {
            // rete di guadagno dell'op-amp: le frequenze sopra ~1,5 kHz ricevono ~4x più guadagno
            s.emphLp += cEmph * (in - s.emphLp);
            const float u = g * (in + 3.0f * (in - s.emphLp));
            s.gbw += cGbw * (u - s.gbw);
            y = ratClip (s.gbw);
        }
        else if constexpr (T == Fuzz)
        {
            const float a = fuzzClip (gs * in);
            s.stage += cStage * (a - s.stage);          // le alte del 1° stadio si arrotondano
            s.couple += cCouple * (s.stage - s.couple);
            const float b = fuzzClip (gs * (s.stage - s.couple));
            // tone stack: passa-basso 600 Hz + passa-alto 1,8 kHz = medie scavate (-6 dB a ~1 kHz)
            s.scoopLp += cScoopLp * (b - s.scoopLp);
            s.scoopHp += cScoopHp * (b - s.scoopHp);
            y = s.scoopLp + (b - s.scoopHp);
        }
        else
            y = foldSin (g * in);

        if constexpr (T == Drive || T == Fuzz)                // passa-basso dopo il clipping
        {
            s.post += cPost * (y - s.post);
            y = s.post;
        }
        if constexpr (T == Tube || T == Fuzz)                 // via la componente continua
        {
            const float yy = y - s.dcX + cDc * s.dcY;
            s.dcX = y; s.dcY = yy; y = yy;
        }
        x[j] = y;
    }
}

void BDistortProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2, getTotalNumInputChannels());
    const int total = buffer.getNumSamples();
    const int type = juce::jlimit (0, NumTypes - 1, (int) pType->load());
    const bool analyse = spectrumActive.load (std::memory_order_relaxed);
    if (analyse) inTap.push (buffer.getArrayOfReadPointers(), numCh, total);
    {
        float pk = 0.0f;
        for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, total));
        const float prev = inputPeak.load (std::memory_order_relaxed);
        const float fall = std::exp (-(float) total / (0.15f * (float) sr));
        inputPeak.store (pk > prev ? pk : prev * fall, std::memory_order_relaxed);
    }

    if (type != coeffType)   // nuovo tipo: coefficienti e stati da capo
    {
        const auto& v = voicing (type);
        cPreHp = v.preHpHz > 0.0f ? lpCoef (v.preHpHz, srOs) : 0.0f;
        cEmph  = v.emphHz  > 0.0f ? lpCoef (v.emphHz, srOs) : 0.0f;
        cPost  = v.postLpHz > 0.0f ? lpCoef (v.postLpHz, srOs) : 1.0f;
        cStage = lpCoef (6000.0, srOs);
        cCouple = lpCoef (type == Fuzz ? 100.0 : 20.0, srOs);
        cScoopLp = lpCoef (600.0, srOs);
        cScoopHp = lpCoef (1800.0, srOs);
        cDc    = (float) (1.0 - juce::MathConstants<double>::twoPi * 5.0 / srOs);
        for (auto& s : st) { const float t = s.tone; s = {}; s.tone = t; }
        coeffType = type;
    }

    const float tDrive = dB2lin (pDrive->load());
    const float tMix = pMix->load() / 100.0f;
    const float tOut = dB2lin (pOutput->load());
    const float tTrim = dB2lin (trimDb (type, pDrive->load()));   // stesso volume per ogni tipo e drive

    // low cut in uscita (0 Hz = spento): SVF TPT a 12 dB/ott (Butterworth), coefficienti solo se cambia
    const float lc = pLocut->load();
    const bool locutOn = lc >= 1.0f;
    if (locutOn && std::abs (lc - lcHz) > 0.01f)
    {
        lcHz = lc;
        lcG = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) lc, 0.45 * sr) / sr);
        lcK = 1.41421356f;
        lcA1 = 1.0f / (1.0f + lcG * (lcG + lcK));
        lcA2 = lcG * lcA1;
        lcA3 = lcG * lcA2;
    }
    const float cTone = lpCoef (juce::jlimit (20.0, 0.45 * sr, (double) pTone->load()), sr);
    const float cTilt = lpCoef ((double) kTiltHz, sr);
    float tLowG, tHighG; tiltGains (pTilt->load(), tLowG, tHighG);
    const float k = smoothCoef;
    const int lineMask = dryLine.getNumSamples() - 1;

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);

        // drive con smoothing, un valore per campione base
        float d = sDrive;
        for (int i = 0; i < n; ++i) driveArr[(size_t) i] = d = tDrive + k * (d - tDrive);
        sDrive = d;

        // dry nella linea di ritardo (resta allineato al segnale che passa dall'oversampling)
        for (int c = 0; c < numCh; ++c)
        {
            const float* x = buffer.getReadPointer (c, start);
            float* line = dryLine.getWritePointer (c);
            for (int i = 0; i < n; ++i) line[(dryPos + i) & lineMask] = x[i];
        }

        // parte non lineare in oversampling 4x
        auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numCh)
                                                          .getSubBlock ((size_t) start, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        for (int c = 0; c < numCh; ++c)
        {
            float* u = up.getChannelPointer ((size_t) c);
            const int numOs = (int) up.getNumSamples();
            switch (type)
            {
                case Drive: runNonLinear<Drive> (u, numOs, c); break;
                case Rat:   runNonLinear<Rat>   (u, numOs, c); break;
                case Fuzz:  runNonLinear<Fuzz>  (u, numOs, c); break;
                case Fold:  runNonLinear<Fold>  (u, numOs, c); break;
                default:    runNonLinear<Tube>  (u, numOs, c); break;
            }
        }
        oversampler->processSamplesDown (block);

        // tono, mix col dry allineato, uscita
        const float* dryL = dryLine.getReadPointer (0);
        const float* dryR = dryLine.getReadPointer (1);
        float m = sMix, o = sOut, tr = sTrim, gL = sLowG, gH = sHighG;
        for (int i = 0; i < n; ++i)
        {
            gL = tLowG + k * (gL - tLowG);
            gH = tHighG + k * (gH - tHighG);
            m = tMix + k * (m - tMix);
            o = tOut + k * (o - tOut);
            tr = tTrim + k * (tr - tTrim);
            const float wetGain = m * tr;
            for (int c = 0; c < numCh; ++c)
            {
                float* x = buffer.getWritePointer (c, start);
                auto& s = st[(size_t) c];
                // tone: bilanciamento basse / alte attorno a 800 Hz (a 50 % le due metà si risommano identiche)
                s.tilt += cTilt * (x[i] - s.tilt);
                const float bal = s.tilt * gL + (x[i] - s.tilt) * gH;
                s.tone += cTone * (bal - s.tone);   // hicut
                const float dry = (c == 0 ? dryL : dryR)[(dryPos + i - osLatency) & lineMask];
                float y = dry * (1.0f - m) + s.tone * wetGain;
                if (locutOn)
                {
                    const float v3 = y - s.lcS2;
                    const float v1 = lcA1 * s.lcS1 + lcA2 * v3;
                    const float v2 = s.lcS2 + lcA2 * s.lcS1 + lcA3 * v3;
                    s.lcS1 = 2.0f * v1 - s.lcS1;
                    s.lcS2 = 2.0f * v2 - s.lcS2;
                    y = y - lcK * v1 - v2;   // uscita passa-alto
                }
                x[i] = y * o;
            }
        }
        sMix = m; sOut = o; sTrim = tr; sLowG = gL; sHighG = gH;
        dryPos = (dryPos + n) & lineMask;
    }

    if (analyse) outTap.push (buffer.getArrayOfReadPointers(), numCh, total);

    for (auto& s : st)
        for (float* v : { &s.preLp, &s.emphLp, &s.gbw, &s.stage, &s.couple, &s.scoopLp, &s.scoopHp, &s.post, &s.dcY, &s.tone, &s.tilt, &s.lcS1, &s.lcS2 })
            if (std::abs (*v) < 1.0e-15f) *v = 0.0f;
}

void BDistortProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("types_version", kStateVersion, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BDistortProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()) || xml->hasTagName ("BDrive"))   // anche progetti col nome precedente
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.getType() != apvts.state.getType())
            {
                juce::ValueTree renamed (apvts.state.getType());
                renamed.copyPropertiesAndChildrenFrom (tree, nullptr);
                tree = renamed;
            }
            // progetti con i sei tipi di prima: soft/tube -> tube, hard/rat -> rat, fuzz, fold
            if ((int) tree.getProperty ("types_version", 1) < kStateVersion)
                for (auto child : tree)
                    if (child.getProperty ("id").toString() == BDistortIDs::type)
                    {
                        static constexpr int remap[6] = { Tube, Tube, Rat, Rat, Fuzz, Fold };
                        const int old = juce::jlimit (0, 5, juce::roundToInt ((float) child.getProperty ("value")));
                        child.setProperty ("value", remap[old], nullptr);
                    }
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* BDistortProcessor::createEditor() { return new BDistortEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BDistortProcessor(); }

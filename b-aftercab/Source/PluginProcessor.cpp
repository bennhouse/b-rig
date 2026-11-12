#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ============================================================================
// Thread di analisi: fa i calcoli pesanti fuori dal thread audio.
class BAftercabProcessor::AnalyserThread final : public juce::Thread
{
public:
    explicit AnalyserThread (BAftercabProcessor& p) : juce::Thread ("b-aftercab align"), owner (p)
    {
        startThread (juce::Thread::Priority::low);
    }
    ~AnalyserThread() override { stopThread (2000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (50);
            if (owner.getAlignState() == AlignState::Analysing)
                owner.runAnalysis();
        }
    }

private:
    BAftercabProcessor& owner;
};

// ============================================================================
BAftercabProcessor::BAftercabProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BAftercab", createLayout())
{
    pDelayMs    = apvts.getRawParameterValue (ParamIDs::delayMs);
    pSpreadOn   = apvts.getRawParameterValue (ParamIDs::spreadOn);
    pRevOn      = apvts.getRawParameterValue (ParamIDs::revOn);
    pRevType    = apvts.getRawParameterValue (ParamIDs::revType);
    pRevMix     = apvts.getRawParameterValue (ParamIDs::revMix);
    pCompOn     = apvts.getRawParameterValue (ParamIDs::compOn);
    pCompMode   = apvts.getRawParameterValue (ParamIDs::compMode);
    pCompThr    = apvts.getRawParameterValue (ParamIDs::compThr);
    pOutGain    = apvts.getRawParameterValue (ParamIDs::outGain);
    pOutAuto    = apvts.getRawParameterValue (ParamIDs::outAuto);

    // memoria per preset: ogni tipo/timbro ricorda i suoi valori finché il plugin resta caricato
    compMemory.add (apvts.getParameter (ParamIDs::compThr), [] (int m) { return (float) sp::Compressor::preset (m).thresholdDb; });
    revMemory.add (apvts.getParameter (ParamIDs::revMix), [] (int t) { return t == 0 ? 10.0f : 20.0f; });   // room più asciutta
    revMemory.add (apvts.getParameter (ParamIDs::revBalance),
                   [] (int t) { return (float) juce::roundToInt (100.0 * sp::Delverb::presetDelayShare (t == 0 ? 1 : t)); });
    pRevBalance = apvts.getRawParameterValue (ParamIDs::revBalance);
    pRevTimeMode = apvts.getRawParameterValue (ParamIDs::revTimeMode);
    pRevTapBpm  = apvts.getRawParameterValue (ParamIDs::revTapBpm);
    pAlignRange = apvts.getRawParameterValue (ParamIDs::alignRange);
    pEqOn       = apvts.getRawParameterValue (ParamIDs::eqOn);
    pEqMode     = apvts.getRawParameterValue (ParamIDs::eqMode);
    pEqQuality  = apvts.getRawParameterValue (ParamIDs::eqQuality);

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& bp = pBands[(size_t) b];
        if (b == 0 || b == kNumBands - 1)
            bp.type = apvts.getRawParameterValue (ParamIDs::band (b, "type"));
        bp.on   = apvts.getRawParameterValue (ParamIDs::band (b, "on"));
        bp.freq = apvts.getRawParameterValue (ParamIDs::band (b, "freq"));
        bp.gain = apvts.getRawParameterValue (ParamIDs::band (b, "gain"));
        bp.q    = apvts.getRawParameterValue (ParamIDs::band (b, "q"));
    }

    updateMode();
    analyser = std::make_unique<AnalyserThread> (*this);
    for (size_t q = 0; q < convBlock.size(); ++q)
        convBlock[q] = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::Latency { kConvBlock[q] }, convQueue);
    irBuilder = std::make_unique<IrBuilder> (*this);
    startTimerHz (20);
}

BAftercabProcessor::~BAftercabProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    irBuilder.reset();
    analyser.reset();
}

APVTS::ParameterLayout BAftercabProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    auto msText  = [] (float v, int) { return String (v, 3) + " ms"; };
    auto hzText  = [] (float v, int) { return v >= 1000.f ? String (v / 1000.f, 2) + " kHz" : String (v, 1) + " Hz"; };
    auto dbText  = [] (float v, int) { return String (v, 1) + " dB"; };
    auto qText   = [] (float v, int) { return String (v, 2); };

    // --- Sdoppiamento / delay (solo Mono -> Stereo)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::spreadOn, 1 }, "Spread", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::delayMs, 1 }, "Delay",
        NormalisableRange<float> ((float) kMinDelayMs, (float) kMaxDelayMs), 10.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::alignRange, 1 }, "Align range",
        NormalisableRange<float> (0.1f, 5.0f, 0.01f), 2.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
            [] (float v, int) { return "+/- " + String (v, 2) + " ms"; })));

    // --- Compressore
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::compOn, 1 }, "Comp", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::compMode, 1 }, "Comp mode", StringArray { "Rhythm", "Lead", "Clean" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::compThr, 1 }, "Comp threshold", NormalisableRange<float> (-40.0f, 0.0f, 0.5f),
        (float) sp::Compressor::preset (0).thresholdDb,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    // --- Uscita
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::outGain, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::outAuto, 1 }, "Output auto", true));

    // --- Riverbero
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::revOn, 1 }, "Delverb", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::revType, 1 }, "Delverb type", StringArray { "Room", "Plate", "Hall" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::revMix, 1 }, "Delverb mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::revBalance, 1 }, "Delverb balance (delay %)", NormalisableRange<float> (0.0f, 100.0f, 1.0f),
        (float) juce::roundToInt (100.0 * sp::Delverb::presetDelayShare (1)),   // preset della plate (timbro predefinito)
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int i = roundToInt (v);
            return "del " + String (i) + " / rev " + String (100 - i);
        })));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::revTimeMode, 1 }, "Delverb time", StringArray { "Default", "Sync", "Tap" }, 1));   // di default segue il tempo della DAW
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::revTapBpm, 1 }, "Delverb tap BPM", NormalisableRange<float> (30.0f, 300.0f, 0.01f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("BPM")));

    // --- EQ
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::eqOn, 1 }, "EQ on", true));   // acceso ma piatto (tutte le bande a 0 dB)
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::eqMode, 1 }, "EQ mode", StringArray { "Zero latency", "Linear phase" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::eqQuality, 1 }, "Linear quality", StringArray { "Low", "Medium", "High" }, 1));

    struct BandDef { float fMin, fMax, fDef; };
    const BandDef defs[kNumBands] = { { 20, 1000, 80 }, { 20, 20000, 250 }, { 20, 20000, 1000 },
                                      { 20, 20000, 4000 }, { 1000, 20000, 10000 } };
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto n = "Band " + String (b + 1) + " ";
        if (b == 0)
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::band (b, "type"), 1 },
                        n + "type", StringArray { "Low cut", "Low shelf" }, 1));
        if (b == kNumBands - 1)
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::band (b, "type"), 1 },
                        n + "type", StringArray { "High cut", "High shelf" }, 1));

        // intervallo (snap) per far tornare esatti i valori dopo la normalizzazione 0..1
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::band (b, "on"), 1 }, n + "on", true));
        NormalisableRange<float> fr (defs[b].fMin, defs[b].fMax, 0.1f);
        fr.setSkewForCentre (std::sqrt (defs[b].fMin * defs[b].fMax));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::band (b, "freq"), 1 },
                    n + "freq", fr, defs[b].fDef, AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::band (b, "gain"), 1 },
                    n + "gain", NormalisableRange<float> (-kMaxEqGainDb, kMaxEqGainDb, 0.01f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        NormalisableRange<float> qr (0.2f, 8.0f, 0.01f);
        qr.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::band (b, "q"), 1 },
                    n + "Q", qr, (b == 0 || b == kNumBands - 1) ? 0.71f : 1.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (qText)));
    }
    return layout;
}

// ============================================================================
bool BAftercabProcessor::getLinearPhaseResponseDb (double hz, double& db) const
{
    if (! linearActive.load()) return false;
    const juce::SpinLock::ScopedLockType l (linearResponseLock);
    // mentre il filtro si sta ricostruendo (es. trascinando un punto) il display usa la curva ideale
    if (linearResponseDb.size() < 2 || ! (loadedIrCoeffs == requestedIrCoeffs)) return false;
    const double pos = juce::jlimit (0.0, (double) linearResponseDb.size() - 1.001, hz / linearResponseBinHz);
    const auto k = (size_t) pos;
    const double t = pos - (double) k;
    db = linearResponseDb[k] + t * (linearResponseDb[k + 1] - linearResponseDb[k]);
    return true;
}

bool BAftercabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    return (in == mono && out == mono) || (in == mono && out == stereo) || (in == stereo && out == stereo);
}

void BAftercabProcessor::updateMode()
{
    const int in = getMainBusNumInputChannels(), out = getMainBusNumOutputChannels();
    Mode m = Mode::StereoToStereo;
    if (in == 1 && out == 1) m = Mode::MonoToMono;
    else if (in == 1 && out >= 2) m = Mode::MonoToStereo;
    mode.store ((int) m);
}

void BAftercabProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    updateMode();
    currentSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;

    delayLine.prepare ((int) std::ceil (kMaxDelayMs * 0.001 * sampleRate) + 64);
    delaySamples.reset (sampleRate, 0.08);
    delaySamples.setCurrentAndTargetValue (
        juce::jmax (sp::FractionalDelay::kMinDelaySamples, (double) pDelayMs->load() * 0.001 * sampleRate));
    lastKernelDelay = -1.0;
    spreadMix.reset (sampleRate, 0.03);
    spreadMix.setCurrentAndTargetValue (pSpreadOn->load() > 0.5f ? 1.0f : 0.0f);

    convolution.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    for (auto& cb : convBlock) cb->prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    lastConv = -2;

    reverb.prepare (sampleRate, samplesPerBlock);
    compressor.prepare (sampleRate);
    outG = juce::Decibels::decibelsToGain (getEffectiveOutDb());
    outUp = 1.0f - std::exp (-1.0f / (0.35f * (float) sampleRate));
    outDown = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
    outManual = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate / 3.0f));
    compGate.reset (sampleRate, 0.02);
    compGate.setCurrentAndTargetValue (0.0f);
    compGateBuf.assign ((size_t) samplesPerBlock, 0.0f);
    compRunning = false;
    revIn.assign ((size_t) samplesPerBlock, 0.0f);
    revL.assign ((size_t) samplesPerBlock, 0.0f);
    revR.assign ((size_t) samplesPerBlock, 0.0f);
    revMixSmoothed.reset (sampleRate, 0.03);
    revMixSmoothed.setCurrentAndTargetValue (pRevMix->load() / 100.0f);
    revGate.reset (sampleRate, 0.03);
    revGate.setCurrentAndTargetValue (0.0f);
    reverbRunning = false;
    reverbTimbre = -1;

    {
        const juce::ScopedLock sl (captureLock);
        if (getAlignState() == AlignState::Listening || getAlignState() == AlignState::Analysing)
            alignState.store ((int) AlignState::Idle);
        capture.assign ((size_t) (kCaptureSeconds * sampleRate), 0.0f);
        captureFill.store (0);
    }

    reset();
    refreshNonRealtimeState();
}

void BAftercabProcessor::reset()
{
    for (auto& ch : iir)
        for (auto& f : ch) f.reset();
    eqWasOn = false;
    convolution.reset();
    for (auto& cb : convBlock) cb->reset();
    delayLine.reset();
}

// ============================================================================
std::array<sp::BiquadCoeffs, BAftercabProcessor::kNumBands> BAftercabProcessor::computeEqCoeffs (double sr) const
{
    std::array<sp::BiquadCoeffs, kNumBands> c;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bp = pBands[(size_t) b];
        if (bp.on->load() < 0.5f) { c[(size_t) b] = {}; continue; }   // banda spenta: filtro neutro
        sp::FilterType t = sp::FilterType::Bell;
        if (b == 0)             t = bp.type->load() < 0.5f ? sp::FilterType::LowCut  : sp::FilterType::LowShelf;
        if (b == kNumBands - 1) t = bp.type->load() < 0.5f ? sp::FilterType::HighCut : sp::FilterType::HighShelf;
        c[(size_t) b] = sp::BiquadCoeffs::make (t, sr, bp.freq->load(), bp.q->load(), bp.gain->load());
    }
    return c;
}

int BAftercabProcessor::linearPhaseSize() const
{
    static constexpr int base[] = { 2048, 8192, 32768 };
    const int q = juce::jlimit (0, 2, (int) pEqQuality->load());
    int scale = 1;
    while (currentSampleRate > 66000.0 * scale) scale *= 2;   // stessa risoluzione in Hz a 96/192 kHz
    return base[q] * scale;
}

// ----------------------------------------------------------------------------
// EQ a fase lineare: il FIR si costruisce su un thread dedicato (mai sul thread dell'interfaccia,
// che restava bloccato mentre si trascinava un punto). Le richieste si sovrascrivono: si
// costruisce sempre e solo l'ultima, così il filtro segue i movimenti senza accumulare ritardo.
class BAftercabProcessor::IrBuilder final : public juce::Thread
{
public:
    explicit IrBuilder (BAftercabProcessor& p) : juce::Thread ("b-aftercab linear EQ"), owner (p) { startThread (juce::Thread::Priority::normal); }
    ~IrBuilder() override { signalThreadShouldExit(); notify(); stopThread (2000); }

    void request (const std::array<sp::BiquadCoeffs, kNumBands>& c, int n, double sr, int q)
    {
        { const juce::ScopedLock sl (lock); pendingC = c; pendingN = n; pendingSr = sr; pendingQ = q; hasPending = true; }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::array<sp::BiquadCoeffs, kNumBands> c;
            int n = 0, q = 0; double sr = 0;
            {
                const juce::ScopedLock sl (lock);
                if (hasPending) { c = pendingC; n = pendingN; sr = pendingSr; q = pendingQ; hasPending = false; }
            }
            if (n > 0) owner.buildLinearIr (c, n, sr, q);
            else wait (-1);
        }
    }

private:
    BAftercabProcessor& owner;
    juce::CriticalSection lock;
    std::array<sp::BiquadCoeffs, kNumBands> pendingC {};
    int pendingN = 0, pendingQ = 0; double pendingSr = 0; bool hasPending = false;
};

// Latenza propria del motore a blocchi di una qualità (come la calcola JUCE: potenza di 2,
// almeno il buffer massimo dell'host).
int BAftercabProcessor::convBlockLatency (int quality) const noexcept
{
    return juce::nextPowerOfTwo (juce::jmax (64, maxBlockSize, kConvBlock[juce::jlimit (0, 2, quality)]));
}

void BAftercabProcessor::buildLinearIr (const std::array<sp::BiquadCoeffs, kNumBands>& c, int n, double sr, int quality)
{
    // Risposta in ampiezza dell'EQ -> FIR simmetrico (fase lineare) su una griglia di n punti.
    const int order = juce::roundToInt (std::log2 ((double) n));
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) (2 * n), 0.0f);
    double expectedCentre = 0.0;
    for (int k = 0; k <= n / 2; ++k)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * k / n;
        double mag = 1.0;
        for (const auto& bc : c) mag *= bc.magnitude (w);
        data[(size_t) (2 * k)] = (float) mag;
        expectedCentre += (k == 0 || k == n / 2) ? mag : 2.0 * mag;
    }
    expectedCentre /= n;
    fft.performRealOnlyInverseTransform (data.data());
    const double scale = std::abs (data[0]) > 1.0e-20 ? expectedCentre / data[0] : 1.0;

    // Motore: a blocchi (FIR più corto di 2B, stessa latenza totale) se resta almeno il 75% del FIR.
    const int blockLat = convBlockLatency (quality);
    const int engine = n - 2 * blockLat >= (3 * n) / 4 ? quality : -1;
    const int len = engine < 0 ? n : n - 2 * blockLat;

    juce::AudioBuffer<float> ir (1, len);
    auto* h = ir.getWritePointer (0);
    for (int i = 0; i < len; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * i / len;
        const double win = 0.42 - 0.5 * std::cos (t) + 0.08 * std::cos (2.0 * t);   // Blackman sulla lunghezza reale
        h[i] = (float) (data[(size_t) ((i - len / 2 + n) % n)] * scale * win);
    }

    // Risposta reale del FIR (zero-padding ad almeno 32768 punti: precisa anche a 20 Hz;
    // calcolata su un thread a parte, quindi non pesa né sull'audio né sull'interfaccia)
    const int fftOrder = juce::jlimit (15, 17, order + 1);
    const int m = 1 << fftOrder;
    std::vector<float> spec ((size_t) (2 * m), 0.0f);
    std::copy (h, h + len, spec.begin());
    juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (spec.data(), true);
    std::vector<float> db ((size_t) (m / 2 + 1));
    for (size_t k = 0; k < db.size(); ++k) db[k] = 20.0f * std::log10 (juce::jmax (1.0e-9f, spec[k]));

    convFor (engine).loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::no,
                                          juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    activeConv.store (engine);
    const juce::SpinLock::ScopedLockType l (linearResponseLock);
    linearResponseDb = std::move (db);
    linearResponseBinHz = sr / m;
    loadedIrCoeffs = c;
    loadedIrSize = n;
    loadedIrRate = sr;
    ++linearResponseVersion;
}

void BAftercabProcessor::refreshNonRealtimeState()
{
    autoOutDb.store (computeAutoOutDb());

    const bool wantLinear = pEqOn->load() > 0.5f && pEqMode->load() > 0.5f;
    int latency = 0;

    if (wantLinear)
    {
        const double sr = currentSampleRate;
        const int n = linearPhaseSize();
        const auto c = computeEqCoeffs (sr);
        if (c != requestedIrCoeffs || n != requestedIrSize || ! juce::exactlyEqual (sr, requestedIrRate))
        {
            requestedIrCoeffs = c; requestedIrSize = n; requestedIrRate = sr;
            irBuilder->request (c, n, sr, juce::jlimit (0, 2, (int) pEqQuality->load()));
        }
        latency = n / 2;   // con qualsiasi motore: il FIR è centrato in modo che il totale sia n/2
    }

    linearActive.store (wantLinear);
    if (getLatencySamples() != latency)
        setLatencySamples (latency);
}

// Solo per i test: aspetta che il filtro richiesto sia stato costruito.
bool BAftercabProcessor::waitForLinearIr (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        { const juce::SpinLock::ScopedLockType l (linearResponseLock);
          if (loadedIrCoeffs == requestedIrCoeffs && loadedIrSize == requestedIrSize) return true; }
        juce::Thread::sleep (5);
    }
    return false;
}

void BAftercabProcessor::processEq (juce::AudioBuffer<float>& buffer, int numChannels)
{
    if (pEqOn->load() < 0.5f)
    {
        eqWasOn = false;
        linearWasActive = false;
        return;   // EQ spento: costo zero
    }

    if (linearActive.load())
    {
        const int idx = activeConv.load();
        auto& conv = convFor (idx);
        if (! linearWasActive || idx != lastConv) conv.reset();   // motore nuovo: niente code vecchie
        lastConv = idx;
        linearWasActive = true;
        eqWasOn = false;
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, (size_t) numChannels);
        conv.process (juce::dsp::ProcessContextReplacing<float> (sub));
        return;
    }
    linearWasActive = false;

    const auto c = computeEqCoeffs (currentSampleRate);
    if (! eqWasOn)
    {
        for (auto& ch : iir) for (auto& f : ch) f.reset();
        bandWasActive.fill (false);
    }
    eqWasOn = true;

    // Le bande neutre (spente, o bell/shelf a 0 dB) non vengono calcolate affatto.
    const int n = buffer.getNumSamples();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bp = pBands[(size_t) b];
        const bool isCutType = bp.type != nullptr && bp.type->load() < 0.5f;
        const bool active = bp.on->load() > 0.5f && (isCutType || std::abs (bp.gain->load()) > 0.005f);
        if (! active) { bandWasActive[(size_t) b] = false; continue; }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& f = iir[(size_t) ch][(size_t) b];
            if (! bandWasActive[(size_t) b]) f.reset();   // riparte pulita, senza stato vecchio
            f.c = c[(size_t) b];
            auto* x = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i) x[i] = f.process (x[i]);   // una banda alla volta: ciclo stretto
        }
        bandWasActive[(size_t) b] = true;
    }
}

void BAftercabProcessor::processSplit (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const double sr = currentSampleRate;
    const double maxD = kMaxDelayMs * 0.001 * sr;
    delaySamples.setTargetValue (juce::jlimit (sp::FractionalDelay::kMinDelaySamples, maxD,
                                               (double) pDelayMs->load() * 0.001 * sr));

    captureForAlignment (buffer.getReadPointer (0), n);

    // Il canale diretto è una copia identica dell'ingresso (latenza zero),
    // l'altro passa per la linea di ritardo.
    buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* w = buffer.getWritePointer (1);   // il canale ritardato è sempre il destro

    // Spread spento: i due canali restano identici (con dissolvenza di 30 ms,
    // la linea di ritardo continua a girare per riattivarsi senza buchi).
    spreadMix.setTargetValue (pSpreadOn->load() > 0.5f ? 1.0f : 0.0f);
    const bool fullWet = ! spreadMix.isSmoothing() && spreadMix.getTargetValue() > 0.5f;

    const bool smoothingDelay = delaySamples.isSmoothing();
    if (! smoothingDelay)
    {
        const double d = delaySamples.getTargetValue();
        if (! juce::exactlyEqual (d, lastKernelDelay)) { delayLine.setDelay (d); lastKernelDelay = d; }
    }
    else
        lastKernelDelay = -1.0;

    for (int i = 0; i < n; ++i)
    {
        if (smoothingDelay) delayLine.setDelay (delaySamples.getNextValue());
        const float x = w[i];
        const float d = delayLine.process (x);
        w[i] = fullWet ? d : x + spreadMix.getNextValue() * (d - x);
    }
}

void BAftercabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (n == 0) return;

    processEq (buffer, juce::jmin (numIn, 2));
    if (spectrumActive.load (std::memory_order_relaxed))
        spectrumTap.push (buffer.getArrayOfReadPointers(), juce::jmin (numIn, 2), n);
    processCompressor (buffer, juce::jmin (numIn, 2));

    if (numIn == 1 && numOut >= 2)
        processSplit (buffer);

    processReverb (buffer, numIn, numOut);

    // uscita: manuale o compensazione automatica; in automatico il volume sale piano e scende in fretta
    // (mai un picco quando il recupero cambia); a 0 dB fermo costa zero
    const float target = juce::Decibels::decibelsToGain (getEffectiveOutDb());
    if (std::abs (target - outG) > 1.0e-6f * target)
    {
        const float c = ! isOutAuto() ? outManual : (target > outG ? outUp : outDown);
        for (int i = 0; i < n; ++i)
        {
            outG += c * (target - outG);
            for (int ch = 0; ch < numOut; ++ch) buffer.getWritePointer (ch)[i] *= outG;
        }
    }
    else
    {
        outG = target;
        if (std::abs (outG - 1.0f) > 1.0e-6f) buffer.applyGain (outG);
    }
}

// Compensazione automatica dell'uscita (non dinamica):
//  - EQ: quanto alza o abbassa il volume di un tipico segnale di chitarra dopo il cab
//    (banda 100 Hz - 4 kHz, media in potenza su scala logaritmica);
//  - compressore: la riduzione media misurata sul segnale reale (prima della misura: la stima statica a -10 dBFS);
//  - delverb e spread sono già a potenza costante.
float BAftercabProcessor::computeAutoOutDb() const
{
    float compDb = 0.0f;
    if (pCompOn->load() > 0.5f)
    {
        // solo la riduzione misurata sul segnale reale: finché non c'è (i primi ~150 ms) nessun recupero, così il
        // volume prima scende un poco e poi risale piano, invece di partire da una stima che può essere troppo alta
        if (compMeasValid.load()) compDb = compMeasDb.load();
    }
    if (pEqOn->load() < 0.5f) return juce::jlimit (-12.0f, 24.0f, compDb);
    const double sr = currentSampleRate > 0 ? currentSampleRate : 48000.0;
    const auto c = computeEqCoeffs (sr);
    double num = 0.0, den = 0.0;
    for (int i = 0; i < 64; ++i)
    {
        const double f = 40.0 * std::pow (12000.0 / 40.0, i / 63.0);
        const double w = 1.0 / (1.0 + (100.0 / f) * (100.0 / f)) / (1.0 + (f / 4000.0) * (f / 4000.0));
        double mag = 1.0;
        for (const auto& bc : c) mag *= bc.magnitude (2.0 * juce::MathConstants<double>::pi * f / sr);
        num += w * mag * mag;
        den += w;
    }
    return (float) juce::jlimit (-12.0, 24.0, -10.0 * std::log10 (num / den) + compDb);
}

void BAftercabProcessor::setTimeMode (TimeMode m)
{
    auto* p = apvts.getParameter (ParamIDs::revTimeMode);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
    p->endChangeGesture();
}

void BAftercabProcessor::setTapTempo (float bpm)
{
    auto* p = apvts.getParameter (ParamIDs::revTapBpm);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (30.0f, 300.0f, bpm)));
    p->endChangeGesture();
    setTimeMode (TimeMode::Tap);
    clearTapSeries();
}

// ============================================================================
// Compressore
//
// Catena: EQ -> compressore -> sdoppiamento -> delverb.
//  - dopo l'EQ: i tagli (low cut, bande sui bassi) non fanno più "pompare" il
//    compressore, che reagisce al suono già scolpito;
//  - prima dello sdoppiamento: i due canali ricevono lo stesso segnale compresso,
//    quindi il Haas e l'allineamento di fase non cambiano;
//  - prima del delverb: le code di delay e riverbero non vengono schiacciate e
//    restano naturali mentre il dry è controllato.

void BAftercabProcessor::processCompressor (juce::AudioBuffer<float>& buffer, int numChannels)
{
    const bool want = pCompOn->load() > 0.5f;
    if (want && ! compRunning)
    {
        compressor.reset();
        compRunning = true;
        compGate.setCurrentAndTargetValue (0.0f);
    }
    if (! compRunning) return;   // spento: costo zero, uscita identica

    compressor.setMode (juce::jlimit (0, 2, (int) pCompMode->load()));
    compressor.setThreshold (pCompThr->load());
    compressor.setAutoGain (false);   // il recupero di volume del comp lo fa l'OUT (auto)
    compGate.setTargetValue (want ? 1.0f : 0.0f);

    const int total = buffer.getNumSamples();
    const int chunk = (int) compGateBuf.size();
    float maxGr = 0.0f;
    // misura della riduzione reale: energia prima del comp (questo blocco)
    const int cmode = juce::jlimit (0, 2, (int) pCompMode->load()); const float thr = pCompThr->load();
    if (cmode != compMeasMode || std::abs (thr - compMeasThr) > 0.05f)
    { compMeasMode = cmode; compMeasThr = thr; compInAcc = compOutAcc = 0.0; compMeasSamples = 0; compMeasValid.store (false); }
    double eIn = 0.0;
    for (int c = 0; c < numChannels; ++c) { const float* x = buffer.getReadPointer (c); for (int i = 0; i < total; ++i) eIn += (double) x[i] * x[i]; }
    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);
        for (int i = 0; i < n; ++i) compGateBuf[(size_t) i] = compGate.getNextValue();
        float* chans[2] = { buffer.getWritePointer (0) + start,
                            numChannels > 1 ? buffer.getWritePointer (1) + start : nullptr };
        maxGr = juce::jmax (maxGr, compressor.process (chans, numChannels, n, compGateBuf.data()));
    }
    if (want && ! compGate.isSmoothing() && eIn > (double) (total * numChannels) * 3.16e-6)   // sopra -55 dBFS rms
    {
        double eOut = 0.0;
        for (int c = 0; c < numChannels; ++c) { const float* x = buffer.getReadPointer (c); for (int i = 0; i < total; ++i) eOut += (double) x[i] * x[i]; }
        const double a = std::exp (-(double) total / (2.0 * currentSampleRate));   // finestra ~2 s
        compInAcc = a * compInAcc + eIn; compOutAcc = a * compOutAcc + eOut;
        compMeasSamples += total;
        if (compMeasSamples > (int) (0.15 * currentSampleRate) && compOutAcc > 1.0e-12)
        {
            compMeasDb.store ((float) (10.0 * std::log10 (compInAcc / compOutAcc)));
            compMeasValid.store (true);
        }
    }
    for (float prev = compGrPeak.load(); maxGr > prev && ! compGrPeak.compare_exchange_weak (prev, maxGr);) {}

    if (! want && ! compGate.isSmoothing()) compRunning = false;
}

// ============================================================================
// Riverbero
//
// Posizione nella catena: EQ -> sdoppiamento -> riverbero.
//  - dopo l'EQ: il riverbero riceve il timbro già scolpito, come un aux in un mix;
//  - dopo lo sdoppiamento ma alimentato dal segnale mono diretto (pre-Haas): la coda
//    è un vero stereo decorrelato invece di una copia ritardata di sé stessa, resta
//    compatibile col mono, e l'analisi di fase lavora sul segnale asciutto.

void BAftercabProcessor::processReverb (juce::AudioBuffer<float>& buffer, int numIn, int numOut)
{
    const bool want = pRevOn->load() > 0.5f;
    const int type = juce::jlimit (0, 2, (int) pRevType->load());

    if (want && ! reverbRunning)
    {
        reverb.setTimbre (type);
        reverb.reset();
        reverbTimbre = type;
        reverbRunning = true;
        revGate.setCurrentAndTargetValue (0.0f);
    }
    if (! reverbRunning) return;   // spento: costo zero e uscita identica

    // tempo del delay: default = tempo proprio del timbro (riferito a 120 BPM),
    // sync = tempo della DAW, tap = tempo battuto sul tasto
    double bpm = 120.0;
    switch (getTimeMode())
    {
        case TimeMode::Sync:
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    if (auto b = pos->getBpm()) bpm = *b;
            break;
        case TimeMode::Tap:     bpm = pRevTapBpm->load(); break;
        case TimeMode::Default: break;
    }
    reverb.setTempo (bpm);
    reverb.setDelayShare (pRevBalance->load() / 100.0);

    // cambio timbro: si sfuma a zero, si riconfigura, si riapre
    revGate.setTargetValue (want && type == reverbTimbre ? 1.0f : 0.0f);
    revMixSmoothed.setTargetValue (pRevMix->load() / 100.0f);

    const int total = buffer.getNumSamples();
    const int chunk = (int) revIn.size();
    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);

        // stereo ogni volta che l'uscita è stereo; mono solo con uscita mono
        // uscita stereo (anche mono > stereo, con o senza spread): delay e riverberi sempre stereo
        const bool stereo = numOut >= 2;
        reverb.setStereo (stereo && numOut >= 2);
        const float* inL = buffer.getReadPointer (0) + start;
        const float* inR = (numIn >= 2 || (stereo && numOut >= 2)) ? buffer.getReadPointer (1) + start : inL;
        reverb.process (inL, inR, revL.data(), revR.data(), n);

        float* outL = buffer.getWritePointer (0) + start;
        float* outR = numOut >= 2 ? buffer.getWritePointer (1) + start : nullptr;
        const bool steady = ! revMixSmoothed.isSmoothing() && ! revGate.isSmoothing();
        float dry = 0.0f, wet = 0.0f;
        auto gains = [&]
        {
            // mix a potenza costante: il volume percepito resta stabile mentre si dosa il riverbero
            const float a = revMixSmoothed.getNextValue() * revGate.getNextValue() * juce::MathConstants<float>::halfPi;
            dry = std::cos (a); wet = std::sin (a);
        };
        if (steady) gains();   // fermi: seno e coseno una volta per blocco
        for (int i = 0; i < n; ++i)
        {
            if (! steady) gains();
            if (outR != nullptr)
            {
                outL[i] = dry * outL[i] + wet * revL[(size_t) i];
                outR[i] = dry * outR[i] + wet * revR[(size_t) i];
            }
            else
                outL[i] = dry * outL[i] + wet * revL[(size_t) i];   // uscita mono: il delverb è già mono
        }
    }

    if (! revGate.isSmoothing() && revGate.getCurrentValue() <= 0.0f)
    {
        if (want) { reverb.setTimbre (type); reverb.reset(); reverbTimbre = type; }   // riparte col nuovo timbro
        else reverbRunning = false;
    }
}

void BAftercabProcessor::tapAt (double nowMs)
{
    if (nowMs - lastTapMs > 2000.0) tapTimes.clear();   // pausa lunga: nuova serie
    lastTapMs = nowMs;
    tapTimes.push_back (nowMs);
    if (tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
    const size_t n = tapTimes.size();
    if (n >= 4 && tapTimes.back() - tapTimes[n - 4] <= 3000.0)
    {
        const double avg = (tapTimes.back() - tapTimes.front()) / (double) (n - 1);
        auto* p = apvts.getParameter (ParamIDs::revTapBpm);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (30.0, 300.0, 60000.0 / avg)));
        p->endChangeGesture();
        setTimeMode (TimeMode::Tap);
    }
}

// ============================================================================
// Allineamento di fase

void BAftercabProcessor::startAlignment()
{
    if (getMode() != Mode::MonoToStereo) return;
    const juce::ScopedLock sl (captureLock);
    if (capture.empty()) return;
    alignState.store ((int) AlignState::Idle);
    captureFill.store (0);
    captureRate = currentSampleRate;
    alignState.store ((int) AlignState::Listening);
}

void BAftercabProcessor::cancelAlignment()
{
    alignState.store ((int) AlignState::Idle);
}

float BAftercabProcessor::getAlignProgress() const noexcept
{
    const auto size = capture.size();
    return size == 0 ? 0.0f : (float) captureFill.load() / (float) size;
}

sp::AlignResult BAftercabProcessor::getLastAlignResult() const
{
    const juce::SpinLock::ScopedLockType l (resultLock);
    return lastResult;
}

void BAftercabProcessor::captureForAlignment (const float* data, int n)
{
    if (getAlignState() != AlignState::Listening) return;

    // Salta i blocchi di silenzio, così l'analisi usa solo audio "vero".
    const auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
    if (juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())) < 1.0e-4f) return;

    const int size = (int) capture.size();
    int fill = captureFill.load();
    const int toCopy = juce::jmin (n, size - fill);
    if (toCopy > 0)
    {
        std::copy (data, data + toCopy, capture.begin() + fill);
        fill += toCopy;
        captureFill.store (fill);
    }
    if (fill >= size)
        alignState.store ((int) AlignState::Analysing);
}

void BAftercabProcessor::runAnalysis()
{
    sp::AlignResult res;
    {
        const juce::ScopedLock sl (captureLock);
        if (getAlignState() != AlignState::Analysing) return;
        res = sp::findBestDelay (capture.data(), captureFill.load(), captureRate,
                                 pDelayMs->load(), pAlignRange->load(), kMinDelayMs, kMaxDelayMs);
    }
    {
        const juce::SpinLock::ScopedLockType l (resultLock);
        lastResult = res;
    }
    alignState.store ((int) (res.ok ? AlignState::Done : AlignState::Failed));
    if (res.ok) triggerAsyncUpdate();
}

void BAftercabProcessor::handleAsyncUpdate()
{
    const auto r = getLastAlignResult();
    if (! r.ok) return;
    if (auto* p = apvts.getParameter (ParamIDs::delayMs))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) r.delayMs));
        p->endChangeGesture();
    }
}

// ============================================================================
void BAftercabProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BAftercabProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()) || xml->hasTagName ("SplitPhase"))   // anche progetti salvati col nome precedente
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.getType() != apvts.state.getType())
            {
                juce::ValueTree renamed (apvts.state.getType());
                renamed.copyPropertiesAndChildrenFrom (tree, nullptr);
                tree = renamed;
            }
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* BAftercabProcessor::createEditor()
{
    return new BAftercabEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BAftercabProcessor();
}

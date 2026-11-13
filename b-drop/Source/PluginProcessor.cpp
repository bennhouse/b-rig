#include "PluginProcessor.h"
#include "PluginEditor.h"

BDropProcessor::BDropProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "BDrop", createLayout())
{
    pOn = apvts.getRawParameterValue (BDropIDs::on);
    pRange = apvts.getRawParameterValue (BDropIDs::range);
    pZero = apvts.getRawParameterValue (BDropIDs::zeroLag);
    pSemi = apvts.getRawParameterValue (BDropIDs::semitones);
    pFine = apvts.getRawParameterValue (BDropIDs::fine);
    pTension = apvts.getRawParameterValue (BDropIDs::tension);
    pMix = apvts.getRawParameterValue (BDropIDs::mix);
}

juce::AudioProcessorValueTreeState::ParameterLayout BDropProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDropIDs::on, 1 }, "Detune on", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BDropIDs::range, 1 }, "Range", StringArray { "Shift", "Octave", "Whammy" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDropIDs::zeroLag, 1 }, "Zero-lag pick", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDropIDs::semitones, 1 }, "Semitones", NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDropIDs::fine, 1 }, "Fine", NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ct")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDropIDs::tension, 1 }, "Tension", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 65.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDropIDs::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    return l;
}

bool BDropProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == juce::AudioChannelSet::mono() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void BDropProcessor::rangeLimits (int r, int& lo, int& hi) noexcept
{
    switch (r)
    {
        case octave: lo = -12; hi = -12; break;   // fisso: un'ottava sotto
        default:     lo = -12; hi = 12;  break;   // shift e whammy: da 0 in su o in giù
    }
}

void BDropProcessor::applyRange (int r)
{
    int lo, hi; rangeLimits (r, lo, hi);
    auto* p = apvts.getParameter (BDropIDs::semitones);
    const float cur = p->convertFrom0to1 (p->getValue());
    float v = cur;
    if (cur < (float) lo || cur > (float) hi) v = (float) lo;
    if (std::abs (v - cur) > 0.01f) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

void BDropProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    order = fs > 66000.0 ? 11 : 10;   // 1024 punti a 44,1/48 kHz, 2048 a 88,2/96 kHz: la stessa durata
    N = 1 << order; H = N / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    win.resize ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / N);
    double ws = 0; for (int i = 0; i < N; ++i) ws += (double) win[(size_t) i] * win[(size_t) i];
    olaScale = (float) ((double) H / ws);   // analisi e sintesi con Hann, sovrapposizione 4x: somma costante
    inRing.assign ((size_t) (4 * N), 0.0f);
    frame.assign ((size_t) (2 * N), 0.0f);
    outAcc.assign ((size_t) N, 0.0f);
    fifo.assign ((size_t) kFifo, 0.0f);
    const size_t bins = (size_t) (N / 2 + 1);
    prevPhase.assign (bins, 0.0f); synthPhase.assign (bins, 0.0f);
    mag.assign (bins, 0.0f); phase.assign (bins, 0.0f); omega.assign (bins, 0.0f); peaks.assign (bins, 0);
    inCount = 0; lastStart = -1; fifoW = 0; anaPos = 0.0; readPos = 0.0; reading = false;
    resetPending = true;
    preHp.set (fs, 52.0, 0.70710678); preHp.reset();
    setLatencySamples (0);   // la nota trasposta arriva dopo un frame; l'asciutto e il plettro restano a latenza zero

    ratioSm = std::exp2 ((pSemi->load() + pFine->load() / 100.0f) / 12.0f);
    glide = (pSemi->load() + pFine->load() / 100.0f);
    idSm = std::abs (glide) < 0.005f ? 1.0f : 0.0f;
    onGain = pOn->load() > 0.5f ? 1.0f : 0.0f;
    onCoef = 1.0f - sp::coefMs (10.0 / 3.0, fs);
    mixSm = pMix->load() / 100.0f;
    pickHp1.set (fs, 2800.0); pickHp2.set (fs, 2800.0); pickHp1.reset(); pickHp2.reset();
    fAtk = sp::coefMs (0.1, fs); fRel = sp::coefMs (6.0, fs);
    sAtk = sp::coefMs (6.0, fs); sRel = sp::coefMs (60.0, fs);
    eFast = eSlow = tFast = tSlow = 0; pickLeft = 0; pickGain = 0; pickHold = onsetHold = 0;
    pickLen = (int) std::round (0.005 * fs);
    tMud.reset(); tAir.reset(); lastT = -1.0f;
}

// Un frame del phase vocoder (allungamento del tempo): gli N campioni d'ingresso da start -> spettro -> fasi di sintesi
// (picchi che avanzano di H × la loro frequenza, regioni bloccate al loro picco) -> antitrasformata, finestra, somma
// sovrapposta. Ogni frame aggiunge H campioni allo stream allungato (fifo), che il ricampionatore legge.
void BDropProcessor::processFrame (juce::int64 start, int hopIn) noexcept
{
    const int half = N / 2, ringMask = 4 * N - 1;
    for (int i = 0; i < N; ++i) frame[(size_t) i] = inRing[(size_t) ((start + i) & ringMask)] * win[(size_t) i];
    std::fill (frame.begin() + N, frame.end(), 0.0f);
    fft->performRealOnlyForwardTransform (frame.data(), true);

    // analisi: modulo, fase e frequenza istantanea (radianti per campione) di ogni bin, sul salto d'analisi vero
    const float twoPi = juce::MathConstants<float>::twoPi;
    float peakE = 0.0f, allE = 0.0f;
    for (int k = 0; k <= half; ++k)
    {
        const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
        phase[(size_t) k] = std::atan2 (im, re);
        const float expected = twoPi * (float) k * (float) hopIn / (float) N;
        float dev = phase[(size_t) k] - prevPhase[(size_t) k] - expected;
        dev -= twoPi * std::round (dev / twoPi);   // srotolato in (-pi, pi]
        omega[(size_t) k] = (expected + dev) / (float) juce::jmax (1, hopIn);
        prevPhase[(size_t) k] = phase[(size_t) k];
        allE += mag[(size_t) k] * mag[(size_t) k];
    }

    // picchi locali
    int nPeaks = 0;
    for (int k = 1; k < half; ++k)
        if (mag[(size_t) k] > mag[(size_t) (k - 1)] && mag[(size_t) k] >= mag[(size_t) (k + 1)] && mag[(size_t) k] > 1.0e-7f)
            peaks[(size_t) nPeaks++] = k;

    // sintesi: ogni picco avanza di H × la sua frequenza; i bin della sua regione tengono la fase relativa d'ingresso.
    // A una nuova plettrata (o alla prima volta) le fasi di sintesi ripartono da quelle d'ingresso.
    const bool reset = resetPending || lastStart < 0;
    resetPending = false;
    std::fill (frame.begin(), frame.end(), 0.0f);
    for (int r = 0; r < nPeaks; ++r)
    {
        const int p = peaks[(size_t) r];
        const int lo = r == 0 ? 0 : (peaks[(size_t) (r - 1)] + p) / 2 + 1;
        const int hi = r == nPeaks - 1 ? half : (p + peaks[(size_t) (r + 1)]) / 2;
        const float sp = reset ? phase[(size_t) p] : synthPhase[(size_t) p] + (float) H * omega[(size_t) p];
        if (mag[(size_t) p] > 0.0f) peakE += mag[(size_t) p] * mag[(size_t) p];
        for (int b = lo; b <= hi; ++b)
        {
            const float ph = sp + phase[(size_t) b] - phase[(size_t) p];
            synthPhase[(size_t) b] = std::remainder (ph, twoPi);
            frame[(size_t) (2 * b)] = mag[(size_t) b] * std::cos (ph);
            frame[(size_t) (2 * b + 1)] = mag[(size_t) b] * std::sin (ph);
        }
    }
    if (nPeaks == 0) for (int k = 0; k <= half; ++k) synthPhase[(size_t) k] = phase[(size_t) k];
    frame[1] = 0.0f;                          // DC e Nyquist reali
    frame[(size_t) (2 * half + 1)] = 0.0f;
    fft->performRealOnlyInverseTransform (frame.data());

    // somma sovrapposta (salto di sintesi H): i primi H campioni sono pronti e vanno nello stream allungato
    for (int i = 0; i < N; ++i) outAcc[(size_t) i] += frame[(size_t) i] * win[(size_t) i] * olaScale;
    for (int i = 0; i < H; ++i) fifo[(size_t) ((fifoW + i) & (kFifo - 1))] = outAcc[(size_t) i];
    fifoW += H;
    std::copy (outAcc.begin() + H, outAcc.end(), outAcc.begin());
    std::fill (outAcc.end() - H, outAcc.end(), 0.0f);
    lastStart = start;
    lockQuality.store (allE > 1.0e-12f ? juce::jlimit (0.0f, 1.0f, 4.0f * peakE / allE) : 0.0f, std::memory_order_relaxed);
}

void BDropProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    const int outCh = buffer.getNumChannels();
    if (outCh == 0) return;
    float* io = buffer.getWritePointer (0);

    const float onTarget = pOn->load() > 0.5f ? 1.0f : 0.0f;
    const int range = juce::jlimit (0, 3, (int) pRange->load());
    const float semis = pSemi->load() + pFine->load() / 100.0f;
    const float glideCoef = 1.0f - sp::coefMs (range == whammy ? 60.0 : 4.0, fs);
    const bool zeroLag = pZero->load() > 0.5f;
    const float mixTarget = pMix->load() / 100.0f;

    // tension: quanto scendi (fino a un'ottava) per la quantità scelta
    const float t = semis < 0.0f ? (pTension->load() / 100.0f) * juce::jmin (1.0f, -semis / 12.0f) : 0.0f;
    if (std::abs (t - lastT) > 1.0e-4f)
    {
        tOn = t > 0.001f;
        tMud.set (fs, 160.0, 1.2);
        tAir.set (fs, 3200.0, 0.7071);
        mudG = sp::dbToGain (-4.5f * t);
        airG = sp::dbToGain (2.0f * t) - 1.0f;
        lastT = t;
    }

    int sp0 = scopePos.load (std::memory_order_relaxed);
    // spento e già sfumato: l'uscita è l'ingresso, identica (lo shifter continua a riempirsi)
    if (onTarget < 0.5f && onGain < 1.0e-5f)
    {
        onGain = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            inRing[(size_t) (inCount & (4 * N - 1))] = preHp.processHP (io[i]); ++inCount;
            sp0 = (sp0 + 1) & (kScope - 1); scopeIn[(size_t) sp0] = io[i]; scopeOut[(size_t) sp0] = io[i];
        }
        // al riaccendersi lo shifter riparte pulito: analisi dal punto attuale, fasi d'ingresso
        resetPending = true; anaPos = (double) inCount; lastStart = -1; reading = false;
        std::fill (outAcc.begin(), outAcc.end(), 0.0f);
        scopePos.store (sp0, std::memory_order_release);
        for (int c = 1; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
        return;
    }

    bool flash = false;
    for (int i = 0; i < n; ++i)
    {
        const float x = io[i];
        glide += glideCoef * (semis - glide);
        ratioSm = std::exp2 (glide / 12.0f);
        mixSm += 0.002f * (mixTarget - mixSm);
        onGain += onCoef * (onTarget - onGain);
        // a 0 semitoni il plugin passa l'ingresso identico (senza il frame di ritardo), con 10 ms di dissolvenza
        const float idTarget = std::abs (semis) < 0.005f ? 1.0f : 0.0f;
        idSm += onCoef * (idTarget - idSm);
        if (std::abs (idSm - idTarget) < 1.0e-5f) idSm = idTarget;

        // transiente sulla DI (inviluppo veloce che supera di molto quello lento): al prossimo frame, phase reset
        {
            const float a = std::abs (x);
            tFast = a > tFast ? fAtk * (tFast - a) + a : fRel * (tFast - a) + a;
            tSlow = a > tSlow ? sAtk * (tSlow - a) + a : sRel * (tSlow - a) + a;
            if (onsetHold > 0) --onsetHold;
            else if (tFast > 2.0f * tSlow + 1.0e-3f) { resetPending = true; onsetHold = (int) (0.03 * fs); }
        }

        // phase vocoder: l'ingresso filtrato entra nell'anello; quando c'è un frame intero dalla prossima posizione
        // d'analisi (che avanza di H / rapporto) lo si elabora
        inRing[(size_t) (inCount & (4 * N - 1))] = preHp.processHP (x);
        ++inCount;
        const juce::int64 start = (juce::int64) anaPos;
        if (inCount >= start + N)
        {
            processFrame (start, lastStart < 0 ? H : (int) (start - lastStart));
            anaPos += (double) H / (double) ratioSm;
            if (! reading) { readPos = (double) (fifoW - H) - 2.0; reading = true; }   // la lettura parte un salto indietro
        }
        // ricampionatore: legge lo stream allungato alla velocità del rapporto (Hermite). Il livello della fifo resta
        // attorno a mezzo salto: una correzione minima della velocità compensa gli arrotondamenti dei salti d'analisi.
        float wet = 0.0f;
        if (reading)
        {
            const double level = (double) fifoW - readPos;
            const double rate = (double) ratioSm * (1.0 + 0.0005 * (level - 0.5 * H - 4.0) / (double) H);
            const double pos = juce::jmin (readPos, (double) fifoW - 3.0);
            const juce::int64 i1 = (juce::int64) std::floor (pos);
            const float f = (float) (pos - (double) i1);
            auto at = [this] (juce::int64 k) { return fifo[(size_t) (k & (kFifo - 1))]; };
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            wet = ((c3 * f + c2) * f + c1) * f + y1;
            readPos = pos + rate;
        }

        // tension sul segnale trasposto
        if (tOn)
        {
            tMud.tick (wet); wet += (mudG - 1.0f) * tMud.k * tMud.bp;
            wet += airG * tAir.processHP (wet);
        }

        float y = (1.0f - mixSm) * x + mixSm * wet;

        // zero-lag pick: la parte sopra 2,8 kHz della DI passa subito, per 5 ms, a ogni nuova plettrata
        const float hf = pickHp2.processHP (pickHp1.processHP (x));
        const float a = std::abs (hf);
        eFast = a > eFast ? fAtk * (eFast - a) + a : fRel * (eFast - a) + a;
        eSlow = a > eSlow ? sAtk * (eSlow - a) + a : sRel * (eSlow - a) + a;
        if (pickHold > 0) --pickHold;
        if (zeroLag && pickHold == 0 && eFast > 2.5f * eSlow + 1.0e-4f) { pickLeft = pickLen; pickHold = pickLen * 3; flash = true; }
        float pg = 0.0f;
        if (pickLeft > 0) { pg = pickLeft > pickLen / 4 ? 1.0f : (float) pickLeft / (float) (pickLen / 4); --pickLeft; }
        pickGain += 0.25f * (pg - pickGain);   // ~0,1 ms: niente scalino
        y += 0.8f * mixSm * pickGain * hf;

        const float out = x + onGain * (1.0f - idSm) * (y - x);
        io[i] = out;
        sp0 = (sp0 + 1) & (kScope - 1); scopeIn[(size_t) sp0] = x; scopeOut[(size_t) sp0] = out;
    }
    scopePos.store (sp0, std::memory_order_release);
    if (flash) pickFlash.store (true, std::memory_order_relaxed);
    pickHp1.flush(); pickHp2.flush(); preHp.flush(); tMud.flush(); tAir.flush();
    for (int c = 1; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

void BDropProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BDropProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BDropProcessor::createEditor() { return new BDropEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BDropProcessor(); }

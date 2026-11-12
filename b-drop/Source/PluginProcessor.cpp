#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // finestra di Hann in tabella (niente coseno per campione)
    struct HannTable
    {
        static constexpr int N = 1024;
        std::array<float, N + 2> t {};
        HannTable() { for (int i = 0; i <= N + 1; ++i) t[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * juce::jmin (i, N) / N); }
        float operator() (float p) const noexcept
        {
            p = juce::jlimit (0.0f, 1.0f, p) * (float) N;
            const int i = (int) p; const float f = p - (float) i;
            return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
        }
    };
    const HannTable& hann() { static const HannTable h; return h; }
}

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
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0;
    grain = (float) juce::jlimit (64.0, (double) kBuf / 3.0, 0.014 * fs);
    // l'aggancio deve poter spostare la testina di un periodo intero della nota più bassa (~75 Hz)
    search = juce::jlimit (32, 2048, (int) std::round (640.0 * fs / 48000.0));
    dMin = 4.0f;
    heads[0] = { dMin, 0.0f };
    heads[1] = { dMin + 0.5f * grain, 0.0f };
    ratioSm = std::exp2 ((pSemi->load() + pFine->load() / 100.0f) / 12.0f);
    glide = (pSemi->load() + pFine->load() / 100.0f);
    idSm = std::abs (glide) < 0.005f ? 1.0f : 0.0f;
    onGain = pOn->load() > 0.5f ? 1.0f : 0.0f;
    onCoef = 1.0f - sp::coefMs (10.0 / 3.0, fs);
    mixSm = pMix->load() / 100.0f;
    pickHp1.set (fs, 3500.0); pickHp2.set (fs, 3500.0); pickHp1.reset(); pickHp2.reset();
    fAtk = sp::coefMs (0.1, fs); fRel = sp::coefMs (6.0, fs);
    sAtk = sp::coefMs (6.0, fs); sRel = sp::coefMs (60.0, fs);
    eFast = eSlow = 0; pickLeft = 0; pickGain = 0;
    pickLen = (int) std::round (0.004 * fs);
    pickHold = 0;
    tHp.reset(); tMud.reset(); tAir.reset(); lastT = -1.0f;
}

float BDropProcessor::readAt (float delay) const noexcept
{
    // Hermite a 4 punti attorno alla posizione (w - delay)
    const float pos = (float) w - delay;
    const float fl = std::floor (pos);
    const int i = (int) fl;
    const float f = pos - fl;
    const float y0 = buf[(size_t) ((i - 1) & (kBuf - 1))], y1 = buf[(size_t) (i & (kBuf - 1))];
    const float y2 = buf[(size_t) ((i + 1) & (kBuf - 1))], y3 = buf[(size_t) ((i + 2) & (kBuf - 1))];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

// Aggancio di fase: tra i ritardi baseDelay + o (o = 0...search, passo 2: fino a un periodo della nota più bassa)
// sceglie quello la cui forma d'onda recente somiglia di più a quella letta dall'altra testina (correlazione
// normalizzata su 256 campioni presi a passo 2, poi vertice della parabola per la frazione di campione). A parità quasi esatta vince il ritardo più corto: meno latenza sulle note acute.
float BDropProcessor::findSplice (float baseDelay, float otherDelay) noexcept
{
    constexpr int M = 256;
    const int dB = (int) std::round (otherDelay);
    double eB = 0;
    for (int j = 0; j < M; j += 2) { const float b = buf[(size_t) ((w - dB - j) & (kBuf - 1))]; eB += (double) b * b; }
    if (eB < 1.0e-12) { lockQuality.store (0.0f, std::memory_order_relaxed); return 0.0f; }
    const int nC = search / 2 + 1;
    double bestC = -2.0;
    for (int k = 0; k < nC; ++k)
    {
        const int dA = (int) std::round (baseDelay) + 2 * k;
        double ab = 0, eA = 0;
        for (int j = 0; j < M; j += 2)
        {
            const float a = buf[(size_t) ((w - dA - j) & (kBuf - 1))], b = buf[(size_t) ((w - dB - j) & (kBuf - 1))];
            ab += (double) a * b; eA += (double) a * a;
        }
        corr[(size_t) k] = (float) (ab / std::sqrt (eA * eB + 1.0e-20));
        bestC = juce::jmax (bestC, (double) corr[(size_t) k]);
    }
    float best = 0.0f;
    for (int k = 0; k < nC; ++k)
        if (corr[(size_t) k] >= bestC - 0.02)
        {
            while (k + 1 < nC && corr[(size_t) k + 1] >= corr[(size_t) k]) ++k;   // sale fino al massimo locale
            best = 2.0f * (float) k;
            if (k > 0 && k + 1 < nC)   // vertice della parabola tra i tre punti: aggancio al campione frazionario
            {
                const float c0 = corr[(size_t) k - 1], c1 = corr[(size_t) k], c2 = corr[(size_t) k + 1], den = c0 - 2.0f * c1 + c2;
                if (den < -1.0e-9f) best += 2.0f * juce::jlimit (-0.5f, 0.5f, 0.5f * (c0 - c2) / den);
            }
            break;
        }
    lockQuality.store ((float) juce::jmax (0.0, bestC), std::memory_order_relaxed);
    return best;
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
        tHpOn = t > 0.001f;
        tHp.set (fs, 45.0 + 50.0 * t, 0.7071);
        tMud.set (fs, 165.0, 1.4);
        tAir.set (fs, 3000.0, 0.7071);
        mudG = sp::dbToGain (-6.0f * t);
        airG = sp::dbToGain (3.0f * t) - 1.0f;
        lastT = t;
    }

    // spento e già sfumato: l'uscita è l'ingresso, identica (la linea continua a riempirsi)
    if (onTarget < 0.5f && onGain < 1.0e-5f)
    {
        onGain = 0.0f;
        int sp0 = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) w] = io[i]; w = (w + 1) & (kBuf - 1);
            sp0 = (sp0 + 1) & (kScope - 1); scopeIn[(size_t) sp0] = io[i]; scopeOut[(size_t) sp0] = io[i];
        }
        scopePos.store (sp0, std::memory_order_release);
        for (int c = 1; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
        return;
    }

    const auto& win = hann();
    int sp0 = scopePos.load (std::memory_order_relaxed);
    bool flash = false;
    for (int i = 0; i < n; ++i)
    {
        const float x = io[i];
        buf[(size_t) w] = x;

        glide += glideCoef * (semis - glide);
        ratioSm = std::exp2 (glide / 12.0f);
        mixSm += 0.002f * (mixTarget - mixSm);
        onGain += onCoef * (onTarget - onGain);
        // a 0 semitoni il plugin passa l'ingresso identico (senza il ritardo delle testine), con 10 ms di dissolvenza
        const float idTarget = std::abs (semis) < 0.005f ? 1.0f : 0.0f;
        idSm += onCoef * (idTarget - idSm);
        if (std::abs (idSm - idTarget) < 1.0e-5f) idSm = idTarget;

        // 1-2. due testine sul grano, con aggancio di fase quando una ricomincia
        float wet;
        if (std::abs (ratioSm - 1.0f) < 1.0e-5f) wet = readAt (dMin + 0.5f * grain);   // nessuna trasposizione: stesso ritardo
        else
        {
            const float step = 1.0f - ratioSm;
            wet = 0.0f;
            for (int h = 0; h < 2; ++h)
            {
                auto& hd = heads[(size_t) h];
                hd.d += step;
                float p = (hd.d - hd.off - dMin) / grain;
                if (p >= 1.0f || p < 0.0f)
                {
                    hd.d += p >= 1.0f ? -grain : grain;
                    const auto& other = heads[(size_t) (1 - h)];
                    const float base = hd.d - hd.off;
                    const float o = findSplice (base, other.d);
                    hd.d = base + o; hd.off = o;
                    p = (hd.d - hd.off - dMin) / grain;
                }
                wet += win (p) * readAt (hd.d);
            }
        }

        // 4. tension sul segnale trasposto
        if (tHpOn)
        {
            wet = tHp.processHP (wet);
            tMud.tick (wet); wet += (mudG - 1.0f) * tMud.k * tMud.bp;
            wet += airG * tAir.processHP (wet);
        }

        float y = (1.0f - mixSm) * x + mixSm * wet;

        // 3. zero-lag pick: il clic del plettro (sopra 3,5 kHz) passa subito, per 4 ms, mentre la nota trasposta arriva
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
        w = (w + 1) & (kBuf - 1);
        sp0 = (sp0 + 1) & (kScope - 1); scopeIn[(size_t) sp0] = x; scopeOut[(size_t) sp0] = out;
    }
    scopePos.store (sp0, std::memory_order_release);
    if (flash) pickFlash.store (true, std::memory_order_relaxed);
    pickHp1.flush(); pickHp2.flush(); tHp.flush(); tMud.flush(); tAir.flush();
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

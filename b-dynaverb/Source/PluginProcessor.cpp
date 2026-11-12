#include "PluginProcessor.h"
#include "PluginEditor.h"

BDynaverbProcessor::BDynaverbProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BDynaverb", createLayout())
{
    pDuck = apvts.getRawParameterValue (BDynaverbIDs::duck);
    pSync = apvts.getRawParameterValue (BDynaverbIDs::sync);
    pSplit = apvts.getRawParameterValue (BDynaverbIDs::split);
    pBoost = apvts.getRawParameterValue (BDynaverbIDs::boost);
    pDelay = apvts.getRawParameterValue (BDynaverbIDs::delay);
    pSpace = apvts.getRawParameterValue (BDynaverbIDs::space);
    pSpeed = apvts.getRawParameterValue (BDynaverbIDs::speed);
}

juce::AudioProcessorValueTreeState::ParameterLayout BDynaverbProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BDynaverbIDs::crossover, 1 }, "Crossover", StringArray { "Low-mid", "Mid", "High-neck" }, 1));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDynaverbIDs::duck, 1 }, "Duck delay", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDynaverbIDs::sync, 1 }, "Sync", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDynaverbIDs::split, 1 }, "Split", NormalisableRange<float> (150.0f, 800.0f, 1.0f, 0.5f), 320.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDynaverbIDs::boost, 1 }, "Boost", NormalisableRange<float> (0.0f, 6.0f, 0.1f), 3.5f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDynaverbIDs::delay, 1 }, "Delay", NormalisableRange<float> (40.0f, 1200.0f, 1.0f, 0.5f), 380.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDynaverbIDs::space, 1 }, "Space", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDynaverbIDs::speed, 1 }, "Speed", NormalisableRange<float> (5.0f, 100.0f, 1.0f), 25.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    return l;
}

bool BDynaverbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && out == juce::AudioChannelSet::stereo();
}

void BDynaverbProcessor::applyCrossover (int which)
{
    auto* p = apvts.getParameter (BDynaverbIDs::split);
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (kCrossoverHz[juce::jlimit (0, 2, which)])); p->endChangeGesture();
}

float BDynaverbProcessor::syncedMs (float ms, double bpm, int* noteIndex)
{
    static constexpr float kBeats[6] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    const float beat = (float) (60000.0 / juce::jlimit (30.0, 300.0, bpm));
    int best = 0;
    for (int i = 1; i < 6; ++i) if (std::abs (kBeats[i] * beat - ms) < std::abs (kBeats[best] * beat - ms)) best = i;
    if (noteIndex != nullptr) *noteIndex = best;
    return juce::jlimit (40.0f, 1200.0f, kBeats[best] * beat);
}

void BDynaverbProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    lowHp.set (fs, 75.0); highLp.set (fs, 1800.0); zcrLp.set (fs, 2500.0);
    for (auto* f : { &lowHp, &lowLp, &highHp, &highLp, &zcrLp, &bell, &fbHpL, &fbLpL, &fbHpR, &fbLpR }) f->reset();
    bell.set (fs, 1100.0, 0.8);
    fbHpL.set (fs, 250.0); fbHpR.set (fs, 250.0); fbLpL.set (fs, 3200.0); fbLpR.set (fs, 3200.0);
    lastSplit = -1.0f;
    accLow = accHigh = accAll = 0; accN = 0; zc = 0; lastZ = 0;
    eLow = eHigh = zRate = 0; alpha = 0; score = 0.5f; lead = false; leadCount = 0; bellG = 0; outG = 1.0f;
    std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f); dw = 0;
    dSamples = pDelay->load() * 0.001f * (float) fs;
    env = 0; envAtk = sp::coefMs (10.0, fs); envRel = sp::coefMs (250.0, fs);
    hist = 0;
}

// ogni 32 campioni: energia riff contro lead, passaggi per lo zero, alpha verso 0 o 1
void BDynaverbProcessor::classify() noexcept
{
    const float n = (float) accN;
    const float a = 1.0f - std::exp (-n / (0.010f * (float) fs));   // energie levigate su ~10 ms
    eLow += a * ((float) (accLow / n) - eLow);
    eHigh += a * ((float) (accHigh / n) - eHigh);
    const float rate = (float) zc * (float) fs / (2.0f * n);   // frequenza dai passaggi per lo zero
    zRate += a * (rate - zRate);
    const float split = pSplit->load();
    const float ratioDb = 10.0f * std::log10 ((eHigh + 1.0e-9f) / (eLow + 1.0e-9f));
    const bool playing = accAll / n > 1.0e-6;   // sopra -60 dBFS: sotto, alpha resta com'è
    if (playing)
    {
        // isteresi di 1,5 dB: niente sfarfallio sul confine. Il lead deve durare ~5 ms (il clic del plettro
        // su una corda bassa, largo di banda, non basta); il ritorno al riff è immediato
        const bool wantsLead = (ratioDb > 1.5f && zRate > 0.8f * split) || ratioDb > 9.0f;
        leadCount = wantsLead ? leadCount + 1 : 0;
        if (! lead && leadCount * accN >= (int) (0.005 * fs)) lead = true;
        else if (lead && (ratioDb < -1.5f || (ratioDb < 4.0f && zRate < 0.6f * split))) lead = false;
    }
    const float speed = pSpeed->load();
    const float tau = lead ? speed : 0.6f * speed;
    alpha += (1.0f - std::exp (-n / (tau * 0.001f * (float) fs))) * ((lead ? 1.0f : 0.0f) - alpha);
    if (alpha < 1.0e-4f && ! lead) alpha = 0.0f;
    if (playing) score += a * (0.5f + 0.48f * std::tanh (ratioDb / 18.0f) - score);
    accLow = accHigh = accAll = 0; accN = 0; zc = 0;
}

void BDynaverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (inCh == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    const float split = pSplit->load();
    if (std::abs (split - lastSplit) > 0.5f)
    {
        lowLp.set (fs, 0.8 * split); highHp.set (fs, 1.06 * split);
        lastSplit = split;
    }
    // tempo del delay (sync: dalla figura più vicina al tempo del brano)
    float ms = pDelay->load();
    int note = -1;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    bpmNow.store (bpm, std::memory_order_relaxed);
    if (pSync->load() > 0.5f) ms = syncedMs (ms, bpm, &note);
    noteNow.store (note, std::memory_order_relaxed);
    delayMsNow.store (ms, std::memory_order_relaxed);
    const float dTarget = juce::jlimit (16.0f, (float) (kDl - 4), ms * 0.001f * (float) fs);
    const float space = pSpace->load() / 100.0f;
    const float fb = 0.25f + 0.35f * space;
    const float boost = pBoost->load();
    const bool duck = pDuck->load() > 0.5f;
    const float smooth = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));

    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (L[i] + R[i]);
        // 1. classificatore
        const float lo = lowLp.processLP (lowHp.processHP (x));
        const float hi = highLp.processLP (highHp.processHP (x));
        accLow += (double) lo * lo; accHigh += (double) hi * hi; accAll += (double) x * x;
        const float z = zcrLp.processLP (x);
        if ((z >= 0.0f) != (lastZ >= 0.0f) && std::abs (z - lastZ) > 1.0e-4f) ++zc;
        lastZ = z;
        if (++accN >= 32)
        {
            classify();
            bellG = sp::dbToGain (alpha * boost) - 1.0f;
            outG = sp::dbToGain (1.5f * alpha * juce::jmin (1.0f, boost / 3.5f));
            if (++hist >= 8)
            {
                hist = 0;
                const int p = (historyPos.load (std::memory_order_relaxed) + 1) & (kHistory - 1);
                history[(size_t) p] = { score, alpha };
                historyPos.store (p, std::memory_order_release);
            }
        }

        // 2. lead focus boost (alpha = 0: nessun cambiamento)
        float yl = L[i], yr = R[i];
        if (alpha > 0.0f)
        {
            bell.tick (x);
            const float add = bellG * bell.k * bell.bp;
            yl = (yl + add) * outG; yr = (yr + add) * outG;
        }
        else bell.tick (x);

        // 3. delay ping-pong: alpha apre l'invio, la coda resta
        dSamples += smooth * (dTarget - dSamples);
        const float pos = (float) dw - dSamples + (float) kDl;
        const int i0 = (int) pos; const float f = pos - (float) i0;
        const float rl = dl[(size_t) (i0 & (kDl - 1))] + f * (dl[(size_t) ((i0 + 1) & (kDl - 1))] - dl[(size_t) (i0 & (kDl - 1))]);
        const float rr = dr[(size_t) (i0 & (kDl - 1))] + f * (dr[(size_t) ((i0 + 1) & (kDl - 1))] - dr[(size_t) (i0 & (kDl - 1))]);
        const float send = alpha * space * 0.5f * (yl + yr);
        dl[(size_t) dw] = send + fb * fbLpR.processLP (fbHpR.processHP (rr));
        dr[(size_t) dw] = fb * fbLpL.processLP (fbHpL.processHP (rl));
        dw = (dw + 1) & (kDl - 1);
        const float a = std::abs (x);
        env = a > env ? envAtk * (env - a) + a : envRel * (env - a) + a;
        const float duckG = duck ? 1.0f - 0.75f * juce::jlimit (0.0f, 1.0f, (sp::gainToDb (env) + 40.0f) / 30.0f) : 1.0f;
        L[i] = yl + 0.8f * duckG * rl;
        R[i] = yr + 0.8f * duckG * rr;
    }
    for (auto* fl : { &lowHp, &lowLp, &highHp, &highLp, &zcrLp, &bell, &fbHpL, &fbLpL, &fbHpR, &fbLpR }) fl->flush();
    scoreNow.store (score, std::memory_order_relaxed);
    alphaNow.store (alpha, std::memory_order_relaxed);
}

void BDynaverbProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BDynaverbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BDynaverbProcessor::createEditor() { return new BDynaverbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BDynaverbProcessor(); }

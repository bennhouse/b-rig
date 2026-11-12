#include "PluginProcessor.h"
#include "PluginEditor.h"

BMeterProcessor::BMeterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BMeter", createLayout())
{
    pGain = apvts.getRawParameterValue (BMeterIDs::gain);
    startTimerHz (20);
}

juce::AudioProcessorValueTreeState::ParameterLayout BMeterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto c = [&] (const char* id, const char* name, StringArray items, int def) { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    c (BMeterIDs::view, "View", { "Osc", "Vec", "Wave", "Spec" }, 3);
    NormalisableRange<float> t (1.0f, 50.0f, 0.1f); t.setSkewForCentre (10.0f);
    f (BMeterIDs::oscTime, "Osc time", t, 10.0f, "ms");
    f (BMeterIDs::oscGain, "Osc gain", { 0.0f, 24.0f, 0.1f }, 0.0f, "dB");
    f (BMeterIDs::vecPersist, "Vec persist", { 50.0f, 1000.0f, 10.0f }, 250.0f, "ms");
    f (BMeterIDs::vecScale, "Vec scale", { 25.0f, 400.0f, 1.0f }, 100.0f, "%");
    f (BMeterIDs::waveWindow, "Wave window", { 2.0f, 15.0f, 0.5f }, 5.0f, "s");
    c (BMeterIDs::waveMode, "Wave mode", { "Peak+RMS", "L/R" }, 0);
    c (BMeterIDs::specSpeed, "Spec speed", { "Fast", "Med", "Slow" }, 1);
    c (BMeterIDs::specTilt, "Spec tilt", { "Flat", "3 dB", "4.5 dB" }, 1);
    c (BMeterIDs::target, "Target", { "Social", "YouTube", "Digital", "CD", "Podcast" }, 0);
    f (BMeterIDs::gain, "Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB");
    return l;
}

bool BMeterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BMeterProcessor::prepareToPlay (double rate, int)
{
    fs = rate;
    for (int c = 0; c < 2; ++c)
    {
        // filtri K esatti di ITU-R BS.1770-4 (stessi coefficienti a 48 kHz, ricavati per gli altri sample rate)
        auto& sh = kShelf[(size_t) c]; sh = {};
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            sh.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0); sh.b1 = (float) (2.0 * (K * K - Vh) / a0); sh.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            sh.a1 = (float) (2.0 * (K * K - 1.0) / a0); sh.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        auto& hp = kHp[(size_t) c]; hp = {};
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0f; hp.b1 = -2.0f; hp.b2 = 1.0f;
            hp.a1 = (float) (2.0 * (K * K - 1.0) / a0); hp.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
    }
    blockLen = (int) (0.1 * fs); blocks.fill (0.0); blockPos = blockFill = blockCount = 0; blockAcc = 0;
    gainSm.reset (fs, 0.010);
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    fifo.reset();
}

bool BMeterProcessor::applyAutoIfReady()
{
    if (! autoReady.exchange (false)) return false;
    auto* p = apvts.getParameter (BMeterIDs::gain);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, autoResult.load())));
    p->endChangeGesture();
    return true;
}

void BMeterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numCh = juce::jmin (2, getTotalNumInputChannels()), n = buffer.getNumSamples();
    if (const int cmd = autoCmd.exchange (0); cmd == 1) { autoLeft.store ((int) (5.0 * fs)); autoMaxSt = autoMaxPk = -100.0f; }
    else if (cmd == 2) autoLeft.store (0);

    // guadagno: letto una volta per blocco, rampa moltiplicativa da 10 ms (SIMD); a 0 dB fermo l'uscita è identica
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    if (gainSm.isSmoothing())
    {
        float* ch[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
        for (int i = 0; i < n; ++i) { const float gi = gainSm.getNextValue(); ch[0][i] *= gi; if (ch[1] != nullptr) ch[1][i] *= gi; }
    }
    else if (const float gi = gainSm.getCurrentValue(); std::abs (gi - 1.0f) > 1e-7f)
        for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::multiply (buffer.getWritePointer (c), gi, n);

    // loudness: pesatura K e blocchi da 100 ms (momentary = 4 blocchi, short-term = 30)
    int left = autoLeft.load();
    const float* rd[2] = { buffer.getReadPointer (0), buffer.getReadPointer (numCh > 1 ? 1 : 0) };
    for (int i = 0; i < n; ++i)
    {
        double pw = 0;
        for (int c = 0; c < numCh; ++c)
        {
            const float x = rd[c][i];
            const float k = kHp[(size_t) c].process (kShelf[(size_t) c].process (x));
            pw += (double) k * k;
            blockPeak = juce::jmax (blockPeak, std::abs (x));
        }
        if (numCh == 1) pw *= 2.0;   // mono = stesso segnale su due canali
        blockAcc += pw;
        if (++blockCount >= blockLen)
        {
            blocks[(size_t) blockPos] = blockAcc / blockLen;
            blockPos = (blockPos + 1) % 30; blockFill = juce::jmin (30, blockFill + 1);
            blockAcc = 0; blockCount = 0;
            auto lufs = [&] (int count)
            {
                double s = 0; for (int b = 0; b < count; ++b) s += blocks[(size_t) ((blockPos - 1 - b + 30) % 30)];
                const double m = s / count;
                return m > 1e-12 ? (float) (-0.691 + 10.0 * std::log10 (m)) : -100.0f;
            };
            const float mom = lufs (juce::jmin (4, blockFill)), st = lufs (juce::jmin (30, blockFill));
            const float pk = blockPeak > 1e-9f ? juce::Decibels::gainToDecibels (blockPeak) : -100.0f;
            momentary.store (mom); shortTerm.store (st); peakDb.store (pk);
            if (left > 0) { autoMaxSt = juce::jmax (autoMaxSt, st); autoMaxPk = juce::jmax (autoMaxPk, pk); }
            blockPeak = 0;
        }
        if (left > 0 && --left == 0 && autoMaxSt > -90.0f)
        {
            const auto& t = currentTarget();
            const float cur = pGain->load();
            autoResult.store (cur + juce::jmin (t.lufs - autoMaxSt, t.truePeak - autoMaxPk));   // il picco non supera il suo limite
            autoReady.store (true);
        }
    }
    autoLeft.store (left);

    // FIFO verso l'interfaccia: se è piena (interfaccia chiusa) i campioni si scartano, senza mai bloccare
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int c = 0; c < 2; ++c)
    {
        const int src = juce::jmin (c, numCh - 1);
        if (n1 > 0) fifoBuf.copyFrom (c, s1, buffer, src, 0, n1);
        if (n2 > 0) fifoBuf.copyFrom (c, s2, buffer, src, n1, n2);
    }
    fifo.finishedWrite (n1 + n2);
    for (auto& f : kShelf) f.flush(); for (auto& f : kHp) f.flush();
}

void BMeterProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
void BMeterProcessor::setStateInformation (const void* d, int s)
{
    if (auto x = getXmlFromBinary (d, s)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
}
juce::AudioProcessorEditor* BMeterProcessor::createEditor() { return new BMeterEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BMeterProcessor(); }

#include "PluginProcessor.h"
#include "PluginEditor.h"

BSynthesiaProcessor::BSynthesiaProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BSynthesia", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout BSynthesiaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto c = [&] (const char* id, const char* name, StringArray items, int def) { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    c (BSynIDs::mode, "Mode", { "Tunnel", "Flow", "Terrain", "Prism", "Attractor" }, 0);
    f (BSynIDs::reactivity, "Reactivity", { 0.0f, 100.0f, 1.0f }, 60.0f, "%");
    NormalisableRange<float> d (20.0f, 1000.0f, 1.0f); d.setSkewForCentre (150.0f);
    f (BSynIDs::decay, "Decay", d, 150.0f, "ms");
    f (BSynIDs::chaos, "Chaos", { 0.0f, 100.0f, 1.0f }, 30.0f, "%");
    f (BSynIDs::grid, "Grid", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    c (BSynIDs::colour, "Colour", { "White", "Amber", "Crimson", "Indigo", "Lime", "Cyan", "Teal" }, 0);
    c (BSynIDs::aspect, "Aspect", { "Full", "16:9", "9:16" }, 0);
    c (BSynIDs::bg, "Background", { "Dark", "Green", "Magenta", "Alpha" }, 0);
    return l;
}

bool BSynthesiaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BSynthesiaProcessor::prepareToPlay (double rate, int)
{
    fs = rate;
    subHp = {}; subHp.set (sp::Bq::HP, fs, 60.0, 0.7071); subLp = {}; subLp.set (sp::Bq::LP, fs, 180.0, 0.7071);
    hiHp1 = {}; hiHp1.set (sp::Bq::HP, fs, 1500.0, 0.7071); hiHp2 = hiHp1;
    sA = sp::coefMs (5.0, fs); sR = sp::coefMs (80.0, fs);
    fA = sp::coefMs (0.5, fs); fR = sp::coefMs (15.0, fs); slA = sp::coefMs (20.0, fs); slR = sp::coefMs (120.0, fs);
    hitR = sp::coefMs (60.0, fs);
    eSub = eF = eS = hit = 0; fifo.reset();
}

void BSynthesiaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numCh = juce::jmin (2, getTotalNumInputChannels()), n = buffer.getNumSamples();
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (numCh > 1 ? 1 : 0);
    float pk = 0; double lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        const float s = subLp.process (subHp.process (m)), s2 = s * s;
        eSub = s2 > eSub ? sA * (eSub - s2) + s2 : sR * (eSub - s2) + s2;
        const float h = std::abs (hiHp2.process (hiHp1.process (m)));
        eF = h > eF ? fA * (eF - h) + h : fR * (eF - h) + h;
        eS = h > eS ? slA * (eS - h) + h : slR * (eS - h) + h;
        const float rise = eS > 1.0e-5f ? juce::jlimit (0.0f, 1.0f, (eF / eS - 1.2f) * 0.6f) : 0.0f;
        hit = rise > hit ? rise : hitR * hit;
        pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
        lr += (double) L[i] * R[i]; ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i];
    }
    subEnergy.store (std::sqrt (eSub));
    transientHit.store (hit);
    levelDb.store (pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -100.0f);
    if (ll + rr > 1e-12) stereoWidth.store ((float) (lr / std::sqrt (ll * rr + 1e-20)));
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing.store (pos->getIsPlaying());
            if (auto t = pos->getTimeInSeconds()) timeMs.store (*t * 1000.0);
        }
    // FIFO verso l'interfaccia: se è piena (finestra chiusa) i campioni si scartano, senza mai bloccare
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int c = 0; c < 2; ++c)
    {
        const int src = juce::jmin (c, numCh - 1);
        if (n1 > 0) fifoBuf.copyFrom (c, s1, buffer, src, 0, n1);
        if (n2 > 0) fifoBuf.copyFrom (c, s2, buffer, src, n1, n2);
    }
    fifo.finishedWrite (n1 + n2);
    for (auto* f : { &subHp, &subLp, &hiHp1, &hiHp2 }) f->flush();
}

void BSynthesiaProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
void BSynthesiaProcessor::setStateInformation (const void* d, int s)
{
    if (auto x = getXmlFromBinary (d, s)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
}
juce::AudioProcessorEditor* BSynthesiaProcessor::createEditor() { return new BSynthesiaEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BSynthesiaProcessor(); }

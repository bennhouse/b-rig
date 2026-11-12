#include "PluginProcessor.h"
#include "PluginEditor.h"

BFocusProcessor::BFocusProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BFocus", createLayout())
{
    pThreshold = apvts.getRawParameterValue (BFocusIDs::threshold);
    pStrength  = apvts.getRawParameterValue (BFocusIDs::strength);
    pRelease   = apvts.getRawParameterValue (BFocusIDs::release);
    startTimerHz (30);
}

void BFocusProcessor::toggleLearn()
{
    const bool start = ! isLearning();
    engine.requestLearn (start);
    if (start) { learnRequestedMs = juce::Time::getMillisecondCounterHiRes(); learnMessage = {}; }
}

juce::String BFocusProcessor::getLearnStatus() const
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (engine.isLearnPending())
        return now - learnRequestedMs > 400.0 ? "learn: waiting for audio, press play" : juce::String();
    if (engine.getLearnState() == sp::FocusEngine::LearnListening)
        return "learn: mute the strings, then play a few notes";
    if (now - learnResultMs < 8000.0) return learnMessage;
    if (engine.isStuttering()) return "tip: works best on tight rhythm parts";
    return {};
}

bool BFocusProcessor::applyLearnResultIfReady()
{
    const int st = engine.getLearnState();
    if (st == sp::FocusEngine::LearnFailed)
    {
        learnFailed.store (true);
        learnMessage = engine.getLearnFailReason() == sp::FocusEngine::FailNoSignal
                     ? "learn failed: no audio received"
                     : "learn failed: no pause, mute the strings for a moment";
        learnResultMs = juce::Time::getMillisecondCounterHiRes();
        engine.acknowledgeLearn();
        return false;
    }
    if (st != sp::FocusEngine::LearnDone) return false;

    const auto r = engine.getLearnResult();
    auto set = [this] (const char* id, float v)
    {
        auto* prm = apvts.getParameter (id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    };
    // il learn regola solo la soglia: strength e release restano quelli scelti (di default 100 % e 15 ms)
    set (BFocusIDs::threshold, r.threshold);
    engine.setLfGate (r.lfGate);
    apvts.state.setProperty (lfGateId, r.lfGate, nullptr);   // salvata col progetto
    engine.setBandOffsets (r.offsets);
    for (int k = 0; k < sp::FocusEngine::kBands; ++k)
    {
        apvts.state.setProperty ("band_off_" + juce::String (k + 1), r.offsets[(size_t) k], nullptr);
        apvts.state.setProperty ("noise_db_" + juce::String (k + 1), r.noiseDb[(size_t) k], nullptr);
    }
    learnFailed.store (false);
    learnMessage = "learned: threshold " + juce::String (r.threshold, 1) + " dB";
    learnResultMs = juce::Time::getMillisecondCounterHiRes();
    engine.acknowledgeLearn();
    return true;
}

juce::AudioProcessorValueTreeState::ParameterLayout BFocusProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BFocusIDs::threshold, 1 }, "Threshold",
        NormalisableRange<float> (-100.0f, -10.0f, 0.1f), -85.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BFocusIDs::strength, 1 }, "Strength",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    NormalisableRange<float> rel (sp::FocusEngine::kMinReleaseMs, 1500.0f, 1.0f);
    rel.setSkewForCentre (100.0f);   // scala logaritmica: tanta risoluzione sui rilasci brevi
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { BFocusIDs::release, 1 }, "Release", rel, 15.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    return layout;
}

bool BFocusProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BFocusProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    loadBandState();
    setLatencySamples (0);
}

void BFocusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels(), sp::FocusEngine::kMaxChannels);
    engine.setParameters (pThreshold->load(), pStrength->load() / 100.0f, pRelease->load());
    engine.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples());
}

void BFocusProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void BFocusProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            loadBandState();
        }
}

// soglia del corpo e offset per banda appresi (salvati col progetto); senza learn i valori tipici di una DI
void BFocusProcessor::loadBandState()
{
    if (apvts.state.hasProperty (lfGateId)) engine.setLfGate ((float) apvts.state.getProperty (lfGateId));
    std::array<float, sp::FocusEngine::kBands> o {};
    for (int k = 0; k < sp::FocusEngine::kBands; ++k)
        o[(size_t) k] = (float) apvts.state.getProperty ("band_off_" + juce::String (k + 1), sp::FocusEngine::kDefaultOffset[k]);
    engine.setBandOffsets (o);
}

std::array<float, sp::FocusEngine::kBands> BFocusProcessor::getNoiseProfile() const
{
    std::array<float, sp::FocusEngine::kBands> r {};
    for (int k = 0; k < sp::FocusEngine::kBands; ++k)
        r[(size_t) k] = (float) apvts.state.getProperty ("noise_db_" + juce::String (k + 1), std::numeric_limits<float>::quiet_NaN());
    return r;
}

juce::AudioProcessorEditor* BFocusProcessor::createEditor() { return new BFocusEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BFocusProcessor(); }

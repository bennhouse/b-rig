#pragma once
// b-rig come plugin (AU, VST3): la stessa catena dell'app dentro una DAW, sempre mono in ingresso e stereo in uscita.
// I preset sono gli stessi dell'app (stessa cartella); la catena di ogni istanza, con il preset da cui viene, si salva
// nel progetto. Niente metronomo né setup audio: il tempo è quello del progetto (i moduli lo seguono con il loro
// sync, come i plugin singoli), l'audio è della DAW.
#include "UI/RigComponent.h"

namespace rig
{
class RigPluginProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    RigPluginProcessor();
    ~RigPluginProcessor() override;

    const juce::String getName() const override { return "b-rig"; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 10.0; }   // riverberi e delay
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    RigEngine engine;
    // il preset da cui viene la catena e se è stata modificata (li aggiorna l'interfaccia, li salva il progetto)
    juce::String presetName { RigComponent::kCleanSlate };
    bool presetDirty = false;
    int selectedIndex = -1;
    int editorW = 1180, editorH = 760;
    std::function<void()> onStateLoading;  // l'interfaccia aperta chiude il modulo prima che la catena cambi
    std::function<void()> onStateLoaded;   // e si aggiorna dopo setStateInformation

    juce::ValueTree saveState() const;
    void loadState (const juce::ValueTree&);

private:
    void timerCallback() override;
    juce::CriticalSection pendingLock;
    juce::ValueTree pending;               // stato arrivato fuori dal thread dei messaggi: si applica nel timer
};

class RigPluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RigPluginEditor (RigPluginProcessor&);
    ~RigPluginEditor() override;
    void resized() override;
    RigComponent& rig() { return *rc; }

private:
    RigPluginProcessor& proc;
    std::unique_ptr<RigComponent> rc;
};
}

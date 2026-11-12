#pragma once
// b-focus: de-noiser per DI di chitarra e basso. Latenza zero, trasparente bit per bit da aperto.

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/FocusEngine.h"

namespace BFocusIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* strength  = "strength";
    inline constexpr const char* release   = "release";
}

class BFocusProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BFocusProcessor();
    ~BFocusProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-focus"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Per l'interfaccia
    float getGainReductionDb() const noexcept { return engine.getGainReductionDb(); }
    float getStageMultiplier (int k) const noexcept { return engine.getMultiplier (k); }
    sp::FocusEngine& getEngine() noexcept { return engine; }   // test

    // Auto-Learn: il pulsante "learn" avvia o annulla l'ascolto di 4,5 s.
    // "In corso" comprende l'attesa dell'audio (la richiesta parte col primo blocco elaborato).
    void toggleLearn();
    bool isLearning() const noexcept { return engine.isLearnPending() || engine.getLearnState() == sp::FocusEngine::LearnListening; }
    float getLearnRemaining() const noexcept { return engine.isLearnPending() ? sp::FocusEngine::kLearnSeconds : engine.getLearnRemaining(); }
    // Riga di stato del learn per l'interfaccia (vuota se non c'è niente da dire).
    juce::String getLearnStatus() const;
    // Sul message thread: se l'ascolto è finito applica threshold/strength/release (notificando la DAW).
    // Restituisce true se ha applicato un risultato. Chiamato dal timer; pubblico per i test.
    bool applyLearnResultIfReady();
    bool lastLearnFailed() const noexcept { return learnFailed.load(); }

private:
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void timerCallback() override { applyLearnResultIfReady(); }

    sp::FocusEngine engine;
    std::atomic<bool> learnFailed { false };
    double learnRequestedMs = 0.0, learnResultMs = -1.0e9;
    juce::String learnMessage;                     // ultimo risultato (message thread)
    static inline const juce::Identifier lfGateId { "lf_gate_db" };
public:
    // noise profile del learn (dBFS per banda, salvato col progetto; NaN = nessun learn)
    std::array<float, sp::FocusEngine::kBands> getNoiseProfile() const;
    void loadBandState();
private:
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pStrength = nullptr;
    std::atomic<float>* pRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BFocusProcessor)
};

#pragma once
// b-synthesia: generatore visuale audio-reattivo (overlay e HUD per video di chitarra). L'audio passa inalterato,
// latenza zero. Thread audio: 4 descrittori atomici + copia dei campioni in un FIFO lock-free; nient'altro.
//   sub_energy    - RMS della banda 60-180 Hz (palm-mute), inviluppo 5/80 ms
//   transient_hit - salita dell'inviluppo veloce sopra 1,5 kHz rispetto a quello lento (scatta a ogni plettrata)
//   level         - picco del blocco (dBFS)
//   stereo_width  - correlazione L/R del blocco (-1..+1)
// Frequenza dominante, note e picchi FFT si calcolano sull'interfaccia (60 fps), solo a finestra aperta.
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "smp/Filters.h"

namespace BSynIDs
{
    inline constexpr const char* mode = "mode";
    inline constexpr const char* reactivity = "reactivity";
    inline constexpr const char* decay = "decay";
    inline constexpr const char* chaos = "chaos";
    inline constexpr const char* grid = "grid";
    inline constexpr const char* colour = "colour";
    inline constexpr const char* aspect = "aspect";
    inline constexpr const char* bg = "bg";
}

class BSynthesiaProcessor final : public juce::AudioProcessor
{
public:
    BSynthesiaProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-synthesia"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> subEnergy { 0.0f }, transientHit { 0.0f }, levelDb { -100.0f }, stereoWidth { 1.0f };
    std::atomic<bool> playing { false };
    std::atomic<double> timeMs { 0.0 };      // posizione della DAW (ms), per il timecode e i nomi dei fotogrammi
    static constexpr int kFifo = 1 << 15;
    juce::AbstractFifo fifo { kFifo };
    juce::AudioBuffer<float> fifoBuf { 2, kFifo };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    double fs = 48000.0;
    sp::Bq subHp, subLp, hiHp1, hiHp2;
    float eSub = 0, sA = 0, sR = 0, eF = 0, eS = 0, fA = 0, fR = 0, slA = 0, slR = 0, hit = 0, hitR = 0;
};

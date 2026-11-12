#pragma once
// b-meter: analisi di precisione + loudness con guadagno. Trasparente (salvo "gain"), latenza zero.
// Thread audio: guadagno, loudness EBU R128 / ITU-R BS.1770-4 (pesatura K: shelf +4 dB a 1,5 kHz + passa-alto
// 38 Hz; momentary 400 ms, short-term 3 s, blocchi da 100 ms), picco, e copia dei campioni in un FIFO lock-free
// (juce::AbstractFifo). FFT, oscilloscopio, vettorscopio e storia girano tutti sul thread dell'interfaccia.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"

namespace BMeterIDs
{
    inline constexpr const char* view = "view";
    inline constexpr const char* oscTime = "osc_time";
    inline constexpr const char* oscGain = "osc_gain";
    inline constexpr const char* vecPersist = "vec_persist";
    inline constexpr const char* vecScale = "vec_scale";
    inline constexpr const char* waveWindow = "wave_window";
    inline constexpr const char* waveMode = "wave_mode";
    inline constexpr const char* specSpeed = "spec_speed";
    inline constexpr const char* specTilt = "spec_tilt";
    inline constexpr const char* target = "target";
    inline constexpr const char* gain = "gain";
}

class BMeterProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BMeterProcessor();
    ~BMeterProcessor() override { stopTimer(); }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-meter"; }
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
    struct Target { const char* name; float lufs, truePeak; };
    static constexpr Target kTargets[5] = { { "social", -14.0f, -1.0f }, { "youtube", -14.0f, -1.0f }, { "digital", -10.5f, -0.5f },
                                            { "cd", -8.5f, -0.3f }, { "podcast", -16.0f, -1.0f } };
    const Target& currentTarget() const { return kTargets[juce::jlimit (0, 4, (int) apvts.getRawParameterValue (BMeterIDs::target)->load())]; }

    // loudness dopo il guadagno (LUFS; -inf = -100) e picco campione (dBFS)
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, peakDb { -100.0f };
    // auto: 5 s di ascolto del massimo short-term e del picco, poi gain = target - misura (limitato dal true peak)
    void toggleAuto() { autoCmd.store (isAutoListening() ? 2 : 1); }
    bool isAutoListening() const noexcept { return autoLeft.load() > 0 || autoCmd.load() == 1; }
    float autoRemaining() const noexcept { return (float) autoLeft.load() / (float) fs; }
    bool applyAutoIfReady();

    // FIFO lock-free verso l'interfaccia (stereo)
    static constexpr int kFifo = 1 << 16;
    juce::AbstractFifo fifo { kFifo };
    juce::AudioBuffer<float> fifoBuf { 2, kFifo };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { applyAutoIfReady(); }
    double fs = 48000.0;
    std::atomic<float>* pGain = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSm { 1.0f };
    std::array<sp::Bq, 2> kShelf, kHp;
    std::array<double, 30> blocks {};   // potenza media pesata K dei blocchi da 100 ms
    int blockPos = 0, blockFill = 0, blockLen = 4800; double blockAcc = 0; int blockCount = 0;
    float blockPeak = 0;
    std::atomic<int> autoCmd { 0 }, autoLeft { 0 };
    std::atomic<bool> autoReady { false };
    float autoMaxSt = -100.0f, autoMaxPk = -100.0f;
    std::atomic<float> autoResult { 0.0f };
};

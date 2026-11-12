#pragma once
// b-wire: synth sub-armonico e fuzz a PLL, latenza zero.
//   1. rilevamento - passa-banda 65-900 Hz (solo per il rilevamento), RMS veloce (2/25 ms), trigger di Schmitt
//                    a soglie adattive (±0,18 x RMS), gate interno sotto -54 dB; periodo tra due fronti di salita
//                    con frazione di campione, inseguito con il tempo di "tracking" (2 ms stretto, 45 ms glide)
//   2. oscillatore - fase master con quadra PolyBLEP; flip-flop -1 e -2 ottave (anche loro PolyBLEP)
//   3. fuzz & xor  - tanh(8 x) e "glitch" = fuzz x sub -1 (porta XOR analogica); "ring" = DI x seno alla
//                    fondamentale agganciata (ring modulator adattivo: la portante segue la nota); mix delle 5 voci
//   4. vca & filtro- inviluppo della DI (gli oscillatori suonano solo quando suoni) e passa-basso SVF (Q 1,4)
//   5. uscita      - mix dry/wet, level fisso o "auto" (RMS di uscita pareggiato a quello d'ingresso mentre suoni)
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"

namespace BWireIDs
{
    inline constexpr const char* tracking = "tracking";
    inline constexpr const char* fuzz = "fuzz";
    inline constexpr const char* sub1 = "sub1";
    inline constexpr const char* sub2 = "sub2";
    inline constexpr const char* glitch = "glitch";
    inline constexpr const char* filter = "filter";
    inline constexpr const char* ring = "ring";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* level = "level";
    inline constexpr const char* levelAuto = "level_auto";
}

class BWireProcessor final : public juce::AudioProcessor
{
public:
    BWireProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-wire"; }
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
    static constexpr int kNumP = 10;
    // Per l'interfaccia: frequenza agganciata e ultime forme d'onda (quadra, -1, -2 ottave)
    std::atomic<float> pllHz { 0.0f }, gateLevel { 0.0f }, autoGainDb { 0.0f };
    static constexpr int kScope = 2048;
    std::array<std::array<float, 3>, kScope> scope {};
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    std::atomic<float>* p[kNumP] {};
    double fs = 48000.0;
    sp::Bq detHp, detLp;
    float env = 0, eAtk = 0, eRel = 0, prevX = 0, sinceRise = 0, period = 0, gateEnv = 0, gAtk = 0, gRel = 0;
    int cmp = -1;
    float phase = 0, sub1 = 1, sub2 = 1; bool sub2Next = false;
    float svfIc1[2] {}, svfIc2[2] {}, svfA1 = 0, svfA2 = 0, svfA3 = 0, svfK = 0, lastCut = -1;
    float autoG = 1, eIn = 0, eOut = 0, aRms = 0, aGain = 0;
    juce::SmoothedValue<float> mixSm { 1.0f }, levelSm { 1.0f };
};

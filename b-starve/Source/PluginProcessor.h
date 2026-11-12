#pragma once
// b-starve: circuito sotto-alimentato + 3 "datamosh" digitali per chitarra, in serie.
// Ogni effetto: on/off, intensity, volume, mix. Tutti spenti all'apertura. La PRIMA volta che un effetto si accende
// (per ogni caricamento del plugin) si misura per ~0,4 s di suono (dopo 0,25 s di assestamento) l'RMS in ingresso e in uscita dall'effetto e il suo
// volume si regola da solo per pareggiarli; poi resta quello (o quello scelto a mano). Ogni salto (stutter, maschere di bit, buchi) ha un crossfade
// coseno di 64 campioni: niente click digitali.
//   1. starve  - sag dalle basse (8/140 ms) che sposta il bias, gate "velcro" a zona morta, saturazione asimmetrica
//   2. mosh    - buffer di 250 ms: a tempi casuali (16-80 ms) congela e ripete finestre da 60 a 6 ms, al 25 % al contrario
//   3. corrupt - campioni a 16 bit con XOR sui bit bassi (maschera casuale) e buchi ritmici di 3-15 ms
//   4. melt    - sample & hold la cui frequenza crolla mentre la nota decade, con filtro risonante instabile
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BStarveIDs
{
    inline const char* const fx[4] = { "starve", "mosh", "corrupt", "melt" };
    inline juce::String on (int e) { return juce::String (fx[e]) + "_on"; }
    inline juce::String intensity (int e) { return juce::String (fx[e]) + "_int"; }
    inline juce::String mix (int e) { return juce::String (fx[e]) + "_mix"; }
    inline juce::String volume (int e) { return juce::String (fx[e]) + "_vol"; }
    inline constexpr const char* output = "output";
}

class BStarveProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BStarveProcessor();
    ~BStarveProcessor() override { stopTimer(); }
    bool isCalibrating (int e) const noexcept { return calState[(size_t) e].load() == 1; }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-starve"; }
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
    // mini-visualizzatori: uscita di ogni effetto (canale sinistro, 1 campione su 4)
    static constexpr int kScope = 512;
    std::array<std::array<float, kScope>, 4> scope {};
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // applica (sul message thread) il volume calibrato
    struct Ch
    {
        // starve
        sp::Bq sagLp; float sag = 0;
        // mosh
        std::vector<float> buf; int w = 0; bool stut = false; int segStart = 0, segLen = 1, idx = 0, repeats = 0; bool rev = false;
        float moshMix = 0;
        // corrupt
        int maskOld = 0, maskNew = 0, maskX = 64, maskTimer = 0, dropTimer = 0, dropLeft = 0; float dropG = 1;
        // melt
        float env = 0, peak = 1e-4f, held = 0, ph = 0, ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0; int ctl = 0;
    };
    float starve (Ch&, float x, float k);
    float mosh (Ch&, float x, float k, int c);
    float corrupt (Ch&, float x, float k);
    float melt (Ch&, float x, float k);

    std::atomic<float>* pOn[4] {}; std::atomic<float>* pInt[4] {}; std::atomic<float>* pMix[4] {}; std::atomic<float>* pVol[4] {};
    // calibrazione del volume alla prima accensione: 0 = mai acceso, 1 = misura in corso, 2 = fatta (fino al ricaricamento)
    std::array<std::atomic<int>, 4> calState {};
    std::array<std::atomic<float>, 4> calResult {};
    std::array<std::atomic<bool>, 4> calReady {};
    std::array<double, 4> calIn {}, calOut {};
    std::array<int, 4> calCount {};
    std::array<float, 4> lastOn {}, volG { 1, 1, 1, 1 };
    float volC = 0;
    std::atomic<float>* pOut = nullptr;
    double fs = 48000.0;
    std::array<Ch, 2> ch;
    std::array<float, 4> onG {};
    float onCoef = 0, sagA = 0, sagR = 0, meltR = 0, peakR = 0, outC = 0;
    float outG = 1, lfo = 0;
    int bufMask = 0;
    int clock = 0, nextTick = 0, scopeDecim = 0;
    juce::Random rng { 99 };
};

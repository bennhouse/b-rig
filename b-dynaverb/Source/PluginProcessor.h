#pragma once
// b-dynaverb: capisce da solo se stai suonando un riff sulle corde basse o una frase/assolo su quelle alte e,
// solo sulle note soliste, accende un canale "lead" (spinta sulle medie + delay stereo ping-pong). Senza
// automazioni, latenza zero, niente FFT né pitch tracker:
//   1. classificatore (ogni 32 campioni) - energia della banda riff (75 Hz...0,8 x split) contro quella della banda
//      lead (1,06 x split...1,8 kHz), più i passaggi per lo zero sotto 2,5 kHz. Sopra la soglia "alpha" sale verso
//      1 (lead) in "speed" ms, sotto torna a 0 (riff) più in fretta (0,6 x speed): un palm-mute chiude subito
//   2. lead focus boost - campana a 1,1 kHz (Q 0,8) fino a "boost" dB e +1,5 dB d'uscita, dosati da alpha
//   3. delay ping-pong   - alpha apre l'INVIO al delay, non l'uscita: tornando al riff l'ultima nota dell'assolo
//      sfuma da sola. Nel ritorno un passa-banda 250 Hz...3,2 kHz; "duck delay" abbassa le ripetizioni mentre suoni
// Con alpha = 0 e nessuna coda il segnale passa identico.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BDynaverbIDs
{
    inline constexpr const char* crossover = "crossover";
    inline constexpr const char* duck      = "duck";
    inline constexpr const char* sync      = "sync";
    inline constexpr const char* split     = "split";
    inline constexpr const char* boost     = "boost";
    inline constexpr const char* delay     = "delay";
    inline constexpr const char* space     = "space";
    inline constexpr const char* speed     = "speed";
}

class BDynaverbProcessor final : public juce::AudioProcessor
{
public:
    BDynaverbProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-dynaverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    static constexpr float kCrossoverHz[3] = { 260.0f, 320.0f, 420.0f };   // low-mid, mid, high-neck
    void applyCrossover (int which);   // message thread: imposta split

    // tempo del delay effettivo (con sync: la figura più vicina al tempo del brano)
    static float syncedMs (float ms, double bpm, int* noteIndex = nullptr);
    static constexpr const char* kNoteNames[6] = { "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2" };

    // per l'interfaccia
    std::atomic<float> scoreNow { 0.5f }, alphaNow { 0.0f }, delayMsNow { 380.0f };
    std::atomic<int> noteNow { -1 };
    std::atomic<double> bpmNow { 120.0 };
    struct Frame { float score = 0.5f, alpha = 0.0f; };
    static constexpr int kHistory = 256;
    std::array<Frame, kHistory> history {};
    std::atomic<int> historyPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void classify() noexcept;
    std::atomic<float>* pDuck = nullptr; std::atomic<float>* pSync = nullptr; std::atomic<float>* pSplit = nullptr;
    std::atomic<float>* pBoost = nullptr; std::atomic<float>* pDelay = nullptr; std::atomic<float>* pSpace = nullptr;
    std::atomic<float>* pSpeed = nullptr;

    double fs = 48000.0;
    // classificatore
    sp::Svf lowHp, lowLp, highHp, highLp, zcrLp;
    double accLow = 0, accHigh = 0, accAll = 0;
    int accN = 0, zc = 0;
    float lastZ = 0.0f, eLow = 0, eHigh = 0, zRate = 0, alpha = 0.0f, score = 0.5f, lastSplit = -1.0f;
    bool lead = false;
    int leadCount = 0;
    // boost
    sp::Svf bell;
    float bellG = 0.0f, outG = 1.0f;
    // delay
    static constexpr int kDl = 1 << 17;
    std::vector<float> dl = std::vector<float> (kDl, 0.0f), dr = std::vector<float> (kDl, 0.0f);
    int dw = 0;
    float dSamples = 18240.0f;
    sp::Svf fbHpL, fbLpL, fbHpR, fbLpR;
    float env = 0.0f, envAtk = 0, envRel = 0;
    int hist = 0;
};

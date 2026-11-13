#pragma once
// b-comp: compressore per chitarra (e per tutto il resto). Latenza zero, nessuna allocazione nel ciclo audio.
//   1. sidechain       - il rilevatore legge l'ingresso dopo un passa-alto ("sc hp", 80 Hz di base): le basse di un
//                        palm-mute non fanno pompare tutto il suono. Stereo: un solo rilevatore (il più forte dei due)
//   2. gain computer   - in dB, con il ginocchio morbido ("knee"): sotto la soglia nulla, sopra 1/ratio
//   3. tempi           - attack e release sulla riduzione, in dB (smoothing a due rami), per stile:
//        clean   VCA trasparente, picco
//        opto    più lento ad attaccare (almeno 10 ms) e con il rilascio che si allunga quanto più e quanto più a lungo
//                comprime, come una cella ottica
//        fet     veloce, ginocchio duro e un poco di saturazione che cresce con la riduzione (la grinta)
//        squash  rilevatore RMS (10 ms) per una compressione forte ma liscia: sustain da country e funk
//        multi   tre bande (Linkwitz-Riley del 4° ordine a 250 Hz e 2,5 kHz) tarate per la chitarra: le basse
//                comprimono 3 dB prima e con tempi più lenti (palm-mute fermi), le alte 3 dB dopo e più veloci (il
//                plettro resta vivo); "low", "mid" e "high" dicono quanto lavora ogni banda
//   4. guadagno        - "auto gain": recupera quello che il compressore toglie a -10 dBFS; altrimenti "makeup" a mano
//   5. parallel        - mix tra asciutto e compresso (acceso / spento)
//   6. output
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"
#include "smp/PresetMemory.h"

namespace BCompIDs
{
    inline constexpr const char* style     = "style";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* autoGain  = "autogain";
    inline constexpr const char* knee      = "knee";
    inline constexpr const char* scHp      = "schp";
    inline constexpr const char* mixOn     = "mixOn";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* low       = "low";
    inline constexpr const char* mid       = "mid";
    inline constexpr const char* high      = "high";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* output    = "output";
}

class BCompProcessor final : public juce::AudioProcessor
{
public:
    BCompProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-comp"; }
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
    enum Style { clean, opto, fet, squash, multi };
    static constexpr int kStyles = 5;
    static float styleDefault (const char* id, int style);
    ui::PresetMemory styleMemory;   // ogni stile ricorda soglia, ratio, tempi e ginocchio finché il plugin resta aperto

    // gain computer (dB in -> dB di riduzione, <= 0): per il grafico e i test
    static float gainDb (float inDb, float thr, float ratio, float knee) noexcept;
    // offset di soglia e scala della ratio delle tre bande dello stile multi
    static constexpr float kBandThr[3] = { -3.0f, 0.0f, 3.0f };
    float autoMakeupDb() const;   // il guadagno che "auto gain" restituisce

    // per l'interfaccia
    std::atomic<float> grDb { 0.0f }, inDb { -100.0f }, outDb { -100.0f };
    std::array<std::atomic<float>, 3> bandGr {};
    static constexpr int kHistory = 512;           // ~2,7 s a 48 kHz (un punto ogni 256 campioni)
    struct Frame { float in = -100.0f, out = -100.0f, gr = 0.0f; };
    std::array<Frame, kHistory> history {};
    std::atomic<int> historyPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    struct Det { float env = 0.0f, gr = 0.0f, rms = 0.0f, mem = 0.0f; };
    float runDetector (Det&, float level, float thr, float ratio, float knee, float aC, float rC, int st) noexcept;

    std::atomic<float>* p[15] {};
    double fs = 48000.0;
    std::array<sp::Svf, 2> scHpF;
    // multi: Linkwitz-Riley 4 (due Butterworth in cascata) a 250 Hz e 2,5 kHz, per canale
    struct Band2 { sp::Svf a, b; };
    std::array<Band2, 2> lp1, hp1, lp2, hp2;
    std::array<Det, 3> bandDet {};
    Det det;
    float lastSc = -1.0f, optoRel = 0.0f, rmsC = 0.0f, peakC = 0.0f;
    juce::SmoothedValue<float> makeupSm { 1.0f }, outSm { 1.0f }, mixSm { 1.0f };
    int histCount = 0; float histIn = 0.0f, histOut = 0.0f, histGr = 0.0f;
};

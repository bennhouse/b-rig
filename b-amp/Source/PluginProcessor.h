#pragma once
// b-amp: testata di riferimento a 3 canali (clean ad alto headroom, crunch britannico, lead high gain moderno).
// Vedi DSP/AmpEngine.h per la catena. Il gate legge solo la DI e agisce in due punti (pre-gain e post-preamp).
// Il tuner (McLeod Pitch Method) lavora sulla DI filtrata sotto 500 Hz e decimata 8x, nell'interfaccia.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "DSP/AmpEngine.h"
#include "smp/PresetMemory.h"

namespace BAmpIDs
{
    inline constexpr const char* channel = "channel";
    inline constexpr const char* os = "os";
    inline constexpr const char* gate = "gate";
    inline constexpr const char* gateOn = "gate_on";
    inline constexpr const char* tight = "tight";
    inline constexpr const char* gain = "gain";
    inline constexpr const char* bass = "bass";
    inline constexpr const char* mid = "mid";
    inline constexpr const char* treble = "treble";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* resonance = "resonance";
    inline constexpr const char* master = "master";
    inline constexpr const char* level = "level";
    inline constexpr const char* boost = "boost";   // off / vol / vol + eq
}

class BAmpProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BAmpProcessor();
    ~BAmpProcessor() override { stopTimer(); }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-amp"; }
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
    enum Channel { Clean = 0, Crunch, Lead };
    static constexpr int kStages[3] = { 2, 4, 5 };
    // volume di ogni canale (come i "channel volume" di un ampli vero, prima del master): con gain a metà strada i tre
    // canali suonano forti quasi uguali; il clean satura il finale solo con il master alto
    static constexpr float kChannelTrim[3] = { 4.7f, 0.93f, 1.6f };

    // per l'interfaccia
    std::atomic<float> satEnv { 0.0f }, gateGain { 0.0f }, outRmsDb { -100.0f };
    // level auto: 3 s di misura solo con DI sopra -55 dBFS
    void toggleAuto() { autoCmd.store (isAutoListening() ? 2 : 1); }
    bool isAutoListening() const noexcept { return autoLeft.load() > 0 || autoCmd.load() == 1; }
    float autoRemaining() const noexcept { return (float) autoLeft.load() / (float) fs; }
    bool applyAutoIfReady();
    // tuner: DI < 500 Hz decimata 8x in un FIFO; mute mentre si accorda
    static constexpr int kTunerFifo = 1 << 14;
    juce::AbstractFifo tunerFifo { kTunerFifo };
    std::array<float, kTunerFifo> tunerBuf {};
    std::atomic<bool> tunerMute { false };
    double tunerRate() const noexcept { return fs / 8.0; }
    // tone stack per il grafico/test
    float toneStackDb (double hz) const;
    // ogni testata ricorda i suoi controlli (tutti tranne gate, boost e level) finché il plugin resta aperto
    ui::PresetMemory headMemory;
    static float headDefault (const char* id, int ch);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { applyAutoIfReady(); }
    void configureChannel (int ch, float tight) noexcept;
    void resetChannelState() noexcept;
    static float stageDrive (int ch, int k, float gain01) noexcept;

    std::atomic<float>* p[12] {};
    std::atomic<float>* pGateOn = nullptr, *pBoost = nullptr;
    double fs = 48000.0;
    int curChannel = -1, osFactor = 2, lastOs = -1; float lastTight = -1;
    juce::dsp::Oversampling<float> os2 { 1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::dsp::Oversampling<float> os4 { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::AudioBuffer<float> mono;
    // ingresso
    sp::Bq inHp, notch50, notch100, tunerLp, diLp;
    int tunerDecim = 0;
    // gate
    amp::GateDetector det;
    amp::Tpt1 hissLp, hissLp2; float hissCut = 12000;
public:
    float noiseFloorDb() const noexcept { return det.floorDb; }
private:
    // canale
    std::array<amp::Stage, 5> stages;
    amp::Tpt1 boostHp; sp::Bq boostMid, brightShelf;
    bool useBoost = false, useBright = false;
    amp::ToneStack tone; int toneCtl = 0; float lastB = -1, lastM = -1, lastT = -1, lastBright = -1;
    std::array<sp::Bq, 3> cleanEq; sp::Bq brightCap, tubeEq; float tubeHead = 1.0f;
    // finale e cassa
    amp::PowerClip pclip; float sagEnv = 0, sagA = 0, sagR = 0;
    sp::Bq resPeak, presShelf, safeLp; float lastRes = -1, lastPres = -1;
    // cambio canale: rampa 15 ms (giù, reset, su)
    float chanRamp = 1.0f, rampStep = 0; int pendingChannel = -1; bool rampHold = false;
    // livelli
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterSm { 1.0f }, levelSm { 1.0f };
    float autoG = 1.0f; double accIn = 0, accOut = 0; int accN = 0;
    std::atomic<int> autoCmd { 0 }, autoLeft { 0 }; std::atomic<bool> autoReady { false }; std::atomic<float> autoResult { 0.0f };
    float muteG = 1.0f;
    // boost davanti all'ampli: guadagno e quota della catena EQ + drive, entrambi con rampa (nessun click)
    juce::SmoothedValue<float> boostGainSm { 1.0f }, boostEqSm { 0.0f };
    // potenziometro del gain: come in un ampli vero, a 0 non passa nulla. Nel primo 10 % della corsa il segnale
    // sale da zero (curva a S); da 10 in su resta il guadagno pieno e il gain lavora sugli stadi.
    juce::SmoothedValue<float> potSm { 1.0f };
    static float potFade (float gain01) noexcept { const float t = juce::jlimit (0.0f, 1.0f, gain01 / 0.1f); return t * t * (3.0f - 2.0f * t); }
    amp::Tpt1 boostLowCut; sp::Bq boostArt, boostFizz; amp::Shaper boostShaper; float boostPrev = 0;
    // cathode follower prima del tone stack (crunch, lead) e finale push-pull con trasformatore d'uscita
    amp::DcBlock cfDc, paDc; amp::Tpt1 otLow; sp::Bq voiceEq; float paAsym = 0.06f;
};

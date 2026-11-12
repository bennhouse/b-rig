#pragma once
// b-scenery: scenari acustici procedurali (nessun file WAV, RAM < 1 MB) che respirano con la chitarra.
//   - vinyl : crackle di Poisson (~40/s) con ampiezze a legge di potenza (tanti micro-click, pochi forti), ciascuno
//             un impulso nel passa-alto 1,2 kHz + risonanza a 3,5 kHz con pan casuale; pop rari (0,4/s) col corpo
//             sotto i 600 Hz; soffio di superficie 1-9 kHz stereo modulato dalla rotazione (33 1/3 giri = 0,56 Hz);
//             rumble del giradischi sotto i 30 Hz
//   - hum   : ronzio d'ampli realistico: fondamentale di rete debole, ripple del raddrizzatore (|sin|: 100/120 Hz e
//             armoniche pari, il "buzz"), un po' di dispari dal trasformatore (sin^3), ampiezza che ondeggia; soffio
//             dell'ampli 2-9 kHz. Oscillatori in quadratura: nessuna funzione trigonometrica per campione
//   - rain  : tappeto rosa stereo decorrelato (200 Hz - 7 kHz) con raffiche lente + ~260 gocce/s su tre risonanze
//             fisse (1,8 / 3,2 / 5,5 kHz, decadimento 1,5 ms, pan casuale) e gocce grosse rare a 900 Hz
//   - lo-fi : processa tutto il bus (chitarra + texture): decimatore, wow & flutter, banda telefonica morbida
// Ogni layer spento o a 0 % non costa nulla (il suo blocco DSP viene saltato).
// Le texture suonano solo con il transport in play o con la chitarra sopra -65 dBFS; altrimenti si spengono in 35 ms.
// react: le texture si abbassano mentre suoni (inviluppo RMS 4/220 ms). balance: 2,5 s di ascolto e il livello
// delle texture va a -16 dB sotto la chitarra.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"

namespace BSceneryIDs
{
    inline const char* const layer[4] = { "vinyl", "hum", "rain", "lofi" };
    inline juce::String on (int l) { return juce::String (layer[l]) + "_on"; }
    inline juce::String level (int l) { return juce::String (layer[l]) + "_level"; }
    inline constexpr const char* mix = "texture_gain";
    inline constexpr const char* react = "react";
    inline constexpr const char* mains = "mains";
}

class BSceneryProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BSceneryProcessor();
    ~BSceneryProcessor() override { stopTimer(); }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-scenery"; }
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
    // balance: avvia / annulla l'ascolto; durante l'ascolto il tempo che resta
    void toggleBalance() { balanceCmd.store (isListening() ? 2 : 1); }
    bool isListening() const noexcept { return listenLeft.load() > 0 || balanceCmd.load() == 1; }
    float listenRemaining() const noexcept { return (float) listenLeft.load() / (float) fs; }
    bool applyBalanceIfReady();     // message thread (timer), pubblico per i test
    // grafico: storia (un fotogramma ogni 512 campioni, ~5,5 s): chitarra, texture potenziale (senza react) e texture reale
    struct Frame { float guitar = 0, texPre = 0, tex = 0; };
    static constexpr int kHistory = 1024, kHop = 512;
    std::array<Frame, kHistory> history {};
    std::atomic<int> historyPos { 0 };
    std::array<std::atomic<float>, 4> layerLevel {};   // picco di ogni layer nell'ultimo blocco (dopo gain e react)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { applyBalanceIfReady(); }
    inline float rnd() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s * (1.0f / 4294967296.0f) * 2.0f - 1.0f; }   // xorshift32
    struct Pink { float b0 = 0, b1 = 0, b2 = 0; float next (float w) { b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f; return (b0 + b1 + b2 + w * 0.1848f) * 0.25f; } };

    std::atomic<float>* pOn[4] {}; std::atomic<float>* pLvl[4] {};
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pReact = nullptr; std::atomic<float>* pMains = nullptr;
    double fs = 48000.0;
    std::uint32_t s = 0x9e3779b9u;
    inline float u01() noexcept { return rnd() * 0.5f + 0.5f; }
    // vinyl
    sp::Bq clickHp, clickBp, popLp, hissHpL, hissHpR, hissLpL, hissLpR, rumbleLp; Pink pinkVL, pinkVR;
    float crackL = 0, crackR = 0, popImp = 0, rotS = 0, rotC = 1, rotCs = 1, rotSn = 0;
    // hum
    float hs = 0, hc = 1, hRotC = 1, hRotS = 0, wobS = 0, wobC = 1, wobRC = 1, wobRS = 0; sp::Bq humBuzzLp, humHissHp, humHissLp;
    // rain
    Pink pinkL, pinkR; sp::Bq washHpL, washHpR, washLpL, washLpR; float gust = 0.7f, gustT = 0.7f;
    std::array<sp::Bq, 4> dropBank; std::array<float, 4> dropEnv {}, dropPan {}, dropDecay {};    // lo-fi
    std::array<std::array<float, 4096>, 2> lofiBuf {}; int lofiW = 0; float holdL = 0, holdR = 0, srPh = 0, jitter = 0;
    float wowS = 0, wowC = 1, wowRC = 1, wowRS = 0;
    std::array<sp::Bq, 2> lofiHp, lofiLp;
    // dinamica
    float envG = 0, eA = 0, eR = 0, active = 0, fadeC = 0, texGain = 0.1f, gainC = 0, duckLast = 1;
    // balance
    std::atomic<int> balanceCmd { 0 }, listenLeft { 0 };
    std::atomic<bool> balanceReady { false };
    double accG = 0, accT = 0; int accN = 0; std::atomic<float> balanceResult { 0.0f };
    int histCount = 0; float histG = 0, histT = 0, histP = 0;
    std::array<float, 4> layerPk {};
};

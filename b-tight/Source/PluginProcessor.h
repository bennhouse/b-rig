#pragma once
// b-tight: da mettere sulla DI della chitarra PRIMA dell'ampli/distorsore. Scolpisce i palm-mute
// e mette in evidenza l'attacco della plettrata. Latenza zero, 4 stadi in serie:
//   1. mud cut     - passa-alto Butterworth 12 dB/ott: standard 80 Hz, drop 62 Hz, baritone 45 Hz
//                    (tuning spento di default: il plugin parte senza fare nulla; scegliendo un tuning
//                    si carica il suo preset di clamp, bite e push)
//   2. chug clamp  - crossover Linkwitz-Riley 24 dB/ott (220 Hz, 190 Hz in drop, 160 Hz in baritone);
//                    compressore veloce solo sulle basse (RMS, attacco 3 ms, rilascio 65 ms, 4:1, max 12 dB).
//                    "auto": il make-up segue la riduzione media (1 s) e restituisce alle basse il volume tolto
//   3. pick attack - il rilevatore ascolta il "clic" della plettrata (passa-alto 1 kHz), uguale con ogni
//                    tuning: inviluppo veloce (0,5/15 ms) contro lento (15/60 ms). Durante l'attacco la banda
//                    alta sale (bite > 0) o scende (bite < 0), max 9 dB, con un guadagno levigato (0,2/8 ms)
//                    che non crea click né distorsione
//   4. focus tilt  - campana a 1,4 kHz (Q 0,9) da 0 a +12 dB ("push"), uscita compensata di -push/2
//   5. uscita      - "gain" (-24...+24 dB) dopo l'effetto; "autogain": l'RMS di uscita insegue quello della DI in
//                    ingresso (finestre di ~1 s, solo mentre suoni) e il gain manuale si ferma
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"

namespace BTightIDs
{
    inline constexpr const char* tuning = "tuning";
    inline constexpr const char* clamp  = "clamp";
    inline constexpr const char* autoMu = "auto";
    inline constexpr const char* bite   = "bite";
    inline constexpr const char* push   = "push";
    inline constexpr const char* gain   = "gain";
    inline constexpr const char* autoGain = "autogain";
}

class BTightProcessor final : public juce::AudioProcessor
{
public:
    BTightProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-tight"; }
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
    static constexpr float kTuningHz[4] = { 0.0f, 80.0f, 62.0f, 45.0f };   // 0 = off
    static constexpr float kSplitHz[4] = { 220.0f, 220.0f, 190.0f, 160.0f };
    struct Preset { float clamp, bite, push; };
    static constexpr Preset kPresets[4] = { { 0.0f, 0.0f, 0.0f }, { -24.0f, 25.0f, 3.0f }, { -26.0f, 30.0f, 4.0f }, { -28.0f, 35.0f, 4.5f } };
    void applyTuningPreset (int tuning);   // message thread: chiamato quando l'utente sceglie un tuning

    // Storia per il grafico scorrevole: un fotogramma ogni 256 campioni.
    struct Frame { float peak = 0, clampDb = 0, attackDb = 0; };
    static constexpr int kHistory = 1024;
    std::array<Frame, kHistory> history {};
    std::atomic<int> historyPos { 0 };
    std::atomic<float> appliedGainDb { 0.0f };   // guadagno d'uscita in uso (manuale o autogain), per l'interfaccia

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* pTuning = nullptr; std::atomic<float>* pClamp = nullptr; std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pBite = nullptr; std::atomic<float>* pPush = nullptr;
    std::atomic<float>* pGain = nullptr; std::atomic<float>* pAutoGain = nullptr;
    void outputStage (juce::AudioBuffer<float>&, int numCh, double inPow) noexcept;
    float outG = 1.0f, autoG = 1.0f; double eIn = 0, eOut = 0;
    double fs = 48000.0;
    struct Ch { sp::Bq hp, lp1, lp2, hp1, hp2, focus; };
    std::array<Ch, 2> ch {};
    float envLow = 0, eFast = 0, eSlow = 0, lowGain = 1, clampAtk = 0, clampRel = 0;
    float fAtk = 0, fRel = 0, sAtk = 0, sRel = 0, gainSmooth = 0;
    float avgGrDb = 0, autoCoef = 0, tSm = 0, tUp = 0, tDown = 0;
    sp::Bq pickHp1, pickHp2;
    int lastTuning = -1; float lastPush = -100;
    Frame acc; int accN = 0;
};

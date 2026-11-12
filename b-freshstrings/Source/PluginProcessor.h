#pragma once
// b-freshstrings: sulla DI pulita, subito dopo b-focus. Corde nuove, plettro e pickup virtuali, latenza zero,
// solo filtri nel dominio del tempo (coefficienti aggiornati ogni 32 campioni):
//   1. fresh strings  - la banda sopra 2,2 kHz passa da una saturazione morbida (armoniche pari e dispari, poi
//                       ripulite sopra 3 kHz) e da uno scaffale a 3,2 kHz che si apre solo sull'attacco della
//                       plettrata e si richiude sul decadimento: il rumore di fondo della DI non sale
//   2. pick           - nei primi 0,5-4 ms della plettrata: nylon ammorbidisce sopra 4,5 kHz, tortex spinge
//                       1,8 kHz, carbon 3,4 kHz e stringe le basse sotto 180 Hz, metal aggiunge un picco
//                       risonante a 5,2 kHz per 3 ms. "attack" dosa la spinta
//   3. gauge          - 4 passa-tutto del 1° ordine (dispersione di fase delle corde più grosse, timbro
//                       invariato) e uno scaffale a 110 Hz: asciuga le corde sottili quando le basse suonano
//                       forte, dà peso alle mute grosse. "tension" ne dosa la quantità
//   4. pickup         - risonanza del pickup: bridge 3,4 kHz (Q 2,2), both 2,6 kHz, neck 1,9 kHz (Q 1,4) con
//                       un pettine di 0,45 ms che scava 1,1 kHz; sopra, il roll-off del pickup
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"

namespace BFreshIDs
{
    inline constexpr const char* pick      = "pick";
    inline constexpr const char* gauge     = "gauge";
    inline constexpr const char* pickup    = "pickup";
    inline constexpr const char* freshness = "freshness";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* tension   = "tension";
    inline constexpr const char* output    = "output";
}

class BFreshstringsProcessor final : public juce::AudioProcessor
{
public:
    BFreshstringsProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-freshstrings"; }
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

    // valori per pickup (bridge, both, neck) e gauge (.009, .010, .011, heavy): usati anche dal grafico
    // pickup: più vicino al ponte = la corda si muove meno lì, meno fondamentale e risonanza più alta; vicino al
    // manico = più fondamentale, risonanza più bassa e il pettine della posizione che scava le medie
    struct Pickup { float hz, q, db, combGain, rollHz, lowDb; };
    static constexpr Pickup kPickups[3] = { { 3400.0f, 2.2f, 3.0f, 0.0f, 12000.0f, -2.5f }, { 2600.0f, 1.8f, 2.5f, 0.15f, 10000.0f, 0.0f },
                                            { 1900.0f, 1.4f, 2.0f, 0.35f, 8000.0f, 2.0f } };
    // gauge: una corda più grossa ha più tensione e più rigidità (più inarmonicità, alte che si spengono prima) e più
    // fondamentale; una sottile ha meno tensione, oscilla di più sui bassi ("flubby") ed è più brillante
    static constexpr float kGaugeShelfDb[4] = { -2.5f, -1.0f, 1.5f, 3.0f };   // 110 Hz
    static constexpr float kGaugeHighDb[4] = { 1.0f, 0.0f, -0.8f, -1.5f };    // 4 kHz
    static constexpr float kGaugeApHz[4] = { 6500.0f, 5000.0f, 3800.0f, 2600.0f };
    static constexpr float kCombMs = 0.45f;

    // per l'interfaccia: livello d'ingresso, brillantezza aggiunta in quel momento (0..1), attacco del plettro (0..1)
    std::atomic<float> levelNow { 0.0f }, freshNow { 0.0f }, pickNow { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateControls (bool snap) noexcept;
    std::atomic<float>* pPick = nullptr; std::atomic<float>* pGauge = nullptr; std::atomic<float>* pPickup = nullptr;
    std::atomic<float>* pFresh = nullptr; std::atomic<float>* pAttack = nullptr; std::atomic<float>* pTension = nullptr;
    std::atomic<float>* pOut = nullptr;

    double fs = 48000.0;
    // rilevatore della plettrata
    sp::Svf detHp;
    float eFast = 0, eSlow = 0, fAtk = 0, fRel = 0, sAtk = 0, sRel = 0, trSm = 0, trRel = 0, pickEnv = 0, pickDecay = 0;
    // 1. fresh
    sp::Svf hf22, exHp, shelf32;
    // 2. pick
    sp::Svf pickBand, lowBand;
    float pickBandGain = 0.0f, pickLowGain = 0.0f;
    bool pickIsHp = false;
    // 3. gauge
    std::array<sp::Allpass1, 4> ap {};
    sp::Svf lowShelf, lowDet;
    float shelfM1 = 0.0f, shelfM2 = 0.0f, envLow = 0.0f, lowAtk = 0, lowRel = 0;
    // 4. pickup (valori correnti che inseguono quelli scelti)
    sp::Svf peak, roll;
    // scaffale esatto su stato-variabile (Cytomic): basso o alto
    struct Shelf
    {
        sp::Svf f; float m0 = 1, m1 = 0, m2 = 0; bool low = true;
        void set (double rate, double hz, float db, bool isLow) noexcept
        {
            low = isLow;
            const float A = std::pow (10.0f, db / 40.0f);
            const float t = (float) std::tan (3.14159265358979 * hz / rate);
            f.g = isLow ? t / std::sqrt (A) : t * std::sqrt (A);
            f.k = 1.41421356f;
            f.a1 = 1.0f / (1.0f + f.g * (f.g + f.k)); f.a2 = f.g * f.a1; f.a3 = f.g * f.a2;
            if (isLow) { m0 = 1.0f; m1 = f.k * (A - 1.0f); m2 = A * A - 1.0f; }
            else       { m0 = A * A; m1 = f.k * (1.0f - A) * A; m2 = 1.0f - A * A; }
        }
        float process (float x) noexcept { f.tick (x); return m0 * x + m1 * f.bp + m2 * f.lp; }
    };
    Shelf gaugeHigh, pickupLow;
    float curLowDb = 0.0f;
    float curHz = 3400.0f, curQ = 2.2f, curDb = 3.0f, curComb = 0.0f, curRoll = 9000.0f, peakG = 0.0f;
    static constexpr int kComb = 64;
    std::array<float, kComb> comb {};
    int combPos = 0;
    float combDelay = 21.6f;
    float outG = 1.0f, outTarget = 1.0f, fresh = 0.5f;
    int ctl = 0;
};

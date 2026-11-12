#pragma once
// b-double: da una chitarra mono una doppia stereo realistica, senza cancellazioni in mono.
//   1. ritardo     - manuale di default ("delay", 5-40 ms, fisso). Con "dynamic" acceso diventa umanizzato:
//                    a ogni plettrata un nuovo ritardo casuale attorno a "delay" (jitter ±0,5 ms con tightness
//                    100 %, ±8 ms con 0 %) e una nuova intonazione attorno a "detune".
//                    Il ritardo NON scivola mai (uno scivolamento di millisecondi è un glissato udibile, il
//                    "burst" agli attacchi): si passa dalla presa vecchia alla nuova con un crossfade coseno di
//                    40 ms tra due prese fisse.
//   2. doubler     - linea di ritardo (potenza di 2, indici a maschera), interpolazione di Hermite (4 punti),
//                    LFO lento (0,35 Hz, profondità secondo tightness) e pitch shifter a due teste con finestre
//                    sin^2 complementari (somma costante: nessuna discontinuità) per i cents di "detune"
//   3. variation   - un'altra chitarra: tilt ±4,5 dB attorno a 1,2 kHz (verso opposto sulle due doppie) e due
//                    passa-tutto a 900 / 2600 Hz mescolati al 50 % (timbro diverso, non solo volume)
//   4. mono-safe   - passa-tutto (180/650/1900/4200 Hz) nel canale generato, passa-alto fino a 300 Hz e fino a
//                    -6 dB nei passaggi tenuti senza attacchi. Si alza ascoltando la DAW in mono.
//   5. phase guard - solo in dynamic: 30 ms dopo ogni plettrata si sceglie, entro ±1,5 ms, la presa in cui la nota
//                    è meno correlata con sé stessa; poi, ogni ~43 ms, si misura la correlazione dry/doppia nella
//                    banda 80-600 Hz (dove stanno le fondamentali): se va in controfase (< -0,3) un passa-tutto
//                    accordato sulla frequenza dominante (stimata dagli zero-crossing) ruota la fase della doppia di
//                    90° lì, finché la correlazione torna positiva.
// Modi: hard-pan (L = dry ritardato di 2 ms, R = doppia) e center-wide (dry al centro, due doppie a L e R).
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BDoubleIDs
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* tightness = "tightness";
    inline constexpr const char* variation = "variation";
    inline constexpr const char* monoSafe  = "mono_safe";
    inline constexpr const char* active    = "active";
    inline constexpr const char* delay     = "delay";
    inline constexpr const char* detune    = "detune";
    inline constexpr const char* dynamic   = "dynamic";
}

class BDoubleProcessor final : public juce::AudioProcessor
{
public:
    BDoubleProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-double"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.06; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Per l'interfaccia: ritardo e intonazione attuali della doppia (canale R), punti per il goniometro.
    std::atomic<float> shownDelayMs { 0.0f }, shownCents { 0.0f };
    std::atomic<bool> guardActive { false };
    static constexpr int kScope = 2048;
    std::array<std::array<float, 2>, kScope> scope {};
    std::atomic<int> scopePos { 0 };
    void setSeed (int s) { rng.setSeed (s); }   // test
    bool isIdle() const noexcept { return idle.load(); }   // nessun audio da 0,5 s: indicatori a riposo
    std::atomic<bool> idle { true };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    struct Voice
    {
        float curDelay = 768, newDelay = 768;   // prese (campioni): fisse; il cambio è un crossfade
        float xf = 1.0f;                        // 1 = nessun crossfade in corso
        float cents = 0, tCents = 0, phase = 0, side = 1;
        sp::Allpass1 ap[4], varAp[2], guardAp;
        sp::Bq hp;
        float tiltLp = 0, guardMix = 0, gLo = 1, gHi = 1;
    };
    float heads (const Voice& v, float baseSamples) const noexcept;   // pitch shifter attorno a una presa
    float render (Voice& v) noexcept;
    void moveTo (Voice& v, float delaySamples) noexcept;
    float leastCorrelatedDelay (float targetSamples, float offsetSamples) const;
    float post (Voice& v, float y, float ms, float var) noexcept;

    std::atomic<float>* pMode = nullptr; std::atomic<float>* pTight = nullptr; std::atomic<float>* pVar = nullptr;
    std::atomic<float>* pSafe = nullptr; std::atomic<float>* pActive = nullptr; std::atomic<float>* pDelay = nullptr;
    std::atomic<float>* pDetune = nullptr; std::atomic<float>* pDynamic = nullptr;
    int alignIn = -1;                 // campioni che mancano all'allineamento (-1 = nessuno in attesa)
    std::array<float, 2> pendingDelay {};
    float wetMix = 1.0f, wetCoef = 0.0f, xfInc = 0.0f;
    double fs = 48000.0;
    std::vector<float> line;
    int mask = 0, pos = 0, dryDelay = 96, holdoff = 0, sinceAttack = 0, scopeDecim = 0;
    float eFast = 0, eSlow = 0, fA = 0, fR = 0, sA = 0, sR = 0, centsSmooth = 0, lfoS = 0, lfoC = 1, rotC = 1, rotS = 0;
    float windowSamples = 960, tiltCoef = 0, sustainGain = 1, sustainCoef = 0, lastSafe = -1, lastManual = -1;
    bool lastDynamic = false;
    // phase guard: correlazione dry / doppia nella banda 80-600 Hz, a finestre
    sp::Bq gHpD, gLpD, gHpW, gLpW;
    double gDW = 0, gDD = 0, gWW = 0; int gCount = 0, gCross = 0; float gPrevD = 0;
    std::array<Voice, 2> voices {};
    juce::Random rng { 12345 };
};

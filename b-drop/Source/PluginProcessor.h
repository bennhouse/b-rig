#pragma once
// b-drop: traspositore di accordatura (-12...+12 semitoni) da mettere per primo sulla DI della chitarra.
// Nel dominio del tempo, senza FFT:
//   1. pitch shifter a due testine     - linea di ritardo circolare letta da due testine che ruotano su un grano
//                                        di 14 ms, incrociate con finestre di Hann (somma costante). Il ritardo
//                                        medio è ~8 ms
//   2. aggancio di fase                - quando una testina ricomincia il grano, cerca entro un periodo della nota più
//                                        bassa (640 campioni a 48 kHz, passo 2) il punto più simile all'altra testina
//                                        (correlazione normalizzata): intonazione esatta e niente tremolio sugli accordi
//   3. zero-lag pick                   - un rilevatore veloce (0,1/6 ms) sulle alte sopra 3,5 kHz (il clic del
//                                        plettro, senza nota) apre per 4 ms un passaggio diretto, a latenza zero,
//                                        di quella banda: il colpo di plettro si sente subito, la nota trasposta
//                                        arriva ~8 ms dopo
//   4. tension                         - scendendo di tono: passa-alto da 45 a 95 Hz, taglio a 165 Hz (Q 1,4) e
//                                        un po' di presenza a 3 kHz, in proporzione ai semitoni scesi
//   5. mix                             - asciutto (senza ritardo) e trasposto; on/off con dissolvenza di 10 ms
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BDropIDs
{
    inline constexpr const char* on        = "on";
    inline constexpr const char* range     = "range";
    inline constexpr const char* zeroLag   = "zerolag";
    inline constexpr const char* semitones = "semitones";
    inline constexpr const char* fine      = "fine";
    inline constexpr const char* tension   = "tension";
    inline constexpr const char* mix       = "mix";
}

class BDropProcessor final : public juce::AudioProcessor
{
public:
    BDropProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-drop"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.02; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // range: shift (da 0 in su o in giù, -12...+12), octave (fisso a -12), whammy (tutto, con glide)
    enum Range { shift, octave, whammy };
    static void rangeLimits (int range, int& lo, int& hi) noexcept;
    void applyRange (int range);   // message thread: porta i semitoni dentro il range scelto

    // Per l'interfaccia: ultimi campioni d'ingresso e d'uscita, qualità dell'aggancio (0..1)
    static constexpr int kScope = 4096;
    std::array<float, kScope> scopeIn {}, scopeOut {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> lockQuality { 0.0f };
    std::atomic<bool> pickFlash { false };


private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* pOn = nullptr; std::atomic<float>* pRange = nullptr; std::atomic<float>* pZero = nullptr;
    std::atomic<float>* pSemi = nullptr; std::atomic<float>* pFine = nullptr; std::atomic<float>* pTension = nullptr;
    std::atomic<float>* pMix = nullptr;

    float readAt (float delay) const noexcept;
    float findSplice (float baseDelay, float otherDelay) noexcept;

    double fs = 48000.0;
    static constexpr int kBuf = 8192;
    std::vector<float> buf = std::vector<float> (kBuf, 0.0f);
    int w = 0;
    float grain = 672.0f, dMin = 68.0f;
    int search = 640;
    std::array<float, 1026> corr {};
    struct Head { float d = 0.0f; float off = 0.0f; };   // ritardo attuale e spostamento dell'aggancio
    std::array<Head, 2> heads {};
    float ratioSm = 1.0f, glide = 0.0f, idSm = 1.0f, onGain = 1.0f, onCoef = 0.0f, mixSm = 1.0f;

    // zero-lag pick
    sp::Svf pickHp1, pickHp2;
    float eFast = 0, eSlow = 0, fAtk = 0, fRel = 0, sAtk = 0, sRel = 0;
    int pickLeft = 0, pickLen = 192, pickHold = 0;
    float pickGain = 0.0f;

    // tension (solo sul segnale trasposto)
    sp::Svf tHp, tMud, tAir;
    float lastT = -1.0f, mudG = 1.0f, airG = 0.0f;
    bool tHpOn = false;
};

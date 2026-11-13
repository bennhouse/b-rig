#pragma once
// b-drop: traspositore di accordatura (-12...+12 semitoni) da mettere per primo sulla DI della chitarra.
// Polifonico e stabile: nessun inseguimento dell'altezza. Il rapporto è una costante, 2^((semitoni + cent/100) / 12).
//   1. pre-filtro                      - passa-alto di Butterworth del 2° ordine a 52 Hz sull'ingresso dello shifter: il
//                                        rumore meccanico sotto le corde non viene trasposto in basso
//   2. phase vocoder a picchi bloccati - FFT di 1024 punti (a 44,1/48 kHz; 2048 a 88,2/96 kHz), salto di sintesi 256 (4
//      (Laroche e Dolson)                volte di sovrapposizione), finestra di Hann in analisi e sintesi. Allunga o accorcia
//                                        il tempo del rapporto (salto d'analisi = 256 / rapporto) e un ricampionatore
//                                        (Hermite) lo rilegge alla velocità del rapporto: la durata torna quella di prima e
//                                        l'altezza cambia esatta, a qualunque frequenza (spostare i bin dello spettro, con
//                                        bin da 47 Hz, sbaglierebbe le note basse). Per ogni frame: modulo e frequenza
//                                        istantanea di ogni bin (avanzamento di fase srotolato), i picchi locali e la regione
//                                        di ogni picco (i bin più vicini a lui). Solo i picchi avanzano la fase di sintesi;
//                                        ogni bin della regione tiene con il suo picco la relazione di fase che aveva in
//                                        ingresso (identity phase locking): ogni armonica di ogni corda si muove come un
//                                        corpo rigido, niente tremolio sugli accordi
//   3. attacco                         - un rilevatore di transienti (inviluppo veloce contro lento della DI) a ogni nuova
//                                        plettrata riallinea tutte le fasi di sintesi a quelle d'ingresso (phase reset), e
//                                        "zero-lag pick" mescola per 5 ms, a latenza zero, la parte sopra 2,8 kHz della DI:
//                                        il colpo di plettro si sente subito, la nota trasposta arriva un frame dopo
//                                        (circa 24 ms a 48 kHz)
//   4. tension                         - scendendo di tono, in proporzione ai semitoni scesi e a "tension": un taglio a
//                                        160 Hz (fino a -4,5 dB a -12 st) asciuga il fango e uno scaffale a 3,2 kHz (fino a
//                                        +2 dB) ridà la brillantezza persa, come una baritona con le corde tese
//   5. mix                             - asciutto (senza ritardo) e trasposto; on/off con dissolvenza di 10 ms; a 0 semitoni
//                                        l'ingresso passa identico
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include <juce_dsp/juce_dsp.h>
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

    void processFrame (juce::int64 start, int hopIn) noexcept;

    double fs = 48000.0;
    // phase vocoder
    int N = 1024, H = 256, order = 10;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inRing, frame, outAcc, prevPhase, synthPhase, mag, phase, omega, fifo;
    std::vector<int> peaks;
    juce::int64 inCount = 0, lastStart = -1, fifoW = 0;
    double anaPos = 0.0, readPos = 0.0;
    bool reading = false;
    static constexpr int kFifo = 1 << 14;
    float olaScale = 1.0f;
    bool resetPending = true;
    sp::Svf preHp;
    float ratioSm = 1.0f, glide = 0.0f, idSm = 1.0f, onGain = 1.0f, onCoef = 0.0f, mixSm = 1.0f;

    // attacco: transienti (phase reset) e zero-lag pick
    sp::Svf pickHp1, pickHp2;
    float eFast = 0, eSlow = 0, fAtk = 0, fRel = 0, sAtk = 0, sRel = 0;
    float tFast = 0, tSlow = 0;
    int pickLeft = 0, pickLen = 240, pickHold = 0, onsetHold = 0;
    float pickGain = 0.0f;

    // tension (solo sul segnale trasposto)
    sp::Svf tMud, tAir;
    float lastT = -1.0f, mudG = 1.0f, airG = 0.0f;
    bool tOn = false;
};

#pragma once
// b-distort: distorsione con cinque tipi ("colori"), tone (bilanciamento basse/alte), hicut sul segnale
// distorto, low cut, mix e uscita.
// La parte non lineare gira in oversampling 4x (meno aliasing); ogni tipo ha i suoi
// filtri prima e dopo il clipping, come nei circuiti a cui si ispira.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "smp/Spectrum.h"
#include "smp/PresetMemory.h"
#include <array>
#include <cmath>
#include <vector>

namespace BDistortIDs
{
    inline constexpr const char* type   = "type";
    inline constexpr const char* drive  = "drive";
    inline constexpr const char* tone   = "tone";     // (id storico) hicut: passa-basso sul distorto
    inline constexpr const char* tilt   = "tilt";     // tone: bilanciamento basse / alte, come il tono di un overdrive verde
    inline constexpr const char* mix    = "mix";
    inline constexpr const char* output = "output";
    inline constexpr const char* locut  = "locut";
}

class BDistortProcessor final : public juce::AudioProcessor
{
public:
    // Tipi di distorsione (cinque caratteri distinti, dal più morbido al più estremo):
    //   tube  - due stadi a valvola in cascata (triodi polarizzati, accoppiamento e Miller):
    //           armoniche pari e dispari, caldo, si apre col tocco
    //   drive - overdrive "verde" a gobba sulle medie: distorce solo sopra ~720 Hz e somma il segnale
    //           pulito, quindi bassi stretti e medie in avanti; taglio delle alte a 5 kHz
    //   rat   - distorsione a op-amp: con guadagno che sale sopra 1,5 kHz e banda che si stringe
    //           col guadagno, diodi al silicio verso massa (clipping duro)
    //   fuzz  - due stadi a transistor saturati e asimmetrici (quasi un'onda quadra) e tone stack
    //           con le medie scavate (-6 dB a ~1 kHz): muro di fuzz pieno di bassi e di alte
    //   fold  - wavefolder: oltre la soglia l'onda si ripiega; timbro metallico/sintetico
    enum Type { Tube = 0, Drive, Rat, Fuzz, Fold, NumTypes };
    static constexpr int kStateVersion = 2;   // 1 = sei tipi (soft, tube, hard, rat, fuzz, fold)

    struct Voicing
    {
        float preHpHz;     // passa-alto prima del clipping (0 = nessuno)
        float emphHz;      // drive: frequenza sopra cui si distorce; rat: sopra cui sale il guadagno
        float postLpHz;    // passa-basso dopo il clipping (0 = nessuno)
        float gainMul;     // guadagno interno del circuito rispetto al drive
    };
    static const Voicing& voicing (int type);

    // Allineamento di volume (dB) per tipo e drive, misurato (vedi Tests: --calibrate):
    // a ogni drive tutti i tipi suonano forti quanto il segnale d'ingresso.
    static float trimDb (int type, float driveDb) noexcept;
    static inline bool calibrating = false;   // solo per la misura: nessun allineamento

    BDistortProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-distort"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Memoria per tipo (solo sessione): drive e tono che si erano scelti per ciascun tipo.
    ui::PresetMemory typeMemory;

    // tone: da 0 (scuro) a 100 (brillante), 50 = piatto. fino a 12 dB di differenza tra basse e alte, perno a 800 Hz
    static constexpr float kTiltHz = 800.0f, kTiltDb = 12.0f;   // ±6 dB per lato
    static void tiltGains (float tone, float& low, float& high) noexcept
    { const float t = (tone - 50.0f) / 50.0f; low = std::pow (10.0f, -t * kTiltDb / 40.0f); high = std::pow (10.0f, t * kTiltDb / 40.0f); }
    // Risposta in frequenza dei filtri lineari in uscita (low cut su tutto, tone e hicut sul distorto,
    // dosati dal mix), in dB: per il grafico.
    float responseDb (double hz) const;

    // Analizzatore di spettro: ingresso e uscita, copiati solo mentre l'interfaccia è aperta.
    ui::SpectrumTap inTap, outTap;
    std::atomic<bool> spectrumActive { false };

    // Picco dell'ingresso (inviluppo, ~150 ms di rilascio): il punto luminoso che scorre sulla curva.
    std::atomic<float> inputPeak { 0.0f };

    // Curva statica (senza filtri) del tipo selezionato, per il grafico: x in [-1, 1].
    float transfer (float x) const;   // solo per l'interfaccia (cache non thread-safe)
    mutable int curveType = -1; mutable float curveDrive = -1.0f, curvePeak = 1.0f;
    // Curva statica di un tipo con guadagno lineare d (stessa usata dal motore audio).
    static float shape (int type, float x, float d) noexcept;

private:
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct ChannelState
    {
        float preLp = 0;              // passa-alto d'ingresso (come x - passa-basso)
        float emphLp = 0;             // drive/rat: parte bassa del segnale
        float gbw = 0;                // rat: banda dell'op-amp
        float stage = 0;              // tube: Miller del 1° stadio; fuzz: alte del 1° stadio
        float couple = 0;             // tube/fuzz: accoppiamento tra gli stadi (passa-alto)
        float scoopLp = 0, scoopHp = 0; // fuzz: tone stack
        float post = 0;               // passa-basso dopo il clipping
        float dcX = 0, dcY = 0;       // filtro anti-DC
        float tone = 0;               // hicut (a frequenza base)
        float tilt = 0;               // tone: metà bassa del bilanciamento (passa-basso a 800 Hz)
        float lcS1 = 0, lcS2 = 0;     // low cut (SVF a 12 dB/ott) in uscita
    };

    template <int T> void runNonLinear (float* x, int numOs, int ch) noexcept;

    std::atomic<float>* pType = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pTilt = nullptr;
    float sLowG = 1.0f, sHighG = 1.0f;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pLocut = nullptr;
    float lcG = 0, lcK = 0, lcA1 = 0, lcA2 = 0, lcA3 = 0, lcHz = -1;   // coefficienti del low cut

    static constexpr int kOsLog2 = 2;                     // oversampling 4x
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osLatency = 0;

    double sr = 44100.0, srOs = 176400.0;
    int maxBlock = 512;
    std::array<ChannelState, 2> st {};
    std::vector<float> driveArr;                          // drive (lineare) per campione, a frequenza base
    juce::AudioBuffer<float> dryLine;                     // dry ritardato quanto l'oversampling (fase allineata nel mix)
    int dryPos = 0;
    float sDrive = 1.0f, sMix = 1.0f, sOut = 1.0f, sTrim = 1.0f, smoothCoef = 0.0f;
    float cPreHp = 0, cEmph = 0, cPost = 0, cDc = 0;      // coefficienti alla frequenza di oversampling
    float cStage = 0, cCouple = 0, cScoopLp = 0, cScoopHp = 0;
    int coeffType = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BDistortProcessor)
};

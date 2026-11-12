#pragma once
// b-blend: mixer vettoriale di 4 risposte all'impulso (cabinet) con allineamento di fase automatico
// e risonanza dinamica del cono.
//   1. allineamento   - al caricamento ogni IR è allineato all'IR di riferimento (A, o il primo caricato)
//                       con la cross-correlazione via FFT e uno spostamento sub-campione (rampa di fase)
//   2. mixer XY       - pesi a potenza costante dai 4 angoli; si calcola UN solo IR composito (su un
//                       thread a parte) e si fa una sola convoluzione a latenza zero
//   3. thump & sag    - campana dinamica a 98 Hz (Q 2,8) che segue l'RMS della banda 60-150 Hz:
//                       sui palm-mute il cono "si muove" (fino a +6 dB)
//   4. air, low cut, high cut finali
// Gli IR (dati audio, non solo i percorsi) si salvano col progetto.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BBlendIDs
{
    inline constexpr const char* x = "x";
    inline constexpr const char* y = "y";
    inline constexpr const char* thump = "thump";
    inline constexpr const char* air = "air";
    inline constexpr const char* locut = "locut";
    inline constexpr const char* hicut = "hicut";
}

class BBlendProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BBlendProcessor();
    ~BBlendProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-blend"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static constexpr double kMaxIrSeconds = 0.5;
    static constexpr float kInputTrim = 0.44668359f;   // -7 dB fissi in ingresso

    // Caricamento (message thread): da file o da un buffer (test). slot 0..3 = A, B, C, D.
    bool loadIrFile (int slot, const juce::File&);          // sincrono (test, stato)
    bool loadIrFileAsync (int slot, const juce::File&);     // interfaccia: lettura e decodifica sul thread del builder
                                                            // (false se quel file è già in un altro slot: mai due volte)
    int slotUsing (const juce::File&) const;                // slot che ha già questo file (-1 = nessuno)
    // Ascolto della scelta: mentre il browser della cartella è aperto si sente solo lo slot che si sta scegliendo
    // (-1 = il blend normale)
    void setSoloSlot (int slot);
    int getSoloSlot() const noexcept { return soloSlot.load(); }
    bool isLoading (int slot) const { return loading[(size_t) slot].load(); }
    void loadIr (int slot, const juce::AudioBuffer<float>& data, double rate, const juce::String& name);
    juce::String getSlotName (int slot) const;
    double getSlotRate (int slot) const;              // frequenza di campionamento del file caricato (0 = vuoto)
    void removeIr (int slot);
    juce::File getSlotFile (int slot) const;   // file dell'IR dello slot (anche dopo aver riaperto il progetto)
    // Cartella in cui si apre il browser cliccando uno slot con un IR: quella scelta per lo slot (se contiene
    // ancora l'IR), altrimenti quella del file, altrimenti l'ultima cartella aperta. Vuota = nessuna nota.
    juce::File getBrowseFolder (int slot) const;
    juce::File getLastFolder() const;
    void setBrowseFolder (int slot, const juce::File& dir);   // cartella scelta per lo slot (diventa anche l'ultima)
    // Forma del pad secondo quanti IR sono caricati: 1 = un punto, 2 = una linea, 3 = un triangolo,
    // 4 = un quadrato. Vertici in coordinate normalizzate (0..1, y = 1 in alto), nell'ordine degli slot caricati.
    static int shapeVertices (int count, std::array<juce::Point<float>, 4>& v);
    // Pesi a potenza costante (somma dei quadrati = 1) per gli IR caricati, dalla posizione X/Y.
    static void weightsFor (float x, float y, int count, std::array<float, 4>& w);
    // Posizione mostrata del nodo (dentro la forma corrente).
    static juce::Point<float> constrain (float x, float y, int count);
    static juce::Point<float> centreOf (int count);          // centro geometrico della forma (doppio clic sul nodo)
    bool waitForBuild (int timeoutMs);                       // test
    float getLastAlignmentShift (int slot) const { return alignShift[(size_t) slot].load(); }

    // Grafico: risposta dell'IR composito (dB, normalizzata a 0 dB al massimo) e filtri d'uscita.
    float irResponseDb (double hz) const;
    float filtersDb (double hz) const;
    int getResponseVersion() const noexcept { return responseVersion.load(); }
    std::atomic<float> thumpDb { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;          // segue X/Y e chiede un nuovo IR composito
    class Builder;
    friend class Builder;
    void build();                           // thread del builder
    void requestBuild();

    struct Slot { juce::AudioBuffer<float> data; double rate = 0; juce::String name, path, folder; };
    std::array<Slot, 4> slots;
    mutable juce::CriticalSection slotLock;
    std::atomic<int> slotsVersion { 0 }, builtSlotsVersion { -1 }, buildRequests { 0 }, buildsDone { 0 };
    std::array<std::vector<float>, 4> aligned;   // al sample rate corrente, energia unitaria
    std::array<std::atomic<float>, 4> alignShift {};
    std::array<juce::File, 4> pendingFiles;          // protetti da slotLock
    std::array<std::atomic<bool>, 4> loading {};
    std::array<juce::File, 4> slotFiles;   // solo thread dei messaggi
    juce::String lastFolder;                // protetta da slotLock
    float lastX = -1, lastY = -1;
    std::unique_ptr<Builder> builder;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    mutable juce::SpinLock respLock;
    std::vector<float> respDb; double respBinHz = 1.0;
    std::atomic<int> responseVersion { 0 };
    std::atomic<bool> hasIr { false };
    std::atomic<int> soloSlot { -1 };

    std::atomic<float>* pX = nullptr; std::atomic<float>* pY = nullptr; std::atomic<float>* pThump = nullptr;
    std::atomic<float>* pAir = nullptr; std::atomic<float>* pLo = nullptr; std::atomic<float>* pHi = nullptr;
    std::atomic<double> fs { 48000.0 };
    // tagli finali: SVF TPT con frequenza smussata (in ottave) e aggiornata ogni 16 campioni, stato mai azzerato;
    // quando il taglio va a fondo scala si passa a diretto con una dissolvenza (niente click, niente "sparkle")
    struct SmoothCut
    {
        std::array<sp::Svf, 2> f;
        float logHz = 0, mix = 0, q = 0.7071f;
        bool hp = false;
    };
    void runCut (SmoothCut&, float targetHz, bool active, float* const* chans, int numCh, int n, double rate) noexcept;
    struct Ch { sp::Bq thump, air; };
    std::array<Ch, 2> ch {};
    SmoothCut loCut, hiCut;
    sp::Bq lowBand;
    float env = 0, eAtk = 0, eRel = 0, appliedThump = 100, lastAir = -1;
    std::vector<float> envSteps;

};

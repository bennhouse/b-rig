#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

#include "DSP/Biquad.h"
#include "DSP/FractionalDelay.h"
#include "DSP/PhaseAligner.h"
#include "DSP/Delverb.h"
#include "DSP/Compressor.h"
#include "smp/Spectrum.h"
#include "smp/PresetMemory.h"

namespace ParamIDs
{
    inline constexpr const char* spreadOn    = "spread_on";
    inline constexpr const char* delayMs     = "delay_ms";
    inline constexpr const char* alignRange  = "align_range_ms";
    inline constexpr const char* revOn       = "rev_on";
    inline constexpr const char* revType     = "rev_type";
    inline constexpr const char* revMix      = "rev_mix";
    inline constexpr const char* revBalance  = "rev_balance";
    inline constexpr const char* revTimeMode = "rev_time_mode";
    inline constexpr const char* revTapBpm   = "rev_tap_bpm";
    inline constexpr const char* compOn      = "comp_on";
    inline constexpr const char* compMode    = "comp_mode";
    inline constexpr const char* compThr     = "comp_thr";
    inline constexpr const char* outGain     = "out_gain";
    inline constexpr const char* outAuto     = "out_auto";
    inline constexpr const char* eqOn        = "eq_on";
    inline constexpr const char* eqMode      = "eq_mode";
    inline constexpr const char* eqQuality   = "eq_quality";
    inline juce::String band (int b, const char* what) { return "b" + juce::String (b + 1) + "_" + what; }
}

class BAftercabProcessor final : public juce::AudioProcessor,
                                  private juce::Timer,
                                  private juce::AsyncUpdater
{
public:
    enum class Mode { MonoToMono = 0, MonoToStereo = 1, StereoToStereo = 2 };
    enum class AlignState { Idle = 0, Listening, Analysing, Done, Failed };

    static constexpr int kNumBands = 5;
    static constexpr double kMinDelayMs = 1.0, kMaxDelayMs = 30.0;
    static constexpr double kCaptureSeconds = 3.0;
    static constexpr float kMaxEqGainDb = 15.0f;   // scala normale ±15 dB (precision: ±4)

    BAftercabProcessor();
    ~BAftercabProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processorLayoutsChanged() override { updateMode(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-aftercab"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }   // coda di hall + ripetizioni del delay
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Per l'interfaccia
    juce::AudioProcessorValueTreeState apvts;
    Mode getMode() const noexcept { return (Mode) mode.load(); }

    void startAlignment();          // avvia ascolto + analisi (solo Mono -> Stereo)
    void cancelAlignment();
    AlignState getAlignState() const noexcept { return (AlignState) alignState.load(); }
    float getAlignProgress() const noexcept;
    sp::AlignResult getLastAlignResult() const;

    // Aggiorna latenza e risposta all'impulso dell'EQ a fase lineare.
    // Gira sul message thread (timer); pubblico per poterlo chiamare dai test.
    void refreshNonRealtimeState();
    bool waitForLinearIr (int timeoutMs);   // test: attende la costruzione del FIR richiesto

    // Coefficienti dell'EQ dai parametri correnti (usato anche dal display grafico).
    std::array<sp::BiquadCoeffs, kNumBands> computeEqCoeffs (double sr) const;

    // In linear phase: risposta reale (dB) del filtro FIR caricato. false se non attivo.
    bool getLinearPhaseResponseDb (double hz, double& db) const;

    // Riduzione di guadagno del compressore (dB), per l'indicatore nell'interfaccia.
    // Per il meter: la riduzione massima dall'ultima lettura (nessun picco perso tra un frame e l'altro).
    // Analizzatore di spettro: il segnale dopo l'EQ, copiato solo mentre l'interfaccia è aperta.
    ui::SpectrumTap spectrumTap;
    std::atomic<bool> spectrumActive { false };

    float takeCompGainReductionPeak() noexcept { return compGrPeak.exchange (0.0f); }

    // Tempo del delay: default (tempo proprio del timbro), sync (tempo della DAW), tap.
    enum class TimeMode { Default = 0, Sync = 1, Tap = 2 };
    TimeMode getTimeMode() const noexcept { return (TimeMode) juce::jlimit (0, 2, (int) pRevTimeMode->load()); }
    float getTapBpm() const noexcept { return pRevTapBpm->load(); }
    // Il tasto sync/tap: un tocco isolato alterna default <-> sync (o torna a default
    // da tap); tocchi ravvicinati (< 2 s) misurano il tempo -> modalità tap.
    // Tasto tap (separato dal sync): tocchi in serie, il tempo si aggiorna dal 4° tocco se gli ultimi 4 stanno in 3 s.
    void tapPressed() { tapAt (juce::Time::getMillisecondCounterHiRes()); }
    void tapAt (double nowMs);
    bool isSync() const noexcept { return getTimeMode() == TimeMode::Sync; }
    void setSync (bool on) { setTimeMode (on ? TimeMode::Sync : TimeMode::Tap); }
    // Modifica diretta (doppio clic sul tasto): modo e BPM scritti a tastiera.
    void setTimeMode (TimeMode m);
    void setTapTempo (float bpm);       // passa anche in modalità tap
    void clearTapSeries() { tapTimes.clear(); lastTapMs = -1.0e9; }

    // Uscita (OUT): guadagno manuale, oppure "auto" = compensazione fissa (non dinamica) calcolata
    // dalle impostazioni, così i diversi effetti restano bilanciati tra loro.
    float getAutoOutDb() const noexcept { return autoOutDb.load(); }
    bool isOutAuto() const noexcept { return pOutAuto->load() > 0.5f; }
    float getEffectiveOutDb() const noexcept { return isOutAuto() ? getAutoOutDb() : pOutGain->load(); }
    float computeAutoOutDb() const;     // per i test

    // Memoria per preset (solo sessione): soglia per tipo di compressione, mix e balance per timbro.
    ui::PresetMemory compMemory, revMemory;

    // Stato solo-interfaccia salvato col progetto (es. zoom "precision").
    bool getUiFlag (const juce::Identifier& id) const { return (bool) apvts.state.getProperty (id, false); }
    void setUiFlag (const juce::Identifier& id, bool v) { apvts.state.setProperty (id, v, nullptr); }

private:
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void timerCallback() override { refreshNonRealtimeState(); }
    void handleAsyncUpdate() override;          // applica il risultato dell'analisi
    void updateMode();

    int linearPhaseSize() const;
    void processEq (juce::AudioBuffer<float>&, int numChannels);
    void processSplit (juce::AudioBuffer<float>&);
    void captureForAlignment (const float* data, int n);
    void processReverb (juce::AudioBuffer<float>&, int numIn, int numOut);
    void processCompressor (juce::AudioBuffer<float>&, int numChannels);

    // parametri (puntatori ai valori "raw")
    std::atomic<float>* pDelayMs = nullptr;
    std::atomic<float>* pSpreadOn = nullptr;
    std::atomic<float>* pRevOn = nullptr;
    std::atomic<float>* pRevType = nullptr;
    std::atomic<float>* pRevMix = nullptr;
    std::atomic<float>* pCompOn = nullptr;
    std::atomic<float>* pCompMode = nullptr;
    std::atomic<float>* pCompThr = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pOutAuto = nullptr;
    std::atomic<float> autoOutDb { 0.0f };
    // compressore: riduzione media REALE misurata sul segnale che passa (energia in / out, finestra ~2 s, solo con
    // segnale sopra -55 dBFS). L'OUT auto usa questa misura appena è valida (0,4 s di musica dopo l'ultimo cambio
    // di tipo o soglia); prima usa la stima statica. Così il volume resta uguale a qualunque livello d'ingresso.
    std::atomic<float> compMeasDb { 0.0f }; std::atomic<bool> compMeasValid { false };
    double compInAcc = 0.0, compOutAcc = 0.0; int compMeasSamples = 0, compMeasMode = -1; float compMeasThr = 1.0e9f;
    // guadagno d'uscita: in automatico SALE piano (350 ms) e SCENDE in fretta (30 ms): accendendo il comp o
    // cambiando l'EQ il volume non fa mai un picco; a mano segue in 50 ms
    float outG = 1.0f, outUp = 0.0f, outDown = 0.0f, outManual = 0.0f;
    std::atomic<float>* pRevBalance = nullptr;
    std::atomic<float>* pRevTimeMode = nullptr;
    std::atomic<float>* pRevTapBpm = nullptr;
    double lastTapMs = -1.0e9;
    std::vector<double> tapTimes;
    std::atomic<float>* pAlignRange = nullptr;
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pEqMode = nullptr;
    std::atomic<float>* pEqQuality = nullptr;
    struct BandParams { std::atomic<float>* type = nullptr; std::atomic<float>* on; std::atomic<float>* freq; std::atomic<float>* gain; std::atomic<float>* q; };
    std::array<BandParams, kNumBands> pBands {};

    std::atomic<int> mode { (int) Mode::StereoToStereo };
    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    // EQ a fase minima (IIR)
    std::array<std::array<sp::Biquad, kNumBands>, 2> iir {};
    bool eqWasOn = false;
    std::array<bool, kNumBands> bandWasActive {};

    // EQ a fase lineare (FIR via convoluzione). La latenza dichiarata è sempre n/2 (metà del FIR
    // nominale). Per ogni qualità c'è un motore a blocchi grandi (latenza propria B): il FIR viene
    // accorciato di 2B e centrato in n/2 - B, così la latenza totale resta n/2 ma il costo scende
    // di 3-4 volte. Se il buffer dell'host è troppo grande rispetto al FIR si usa il motore a latenza zero.
    juce::dsp::ConvolutionMessageQueue convQueue { 64 };
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 }, convQueue };
    static constexpr int kConvBlock[3] = { 256, 512, 2048 };   // low: /8, mid e high: /16 del FIR nominale a 48 kHz
    std::array<std::unique_ptr<juce::dsp::Convolution>, 3> convBlock;
    std::atomic<int> activeConv { -1 };                         // -1 = motore a latenza zero
    int convBlockLatency (int quality) const noexcept;
    juce::dsp::Convolution& convFor (int idx) noexcept { return idx < 0 ? convolution : *convBlock[(size_t) idx]; }
    int lastConv = -2;
    std::atomic<bool> linearActive { false };
public:
    // Cresce a ogni nuovo FIR caricato: l'interfaccia ricalcola la curva solo quando cambia qualcosa.
    int getLinearResponseVersion() const noexcept { return linearResponseVersion.load(); }
private:
    std::atomic<int> linearResponseVersion { 0 };
    mutable juce::SpinLock linearResponseLock;
    std::vector<float> linearResponseDb;
    double linearResponseBinHz = 1.0;
    bool linearWasActive = false;
    class IrBuilder;
    std::unique_ptr<IrBuilder> irBuilder;
    void buildLinearIr (const std::array<sp::BiquadCoeffs, kNumBands>& c, int n, double sr, int quality);
    std::array<sp::BiquadCoeffs, kNumBands> requestedIrCoeffs {};
    int requestedIrSize = 0;
    double requestedIrRate = 0.0;
    std::array<sp::BiquadCoeffs, kNumBands> loadedIrCoeffs {};
    int loadedIrSize = 0;
    double loadedIrRate = 0.0;

    // Delay del canale sdoppiato
    sp::FractionalDelay delayLine;
    juce::SmoothedValue<double> delaySamples;
    juce::SmoothedValue<float> spreadMix;   // 0 = canali identici, 1 = canale ritardato
    double lastKernelDelay = -1.0;

    // Compressore (dopo l'EQ, prima di sdoppiamento e delverb)
    sp::Compressor compressor;
    juce::SmoothedValue<float> compGate;
    std::vector<float> compGateBuf;
    bool compRunning = false;
    std::atomic<float> compGrPeak { 0.0f };

    // Delverb: delay + riverbero (dopo EQ e sdoppiamento, alimentato dal segnale mono diretto)
    sp::Delverb reverb;
    std::vector<float> revIn, revL, revR;
    juce::SmoothedValue<float> revMixSmoothed, revGate;   // mix e dissolvenza on/off/cambio timbro
    bool reverbRunning = false;
    int reverbTimbre = -1;

    // Allineamento di fase
    class AnalyserThread;
    std::unique_ptr<AnalyserThread> analyser;
    void runAnalysis();             // chiamato dal thread di analisi
    juce::CriticalSection captureLock;
    std::vector<float> capture;
    std::atomic<int> captureFill { 0 };
    std::atomic<int> alignState { (int) AlignState::Idle };
    double captureRate = 44100.0;
    mutable juce::SpinLock resultLock;
    sp::AlignResult lastResult;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BAftercabProcessor)
};

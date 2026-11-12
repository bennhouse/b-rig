#pragma once
// b-delay: delay universale, latenza zero. Un buffer circolare per canale (allocato in prepareToPlay) letto con
// interpolazione Hermite a 4 punti: cambiando il tempo le ripetizioni cambiano altezza come su un nastro, senza click.
// Tutto il carattere sta DENTRO il loop di feedback, così ogni eco si consuma un po' più del precedente:
//   digital - ripetizioni pulite
//   tape    - saturazione morbida (compressione dei picchi), passa-basso a 9 kHz, wow (0,5 Hz) e flutter (6 Hz)
//   bbd     - bucket brigade: passa-basso a 2,8 kHz (2° ordine), saturazione leggera, ripetizioni scure
//   lofi    - campioni tenuti (circa 12 kHz) e 8 bit, banda 300 Hz-3 kHz
//   reverse - due testine leggono all'indietro grani lunghi quanto il tempo di delay, incrociate a coseno
// Nel loop anche locut / hicut, "smear" (4 passa-tutto che diffondono gli attacchi a ogni giro), modulazione
// ("mod", chorus/vibrato sulle ripetizioni) e un soft clipper (feedback fino al 110 % senza esplodere).
// time 1-2000 ms o, con sync, una figura (1/1...1/16, puntata D, terzina T) sul tempo del brano; "tap" fa la media
// degli ultimi click; "offset" sposta il tempo del destro (±50 %); "ping-pong" alterna L e R.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BDelayIDs
{
    inline constexpr const char* type     = "type";
    inline constexpr const char* time     = "time";
    inline constexpr const char* sync     = "sync";
    inline constexpr const char* division = "division";
    inline constexpr const char* feedback = "feedback";
    inline constexpr const char* locut    = "locut";
    inline constexpr const char* hicut    = "hicut";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* offset   = "offset";
    inline constexpr const char* pingpong = "pingpong";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* smear    = "smear";
    inline constexpr const char* duck     = "duck";
    inline constexpr const char* output   = "output";
    inline constexpr const char* trails   = "trails";
    inline constexpr const char* bypass   = "bypass";
}

class BDelayProcessor final : public juce::AudioProcessor
{
public:
    BDelayProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (BDelayIDs::bypass); }

    juce::AudioProcessorValueTreeState apvts;

    enum Type { Digital, Tape, Bbd, Lofi, Reverse };
    static constexpr int kDivisions = 15;
    static const juce::StringArray& divisionNames();          // 1/1, 1/1D, 1/1T, 1/2 ... 1/16T
    static float divisionBeats (int index) noexcept;          // in quarti
    // tempo del canale sinistro (ms) dai parametri e dal tempo del brano
    static float leftMs (float timeMs, bool sync, int division, double bpm) noexcept;
    void tap();                                               // message thread: un click su tap

    // per il grafico
    std::atomic<double> bpmNow { 120.0 };
    std::atomic<float> msL { 380.0f }, msR { 380.0f }, wetPeak { 0.0f };
    static constexpr int kRing = 8192, kDecim = 64;   // un punto ogni 64 campioni: ~11 s a 48 kHz
    std::array<float, kRing> ringIn {}, ringL {}, ringR {};   // ingresso dry e ingresso delle due linee
    std::atomic<int> ringPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::array<std::atomic<float>*, 16> raw {};
    float prm (int i) const noexcept { return raw[(size_t) i]->load(); }
    float readHermite (const std::vector<float>& b, float delay) const noexcept;
    float character (int ch, float x) noexcept;   // il carattere del tipo, dentro il loop

    double fs = 48000.0;
    static constexpr int kBuf = 1 << 19;
    std::array<std::vector<float>, 2> buf;
    int w = 0;
    std::array<float, 2> dSm {};
    // reverse: due testine per canale
    std::array<std::array<float, 2>, 2> revPhase {};
    // filtri nel loop
    std::array<sp::Svf, 2> lo, hi, bbd1, bbd2, lofiHp, lofiLp;
    std::array<float, 2> tapeLp {}, holdV {};
    int holdN = 0;
    struct Ap { std::vector<float> b; int pos = 0, len = 1; };
    std::array<std::array<Ap, 4>, 2> smearAp;
    // modulazione (oscillatori in quadratura)
    float modS = 0, modC = 1, modK = 0, wowS = 0, wowC = 1, wowK = 0, flS = 0, flC = 1, flK = 0;
    std::array<float, 2> fbSig {};
    float fbSm = 0.45f, mixSm = 0.3f, outSm = 1.0f, sendSm = 1.0f, wetOutSm = 1.0f, duckEnv = 0, duckAtk = 0, duckRel = 0, lastHi = -1, lastLo = -1;
    int lastType = -1;
    // tap
    std::vector<double> taps;
    // anello per il grafico
    float rIn = 0, rL = 0, rR = 0; int rN = 0;
};

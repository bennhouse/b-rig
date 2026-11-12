#pragma once
// b-verb: riverbero universale (chitarra, voce, synth). Latenza zero, nessuna allocazione nel ciclo audio.
//   1. predelay       - un buffer circolare per canale (0-200 ms), da cui si leggono anche le prime riflessioni
//   2. early          - 10 tap per canale con uno schema diverso a sinistra e a destra, scalati da "size"
//   3. diffusione     - 4 passa-tutto di Schroeder in serie (densità diversa per tipo); spring: 24 passa-tutto del
//                       1° ordine in cascata (dispersione: il "chirp" delle molle)
//   4. coda           - FDN a 8 linee con matrice di Hadamard (trasformata veloce: solo somme e differenze), tempo
//                       di riverbero RT60 da "decay", filtro assorbente "damp" nel loop (sopra quella frequenza la coda
//                       dura metà), modulazione lenta delle linee
//                       ("mod") contro le risonanze metalliche
//   5. shimmer        - nel loop un pitch shifter a +1 ottava (due testine, grani di 60 ms) rientra al 25 %
//   6. freeze         - feedback 1, niente damping né ingresso: la coda resta sospesa
//   7. uscita         - locut e hicut sul wet, larghezza stereo, duck (il wet si abbassa mentre il dry è forte),
//                       mix (il dry resta pieno fino al 50 %) e output
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "smp/Filters.h"

namespace BVerbIDs
{
    inline constexpr const char* type     = "type";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* predelay = "predelay";
    inline constexpr const char* size     = "size";
    inline constexpr const char* locut    = "locut";
    inline constexpr const char* hicut    = "hicut";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* early    = "early";
    inline constexpr const char* damp     = "damp";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* duck     = "duck";
    inline constexpr const char* stereo   = "stereo";
    inline constexpr const char* freeze   = "freeze";
    inline constexpr const char* output   = "output";
}

class BVerbProcessor final : public juce::AudioProcessor
{
public:
    BVerbProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-verb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    enum Type { Room, Plate, Hall, Spring, Shimmer };
    static constexpr int kLines = 8, kTaps = 10;
    // prime riflessioni (ms a size 100 %, prima della scala del tipo) e loro livello per tipo
    static constexpr float kTapMsL[kTaps] = { 7.1f, 11.3f, 17.9f, 23.2f, 31.7f, 37.9f, 46.1f, 53.3f, 61.7f, 71.9f };
    static constexpr float kTapMsR[kTaps] = { 8.3f, 13.1f, 16.4f, 25.7f, 29.9f, 40.3f, 44.6f, 57.1f, 63.9f, 69.4f };
    static constexpr float kTypeEarly[5] = { 1.0f, 0.15f, 0.6f, 0.0f, 0.5f };
    static constexpr float kTypeScale[5] = { 0.45f, 0.35f, 1.0f, 0.4f, 1.0f };   // dimensione della stanza per tipo
    static float tapTimeMs (int type, float size01, int k, bool right) noexcept
    { return (right ? kTapMsR[k] : kTapMsL[k]) * (0.3f + 0.7f * size01) * kTypeScale[type]; }
    static float tapGain (int k) noexcept { return std::pow (0.85f, (float) k); }

    // per il grafico: livelli d'ingresso e del wet, uno ogni 256 campioni
    struct Frame { float in = 0, wet = 0; };
    static constexpr int kHistory = 1024;
    std::array<Frame, kHistory> history {};
    std::atomic<int> historyPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void control() noexcept;   // ogni 32 campioni
    std::array<std::atomic<float>*, 14> raw {};
    float prm (int i) const noexcept { return raw[(size_t) i]->load(); }

    double fs = 48000.0;
    static constexpr int kIn = 1 << 16, kLine = 1 << 15;
    std::array<std::vector<float>, 2> inBuf;
    int inPos = 0;
    std::array<std::vector<float>, kLines> lines;
    int linePos = 0;
    std::array<float, kLines> len {}, lenT {}, g {}, gT {}, lp {};
    float dampC = 0.5f, preSm = 0, preT = 0;
    std::array<std::array<float, kTaps>, 2> tapD {}, tapDT {};
    float earlyG = 0, earlyGT = 0;
    // diffusione
    struct Ap { std::vector<float> b; int pos = 0, len = 1; float g = 0.5f; };
    std::array<Ap, 4> diff;
    std::array<sp::Allpass1, 24> springAp {};
    sp::Svf springLp;
    // modulazione: oscillatori in quadratura (niente seno per campione)
    std::array<float, kLines> oscS {}, oscC {}, oscK {};
    float modDepth = 0;
    // shimmer
    std::vector<float> shBuf = std::vector<float> (1 << 15, 0.0f);
    int shPos = 0;
    std::array<float, 2> shD {};
    float shGrain = 2880.0f, shimmerFb = 0.0f;
    // uscita
    std::array<sp::Svf, 2> loCut, hiCut;
    float mixSm = 0.25f, outSm = 1.0f, widthSm = 1.0f, duckEnv = 0, duckAtk = 0, duckRel = 0;
    int ctl = 0, type = -1;
    bool frozen = false;
    Frame acc; int accN = 0;
};

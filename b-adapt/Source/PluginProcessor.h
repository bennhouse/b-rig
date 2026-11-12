#pragma once
// b-adapt: prepara una chitarra (o un mix) per i social e per gli altoparlanti piccoli.
// 5 stadi in serie:
//   1. mono guard    - M/S: passa-alto 250 Hz sul side (bassi mono); se la correlazione L/R va sotto zero
//                      il side si abbassa fino a 3 dB ("safe width")
//   2. phantom thump - banda 75-155 Hz -> armoniche pari (raddrizzamento) e dispari (tanh) -> banda
//                      170-420 Hz sommata al segnale: i bassi si "sentono" anche dove non ci sono
//   3. de-harsh      - per il 70 % un EQ fisso sui punti caldi (campane a 3 kHz -5 dB e 5 kHz -4 dB), per il 30 % un taglio
//                      dinamico molto stretto (Q 5, max 2 dB) su 4 bande tra 2,5 e 6,8 kHz che scende solo quando una banda
//                      sporge sulle altre (una risonanza, non il volume): attacco 1 ms, rilascio 40 ms
//   4. codec shield  - passa-basso 13,5 kHz, soft clipper in oversampling 4x e limiter con 1 ms di
//                      lookahead che misura i picchi veri (4x): uscita <= -1 dBTP
//   5. monitor       - simulazione di ascolto (phone / laptop). Solo per ascoltare: va spenta prima
//                      dell'export (l'interfaccia lo segnala in verde su tutto il riquadro)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "smp/Filters.h"
#include "smp/Spectrum.h"

namespace BAdaptIDs
{
    inline constexpr const char* safeWidth = "safe_width";
    inline constexpr const char* thump     = "thump";
    inline constexpr const char* deharsh   = "deharsh";
    inline constexpr const char* monitor   = "monitor";
}

class BAdaptProcessor final : public juce::AudioProcessor
{
public:
    BAdaptProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-adapt"; }
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
    static constexpr float kCeiling = 0.8710f;   // -1,2 dBFS dentro il clipper: dopo i filtri resta <= -1 dBTP

    // Per l'interfaccia
    static constexpr int kDyn = 4;
    static constexpr float kDynHz[kDyn] = { 2500.0f, 3400.0f, 4800.0f, 6800.0f }, kDynQ = 5.0f, kDynMaxDb = 2.0f;   // dinamico: al massimo 2 dB (EQ fisso 5 dB: 70/30)
    static constexpr float kStaticHz[2] = { 3000.0f, 5000.0f }, kStaticQ[2] = { 2.0f, 2.2f }, kStaticDb[2] = { 5.0f, 4.0f };   // a de-harsh 100 %
    std::array<std::atomic<float>, kDyn> dynCutDb {};
    std::atomic<float> harshCutDb { 0.0f }, correlation { 0.0f }, sideCutDb { 0.0f };   // harshCutDb: il taglio dinamico più forte
    std::atomic<bool> hasSignal { false };
    ui::SpectrumTap spectrumTap;          // uscita, solo a interfaccia aperta
    std::atomic<bool> spectrumActive { false };
    bool isAuditioning() const noexcept { return pMonitor->load() > 0.5f; }
    // Risposta composita per il grafico (dB): thump (indicativo), taglio dinamico attuale, codec shield, monitor.
    float responseDb (double hz) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* pSafe = nullptr; std::atomic<float>* pThump = nullptr;
    std::atomic<float>* pHarsh = nullptr; std::atomic<float>* pMonitor = nullptr;
    double fs = 48000.0;
    struct Ch { sp::Bq bassHp, bassLp, harmHp, harmLp, stat1, stat2, codec, m1, m2, m3, m4, m5; std::array<sp::Svf, 4> dyn; float dc = 0; };
    std::array<Ch, 2> ch {};
    sp::Bq sideHp;
    std::array<sp::Bq, 4> scBand {};
    std::array<float, 4> bandEnv {}, bandG {}, bandGTarget {};
    float lastAmt = -1.0f;
    float cLR = 0, cLL = 0, cRR = 0, corrCoef = 0, sGain = 1, sCoef = 0, env = 0, eAtk = 0, eRel = 0, dcCoef = 0;
    float curCutDb = 0, appliedCutDb = 100, sideTargetDb = 0;
    int lastMonitor = -1, counter = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os, tpDetect;
    // limiter true peak: ritardo di lookahead, guadagno richiesto per ogni campione, rampa lineare
    static constexpr int kLook = 48, kRing = 128;
    std::array<std::array<float, kRing>, 2> look {};
    std::array<float, kRing> gReq {};
    int lookPos = 0, detLag = 0;
    float limG = 1.0f, limRel = 0.0f;
    juce::AudioBuffer<float> detBuf;
    static constexpr float kLimCeiling = 0.8511f;   // -1,4 dB sui picchi stimati a 4x: margine per i picchi veri (misurati a 8x)
};

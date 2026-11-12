#pragma once
// Motore audio di b-rig: catena di moduli della suite, bypass con crossfade, limiter di sicurezza,
// metronomo e presa del tuner sulla DI (solo quando il tuner è acceso). Nessuna allocazione nel thread audio:
// i moduli si creano e si preparano nel thread dei messaggi, poi la nuova catena viene pubblicata
// con un puntatore atomico; quelle vecchie si liberano solo quando il thread audio le ha lasciate.
#include "Modules/Modules.h"
#include <array>
#include <atomic>

namespace rig
{
// Un blocco della catena. Il processore è nullptr per i segnaposto dei moduli non ancora pronti.
struct Slot
{
    const ModuleInfo* info = nullptr;
    std::unique_ptr<juce::AudioProcessor> proc;
    std::atomic<bool> enabled { false };
    int inCh = 2, outCh = 2;

    // solo thread audio
    float fade = 0.0f;            // 0 = spento (processBlock saltato), 1 = acceso
    bool fadeTarget = false;
    float fadeStep = 0.0f;
    juce::AudioBuffer<float> dry; // segnale prima del modulo durante il crossfade (pre-allocato)
    std::atomic<int> processedBlocks { 0 };   // per i test: quante volte è stato chiamato processBlock

    bool isActive() const noexcept { return fade > 0.0f || fadeTarget; }
};

// Tempo del metronomo come playhead: i moduli sincronizzati al tempo (delay di b-aftercab) seguono i bpm.
class RigPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

class RigEngine
{
public:
    RigEngine();
    ~RigEngine();

    // --- audio (chiamati dal callback del dispositivo)
    void prepare (double sampleRate, int maxBlockSize);
    void release();
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;

    // --- catena (thread dei messaggi)
    static constexpr int kMaxSlots = 24;
    int numSlots() const { return (int) order.size(); }
    Slot& slot (int i) { return *order[(size_t) i]; }
    int indexOf (const Slot*) const;
    // true se dopo la posizione index c'è un b-blend (con almeno un IR, se needIr)
    bool cabAfter (int index, bool needIr) const;
    Slot* addModule (const juce::String& id, int position = -1);   // -1 = posizione logica
    int logicalPosition (const ModuleInfo&) const;
    void removeSlot (int index);
    void moveSlot (int index, int delta);
    void moveSlotTo (int from, int to);   // trascinamento della card
    void setEnabled (int index, bool on);
    void resetDefault();      // drop (off) -> amp (off) -> blend (on) -> aftercab (on)
    void clear();
    void collectGarbage();    // libera le catene/moduli che il thread audio non usa più

    juce::ValueTree saveRig() const;
    void loadRig (const juce::ValueTree&);
    std::function<void()> onChainChanged;   // la UI si aggiorna (anche dopo un preset)

    // --- stato condiviso con la UI
    std::atomic<int> diChannel { 0 };
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::atomic<bool> inClip { false }, outClip { false };
    std::atomic<bool> globalMute { false };
    std::atomic<float> limiterGr { 1.0f };   // guadagno del limiter (1 = nessuna riduzione)
    static constexpr float kCeiling = 0.94406088f;   // -0.5 dBFS

    // metronomo
    std::atomic<bool> metroOn { false };
    std::atomic<int> metroSound { 0 };        // 0 beep, 1 hat, 2 wood
    std::atomic<float> metroVolume { 0.5f };  // 0..1
    static constexpr int kNumSounds = 3;
    static constexpr std::array<const char*, kNumSounds> kSoundNames { { "beep", "hat", "wood" } };
    std::atomic<float> bpm { 120.0f };
    std::atomic<int> sigIndex { 2 };       // vedi kSigs
    std::atomic<int> beatInBar { -1 };     // battito corrente, per la UI
    static constexpr int kNumSigs = 6;
    static constexpr std::array<std::pair<int, int>, kNumSigs> kSigs { { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 } } };
    static constexpr float kMinBpm = 30.0f, kMaxBpm = 300.0f;

    // tuner: DI sotto 1 kHz decimata 4x, solo quando è acceso
    std::atomic<bool> tunerOn { false };
    std::atomic<bool> tunerMutes { true };   // il tuner muta l'uscita quando si accende (scelta dell'utente, tenuta tra le sessioni)
    static constexpr int kTunerFifo = 1 << 14;
    juce::AbstractFifo tunerFifo { kTunerFifo };
    std::array<float, kTunerFifo> tunerBuf {};
    double tunerRate() const noexcept { return fs / 4.0; }


private:
    // b-amp acceso con un b-blend dopo: se la cassa sparisce da dopo di lui (tolta o spostata) torna muto
    std::vector<const Slot*> coveredAmps() const;
    void remuteUncovered (const std::vector<const Slot*>& before);
    struct Chain { std::array<Slot*, kMaxSlots> s {}; int n = 0; };
    void publish();
    void prepareSlot (Slot&);
    void processSlot (Slot&, int n) noexcept;
    void runProcessor (Slot&, int n) noexcept;
    bool metronome (int n) noexcept;   // false = spento (nessun click in metroBuf)

    std::vector<std::unique_ptr<Slot>> order;       // thread dei messaggi
    std::atomic<Chain*> live { nullptr };
    std::atomic<juce::uint64> audioTicks { 0 };     // avanza a ogni callback
    struct Retired { std::unique_ptr<Chain> chain; std::vector<std::unique_ptr<Slot>> slots; juce::uint64 tick; };
    std::vector<Retired> retired;

    double fs = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> work;     // 2 canali: il segnale della catena
    std::vector<float> metroBuf;       // il click del metronomo: a parte, si somma dopo il limiter della chitarra
    juce::MidiBuffer midi;
    RigPlayHead playHead;

    // limiter e mute
    float limGain = 1.0f, limRelease = 0.0f, muteGain = 1.0f, muteStep = 0.0f;
    // metronomo
    double samplesToBeat = 0.0;
    int beat = -1;
    float clickPhase = 0, clickInc = 0, clickEnv = 0, clickDecay = 0, clickPhase2 = 0, clickInc2 = 0, clickPrev = 0, clickPrev2 = 0;
    std::array<float, kNumSounds> decays {};
    int clickSound = 0;
    juce::uint32 noise = 22222;
    double ppq = 0.0;
    // tuner
    struct Lp { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                float process (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Lp tunerLp1, tunerLp2;
    int tunerDecim = 0;
};
}

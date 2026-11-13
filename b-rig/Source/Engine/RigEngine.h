#pragma once
// Motore audio di b-rig: catena di moduli della suite, bypass con crossfade, limiter di sicurezza,
// metronomo e presa del tuner sulla DI (solo quando il tuner è acceso).
// Split: la catena si può dividere in due rami paralleli (A e B). I moduli prima del punto di split (lane 0)
// suonano per entrambi; dopo, ogni ramo ha i suoi moduli e alla fine i due si sommano (a -6 dB ciascuno, così
// due rami uguali danno lo stesso livello di uno) oppure, con "wide", A a sinistra e B a destra. Nessuna allocazione nel thread audio:
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
    // b-amp spento dall'app perché dopo di lui non c'è una cassa (badge "safe muted"). Se lo spegne l'utente è
    // solo spento, come gli altri moduli.
    std::atomic<bool> safeMuted { false };
    int lane = 0;                 // 0 = catena unica o prima dello split, 1 = ramo A, 2 = ramo B
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
    static constexpr int kMaxSlots = 48;
    int numSlots() const { return (int) order.size(); }
    Slot& slot (int i) { return *order[(size_t) i]; }
    const Slot& slot (int i) const { return *order[(size_t) i]; }
    int indexOf (const Slot*) const;
    // true se l'uscita di questo modulo passa sempre da un b-blend dopo di lui (con almeno un IR, se needIr):
    // nello stesso ramo, oppure, prima dello split, in tutti e due i rami
    bool cabAfter (int index, bool needIr) const;
    Slot* addModule (const juce::String& id, int position = -1);   // catena unica / prima dello split; -1 = posizione logica
    int logicalPosition (const ModuleInfo&) const;
    void removeSlot (int index);
    void moveSlot (int index, int delta);
    void moveSlotTo (int from, int to);   // trascinamento della card (catena unica)
    void setEnabled (int index, bool on);   // scelta dell'utente: toglie il "safe muted"
    void resetDefault();      // drop (off) -> amp (safe muted) -> blend (on) -> aftercab (on)
    void clear();             // catena vuota e non divisa
    void collectGarbage();    // libera le catene/moduli che il thread audio non usa più

    // --- split (thread dei messaggi). Le posizioni sono dentro il ramo (lane 0 = prima dello split).
    bool isSplit() const { return split; }
    int laneSize (int lane) const;
    int laneStart (int lane) const;      // indice del primo modulo del ramo nell'elenco
    bool laneHas (int lane, const juce::String& id, const Slot* except = nullptr) const;
    bool canPlace (const juce::String& id, int lane, const Slot* except = nullptr) const;   // un modulo per tipo in ogni ramo
    Slot* addModuleAt (const juce::String& id, int lane, int pos);   // pos -1 = posizione logica nel ramo
    bool placeSlot (int index, int lane, int pos);                   // sposta (pos contata senza il modulo stesso)
    // divide la catena dalla DI: tutto va nel ramo A e il ramo B parte come copia (stesso suono finché non lo cambi)
    void splitChain();
    // riunisce i rami: resta keepLane, l'altro ramo si toglie
    void mergeChain (int keepLane);
    // sposta il punto di split: newTrunk = moduli prima dello split. Prima: i moduli che escono passano nel ramo
    // viewLane e una copia nell'altro (il suono resta uguale). Dopo: i primi moduli di viewLane entrano
    // nella parte comune e lo stesso tipo nell'altro ramo si toglie.
    void moveSplit (int viewLane, int newTrunk);
    // fine dei rami:
    //   "stereo" (acceso di default quando si divide) = a a sinistra, b a destra; spento = "mono", somma a -6 dB.
    //   "widen" (in stereo, automatico): b esce un poco dopo (5-25 ms), al ritardo misurato in cui a e b sono più simili
    //   (correlazione massima), così anche sommati in mono si rinforzano invece di cancellarsi.
    //   "balance" (anche in mono): allinea la fase dei due rami (ritardo entro ±5 ms ed eventuale polarità invertita)
    //   e ne pareggia il volume. Si calcola una volta e resta fisso finché non si rilancia.
    // Le misure registrano 0,75 s dei due rami mentre si suona (sopra -50 dBFS).
    std::atomic<bool> wide { false };
    std::atomic<bool> widen { false };
    std::atomic<float> widenMs { 0.0f };
    std::atomic<bool> balance { false };
    std::atomic<float> gainA { 1.0f }, gainB { 1.0f };   // balance: guadagno di ogni ramo (b negativo = polarità invertita)
    std::atomic<float> alignMs { 0.0f };                  // balance: > 0 b in ritardo, < 0 a in ritardo
    enum MeasureFor { kWiden = 1, kBalance = 2 };
    void startMeasure (int what);         // thread dei messaggi; più richieste insieme usano la stessa registrazione
    void startWidenMeasure() { startMeasure (kWiden); }
    void cancelMeasure() { widenState = 0; measureWhat = 0; }
    void cancelWidenMeasure() { cancelMeasure(); }
    bool isMeasuring (int what = kWiden | kBalance) const { return widenState.load() != 0 && (measureWhat.load() & what) != 0; }
    bool isMeasuringWiden() const { return isMeasuring (kWiden); }
    int pollMeasure();                    // thread dei messaggi: le misure appena finite (kWiden / kBalance), 0 se nessuna
    bool pollWiden() { return (pollMeasure() & kWiden) != 0; }
    static float bestWidenLag (const float* a, const float* b, int n, double fs);   // ms; per i test
    // balance: ritardo (ms, > 0 = b in ritardo) e segno della correlazione migliori entro ±5 ms; guadagni che pareggiano
    struct BalanceResult { float alignMs = 0, gainA = 1, gainB = 1; };
    static BalanceResult measureBalance (const float* a, const float* b, int n, double fs);

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
    std::atomic<float> metroPitch { 0.0f };   // semitoni, -12..+12 (beep e wood; hat più chiaro o più scuro)
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
    struct Chain { std::array<Slot*, kMaxSlots> s {}; int n = 0, nTrunk = 0, nA = 0; bool split = false; };
    void publish();
    void prepareSlot (Slot&);
    void processSlot (Slot&, juce::AudioBuffer<float>&, int n) noexcept;
    void runProcessor (Slot&, juce::AudioBuffer<float>&, int n) noexcept;
    std::unique_ptr<Slot> makeSlot (const ModuleInfo&);
    std::unique_ptr<Slot> cloneSlot (const Slot&);
    void insertSlot (std::unique_ptr<Slot>, int lane, int pos);
    bool isCab (const Slot&, bool needIr) const;
    void armIfCovered (Slot&);
    void armCovered();   // arma ogni b-amp muto per protezione che ha un b-blend dopo di sé
    bool split = false;
    bool metronome (int n) noexcept;   // false = spento (nessun click in metroBuf)

    std::vector<std::unique_ptr<Slot>> order;       // thread dei messaggi
    std::atomic<Chain*> live { nullptr };
    std::atomic<juce::uint64> audioTicks { 0 };     // avanza a ogni callback
    struct Retired { std::unique_ptr<Chain> chain; std::vector<std::unique_ptr<Slot>> slots; juce::uint64 tick; };
    std::vector<Retired> retired;

    double fs = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> work;     // 2 canali: il segnale della catena (e del ramo A)
    juce::AudioBuffer<float> workB;    // ramo B
    float wideMix = 0.0f;
    // fine dei rami: una linea di ritardo per ramo (2 canali ciascuno), guadagni, misura
    static constexpr int kWidenBuf = 1 << 13;
    std::array<std::vector<float>, 4> laneLine;      // a sinistra, a destra, b sinistra, b destra
    int widenW = 0;
    float delayCur[2] = { 0.0f, 0.0f }, gainCur[2] = { 1.0f, 1.0f }, widenSmooth = 0.001f;
    std::atomic<int> widenState { 0 };              // 0 niente, 1 registra, 2 pronto
    std::atomic<int> measureWhat { 0 };
    std::vector<float> capA, capB;
    int capPos = 0;
    std::vector<float> metroBuf;       // il click del metronomo: a parte, si somma dopo il limiter della chitarra
    juce::MidiBuffer midi;
    RigPlayHead playHead;

    // limiter e mute
    float limGain = 1.0f, limRelease = 0.0f, muteGain = 1.0f, muteStep = 0.0f;
    // metronomo
    double samplesToBeat = 0.0;
    int beat = -1;
    float clickPhase = 0, clickInc = 0, clickEnv = 0, clickDecay = 0, clickPhase2 = 0, clickInc2 = 0, clickPrev = 0, clickPrev2 = 0;
    float hatMix = 0.5f;
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

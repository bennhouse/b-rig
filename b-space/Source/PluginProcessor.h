#pragma once
// b-space: mette un ampli (o qualsiasi strumento) in una stanza vera, partendo dalla sua geometria. Nessun
// controllo da riverbero classico (decay, wet/dry, pre-delay): si scelgono stanza, pareti, sorgente e posizioni.
// Latenza zero, nessuna funzione trigonometrica nel ciclo audio:
//   1. geometria (solo quando cambia un parametro): stanza rettangolare W x D x H, sorgenti speculari del 1° ordine.
//      6 percorsi: diretto, pareti sinistra/destra/fronte/retro, soffitto. Per ognuno: ritardo, guadagno
//      d0/d * riflessione della parete, direzione d'arrivo (ITD fino a 0,6 ms e ombra della testa sopra 1,5 kHz
//      sull'orecchio lontano), filtro di direttività dell'ampli (12 kHz in asse -> 1,8 kHz di spalle) e dell'aria.
//      "dist comp" acceso: il diretto ha ritardo 0, le riflessioni il ritardo relativo (d - d0)
//   2. un solo buffer circolare (potenza di 2, maschera di bit) da cui si leggono tutti i percorsi; ritardi e
//      guadagni inseguono i valori nuovi in ~30 ms (niente click trascinando)
//   3. coda: FDN a 4 linee (lunghezze prime scalate sul cammino libero medio 4V/S) con matrice di Hadamard
//      (solo somme e differenze). Tempo di riverbero di Sabine dalle pareti; livello della coda = d0 / distanza
//      critica (più lontano dall'ampli = più stanza)
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

namespace BSpaceIDs
{
    inline constexpr const char* space    = "space";
    inline constexpr const char* walls    = "walls";
    inline constexpr const char* source   = "source";
    inline constexpr const char* distComp = "distcomp";
    inline constexpr const char* width    = "width";
    inline constexpr const char* depth    = "depth";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* angle    = "angle";
    inline constexpr const char* air      = "air";
    inline constexpr const char* srcX     = "srcx";
    inline constexpr const char* srcY     = "srcy";
    inline constexpr const char* lisX     = "lisx";
    inline constexpr const char* lisY     = "lisy";
}

class BSpaceProcessor final : public juce::AudioProcessor
{
public:
    BSpaceProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b-space"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // stanze tipo: un clic su "space" carica le misure
    struct Room { float w, d, h; };
    static constexpr Room kRooms[4] = { { 5.5f, 7.0f, 3.0f }, { 6.0f, 7.5f, 2.6f }, { 12.0f, 18.0f, 5.0f }, { 22.0f, 34.0f, 11.0f } };
    void applySpace (int space);   // message thread

    // geometria (funzione pura dei parametri: la usano l'audio e il disegno della pianta)
    static constexpr int kTaps = 6;   // diretto, sinistra, destra, fronte, retro, soffitto
    struct Params { float w, d, h, sx, sy, lx, ly, angleDeg, air; int walls, source; bool distComp; };
    struct Path
    {
        float dist = 0;               // metri
        float delayS = 0;             // secondi (relativi se dist comp)
        float gain = 0;               // d0/d * riflessione
        float azimuth = 0;            // radianti, + = a destra dell'ascoltatore
        float offAxisDeg = 0;         // angolo rispetto al fronte dell'ampli
        float cutoffHz = 20000;       // direttività + aria
        juce::Point<float> bounce;    // punto di rimbalzo sulla parete (metri), per il disegno
    };
    struct Geometry { std::array<Path, kTaps> paths; float rt60 = 0.3f, tailGain = 0.1f, mfp = 3.0f, wallDampHz = 5000; };
    static Geometry geometry (const Params&);
    Params currentParams() const;

    static constexpr float kWallReflect[4] = { 0.45f, 0.70f, 0.85f, 0.92f };   // treated, wood, brick, concrete
    static constexpr float kWallAbsorb[4] = { 0.55f, 0.25f, 0.08f, 0.04f };
    static constexpr float kWallDampHz[4] = { 2500.0f, 4500.0f, 6500.0f, 8500.0f };
    static constexpr float kSourceHeight = 0.6f, kEarHeight = 1.5f;

    std::atomic<float> outLevel { 0.0f };   // per l'interfaccia

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void recompute() noexcept;
    std::array<std::atomic<float>*, 13> raw {};

    double fs = 48000.0;
    static constexpr int kBuf = 1 << 16;
    std::vector<float> buf = std::vector<float> (kBuf, 0.0f);
    int w = 0;
    // per percorso e orecchio: ritardo (campioni), guadagno, coefficiente del passa-basso a un polo
    struct Ear { float delay = 0, gain = 0, lpA = 0; float tDelay = 0, tGain = 0, tLpA = 0; float z = 0; };
    std::array<std::array<Ear, 2>, kTaps> ears {};
    // coda FDN
    static constexpr int kLine = 1 << 14;
    std::array<std::vector<float>, 4> lines;
    int lw = 0;
    std::array<float, 4> lineLen {}, tLineLen {}, lineG {}, tLineG {}, damp {};
    float dampA = 0, tDampA = 0, send = 0, tSend = 0, norm = 1, tNorm = 1;
    float smooth = 0.001f;
    juce::int64 lastSig = 0;
    int ctl = 0;
    bool first = true;
};

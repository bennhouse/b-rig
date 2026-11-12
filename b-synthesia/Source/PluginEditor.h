#pragma once
// Interfaccia b-synthesia (titanio): titolo e [ tunnel | flow | terrain | prism | attractor ] in alto; due righe di
// controlli (reactivity, decay, chaos, grid / color, aspect, bg, clean view, rec sync); sotto la tela del visual,
// nelle proporzioni scelte (full, 16:9, 9:16). "clean view" nasconde tutto tranne la tela (un clic la richiama).
// "rec sync": mentre la DAW suona salva la tela come sequenza PNG (con alfa vero se bg = alpha) a 30 fps su un thread
// separato, con una coda limitata (se il disco rallenta salta un fotogramma, non blocca mai né audio né interfaccia).
#include "PluginProcessor.h"
#include "smp/Widgets.h"
#include <juce_dsp/juce_dsp.h>

// Stato visivo condiviso dai 4 modi (aggiornato a 60 fps dal FIFO)
struct VisualState
{
    static constexpr int kRoll = 8192, kOrder = 12, kN = 1 << kOrder, kBands = 48, kRows = 28;
    std::array<std::vector<float>, 2> roll { std::vector<float> (kRoll, 0.0f), std::vector<float> (kRoll, 0.0f) };
    int w = 0;
    std::array<float, kBands> bands {};                  // spettro log 40 Hz - 12 kHz (dB normalizzati 0..1)
    std::array<std::array<float, kBands>, kRows> history {};   // per la mesh
    int histPos = 0;
    float sub = 0, hit = 0, level = 0, width = 1, domHz = 0, domDb = -100;
    struct Peak { float hz, db, x; };
    std::array<Peak, 5> peaks {}; int numPeaks = 0;
    float at (int c, int back) const { return roll[(size_t) c][(size_t) ((w - 1 - back) & (kRoll - 1))]; }
};

class FrameWriter final : public juce::Thread
{
public:
    FrameWriter() : juce::Thread ("b-synthesia frames") {}
    ~FrameWriter() override { stopThread (2000); }
    bool push (juce::Image img, const juce::String& name);   // false = coda piena (fotogramma saltato)
    void run() override;
    juce::File folder;
    std::atomic<int> written { 0 }, dropped { 0 };
private:
    juce::CriticalSection lock;
    std::deque<std::pair<juce::Image, juce::String>> queue;
};

class Canvas final : public juce::Component
{
public:
    Canvas (BSynthesiaProcessor& p, VisualState& v) : proc (p), vs (v) {}
    void paint (juce::Graphics& g) override { render (g, getLocalBounds().toFloat(), false); }
    void render (juce::Graphics&, juce::Rectangle<float> area, bool forExport);
    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    float t = 0;   // tempo animazione (s)
    void advance (float dt);   // un passo della simulazione (60 fps, dal timer dell'editor)
private:
    void terrain (juce::Graphics&, juce::Rectangle<float>, juce::Colour, int bg);
    void ensureBuffer (int w, int h);
    juce::Image fb, fb2;   // scia (ARGB trasparente, bassa risoluzione)
    struct Particle { float x, y, vx, vy, seed; };
    std::vector<Particle> parts;
    float ax = 0.1f, ay = 0.1f;   // stato dell'attrattore
    int lastMode = -1;
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    BSynthesiaProcessor& proc;
    VisualState& vs;
    juce::Random rng { 7 };
};

class TextButton2 final : public juce::Component
{
public:
    std::function<void()> onClick; std::function<bool()> isOn; juce::String caption;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

class BSynthesiaEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BSynthesiaEditor (BSynthesiaProcessor&);
    ~BSynthesiaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::Colour paletteColour (int i);
    juce::Rectangle<int> canvasArea() const;
    VisualState& state() { return vs; }   // test
    void frameForTest() { timerCallback(); }
private:
    void timerCallback() override;
    void analyse();
    void setClean (bool c);
    BSynthesiaProcessor& proc;
    VisualState vs;
    Canvas canvas;
    ui::SegmentedChoice modeChoice, colourChoice, aspectChoice, bgChoice;
    std::array<ui::ValueBox, 4> boxes;
    TextButton2 cleanBtn, recBtn;
    bool clean = false, recArmed = false;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::dsp::FFT fft { VisualState::kOrder };
    std::vector<float> fftData = std::vector<float> (2 * VisualState::kN, 0.0f), window = std::vector<float> (VisualState::kN, 0.0f);
    FrameWriter writer;
    double lastFrameMs = -1.0e9; int frameCounter = 0;
    ui::AboutOverlay about { *this, "synthesia" };   // clic sul logo: pannello about (ultimo membro: sta sopra a tutto)
};

#pragma once
// Interfaccia b-meter (accento celeste): in alto la vista [osc | vec | wave | spec]; a sinistra (75 %) l'analizzatore
// con i suoi due parametri, a destra (25 %) la loudness: barra 0 / -36 LUFS con la zona del target (±1 LU),
// short-term (linea spessa) e momentary (linea sottile), gain e auto. Tutto il disegno e le FFT sull'interfaccia (60 fps).
#include "PluginProcessor.h"
#include "smp/Shell.h"
#include <juce_dsp/juce_dsp.h>

struct MeterData   // campioni presi dal FIFO, sul thread dell'interfaccia
{
    static constexpr int kRoll = 16384;
    std::array<std::vector<float>, 2> roll { std::vector<float> (kRoll, 0.0f), std::vector<float> (kRoll, 0.0f) };
    int w = 0, fresh = 0;
    struct Bin { float pkL = 0, pkR = 0, rmsL = 0, rmsR = 0; };
    std::array<Bin, 1600> bins {}; int binPos = 0, binN = 0; Bin acc; double accL = 0, accR = 0;
    float at (int c, int back) const { return roll[(size_t) c][(size_t) ((w - 1 - back) & (kRoll - 1))]; }
    void pull (BMeterProcessor&);
};

class AutoButton final : public juce::Component
{
public:
    explicit AutoButton (BMeterProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.toggleAuto(); repaint(); }
private:
    BMeterProcessor& proc;
};

class MeterView final : public juce::Component
{
public:
    MeterView (BMeterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void frame();   // 60 fps
private:
    void paintOsc (juce::Graphics&, juce::Rectangle<float>);
    void paintVec (juce::Graphics&, juce::Rectangle<float>);
    void paintWave (juce::Graphics&, juce::Rectangle<float>);
    void paintSpec (juce::Graphics&, juce::Rectangle<float>);
    void paintLoudness (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<float> graphArea() const;
    juce::Rectangle<float> loudBar (juce::Rectangle<float>) const;
    int view() const { return (int) proc.apvts.getRawParameterValue (BMeterIDs::view)->load(); }
    BMeterProcessor& proc;
    MeterData data;
    ui::ValueBox oscTime, oscGain, vecPersist, vecScale, waveWindow, gainBox;
    ui::SegmentedChoice waveMode, specSpeed, specTilt;
    AutoButton autoBtn;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    void paintStatic (juce::Graphics&, int view);
    juce::int64 staticKey (int view) const;
    void buildSpecWeights (double fs);
    juce::Image phosphor;
    float lastVx = 0, lastVy = 0;
    ui::LayerCache staticLayer;
    // spettro: FFT 8192 (Blackman-Harris), 200 punti log da 20 Hz a 20 kHz, lisciatura gaussiana di 1/3 d'ottava
    static constexpr int kOrder = 13, kN = 1 << kOrder, kPts = 200;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> fftL = std::vector<float> (2 * kN, 0.0f), fftR = std::vector<float> (2 * kN, 0.0f), window = std::vector<float> (kN, 0.0f);
    std::vector<int> wStart, wCount, wBin; std::vector<float> wVal; double weightsFs = 0;
    std::array<float, kPts> bands {};
    int lastView = -1;
};

class BMeterEditor final : public ui::ShellEditor
{
public:
    explicit BMeterEditor (BMeterProcessor&);
private:
    void tick() override { view.frame(); }
    MeterView view;
};

#pragma once
// Interfaccia b-distort: stesso stile di b-aftercab (minimal, linee continue da 1 px,
// IBM Plex), accento rosso.
//
//   b-distort                                   type [tube|drive|rat|fuzz|fold]
//   ┌──────────────────────────────────────────────────────────────────────────┐
//     drive < 12.0 dB >  tone < 50 % >  locut < 0 Hz >  hicut < 12.0 kHz >  mix < 100 % >  output < 0.0 dB >  stereo
//     curva di trasferimento (quadrata)   spettro dal vivo: ingresso e uscita
//   └──────────────────────────────────────────────────────────────────────────┘

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "smp/Widgets.h"

// Grafico della curva di saturazione: ingresso (x) -> uscita (y), da -1 a +1.
class TransferDisplay final : public juce::Component
{
public:
    explicit TransferDisplay (BDistortProcessor& p) : proc (p) {}
    float scaleY = 1.0f;                        // scala dell'asse Y: 1, 5 o 12
    void paint (juce::Graphics&) override;
    float getScaleLeft() const;                 // bordo sinistro delle etichette (allineamento)
    float getXLabelBaselineFromBottom() const { return (float) (kBottomMargin - 12) - ui::capHeight (ui::kAxisSize, ui::Face::MonoRegular) * 0.5f; }
    static constexpr int kInner = 6;            // la griglia -1..+1 è rientrata di 6 px nell'area del grafico

private:
    juce::Rectangle<int> plotArea() const;
    BDistortProcessor& proc;
    static constexpr int kLeftMargin = 30, kBottomMargin = 20, kTopMargin = 8;
};

// Spettro dal vivo: ingresso (grigio) e uscita (accento), 20 Hz - 20 kHz, 0 / -84 dB.
// Le righe della griglia coincidono con quelle della curva di trasferimento.
class SpectrumDisplay final : public juce::Component
{
public:
    explicit SpectrumDisplay (BDistortProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update (float fps);                    // dal timer dell'interfaccia
    static constexpr int kSide = 14, kTopMargin = 8, kBottomMargin = 20, kInner = 6;

private:
    juce::Rectangle<float> grid() const;
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    BDistortProcessor& proc;
    ui::SpectrumAnalyser inSpec, outSpec;
    bool visible = false;
};

class BDistortEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BDistortEditor (BDistortProcessor&);
    ~BDistortEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> box() const;
    int framePad() const;

    BDistortProcessor& proc;
    TransferDisplay display;
    SpectrumDisplay spectrum;
    ui::ValueBox driveBox, tiltBox, toneBox, locutBox, mixBox, outputBox;   // toneBox = hicut (id storico "tone")
    ui::SegmentedChoice typeChoice;
    juce::Rectangle<int> modeArea;

    struct TextItem { juce::String text; juce::Rectangle<int> area; };
    std::vector<TextItem> labels;
    juce::int64 lastSignature = 0;
    int graphRightEdge = 0;
    float shownPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BDistortEditor)
    ui::AboutOverlay about { *this, "distort" };   // clic sul logo: pannello about (ultimo membro: sta sopra a tutto)
};

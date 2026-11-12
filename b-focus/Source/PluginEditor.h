#pragma once
// Interfaccia b-focus: stesso design system di b-aftercab e b-distort, accento blu elettrico.
//
//   b-focus                              hf  [█████▒▒▒▒▒▒▒]  -12.3 dB  [learn]
//   ┌────────────────────────────────────────────────────────────────────┐
//     threshold < -42.0 dB >  strength < 70 % >  release < 180 ms >      stereo
//     risposta in tempo reale della rete a 3 stadi (20 Hz - 20 kHz, 0 / -36 dB)
//   └────────────────────────────────────────────────────────────────────┘

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "smp/Widgets.h"

// Barra orizzontale della riduzione sulle alte: si riempie da sinistra (pieno = 36 dB).
class ReductionMeter final : public juce::Component
{
public:
    void setReduction (float db) { if (std::abs (db - value) > 0.01f) { value = db; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;   // dB, positivo
};

// Pulsante "learn" (stile del tasto tempo di b-aftercab): a riposo bordo blu e scritta blu;
// durante l'ascolto pieno blu con il conto alla rovescia (4.5 s -> 0.0 s). Un clic durante
// l'ascolto lo annulla.
class LearnButton final : public juce::Component
{
public:
    explicit LearnButton (BFocusProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.toggleLearn(); repaint(); }   // un secondo clic annulla
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    BFocusProcessor& proc;
};

// Risposta in frequenza attuale della rete (calcolata dai moltiplicatori in uso nel motore audio).
class ResponseDisplay final : public juce::Component
{
public:
    explicit ResponseDisplay (BFocusProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    float getScaleLeft() const;
    float getXLabelBaselineFromBottom() const { return (float) (kBottomMargin - 12) - ui::capHeight (ui::kAxisSize, ui::Face::MonoRegular) * 0.5f; }
    static constexpr int kInner = 6;

private:
    juce::Rectangle<int> plotArea() const;
    BFocusProcessor& proc;
    static constexpr int kLeftMargin = 34, kBottomMargin = 20, kTopMargin = 8;
};

class BFocusEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BFocusEditor (BFocusProcessor&);
    ~BFocusEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> box() const;

    BFocusProcessor& proc;
    ResponseDisplay display;
    ReductionMeter meter;
    LearnButton learnButton;
    bool wasLearning = false;
    juce::String shownStatus;
    ui::ValueBox thresholdBox, strengthBox, releaseBox;

    struct TextItem { juce::String text; juce::Rectangle<int> area; };
    std::vector<TextItem> labels;
    juce::Rectangle<int> modeArea, meterValueArea;
    float meterDb = 0.0f;
    juce::int64 lastSignature = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BFocusEditor)
    ui::AboutOverlay about { *this, "focus" };   // clic sul logo: pannello about (ultimo membro: sta sopra a tutto)
};

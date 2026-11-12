#pragma once
// Interfaccia b-drop (accento viola):
//   b-drop                        detune [■]  range [shift|octave|whammy]  zero-lag pick [■]
//   ┌───────────────────────────────────────────────────────────────────────────────┐
//     tension < 65 % >  mix < 100 % >                                                 mono
//     a sinistra il rullo dei semitoni (si trascina in verticale, scatta di un semitono alla volta; più ci si
//     allontana da 0 più si accende la barra accanto e il numero prende il colore del plugin), a destra le due
//     forme d'onda: grigio = DI in ingresso, viola = uscita trasposta, con la qualità dell'aggancio di fase
//   └───────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class DropDisplay final : public juce::Component
{
public:
    explicit DropDisplay (BDropProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int pickGlow = 0;
    static constexpr float kSlot = 56.0f, kStepPx = 22.0f;   // altezza di un numero sul rullo, pixel di trascinamento per semitono
private:
    juce::Rectangle<float> zone() const { return getLocalBounds().removeFromLeft (200).toFloat(); }
    juce::Rectangle<float> drum() const;
    bool locked() const;   // octave: fisso a -12
    void setSemitones (int v);
    BDropProcessor& proc;
    bool dragging = false;
    int startValue = 0;
    float startY = 0, nudge = 0, wheelAcc = 0;
};

class BDropEditor final : public ui::ShellEditor
{
public:
    explicit BDropEditor (BDropProcessor&);
private:
    void tick() override
    {
        if (proc.pickFlash.exchange (false)) display.pickGlow = 6;
        else if (display.pickGlow > 0) --display.pickGlow;
        display.repaint();
    }
    BDropProcessor& proc;
    DropDisplay display;
};

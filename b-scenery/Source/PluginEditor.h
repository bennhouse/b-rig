#pragma once
// Interfaccia b-scenery (accento bianco ghiaccio): riga 1 texture gain, react, balance, rete 50/60 Hz;
// righe 2-3 i quattro scenari (on/off + livello). Grafico a scorrimento (~5,5 s) su una sola scala dBFS:
//   - chitarra: superficie grigia dal basso
//   - texture: linea bianca; la linea tratteggiata è dove starebbe senza react, e la fascia tra le due è il
//     ducking inverso (più suoni forte, più la fascia si apre)
// A destra quattro barre verticali: quanto suona adesso ogni layer (vinyl, hum, rain, lo-fi).
#include "PluginProcessor.h"
#include "smp/Shell.h"

class BalanceButton final : public juce::Component
{
public:
    explicit BalanceButton (BSceneryProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.toggleBalance(); repaint(); }
private:
    BSceneryProcessor& proc;
};

class SceneryView final : public juce::Component
{
public:
    explicit SceneryView (BSceneryProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    void paintStatic (juce::Graphics&);
    juce::Rectangle<float> plot() const;
    juce::Rectangle<float> meters() const;
    BSceneryProcessor& proc;
    ui::LayerCache bg;
};

class BSceneryEditor final : public ui::ShellEditor
{
public:
    explicit BSceneryEditor (BSceneryProcessor&);
private:
    void tick() override { view.repaint(); balance.repaint(); }
    BalanceButton balance;
    SceneryView view;
};

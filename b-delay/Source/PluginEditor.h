#pragma once
// Interfaccia b-delay (accento indaco):
//   b-delay                                         type [digital|tape|bbd|lofi|reverse]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     time < 380 ms >  sync [■]  [tap]  feedback < 45 % >  locut < 120 Hz >  hicut < 6.00 kHz >  mix < 30 % >
//     offset < 0 % >    ┌ timeline 0-2000 ms: sopra L, sotto R; i tap previsti ┐   smear < 0 % >
//     ping-pong [■]     │ (posizione da time / offset / ping-pong, altezza dal │   duck < 0 % >
//     mod < 20 % >      └ feedback, larghezza dallo smear) e il suono che scorre┘  output < 0.0 dB >  trails [■]
//   └──────────────────────────────────────────────────────────────────────────────────┘
// Con sync acceso il riquadro del tempo mostra la figura (1/8D, 1/4T...) invece dei millisecondi.
#include "PluginProcessor.h"
#include "smp/Shell.h"

class TimeSlot final : public juce::Component   // tempo in ms o figura, nello stesso posto
{
public:
    explicit TimeSlot (BDelayProcessor&);
    void resized() override { ms.setBounds (getLocalBounds()); div.setBounds (getLocalBounds()); }
    void update();
    int idealWidth() const { return juce::jmax (ms.getIdealWidth(), div.getIdealWidth()); }
    ui::ValueBox ms, div;
private:
    BDelayProcessor& proc;
};

class TapButton final : public juce::Component
{
public:
    std::function<void()> onTap;
    int flash = 0;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { flash = 6; if (onTap) onTap(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

class DelayGraph final : public juce::Component
{
public:
    explicit DelayGraph (BDelayProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BDelayProcessor& proc;
};

class BDelayEditor final : public ui::ShellEditor
{
public:
    explicit BDelayEditor (BDelayProcessor&);
private:
    void tick() override
    {
        timeSlot.update();
        if (tapBtn.flash > 0) { --tapBtn.flash; tapBtn.repaint(); }
        graph.repaint();
    }
    TimeSlot timeSlot;
    TapButton tapBtn;
    DelayGraph graph;
};

#pragma once
// Interfaccia b-delay (accento indaco). Gerarchia: in alto il ritmo (tempo, ripetizioni) e quanto se ne sente,
// a sinistra il colore delle ripetizioni, a destra stereo e uscita; i modi (trails, ping-pong, tipo) nell'intestazione.
//   b-delay                         trails [■]  ping-pong [ ]  type [digital|tape|bbd|lofi|reverse]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     time < 380 ms >  sync [■]  [tap]  feedback < 45 % >  mix < 30 % >                          stereo
//     locut < 120 Hz >   ┌ timeline: sopra L, sotto R; i tap previsti (posizione  ┐  offset < 0 % >
//     hicut < 6.00 kHz > │ da time / offset / ping-pong, altezza dal feedback,     │  duck < 0 % >
//     mod < 20 % >       │ larghezza dallo smear) e il suono che scorre; la finestra│  output < 0.0 dB >
//     smear < 0 % >      └ si adatta al tempo (250 ms ... 2 s)                     ┘
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

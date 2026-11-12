#pragma once
// Interfaccia b-delay (accento indaco). Tre livelli, dal più importante:
//   1. in grande, in alto: time (ms, o la figura con sync), feedback e mix, con i pulsanti sync e tap;
//   2. la timeline: le ripetizioni previste e il suono che scorre (la finestra si adatta al tempo);
//   3. sotto, i gruppi con il loro titolo; quelli facoltativi hanno l'on/off accanto al titolo:
//        tone | [■] modulation | [ ] smear | [ ] duck | stereo | output
//   b-delay                                            type [digital|tape|bbd|lofi|reverse]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     time         [sync] [tap]     feedback                                     mix
//     380 ms                        45 %                                         30 %
//     ▬▬▬▬                          ▬▬▬▬                                         ▬▬▬▬
//     ┌ timeline ────────────────────────────────────────────────────────────────┐
//     └──────────────────────────────────────────────────────────────────────────┘
//     tone          │ [■] modulation │ [ ] smear   │ [ ] duck    │ stereo      │ output
//     locut 120 Hz  │ depth 20 %     │ amount 40 % │ amount 50 % │ offset 0 %  │ output 0.0 dB
//     hicut 6 kHz   │                │             │             │ [ping-pong] │ [trails]
//   └──────────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

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
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void tick() override;
    BDelayProcessor& proc;
    DelayGraph graph;
    ui::BigValue time, division, feedback, mix;   // time e division nello stesso posto: si vede quello del modo sync
    ui::TextToggle sync { "sync" }, pingpong { "ping-pong" }, trails { "trails" };
    TapButton tapBtn;
    ui::ValueBox locut, hicut, mod, smear, duck, offset, output;
    ui::Section toneSec, modSec, smearSec, duckSec, stereoSec, outSec;
    std::array<ui::Section*, 6> sections { &toneSec, &modSec, &smearSec, &duckSec, &stereoSec, &outSec };
    std::vector<int> separators;
};

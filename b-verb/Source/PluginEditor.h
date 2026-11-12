#pragma once
// Interfaccia b-verb (accento ciano). Tre livelli, dal più importante:
//   1. in grande, in alto: size, decay, predelay e mix (numero grande, barra colorata), con il pulsante freeze;
//   2. il grafico: prime riflessioni, decadimento del corpo e delle alte, livelli IN / WET;
//   3. sotto, i gruppi con il loro titolo; quelli facoltativi hanno l'on/off accanto al titolo:
//        [■] early | tone | [■] modulation | [ ] duck | output
//   b-verb                                                   type [room|plate|hall|spring|shimmer]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     size      decay      predelay      [freeze]                                  mix
//     65 %      2.40 s     20.0 ms                                                 25 %
//     ▬▬▬▬      ▬▬▬▬       ▬▬▬▬                                                    ▬▬▬▬
//     ┌ grafico ─────────────────────────────────────────────────────────────────┐
//     └──────────────────────────────────────────────────────────────────────────┘
//     [■] early  │ tone          │ [■] modulation │ [ ] duck     │ output
//     level 50 % │ damp 6.50 kHz │ depth 30 %     │ amount 50 %  │ width 100 %
//                │ locut 100 Hz  │                │              │ output 0.0 dB
//                │ hicut 10 kHz  │                │              │
//   └──────────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class VerbGraph final : public juce::Component
{
public:
    explicit VerbGraph (BVerbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BVerbProcessor& proc;
};

class BVerbEditor final : public ui::ShellEditor
{
public:
    explicit BVerbEditor (BVerbProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void tick() override { graph.repaint(); for (auto* sec : sections) sec->refresh(); }
    VerbGraph graph;
    ui::BigValue size, decay, predelay, mix;
    ui::TextToggle freeze { "freeze" };
    ui::ValueBox early, damp, locut, hicut, mod, duck, width, output;
    ui::Section earlySec, toneSec, modSec, duckSec, outSec;
    std::array<ui::Section*, 5> sections { &earlySec, &toneSec, &modSec, &duckSec, &outSec };
    std::vector<int> separators;
};

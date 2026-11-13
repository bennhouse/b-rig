#pragma once
// Interfaccia b-comp (accento lime). Tre livelli, come b-verb e b-delay:
//   1. in grande, in alto: threshold, ratio, attack, release e il pulsante auto gain;
//   2. il grafico: a sinistra la curva del compressore (ingresso -> uscita, con il ginocchio e il punto dove si trova il
//      suono adesso), a destra gli ultimi secondi: ingresso (grigio), uscita (lime) e la riduzione che scende dall'alto;
//      con lo stile multi, accanto, la riduzione di ognuna delle tre bande;
//   3. sotto, i gruppi: detector | [■] parallel | multiband (solo con multi) | output
//   b-comp                                            style [clean|opto|fet|squash|multi]
#include "PluginProcessor.h"
#include "smp/Shell.h"

class CompGraph final : public juce::Component
{
public:
    explicit CompGraph (BCompProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BCompProcessor& proc;
};

class BCompEditor final : public ui::ShellEditor
{
public:
    explicit BCompEditor (BCompProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void tick() override;
    BCompProcessor& proc;
    CompGraph graph;
    ui::BigValue threshold, ratio, attack, release;
    ui::TextToggle autoGain { "auto gain" };
    ui::ValueBox knee, scHp, mix, low, mid, high, makeup, output;
    ui::Section detSec, parSec, multiSec, outSec;
    std::array<ui::Section*, 4> sections { &detSec, &parSec, &multiSec, &outSec };
    std::vector<int> separators;
};

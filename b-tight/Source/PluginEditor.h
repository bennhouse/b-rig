#pragma once
// Interfaccia b-tight (accento lime):
//   b-tight                                tuning [standard|drop|baritone]
//   ┌───────────────────────────────────────────────────────────────────┐
//     clamp < -24.0 dB >  auto [■]  bite < 0 % >  push < 0.0 dB >      mono
//     forma d'onda scorrevole: riduzione sulle basse (scuro, dall'alto),
//     attacchi esaltati (barre lime), uscita (inviluppo)
//   └───────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class TightScope final : public juce::Component
{
public:
    explicit TightScope (BTightProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BTightProcessor& proc;
};

class BTightEditor final : public ui::ShellEditor
{
public:
    explicit BTightEditor (BTightProcessor&);
private:
    void tick() override
    {
        scope.repaint();
        if (gainBox != nullptr) gainBox->setEnabled (state.getRawParameterValue (BTightIDs::autoGain)->load() < 0.5f);   // con autogain è calcolato
    }
    TightScope scope;
    ui::ValueBox* gainBox = nullptr;
};

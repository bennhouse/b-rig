#pragma once
// Interfaccia b-wire (accento indaco): due righe di controlli e un oscilloscopio a gradini con la quadra
// principale e le due sub-ottave sovrapposte; la frequenza agganciata dal PLL in basso a destra.
#include "PluginProcessor.h"
#include "smp/Shell.h"

class WireScope final : public juce::Component
{
public:
    explicit WireScope (BWireProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BWireProcessor& proc;
};

class BWireEditor final : public ui::ShellEditor
{
public:
    explicit BWireEditor (BWireProcessor&);
private:
    void tick() override
    {
        scope.repaint();
        if (levelBox != nullptr) levelBox->setEnabled (state.getRawParameterValue (BWireIDs::levelAuto)->load() < 0.5f);   // con auto il level è calcolato
    }
    WireScope scope;
    ui::ValueBox* levelBox = nullptr;
};

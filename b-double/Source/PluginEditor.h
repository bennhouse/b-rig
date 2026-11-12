#pragma once
// Interfaccia b-double (accento ciano):
//   b-double                                   mode [hard-pan|center-wide]
//   ┌───────────────────────────────────────────────────────────────────┐
//     double [x]  delay < 16.0 ms >  detune < 6.0 ct >  dynamic [ ]       mono > stereo
//     tightness < 50 % >  variation < 50 % >  mono-safe < 0 % >
//     DELAY 18.3 ms        goniometro (Lissajous)        DETUNE +4.2 cents
//   └───────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class DoubleScope final : public juce::Component
{
public:
    explicit DoubleScope (BDoubleProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BDoubleProcessor& proc;
};

class BDoubleEditor final : public ui::ShellEditor
{
public:
    explicit BDoubleEditor (BDoubleProcessor&);
private:
    void tick() override
    {
        scope.repaint();
        if (tightBox != nullptr) tightBox->setEnabled (state.getRawParameterValue (BDoubleIDs::dynamic)->load() > 0.5f);   // il jitter esiste solo in dynamic
    }
    DoubleScope scope;
    ui::ValueBox* tightBox = nullptr;
};

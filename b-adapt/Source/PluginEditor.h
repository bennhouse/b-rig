#pragma once
// Interfaccia b-adapt (accento magenta):
//   b-adapt                                         monitor [off|phone|laptop]
//   ┌───────────────────────────────────────────────────────────────────┐
//     safe width < 50 % >  thump < 0 % >  de-harsh < 0 % >              stereo
//     curva composita (thump, taglio dinamico a 3,4 kHz, codec shield, monitor)
//     correlazione L/R in basso a destra
//   └───────────────────────────────────────────────────────────────────┘
// Con il monitor acceso il riquadro diventa verde e compare l'avviso di bypass prima dell'export.
#include "PluginProcessor.h"
#include "smp/Shell.h"

class AdaptDisplay final : public juce::Component
{
public:
    explicit AdaptDisplay (BAdaptProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update() { visible = spectrum.update (proc.spectrumTap, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0, 30.0f, -72.0f); }
private:
    BAdaptProcessor& proc;
    ui::SpectrumAnalyser spectrum;
    bool visible = false;
};

class BAdaptEditor final : public ui::ShellEditor
{
public:
    explicit BAdaptEditor (BAdaptProcessor&);
    ~BAdaptEditor() override { proc.spectrumActive.store (false); }
    void paintOverChildren (juce::Graphics&) override;
    static inline const juce::Colour kAudition { 0xff10b981 };
private:
    void tick() override;
    BAdaptProcessor& proc;
    AdaptDisplay display;
};

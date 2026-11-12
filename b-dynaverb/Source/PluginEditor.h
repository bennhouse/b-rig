#pragma once
// Interfaccia b-dynaverb (accento oro):
//   b-dynaverb                    crossover [low-mid|mid|high-neck]  duck delay [■]  sync [■]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     split < 320 Hz >  boost < +3.5 dB >  delay < 380 ms >  space < 35 % >  speed < 25 ms >  stereo
//     barra divisa in due: RHYTHM ZONE (grigio, a sinistra) e LEAD ZONE (oro, a destra), con il cursore
//     che si sposta mentre suoni e la sua scia; sotto, quanto è acceso il canale lead
//   └──────────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class ZoneMeter final : public juce::Component
{
public:
    explicit ZoneMeter (BDynaverbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    BDynaverbProcessor& proc;
};

class BDynaverbEditor final : public ui::ShellEditor
{
public:
    explicit BDynaverbEditor (BDynaverbProcessor&);
private:
    void tick() override { meter.repaint(); }
    ZoneMeter meter;
};

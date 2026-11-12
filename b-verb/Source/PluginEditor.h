#pragma once
// Interfaccia b-verb (accento ciano):
//   b-verb                                          type [room|plate|hall|spring|shimmer]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     decay < 2.40 s >  predelay < 20.0 ms >  size < 65 % >  locut < 100 Hz >  hicut < 10.0 kHz >  mix < 25 % >
//     early < 50 % >   ┌ grafico: tap delle prime riflessioni, inviluppo ┐   duck < 0 % >
//     damp < 6.50 kHz >│ del decadimento (corpo e alte), ingresso e wet   │   stereo < 100 % >
//     mod < 30 % >     └ dal vivo, 0...-60 dB                              ┘   freeze [■]  output < 0.0 dB >
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
private:
    void tick() override { graph.repaint(); }
    VerbGraph graph;
};

#pragma once
// Interfaccia b-verb (accento ciano). Gerarchia: in alto lo spazio e quanto se ne sente, ai lati il colore della
// coda e l'uscita; i due modi (freeze, tipo) nell'intestazione.
//   b-verb                                     freeze [■]  type [room|plate|hall|spring|shimmer]
//   ┌──────────────────────────────────────────────────────────────────────────────────┐
//     size < 65 % >  decay < 2.40 s >  predelay < 20.0 ms >  early < 50 % >  mix < 25 % >          stereo
//     damp < 6.50 kHz >  ┌ grafico: prime riflessioni, inviluppo del decadimento  ┐  width < 100 % >
//     locut < 100 Hz >   │ (corpo e alte, area piena), livelli IN e WET dal vivo   │  duck < 0 % >
//     hicut < 10.0 kHz > │ a destra, 0...-60 dB                                    │  output < 0.0 dB >
//     mod < 30 % >       └─────────────────────────────────────────────────────────┘
//   └──────────────────────────────────────────────────────────────────────────────────┘
//   colonna sinistra = tono e movimento della coda; colonna destra = immagine stereo, dinamica, livello.
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

#pragma once
// Interfaccia b-freshstrings (accento rame):
//   b-freshstrings
//   ┌──────────────────────────────────────────────────────────────────────────────────────┐
//     pick [nylon|tortex|carbon|metal]  gauge [.009|.010|.011|heavy]  pickup [bridge|both|neck]  mono
//     freshness < 50 % >  attack < +2.0 dB >  tension < 50 % >  output < 0.0 dB >
//     in alto la corda che vibra (più brillante sugli attacchi), sotto la curva del pickup e della corda
//     con lo scaffale "fresh" che si apre sull'attacco
//   └──────────────────────────────────────────────────────────────────────────────────────┘
#include "PluginProcessor.h"
#include "smp/Shell.h"

class FreshDisplay final : public juce::Component
{
public:
    explicit FreshDisplay (BFreshstringsProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    float phase = 0.0f;
private:
    BFreshstringsProcessor& proc;
};

class BFreshstringsEditor final : public ui::ShellEditor
{
public:
    explicit BFreshstringsEditor (BFreshstringsProcessor&);
private:
    void tick() override
    {
        // la corda si muove solo mentre suoni: velocità e ampiezza dal livello della DI, ferma nel silenzio
        const float lvl = proc.levelNow.load();
        if (lvl > 0.003f) display.phase += 0.35f + 0.9f * juce::jmin (1.0f, lvl * 4.0f);
        display.repaint();
    }
    BFreshstringsProcessor& proc;
    FreshDisplay display;
};

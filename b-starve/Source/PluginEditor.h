#pragma once
// Interfaccia b-starve (accento corallo): in alto output; al centro 4 card (STARVE, MOSH,
// CORRUPT, MELT) con on/off, intensity, volume, mix e un mini visualizzatore della forma d'onda dopo l'effetto.
// Alla prima accensione di un effetto la sua card mostra "auto level..." mentre il volume si calibra.
#include "PluginProcessor.h"
#include "smp/Shell.h"

class StarveCard final : public juce::Component
{
public:
    StarveCard (BStarveProcessor&, int effect);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::Rectangle<int> inner() const;
    BStarveProcessor& proc;
    int fx;
    ui::PixelCheckbox onBox;
    ui::ValueBox intBox, volBox, mixBox;
};

class StarveCards final : public juce::Component
{
public:
    explicit StarveCards (BStarveProcessor& p) { for (int e = 0; e < 4; ++e) { cards[(size_t) e] = std::make_unique<StarveCard> (p, e); addAndMakeVisible (*cards[(size_t) e]); } }
    void resized() override;
private:
    std::array<std::unique_ptr<StarveCard>, 4> cards;
};

class BStarveEditor final : public ui::ShellEditor
{
public:
    explicit BStarveEditor (BStarveProcessor&);
private:
    void tick() override { cards.repaint(); }
    StarveCards cards;
};

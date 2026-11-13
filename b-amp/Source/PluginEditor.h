#pragma once
// Interfaccia b-amp "borderless": nessun riquadro, sfondo continuo #0D0D0F. In alto a sinistra il titolo, a destra
// [ clean | crunch | lead ], [ 2x | 4x ] e il diapason del tuner. Al centro la silhouette frontale della testata
// attiva (griglia e colore d'accento diversi per canale, bagliore interno proporzionale alla saturazione) con dentro
// La testata (solo corpo e griglia, simmetrica) occupa il 50 % della larghezza; sotto, tre righe di controlli:
// noise gate [x] + soglia + boost (off / vol / vol + eq, un solo pulsante a rotazione; con vol + eq accanto c'è tone)
// gain, tight | bass, mid, treble | presence, resonance / (a destra) master, level.
// Il tuner copre tutta la finestra: nota gigante, Hz, ago/strobe, [mute] (acceso a ogni apertura); un clic in un punto libero lo chiude.
// Testata: maniglia e piedini uguali per i tre ampli; un clic sulla spia la gira e mostra il retro (valvole dalla fessura).
#include "PluginProcessor.h"
#include "smp/Widgets.h"

class AmpHead final : public juce::Component
{
public:
    explicit AmpHead (BAmpProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void resized() override { statics.invalidate(); cover.invalidate(); }
    // clic sulla spia: la testata si gira e mostra il retro; un clic sul retro la rigira
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::Rectangle<float> bodyArea() const;    // corpo (senza maniglia e piedini)
    juce::Rectangle<float> grilleArea() const;
    juce::Rectangle<float> ledArea() const;
    void showFront() { if (back) { back = false; repaint(); } }
    void showBack() { back = true; repaint(); }   // test
    float glow = 0;
    static constexpr float kHandleH = 14.0f, kFeetH = 7.0f;
private:
    void paintStatic (juce::Graphics&, int channel);
    void paintBackStatic (juce::Graphics&, int channel);
    void paintBackCover (juce::Graphics&, int channel);   // pareti / griglie che coprono metà delle valvole
    juce::Rectangle<float> backSlot() const { return grilleArea().reduced (20.0f, 0.0f).withSizeKeepingCentre (grilleArea().getWidth() - 40.0f, 136.0f); }   // apertura alta quanto le valvole + chassis
    void paintCabinet (juce::Graphics&, int channel);   // contorno, maniglia e piedini: uguali davanti e dietro
    std::vector<juce::Rectangle<float>> tubeRects (int channel) const;   // valvole viste dalla fessura del retro
    bool back = false;
    BAmpProcessor& proc;
    ui::LayerCache statics, cover;
};

class TunerOverlay final : public juce::Component
{
public:
    explicit TunerOverlay (BAmpProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void analyse();   // 60 fps: legge il FIFO, MPM
    std::function<void()> onClose;
    float hz = 0, cents = 0, shownCents = 0, clarity = 0; int midi = -1;
private:
    juce::Rectangle<int> muteArea() const { return { getWidth() / 2 - 40, getHeight() - 70, 80, ui::kControlH }; }
    BAmpProcessor& proc;
    std::array<float, 4096> win {}; int wpos = 0, filled = 0;
};

class SmallButton final : public juce::Component
{
public:
    std::function<void()> onClick; std::function<bool()> isOn; juce::String caption;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

class BAmpEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BAmpEditor (BAmpProcessor&);
    ~BAmpEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::Colour channelColour (int ch);
private:
    void timerCallback() override;
    void layoutControls();
    BAmpProcessor& proc;
    AmpHead head;
    TunerOverlay tuner;
    ui::SegmentedChoice channelChoice, osChoice;
    SmallButton tunerBtn, boostBtn;
    ui::ValueBox toneBox;   // tono del boost: si vede solo con vol + eq
    juce::Rectangle<int> toneLabel;
    std::array<ui::ValueBox, 10> boxes;
    static constexpr const char* kLabels[10] = { "threshold", "tight", "gain", "bass", "mid", "treble", "presence", "resonance", "master", "level" };
    std::array<juce::Rectangle<int>, 10> labelAreas {};
    ui::PixelCheckbox gateCheck; juce::Rectangle<int> gateLabel, boostLabel, osLabel;
    std::array<juce::Point<int>, 2> groupDividers {};
    int shownChannel = -1; juce::int64 lastSig = 0;
    ui::AboutOverlay about { *this, "amp" };   // clic sul logo (ultimo membro: sta sopra a tutto)
};

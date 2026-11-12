#pragma once
// Controlli specifici di b-aftercab (tasto tempo del delay, pulsante smart phase).
#include "smp/Widgets.h"
#include "../PluginProcessor.h"

namespace ui
{

// Tempo del delay, a sinistra: riquadro BPM come gli altri (frecce, trascinamento, rotella, barra di riempimento).
//   clic singolo   -> sync on/off (sync acceso: pieno giallo con "sync"; spento: grigio con i BPM in giallo)
//   doppio clic    -> veloce (< 250 ms): i BPM si scrivono a tastiera (il primo clic non conta)
//   trascinare / frecce / rotella -> BPM (e sync spento)
class TempoBox final : public juce::Component
{
public:
    explicit TempoBox (BAftercabProcessor& p);
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void enablementChanged() override { repaint(); }
private:
    juce::Rectangle<int> boxArea() const;
    void setBpm (float bpm);
    BAftercabProcessor& proc;
    juce::RangedAudioParameter* bpm = nullptr;
    InlineEditor editor;
    bool dragging = false, moved = false, syncBeforeClick = true;
    float dragStart = 120.0f;
    double lastClickMs = -1.0e9;
};

// Tasto tap, a destra: giallo con testo nero quando il sync è spento; oscurato (inattivo) col sync acceso.
class TapButton final : public juce::Component
{
public:
    explicit TapButton (BAftercabProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (isEnabled() && ! proc.isSync()) { proc.tapPressed(); repaint(); } }
    void enablementChanged() override { repaint(); }
private:
    BAftercabProcessor& proc;
};

// Uscita (OUT), a destra del riquadro EQ: barra verticale (minimo in basso, massimo in alto)
// con il valore sotto e il tasto "auto" (acceso di default). Con auto acceso la barra mostra la
// compensazione automatica; trascinarla, usare la rotella o scrivere un valore la spegne.
// Doppio clic sulla barra: torna al default (auto) e apre la modifica a tastiera.
class OutputSection final : public juce::Component
{
public:
    explicit OutputSection (BAftercabProcessor&);
    static int getIdealWidth();
    // geometria in coordinate locali, impostata dall'editor
    juce::Rectangle<int> barArea, autoArea;
    float tagBaseline = 0.0f, valueBaseline = 0.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void setManual (float db);          // spegne auto e imposta il guadagno
    void setAuto (bool on);
    BAftercabProcessor& proc;
    juce::RangedAudioParameter* gain = nullptr;
    juce::RangedAudioParameter* autoP = nullptr;
    InlineEditor editor;
    bool dragging = false;
    float dragStartNorm = 0.0f;
};

// Pulsante "smart phase": quadrato giallo con un "=" ritagliato.
class SmartPhaseButton final : public juce::Component
{
public:
    explicit SmartPhaseButton (BAftercabProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    BAftercabProcessor& proc;
};

} // namespace ui

#pragma once
#include "smp/Theme.h"
#include "smp/Spectrum.h"
#include "../PluginProcessor.h"

namespace ui
{

// Griglia EQ: asse X logaritmico 20 Hz - 20 kHz, asse Y ±10 dB (±4 in "precision").
// 5 punti banda trascinabili:
//   - trascina           -> frequenza (orizzontale) e guadagno (verticale)
//   - rotella            -> Q (larghezza)
//   - clic singolo       -> banda spenta / accesa, dopo il tempo del doppio clic (così il punto non cambia sotto al
//                           mouse tra il primo e il secondo clic)
//   - doppio clic        -> banda ai valori di default (freq, gain, q, tipo); acceso/spento resta com'era
//   - trascinamento      -> solo frequenza e guadagno: oltre 4 px è un trascinamento, mai un clic
//   - clic destro (1, 5) -> alterna shelf / taglio
class EqDisplay final : public juce::Component
{
public:
    explicit EqDisplay (BAftercabProcessor&);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Descrizione della banda sotto il mouse (mostrata sopra la griglia), vuota se nessuna.
    juce::String getInfoText() const;
    // test: esegue subito il clic singolo in attesa
    void flushClickForTest() { clickTimer.stopTimer(); applyPendingToggle(); }

    // x (in coordinate locali) del bordo sinistro dell'inchiostro della scala dB (+10 / -10).
    float getScaleLeft() const;
    // distanza della linea di base delle etichette di frequenza dal fondo del componente
    float getXLabelBaselineFromBottom() const { return (float) (kBottomMargin - 12) - capHeight (kAxisSize, Face::MonoRegular) * 0.5f; }
    // Mappatura frequenza <-> x (pubbliche per i test).
    float xForFreq (double hz) const;
    double freqForX (float x) const;
    // Guadagno (dB) disegnato dalla curva a una frequenza: è la stessa funzione usata per il suono.
    double responseDb (double hz) const;

    // y (locali) delle righe +max e -max della griglia: la barra OUT si allinea a queste.
    juce::Range<float> getGridYRange() const { const auto a = innerArea(); return { a.getY(), a.getBottom() }; }
    // x (in coordinate locali) del bordo destro della griglia.
    std::function<void()> onInfoChanged;

    static inline const juce::Identifier precisionId { "precision" };
    static inline const juce::Identifier selectedId { "selected_band" };

    // Spettro dal vivo (dopo l'EQ): da chiamare dal timer dell'interfaccia; ridisegna solo se serve.
    void updateSpectrum (float fps);

    // Punto selezionato (l'ultimo cliccato): i suoi controlli stanno sotto la griglia.
    int getSelected() const;
    void setSelected (int band);
    std::function<void (int)> onSelectionChanged;

private:
    struct Band
    {
        juce::RangedAudioParameter* type = nullptr;   // solo bande 1 e 5
        juce::RangedAudioParameter* on = nullptr;
        juce::RangedAudioParameter* freq = nullptr;
        juce::RangedAudioParameter* gain = nullptr;
        juce::RangedAudioParameter* q = nullptr;
    };

    juce::Rectangle<int> plotArea() const;
    juce::Rectangle<float> innerArea() const;
    float range() const;                       // 10 o 4 dB
    float yForGain (double db) const;
    double gainForY (float y) const;
    static float get (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
    static void set (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    bool isCut (int b) const;
    bool isOn (int b) const;
    static bool isBell (int b);
    double responseDbWith (double hz, const std::array<sp::BiquadCoeffs, BAftercabProcessor::kNumBands>&, double sr) const;
    juce::Point<float> pointFor (int b) const;
    int bandAt (juce::Point<float>) const;
    void setHover (int b);

    BAftercabProcessor& proc;
    std::array<Band, BAftercabProcessor::kNumBands> bands;
    juce::RangedAudioParameter* eqOn = nullptr;
    int hover = -1, dragging = -1;
    SpectrumAnalyser spectrum;
    // curva di risposta in cache: si ricalcola solo se cambiano coefficienti, modo, scala o dimensioni
    juce::Path cachedCurve;
    std::array<sp::BiquadCoeffs, BAftercabProcessor::kNumBands> cachedCoeffs {};
    juce::Rectangle<int> cachedArea;
    float cachedRange = 0.0f;
    int cachedVersion = -1;
    bool cachedLinear = false, cachedValid = false;
    bool spectrumVisible = false;
    double dragStartFreq = 0.0, dragStartGain = 0.0;
    bool moved = false;
    int clickBand = -1, pendingToggle = -1;
    juce::TimedCallback clickTimer { [this] { applyPendingToggle(); } };
    void applyPendingToggle();
    void cancelPendingToggle() { clickTimer.stopTimer(); pendingToggle = -1; }
    static constexpr float kDragThresholdPx = 4.0f;
    void setParam (juce::RangedAudioParameter* p, float normalised);

    static constexpr int kLeftMargin = 30, kBottomMargin = 20, kTopMargin = 20, kPointSize = 8, kPad = 8;
};

} // namespace ui

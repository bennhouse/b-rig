#include "EqDisplay.h"

namespace ui
{

namespace
{
    constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
}

EqDisplay::EqDisplay (BAftercabProcessor& p) : proc (p)
{
    auto& s = p.apvts;
    for (int b = 0; b < BAftercabProcessor::kNumBands; ++b)
    {
        auto& band = bands[(size_t) b];
        if (b == 0 || b == BAftercabProcessor::kNumBands - 1)
            band.type = s.getParameter (ParamIDs::band (b, "type"));
        band.on   = s.getParameter (ParamIDs::band (b, "on"));
        band.freq = s.getParameter (ParamIDs::band (b, "freq"));
        band.gain = s.getParameter (ParamIDs::band (b, "gain"));
        band.q    = s.getParameter (ParamIDs::band (b, "q"));
    }
    eqOn = s.getParameter (ParamIDs::eqOn);
}

// ---------------------------------------------------------------- selezione
int EqDisplay::getSelected() const
{
    return juce::jlimit (0, BAftercabProcessor::kNumBands - 1, (int) proc.apvts.state.getProperty (selectedId, 2));
}

void EqDisplay::setSelected (int b)
{
    if (b == getSelected()) return;
    proc.apvts.state.setProperty (selectedId, b, nullptr);
    repaint();
    if (onSelectionChanged) onSelectionChanged (b);
}

void EqDisplay::updateSpectrum (float fps)
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const bool visible = spectrum.update (proc.spectrumTap, sr, fps, -84.0f);
    if (visible || spectrumVisible) repaint();   // in silenzio, a spettro sparito, nessun ridisegno
    spectrumVisible = visible;
}

bool EqDisplay::isOn (int b) const { return bands[(size_t) b].on->getValue() > 0.5f; }

// ---------------------------------------------------------------- geometria
juce::Rectangle<int> EqDisplay::plotArea() const
{
    return getLocalBounds().withTrimmedLeft (kLeftMargin).withTrimmedBottom (kBottomMargin).withTrimmedTop (kTopMargin).reduced (2, 0);
}

float EqDisplay::range() const
{
    return proc.getUiFlag (precisionId) ? 4.0f : BAftercabProcessor::kMaxEqGainDb;
}

// Area di mappatura: il grafico ridotto di un padding, così i punti ai limiti
// (20 Hz, 20 kHz, ±max dB) restano interi dentro al riquadro.
juce::Rectangle<float> EqDisplay::innerArea() const
{
    return plotArea().toFloat().reduced ((float) kPad);
}

// Asse X logaritmico "a tratti": ogni decade resta logaritmica, ma 1k-10k è più
// larga (43% invece del 33%) e 100-1k più stretta (27%), per lavorare meglio
// sulle medio-alte. La mappa è continua e invertibile.
namespace
{
    constexpr double kAnchorHz[]  = { 20.0, 100.0, 1000.0, 10000.0, 20000.0 };
    constexpr double kAnchorPos[] = { 0.0,  0.20,  0.47,   0.90,    1.0 };

    double posForFreq (double hz)
    {
        hz = juce::jlimit (kMinHz, kMaxHz, hz);
        int i = 0;
        while (i < 3 && hz > kAnchorHz[i + 1]) ++i;
        const double t = std::log (hz / kAnchorHz[i]) / std::log (kAnchorHz[i + 1] / kAnchorHz[i]);
        return kAnchorPos[i] + t * (kAnchorPos[i + 1] - kAnchorPos[i]);
    }

    double freqForPos (double pos)
    {
        pos = juce::jlimit (0.0, 1.0, pos);
        int i = 0;
        while (i < 3 && pos > kAnchorPos[i + 1]) ++i;
        const double t = (pos - kAnchorPos[i]) / (kAnchorPos[i + 1] - kAnchorPos[i]);
        return kAnchorHz[i] * std::pow (kAnchorHz[i + 1] / kAnchorHz[i], t);
    }
}

float EqDisplay::xForFreq (double hz) const
{
    const auto a = innerArea();
    return a.getX() + a.getWidth() * (float) posForFreq (hz);
}

double EqDisplay::freqForX (float x) const
{
    const auto a = innerArea();
    return freqForPos ((double) ((x - a.getX()) / a.getWidth()));
}

// Risposta totale dell'EQ (dB) a una frequenza.
double EqDisplay::responseDb (double hz) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    return responseDbWith (hz, proc.computeEqCoeffs (sr), sr);
}

double EqDisplay::responseDbWith (double hz, const std::array<sp::BiquadCoeffs, BAftercabProcessor::kNumBands>& coeffs, double sr) const
{
    // in linear phase la curva è la risposta misurata del filtro FIR realmente in uso
    double linDb = 0.0;
    if (eqOn->getValue() > 0.5f && proc.getLinearPhaseResponseDb (hz, linDb))
        return linDb;

    // altrimenti: gli stessi coefficienti biquad usati dal motore audio
    const double w = 2.0 * juce::MathConstants<double>::pi * juce::jmin (hz, sr * 0.499) / sr;
    double mag = 1.0;
    for (const auto& c : coeffs) mag *= c.magnitude (w);
    return 20.0 * std::log10 (juce::jmax (mag, 1.0e-6));
}

bool EqDisplay::isBell (int b) { return b > 0 && b < BAftercabProcessor::kNumBands - 1; }

float EqDisplay::yForGain (double db) const
{
    const auto a = innerArea();
    return a.getCentreY() - (float) (db / range()) * a.getHeight() * 0.5f;
}

double EqDisplay::gainForY (float y) const
{
    const auto a = innerArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * range();
}

bool EqDisplay::isCut (int b) const
{
    const auto* t = bands[(size_t) b].type;
    return t != nullptr && t->getValue() < 0.5f;   // indice 0 = taglio
}

juce::Point<float> EqDisplay::pointFor (int b) const
{
    const auto& band = bands[(size_t) b];
    // bell accesa: il quadratino sta esattamente sulla curva (risposta totale a quella frequenza);
    // shelf e cut (e bande spente) restano al proprio guadagno
    const double g = isCut (b) ? 0.0
                   : (isBell (b) && isOn (b) ? responseDb (get (band.freq)) : (double) get (band.gain));
    const float r = range();
    return { xForFreq (get (band.freq)), yForGain (juce::jlimit ((double) -r, (double) r, g)) };
}

int EqDisplay::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 12.0f;
    for (int b = 0; b < BAftercabProcessor::kNumBands; ++b)
    {
        const float d = pointFor (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqDisplay::setHover (int b)
{
    if (b == hover) return;
    hover = b;
    repaint();
    if (onInfoChanged) onInfoChanged();
}

// ---------------------------------------------------------------- disegno
void EqDisplay::paint (juce::Graphics& g)
{
    const auto a = plotArea();
    const auto af = a.toFloat();
    const float r = range();
    const bool on = eqOn->getValue() > 0.5f;

    // --- griglia: solo linee continue da 1 px, molto scure
    const double vLines[] = { 50, 75, 100, 250, 500, 750, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    g.setColour (gridLine);
    for (const double hz : vLines)
        g.fillRect (std::round (xForFreq (hz)), af.getY(), kLine, af.getHeight());

    const float stepDb = r > 5.0f ? 5.0f : 2.0f;   // ±15: righe ogni 5 dB; precision ±4: ogni 2 dB
    const int lines = juce::roundToInt (r / stepDb);
    for (int i = -lines; i <= lines; ++i)
    {
        const float db = stepDb * (float) i;
        const float y = std::round (yForGain (db));
        g.setColour (i == 0 ? gridZero : gridLine);   // (riga dello 0 più chiara)
        g.fillRect (af.getX(), y, af.getWidth(), kLine);
        const auto lbl = (db > 0 ? "+" : "") + juce::String (juce::roundToInt (db));
        drawLabel (g, lbl, { 0, (int) y - 8, kLeftMargin - 8, 16 }, info, kAxisSize, juce::Justification::centredRight, Face::MonoRegular);
    }

    // --- etichette di frequenza (2k-8k un pizzico più piccole)
    struct Mark { double hz; const char* text; float size; };
    const Mark marks[] = { { 20, "20", kAxisSize }, { 50, "50", kAxisSmall }, { 75, "75", kAxisSmall }, { 100, "100", kAxisSize },
                           { 250, "250", kAxisSmall }, { 500, "500", kAxisSmall }, { 750, "750", kAxisSmall },
                           { 1000, "1k", kAxisSize }, { 2000, "2k", kAxisSmall },
                           { 4000, "4k", kAxisSmall }, { 6000, "6k", kAxisSmall }, { 8000, "8k", kAxisSmall },
                           { 10000, "10k", 12 }, { 20000, "20k", 12 } };
    for (const auto& m : marks)
    {
        const int x = juce::roundToInt (xForFreq (m.hz));
        // stessa linea di base per tutte le etichette, anche quelle più piccole (2k, 4k...)
        drawAtBaseline (g, m.text, (float) x, (float) getHeight() - getXLabelBaselineFromBottom(), info, m.size,
                        juce::Justification::horizontallyCentred);
    }

    // --- spettro dal vivo dopo l'EQ (grigio tenue, sotto la curva): 0 dB in alto, -84 dB in basso
    if (spectrumVisible)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (a);
        spectrum.draw (g, af, [this] (float x) { return freqForX (x); }, text);
    }

    // --- curva di risposta, tagliata nettamente ai bordi del grafico
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const auto coeffs = proc.computeEqCoeffs (sr);   // calcolati una volta per tutta la curva
    const bool linearNow = on && proc.apvts.getRawParameterValue (ParamIDs::eqMode)->load() > 0.5f;
    if (! cachedValid || coeffs != cachedCoeffs || a != cachedArea || ! juce::exactlyEqual (r, cachedRange)
        || linearNow != cachedLinear || (linearNow && proc.getLinearResponseVersion() != cachedVersion))
    {
        cachedCurve.clear();
        for (int x = a.getX(); x <= a.getRight(); ++x)
        {
            const double hz = juce::jlimit (5.0, sr * 0.499, freqForX ((float) x));
            const double db = juce::jlimit (-60.0, 60.0, responseDbWith (hz, coeffs, sr));
            const float y = yForGain (db);
            if (x == a.getX()) cachedCurve.startNewSubPath ((float) x, y);
            else               cachedCurve.lineTo ((float) x, y);
        }
        cachedCoeffs = coeffs; cachedArea = a; cachedRange = r; cachedLinear = linearNow;
        cachedVersion = proc.getLinearResponseVersion(); cachedValid = true;
    }
    const auto& curve = cachedCurve;
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (a);
        g.setColour (on ? accent : text.withAlpha (0.5f));
        g.strokePath (curve, juce::PathStrokeType (1.5f));
    }

    // --- punti banda (quadrati)
    for (int b = 0; b < BAftercabProcessor::kNumBands; ++b)
    {
        const auto p = pointFor (b);
        const bool active = b == hover || b == dragging;
        const bool selected = b == getSelected();
        const float s = (float) (active ? kPointSize + 2 : kPointSize);
        const auto pr = juce::Rectangle<float> (s, s).withCentre (p).toNearestInt().toFloat();
        if (isOn (b) && (on || active))
        {
            g.setColour (accent);
            g.fillRect (pr);
        }
        else
        {
            // banda spenta (o EQ spento): solo bordo grigio, vuoto (la curva resta visibile e continua)
            g.setColour (active ? accent : text);
            outline (g, pr);
        }
        // banda spenta: tutto grigio come il suo quadratino, anche se selezionata (cornice e numero)
        const bool lit = isOn (b) && on;
        if (selected)   // punto selezionato: cornice sottile intorno
        {
            g.setColour (lit ? accent : text);
            outline (g, pr.expanded (3.0f));
        }
        {
            // numero della banda sopra il punto (nessun riquadro di sfondo: la curva non si interrompe)
            const auto numArea = pr.toNearestInt().translated (0, selected ? -16 : -14).withSizeKeepingCentre (16, 14);
            drawLabel (g, juce::String (b + 1), numArea, (active || selected) && lit ? accent : (selected ? text : info),
                       kAxisSmall, juce::Justification::centred, Face::MonoRegular);
        }

        // guadagno fuori scala (es. +8 dB in modalità precision): piccolo segno sul punto
        const double gdb = isCut (b) ? 0.0 : get (bands[(size_t) b].gain);
        if (std::abs (gdb) > range() + 0.001)
        {
            g.setColour (lit ? accent : text);
            const float ty = gdb > 0 ? pr.getY() - 3.0f : pr.getBottom() + 2.0f;
            g.fillRect (pr.getX(), ty, pr.getWidth(), 1.0f);
        }
    }

    // --- info (banda sotto il mouse, oppure latenza) in alto a destra
    auto infoText = getInfoText();
    if (infoText.isEmpty() && proc.getLatencySamples() > 0)
        infoText = "latency " + num (1000.0 * proc.getLatencySamples() / sr, 1) + " ms";
    drawLabel (g, infoText, a.withHeight (16).translated (0, -18), ui::info, kInfoSize, juce::Justification::centredRight, Face::SansRegular);
}

float EqDisplay::getScaleLeft() const
{
    // le etichette dB sono allineate a destra su x = kLeftMargin - 8; la più larga è ±10
    float left = (float) (kLeftMargin - 8);
    for (const char* s : { "+15", "-15" })
    {
        const float advance = juce::GlyphArrangement::getStringWidth (face (Face::MonoRegular, kAxisSize), s);
        const auto ink = makeInkText (s, kAxisSize, Face::MonoRegular);
        left = juce::jmin (left, (float) (kLeftMargin - 8) - advance + ink.left);
    }
    return left;
}

juce::String EqDisplay::getInfoText() const
{
    const int b = dragging >= 0 ? dragging : hover;
    if (b < 0) return {};
    const auto& band = bands[(size_t) b];
    juce::String name = "band " + juce::String (b + 1);
    if (b == 0) name = isCut (b) ? "low cut" : "low shelf";
    if (b == BAftercabProcessor::kNumBands - 1) name = isCut (b) ? "high cut" : "high shelf";

    juce::String s = name + "  " + freqText (get (band.freq));
    if (! isCut (b))
    {
        const float gdb = get (band.gain);
        s << "  " << (gdb > 0.0f ? "+" : "") << num (gdb, 2) << " dB";
    }
    s << "  q " << num (get (band.q), 2);
    return s;
}

// ---------------------------------------------------------------- mouse
void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    setHover (bandAt (e.position));
    setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqDisplay::mouseExit (const juce::MouseEvent&) { if (dragging < 0) setHover (-1); }

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b < 0) return;

    if (e.mods.isPopupMenu())
    {
        if (auto* t = bands[(size_t) b].type)
        {
            t->beginChangeGesture();
            t->setValueNotifyingHost (t->getValue() < 0.5f ? 1.0f : 0.0f);
            t->endChangeGesture();
            repaint();
            if (onInfoChanged) onInfoChanged();
        }
        return;
    }

    setSelected (b);
    dragging = b;
    auto& band = bands[(size_t) b];
    dragStartFreq = get (band.freq);
    // in precision un guadagno fuori scala (es. +8 dB con ±4) si prende dal bordo dove è disegnato:
    // il trascinamento parte da lì, così il volume si muove subito (non serve rilasciare e riprendere)
    dragStartGain = juce::jlimit ((double) -range(), (double) range(), (double) get (band.gain));
    band.freq->beginChangeGesture();
    if (! isCut (b)) band.gain->beginChangeGesture();
    moved = false;   // nessuna modifica finché il mouse non si sposta davvero
    if (e.getNumberOfClicks() == 1) { applyPendingToggle(); clickBand = b; }   // un clic precedente non era un doppio clic
    else cancelPendingToggle();   // secondo clic: il primo non deve più accendere / spegnere
}

void EqDisplay::setParam (juce::RangedAudioParameter* p, float normalised)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (normalised);
    p->endChangeGesture();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    // sotto la soglia è ancora un clic (la mano che trema non sposta il punto)
    if (! moved && e.getDistanceFromDragStart() < kDragThresholdPx) return;
    moved = true;
    auto& band = bands[(size_t) dragging];
    const auto fr = band.freq->getNormalisableRange();
    const float x = xForFreq (dragStartFreq) + (float) e.getDistanceFromDragStartX();
    set (band.freq, (float) juce::jlimit ((double) fr.start, (double) fr.end, freqForX (x)));
    if (! isCut (dragging))
    {
        // spostamento verticale -> variazione di guadagno (relativa, così il punto non salta al clic)
        const double dDb = gainForY ((float) e.getDistanceFromDragStartY()) - gainForY (0.0f);
        const double r = range();
        set (band.gain, (float) juce::jlimit (-r, r, dragStartGain + dDb));
    }
    repaint();
    if (onInfoChanged) onInfoChanged();
}

void EqDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const int b = dragging;
    auto& band = bands[(size_t) b];
    band.freq->endChangeGesture();
    if (! isCut (b)) band.gain->endChangeGesture();
    dragging = -1;

    if (! moved)   // è stato un clic, non un trascinamento
    {
        if (e.getNumberOfClicks() >= 2 && clickBand == b)
        {
            // doppio clic: la banda torna ai valori di default; acceso/spento non cambia
            for (auto* p : { band.freq, band.gain, band.q, band.type }) if (p != nullptr) setParam (p, p->getDefaultValue());
        }
        else
        {
            // clic singolo: spenta / accesa solo quando è chiaro che non arriva un secondo clic
            pendingToggle = b;
            clickTimer.startTimer (juce::MouseEvent::getDoubleClickTimeout());
        }
        if (onInfoChanged) onInfoChanged();
    }
    setHover (bandAt (e.position));
    repaint();
}

void EqDisplay::applyPendingToggle()
{
    clickTimer.stopTimer();
    if (pendingToggle < 0) return;
    const int b = std::exchange (pendingToggle, -1);
    setParam (bands[(size_t) b].on, isOn (b) ? 0.0f : 1.0f);
    repaint();
    if (onInfoChanged) onInfoChanged();
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = dragging >= 0 ? dragging : bandAt (e.position);
    if (b < 0) return;
    auto* qp = bands[(size_t) b].q;
    const auto qr = qp->getNormalisableRange();
    const float q = juce::jlimit (qr.start, qr.end, get (qp) * std::exp (w.deltaY * 0.6f));
    qp->beginChangeGesture();
    set (qp, q);
    qp->endChangeGesture();
    repaint();
    if (onInfoChanged) onInfoChanged();
}

} // namespace ui

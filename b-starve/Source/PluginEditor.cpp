#include "PluginEditor.h"

StarveCard::StarveCard (BStarveProcessor& p, int e) : proc (p), fx (e)
{
    using namespace ui;
    onBox.bindTo (*p.apvts.getParameter (BStarveIDs::on (e)));
    for (auto* vb : { &intBox, &mixBox })
    {
        vb->format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
        vb->step = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
        addAndMakeVisible (vb);
    }
    auto* pi = p.apvts.getParameter (BStarveIDs::intensity (e));
    auto* pm = p.apvts.getParameter (BStarveIDs::mix (e));
    intBox.setParameter (pi); mixBox.setParameter (pm);
    auto* pv = p.apvts.getParameter (BStarveIDs::volume (e));
    volBox.setParameter (pv);
    volBox.format = [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; };
    volBox.step = [] (float v, int d) { return std::round (v) + (float) d; };
    volBox.drag = [pv] (float, float s, float dx) { return pv->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * 0.003f)); };
    volBox.sizeSamples = { "+24.0 dB", "-24.0 dB" };
    addAndMakeVisible (volBox);
    intBox.drag = [pi] (float, float s, float dx) { return pi->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * 0.004f)); };
    mixBox.drag = [pm] (float, float s, float dx) { return pm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * 0.004f)); };
    addAndMakeVisible (onBox);
}

// Margine interno unico (kGapInner) su tutti e 4 i lati: titolo, etichette e scope a sinistra sullo stesso
// bordo; checkbox e riquadri a destra sullo stesso bordo; lo scope chiude in basso alla stessa distanza.
juce::Rectangle<int> StarveCard::inner() const { return getLocalBounds().reduced (ui::kGapInner); }

void StarveCard::resized()
{
    using namespace ui;
    auto r = inner();
    auto top = r.removeFromTop (kControlH);
    onBox.setBounds (top.removeFromRight (kBoxSize).withSizeKeepingCentre (kBoxSize, kBoxSize).expanded (kHitPad));
    r.removeFromTop (kGapInner);
    intBox.setBounds (r.removeFromTop (kControlH).removeFromRight (intBox.getIdealWidth()));
    r.removeFromTop (kGapInner);
    volBox.setBounds (r.removeFromTop (kControlH).removeFromRight (volBox.getIdealWidth()));
    r.removeFromTop (kGapInner);
    mixBox.setBounds (r.removeFromTop (kControlH).removeFromRight (mixBox.getIdealWidth()));
}

void StarveCard::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto b = getLocalBounds().toFloat();
    const bool on = proc.apvts.getRawParameterValue (BStarveIDs::on (fx))->load() > 0.5f;
    g.setColour (on ? text : frame); outline (g, b);
    const char* titles[4] = { "starve", "mosh", "corrupt", "melt" };
    auto r = inner();
    drawLabel (g, juce::String (titles[fx]).toUpperCase(), r.removeFromTop (kControlH), on ? accent : labelStatic, kLabelSize,
               juce::Justification::centredLeft, Face::SansSemiBold, 0.06f);
    r.removeFromTop (kGapInner);
    drawLabel (g, "intensity", r.removeFromTop (kControlH), labelStatic, kLabelSize);
    r.removeFromTop (kGapInner);
    drawLabel (g, proc.isCalibrating (fx) ? "auto..." : "volume", r.removeFromTop (kControlH), proc.isCalibrating (fx) ? accent : labelStatic, kLabelSize);
    r.removeFromTop (kGapInner);
    drawLabel (g, "mix", r.removeFromTop (kControlH), labelStatic, kLabelSize);
    // mini visualizzatore: forma d'onda dopo questo effetto (~43 ms)
    r.removeFromTop (kGapInner);
    const auto sc = r.toFloat();   // lo scope occupa il resto, con lo stesso margine sui lati e in fondo
    g.setColour (gridLine); outline (g, sc);
    g.setColour (gridZero); g.fillRect (sc.getX(), std::round (sc.getCentreY()), sc.getWidth(), kLine);
    const int last = proc.scopePos.load (std::memory_order_acquire);
    juce::Path p;
    const int w = (int) sc.getWidth() - 2;
    float energy = 0;
    for (int x = 0; x < w; ++x)
    {
        const int idx = (last - (int) ((float) (w - 1 - x) * 512.0f / (float) w)) & (BStarveProcessor::kScope - 1);
        const float v = juce::jlimit (-1.0f, 1.0f, proc.scope[(size_t) fx][(size_t) idx]);
        energy += std::abs (v);
        const float y = sc.getCentreY() - v * sc.getHeight() * 0.45f;
        if (x == 0) p.startNewSubPath (sc.getX() + 1.0f, y); else p.lineTo (sc.getX() + 1.0f + (float) x, y);
    }
    if (energy < 1e-3f) return;   // a riposo: solo la riga dello zero
    g.setColour (on ? accent : text.withAlpha (0.5f));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void StarveCards::resized()
{
    const int gap = ui::kGapInner, w = (getWidth() - 3 * gap) / 4;
    for (int e = 0; e < 4; ++e) cards[(size_t) e]->setBounds (e * (w + gap), 0, w, getHeight());
}

BStarveEditor::BStarveEditor (BStarveProcessor& p)
    : ShellEditor (p, p.apvts, "starve", 760, 430), cards (p)
{
    using namespace ui;
    addValue ("output", BStarveIDs::output, [] (float v) { return num (v, 1) + " dB"; }, [] (float v, int d) { return std::round (v) + (float) d; }, {});
    setDisplay (cards);
    resized();
}

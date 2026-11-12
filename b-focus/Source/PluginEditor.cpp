#include "PluginEditor.h"
#include <complex>

namespace
{
    constexpr int kWidth = 640, kHeight = 390, kMargin = 24;
    constexpr float kTitleSize = 30.0f;
    constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    constexpr float kRangeDb = 48.0f;

    int textWidth (const juce::String& s, float size = ui::kLabelSize)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (ui::font (size), s));
    }
}

// ============================================================================ ReductionMeter
void ReductionMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), ui::kControlH).toFloat();
    g.setColour (ui::text);
    ui::outline (g, r);
    const auto in = r.reduced (2.0f);
    const float w = std::round (in.getWidth() * juce::jlimit (0.0f, 1.0f, value / kRangeDb));
    if (w > 0.0f)
    {
        g.setColour (ui::accent);
        g.fillRect (in.withWidth (w));
    }
}

// ============================================================================ LearnButton
int LearnButton::getIdealWidth() const
{
    // larghezza fissa sulla scritta più lunga: il layout non salta durante il conto alla rovescia
    return (int) std::ceil (juce::jmax (juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::SansSemiBold, ui::kControlTextSize), "learn"),
                                        juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::MonoSemiBold, ui::kControlTextSize), "0.0 s"))) + 16;
}

void LearnButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (proc.isLearning())
    {
        g.setColour (ui::accent);
        g.fillRect (r);
        // conto alla rovescia: Plex Mono SemiBold (cifre a larghezza fissa), testo scuro sul blu
        ui::drawLabel (g, juce::String (proc.getLearnRemaining(), 1) + " s", getLocalBounds(), ui::onAccent,
                       ui::kControlTextSize, juce::Justification::centred, ui::Face::MonoSemiBold);
    }
    else
    {
        g.setColour (isMouseOver() ? ui::accent.brighter (0.2f) : ui::accent);
        ui::outline (g, r);
        ui::drawLabel (g, "learn", getLocalBounds(), isMouseOver() ? ui::accent.brighter (0.2f) : ui::accent,
                       ui::kControlTextSize, juce::Justification::centred, ui::Face::SansSemiBold);
    }
}

// ============================================================================ ResponseDisplay
juce::Rectangle<int> ResponseDisplay::plotArea() const
{
    return getLocalBounds().withTrimmedLeft (kLeftMargin).withTrimmedBottom (kBottomMargin)
                           .withTrimmedTop (kTopMargin).reduced (2, 0);
}

float ResponseDisplay::getScaleLeft() const
{
    float left = (float) (kLeftMargin - 8);
    for (const char* s : { "-12", "-24", "-36", "-48" })
    {
        const float advance = juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::MonoRegular, ui::kAxisSize), s);
        left = juce::jmin (left, (float) (kLeftMargin - 8) - advance + ui::makeInkText (s, ui::kAxisSize, ui::Face::MonoRegular).left);
    }
    return left;
}

void ResponseDisplay::paint (juce::Graphics& g)
{
    const auto a = plotArea();
    const auto in = a.toFloat().reduced ((float) kInner);
    auto xFor = [&] (double hz) { return in.getX() + in.getWidth() * (float) (std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz)); };
    auto yFor = [&] (float db) { return in.getY() - db / kRangeDb * in.getHeight(); };   // 0 dB in alto, -48 in basso
    const float x0 = std::round (xFor (kMinHz)), x1 = std::round (xFor (kMaxHz)) + 1.0f;
    const float y0 = std::round (yFor (0.0f)), y1 = std::round (yFor (-kRangeDb)) + 1.0f;

    // griglia chiusa: dB orizzontali, frequenze dei tre stadi in verticale
    for (const float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        g.setColour (juce::exactlyEqual (db, 0.0f) ? ui::gridZero : ui::gridLine);
        g.fillRect (x0, std::round (yFor (db)), x1 - x0, ui::kLine);
        ui::drawLabel (g, juce::String (juce::roundToInt (db)), { 0, juce::roundToInt (yFor (db)) - 8, kLeftMargin - 8, 16 },
                       ui::info, ui::kAxisSize, juce::Justification::centredRight, ui::Face::MonoRegular);
    }
    struct Mark { double hz; const char* text; bool stage; };   // stage = frequenza di uno dei tre stadi
    const Mark marks[] = { { 20, "20", false }, { 100, "100", false }, { 300, "300", true }, { 900, "900", true },
                           { 2800, "2.8k", true }, { 10000, "10k", false }, { 20000, "20k", false } };
    for (const auto& [hz, t, stage] : marks)
    {
        if (hz > kMinHz && hz < kMaxHz)
        {
            g.setColour (stage ? ui::gridZero : ui::gridLine);
            g.fillRect (std::round (xFor (hz)), y0, ui::kLine, y1 - y0);
        }
        ui::drawAtBaseline (g, t, xFor (hz), (float) getHeight() - getXLabelBaselineFromBottom(), ui::info,
                            stage ? ui::kAxisSize : ui::kAxisSmall, juce::Justification::horizontallyCentred);
    }

    // stato del learn (o il suggerimento, grigio tenue) in alto a sinistra: lì la curva passa solo a gate tutto
    // aperto (sulla linea dello 0 dB, sopra il testo) o tutto chiuso (in basso)
    if (const auto status = proc.getLearnStatus(); status.isNotEmpty())
        ui::drawAtBaseline (g, status, x0 + 8.0f, y0 + 18.0f, status.startsWith ("tip") ? ui::frame.brighter (0.25f) : ui::info,
                            ui::kInfoSize, juce::Justification::left, ui::Face::SansRegular);

    // noise profile del learn: linea grigia tratteggiata, normalizzata alla banda più rumorosa (0 dB)
    {
        const auto np = proc.getNoiseProfile();
        if (! std::isnan (np[0]))
        {
            const float mx = juce::jmax (juce::jmax (np[0], np[1]), juce::jmax (np[2], np[3]));
            const double centres[4] = { 6000.0, 1590.0, 520.0, 120.0 };   // bande 1..4
            juce::Path prof;
            for (int k = 3; k >= 0; --k)
            {
                const auto pt = juce::Point<float> (xFor (centres[k]), yFor (juce::jlimit (-kRangeDb, 0.0f, np[(size_t) k] - mx)));
                if (k == 3) { prof.startNewSubPath (in.getX(), pt.y); prof.lineTo (pt); } else prof.lineTo (pt);
                if (k == 0) prof.lineTo (in.getRight(), pt.y);
            }
            juce::Path dashed;
            const float dash[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, prof, dash, 2);
            g.setColour (ui::frame);
            g.fillPath (dashed);
            ui::drawAtBaseline (g, "noise profile", in.getX() + 6.0f, yFor (juce::jlimit (-kRangeDb, 0.0f, np[3] - mx)) - 6.0f, ui::frame.brighter (0.2f),
                                ui::kTagSize, juce::Justification::left, ui::Face::SansRegular);
        }
    }

    // curva reale delle 4 bande: prodotto dei tre shelf (H = L + m (1 - L), L = 1 / (1 + j f/fc)^2)
    // e del guadagno della banda 4 (tutto lo spettro): quando il gate chiude del tutto scendono anche medie e basse
    float mk[4];
    for (int k = 0; k < 4; ++k) mk[k] = proc.getStageMultiplier (k);
    juce::Path curve;
    for (int px = 0; px <= (int) in.getWidth(); ++px)
    {
        const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (double) px / in.getWidth());
        double mag = mk[3];
        for (int k = 0; k < 3; ++k)
        {
            const std::complex<double> d (1.0, hz / sp::FocusEngine::kSplitHz[k]);
            const auto L = 1.0 / (d * d);
            mag *= std::abs (L + (double) mk[k] * (1.0 - L));
        }
        const float db = (float) juce::jlimit (-60.0, 0.0, 20.0 * std::log10 (juce::jmax (mag, 1.0e-6)));
        const float x = in.getX() + (float) px, y = yFor (db);
        if (px == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (a);
    g.setColour (ui::accent);
    g.strokePath (curve, juce::PathStrokeType (1.5f));
}

// ============================================================================ BFocusEditor
BFocusEditor::BFocusEditor (BFocusProcessor& p) : AudioProcessorEditor (&p), proc (p), display (p), learnButton (p)
{
    auto& s = p.apvts;
    thresholdBox.setParameter (s.getParameter (BFocusIDs::threshold));
    thresholdBox.format = [] (float v) { return ui::num (v, 1) + " dB"; };
    thresholdBox.step = [] (float v, int dir) { return std::round (v) + (float) dir; };
    thresholdBox.drag = [] (float start, float, float dx) { return std::round ((start + dx / 4.0f) * 10.0f) / 10.0f; };

    strengthBox.setParameter (s.getParameter (BFocusIDs::strength));
    strengthBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    strengthBox.step = [] (float v, int dir) { return std::round (v) + 5.0f * (float) dir; };
    strengthBox.drag = [] (float start, float, float dx) { return std::round (start + dx / 2.0f); };

    releaseBox.setParameter (s.getParameter (BFocusIDs::release));
    releaseBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    releaseBox.step = [] (float v, int dir) { return std::round (v * std::pow (1.15f, (float) dir)); };
    releaseBox.drag = [this] (float, float startNorm, float dx)
    {
        auto* prm = proc.apvts.getParameter (BFocusIDs::release);
        return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.003f));
    };

    // misura fissa: ogni riquadro è largo quanto il valore più lungo possibile
    thresholdBox.sizeSamples = { "-100.0 dB", "-88.8 dB" };
    strengthBox.sizeSamples  = { "100 %" };
    releaseBox.sizeSamples   = { "1500 ms", "8888 ms" };
    for (auto* vb : { &thresholdBox, &strengthBox, &releaseBox })
    {
        vb->onWidthChanged = [this] { resized(); };
        addAndMakeVisible (vb);
    }
    addAndMakeVisible (display);
    addAndMakeVisible (meter);
    addAndMakeVisible (learnButton);
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

BFocusEditor::~BFocusEditor() { stopTimer(); }

juce::Rectangle<int> BFocusEditor::box() const { return { kMargin, 66, kWidth - 2 * kMargin, kHeight - 66 - kMargin }; }

void BFocusEditor::resized()
{
    using namespace ui;
    labels.clear();
    const auto b = box();
    const int pad = kFramePad;
    const int left = b.getX() + 1 + pad;
    const int graphRight = b.getRight() - 1 - pad;

    auto top = juce::Rectangle<int> (left, b.getY() + 1 + pad, graphRight - left, kControlH);
    const int dispBottom = juce::roundToInt ((float) (b.getBottom() - 1 - pad) + display.getXLabelBaselineFromBottom());
    const int dispX = left - juce::roundToInt (display.getScaleLeft());
    display.setBounds (juce::Rectangle<int>::leftTopRightBottom (dispX, top.getBottom() + 6,
                                                                 graphRight + 1 + 2 + ResponseDisplay::kInner, dispBottom));

    auto add = [&] (const juce::String& name, ValueBox& vb, int gapAfter)
    {
        labels.push_back ({ name, top.removeFromLeft (textWidth (name)) });
        top.removeFromLeft (kGapLabel);
        vb.setBounds (top.removeFromLeft (vb.getIdealWidth()));
        top.removeFromLeft (gapAfter);
    };
    add ("threshold", thresholdBox, kGapGroup);
    add ("strength", strengthBox, kGapGroup);
    add ("release", releaseBox, kGapGroup);
    modeArea = top;

    // meter della riduzione nell'header, sulla linea di base del titolo, fino al bordo della griglia
    // centrati in verticale sul titolo: stessa mezzeria delle sue lettere (dalla linea di base all'altezza delle maiuscole)
    auto head = juce::Rectangle<int> (left, headerRowY(), graphRight - left, kControlH);
    learnButton.setBounds (head.removeFromRight (learnButton.getIdealWidth()));
    head.removeFromRight (kGapGroup);
    meterValueArea = head.removeFromRight ((int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::MonoRegular, kInfoSize), "-36.0 dB")));
    head.removeFromRight (kGapLabel);
    meter.setBounds (head.removeFromRight (160));
    head.removeFromRight (kGapLabel);
    labels.push_back ({ "gr", head.removeFromRight (textWidth ("gr")) });   // riduzione massima tra le 4 bande
    ui::centreHeaderInRig (getWidth(), { &learnButton, &meter }, { &meterValueArea, &labels.back().area });
}

void BFocusEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);
    const juce::Rectangle<int> r (kMargin - 1, 16, 300, 36);   // centro y = 34
    ui::drawTitle (g, "focus", r, kTitleSize);
    const auto mode = getAudioProcessor()->getTotalNumInputChannels() == 1 ? "mono" : "stereo";
    ui::drawLabel (g, mode, modeArea, ui::info, ui::kInfoSize, juce::Justification::centredRight, ui::Face::SansRegular);
    ui::drawLabel (g, ui::num (meterDb > 0.05f ? -meterDb : 0.0, 1) + " dB", meterValueArea, meterDb > 0.05f ? ui::valueText : ui::info,
                   ui::kInfoSize, juce::Justification::centredRight, ui::Face::MonoRegular);

    g.setColour (ui::frame);
    ui::outline (g, box().toFloat());
    for (const auto& l : labels) ui::drawLabel (g, l.text, l.area, ui::labelStatic, ui::kLabelSize);
}

void BFocusEditor::timerCallback()
{
    // conto alla rovescia del learn (il risultato lo applica il processor sul message thread)
    if (proc.isLearning() || wasLearning) learnButton.repaint();
    wasLearning = proc.isLearning();
    if (const auto status = proc.getLearnStatus(); status != shownStatus) { shownStatus = status; display.repaint(); }

    // meter fluido: sale subito, scende di ~24 dB/s
    const float gr = -proc.getGainReductionDb();
    const float shown = gr >= meterDb ? gr : juce::jmax (gr, meterDb - 0.8f);
    if (std::abs (shown - meterDb) > 0.01f)
    {
        meterDb = shown;
        meter.setReduction (meterDb);
        display.repaint();
        repaint (meterValueArea);
    }

    juce::int64 sig = 17;
    for (auto* p : proc.getParameters()) sig = sig * 31 + (juce::int64) (p->getValue() * 1.0e6f);
    if (sig == lastSignature) return;
    lastSignature = sig;
    repaint();
    for (auto* vb : { &thresholdBox, &strengthBox, &releaseBox }) vb->repaint();
}

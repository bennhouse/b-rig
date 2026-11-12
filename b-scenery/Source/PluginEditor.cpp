#include "PluginEditor.h"

void BalanceButton::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto r = getLocalBounds().toFloat();
    if (proc.isListening())
    {
        g.setColour (accent); g.fillRect (r);
        drawLabel (g, num (proc.listenRemaining(), 1) + " s", getLocalBounds(), onAccent, kControlTextSize, juce::Justification::centred, Face::MonoSemiBold);
    }
    else
    {
        g.setColour (isMouseOver() ? labelHover : text); outline (g, r);
        drawLabel (g, "balance", getLocalBounds(), selector, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
}

namespace
{
    constexpr float kFloor = -72.0f, kMeterW = 150.0f;
    float yNorm (float lin) { const float db = lin > 1.0e-7f ? 20.0f * std::log10 (lin) : -140.0f; return juce::jlimit (0.0f, 1.0f, db / kFloor); }
    const char* kLayerNames[4] = { "vinyl", "hum", "rain", "lo-fi" };
}

juce::Rectangle<float> SceneryView::plot() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (20.0f).withTrimmedTop (8.0f).withTrimmedLeft (34.0f).withTrimmedRight (kMeterW + 24.0f);
}

juce::Rectangle<float> SceneryView::meters() const
{
    const auto p = plot();
    return { p.getRight() + 24.0f, p.getY(), kMeterW, p.getHeight() };
}

// griglia, scala dBFS, legenda e nomi dei layer: in cache (si ridisegnano solo cambiando dimensione)
void SceneryView::paintStatic (juce::Graphics& g)
{
    using namespace ui;
    const auto a = plot();
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f })
    {
        const float y = a.getY() + a.getHeight() * db / kFloor;
        g.setColour (juce::exactlyEqual (db, 0.0f) ? gridZero : gridLine);
        g.fillRect (a.getX(), std::round (y), a.getWidth(), kLine);
        drawLabel (g, juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 8.0f, a.getX() - 6.0f, 16.0f).toNearestInt(),
                   info, kAxisSmall, juce::Justification::centredRight, Face::MonoRegular);
    }
    g.setColour (gridLine); outline (g, a);
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    const double secs = (double) (a.getWidth() - 2.0f) * BSceneryProcessor::kHop / (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    drawAtBaseline (g, "-" + num (secs, 1) + " s", a.getX(), base, info, kAxisSize, juce::Justification::left);
    // legenda
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    float lx = a.getX() + 64.0f;
    auto item = [&] (const char* t, int kind)
    {
        const float cy = base - tagH * 0.5f;
        if (kind == 0) { g.setColour (text.withAlpha (0.18f)); g.fillRect (lx, cy - 3.0f, 10.0f, 6.0f); g.setColour (text.withAlpha (0.5f)); g.fillRect (lx, cy - 3.0f, 10.0f, 1.0f); }
        if (kind == 1) { g.setColour (accent); g.fillRect (lx, cy - 0.75f, 10.0f, 1.5f); }
        if (kind == 2) { g.setColour (accent.withAlpha (0.18f)); g.fillRect (lx, cy - 3.0f, 10.0f, 6.0f); g.setColour (accent.withAlpha (0.6f)); g.fillRect (lx, cy - 3.0f, 3.0f, 1.0f); g.fillRect (lx + 6.0f, cy - 3.0f, 3.0f, 1.0f); }
        drawTag (g, t, lx + 14.0f, base);
        lx += 14.0f + juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kTagSize, 0.08f), t) + 18.0f;
    };
    item ("guitar", 0); item ("texture", 1); item ("react (ducking)", 2);
    // colonna dei layer
    const auto m = meters();
    const float bw = (m.getWidth() - 3.0f * 12.0f) / 4.0f;
    for (int l = 0; l < 4; ++l)
    {
        const auto bar = juce::Rectangle<float> (m.getX() + (float) l * (bw + 12.0f), m.getY(), bw, m.getHeight());
        g.setColour (gridLine); outline (g, bar);
        drawAtBaseline (g, kLayerNames[l], bar.getCentreX(), base, info, kAxisSmall, juce::Justification::horizontallyCentred, Face::SansRegular);
    }
}

void SceneryView::paint (juce::Graphics& g)
{
    using namespace ui;
    bg.draw (g, getLocalBounds(), 1, [this] (juce::Graphics& sg) { paintStatic (sg); });
    const auto a = plot();
    const int last = proc.historyPos.load (std::memory_order_acquire), w = (int) a.getWidth() - 2;
    const float x0 = a.getX() + 1.0f;
    auto yOf = [&] (float lin) { return a.getY() + a.getHeight() * yNorm (lin); };
    // un vertice per pixel: chitarra (superficie), texture reale (linea) e potenziale (tratteggio), fascia tra le due
    juce::Path gtr, tex, pre, band;
    gtr.startNewSubPath (x0, a.getBottom());
    std::vector<float> yPre ((size_t) juce::jmax (1, w));
    bool any = false;
    for (int x = 0; x < w; ++x)
    {
        const auto& f = proc.history[(size_t) ((last - (w - 1 - x)) & (BSceneryProcessor::kHistory - 1))];
        const float px = x0 + (float) x;
        gtr.lineTo (px, yOf (f.guitar));
        const float yt = yOf (f.tex), yp = yOf (f.texPre);
        yPre[(size_t) x] = yp;
        if (x == 0) { tex.startNewSubPath (px, yt); pre.startNewSubPath (px, yp); band.startNewSubPath (px, yt); }
        else { tex.lineTo (px, yt); pre.lineTo (px, yp); band.lineTo (px, yt); }
        any = any || f.guitar > 1.0e-4f || f.tex > 1.0e-6f;
    }
    if (any && w > 1)
    {
        gtr.lineTo (x0 + (float) (w - 1), a.getBottom()); gtr.closeSubPath();
        for (int x = w - 1; x >= 0; --x) band.lineTo (x0 + (float) x, yPre[(size_t) x]);
        band.closeSubPath();
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (a.toNearestInt());
        g.setColour (text.withAlpha (0.10f)); g.fillPath (gtr);
        g.setColour (text.withAlpha (0.35f)); g.strokePath (gtr, juce::PathStrokeType (1.0f));
        g.setColour (accent.withAlpha (0.12f)); g.fillPath (band);
        juce::Path dashed; const float dash[] = { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, pre, dash, 2);
        g.setColour (accent.withAlpha (0.55f)); g.fillPath (dashed);
        g.setColour (accent); g.strokePath (tex, juce::PathStrokeType (1.5f));
    }
    // barre dei layer: livello attuale (dBFS), grigio se il layer è spento
    const auto m = meters();
    const float bw = (m.getWidth() - 3.0f * 12.0f) / 4.0f;
    for (int l = 0; l < 4; ++l)   // barre sottili al centro di ogni colonna
    {
        const bool on = proc.apvts.getRawParameterValue (BSceneryIDs::on (l))->load() > 0.5f;
        const auto col = juce::Rectangle<float> (m.getX() + (float) l * (bw + 12.0f), m.getY(), bw, m.getHeight()).reduced (2.0f);
        const auto bar = col.withSizeKeepingCentre (juce::jmin (10.0f, col.getWidth()), col.getHeight());
        const float h = (1.0f - yNorm (proc.layerLevel[(size_t) l].load())) * bar.getHeight();
        if (h > 0.5f)
        {
            g.setColour (on ? accent.withAlpha (0.55f) : text.withAlpha (0.25f)); g.fillRect (bar.withTop (bar.getBottom() - h));
            g.setColour (on ? accent : text); g.fillRect (bar.getX() - 3.0f, std::round (bar.getBottom() - h), bar.getWidth() + 6.0f, 1.5f);
        }
    }
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    if (proc.isListening())
        drawAtBaseline (g, "balance: play your guitar...", a.getRight(), base, accent, kInfoSize, juce::Justification::right, Face::SansRegular);
}

BSceneryEditor::BSceneryEditor (BSceneryProcessor& p)
    : ShellEditor (p, p.apvts, "scenery", 760, 470), balance (p), view (p)
{
    using namespace ui;
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    setHeaderChoice ("mains", BSceneryIDs::mains, { "50 Hz", "60 Hz" });
    addValue ("texture", BSceneryIDs::mix, [] (float v) { return num (v, 1) + " dB"; }, [] (float v, int d) { return std::round (v) + (float) d; }, {});
    addValue ("react", BSceneryIDs::react, pct, stepPct, {});
    addComponent ("", balance, 84);
    const char* names[4] = { "vinyl", "amp hum", "rain", "lo-fi" };
    for (int l = 0; l < 4; ++l)
    {
        if (l % 2 == 0) newRow();
        addCheck (names[l], BSceneryIDs::on (l).toRawUTF8());
        addValue ("", BSceneryIDs::level (l).toRawUTF8(), pct, stepPct, {});
    }
    setDisplay (view);
    displayRefreshesItself = true;
    resized();
}

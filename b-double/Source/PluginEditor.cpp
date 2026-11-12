#include "PluginEditor.h"

void DoubleScope::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto all = getLocalBounds().toFloat();
    const float side = all.getHeight();
    const auto sq = juce::Rectangle<float> (side, side).withCentre (all.getCentre());
    // griglia del goniometro: M verticale, S orizzontale, assi L e R in diagonale
    g.setColour (gridLine);
    outline (g, sq);
    g.drawLine (sq.getX(), sq.getY(), sq.getRight(), sq.getBottom(), 1.0f);
    g.drawLine (sq.getRight(), sq.getY(), sq.getX(), sq.getBottom(), 1.0f);
    g.setColour (gridZero);
    g.fillRect (std::round (sq.getCentreX()), sq.getY(), kLine, sq.getHeight());
    g.fillRect (sq.getX(), std::round (sq.getCentreY()), sq.getWidth(), kLine);
    drawTag (g, "m", sq.getCentreX() + 6.0f, sq.getY() + 6.0f + capHeight (kTagSize, Face::SansSemiBold));
    drawTag (g, "l", sq.getX() + 6.0f, sq.getY() + 6.0f + capHeight (kTagSize, Face::SansSemiBold));
    drawTag (g, "r", sq.getRight() - 6.0f, sq.getY() + 6.0f + capHeight (kTagSize, Face::SansSemiBold), juce::Justification::right);

    // punti: x = (R - L), y = (L + R) (verso l'alto), in scala con un leggero guadagno
    const int last = proc.scopePos.load (std::memory_order_acquire);
    const float k = sq.getWidth() * 0.5f * 4.0f;   // guadagno visivo: una DI tipica riempie il quadrato
    for (int i = 0; i < BDoubleProcessor::kScope; ++i)
    {
        const auto& s = proc.scope[(size_t) ((last - i) & (BDoubleProcessor::kScope - 1))];
        const float x = sq.getCentreX() + (s[1] - s[0]) * 0.7071f * k;
        const float y = sq.getCentreY() - (s[0] + s[1]) * 0.7071f * k;
        if (! sq.contains (x, y)) continue;
        g.setColour (accent.withAlpha (0.15f + 0.6f * (1.0f - (float) i / BDoubleProcessor::kScope)));
        g.fillRect (x, y, 1.0f, 1.0f);
    }

    // indicatori laterali: ritardo (ms) a sinistra, intonazione (cents) a destra
    auto big = [&] (const juce::String& value, const juce::String& unit, const char* tag, float cx)
    {
        const float y = all.getCentreY();
        drawTag (g, tag, cx, y - 18.0f, juce::Justification::horizontallyCentred);
        drawAtBaseline (g, value, cx, y + 8.0f, valueText, 24.0f, juce::Justification::horizontallyCentred, Face::MonoMedium);
        drawAtBaseline (g, unit, cx, y + 28.0f, unitText, kUnitSize, juce::Justification::horizontallyCentred, Face::MonoRegular);
    };
    const bool idle = proc.isIdle();
    const float c = idle ? 0.0f : proc.shownCents.load();
    big (num (idle ? 0.0f : proc.shownDelayMs.load(), 1), "ms", "delay", (all.getX() + sq.getX()) * 0.5f);
    big ((c >= 0 ? "+" : "") + num (c, 1), "cents", "detune", (sq.getRight() + all.getRight()) * 0.5f);
    if (proc.guardActive.load())   // la phase guard sta ruotando la fase della doppia
        drawTag (g, "phase guard", (sq.getRight() + all.getRight()) * 0.5f, all.getBottom() - 4.0f, juce::Justification::horizontallyCentred);
}

BDoubleEditor::BDoubleEditor (BDoubleProcessor& p)
    : ShellEditor (p, p.apvts, "double", 760, 421), scope (p)
{
    using namespace ui;
    setHeaderChoice ("mode", BDoubleIDs::mode, { "hard-pan", "center-wide" });
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    addCheck ("double", BDoubleIDs::active);   // attiva / disattiva (il goniometro resta acceso)
    addValue ("delay", BDoubleIDs::delay, [] (float v) { return num (v, 1) + " ms"; }, [] (float v, int d) { return std::round (v) + (float) d; }, { "40.0 ms" });
    addValue ("detune", BDoubleIDs::detune, [] (float v) { return num (v, 1) + " ct"; }, [] (float v, int d) { return std::round (v) + (float) d; }, { "20.0 ct" });
    addCheck ("dynamic", BDoubleIDs::dynamic);   // ritardo e intonazione umanizzati a ogni plettrata + phase guard
    newRow();
    tightBox = &addValue ("tightness", BDoubleIDs::tightness, pct, stepPct, { "100 %" });
    addValue ("variation", BDoubleIDs::variation, pct, stepPct, { "100 %" });
    addValue ("mono-safe", BDoubleIDs::monoSafe, pct, stepPct, { "100 %" });
    infoText = p.getTotalNumInputChannels() == 1 ? "mono > stereo" : "stereo > stereo";
    setDisplay (scope);
    resized();
}

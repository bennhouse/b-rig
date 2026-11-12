#include "PluginEditor.h"

void FreshDisplay::paint (juce::Graphics& g)
{
    using namespace ui;
    auto all = getLocalBounds().toFloat();
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    auto& s = proc.apvts;
    const float fresh = s.getRawParameterValue (BFreshIDs::freshness)->load() / 100.0f;
    const int gg = juce::jlimit (0, 3, (int) s.getRawParameterValue (BFreshIDs::gauge)->load());
    const int pu = juce::jlimit (0, 2, (int) s.getRawParameterValue (BFreshIDs::pickup)->load());
    const float tension = s.getRawParameterValue (BFreshIDs::tension)->load() / 100.0f;
    const float level = juce::jmin (1.0f, proc.levelNow.load() * 3.0f);
    const float freshNow = proc.freshNow.load(), pickNow = proc.pickNow.load();

    // corda che vibra: ampiezza dal livello della DI, armoniche alte più vive quando "fresh" lavora sull'attacco
    auto top = all.removeFromTop (std::round (all.getHeight() * 0.32f));
    all.removeFromTop (12.0f);
    g.setColour (gridLine);
    outline (g, top);
    drawTag (g, "string", top.getX() + 8.0f, top.getY() + 8.0f + tagH);
    drawTag (g, "fresh " + juce::String (juce::roundToInt (100.0f * freshNow)) + " %   pick " + juce::String (juce::roundToInt (100.0f * pickNow)) + " %",
             top.getRight() - 8.0f, top.getY() + 8.0f + tagH, juce::Justification::right);
    {
        const float x0 = top.getX() + 24.0f, x1 = top.getRight() - 24.0f, cy = top.getCentreY() + 6.0f;
        const float amp = (top.getHeight() * 0.5f - 18.0f) * level;   // ferma e dritta quando non suoni
        const float thick = 1.0f + 0.6f * (float) gg;
        auto draw = [&] (float bright, juce::Colour c, float w)
        {
            juce::Path p;
            const int cols = (int) (x1 - x0);
            for (int i = 0; i <= cols; ++i)
            {
                const float u = (float) i / (float) cols;
                float v = std::sin (juce::MathConstants<float>::pi * u) * std::sin (phase);
                v += 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * u) * std::sin (2.0f * phase + 0.7f);
                v += bright * 0.30f * std::sin (5.0f * juce::MathConstants<float>::pi * u) * std::sin (5.3f * phase);
                v += bright * 0.20f * std::sin (9.0f * juce::MathConstants<float>::pi * u) * std::sin (9.1f * phase + 1.1f);
                const float px = x0 + (float) i, py = cy - amp * v / 1.6f;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w));
        };
        draw (0.2f, text.withAlpha (0.35f), thick);
        draw (0.2f + 0.8f * juce::jmin (1.0f, freshNow * 1.5f + 0.4f * fresh), accent, thick);
        g.setColour (frame);
        g.fillRect (x0 - 4.0f, cy - 6.0f, 3.0f, 12.0f);
        g.fillRect (x1 + 1.0f, cy - 6.0f, 3.0f, 12.0f);
    }

    // curva: pickup (risonanza, pettine, roll-off) + scaffale della corda; area chiara = "fresh" sull'attacco
    g.setColour (gridLine);
    outline (g, all);
    auto plot = all.reduced (1.0f).withTrimmedBottom (18.0f).withTrimmedTop (22.0f);
    const double fsd = 48000.0;
    const double lo = std::log (40.0), hi = std::log (16000.0);
    auto xOf = [&] (double f) { return plot.getX() + (float) ((std::log (f) - lo) / (hi - lo)) * plot.getWidth(); };
    const float range = 12.0f;
    auto yOf = [&] (double db) { return plot.getCentreY() - (float) (db / range) * plot.getHeight() * 0.5f; };
    for (double f : { 100.0, 1000.0, 10000.0 }) { g.setColour (gridLine); g.fillRect (std::round (xOf (f)), plot.getY(), kLine, plot.getHeight()); }
    for (double db : { -6.0, 6.0 }) { g.setColour (gridLine); g.fillRect (plot.getX(), std::round (yOf (db)), plot.getWidth(), kLine); }
    g.setColour (gridZero);
    g.fillRect (plot.getX(), std::round (yOf (0.0)), plot.getWidth(), kLine);
    const float base = all.getBottom() - 6.0f;
    for (auto [f, t] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
        drawAtBaseline (g, t, xOf (f), base, info, kAxisSize, juce::Justification::horizontallyCentred);
    drawTag (g, "pickup + strings", all.getX() + 8.0f, all.getY() + 8.0f + tagH);
    drawValueTag (g, "+6 dB", plot.getRight() - 4.0f, yOf (6.0) - 4.0f, juce::Justification::right);

    const auto& pk = BFreshstringsProcessor::kPickups[pu];
    sp::Bq peak, roll, shelf, fr;
    peak.set (sp::Bq::Peak, fsd, pk.hz, pk.q, pk.db);
    roll.set (sp::Bq::LP, fsd, pk.rollHz, 0.7071);
    shelf.set (sp::Bq::LowShelf, fsd, 110.0, 0.7071, BFreshstringsProcessor::kGaugeShelfDb[gg] * tension * 2.0f);
    sp::Bq gHigh, pLow;
    gHigh.set (sp::Bq::HighShelf, fsd, 4000.0, 0.7071, BFreshstringsProcessor::kGaugeHighDb[gg] * tension * 2.0f);
    pLow.set (sp::Bq::LowShelf, fsd, 250.0, 0.7071, pk.lowDb);
    const double D = BFreshstringsProcessor::kCombMs * 0.001;
    auto curve = [&] (double freshDb)
    {
        juce::Path p;
        fr.set (sp::Bq::HighShelf, fsd, 3200.0, 0.7071, freshDb);
        for (int i = 0; i <= (int) plot.getWidth(); i += 2)
        {
            const double f = std::exp (lo + (hi - lo) * i / plot.getWidth());
            const double w = juce::MathConstants<double>::twoPi * f * D;
            const double cr = 1.0 + pk.combGain * std::cos (w), ci = -pk.combGain * std::sin (w);
            const double comb = std::sqrt (cr * cr + ci * ci) / (1.0 + pk.combGain);
            const double m = peak.magnitude (f, fsd) * roll.magnitude (f, fsd) * shelf.magnitude (f, fsd) * comb * fr.magnitude (f, fsd)
                           * gHigh.magnitude (f, fsd) * pLow.magnitude (f, fsd);
            const float y = juce::jlimit (plot.getY(), plot.getBottom(), yOf (20.0 * std::log10 (m)));
            if (i == 0) p.startNewSubPath (plot.getX(), y); else p.lineTo (plot.getX() + (float) i, y);
        }
        return p;
    };
    const auto rest = curve (0.0);
    const auto attackCurve = curve (6.0 * juce::jmax ((double) freshNow, 0.25 * fresh));
    juce::Path area (attackCurve);
    area.lineTo (plot.getRight(), yOf (0.0)); area.lineTo (plot.getX(), yOf (0.0)); area.closeSubPath();
    g.setColour (accent.withAlpha (0.12f));
    g.fillPath (area);
    g.setColour (accent.withAlpha (0.45f));
    g.strokePath (attackCurve, juce::PathStrokeType (1.0f));
    g.setColour (accent);
    g.strokePath (rest, juce::PathStrokeType (1.5f));
}

BFreshstringsEditor::BFreshstringsEditor (BFreshstringsProcessor& p)
    : ShellEditor (p, p.apvts, "freshstrings", 940, 440), proc (p), display (p)
{
    using namespace ui;
    // il nome lungo non lascia spazio ai tre selettori in alto: nel titolo "b-fs" (il nome completo resta nel file
    // e nel pannello che si apre cliccando il titolo)
    titleText = "fs";
    addHeaderChoice ("pick", BFreshIDs::pick, { "nylon", "tortex", "carbon", "metal" });
    addHeaderChoice ("gauge", BFreshIDs::gauge, { ".009", ".010", ".011", "heavy" });
    addHeaderChoice ("pickup", BFreshIDs::pickup, { "bridge", "both", "neck" });
    addValue ("freshness", BFreshIDs::freshness, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    addValue ("attack", BFreshIDs::attack, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+6.0 dB", "-6.0 dB" });
    addValue ("tension", BFreshIDs::tension, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    addValue ("output", BFreshIDs::output, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+12.0 dB", "-12.0 dB" });
    setDisplay (display);
    displayRefreshesItself = true;
    resized();
}

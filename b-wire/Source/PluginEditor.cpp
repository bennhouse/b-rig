#include "PluginEditor.h"

void WireScope::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto a = getLocalBounds().toFloat().withTrimmedBottom (20.0f);
    g.setColour (gridLine); outline (g, a);
    const char* names[3] = { "main", "sub -1", "sub -2" };
    const float laneH = a.getHeight() / 3.0f;
    const int last = proc.scopePos.load (std::memory_order_acquire);
    const bool live = proc.gateLevel.load() > 0.01f;
    const int w = (int) a.getWidth() - 2;
    for (int lane = 0; lane < 3; ++lane)
    {
        const float top = a.getY() + laneH * (float) lane, mid = top + laneH * 0.5f, amp = laneH * 0.32f;
        g.setColour (gridLine);
        if (lane > 0) g.fillRect (a.getX(), std::round (top), a.getWidth(), kLine);
        g.setColour (gridZero);
        g.fillRect (a.getX(), std::round (mid), a.getWidth(), kLine);
        drawTag (g, names[lane], a.getX() + 6.0f, top + 6.0f + capHeight (kTagSize, Face::SansSemiBold));
        if (! live) continue;   // a riposo: righe vuote
        juce::Path path;
        for (int x = 0; x < w; ++x)   // ~21 ms: ultimi 1024 campioni, 2 per pixel circa
        {
            const int idx = (last - (int) ((float) (w - 1 - x) * 1024.0f / (float) w)) & (BWireProcessor::kScope - 1);
            const float v = juce::jlimit (-1.2f, 1.2f, proc.scope[(size_t) idx][(size_t) lane]);
            const float y = mid - v * amp;
            if (x == 0) path.startNewSubPath (a.getX() + 1.0f, y); else path.lineTo (a.getX() + 1.0f + (float) x, y);
        }
        g.setColour (lane == 0 ? accent : accent.withAlpha (0.65f - 0.2f * (float) lane));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
    const float hz = proc.pllHz.load();
    if (const float ag = proc.autoGainDb.load(); ag > -999.0f)
        drawAtBaseline (g, "auto level " + juce::String (ag >= 0.05f ? "+" : "") + num (ag, 1) + " dB", a.getCentreX(), (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f,
                        accent, kAxisSize, juce::Justification::horizontallyCentred, Face::MonoMedium);
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    drawAtBaseline (g, "-21 ms", a.getX(), base, info, kAxisSize, juce::Justification::left);
    drawAtBaseline (g, hz > 0 ? "pll " + num (hz, 1) + " Hz" : juce::String ("pll --"), a.getRight(), base, hz > 0 ? valueText : info,
                    kAxisSize, juce::Justification::right, Face::MonoMedium);
}

BWireEditor::BWireEditor (BWireProcessor& p)
    : ShellEditor (p, p.apvts, "wire", 800, 430), scope (p)
{
    using namespace ui;
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto db = [] (float v) { return num (v, 1) + " dB"; };
    auto stepDb = [] (float v, int d) { return std::round (v) + (float) d; };
    addValue ("fuzz", BWireIDs::fuzz, pct, stepPct, {});
    addValue ("sub 1", BWireIDs::sub1, pct, stepPct, {});
    addValue ("sub 2", BWireIDs::sub2, pct, stepPct, {});
    addValue ("glitch", BWireIDs::glitch, pct, stepPct, {});
    auto& mixBox = addValue ("mix", BWireIDs::mix, pct, stepPct, {});
    alignLastRight();
    newRow();
    addValue ("ring", BWireIDs::ring, pct, stepPct, {});
    addValue ("tracking", BWireIDs::tracking, [] (float v) { return num (v, 1) + " ms"; }, [] (float v, int d) { return v * std::pow (1.15f, (float) d); }, {});
    addValue ("filter", BWireIDs::filter, [] (float v) { return freqText (v); }, [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); }, {});
    addCheck ("auto", BWireIDs::levelAuto);
    alignLastRight();
    levelBox = &addValue ("level", BWireIDs::level, db, stepDb, {});
    alignLastRight();
    mixBox.extraWidth = juce::jmax (0, levelBox->getIdealWidth() - mixBox.getIdealWidth());   // mix e level in colonna, stessa larghezza
    infoText = {};
    setDisplay (scope);
    resized();
}

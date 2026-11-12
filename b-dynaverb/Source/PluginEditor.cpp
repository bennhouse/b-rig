#include "PluginEditor.h"

void ZoneMeter::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto all = getLocalBounds().toFloat();
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    auto bar = all.withTrimmedTop (34.0f).withTrimmedBottom (54.0f);
    const float mid = std::round (bar.getCentreX());
    // due emisferi: ritmo a sinistra (grigio), lead a destra (oro)
    g.setColour (juce::Colour (0xff181818));
    g.fillRect (bar.withRight (mid));
    g.setColour (accent.withAlpha (0.07f));
    g.fillRect (bar.withLeft (mid));
    g.setColour (gridLine);
    outline (g, bar);
    g.setColour (gridZero);
    g.fillRect (mid, bar.getY(), kLine, bar.getHeight());
    drawTag (g, "rhythm zone", bar.getX() + 8.0f, all.getY() + 8.0f + tagH);
    drawAtBaseline (g, "LEAD ZONE", bar.getRight() - 8.0f, all.getY() + 8.0f + tagH, accent, kTagSize, juce::Justification::right, Face::SansSemiBold, 0.08f);

    // scia: le ultime posizioni del cursore, dal basso (vecchie) all'alto (recenti)
    const int last = proc.historyPos.load (std::memory_order_acquire);
    const int rows = 64;
    for (int k = 0; k < rows; ++k)
    {
        const auto& f = proc.history[(size_t) ((last - (rows - 1 - k)) & (BDynaverbProcessor::kHistory - 1))];
        const float x = bar.getX() + f.score * bar.getWidth();
        const float y = bar.getBottom() - 4.0f - (bar.getHeight() - 8.0f) * (float) k / (float) (rows - 1);
        const float age = (float) k / (float) rows;
        g.setColour ((f.alpha > 0.5f ? accent : text).withAlpha (0.08f + 0.35f * age));
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
    // cursore luminoso
    const float score = proc.scoreNow.load(), alpha = proc.alphaNow.load();
    const float cx = bar.getX() + score * bar.getWidth();
    const auto c = text.interpolatedWith (accent, alpha);
    g.setColour (c.withAlpha (0.18f));
    g.fillRect (cx - 7.0f, bar.getY() + 1.0f, 14.0f, bar.getHeight() - 2.0f);
    g.setColour (c);
    g.fillRect (cx - 1.0f, bar.getY() + 1.0f, 2.0f, bar.getHeight() - 2.0f);

    // quanto è acceso il canale lead, e il delay in uso
    auto lane = all.withTop (bar.getBottom() + 14.0f).withHeight (6.0f);
    g.setColour (valueFill);
    g.fillRect (lane);
    g.setColour (accent);
    g.fillRect (lane.withWidth (lane.getWidth() * alpha));
    const float base = all.getBottom() - 8.0f;
    drawAtBaseline (g, "lead channel " + juce::String (juce::roundToInt (100.0f * alpha)) + " %", all.getX(), base, alpha > 0.01f ? accent : info, kAxisSize,
                    juce::Justification::left, Face::MonoMedium);
    const int note = proc.noteNow.load();
    const juce::String d = note >= 0 ? juce::String (BDynaverbProcessor::kNoteNames[note]) + " at " + juce::String (juce::roundToInt (proc.bpmNow.load())) + " bpm = "
                                       + juce::String (juce::roundToInt (proc.delayMsNow.load())) + " ms"
                                     : "delay " + juce::String (juce::roundToInt (proc.delayMsNow.load())) + " ms";
    drawAtBaseline (g, d, all.getRight(), base, info, kAxisSize, juce::Justification::right);
}

BDynaverbEditor::BDynaverbEditor (BDynaverbProcessor& p)
    : ShellEditor (p, p.apvts, "dynaverb", 900, 400), meter (p)
{
    using namespace ui;
    addHeaderChoice ("crossover", BDynaverbIDs::crossover, { "low-mid", "mid", "high-neck" }).onUserChange = [&p] (int seg) { p.applyCrossover (seg); };
    addHeaderCheck ("duck delay", BDynaverbIDs::duck);
    addHeaderCheck ("sync", BDynaverbIDs::sync);
    addValue ("split", BDynaverbIDs::split, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; },
              [] (float v, int d) { return std::round (v / 10.0f + (float) d) * 10.0f; }, { "800 Hz" });
    addValue ("boost", BDynaverbIDs::boost, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+6.0 dB" });
    addValue ("delay", BDynaverbIDs::delay, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; },
              [] (float v, int d) { return std::round (v / 10.0f + (float) d) * 10.0f; }, { "1200 ms" });
    addValue ("space", BDynaverbIDs::space, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    addValue ("speed", BDynaverbIDs::speed, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 ms" });
    infoText = "stereo out";
    setDisplay (meter);
    displayRefreshesItself = true;
    resized();
}

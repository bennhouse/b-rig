#include "PluginEditor.h"

TimeSlot::TimeSlot (BDelayProcessor& p) : proc (p)
{
    ms.setParameter (p.apvts.getParameter (BDelayIDs::time));
    ms.format = [] (float v) { return v < 100.0f ? ui::num (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    ms.step = [] (float v, int d) { return v < 100.0f ? std::round (v) + (float) d : std::round (v / 10.0f + (float) d) * 10.0f; };
    ms.drag = [&p] (float, float startNorm, float dx) { auto* a = p.apvts.getParameter (BDelayIDs::time); return a->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.002f)); };
    ms.sizeSamples = { "2000 ms", "88.8 ms" };
    div.setParameter (p.apvts.getParameter (BDelayIDs::division));
    div.format = [] (float v) { return BDelayProcessor::divisionNames()[juce::jlimit (0, BDelayProcessor::kDivisions - 1, juce::roundToInt (v))]; };
    div.step = [] (float v, int d) { return (float) juce::jlimit (0, BDelayProcessor::kDivisions - 1, juce::roundToInt (v) + d); };
    div.drag = [&p] (float, float startNorm, float dx) { auto* a = p.apvts.getParameter (BDelayIDs::division); return a->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.004f)); };
    div.sizeSamples = { "2000 ms", "1/16D" };   // stessa larghezza del riquadro in ms: il layout non si sposta
    addAndMakeVisible (ms);
    addChildComponent (div);
    update();
}

void TimeSlot::update()
{
    const bool sync = proc.apvts.getRawParameterValue (BDelayIDs::sync)->load() > 0.5f;
    if (div.isVisible() != sync) { div.setVisible (sync); ms.setVisible (! sync); }
}

void TapButton::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto r = getLocalBounds().toFloat();
    if (flash > 0) { g.setColour (accent); g.fillRect (r); }
    else { g.setColour (isMouseOver() ? labelHover : text); outline (g, r); }
    drawLabel (g, "tap", getLocalBounds(), flash > 0 ? onAccent : (isMouseOver() ? labelHover : selector), kControlTextSize,
               juce::Justification::centred, Face::SansSemiBold);
}

void DelayGraph::paint (juce::Graphics& g)
{
    using namespace ui;
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const auto all = getLocalBounds().toFloat();
    const auto plot = all.withTrimmedBottom (18.0f).withTrimmedLeft (18.0f).reduced (0.0f, 1.0f);
    constexpr float kMaxMs = 2000.0f;
    auto xOf = [&] (float ms) { return plot.getX() + plot.getWidth() * ms / kMaxMs; };
    const float mid = std::round (plot.getCentreY()), half = plot.getHeight() * 0.5f - 6.0f;

    for (float t = 0.0f; t <= kMaxMs; t += 250.0f)
    {
        g.setColour (gridLine);
        g.fillRect (std::round (xOf (t)), plot.getY(), kLine, plot.getHeight());
        if (std::fmod (t, 500.0f) < 1.0f)
            drawAtBaseline (g, t == 0.0f ? juce::String ("0") : juce::String ((int) t) + " ms", xOf (t), all.getBottom() - 4.0f, info, kAxisSize,
                            t == 0.0f ? juce::Justification::left : (t >= kMaxMs ? juce::Justification::right : juce::Justification::horizontallyCentred));
    }
    for (float f : { 0.5f, 1.0f })
    {
        g.setColour (gridLine);
        g.fillRect (plot.getX(), std::round (mid - half * f), plot.getWidth(), kLine);
        g.fillRect (plot.getX(), std::round (mid + half * f), plot.getWidth(), kLine);
    }
    g.setColour (gridZero);
    g.fillRect (plot.getX(), mid, plot.getWidth(), kLine);
    g.setColour (gridLine);
    outline (g, plot);
    drawAtBaseline (g, "L", all.getX() + 4.0f, mid - half * 0.5f + 4.0f, info, kAxisSize);
    drawAtBaseline (g, "R", all.getX() + 4.0f, mid + half * 0.5f + 4.0f, info, kAxisSize);

    // il suono che viaggia nella linea: il contenuto a ogni ritardo (L sopra, R sotto), ingresso grigio a 0 ms
    const int last = proc.ringPos.load (std::memory_order_acquire);
    const float fs = (float) (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    auto ringAt = [&] (const std::array<float, BDelayProcessor::kRing>& r, float ms)
    {
        const int back = (int) (ms * 0.001f * fs / (float) BDelayProcessor::kDecim);
        return back < BDelayProcessor::kRing ? r[(size_t) ((last - back) & (BDelayProcessor::kRing - 1))] : 0.0f;
    };
    const int cols = (int) plot.getWidth();
    for (int x = 0; x < cols; ++x)
    {
        const float ms = kMaxMs * (float) x / (float) cols;
        const float a = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringL, ms))), b = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringR, ms)));
        g.setColour (accent.withAlpha (0.28f));
        if (a > 0.01f) g.fillRect (plot.getX() + (float) x, mid - a * half, 1.0f, a * half);
        if (b > 0.01f) g.fillRect (plot.getX() + (float) x, mid + 1.0f, 1.0f, b * half);
    }
    {
        const float in = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringIn, 0.0f)));
        g.setColour (text.withAlpha (0.6f));
        g.fillRect (plot.getX() + 1.0f, mid - in * half, 4.0f, 2.0f * in * half);
    }

    // tap previsti: posizione da time / offset / ping-pong, altezza dal feedback, campana più larga con lo smear
    const float tL = proc.msL.load(), tR = proc.msR.load(), fb = v (BDelayIDs::feedback) / 100.0f;
    const float smear = v (BDelayIDs::smear) / 100.0f;
    const bool pp = v (BDelayIDs::pingpong) > 0.5f;
    auto drawTap = [&] (float ms, float amp, bool left)
    {
        if (ms > kMaxMs || amp < 0.01f) return;
        const float x = xOf (ms), h = juce::jmin (1.0f, amp) * half;
        const float lit = juce::jmin (1.0f, 3.0f * ringAt (left ? proc.ringL : proc.ringR, ms));   // si accende quando l'eco suona
        g.setColour (accent.withAlpha (0.55f + 0.45f * lit));
        if (smear < 0.02f) g.fillRect (std::round (x) - 1.0f, left ? mid - h : mid + 1.0f, 2.0f, h);
        else
        {
            const float w = 3.0f + smear * 0.012f * plot.getWidth();
            juce::Path bell;
            bell.startNewSubPath (x - w, mid);
            for (int k = -12; k <= 12; ++k)
            {
                const float u = (float) k / 12.0f;
                bell.lineTo (x + u * w, mid + (left ? -1.0f : 1.0f) * h * std::exp (-4.0f * u * u));
            }
            bell.lineTo (x + w, mid); bell.closeSubPath();
            g.fillPath (bell);
        }
    };
    if (pp)
    {
        float t = 0.0f, amp = 1.0f;
        for (int k = 0; k < 64; ++k)
        {
            const bool left = (k % 2) == 0;
            t += left ? tL : tR;
            drawTap (t, amp, left);
            amp *= fb;
            if (t > kMaxMs) break;
        }
    }
    else
    {
        float amp = 1.0f;
        for (int k = 1; k < 64; ++k, amp *= fb) { drawTap (tL * (float) k, amp, true); drawTap (tR * (float) k, amp, false); if (tL * (float) k > kMaxMs && tR * (float) k > kMaxMs) break; }
    }
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    juce::String info2 = num (tL, tL < 100.0f ? 1 : 0) + " ms";
    if (std::abs (tR - tL) > 0.05f) info2 += " / " + num (tR, tR < 100.0f ? 1 : 0) + " ms";
    if (v (BDelayIDs::sync) > 0.5f) info2 = BDelayProcessor::divisionNames()[(int) v (BDelayIDs::division)] + " at " + juce::String (juce::roundToInt (proc.bpmNow.load())) + " bpm = " + info2;
    drawTag (g, "in", plot.getX() + 10.0f, plot.getY() + 8.0f + tagH);
    drawAtBaseline (g, "ECHO", plot.getX() + 32.0f, plot.getY() + 8.0f + tagH, accent, kTagSize, juce::Justification::left, Face::SansSemiBold, 0.08f);
    drawValueTag (g, info2, plot.getRight() - 8.0f, plot.getY() + 8.0f + tagH, juce::Justification::right);
}

BDelayEditor::BDelayEditor (BDelayProcessor& p)
    : ShellEditor (p, p.apvts, "delay", 1100, 470), timeSlot (p), graph (p)
{
    using namespace ui;
    setHeaderChoice ("type", BDelayIDs::type, { "digital", "tape", "bbd", "lofi", "reverse" });
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto oct6 = [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); };
    addComponent ("time", timeSlot, timeSlot.idealWidth());
    addCheck ("sync", BDelayIDs::sync);
    tapBtn.onTap = [&p] { p.tap(); };
    tapBtn.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addComponent ({}, tapBtn, 46);
    addValue ("feedback", BDelayIDs::feedback, pct, stepPct, { "110 %" });
    addValue ("locut", BDelayIDs::locut, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; }, oct6, { "800 Hz", "888 Hz" });
    addValue ("hicut", BDelayIDs::hicut, [] (float v) { return freqText (v); }, oct6, { "20.0 kHz", "8.88 kHz" });
    addValue ("mix", BDelayIDs::mix, pct, stepPct, { "100 %" });
    beginColumn (1);
    addValue ("offset", BDelayIDs::offset, [] (float v) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; }, stepPct, { "-50 %", "+50 %" });
    addCheck ("ping-pong", BDelayIDs::pingpong);
    addValue ("mod", BDelayIDs::mod, pct, stepPct, { "100 %" });
    beginColumn (2);
    addValue ("smear", BDelayIDs::smear, pct, stepPct, { "100 %" });
    addValue ("duck", BDelayIDs::duck, pct, stepPct, { "100 %" });
    addValue ("output", BDelayIDs::output, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+12.0 dB", "-24.0 dB" });
    addCheck ("trails", BDelayIDs::trails);
    endColumn();
    setDisplay (graph);
    displayRefreshesItself = true;
    resized();
}

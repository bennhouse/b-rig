#include "PluginEditor.h"
#include <complex>

void VerbGraph::paint (juce::Graphics& g)
{
    using namespace ui;
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const int type = juce::jlimit (0, 4, (int) v (BVerbIDs::type));
    const float pre = v (BVerbIDs::predelay) / 1000.0f, decay = v (BVerbIDs::decay), size = v (BVerbIDs::size) / 100.0f;
    const float early = v (BVerbIDs::early) / 100.0f, damp = v (BVerbIDs::damp);
    const bool freeze = v (BVerbIDs::freeze) > 0.5f;
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    const auto all = getLocalBounds().toFloat();
    auto plot = all.withTrimmedLeft (34.0f).withTrimmedBottom (18.0f).withTrimmedTop (8.0f).withTrimmedRight (1.0f);
    // asse dei tempi: abbastanza per vedere tutta la coda (fino a 21 s)
    const float tMax = juce::jlimit (0.25f, 21.0f, (pre + (freeze ? 2.0f : decay)) * 1.15f);
    auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * t / tMax; };
    auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };

    // griglia: dB ogni 12, tempi a passi "tondi"
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        g.setColour (db == 0.0f ? gridZero : gridLine);
        g.fillRect (plot.getX(), std::round (yOf (db)), plot.getWidth(), kLine);
        drawAtBaseline (g, juce::String ((int) db), plot.getX() - 6.0f, yOf (db) + 4.0f, info, kAxisSize, juce::Justification::right);
    }
    float step = 0.05f;
    for (float c : { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f }) if (tMax / step > 8.0f) step = c;
    for (float t = 0.0f; t <= tMax + 1.0e-4f; t += step)
    {
        g.setColour (gridLine);
        g.fillRect (std::round (xOf (t)), plot.getY(), kLine, plot.getHeight());
        const juce::String lab = step < 1.0f ? juce::String (juce::roundToInt (t * 1000.0f)) + (t == 0.0f ? "" : "") : juce::String (t, 0) + " s";
        drawAtBaseline (g, t == 0.0f ? juce::String ("0") : (step < 1.0f ? lab + " ms" : lab), xOf (t), all.getBottom() - 4.0f, info, kAxisSize,
                        t == 0.0f ? juce::Justification::left : juce::Justification::horizontallyCentred);
    }
    g.setColour (gridLine);
    outline (g, plot);

    // ingresso (grigio) e wet (ciano) dal vivo: gli ultimi tMax secondi, il più recente a destra
    {
        const int last = proc.historyPos.load (std::memory_order_acquire);
        const float framesPerSec = (float) fs / 256.0f;
        const int cols = (int) plot.getWidth();
        juce::Path wet;
        bool started = false;
        for (int x = 0; x < cols; ++x)
        {
            const float ago = tMax * (1.0f - (float) x / (float) cols);
            const int back = (int) (ago * framesPerSec);
            if (back >= BVerbProcessor::kHistory) continue;
            const auto& f = proc.history[(size_t) ((last - back) & (BVerbProcessor::kHistory - 1))];
            const float inDb = sp::gainToDb (f.in), wetDb = sp::gainToDb (f.wet);
            if (inDb > -60.0f) { g.setColour (text.withAlpha (0.18f)); g.fillRect (plot.getX() + (float) x, yOf (inDb), 1.0f, plot.getBottom() - yOf (inDb)); }
            const float y = yOf (juce::jmax (-60.0f, wetDb));
            if (! started) { wet.startNewSubPath (plot.getX() + (float) x, y); started = true; } else wet.lineTo (plot.getX() + (float) x, y);
        }
        g.setColour (accent.withAlpha (0.45f));
        g.strokePath (wet, juce::PathStrokeType (1.0f));
    }

    // prime riflessioni: pali verticali (spostati dal predelay, alti quanto "early")
    float lastTap = pre;
    for (int k = 0; k < BVerbProcessor::kTaps; ++k)
    {
        const float t = pre + BVerbProcessor::tapTimeMs (type, size, k, false) / 1000.0f;
        const float lvl = early * BVerbProcessor::kTypeEarly[type] * BVerbProcessor::tapGain (k);
        lastTap = juce::jmax (lastTap, t);
        if (lvl <= 0.001f) continue;
        const float y = yOf (20.0f * std::log10 (lvl));
        g.setColour (accent.withAlpha (0.8f));
        g.fillRect (std::round (xOf (t)) - 1.0f, y, 2.0f, plot.getBottom() - y);
    }
    // inviluppo del decadimento: il corpo (pieno) e le alte a 8 kHz (più corte per il damp nel loop)
    const float start = pre + 0.004f;
    if (freeze)
    {
        g.setColour (accent);
        g.drawLine (xOf (start), yOf (-3.0f), plot.getRight(), yOf (-3.0f), 1.5f);
        drawTag (g, "freeze", plot.getRight() - 8.0f, yOf (-3.0f) - 6.0f, juce::Justification::right);
    }
    else
    {
        // alte a 8 kHz: feedback per passaggio g * |lp + g (1 - lp)| (filtro assorbente), passaggi al secondo = fs / lunghezza media
        const double c = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * damp / fs);
        const std::complex<double> z = std::polar (1.0, -juce::MathConstants<double>::twoPi * 8000.0 / fs);
        const std::complex<double> hlp = c / (1.0 - (1.0 - c) * z);
        const double meanLen = 0.0558 * (0.25 + 0.75 * size) * BVerbProcessor::kTypeScale[type];   // s, media delle 8 linee
        const double gl = std::pow (10.0, -3.0 * meanLen / decay);
        const double perPass = gl * std::abs (hlp + gl * (1.0 - hlp));
        const float rtHi = (float) juce::jmin ((double) decay, 60.0 / (-20.0 * std::log10 (perPass) / meanLen));
        g.setColour (accent.withAlpha (0.45f));
        g.drawLine (xOf (start), yOf (0.0f), xOf (start + rtHi), yOf (-60.0f), 1.0f);
        g.setColour (accent);
        g.drawLine (xOf (start), yOf (0.0f), xOf (start + decay), yOf (-60.0f), 1.5f);
        drawValueTag (g, "8 kHz " + num (rtHi, 2) + " s", xOf (start + rtHi * 0.5f) + 6.0f, yOf (-30.0f) - 4.0f);
    }
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    const float wetW = juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kTagSize, 0.08f), "WET");
    drawAtBaseline (g, "WET", plot.getRight() - 8.0f, plot.getY() + 8.0f + tagH, accent, kTagSize, juce::Justification::right, Face::SansSemiBold, 0.08f);
    drawTag (g, "in", plot.getRight() - 20.0f - wetW, plot.getY() + 8.0f + tagH, juce::Justification::right);
    drawValueTag (g, "wet " + freqText (v (BVerbIDs::locut)) + " - " + freqText (v (BVerbIDs::hicut)), plot.getRight() - 8.0f, plot.getY() + 14.0f + 2.0f * tagH,
                  juce::Justification::right);
}

BVerbEditor::BVerbEditor (BVerbProcessor& p)
    : ShellEditor (p, p.apvts, "verb", 1100, 480), graph (p)
{
    using namespace ui;
    setHeaderChoice ("type", BVerbIDs::type, { "room", "plate", "hall", "spring", "shimmer" });
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto oct6 = [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); };
    addValue ("decay", BVerbIDs::decay, [] (float v) { return num (v, 2) + " s"; },
              [] (float v, int d) { return v * std::pow (2.0f, (float) d / 4.0f); }, { "20.00 s", "8.88 s" });
    addValue ("predelay", BVerbIDs::predelay, [] (float v) { return num (v, 1) + " ms"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "200.0 ms", "88.8 ms" });
    addValue ("size", BVerbIDs::size, pct, stepPct, { "100 %" });
    addValue ("locut", BVerbIDs::locut, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; }, oct6, { "500 Hz", "888 Hz" });
    addValue ("hicut", BVerbIDs::hicut, [] (float v) { return freqText (v); }, oct6, { "20.0 kHz", "8.88 kHz" });
    addValue ("mix", BVerbIDs::mix, pct, stepPct, { "100 %" });
    beginColumn (1);
    addValue ("early", BVerbIDs::early, pct, stepPct, { "100 %" });
    addValue ("damp", BVerbIDs::damp, [] (float v) { return freqText (v); }, oct6, { "16.0 kHz", "8.88 kHz" });
    addValue ("mod", BVerbIDs::mod, pct, stepPct, { "100 %" });
    beginColumn (2);
    addValue ("duck", BVerbIDs::duck, pct, stepPct, { "100 %" });
    addValue ("stereo", BVerbIDs::stereo, pct, stepPct, { "100 %" });
    addCheck ("freeze", BVerbIDs::freeze);
    addValue ("output", BVerbIDs::output, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+12.0 dB", "-24.0 dB" });
    endColumn();
    setDisplay (graph);
    displayRefreshesItself = true;
    resized();
}

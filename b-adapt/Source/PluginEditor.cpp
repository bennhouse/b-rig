#include "PluginEditor.h"

void AdaptDisplay::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto a = getLocalBounds().toFloat().withTrimmedBottom (20.0f).withTrimmedLeft (30.0f).withTrimmedTop (8.0f).withTrimmedRight (12.0f);
    auto xFor = [&] (double hz) { return a.getX() + a.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (float db) { return a.getY() + a.getHeight() * (12.0f - db) / 36.0f; };   // +12 .. -24 dB
    for (float db : { 12.0f, 0.0f, -12.0f, -24.0f })
    {
        g.setColour (juce::exactlyEqual (db, 0.0f) ? gridZero : gridLine);
        g.fillRect (a.getX(), std::round (yFor (db)), a.getWidth(), kLine);
        drawLabel (g, (db > 0 ? "+" : "") + juce::String ((int) db), { 0, juce::roundToInt (yFor (db)) - 8, 22, 16 }, info, kAxisSize,
                   juce::Justification::centredRight, Face::MonoRegular);
    }
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    for (auto [hz, t] : { std::pair<double, const char*> { 20, "20" }, { 100, "100" }, { 1000, "1k" }, { 3400, "3.4k" }, { 10000, "10k" }, { 20000, "20k" } })
    {
        g.setColour (gridLine);
        g.fillRect (std::round (xFor (hz)), a.getY(), kLine, a.getHeight());
        drawAtBaseline (g, t, xFor (hz), base, info, kAxisSize, juce::Justification::horizontallyCentred);
    }
    // spettro dell'uscita (0 dB in alto, -72 dB in basso); le zone "calde" (2-5 kHz e 6-10 kHz, dove
    // l'orecchio è più sensibile) diventano rosse quando sporgono di oltre 3 dB sopra le medie 0,2-2 kHz
    if (visible)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (a.toNearestInt());
        auto freqForX = [&] (float x) { return 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth())); };
        spectrum.draw (g, a, freqForX, text, 0.0f, -72.0f, 0.08f, 0.45f);
        double ref = 0; int nRef = 0;
        for (double f = 200; f < 2000; f *= 1.06) { ref += spectrum.levelBetween (f, f * 1.06); ++nRef; }
        ref /= juce::jmax (1, nRef);
        const juce::Colour hot (0xffff4d4d);
        for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
        {
            const double fa = freqForX (x - 0.5f), fb = freqForX (x + 0.5f);
            if (! ((fa > 2000 && fb < 5000) || (fa > 6000 && fb < 10000))) continue;
            const float lvl = spectrum.levelBetween (fa, fb);
            const float over = lvl - (float) ref - 3.0f;
            if (over <= 0.0f) continue;
            const float y = juce::jmap (lvl, 0.0f, -72.0f, a.getY(), a.getBottom());
            g.setColour (hot.withAlpha (juce::jlimit (0.25f, 0.8f, 0.25f + over / 12.0f)));
            g.fillRect (x - 0.5f, y, 1.0f, a.getBottom() - y);
        }
    }

    juce::Path p;
    for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth()));
        const float y = yFor (juce::jlimit (-26.0f, 14.0f, proc.responseDb (hz)));
        if (x <= a.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (a.toNearestInt());
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
    // correlazione L/R in basso a destra: barra da -1 a +1, segno nel colore di accento
    const auto m = juce::Rectangle<float> (a.getRight() - 128.0f, a.getBottom() - 22.0f, 120.0f, 8.0f);
    // fondo pieno sotto i due meter e le loro etichette: curve e spettro non ci passano attraverso
    g.setColour (background);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (m.getX() - 70.0f, m.getY() - 24.0f, m.getRight() + 4.0f, m.getBottom() + 4.0f));
    g.setColour (text); outline (g, m);
    g.setColour (gridZero); g.fillRect (std::round (m.getCentreX()), m.getY(), kLine, m.getHeight());
    if (proc.hasSignal.load())   // a riposo: nessun segno
    {
        const float c = juce::jlimit (-1.0f, 1.0f, proc.correlation.load());
        g.setColour (c < 0 ? juce::Colour (0xffff4d4d) : accent);
        g.fillRect (std::round (m.getX() + (c + 1.0f) * 0.5f * (m.getWidth() - 3.0f)), m.getY() + 1.0f, 3.0f, m.getHeight() - 2.0f);
    }
    drawTag (g, "corr", m.getX() - 6.0f, m.getBottom(), juce::Justification::right);
    // safe width al lavoro: quanto il side viene abbassato adesso (barra da destra, 12 dB = piena)
    const auto sm = m.translated (0.0f, -16.0f);
    g.setColour (text); outline (g, sm);
    const float cut = proc.sideCutDb.load();
    if (cut > 0.05f)
    {
        g.setColour (accent);
        const float w = std::round ((sm.getWidth() - 2.0f) * juce::jmin (1.0f, cut / 12.0f));
        g.fillRect (sm.getRight() - 1.0f - w, sm.getY() + 1.0f, w, sm.getHeight() - 2.0f);
    }
    drawTag (g, "side " + (cut > 0.05f ? "-" + num (cut, 1) : juce::String ("0.0")), sm.getX() - 6.0f, sm.getBottom(), juce::Justification::right);
}

BAdaptEditor::BAdaptEditor (BAdaptProcessor& p)
    : ShellEditor (p, p.apvts, "adapt", 760, 390), proc (p), display (p)
{
    using namespace ui;
    setHeaderChoice ("monitor", BAdaptIDs::monitor, { "off", "phone", "laptop" });
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    addValue ("safe width", BAdaptIDs::safeWidth, pct, stepPct, { "100 %" });
    addValue ("thump", BAdaptIDs::thump, pct, stepPct, { "100 %" });
    addValue ("de-harsh", BAdaptIDs::deharsh, pct, stepPct, { "100 %" });
    setDisplay (display);
    resized();
    proc.spectrumActive.store (true);
}

void BAdaptEditor::tick()
{
    frameColour = proc.isAuditioning() ? kAudition : ui::frame;
    display.update();
    display.repaint();
    repaint (box().removeFromTop (2));
    repaint();
}

void BAdaptEditor::paintOverChildren (juce::Graphics& g)
{
    if (! proc.isAuditioning()) return;
    // avviso: riquadro verde (2 px) e scritta grande sopra il grafico
    g.setColour (kAudition);
    ui::outline (g, box().toFloat());
    ui::outline (g, box().toFloat().reduced (1.0f));
    const auto font = ui::face (ui::Face::SansBold, 22.0f, 0.04f);
    const juce::String warn ("AUDITION ACTIVE - BYPASS BEFORE EXPORT");
    const auto area = display.getBounds().withTrimmedBottom (40);
    // fondo pieno dietro la scritta: la curva non la attraversa
    const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, warn)) + 24;
    g.setColour (ui::background);
    g.fillRect (area.withSizeKeepingCentre (w, 34));
    g.setColour (kAudition);
    g.setFont (font);
    g.drawText (warn, area, juce::Justification::centred);
}

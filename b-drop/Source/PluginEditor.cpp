#include "PluginEditor.h"

namespace
{
    int semisOf (BDropProcessor& p) { auto* a = p.apvts.getParameter (BDropIDs::semitones); return juce::roundToInt (a->convertFrom0to1 (a->getValue())); }
    juce::String signedText (int s) { return s > 0 ? "+" + juce::String (s) : juce::String (s); }
}

juce::Rectangle<float> DropDisplay::drum() const
{
    const auto z = zone();
    // centrato nel riquadro, come le scritte sotto
    return juce::Rectangle<float> (120.0f, z.getHeight() - 70.0f).withCentre ({ z.getCentreX(), z.getY() + 18.0f + (z.getHeight() - 70.0f) * 0.5f });
}

bool DropDisplay::locked() const { return (int) proc.apvts.getRawParameterValue (BDropIDs::range)->load() == BDropProcessor::octave; }

void DropDisplay::setSemitones (int v)
{
    int lo, hi; BDropProcessor::rangeLimits ((int) proc.apvts.getRawParameterValue (BDropIDs::range)->load(), lo, hi);
    auto* a = proc.apvts.getParameter (BDropIDs::semitones);
    a->setValueNotifyingHost (a->convertTo0to1 ((float) juce::jlimit (lo, hi, v)));
}

void DropDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (locked() || ! drum().expanded (8.0f).contains (e.position)) return;
    dragging = true;
    startValue = semisOf (proc);
    startY = e.position.y;
    nudge = 0.0f;
    proc.apvts.getParameter (BDropIDs::semitones)->beginChangeGesture();
}

// il rullo segue il dito per mezzo scatto, poi passa al numero dopo: si sente lo scatto, niente animazioni
void DropDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = startY - e.position.y;   // su = più semitoni
    const int steps = juce::roundToInt (dy / kStepPx);
    setSemitones (startValue + steps);
    const int now = semisOf (proc);
    nudge = now == startValue + steps ? (dy / kStepPx - (float) steps) * kSlot * 0.5f : 0.0f;   // fermo al limite del range
    repaint();
}

void DropDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    nudge = 0.0f;
    proc.apvts.getParameter (BDropIDs::semitones)->endChangeGesture();
    repaint();
}

void DropDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (locked() || ! drum().contains (e.position)) return;
    auto* a = proc.apvts.getParameter (BDropIDs::semitones);
    a->beginChangeGesture(); setSemitones (0); a->endChangeGesture();
    repaint();
}

void DropDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (! locked() && drum().contains (e.position) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void DropDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (locked() || ! zone().contains (e.position)) return;
    wheelAcc += w.deltaY;
    if (std::abs (wheelAcc) < 0.08f) return;
    auto* a = proc.apvts.getParameter (BDropIDs::semitones);
    a->beginChangeGesture(); setSemitones (semisOf (proc) + (wheelAcc > 0 ? 1 : -1)); a->endChangeGesture();
    wheelAcc = 0.0f;
    repaint();
}

void DropDisplay::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto all = getLocalBounds();
    const bool on = proc.apvts.getRawParameterValue (BDropIDs::on)->load() > 0.5f;
    const bool lock = locked();
    const juce::Colour hue = on ? accent : frame;
    const int st = semisOf (proc);
    const float amount = (float) std::abs (st) / 12.0f;   // 0 a riposo, 1 a un'ottava

    // ---- rullo dei semitoni
    const auto z = zone();
    g.setColour (gridLine);
    outline (g, z);
    const auto d = drum();
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillRect (d);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (d.toNearestInt());
        const float cy = d.getCentreY();
        int lo, hi; BDropProcessor::rangeLimits ((int) proc.apvts.getRawParameterValue (BDropIDs::range)->load(), lo, hi);
        for (int k = -3; k <= 3; ++k)
        {
            const int v = st + k;
            if (v < -12 || v > 12) continue;
            const float y = cy - (float) k * kSlot + nudge;
            const float dist = juce::jmin (1.0f, std::abs (y - cy) / (kSlot * 2.5f));
            const float size = 40.0f - 18.0f * dist;
            const bool reachable = v >= lo && v <= hi;
            juce::Colour c = k == 0 && dragging ? valueText : (k == 0 ? valueText.interpolatedWith (hue, amount) : text);
            c = c.withAlpha ((reachable ? 1.0f : 0.35f) * (1.0f - 0.75f * dist));
            drawAtBaseline (g, signedText (v), d.getCentreX() - (k == 0 ? 6.0f : 0.0f), std::round (y + capHeight (size, Face::MonoMedium) * 0.5f), c, size,
                            juce::Justification::horizontallyCentred, Face::MonoMedium);
        }
    }
    // finestra di lettura: due righe ai lati del numero scelto, bordi del rullo
    g.setColour (lock ? frame : text);
    g.fillRect (d.getX(), std::round (d.getCentreY() - kSlot * 0.5f), d.getWidth(), kLine);
    g.fillRect (d.getX(), std::round (d.getCentreY() + kSlot * 0.5f), d.getWidth(), kLine);
    g.setColour (frame);
    outline (g, d);
    // unità dentro la finestra di lettura, sulla stessa linea di base del numero
    drawAtBaseline (g, "st", d.getRight() - 8.0f, std::round (d.getCentreY() + capHeight (40.0f, Face::MonoMedium) * 0.5f), unitText, kUnitSize,
                    juce::Justification::right, Face::MonoRegular);
    // barra accanto al rullo: si riempie da 0 verso l'alto (su) o verso il basso (giù), fino a un'ottava
    {
        const float bx = d.getRight() + 14.0f, half = d.getHeight() * 0.5f - 4.0f, cy = d.getCentreY();
        g.setColour (gridLine);
        g.fillRect (bx, d.getY() + 4.0f, 4.0f, d.getHeight() - 8.0f);
        g.setColour (gridZero);
        g.fillRect (bx - 3.0f, std::round (cy), 10.0f, kLine);
        if (st != 0)
        {
            g.setColour (hue);
            const float h = amount * half;
            g.fillRect (bx, st > 0 ? cy - h : cy, 4.0f, h);
        }
    }
    // sotto: cosa vuol dire, in parole
    const juce::String what = lock ? "one octave down (fixed)"
                            : st == 0 ? "standard pitch"
                            : st == -12 ? "one octave down" : st == 12 ? "one octave up"
                            : st < 0 ? juce::String (-st) + (st == -1 ? " semitone down" : " semitones down")
                                     : juce::String (st) + (st == 1 ? " semitone up" : " semitones up");
    drawAtBaseline (g, what, d.getCentreX(), z.getBottom() - 30.0f, st == 0 ? info : labelStatic, kInfoSize, juce::Justification::horizontallyCentred, Face::SansRegular);
    drawAtBaseline (g, lock ? "" : "drag or scroll", d.getCentreX(), z.getBottom() - 12.0f, info, kAxisSmall, juce::Justification::horizontallyCentred);

    // ---- forme d'onda: DI (grigio) e uscita (viola), agganciate a un passaggio per lo zero dell'ingresso
    auto a = all.toFloat().withTrimmedLeft (z.getWidth() + 12.0f);
    g.setColour (gridLine);
    outline (g, a);
    g.setColour (gridZero);
    g.fillRect (a.getX(), std::round (a.getCentreY()), a.getWidth(), kLine);
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    drawTag (g, "di in", a.getX() + 8.0f, a.getY() + 8.0f + tagH);
    drawAtBaseline (g, "OUT", a.getX() + 8.0f + juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kTagSize, 0.08f), "DI IN") + 12.0f,
                    a.getY() + 8.0f + tagH, hue, kTagSize, juce::Justification::left, Face::SansSemiBold, 0.08f);
    if (pickGlow > 0 && on && st != 0 && proc.apvts.getRawParameterValue (BDropIDs::zeroLag)->load() > 0.5f)
        drawAtBaseline (g, "PICK 0 MS", a.getRight() - 8.0f, a.getY() + 8.0f + tagH, accent.withAlpha (0.4f + 0.1f * (float) pickGlow), kTagSize,
                        juce::Justification::right, Face::SansSemiBold, 0.08f);
    if (on && st != 0)
        drawTag (g, "phase lock " + juce::String (juce::roundToInt (100.0f * proc.lockQuality.load())) + " %", a.getRight() - 8.0f, a.getBottom() - 8.0f,
                 juce::Justification::right);

    const int len = 960;
    const int last = proc.scopePos.load (std::memory_order_acquire);
    int start = last - len - 1;
    for (int k = 0; k < 1200; ++k)   // trigger: passaggio per lo zero in salita dell'ingresso
    {
        const int i = start - k;
        const float p0 = proc.scopeIn[(size_t) ((i - 1) & (BDropProcessor::kScope - 1))], p1 = proc.scopeIn[(size_t) (i & (BDropProcessor::kScope - 1))];
        if (p0 < 0.0f && p1 >= 0.0f) { start = i; break; }
    }
    float peak = 0.05f;
    for (int k = 0; k < len; ++k)
    {
        const size_t i = (size_t) ((start + k) & (BDropProcessor::kScope - 1));
        peak = juce::jmax (peak, std::abs (proc.scopeIn[i]), std::abs (proc.scopeOut[i]));
    }
    const auto plot = a.reduced (2.0f, 26.0f);
    auto trace = [&] (const std::array<float, BDropProcessor::kScope>& src, juce::Colour c, float thick)
    {
        juce::Path p;
        const int cols = (int) plot.getWidth();
        for (int x = 0; x < cols; ++x)
        {
            const size_t i = (size_t) ((start + x * len / cols) & (BDropProcessor::kScope - 1));
            const float y = plot.getCentreY() - src[i] / peak * plot.getHeight() * 0.5f;
            if (x == 0) p.startNewSubPath (plot.getX(), y); else p.lineTo (plot.getX() + (float) x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    };
    trace (proc.scopeIn, text.withAlpha (0.45f), 1.0f);
    trace (proc.scopeOut, hue, 1.5f);
}

BDropEditor::BDropEditor (BDropProcessor& p)
    : ShellEditor (p, p.apvts, "drop", 760, 421), proc (p), display (p)
{
    using namespace ui;
    addHeaderCheck ("detune", BDropIDs::on);
    addHeaderChoice ("range", BDropIDs::range, { "shift", "octave", "whammy" }).onUserChange = [&p] (int seg) { p.applyRange (seg); };
    addHeaderCheck ("zero-lag pick", BDropIDs::zeroLag);
    addValue ("tension", BDropIDs::tension, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    addValue ("mix", BDropIDs::mix, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "100 %" });
    setDisplay (display);
    displayRefreshesItself = true;
    resized();
}

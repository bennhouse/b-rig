#include "AftercabWidgets.h"

namespace ui
{

// ============================================================================ TempoBox
namespace { constexpr int kArrow = 6, kArrowGap = 3; }

TempoBox::TempoBox (BAftercabProcessor& p) : proc (p) { bpm = p.apvts.getParameter (ParamIDs::revTapBpm); }

int TempoBox::getIdealWidth() const
{
    // caso peggiore: "300.0 bpm" (o "sync"), più il padding e le frecce
    const float w = juce::jmax (makeValueInk ("300.0 bpm").width, makeValueInk ("sync").width);
    return 2 * (kArrow + kArrowGap) + (int) std::ceil (w) + 14;
}

juce::Rectangle<int> TempoBox::boxArea() const { return getLocalBounds().reduced (kArrow + kArrowGap, 0); }

void TempoBox::setBpm (float v)
{
    bpm->setValueNotifyingHost (bpm->convertTo0to1 (juce::jlimit (30.0f, 300.0f, v)));
    if (proc.isSync()) proc.setSync (false);   // regolare i BPM vuol dire tempo manuale
    repaint();
}

void TempoBox::paint (juce::Graphics& g)
{
    const bool en = isEnabled(), sync = proc.isSync();
    const auto box = boxArea().toFloat();
    const float value = bpm->convertFrom0to1 (bpm->getValue());
    if (sync)
    {
        g.setColour (en ? accent : frame); g.fillRect (box);
        drawLabel (g, "sync", boxArea(), en ? onAccent : background, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
    else
    {
        const auto inner = box.reduced (1.0f);
        g.setColour (en ? valueFill : valueFillOff);
        g.fillRect (inner.withWidth (std::round (inner.getWidth() * bpm->getValue())));
        g.setColour (en ? (dragging ? accent : text) : frame); outline (g, box);
        const auto ink = makeValueInk (num (value, 1) + " bpm");
        const float x = box.getX() + (box.getWidth() - ink.width) * 0.5f - ink.left;
        const float base = std::round (box.getCentreY() + ink.capHeight * 0.5f);
        g.setColour (en ? accent : frame); g.fillPath (ink.path, juce::AffineTransform::translation (x, base));
        g.setColour (en ? unitText : frame); g.fillPath (ink.unitPath, juce::AffineTransform::translation (x, base));
    }
    const auto c = getMouseXYRelative();
    chevron (g, { 0.0f, box.getY(), (float) kArrow, box.getHeight() }, -1, ! en ? frame : (c.x < kArrow + kArrowGap && isMouseOver() ? accent : text));
    chevron (g, { (float) (getWidth() - kArrow), box.getY(), (float) kArrow, box.getHeight() }, +1,
             ! en ? frame : (c.x >= getWidth() - kArrow - kArrowGap && isMouseOver() ? accent : text));
}

void TempoBox::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const float v = bpm->convertFrom0to1 (bpm->getValue());
    if (e.x < kArrow + kArrowGap) { bpm->beginChangeGesture(); setBpm (std::round (v) - 1.0f); bpm->endChangeGesture(); return; }
    if (e.x >= getWidth() - kArrow - kArrowGap) { bpm->beginChangeGesture(); setBpm (std::round (v) + 1.0f); bpm->endChangeGesture(); return; }
    dragging = true; moved = false; dragStart = v;
    bpm->beginChangeGesture();
}

void TempoBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || (! moved && e.getDistanceFromDragStart() < 4)) return;   // sotto i 4 px è ancora un clic
    moved = true;
    setBpm (std::round ((dragStart + (float) e.getDistanceFromDragStartX() / 3.0f) * 10.0f) / 10.0f);
}

void TempoBox::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    bpm->endChangeGesture();
    if (moved) { repaint(); return; }
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastClickMs < 250.0)
    {
        // doppio clic veloce: il primo clic non conta (sync torna com'era), si scrivono i BPM
        lastClickMs = -1.0e9;
        proc.setSync (syncBeforeClick);
        editor.onCommit = [this] (const juce::String& t)
        {
            bpm->beginChangeGesture();
            setBpm (InlineEditor::parseNumber (t));
            bpm->endChangeGesture();
        };
        editor.start (*this, boxArea().reduced (1), num (bpm->convertFrom0to1 (bpm->getValue()), 1) + " bpm");
        repaint();
        return;
    }
    lastClickMs = now;
    syncBeforeClick = proc.isSync();
    proc.setSync (! syncBeforeClick);   // clic singolo: sync acceso / spento
    repaint();
}

void TempoBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (! isEnabled() || std::abs (d) <= 0.0f) return;
    bpm->beginChangeGesture();
    setBpm (std::round (bpm->convertFrom0to1 (bpm->getValue())) + (d > 0 ? 1.0f : -1.0f));
    bpm->endChangeGesture();
}

// ============================================================================ TapButton
void TapButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool live = isEnabled() && ! proc.isSync();
    if (live)
    {
        g.setColour (isMouseOver() ? accent.brighter (0.15f) : accent); g.fillRect (r);
        drawLabel (g, "tap", getLocalBounds(), onAccent, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
    else
    {
        g.setColour (frame); outline (g, r);   // oscurato: il tempo arriva dalla DAW
        drawLabel (g, "tap", getLocalBounds(), frame, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
}

// ============================================================================ OutputSection
OutputSection::OutputSection (BAftercabProcessor& p) : proc (p)
{
    gain = p.apvts.getParameter (ParamIDs::outGain);
    autoP = p.apvts.getParameter (ParamIDs::outAuto);
}

int OutputSection::getIdealWidth()
{
    const float v = juce::GlyphArrangement::getStringWidth (face (Face::MonoRegular, kAxisSize), "-24.0 dB");
    const float b = juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kControlTextSize), "auto") + 16.0f;
    return (int) std::ceil (juce::jmax (v, b));
}

void OutputSection::setManual (float db)
{
    autoP->beginChangeGesture(); autoP->setValueNotifyingHost (0.0f); autoP->endChangeGesture();
    gain->setValueNotifyingHost (gain->convertTo0to1 (juce::jlimit (-24.0f, 12.0f, db)));
}

void OutputSection::setAuto (bool on)
{
    if (! on)   // spegnendo auto il volume non salta: si parte dal valore automatico
    {
        gain->beginChangeGesture();
        gain->setValueNotifyingHost (gain->convertTo0to1 (proc.getAutoOutDb()));
        gain->endChangeGesture();
    }
    autoP->beginChangeGesture();
    autoP->setValueNotifyingHost (on ? 1.0f : 0.0f);
    autoP->endChangeGesture();
    repaint();
}

void OutputSection::paint (juce::Graphics& g)
{
    const bool isAuto = proc.isOutAuto();
    const float db = proc.getEffectiveOutDb();
    const float cx = (float) barArea.getCentreX();

    drawTag (g, "out", cx, tagBaseline, juce::Justification::horizontallyCentred);

    // barra: riempimento dal basso fino al valore, riferimento sottile a 0 dB
    const auto bar = barArea.toFloat();
    const auto inner = bar.reduced (1.0f);
    const float norm = gain->convertTo0to1 (db);
    const float h = std::round (inner.getHeight() * juce::jlimit (0.0f, 1.0f, norm));
    const auto filled = inner.withTrimmedTop (inner.getHeight() - h);
    if (isAuto)
    {
        // auto: barra e riquadro nel colore di accento
        g.setColour (accent);
        g.fillRect (filled);
        outline (g, bar);
    }
    else
    {
        // manuale: barra grigia; solo il bordo superiore della parte piena è di accento (si trascina)
        g.setColour (valueFill);
        g.fillRect (filled);
        g.setColour (accent);
        g.fillRect (filled.getX(), filled.getY() - (h > 0.0f ? 0.0f : 1.0f), filled.getWidth(), kLine);
        g.setColour (barArea.contains (getMouseXYRelative()) || dragging ? labelHover : text);
        outline (g, bar);
    }

    drawAtBaseline (g, num (db, 1) + " dB", cx, valueBaseline, isAuto ? info : valueText, kAxisSize,
                    juce::Justification::horizontallyCentred, Face::MonoRegular);

    // tasto auto: pieno di accento quando acceso
    const auto ar = autoArea.toFloat();
    if (isAuto) { g.setColour (accent); g.fillRect (ar); }
    else { g.setColour (autoArea.contains (getMouseXYRelative()) ? labelHover : text); outline (g, ar); }
    drawLabel (g, "auto", autoArea, isAuto ? onAccent : selector, kControlTextSize, juce::Justification::centred,
               isAuto ? Face::SansSemiBold : Face::SansRegular);
}

void OutputSection::mouseDown (const juce::MouseEvent& e)
{
    if (autoArea.contains (e.getPosition())) { setAuto (! proc.isOutAuto()); return; }
    if (! barArea.expanded (4).contains (e.getPosition())) return;
    const float startDb = proc.getEffectiveOutDb();
    gain->beginChangeGesture();
    setManual (startDb);
    dragStartNorm = gain->convertTo0to1 (startDb);
    dragging = true;
    repaint();
}

void OutputSection::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm - (float) e.getDistanceFromDragStartY() / (float) barArea.getHeight());
    gain->setValueNotifyingHost (n);
    repaint();
}

void OutputSection::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    gain->endChangeGesture();
    repaint();
}

void OutputSection::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! barArea.expanded (4).contains (e.getPosition())) return;
    if (dragging) { dragging = false; gain->endChangeGesture(); }
    setAuto (true);   // default: compensazione automatica
    editor.onCommit = [this] (const juce::String& t)
    {
        gain->beginChangeGesture();
        setManual (InlineEditor::parseNumber (t));
        gain->endChangeGesture();
        repaint();
    };
    editor.setFont (face (Face::MonoMedium, kAxisSize + 1.0f));
    const auto r = juce::Rectangle<int> (0, juce::roundToInt (valueBaseline) - 13, getWidth(), 17);
    editor.start (*this, r, num (proc.getEffectiveOutDb(), 1));
}

void OutputSection::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! barArea.expanded (4).contains (e.getPosition())) return;
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (std::abs (d) <= 0.0f) return;
    gain->beginChangeGesture();
    setManual (std::round ((proc.getEffectiveOutDb() + (d > 0 ? 0.5f : -0.5f)) * 2.0f) / 2.0f);
    gain->endChangeGesture();
    repaint();
}

// ============================================================================ SmartPhaseButton
void SmartPhaseButton::paint (juce::Graphics& g)
{
    using S = BAftercabProcessor::AlignState;
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) kBoxSize, (float) kBoxSize);
    const auto st = proc.getAlignState();

    if (st == S::Listening || st == S::Analysing)
    {
        // in ascolto: contorno che si riempie dal basso col progresso
        g.setColour (text);
        outline (g, r);
        const float pr = st == S::Analysing ? 1.0f : proc.getAlignProgress();
        const auto in = r.reduced (3.0f);
        const float h = std::round (in.getHeight() * pr);
        g.setColour (accent);
        g.fillRect (in.getX(), in.getBottom() - h, in.getWidth(), h);
        return;
    }

    if (! isEnabled())
    {
        // spread spento: solo contorno e "=" grigi, non cliccabile
        g.setColour (frame);
        outline (g, r);
    }
    else
    {
        g.setColour (isMouseOver() ? accent.brighter (0.15f) : accent);
        g.fillRect (r);
        g.setColour (background);   // simbolo "=" ritagliato: lascia vedere lo sfondo
    }
    const float barW = 7.0f;
    const float x = std::round (r.getCentreX() - barW / 2.0f);
    const float cy = std::round (r.getCentreY());
    g.fillRect (x, cy - 2.5f, barW, 1.5f);
    g.fillRect (x, cy + 1.0f, barW, 1.5f);
}

void SmartPhaseButton::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition())) return;
    using S = BAftercabProcessor::AlignState;
    const auto st = proc.getAlignState();
    if (st == S::Listening || st == S::Analysing) proc.cancelAlignment();
    else proc.startAlignment();
    repaint();
}

} // namespace ui

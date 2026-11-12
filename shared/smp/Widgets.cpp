#include "Widgets.h"

namespace ui
{

// ============================================================================ InlineEditor
InlineEditor::InlineEditor()
{
    setFont (face (Face::MonoMedium, kValueSize));
    setJustification (juce::Justification::centred);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId, valueText);
    setColour (juce::TextEditor::highlightColourId, editHighlight);
    setColour (juce::TextEditor::highlightedTextColourId, background);
    setColour (juce::CaretComponent::caretColourId, valueText);
    setSelectAllWhenFocused (false);
    setIndents (2, 0);
    setBorder ({});
    onTextChange = [this] { typed = true; };
    onReturnKey  = [this] { finish (true); };
    onEscapeKey  = [this] { finish (false); };
    onFocusLost  = [this] { finish (true); };
}

void InlineEditor::start (juce::Component& owner, juce::Rectangle<int> area, const juce::String& initial)
{
    if (getParentComponent() != &owner) owner.addChildComponent (this);
    setBounds (area);
    setText (initial, false);
    typed = false;
    editing = true;
    setVisible (true);
    grabKeyboardFocus();
    selectAll();
    juce::Desktop::getInstance().addGlobalMouseListener (&outside);
}

InlineEditor::~InlineEditor() { juce::Desktop::getInstance().removeGlobalMouseListener (&outside); }

void InlineEditor::finish (bool commit)
{
    if (! editing) return;
    editing = false;
    juce::Desktop::getInstance().removeGlobalMouseListener (&outside);
    const auto t = getText();
    const bool apply = commit && typed && t.trim().isNotEmpty();
    // chiusura dopo il callback del TextEditor (non durante)
    juce::Component::SafePointer<InlineEditor> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setVisible (false); });
    if (apply && onCommit) onCommit (t);
}

float InlineEditor::parseNumber (const juce::String& typed)
{
    auto t = typed.trim().replaceCharacter (',', '.');
    float v = t.getFloatValue();
    const auto rest = t.trimCharactersAtStart ("+-0123456789. ").toLowerCase();
    if (rest.startsWith ("k")) v *= 1000.0f;
    return v;
}

// ============================================================================ PixelCheckbox
void PixelCheckbox::bindTo (juce::RangedAudioParameter& p)
{
    getState = [&p] { return p.getValue() > 0.5f; };
    setState = [&p] (bool on)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (on ? 1.0f : 0.0f);
        p.endChangeGesture();
    };
}

void PixelCheckbox::paint (juce::Graphics& g)
{
    // agganciato ai pixel interi: bordo e riempimento coincidono esattamente
    const auto r = getLocalBounds().withSizeKeepingCentre (kBoxSize, kBoxSize).toFloat();
    if (getState && getState() && meter)
    {
        g.setColour (accentOf (*this));
        outline (g, r);
        const auto in = r.reduced (2.0f);
        const float v = juce::jlimit (0.0f, 1.0f, meter());
        // anche una riduzione minima accende almeno una riga di pixel
        const float h = v > 0.0f ? juce::jmax (1.0f, std::round (in.getHeight() * v)) : 0.0f;
        if (h > 0.0f) g.fillRect (in.withHeight (h));   // dall'alto verso il basso
    }
    else if (getState && getState())
    {
        g.setColour (accentOf (*this));
        g.fillRect (r);
    }
    else
    {
        g.setColour (isMouseOver() ? labelHover : text);   // spenta: solo bordo, sfondo trasparente
        outline (g, r);
    }
}

void PixelCheckbox::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && getState && setState)
    {
        setState (! getState());
        repaint();
    }
}

// ============================================================================ SegmentedChoice
SegmentedChoice::SegmentedChoice (juce::RangedAudioParameter& p, juce::StringArray l)
    : param (p), labels (std::move (l))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int SegmentedChoice::getIdealWidth() const
{
    int w = 0;
    for (int i = 0; i < labels.size(); ++i) w += segmentNatural (i);
    return w;
}

// larghezza naturale del segmento: testo misurato in SemiBold (il carattere del segmento acceso, più largo) + margini
int SegmentedChoice::segmentNatural (int i) const
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kControlTextSize), labels[i])) + 14;
}

// bordo sinistro del segmento i: segmenti proporzionali al loro testo, così nessuna etichetta si tronca
int SegmentedChoice::edgeAt (int i) const
{
    const int n = labels.size();
    if (i <= 0) return 0;
    if (i >= n) return getWidth();
    int cum = 0, total = 0;
    for (int k = 0; k < n; ++k) { const int w = segmentNatural (k); total += w; if (k < i) cum += w; }
    return juce::roundToInt ((float) getWidth() * (float) cum / (float) juce::jmax (1, total));
}

int SegmentedChoice::segmentAt (int x) const
{
    const int n = labels.size();
    for (int i = 1; i < n; ++i) if (x < edgeAt (i)) return i - 1;
    return n - 1;
}

int SegmentedChoice::selected() const
{
    return juce::roundToInt (param.convertFrom0to1 (param.getValue())) - valueOffset;   // < 0 = nessuno
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds();
    const int n = labels.size();
    const bool enabled = isEnabled();
    const int sel = selected();
    auto edge = [&] (int i) { return r.getX() + edgeAt (i); };

    // contorno e separatori da 1 px
    g.setColour (enabled ? text : frame);
    g.drawRect (r, 1);
    for (int i = 1; i < n; ++i) g.fillRect (edge (i), r.getY(), 1, r.getHeight());

    // segmento attivo: pieno, assorbe anche i suoi bordi (come la checkbox accesa)
    if (sel >= 0 && sel < n)
    {
        g.setColour (enabled ? accentOf (*this) : frame);
        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (edge (sel), r.getY(), edge (sel + 1) + (sel + 1 < n ? 1 : 0), r.getBottom()));
    }

    for (int i = 0; i < n; ++i)
    {
        const auto seg = juce::Rectangle<int>::leftTopRightBottom (edge (i), r.getY(), edge (i + 1), r.getBottom());
        // inattivo: Plex Sans Regular chiaro; attivo: SemiBold scuro sul colore di accento
        const auto col = i == sel ? onAccent : (! enabled ? frame : (i == hover ? labelHover : selector));
        drawLabel (g, labels[i], seg, col, kControlTextSize, juce::Justification::centred,
                   i == sel ? Face::SansSemiBold : Face::SansRegular);
    }
}

void SegmentedChoice::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEnabled() && onDoubleClick && getLocalBounds().contains (e.getPosition())) onDoubleClick (segmentAt (e.x));
}

void SegmentedChoice::mouseMove (const juce::MouseEvent& e)
{
    const int h = segmentAt (e.x);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentedChoice::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

void SegmentedChoice::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition())) return;
    int i = segmentAt (e.x);
    const int before = selected();
    if (allowDeselect && i == before) i = -valueOffset;   // secondo clic sul segmento acceso: spento
    const bool changed = i != before;
    param.beginChangeGesture();
    param.setValueNotifyingHost (param.convertTo0to1 ((float) (i + valueOffset)));
    param.endChangeGesture();
    if (changed && onUserSwitch) onUserSwitch (before, i);
    if (changed && onUserChange) onUserChange (i);
    repaint();
}

// ============================================================================ ValueBox
namespace
{
    // Padding interno unico: uguale sui 4 lati (dentro il bordo da 1 px) e tra riquadro e frecce.
    float valuePad()
    {
        static const float pad = ((float) kControlH - 2.0f - makeInkText ("0", kValueSize).capHeight) * 0.5f;
        return pad;
    }
    int arrowGap() { return juce::roundToInt (valuePad()); }
}

void ValueBox::setParameter (juce::RangedAudioParameter* p)
{
    param = p;
    checkWidth();
    repaint();
}

float ValueBox::currentValue() const { return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f; }

juce::String ValueBox::currentText() const
{
    const float v = currentValue();
    return format ? format (v) : num (v, 2);
}

const InkText& ValueBox::inkFor (const juce::String& s) const
{
    if (s != cachedText || cachedText.isEmpty()) { cachedInk = makeValueInk (s); cachedText = s; }
    return cachedInk;
}

int ValueBox::getIdealWidth() const
{
    // LARGHEZZA FISSA, CASO PEGGIORE (regola della suite): il riquadro è largo quanto il testo più largo
    // che il parametro può mostrare, calcolato a priori. Si scorre tutto il range (513 punti più minimo,
    // massimo e default) con lo stesso formato del riquadro, si tengono le stringhe più lunghe e se ne misura
    // l'inchiostro; in più gli eventuali testi d'esempio. Il valore corrente non conta mai.
    const int hash = sizeSamples.joinIntoString ("|").hashCode() ^ (int) (juce::pointer_sized_int) param ^ (format ? 0x5bd1e995 : 0);
    if (hash != cachedSamplesHash || cachedSamplesWidth < 0.0f)
    {
        // un riquadro che cambia parametro (es. la banda EQ selezionata) non si restringe mai
        cachedSamplesWidth = std::max (0.0f, cachedSamplesWidth);
        juce::StringArray cands (sizeSamples);
        if (param != nullptr)
        {
            juce::StringArray all;
            auto add = [&] (float v) { all.addIfNotAlreadyThere (format ? format (v) : num (v, 2)); };
            const auto range = param->getNormalisableRange();
            add (range.start); add (range.end); add (param->convertFrom0to1 (param->getDefaultValue()));
            for (int i = 0; i <= 512; ++i) add (param->convertFrom0to1 ((float) i / 512.0f));
            int longest = 0;
            for (const auto& s : all) longest = std::max (longest, s.length());
            for (const auto& s : all) if (s.length() >= longest - 1) cands.add (s);   // cifre Plex Mono a larghezza fissa
        }
        for (const auto& s : cands) cachedSamplesWidth = std::max (cachedSamplesWidth, makeValueInk (s).width);
        cachedSamplesHash = hash;
    }
    const float w = cachedSamplesWidth;
    const int boxW = (int) std::ceil (w + 2.0f * valuePad() + 2.0f) + extraWidth;
    return 2 * (kArrowW + arrowGap()) + boxW;
}

void ValueBox::checkWidth()
{
    if (getIdealWidth() != getWidth() && onWidthChanged) onWidthChanged();
}

ValueBox::Zone ValueBox::zoneAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p)) return Zone::None;
    const int side = kArrowW + arrowGap();
    if (p.x < side) return Zone::Left;
    if (p.x >= getWidth() - side) return Zone::Right;
    return Zone::Number;
}

void ValueBox::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const float pad = valuePad();
    const int side = kArrowW + arrowGap();
    const auto box = juce::Rectangle<float> ((float) side, std::round (((float) getHeight() - (float) kControlH) * 0.5f),
                                             (float) (getWidth() - 2 * side), (float) kControlH);
    const bool en = isEnabled();

    // barra di riempimento sempre visibile: dal minimo (sinistra) al massimo (destra) del parametro
    {
        const auto inner = box.reduced (1.0f);
        const float w = std::round (inner.getWidth() * juce::jlimit (0.0f, 1.0f, param->getValue()));
        if (w > 0.0f)
        {
            g.setColour (en ? valueFill : valueFillOff);
            g.fillRect (inner.withWidth (w));
        }
    }
    g.setColour (en ? text : frame);
    outline (g, box);

    chevron (g, { 0.0f, box.getY(), (float) kArrowW, box.getHeight() }, -1, ! en ? frame : (hover == Zone::Left ? accentOf (*this) : text));
    chevron (g, { (float) (getWidth() - kArrowW), box.getY(), (float) kArrowW, box.getHeight() }, +1,
             ! en ? frame : (hover == Zone::Right ? accentOf (*this) : text));

    // valore centrato sull'inchiostro: stessa distanza da sinistra, destra, alto (cifre) e basso (baseline)
    const auto& ink = inkFor (currentText());
    const float x = box.getX() + 1.0f + (box.getWidth() - 2.0f - ink.width) * 0.5f - ink.left;
    const float baseline = box.getY() + 1.0f + pad + ink.capHeight;
    const auto at = juce::AffineTransform::translation (x, baseline);
    // numero: Plex Mono Medium bianco; unità: Plex Mono Regular grigia, 1 px più piccola
    g.setColour (! en ? frame : (dragging ? accentOf (*this) : (hover == Zone::Number ? juce::Colours::white : valueText)));
    g.fillPath (ink.path, at);
    g.setColour (! en ? frame : unitText);
    g.fillPath (ink.unitPath, at);
}

void ValueBox::setValue (float v)
{
    const auto range = param->getNormalisableRange();
    param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (range.start, range.end, v)));
    checkWidth();
    repaint();
}

void ValueBox::doStep (int dir)
{
    if (param == nullptr || ! step) return;
    param->beginChangeGesture();
    setValue (step (currentValue(), dir));
    param->endChangeGesture();
}

void ValueBox::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover)
    {
        hover = z;
        setMouseCursor (z == Zone::Number ? juce::MouseCursor::LeftRightResizeCursor
                                          : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ValueBox::mouseExit (const juce::MouseEvent&) { hover = Zone::None; repaint(); }

juce::Rectangle<int> ValueBox::boxArea() const
{
    const int side = kArrowW + arrowGap();
    return { side, (getHeight() - kControlH) / 2, getWidth() - 2 * side, kControlH };
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (param == nullptr || ! isEnabled() || zoneAt (e.getPosition()) != Zone::Number) return;
    if (dragging) { dragging = false; param->endChangeGesture(); }
    // 1) torna al default (quello del preset, se c'è)
    param->beginChangeGesture();
    if (defaultValue) setValue (defaultValue());
    else param->setValueNotifyingHost (param->getDefaultValue());
    param->endChangeGesture();
    checkWidth();
    repaint();

    // 2) modifica a tastiera: tutto il testo (numero e unità) selezionato
    const auto txt = currentText();
    editor.onCommit = [this] (const juce::String& t)
    {
        param->beginChangeGesture();
        setValue (parse ? parse (t) : InlineEditor::parseNumber (t));
        param->endChangeGesture();
    };
    editor.start (*this, boxArea().reduced (1), txt.trim());
}

void ValueBox::mouseDown (const juce::MouseEvent& e)
{
    if (param == nullptr || ! isEnabled()) return;
    const auto z = zoneAt (e.getPosition());
    if (z == Zone::Left)  { doStep (-1); return; }
    if (z == Zone::Right) { doStep (+1); return; }
    if (z != Zone::Number || ! drag) return;
    dragging = true;
    dragStartValue = currentValue();
    dragStartNorm = param->getValue();
    param->beginChangeGesture();
    repaint();
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    setValue (drag (dragStartValue, dragStartNorm, (float) e.getDistanceFromDragStartX()));
}

void ValueBox::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    param->endChangeGesture();
    repaint();
}

void ValueBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (isEnabled() && std::abs (d) > 0.0f) doStep (d > 0 ? +1 : -1);
}

// ============================================================================ BigValue
float BigValue::widestInk() const
{
    // caso peggiore su tutto il range, come ValueBox: il riquadro e lo spazio del numero non cambiano mai
    if (cachedFor == param && cachedWidest >= 0.0f) return cachedWidest;
    float w = 0.0f;
    juce::StringArray cands (sizeSamples);
    if (param != nullptr)
    {
        const auto r = param->getNormalisableRange();
        auto add = [&] (float v) { cands.addIfNotAlreadyThere (format ? format (v) : num (v, 2)); };
        add (r.start); add (r.end);
        for (int i = 0; i <= 256; ++i) add (param->convertFrom0to1 ((float) i / 256.0f));
    }
    for (const auto& s : cands) w = juce::jmax (w, makeValueInk (s, kNumberSize, kUnitSize).width);
    cachedWidest = w; cachedFor = param;
    return w;
}

int BigValue::getIdealWidth() const
{
    const float lw = juce::GlyphArrangement::getStringWidth (face (Face::SansMedium, kLabelSize), label);
    return (int) std::ceil (juce::jmax (lw, widestInk() + 2.0f * 20.0f));   // 20 px d'aria ai lati del numero più largo
}

float BigValue::numberBaseline() const
{
    const auto b = boxArea();
    return std::round (b.getCentreY() + capHeight (kNumberSize, Face::MonoMedium) * 0.5f);
}

void BigValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const bool en = isEnabled();
    const auto col = accentOf (*this);
    const float cx = (float) getWidth() * 0.5f;
    drawAtBaseline (g, label, cx, std::round (capHeight (kLabelSize, Face::SansMedium)) + 2.0f, en ? labelStatic : frame, kLabelSize,
                    juce::Justification::horizontallyCentred, Face::SansMedium);
    // riquadro fisso: riempimento grigio dal minimo al valore, bordo da 1 px, il bordo inferiore colorato fino al valore
    const auto box = boxArea();
    const float v01 = juce::jlimit (0.0f, 1.0f, param->getValue());
    const auto inner = box.reduced (1.0f);
    g.setColour (en ? valueFill : valueFillOff); g.fillRect (inner.withWidth (std::round (inner.getWidth() * v01)));
    g.setColour (! en ? frame : (isMouseOver() || dragging ? labelHover : ui::text));
    outline (g, box);
    // il bordo inferiore del riquadro è la linea del valore: nel colore del plugin fin dove arriva il parametro
    const auto edge = getLocalBounds().toFloat().withTop ((float) getHeight() - 2.0f);
    g.setColour (en ? col : frame); g.fillRect (edge.withWidth (std::round (edge.getWidth() * v01)));
    // numero allineato a destra in uno spazio fisso centrato nel riquadro: non si sposta quando cambiano le cifre
    const auto ink = makeValueInk (format ? format (param->convertFrom0to1 (param->getValue())) : num (param->getValue(), 2), kNumberSize, kUnitSize);
    const float slotRight = std::round (cx + widestInk() * 0.5f);
    const auto at = juce::AffineTransform::translation (slotRight - ink.width - ink.left, numberBaseline());
    g.setColour (! en ? frame : (dragging ? col : (isMouseOver() ? juce::Colours::white : valueText)));
    g.fillPath (ink.path, at);
    g.setColour (! en ? frame : unitText);
    g.fillPath (ink.unitPath, at);
}

void BigValue::setValue (float v)
{
    const auto r = param->getNormalisableRange();
    param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (r.start, r.end, v)));
    repaint();
}

void BigValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr || ! isEnabled()) return;
    dragging = true; startNorm = param->getValue();
    param->beginChangeGesture();
    repaint();
}

void BigValue::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
    param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, startNorm + d * normPerPixel));
    repaint();
}

void BigValue::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    param->endChangeGesture();
    repaint();
}

void BigValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param == nullptr || ! isEnabled()) return;
    if (dragging) { dragging = false; param->endChangeGesture(); }
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->getDefaultValue());
    param->endChangeGesture();
    repaint();
    const auto txt = format ? format (param->convertFrom0to1 (param->getValue())) : juce::String();
    editor.onCommit = [this] (const juce::String& t)
    {
        param->beginChangeGesture();
        setValue (InlineEditor::parseNumber (t));
        param->endChangeGesture();
    };
    editor.start (*this, boxArea().reduced (2.0f).toNearestInt(), txt.trim());
}

void BigValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (param == nullptr || ! isEnabled() || std::abs (d) <= 0.0f || ! step) return;
    param->beginChangeGesture();
    setValue (step (param->convertFrom0to1 (param->getValue()), d > 0 ? +1 : -1));
    param->endChangeGesture();
}

// ============================================================================ TextToggle
int TextToggle::getIdealWidth() const
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kControlTextSize), text)) + 24;
}

void TextToggle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = isOn(), en = isEnabled();
    if (on && en) { g.setColour (accentOf (*this)); g.fillRect (r); }
    else { g.setColour (! en ? frame : (isMouseOver() ? labelHover : ui::text)); outline (g, r); }
    drawLabel (g, text, getLocalBounds(), on && en ? onAccent : (! en ? frame : (isMouseOver() ? labelHover : selector)), kControlTextSize,
               juce::Justification::centred, on ? Face::SansSemiBold : Face::SansRegular);
}

void TextToggle::mouseUp (const juce::MouseEvent& e)
{
    if (param == nullptr || ! isEnabled() || ! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition())) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost (isOn() ? 0.0f : 1.0f);
    param->endChangeGesture();
    repaint();
}

// ============================================================================ Section
Section::Section (juce::String t, juce::RangedAudioParameter* onOff) : title (std::move (t)), onParam (onOff)
{
    if (onParam != nullptr) { power.bindTo (*onParam); addAndMakeVisible (power); }
}

void Section::add (const juce::String& rowLabel, juce::Component& c, int width)
{
    rows.push_back ({ rowLabel, &c, width });
    addAndMakeVisible (c);
}

int Section::labelWidth() const
{
    int w = 0;
    for (auto& r : rows) w = juce::jmax (w, (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansMedium, kLabelSize), r.label)));
    return w;
}

int Section::getIdealWidth() const
{
    int ctl = 0;
    for (auto& r : rows) ctl = juce::jmax (ctl, r.width);
    const int lw = labelWidth();
    const int titleW = (onParam != nullptr ? kBoxSize + kGapLabel : 0)
                     + (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kControlTextSize), title));
    return juce::jmax (titleW, (lw > 0 ? lw + kGapLabel : 0) + ctl);
}

int Section::getIdealHeight() const { return kTitleH + (int) rows.size() * (kControlH + kRowGap); }

void Section::resized()
{
    power.setBounds (juce::Rectangle<int> (0, 0, kBoxSize, kBoxSize).withCentre ({ kBoxSize / 2, kTitleH / 2 - 2 }).expanded (kHitPad));
    const int lw = labelWidth();
    int y = kTitleH + 4;
    for (auto& r : rows)
    {
        r.comp->setBounds (lw > 0 ? lw + kGapLabel : 0, y, r.width, kControlH);
        y += kControlH + kRowGap;
    }
}

void Section::refresh()
{
    const bool on = isOn();
    power.repaint();
    if (on == lastOn) return;
    lastOn = on;
    for (auto& r : rows) r.comp->setEnabled (on);
    repaint();
}

void Section::paint (juce::Graphics& g)
{
    const bool on = isOn();
    const float tx = onParam != nullptr ? (float) (kBoxSize + kGapLabel) : 0.0f;
    // titolo del gruppo: più chiaro e più marcato delle etichette, grigio scuro quando il gruppo è spento
    drawAtBaseline (g, title, tx, (float) kTitleH / 2.0f - 2.0f + capHeight (kControlTextSize, Face::SansSemiBold) * 0.5f,
                    on ? titleName : labelStatic, kControlTextSize, juce::Justification::left, Face::SansSemiBold);
    int y = kTitleH + 4;
    for (auto& r : rows)
    {
        if (r.label.isNotEmpty())
            drawLabel (g, r.label, { 0, y, labelWidth(), kControlH }, on ? labelStatic : frame, kLabelSize);
        y += kControlH + kRowGap;
    }
}

// ============================================================================ AboutOverlay
AboutOverlay::AboutOverlay (juce::Component& editor, const juce::String& n)
    : ed (editor), name (n), text (aboutText (n))
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    ed.addChildComponent (this);
    if (! kInRig) ed.addAndMakeVisible (hotspot);   // in b-rig il logo del plugin non c'è
    ed.addChildComponent (toast);
    ed.addComponentListener (this);
    place();
    // dentro b-rig i moduli non controllano: si aggiorna l'app intera
    if (! kInRig)
        watcher = std::make_unique<updates::Watcher> (ed, product(), [this] (const updates::Available& a) { setUpdate (a); });
}

void AboutOverlay::setUpdate (const updates::Available& a)
{
    update = a;
    toast.setVisible (a.version.isNotEmpty() && ! isVisible());
    place();
    hotspot.repaint();
    repaint();
}

void AboutOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! downloadArea.isEmpty() && downloadArea.contains (e.getPosition()) && update.url.startsWith ("https://"))
        juce::URL (update.url).launchInDefaultBrowser();
    setVisible (false);
}

// quadratino di aggiornamento: in alto a destra del logotipo, all'altezza delle maiuscole
void AboutOverlay::Hotspot::paint (juce::Graphics& g)
{
    if (owner.update.version.isEmpty()) return;
    const float w = logoWidth (owner.logoText.isNotEmpty() ? owner.logoText : owner.name);
    const float capTop = baselineFor (18.0f, kLogoSize, Face::SansBold) - capHeight (kLogoSize, Face::SansBold);
    g.setColour (owner.titleColour ? owner.titleColour() : accentOf (owner.ed));
    g.fillRect (std::round (w + 5.0f), std::round (capTop), 6.0f, 6.0f);
}

juce::String AboutOverlay::Toast::line() const { return owner.product() + " " + updates::versionLabel (owner.update.version); }

void AboutOverlay::Toast::paint (juce::Graphics& g)
{
    const auto col = owner.titleColour ? owner.titleColour() : accentOf (owner.ed);
    const auto r = getLocalBounds().toFloat();
    g.fillAll (background);
    g.setColour (col); outline (g, r);
    const auto body = getLocalBounds().withTrimmedRight (closeArea().getWidth()).reduced (14, 0);
    const float mid = (float) getHeight() * 0.5f;
    drawAtBaseline (g, "update available", (float) body.getX(), mid - 4.0f, info, kAxisSize, juce::Justification::left, Face::SansRegular);
    drawAtBaseline (g, line(), (float) body.getX(), mid + 4.0f + capHeight (kInfoSize, Face::SansSemiBold), col, kInfoSize,
                    juce::Justification::left, Face::SansSemiBold);
    const auto c = closeArea().toFloat().getCentre();
    const bool hot = closeArea().contains (getMouseXYRelative()) && isMouseOver();
    g.setColour (hot ? labelHover : ui::text);
    g.drawLine (c.x - 4.5f, c.y - 4.5f, c.x + 4.5f, c.y + 4.5f, 1.0f);
    g.drawLine (c.x - 4.5f, c.y + 4.5f, c.x + 4.5f, c.y - 4.5f, 1.0f);
}

void AboutOverlay::Toast::mouseDown (const juce::MouseEvent& e)
{
    setVisible (false);
    if (! closeArea().contains (e.getPosition())) owner.show();   // clic sull'avviso: il pannello about con il tasto download
}

void AboutOverlay::clickToastForTest (bool onClose)
{
    const auto p = onClose ? toast.closeArea().getCentre() : juce::Point<int> (10, 10);
    toast.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), {}, juce::MouseInputSource::defaultPressure,
                                       0, 0, 0, 0, &toast, &toast, juce::Time::getCurrentTime(), p.toFloat(), juce::Time::getCurrentTime(), 1, false));
}

AboutOverlay::~AboutOverlay() { ed.removeComponentListener (this); }

void AboutOverlay::componentMovedOrResized (juce::Component&, bool, bool) { place(); }

void AboutOverlay::place()
{
    setBounds (ed.getLocalBounds());
    const float lw = logoWidth (logoText.isNotEmpty() ? logoText : name);
    hotspot.setBounds (logoAt.x, logoAt.y, (int) std::ceil (lw) + (update.version.isNotEmpty() ? 14 : 4), 36);
    hotspot.toFront (false);
    {
        const int tw = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, kInfoSize), toast.line()))) + 28 + 34;
        const auto eb = ed.getLocalBounds().reduced (16);
        toast.setBounds (eb.getRight() - tw, eb.getBottom() - 52, tw, 52);
        toast.toFront (false);
    }
    if (isVisible()) toFront (true);
}

void AboutOverlay::show()
{
    toast.setVisible (false);   // l'avviso ha fatto il suo lavoro: resta il quadratino accanto al logo
    place();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf50d0d0f));
    const auto col = titleColour ? titleColour() : accentOf (ed);
    const float w = juce::jmin (500.0f, (float) getWidth() - 48.0f);
    const float x = ((float) getWidth() - w) * 0.5f;
    // altezza del blocco di testo: misurata, così il pannello resta centrato in verticale
    juce::AttributedString body;
    body.setLineSpacing (5.0f);
    body.append (text, face (Face::SansRegular, 15.0f), labelHover);
    juce::TextLayout tl; tl.createLayout (body, w);
    const bool hasUpdate = update.version.isNotEmpty();
    const float blockH = 48.0f + tl.getHeight() + 34.0f + 70.0f + (hasUpdate ? 52.0f : 0.0f);
    float y = juce::jmax (24.0f, ((float) getHeight() - blockH) * 0.5f);
    drawTitleAlways (g, name, juce::Rectangle<float> (x - 1.0f, y, w, 36.0f).toNearestInt(), kLogoSize, col);
    y += 48.0f;
    tl.draw (g, { x, y, w, tl.getHeight() });
    y += tl.getHeight() + 22.0f;
    g.setColour (frame); g.fillRect (x, std::round (y), 40.0f, kLine);
    y += 30.0f;
    // firma
    float sx = x;
    auto put = [&] (const juce::String& s, Face f, juce::Colour c, float size)
    {
        const auto fnt = face (f, size);
        g.setColour (c); g.setFont (fnt);
        g.drawSingleLineText (s, juce::roundToInt (sx), juce::roundToInt (y));
        sx += juce::GlyphArrangement::getStringWidth (fnt, s);
    };
    put ("b", Face::SansBold, col, 18.0f); put ("-", Face::SansLight, titleDash, 18.0f); put ("suite", Face::SansMedium, titleName, 18.0f);
    y += 24.0f;
    drawAtBaseline (g, juce::String (juce::CharPointer_UTF8 ("free and open source  \xc2\xb7  built with Claude Code")), x, y, info, kInfoSize, juce::Justification::left, Face::SansRegular);
    if (const auto v = updates::versionLabel (updates::currentVersion()); v.isNotEmpty())
        drawAtBaseline (g, "version " + v, x + w, y, info, kInfoSize, juce::Justification::right, Face::MonoRegular);
    downloadArea = {};
    if (hasUpdate)
    {
        // nuova versione: testo nel colore del plugin e un tasto pieno che apre la pagina della release
        y += 34.0f;
        const auto msg = product() + " " + updates::versionLabel (update.version) + " is available";
        const auto bf = face (Face::SansSemiBold, kControlTextSize);
        const int bw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (bf, "download")) + 24;
        downloadArea = { juce::roundToInt (x + w) - bw, juce::roundToInt (y - capHeight (kControlTextSize, Face::SansSemiBold) * 0.5f) - kControlH / 2, bw, kControlH };
        drawAtBaseline (g, msg, x, baselineFor ((float) downloadArea.getCentreY(), kInfoSize, Face::SansMedium), col, kInfoSize, juce::Justification::left, Face::SansMedium);
        g.setColour (col); g.fillRect (downloadArea);
        drawLabel (g, "download", downloadArea, onAccent, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
    drawAtBaseline (g, "click anywhere to close", (float) getWidth() * 0.5f, (float) getHeight() - 16.0f, info, kAxisSmall,
                    juce::Justification::horizontallyCentred, Face::SansRegular);
}

} // namespace ui

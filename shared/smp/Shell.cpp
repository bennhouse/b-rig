#include "Shell.h"

namespace ui
{

namespace
{
    constexpr int kMargin = 24, kBoxTop = 66;
    constexpr float kTitleSize = 30.0f;
    int textWidth (const juce::String& s) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (kLabelSize), s)); }
}

ShellEditor::ShellEditor (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, const juce::String& n, int w, int h)
    : AudioProcessorEditor (&p), state (s), name (n)
{
    setSize (w, h);
    about = std::make_unique<AboutOverlay> (*this, n);
    infoText = p.getTotalNumInputChannels() == 1 ? "mono" : "stereo";
    startTimerHz (30);
}

ShellEditor::~ShellEditor() { stopTimer(); }

juce::Rectangle<int> ShellEditor::box() const { return { kMargin, kBoxTop, getWidth() - 2 * kMargin, getHeight() - kBoxTop - kMargin }; }

ValueBox& ShellEditor::addValue (const juce::String& caption, const char* id, std::function<juce::String (float)> format,
                                 std::function<float (float, int)> step, juce::StringArray sizes, float perPx)
{
    auto vb = std::make_unique<ValueBox>();
    auto* prm = state.getParameter (id);
    vb->setParameter (prm);
    vb->format = std::move (format);
    vb->step = std::move (step);
    vb->drag = [prm, perPx] (float, float startNorm, float dx) { return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * perPx)); };
    vb->sizeSamples = std::move (sizes);
    auto& ref = *vb;
    addAndMakeVisible (ref);
    (column != 0 ? colItems : items).push_back ({ caption, &ref, -1, false, false, column });
    owned.push_back (std::move (vb));
    return ref;
}

PixelCheckbox& ShellEditor::addCheck (const juce::String& caption, const char* id)
{
    auto cb = std::make_unique<PixelCheckbox>();
    cb->bindTo (*state.getParameter (id));
    auto& ref = *cb;
    addAndMakeVisible (ref);
    (column != 0 ? colItems : items).push_back ({ caption, &ref, kBoxSize, true, false, column });
    owned.push_back (std::move (cb));
    return ref;
}

SegmentedChoice& ShellEditor::addChoice (const juce::String& caption, const char* id, juce::StringArray l)
{
    auto sc = std::make_unique<SegmentedChoice> (*state.getParameter (id), l);
    auto& ref = *sc;
    addAndMakeVisible (ref);
    (column != 0 ? colItems : items).push_back ({ caption, &ref, ref.getIdealWidth(), false, false, column });
    owned.push_back (std::move (sc));
    return ref;
}

void ShellEditor::setHeaderChoice (const juce::String& caption, const char* id, juce::StringArray l)
{
    headerChoice = std::make_unique<SegmentedChoice> (*state.getParameter (id), l);
    headerLabel = caption;
    addAndMakeVisible (*headerChoice);
}

SegmentedChoice& ShellEditor::addHeaderChoice (const juce::String& caption, const char* id, juce::StringArray l)
{
    auto sc = std::make_unique<SegmentedChoice> (*state.getParameter (id), l);
    auto& ref = *sc;
    addAndMakeVisible (ref);
    headerItems.push_back ({ caption, &ref, ref.getIdealWidth(), false });
    owned.push_back (std::move (sc));
    return ref;
}

PixelCheckbox& ShellEditor::addHeaderCheck (const juce::String& caption, const char* id)
{
    auto cb = std::make_unique<PixelCheckbox>();
    cb->bindTo (*state.getParameter (id));
    auto& ref = *cb;
    addAndMakeVisible (ref);
    headerItems.push_back ({ caption, &ref, kBoxSize, true });
    owned.push_back (std::move (cb));
    return ref;
}

void ShellEditor::resized()
{
    labels.clear();
    const auto b = box();
    const int left = b.getX() + 1 + kFramePad, right = b.getRight() - 1 - kFramePad;
    auto row = juce::Rectangle<int> (left, b.getY() + 1 + kFramePad, right - left, kControlH);
    juce::Rectangle<int> firstRowRest;
    auto place = [&] (Item& it, bool fromRight)
    {
        const int w = it.width > 0 ? it.width : static_cast<ValueBox*> (it.comp)->getIdealWidth();
        const int lw = textWidth (it.label);
        juce::Rectangle<int> lab, r;
        if (fromRight) { r = row.removeFromRight (w); row.removeFromRight (kGapLabel); lab = row.removeFromRight (lw); row.removeFromRight (kGapGroup); }
        else           { lab = row.removeFromLeft (lw); row.removeFromLeft (kGapLabel); r = row.removeFromLeft (w); row.removeFromLeft (kGapGroup); }
        labels.push_back ({ it.label, lab });
        it.comp->setBounds (it.square ? r.withSizeKeepingCentre (kBoxSize, kBoxSize).expanded (kHitPad) : r);
    };
    for (size_t i = 0; i <= items.size(); ++i)
    {
        if (i < items.size() && items[i].comp != nullptr) { if (! items[i].right) place (items[i], false); continue; }
        // fine riga: i controlli allineati a destra, dall'ultimo al primo
        size_t s0 = i; while (s0 > 0 && items[s0 - 1].comp != nullptr) --s0;
        for (size_t k = i; k-- > s0;) if (items[k].right) place (items[k], true);
        if (i == items.size()) break;
        if (firstRowRest.isEmpty()) firstRowRest = row;
        row = juce::Rectangle<int> (left, row.getBottom() + kGapInner, right - left, kControlH);
    }
    infoArea = firstRowRest.isEmpty() ? row : firstRowRest;
    auto dispArea = juce::Rectangle<int>::leftTopRightBottom (left, row.getBottom() + kGapInner, right + 1, b.getBottom() - 1 - kFramePad);
    // colonne laterali: etichette allineate a sinistra, controlli allineati tra loro, una riga ogni controllo
    for (int side : { 1, 2 })
    {
        int labW = 0, ctlW = 0, count = 0;
        for (auto& it : colItems)
            if (it.col == side)
            {
                labW = juce::jmax (labW, textWidth (it.label));
                ctlW = juce::jmax (ctlW, it.width > 0 ? it.width : static_cast<ValueBox*> (it.comp)->getIdealWidth());
                ++count;
            }
        if (count == 0) continue;
        const int colW = labW + kGapLabel + ctlW;
        auto col = side == 1 ? dispArea.removeFromLeft (colW) : dispArea.removeFromRight (colW);
        if (side == 1) dispArea.removeFromLeft (kGapGroup); else dispArea.removeFromRight (kGapGroup);
        int y = col.getY();
        for (auto& it : colItems)
        {
            if (it.col != side) continue;
            const int w = it.width > 0 ? it.width : static_cast<ValueBox*> (it.comp)->getIdealWidth();
            labels.push_back ({ it.label, { col.getX(), y, labW, kControlH } });
            const auto r = juce::Rectangle<int> (col.getX() + labW + kGapLabel, y, w, kControlH);
            it.comp->setBounds (it.square ? r.withWidth (kBoxSize).withSizeKeepingCentre (kBoxSize, kBoxSize).expanded (kHitPad) : r);
            y += kControlH + kGapInner + 4;
        }
    }
    if (display != nullptr) display->setBounds (dispArea);
    if (headerChoice != nullptr || ! headerItems.empty())
    {
        auto head = juce::Rectangle<int> (left, headerRowY(), right - left, kControlH);
        std::vector<Item> all (headerItems);
        if (headerChoice != nullptr) all.push_back ({ headerLabel, headerChoice.get(), headerChoice->getIdealWidth(), false });
        std::vector<juce::Component*> comps;
        const size_t first = labels.size();
        for (size_t k = all.size(); k-- > 0;)
        {
            auto r = head.removeFromRight (all[k].width);
            all[k].comp->setBounds (all[k].square ? r.withSizeKeepingCentre (kBoxSize, kBoxSize).expanded (kHitPad) : r);
            head.removeFromRight (kGapLabel);
            labels.push_back ({ all[k].label, head.removeFromRight (textWidth (all[k].label)) });
            head.removeFromRight (kGapGroup);
            comps.push_back (all[k].comp);
        }
        std::vector<juce::Rectangle<int>*> areas;
        for (size_t k = first; k < labels.size(); ++k) areas.push_back (&labels[k].area);
        centreHeaderInRig (getWidth(), comps, areas);
    }
}

void ShellEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    drawTitle (g, titleText.isNotEmpty() ? titleText : name, { kMargin - 1, 16, 300, 36 }, kTitleSize);
    g.setColour (frameColour);
    outline (g, box().toFloat());
    for (const auto& l : labels) drawLabel (g, l.text, l.area, labelStatic, kLabelSize);
    drawLabel (g, infoText, infoArea, info, kInfoSize, juce::Justification::centredRight, Face::SansRegular);
}

void ShellEditor::timerCallback()
{
    tick();
    juce::int64 sig = 17;
    for (auto* p : getAudioProcessor()->getParameters()) sig = sig * 31 + (juce::int64) (p->getValue() * 1.0e6f);
    if (sig == lastSignature) return;
    lastSignature = sig;
    // solo ciò che può cambiare: i controlli e la cornice; il grafico si ridisegna da sé se displayRefreshesItself
    if (display == nullptr || ! displayRefreshesItself) { repaint(); return; }
    for (auto* c : getChildren()) if (c != display) c->repaint();
    const auto d = display->getBounds();
    repaint (0, 0, getWidth(), d.getY());
    repaint (0, d.getBottom(), getWidth(), getHeight() - d.getBottom());
}

} // namespace ui

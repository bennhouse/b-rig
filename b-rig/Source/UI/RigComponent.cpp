#include "RigComponent.h"

namespace rig
{
using namespace ui;

namespace
{
    const juce::Colour kGreen  { 0xff10b981 };   // tuner intonato
    const juce::Colour kAmber  { 0xfff59e0b };   // tuner calante/crescente, badge "safe muted"
    const juce::Colour kClip   { 0xffef4444 };
    const juce::Colour kRowHover { 0xff1a1a1a };
    const juce::Colour kMenuHi { 0xff2a2a2a };
    const juce::Colour kOff    { 0xff333333 };
    const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr int kPad = 16, kGap = 8;

    juce::AudioProcessorValueTreeState::ParameterLayout controlsLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "bpm", NormalisableRange<float> (RigEngine::kMinBpm, RigEngine::kMaxBpm, 1.0f), 120.0f));
        StringArray sigs; for (auto s : RigEngine::kSigs) sigs.add (String (s.first) + "/" + String (s.second));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "sig", 1 }, "time", sigs, 2));
        return l;
    }

    float toMeter (float peak) { return peak <= 1.0e-6f ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (peak) / 60.0f); }
    int textW (const juce::String& s, float size, Face f = Face::SansMedium) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (f, size), s)); }

    void chevronDown (juce::Graphics& g, juce::Point<float> c, juce::Colour col)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        p.lineTo (c.x, c.y + 2.0f);
        p.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    }

    void cross (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col)
    {
        g.setColour (col);
        g.drawLine (c.x - r, c.y - r, c.x + r, c.y + r, 1.2f);
        g.drawLine (c.x - r, c.y + r, c.x + r, c.y - r, 1.2f);
    }
}

RigControls::RigControls() : apvts (*this, nullptr, "BRIG", controlsLayout()) {}

// ============================================================================ tema delle parti JUCE

RigLookAndFeel::RigLookAndFeel()
{
    setColourScheme (getDarkColourScheme());
    setDefaultSansSerifTypeface (typeface (Face::SansRegular));
    using C = std::pair<int, juce::Colour>;
    for (auto [id, c] : { C { juce::PopupMenu::backgroundColourId, background },
                          C { juce::PopupMenu::textColourId, selector },
                          C { juce::PopupMenu::highlightedBackgroundColourId, kMenuHi },
                          C { juce::PopupMenu::highlightedTextColourId, valueText },
                          C { juce::ResizableWindow::backgroundColourId, background },
                          C { juce::DocumentWindow::backgroundColourId, background },
                          C { juce::ComboBox::backgroundColourId, background },
                          C { juce::ComboBox::outlineColourId, text },
                          C { juce::ComboBox::textColourId, selector },
                          C { juce::ComboBox::arrowColourId, selector },
                          C { juce::ListBox::backgroundColourId, background },
                          C { juce::ListBox::outlineColourId, frame },
                          C { juce::TextButton::buttonColourId, background },
                          C { juce::TextButton::buttonOnColourId, kMenuHi },
                          C { juce::TextButton::textColourOffId, selector },
                          C { juce::TextButton::textColourOnId, valueText },
                          C { juce::Label::textColourId, selector },
                          C { juce::TextEditor::backgroundColourId, background },
                          C { juce::TextEditor::outlineColourId, text },
                          C { juce::TextEditor::focusedOutlineColourId, labelHover },
                          C { juce::TextEditor::textColourId, valueText },
                          C { juce::TextEditor::highlightColourId, editHighlight },
                          C { juce::TextEditor::highlightedTextColourId, onAccent },
                          C { juce::CaretComponent::caretColourId, valueText },
                          C { juce::ScrollBar::thumbColourId, frame } })
        setColour (id, c);
}

juce::Font RigLookAndFeel::getPopupMenuFont() { return face (Face::SansMedium, kControlTextSize); }
juce::Font RigLookAndFeel::getComboBoxFont (juce::ComboBox&) { return face (Face::SansMedium, kControlTextSize); }
juce::Font RigLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return face (Face::SansSemiBold, kControlTextSize); }
juce::Font RigLookAndFeel::getLabelFont (juce::Label&) { return face (Face::SansMedium, kControlTextSize); }

void RigLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (background);
    g.setColour (frame);
    g.drawRect (0, 0, w, h, 1);
}

void RigLookAndFeel::getIdealPopupMenuItemSize (const juce::String& s, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 40; h = 9; return; }
    w = textW (s, kControlTextSize) + 30 + 24;
    h = 26;
}

void RigLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const juce::String& s, const juce::String&,
                                        const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator) { g.setColour (frame); g.fillRect (area.getX() + 10, area.getCentreY(), area.getWidth() - 20, 1); return; }
    if (isHighlighted && isActive) { g.setColour (kMenuHi); g.fillRect (area); }
    if (isTicked) { g.setColour (accent); g.fillRect (area.getX() + 12, area.getCentreY() - 3, 6, 6); }
    auto col = ! isActive ? frame : isHighlighted ? valueText : selector;
    if (textColour != nullptr && isActive) col = *textColour;
    ui::drawLabel (g, s, area.withTrimmedLeft (30).withTrimmedRight (12), col, kControlTextSize, juce::Justification::centredLeft, Face::SansMedium);
    if (hasSubMenu) chevron (g, area.withLeft (area.getRight() - 20).toFloat(), +1, col);
}

void RigLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& s)
{
    drawTag (g, s, (float) area.getX() + 12.0f, baselineFor ((float) area.getCentreY(), kTagSize, Face::SansSemiBold));
}

void RigLookAndFeel::drawPopupMenuUpDownArrow (juce::Graphics& g, int w, int h, bool up)
{
    g.fillAll (background);
    juce::Path p;
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    if (up) p.addTriangle (cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 2);
    else    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 2);
    g.setColour (selector);
    g.fillPath (p);
}

void RigLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.fillAll (background);
    g.setColour (box.isMouseOver (true) ? labelHover : text);
    g.drawRect (0, 0, w, h, 1);
    chevronDown (g, { (float) w - 12.0f, (float) h * 0.5f }, selector);
}

void RigLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds();
    if (down || b.getToggleState()) { g.setColour (kMenuHi); g.fillRect (r); }
    g.setColour (over ? labelHover : text);
    g.drawRect (r, 1);
}

void RigLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int start, int size, bool over, bool)
{
    g.setColour (over ? text : frame);
    if (vertical) g.fillRect (x + w - 4, start, 3, size);
    else g.fillRect (start, y + h - 4, size, 3);
}

// ============================================================================ controlli

int FlatButton::idealWidth() const { return textW (caption, kControlTextSize, Face::SansSemiBold) + 18; }

void FlatButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = isOn && isOn();
    const auto col = onColour ? onColour() : accentOf (*this);
    if (! isEnabled()) { g.setColour (frame); outline (g, r); drawLabel (g, caption, getLocalBounds(), frame, kControlTextSize, juce::Justification::centred, Face::SansSemiBold); return; }
    const bool h = isMouseOver();
    if (on) { g.setColour (col); g.fillRect (r); }
    else { g.setColour (h ? labelHover : dim ? kOff : text); outline (g, r); }
    drawLabel (g, caption, getLocalBounds(), on ? onAccent : (h ? labelHover : dim ? frame : selector), kControlTextSize,
               juce::Justification::centred, Face::SansSemiBold);
}

void ArrowButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto ink = ! isEnabled() ? kOff : (isMouseOver() ? labelHover : selector);
    // arco di mezzo cerchio aperto in basso, con la punta a sinistra (undo) o a destra (redo)
    const float cx = r.getCentreX(), cy = r.getCentreY() + 2.0f, rad = 5.5f;
    juce::Path arc;
    arc.addCentredArc (cx, cy, rad, rad, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.3f));
    const float tx = redo ? cx + rad : cx - rad;
    juce::Path tip;
    tip.addTriangle (tx - 3.0f, cy - 1.0f, tx + 3.0f, cy - 1.0f, tx, cy + 3.5f);
    g.fillPath (tip);
}

void MetroButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = isOn && isOn(), h = isMouseOver();
    if (on) { g.setColour (accentOf (*this)); g.fillRect (r); }
    else { g.setColour (h ? labelHover : text); outline (g, r); }
    const auto ink = on ? onAccent : (h ? labelHover : selector);
    // corpo a trapezio, asta che oscilla col battito, peso sull'asta
    const float cx = r.getCentreX(), base = r.getBottom() - 4.0f, top = r.getY() + 4.0f;
    juce::Path body;
    body.startNewSubPath (cx - 5.0f, base);
    body.lineTo (cx - 2.0f, top);
    body.lineTo (cx + 2.0f, top);
    body.lineTo (cx + 5.0f, base);
    body.closeSubPath();
    g.setColour (ink);
    g.strokePath (body, juce::PathStrokeType (1.1f, juce::PathStrokeType::mitered));
    const float sw = swing ? swing() : 0.4f;
    const juce::Point<float> pivot (cx, base - 2.0f), tip (cx + sw * 4.5f, top - 0.5f);
    g.drawLine ({ pivot, tip }, 1.2f);
    const auto w = pivot + (tip - pivot) * 0.55f;
    g.fillRect (w.x - 1.5f, w.y - 1.5f, 3.0f, 3.0f);
}

int Dropdown::idealWidth() const
{
    int w = 0;
    for (auto& s : sizeSamples) w = juce::jmax (w, textW (s, kControlTextSize));
    return w + 8 + 24;
}

juce::String Dropdown::currentText() const
{
    if (! items || ! current) return {};
    const auto list = items();
    const int i = current();
    return juce::isPositiveAndBelow (i, list.size()) ? list[i] : juce::String ("-");
}

void Dropdown::paint (juce::Graphics& g)
{
    const bool h = isMouseOver() && isEnabled();
    g.setColour (h ? labelHover : dim ? kOff : text);
    outline (g, getLocalBounds().toFloat());
    const auto col = ! isEnabled() ? frame : h ? labelHover : dim ? frame : selector;
    drawLabel (g, currentText(), getLocalBounds().withTrimmedLeft (8).withTrimmedRight (22), col, kControlTextSize, juce::Justification::centredLeft, Face::SansMedium);
    chevronDown (g, { (float) getWidth() - 11.0f, (float) getHeight() * 0.5f }, col);
}

void Dropdown::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled() || ! items) return;
    const auto list = items();
    const int cur = current ? current() : -1;
    juce::PopupMenu m;
    for (int i = 0; i < list.size(); ++i) m.addItem (i + 1, list[i], true, i == cur);
    juce::Component::SafePointer<Dropdown> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                         .withItemThatMustBeVisible (cur + 1).withStandardItemHeight (26),
                     [safe] (int r) { if (safe != nullptr && r > 0 && safe->onChoose) { safe->onChoose (r - 1); safe->repaint(); } });
}

// ============================================================================ tuner

bool TunerAnalyser::active() const { return midi >= 0 && juce::Time::getMillisecondCounterHiRes() - lastValidMs < 1200.0; }

void TunerAnalyser::update (RigEngine& e)
{
    int s1, n1, s2, n2;
    const int ready = e.tunerFifo.getNumReady();
    e.tunerFifo.prepareToRead (ready, s1, n1, s2, n2);
    auto take = [&] (int st, int c) { for (int i = 0; i < c; ++i) { win[(size_t) wpos] = e.tunerBuf[(size_t) (st + i)]; wpos = (wpos + 1) & 4095; } filled = juce::jmin (4096, filled + c); };
    take (s1, n1); take (s2, n2);
    e.tunerFifo.finishedRead (n1 + n2);
    strobe += shownCents * 0.04f;
    // finestra di 128 ms (1536 campioni a fs/4): abbastanza per il Si basso, metà del tempo di prima
    if (filled < 1536) return;
    std::array<float, 1536> lin;
    for (int i = 0; i < 1536; ++i) lin[(size_t) i] = win[(size_t) ((wpos - 1536 + i) & 4095)];
    float pk = 0; for (float v : lin) pk = juce::jmax (pk, std::abs (v));
    if (pk < kGatePeak) return;   // silenzio: resta l'ultima nota, che sfuma
    const double fsd = e.tunerRate();
    float clarity = 0;
    const float f = rigMpmFrequency (lin.data(), (int) lin.size(), fsd, (int) (fsd / 27.5) + 2, clarity);
    // fruscio della DI: le finestre senza intonazione danno il suo livello; una nota deve stare 10 dB sopra
    if (clarity < 0.5f) floorPk = floorPk <= 0.0f ? pk : juce::jmin (pk, floorPk * 1.05f);
    if (pk < floorPk * 3.2f) return;
    if (f < 27.0f || f > 1320.0f || clarity < kMinClarity) return;
    hz = (hz > 0 && std::abs (f - hz) < hz * 0.03f) ? hz + 0.6f * (f - hz) : f;
    const float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int newMidi = juce::roundToInt (m);
    if (newMidi != midi) shownCents = (m - (float) newMidi) * 100.0f;
    midi = newMidi;
    cents = (m - (float) midi) * 100.0f;
    shownCents += 0.5f * (cents - shownCents);
    lastValidMs = juce::Time::getMillisecondCounterHiRes();
}

// ============================================================================ schermate

// Base comune: tutta la finestra, come il pannello about. Un clic sullo sfondo (o Esc) chiude.
// x di chiusura delle schermate
struct CloseX final : public juce::Component
{
    std::function<void()> onClick;
    CloseX() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { cross (g, getLocalBounds().toFloat().getCentre(), 6.0f, isMouseOver() ? labelHover : selector); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

// Base comune: tutta la finestra. Si chiude con la x (allineata al titolo, sul lato opposto) o con Esc.
struct ScreenBase : public juce::Component
{
    ScreenBase() { setWantsKeyboardFocus (true); close.onClick = [this] { setVisible (false); }; addAndMakeVisible (close); }
    void open() { setVisible (true); toFront (true); grabKeyboardFocus(); refresh(); repaint(); }
    virtual void refresh() {}
    void placeClose (int y0) { close.setBounds (colX() + colW() - 30, y0, 30, 30); }
    CloseX close;
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    int colW() const { return juce::jmin (560, getWidth() - 48); }
    int colX() const { return (getWidth() - colW()) / 2; }
    void paintBase (juce::Graphics& g, const juce::String& title, const juce::String& sub, int y0)
    {
        g.fillAll (juce::Colour (0xf50d0d0f));
        drawLabel (g, title, { colX(), y0, colW(), 30 }, titleName, 24.0f, juce::Justification::centredLeft, Face::SansSemiBold);
        if (sub.isNotEmpty()) drawLabel (g, sub, { colX(), y0 + 32, colW(), 18 }, info, kInfoSize);
    }
};

// Conferma prima di togliere b-blend dopo un ampli: senza la cassa l'ampli arriva crudo alle casse.
class RigComponent::ConfirmOverlay final : public juce::Component
{
public:
    std::function<void()> onConfirm;
    juce::String title, text;
    ConfirmOverlay()
    {
        setWantsKeyboardFocus (true);
        ok.caption = "remove"; ok.onClick = [this] { setVisible (false); if (onConfirm) onConfirm(); };
        ok.onColour = [] { return kAmber; }; ok.isOn = [] { return true; };
        cancel.caption = "cancel"; cancel.onClick = [this] { setVisible (false); };
        addAndMakeVisible (ok); addAndMakeVisible (cancel);
    }
    // si apre dentro il riquadro della catena, sopra le card: domanda a sinistra, tasti a destra
    void show (juce::Rectangle<int> chainBox)
    {
        setBounds (chainBox); setVisible (true); toFront (true); grabKeyboardFocus(); resized(); repaint();
    }
    void resized() override
    {
        const int y = (getHeight() - kControlH) / 2;
        int x = getWidth() - kPad - cancel.idealWidth();
        cancel.setBounds (x, y, cancel.idealWidth(), kControlH);
        x -= 12 + ok.idealWidth();
        ok.setBounds (x, y, ok.idealWidth(), kControlH);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        g.setColour (kAmber);
        outline (g, getLocalBounds().toFloat().reduced (1.0f));
        auto r = getLocalBounds().withTrimmedLeft (kPad).withRight (ok.getX() - 16);
        const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 16.0f), title));
        drawLabel (g, title, r.removeFromLeft (tw), titleName, 16.0f, juce::Justification::centredLeft, Face::SansSemiBold);
        r.removeFromLeft (14);
        drawLabel (g, text, r, labelHover, kInfoSize, juce::Justification::centredLeft, Face::SansRegular);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; }
        if (k == juce::KeyPress::returnKey) { ok.onClick(); return true; }
        return false;
    }
private:
    FlatButton ok, cancel;
};

class RigComponent::InfoOverlay final : public juce::Component
{
public:
    const ModuleInfo* m = nullptr;
    InfoOverlay() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setWantsKeyboardFocus (true); }
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    void paint (juce::Graphics& g) override
    {
        if (m == nullptr) return;
        g.fillAll (juce::Colour (0xf50d0d0f));
        const float w = juce::jmin (480.0f, (float) getWidth() - 48.0f);
        juce::AttributedString body;
        body.setLineSpacing (6.0f);
        body.append (m->info, face (Face::SansRegular, 15.0f), labelHover);
        juce::TextLayout tl; tl.createLayout (body, w);
        const float blockH = 34.0f + 26.0f + tl.getHeight() + 40.0f;
        const float x = ((float) getWidth() - w) * 0.5f;
        float y = juce::jmax (24.0f, ((float) getHeight() - blockH) * 0.5f);
        g.setColour (m->colour); g.fillRect (x, y + 9.0f, 12.0f, 12.0f);
        drawLabel (g, m->name, juce::Rectangle<float> (x + 22.0f, y, w, 30.0f).toNearestInt(), m->available() ? titleName : selector, 22.0f,
                   juce::Justification::centredLeft, Face::SansSemiBold);
        y += 34.0f;
        drawTag (g, categoryName (m->category) + (m->available() ? "" : "  -  coming soon"), x, y + 10.0f);
        y += 26.0f;
        tl.draw (g, { x, y, w, tl.getHeight() });
        y += tl.getHeight() + 28.0f;
        drawLabel (g, "click anywhere to close", juce::Rectangle<float> (x, y, w, 16.0f).toNearestInt(), info, kInfoSize);
    }
};

// ---------------------------------------------------------------- audio setup

class RigComponent::SetupScreen final : public ScreenBase, private juce::Timer
{
public:
    SetupScreen (RigEngine& e, juce::AudioDeviceManager& d) : engine (e), dm (d)
    {
        auto type = [this] { return dm.getCurrentDeviceTypeObject(); };
        out.items = [type] { return type() ? type()->getDeviceNames (false) : juce::StringArray(); };
        out.current = [this, type] { return type() ? type()->getDeviceNames (false).indexOf (dm.getAudioDeviceSetup().outputDeviceName) : -1; };
        out.onChoose = [this, type] (int i) { auto s = dm.getAudioDeviceSetup(); s.outputDeviceName = type()->getDeviceNames (false)[i]; apply (s); defaultBuffer(); };
        in.items = [type] { return type() ? type()->getDeviceNames (true) : juce::StringArray(); };
        in.current = [this, type] { return type() ? type()->getDeviceNames (true).indexOf (dm.getAudioDeviceSetup().inputDeviceName) : -1; };
        in.onChoose = [this, type] (int i) { auto s = dm.getAudioDeviceSetup(); s.inputDeviceName = type()->getDeviceNames (true)[i]; s.useDefaultInputChannels = true; apply (s); defaultBuffer(); };
        di.items = [this] { juce::StringArray a; if (auto* dev = dm.getCurrentAudioDevice()) { const auto n = dev->getInputChannelNames(); for (int i = 0; i < n.size(); ++i) a.add ("in " + juce::String (i + 1) + "   " + n[i]); } return a; };
        di.current = [this] { return engine.diChannel.load(); };
        di.onChoose = [this] (int i) { engine.diChannel = i; };
        rate.items = [this] { juce::StringArray a; if (auto* dev = dm.getCurrentAudioDevice()) for (auto r : dev->getAvailableSampleRates()) a.add (rateText (r)); return a; };
        rate.current = [this] { if (auto* dev = dm.getCurrentAudioDevice()) return dev->getAvailableSampleRates().indexOf (dev->getCurrentSampleRate()); return -1; };
        rate.onChoose = [this] (int i) { if (auto* dev = dm.getCurrentAudioDevice()) { auto s = dm.getAudioDeviceSetup(); s.sampleRate = dev->getAvailableSampleRates()[i]; apply (s); } };
        buffer.items = [this] { juce::StringArray a; for (int b : sizes()) a.add (bufferText (b)); return a; };
        buffer.current = [this] { if (auto* dev = dm.getCurrentAudioDevice()) return sizes().indexOf (dev->getCurrentBufferSizeSamples()); return -1; };
        buffer.onChoose = [this] (int i) { auto s = dm.getAudioDeviceSetup(); s.bufferSize = sizes()[i]; apply (s); };
        out.sizeSamples = in.sizeSamples = di.sizeSamples = { "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM" };
        rate.sizeSamples = buffer.sizeSamples = { "2048 samples   42.7 ms" };
        for (auto* c : { &out, &in, &di, &rate, &buffer }) addAndMakeVisible (c);
    }
    void refresh() override { error.clear(); startTimerHz (30); }
    void visibilityChanged() override { if (! isVisible()) stopTimer(); }
    static juce::String rateText (double r) { return juce::String (r / 1000.0, std::fmod (r, 1000.0) < 0.5 ? 0 : 1) + " kHz"; }
    juce::String bufferText (int b) const
    {
        double sr = 48000.0; if (auto* dev = dm.getCurrentAudioDevice()) sr = dev->getCurrentSampleRate();
        return juce::String (b) + " samples   " + juce::String (1000.0 * b / sr, 1) + " ms";
    }
    juce::Array<int> sizes() const
    {
        juce::Array<int> a;
        if (auto* dev = dm.getCurrentAudioDevice()) for (int b : dev->getAvailableBufferSizes()) if (b >= 16 && b <= 2048) a.add (b);
        return a;
    }
    void apply (juce::AudioDeviceManager::AudioDeviceSetup s) { error = dm.setAudioDeviceSetup (s, true); repaint(); }
    // una scheda appena scelta parte dal buffer di default di b-rig: 64 campioni (o il più vicino che accetta)
    void defaultBuffer()
    {
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr) return;
        auto st = dm.getAudioDeviceSetup();
        int best = dev->getCurrentBufferSizeSamples();
        for (int b : dev->getAvailableBufferSizes()) if (std::abs (b - 64) < std::abs (best - 64)) best = b;
        if (best != dev->getCurrentBufferSizeSamples()) { st.bufferSize = best; apply (st); }
    }
    int y0() const { return juce::jmax (32, (getHeight() - 470) / 2); }
    void resized() override
    {
        placeClose (y0());
        int y = y0() + 72;
        for (auto* c : { &out, &in, &di, &rate, &buffer })
        {
            c->setBounds (colX() + 150, y, juce::jmin (colW() - 150, c->idealWidth()), 26);
            y += 40;
        }
    }
    void paint (juce::Graphics& g) override
    {
        paintBase (g, "audio setup", {}, y0());
        int y = y0() + 72;
        for (auto* l : { "output", "input device", "guitar DI on", "sample rate", "buffer size" })
        {
            drawLabel (g, l, { colX(), y, 140, 26 }, labelStatic, kLabelSize);
            y += 40;
        }
        y += 14;
        g.setColour (frame); g.fillRect (colX(), y - 8, colW(), 1);
        // ingresso (la DI, grigio) e uscita (dopo la catena e il limiter, nel colore di b-rig): due misure ben distinte
        auto bar = [&] (const char* tag, const char* sub, float v, float db, bool clip, juce::Colour col)
        {
            drawLabel (g, tag, { colX(), y, 140, 16 }, labelStatic, kLabelSize);
            drawLabel (g, sub, { colX(), y + 16, 140, 14 }, info, kAxisSize);
            const auto b = juce::Rectangle<float> ((float) colX() + 150.0f, (float) y + 6.0f, 280.0f, 10.0f);
            g.setColour (valueFillOff); g.fillRect (b);
            g.setColour (v > 0.95f ? kAmber : col); g.fillRect (b.withWidth (b.getWidth() * v));
            for (int k = 1; k < 6; ++k) { g.setColour (background); g.fillRect (b.getX() + b.getWidth() * (float) k / 6.0f, b.getY(), 1.0f, b.getHeight()); }   // -10 dB
            g.setColour (clip ? kClip : juce::Colour (0xff2a2a2a)); g.fillRect (b.getRight() + 8.0f, b.getY(), 10.0f, 10.0f);
            drawAtBaseline (g, db > -59.0f ? num (db, 1) + " dB" : juce::String ("-inf"), b.getRight() + 76.0f, baselineFor (b.getCentreY(), kAxisSize, Face::MonoRegular),
                            v > 0.0f ? valueText : info, kAxisSize, juce::Justification::right);
            y += 40;
        };
        bar ("level in", "guitar DI", inShown, inDb, clipIn > 0, juce::Colour (0xff9ca3af));
        bar ("level out", "after the chain", outShown, outDb, clipOut > 0, accent);
        drawLabel (g, "safety limiter on the output: -0.5 dBFS", { colX() + 150, y + 4, colW() - 150, 16 }, info, kInfoSize);
        if (error.isNotEmpty()) drawLabel (g, error, { colX() + 150, y + 28, colW() - 150, 16 }, kClip, kInfoSize);
    }
private:
    void timerCallback() override
    {
        const float ip = engine.inPeak.exchange (0.0f), op = engine.outPeak.exchange (0.0f);
        inShown = juce::jmax (toMeter (ip), inShown - 0.04f);
        outShown = juce::jmax (toMeter (op), outShown - 0.04f);
        inDb = juce::jmax (juce::Decibels::gainToDecibels (ip, -60.0f), inDb - 0.8f);
        outDb = juce::jmax (juce::Decibels::gainToDecibels (op, -60.0f), outDb - 0.8f);
        if (engine.inClip.exchange (false)) clipIn = 45;
        if (engine.outClip.exchange (false)) clipOut = 45;
        clipIn = juce::jmax (0, clipIn - 1); clipOut = juce::jmax (0, clipOut - 1);
        repaint();
    }
    RigEngine& engine;
    juce::AudioDeviceManager& dm;
    Dropdown out, in, di, rate, buffer;
    juce::String error;
    float inShown = 0, outShown = 0, inDb = -60.0f, outDb = -60.0f;
    int clipIn = 0, clipOut = 0;
};

// ---------------------------------------------------------------- presets

class RigComponent::PresetScreen final : public ScreenBase, private juce::Timer
{
public:
    explicit PresetScreen (RigComponent& o) : owner (o)
    {
        auto btn = [this] (FlatButton& b, const char* cap, std::function<void()> f) { b.caption = cap; b.onClick = std::move (f); addAndMakeVisible (b); };
        btn (plus, "+", [this] { owner.createWithUndo(); refresh(); selectByName (owner.currentPreset); startRename (owner.currentPreset); });
        btn (save, "save", [this]
        {
            const auto name = owner.currentPreset;
            const auto before = presetFile (name).loadFileAsString();
            const auto snap = owner.snapshot();
            owner.savePreset (name);
            pushUndo ([this, name, before, snap] { presetFile (name).replaceWithText (before); owner.restoreState (snap, false); });
            refresh(); flash ("saved");
        });
        btn (saveAs, "save as new", [this] { duplicateSelected(); });
        btn (importBtn, "import", [this] { importPreset(); });
        btn (exportBtn, "export", [this] { exportSelected(); });
        btn (exportAll, "export all", [this] { exportEverything(); });
        btn (startupBtn, "load at start-up", [this] { makeSelectedStartup(); });
        btn (undoBtn, "undo", [this] { undo(); });
        undoBtn.setVisible (false);
        field.setFont (face (Face::SansMedium, 15.0f));
        field.setIndents (7, 7);
        field.onReturnKey = [this] { commitRename(); };
        field.onFocusLost = [this] { commitRename(); };
        field.onEscapeKey = [this] { renaming = -1; field.setVisible (false); repaint(); };
        addChildComponent (field);
    }
    void refresh() override
    {
        const auto keep = juce::isPositiveAndBelow (selectedRow, (int) list.size()) ? list[(size_t) selectedRow].name : juce::String();
        list = listPresets();
        startupName = startupPreset();
        selectedRow = -1;
        if (keep.isNotEmpty()) selectByName (keep);
        renaming = -1; field.setVisible (false);
        scroll = juce::jlimit (0, juce::jmax (0, (int) list.size() - visibleRows()), scroll);
        updateButtons();
        resized();
        repaint();
    }
    void visibilityChanged() override
    {
        if (isVisible()) return;
        undoStack.clear();   // l'undo vale finché la schermata resta aperta
        undoBtn.setVisible (false);
        selectedRow = -1;
    }
    void pushUndo (std::function<void()> f) { undoStack.push_back (std::move (f)); undoBtn.setVisible (true); resized(); }
    void undo()
    {
        if (undoStack.empty()) return;
        auto f = std::move (undoStack.back());
        undoStack.pop_back();
        f();
        undoBtn.setVisible (! undoStack.empty());
        refresh();
        flash ("undone");
    }
    void selectByName (const juce::String& n)
    {
        for (size_t k = 0; k < list.size(); ++k) if (list[k].name == n) selectedRow = (int) k;
        updateButtons(); repaint();
    }
    // la riga del preset diventa un campo di testo (metà larghezza, a destra) con il nome già selezionato
    void startRename (const juce::String& name)
    {
        int i = -1;
        for (size_t k = 0; k < list.size(); ++k) if (list[k].name == name) i = (int) k;
        if (i < 0 || ! renamable (i)) return;
        if (i < scroll || i >= scroll + visibleRows()) scroll = juce::jmax (0, i - visibleRows() + 1);
        renaming = i;
        field.setBounds (fieldRect (i));
        field.setText (name, false);
        field.setVisible (true);
        field.grabKeyboardFocus();
        field.selectAll();
        repaint();
    }
    int y0() const { return juce::jmax (32, (getHeight() - 560) / 2); }
    int listY() const { return y0() + 64 + kControlH + 20; }
    int maxRows() const { return juce::jmax (3, (getHeight() - listY() - 96) / kRow); }
    int visibleRows() const { return juce::jmin ((int) list.size(), maxRows()); }
    juce::Rectangle<int> rowRect (int i) const { return { colX(), listY() + (i - scroll) * kRow, colW(), kRow }; }
    juce::Rectangle<int> delRect (int i) const { auto r = rowRect (i); return { r.getRight() - 30, r.getY() + 5, 22, kRow - 10 }; }
    juce::Rectangle<int> penRect (int i) const { return delRect (i).translated (-28, 0); }
    juce::Rectangle<int> fieldRect (int i) const { auto r = rowRect (i); const int w = (r.getWidth() - 60) / 2; return { r.getRight() - 40 - w, r.getY(), w, r.getHeight() }; }
    bool renamable (int i) const { return list[(size_t) i].name != kCleanSlate; }   // tutti i preset salvati, anche "default"
    void resized() override
    {
        placeClose (y0());
        int x = colX();
        const int y = y0() + 64;
        for (auto* b : { &plus, &save, &saveAs, &importBtn, &exportBtn, &exportAll, &startupBtn })
        {
            const int w = b == &plus ? kControlH + 8 : b->idealWidth();
            b->setBounds (x, y, w, kControlH);
            x += w + kGap;
        }
        undoBtn.setBounds (colX(), getHeight() - 56, undoBtn.idealWidth(), kControlH);
        if (renaming >= 0) field.setBounds (fieldRect (renaming));
    }
    void paint (juce::Graphics& g) override
    {
        paintBase (g, "presets", "current: " + owner.currentPreset + (owner.dirty ? "  (unsaved changes)" : ""), y0());
        const auto mouse = getMouseXYRelative();
        for (int i = scroll; i < juce::jmin ((int) list.size(), scroll + visibleRows()); ++i)
        {
            const auto& e = list[(size_t) i];
            const auto r = rowRect (i);
            const bool cur = e.name == owner.currentPreset, sel = i == selectedRow, h = r.contains (mouse) && i != renaming;
            if (sel) { g.setColour (kMenuHi); g.fillRect (r); }
            else if (h) { g.setColour (kRowHover); g.fillRect (r); }
            g.setColour (frame); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
            g.setColour (e.colour); g.fillRect (r.getX() + 10, r.getCentreY() - 5, 10, 10);   // colore del preset
            if (i != renaming)
            {
                drawLabel (g, e.name, r.withTrimmedLeft (32), cur ? titleName : selector, 15.0f, juce::Justification::centredLeft, cur ? Face::SansSemiBold : Face::SansMedium);
                if (cur)
                    drawTag (g, owner.dirty ? "loaded, changed" : "loaded", (float) r.getX() + 44.0f + (float) juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 15.0f), e.name),
                             baselineFor ((float) r.getCentreY(), kTagSize, Face::SansSemiBold));
            }
            const bool startup = e.name == startupName;
            if (! renamable (i) || ! h)
            {
                const juce::String tag = e.name == kCleanSlate ? (startup ? "empty chain, loads at start-up" : "empty chain") : startup ? "loads at start-up" : "";
                if (tag.isNotEmpty())
                    drawTag (g, tag, (float) r.getRight() - 12.0f, baselineFor ((float) r.getCentreY(), kTagSize, Face::SansSemiBold), juce::Justification::right);
                continue;
            }
            // al passaggio del mouse: matita (rinomina) e x (elimina), ciascuna nel suo riquadro
            for (auto b : { penRect (i), delRect (i) }) { g.setColour (b.contains (mouse) ? labelHover : text); outline (g, b.toFloat()); }
            const auto pc = penRect (i).toFloat().getCentre();
            juce::Path pen;
            pen.startNewSubPath (pc.x - 4.0f, pc.y + 4.0f);
            pen.lineTo (pc.x - 3.0f, pc.y + 1.0f);
            pen.lineTo (pc.x + 2.5f, pc.y - 4.5f);
            pen.lineTo (pc.x + 4.5f, pc.y - 2.5f);
            pen.lineTo (pc.x - 1.0f, pc.y + 3.0f);
            pen.closeSubPath();
            g.setColour (penRect (i).contains (mouse) ? labelHover : selector);
            g.strokePath (pen, juce::PathStrokeType (1.1f, juce::PathStrokeType::mitered));
            cross (g, delRect (i).toFloat().getCentre(), 3.5f, delRect (i).contains (mouse) ? labelHover : selector);
        }
        if ((int) list.size() > visibleRows())
            drawLabel (g, juce::String ((int) list.size()) + " presets, scroll for more", rowRect (scroll + visibleRows()).withHeight (18), frame, kInfoSize);
        if (message.isNotEmpty())
            drawLabel (g, message, { undoBtn.isVisible() ? undoBtn.getRight() + 14 : colX(), undoBtn.getY(), colW() - 100, kControlH }, info, kInfoSize);
    }
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll = juce::jlimit (0, juce::jmax (0, (int) list.size() - visibleRows()), scroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0));
        repaint();
    }
    int rowAt (juce::Point<int> p) const
    {
        for (int i = scroll; i < juce::jmin ((int) list.size(), scroll + visibleRows()); ++i) if (rowRect (i).contains (p)) return i;
        return -1;
    }
    // un clic seleziona (per save as new ed export); il doppio clic carica e chiude
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this) return;
        const int i = rowAt (e.getPosition());
        if (i < 0 || i == renaming) return;
        if (renamable (i) && penRect (i).contains (e.getPosition())) { startRename (list[(size_t) i].name); return; }
        if (renamable (i) && delRect (i).contains (e.getPosition())) { deleteRow (i); return; }
        selectedRow = i; updateButtons(); repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (i < 0 || i == renaming || (renamable (i) && (penRect (i).contains (e.getPosition()) || delRect (i).contains (e.getPosition())))) return;
        loadRow (i);
        setVisible (false);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'z' || k.getKeyCode() == 'Z')) { undo(); return true; }
        if (k == juce::KeyPress::returnKey && juce::isPositiveAndBelow (selectedRow, (int) list.size())) { loadRow (selectedRow); setVisible (false); return true; }
        return ScreenBase::keyPressed (k);
    }
    void loadRow (int i)
    {
        const auto snap = owner.snapshot();
        owner.loadPreset (list[(size_t) i].name);
        pushUndo ([this, snap] { owner.restoreState (snap, true); });
    }
    void deleteRow (int i)
    {
        const auto e = list[(size_t) i];
        const auto content = presetFile (e.name).loadFileAsString();
        const bool wasCurrent = owner.currentPreset == e.name, wasDirty = owner.dirty;
        const bool wasStartup = startupPreset() == e.name;
        presetFile (e.name).deleteFile();
        indexRemove (e.name);
        if (wasStartup) setStartupPreset (kCleanSlate);   // il preset d'avvio non c'è più: si parte da una catena vuota
        if (wasCurrent) { owner.currentPreset = kCleanSlate; owner.markDirty(); }
        pushUndo ([this, e, content, wasCurrent, wasDirty, wasStartup]
        {
            presetFile (e.name).replaceWithText (content);
            indexSet (e.name, e.created, e.colour);
            if (wasStartup) setStartupPreset (e.name);
            if (wasCurrent) { owner.currentPreset = e.name; if (wasDirty) owner.markDirty(); else owner.clearDirty(); }
        });
        refresh();
        flash (wasStartup ? "deleted, the app now starts from clean slate" : "deleted");
    }
    // per i test
    std::vector<PresetEntry>& entries() { return list; }
    bool isRenaming() const { return renaming >= 0; }
    void renameForTest (const juce::String& n) { field.setText (n, false); commitRename(); }
    void selectRowForTest (int i) { selectedRow = i; updateButtons(); }
    void duplicateForTest() { duplicateSelected(); }
    void startupForTest (int i) { selectedRow = i; makeSelectedStartup(); }
private:
    static constexpr int kRow = 34;
    void updateButtons()
    {
        const bool hasSel = juce::isPositiveAndBelow (selectedRow, (int) list.size()) && list[(size_t) selectedRow].name != kCleanSlate;
        save.setEnabled (owner.dirty && owner.currentPreset != kCleanSlate);
        saveAs.setEnabled (hasSel);
        exportBtn.setEnabled (hasSel);
        exportAll.setEnabled (list.size() > 1);
        startupBtn.setEnabled (juce::isPositiveAndBelow (selectedRow, (int) list.size()) && list[(size_t) selectedRow].name != startupName);
    }
    void makeSelectedStartup()
    {
        if (! juce::isPositiveAndBelow (selectedRow, (int) list.size())) return;
        const auto before = startupPreset(), n = list[(size_t) selectedRow].name;
        setStartupPreset (n);
        pushUndo ([before] { setStartupPreset (before); });
        refresh();
        flash ("\"" + n + "\" loads at start-up");
    }
    void timerCallback() override { stopTimer(); message.clear(); repaint(); }
    void flash (const juce::String& m) { message = m; startTimer (3000); repaint(); }   // il messaggio sparisce dopo 3 s
    void duplicateSelected()
    {
        if (! juce::isPositiveAndBelow (selectedRow, (int) list.size()) || list[(size_t) selectedRow].name == kCleanSlate) return;
        const auto src = presetFile (list[(size_t) selectedRow].name);
        const auto n = nextUserName();
        if (! src.copyFileTo (presetFile (n))) return;
        indexSet (n, juce::Time::currentTimeMillis(), juce::Colours::transparentBlack);
        pushUndo ([n] { presetFile (n).deleteFile(); indexRemove (n); });
        refresh();
        selectByName (n);
        startRename (n);
    }
    void commitRename()
    {
        if (renaming < 0) return;
        const auto oldName = list[(size_t) renaming].name;
        const auto n = juce::File::createLegalFileName (field.getText().trim());
        renaming = -1;
        field.setVisible (false);
        if (n.isEmpty() || n == oldName) { repaint(); return; }
        if (n == kCleanSlate || presetFile (n).exists()) { refresh(); flash ("\"" + n + "\" already exists"); return; }
        if (renamePreset (oldName, n))
        {
            if (owner.currentPreset == oldName) owner.currentPreset = n;
            pushUndo ([this, oldName, n] { renamePreset (n, oldName); if (owner.currentPreset == n) owner.currentPreset = oldName; owner.layoutTop(); owner.repaint(); });
            owner.layoutTop();
            owner.repaint();
        }
        refresh();
        selectByName (n);
    }
    void importPreset()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a b-rig preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.brig");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto src = fc.getResult();
            if (! src.existsAsFile()) return;
            presetDir().createDirectory();
            const auto base = src.getFileNameWithoutExtension();
            auto dst = presetDir().getNonexistentChildFile (base == kCleanSlate ? "imported" : base, ".brig", false);
            if (! src.copyFileTo (dst)) return;
            const auto n = dst.getFileNameWithoutExtension();
            indexSet (n, juce::Time::currentTimeMillis(), juce::Colours::transparentBlack);
            const auto snap = owner.snapshot();
            owner.loadPreset (n);
            pushUndo ([this, n, snap] { owner.restoreState (snap, true); presetFile (n).deleteFile(); indexRemove (n); });
            refresh(); selectByName (n); flash ("imported \"" + n + "\"");
        });
    }
    void exportSelected()
    {
        if (! juce::isPositiveAndBelow (selectedRow, (int) list.size())) return;
        const auto name = list[(size_t) selectedRow].name;
        chooser = std::make_unique<juce::FileChooser> ("Export the b-rig preset", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name + ".brig"), "*.brig");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, name] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension (".brig");
            // il preset caricato e modificato si esporta com'è adesso; gli altri dal loro file
            const bool ok = name == owner.currentPreset ? owner.writeRig (f) : presetFile (name).copyFileTo (f);
            if (ok) flash ("exported to " + f.getFullPathName());
        });
    }
    void exportEverything()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export all b-rig presets to a folder", juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            int n = 0;
            for (auto& e : list) if (e.name != kCleanSlate && presetFile (e.name).copyFileTo (dir.getChildFile (e.name + ".brig"))) ++n;
            flash ("exported " + juce::String (n) + " presets to " + dir.getFullPathName());
        });
    }
    RigComponent& owner;
    FlatButton plus, save, saveAs, importBtn, exportBtn, exportAll, startupBtn, undoBtn;
    juce::String startupName;
    juce::TextEditor field;
    std::vector<PresetEntry> list;
    std::vector<std::function<void()>> undoStack;
    juce::String message;
    int renaming = -1, selectedRow = -1, scroll = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================ componente principale

struct RigComponent::ParamWatch final : public juce::AudioProcessorListener
{
    RigComponent& owner;
    explicit ParamWatch (RigComponent& o) : owner (o) {}
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { owner.paramTouched(); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { if (d.nonParameterStateChanged) owner.paramTouched(); }
};

// ultimo gesto dell'utente (mouse, rotella) in tutta la finestra
struct RigComponent::InputWatch final : public juce::MouseListener
{
    double& last;
    explicit InputWatch (double& l) : last (l) {}
    void touch() { last = juce::Time::getMillisecondCounterHiRes(); }
    void mouseDown (const juce::MouseEvent&) override { touch(); }
    void mouseUp (const juce::MouseEvent&) override { touch(); }
    void mouseDrag (const juce::MouseEvent&) override { touch(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { touch(); }
};

RigComponent::RigComponent (RigEngine& e, juce::AudioDeviceManager* devices) : engine (e), dm (devices)
{
    setOpaque (true);
    // menu a tendina e parti JUCE standard (anche dentro i moduli) col tema della suite
    juce::LookAndFeel::setDefaultLookAndFeel (&laf);

    presetsBtn.caption = "presets";
    presetsBtn.onClick = [this] { showPresets(); };
    saveBtn.caption = "save";
    saveBtn.onClick = [this] { saveFromTopBar(); };
    saveBtn.setEnabled (false);   // si accende alla prima modifica
    addAndMakeVisible (saveBtn);
    paramWatch = std::make_unique<ParamWatch> (*this);
    inputWatch = std::make_unique<InputWatch> (lastInputMs);
    juce::Desktop::getInstance().addGlobalMouseListener (inputWatch.get());
    setupBtn.caption = "setup";
    setupBtn.onClick = [this] { showAudioSetup(); };
    setupBtn.setEnabled (dm != nullptr);

    metroBtn.isOn = [this] { return engine.metroOn.load(); };
    metroBtn.onClick = [this] { engine.metroOn = ! engine.metroOn.load(); };
    metroBtn.onRightClick = [this] { showMetroMenu(); };
    metroBtn.swing = [this] { const int b = engine.beatInBar.load(); return engine.metroOn.load() && b >= 0 ? (b % 2 == 0 ? -1.0f : 1.0f) : 0.4f; };

    // tempo e misura del metronomo: quelli dell'ultima sessione (non dipendono dai preset)
    controls.set ("bpm", engine.bpm.load());
    controls.set ("sig", (float) engine.sigIndex.load());
    lastBpmParam = engine.bpm.load();
    bpmBox.setParameter (&controls.p ("bpm"));
    bpmBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " bpm"; };
    bpmBox.step = [] (float v, int dir) { return v + (float) dir; };
    bpmBox.drag = [] (float v0, float, float dx) { return v0 + dx / 2.0f; };
    bpmBox.sizeSamples = { "300 bpm" };

    sigBox.items = [] { juce::StringArray a; for (auto s : RigEngine::kSigs) a.add (juce::String (s.first) + "/" + juce::String (s.second)); return a; };
    sigBox.current = [this] { return juce::roundToInt (controls.get ("sig")); };
    sigBox.onChoose = [this] (int i) { controls.set ("sig", (float) i); engine.sigIndex = i; };
    sigBox.sizeSamples = { "7/8" };

    tapBtn.caption = "tap";
    tapBtn.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
        taps.push_back (now);
        if (taps.size() > 5) taps.erase (taps.begin());
        if (taps.size() >= 2)
        {
            const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
            controls.set ("bpm", (float) juce::jlimit ((double) RigEngine::kMinBpm, (double) RigEngine::kMaxBpm, std::round (60000.0 / avg)));
        }
    };

    muteBtn.caption = "mute";
    muteBtn.isOn = [this] { return engine.globalMute.load(); };
    muteBtn.onClick = [this]
    {
        engine.globalMute = ! engine.globalMute.load();
        engine.tunerMutes = engine.globalMute.load();   // la scelta resta: la prossima volta il tuner parte così
    };
    muteBtn.onColour = [] { return kAmber; };

    for (auto* c : std::initializer_list<juce::Component*> { &presetsBtn, &setupBtn, &metroBtn, &bpmBox, &sigBox, &tapBtn, &undoBtn, &redoBtn })
        addAndMakeVisible (c);
    undoBtn.onClick = [this] { undoRig(); };
    redoBtn.onClick = [this] { redoRig(); };
    setWantsKeyboardFocus (true);
    addChildComponent (muteBtn);

    infoOverlay = std::make_unique<InfoOverlay>();
    addChildComponent (*infoOverlay);
    confirm = std::make_unique<ConfirmOverlay>();
    addChildComponent (*confirm);
    if (dm != nullptr) { setup = std::make_unique<SetupScreen> (engine, *dm); addChildComponent (*setup); }
    presets = std::make_unique<PresetScreen> (*this);
    addChildComponent (*presets);
    about = std::make_unique<AboutOverlay> (*this, "rig");
    about->logoAt = { kMargin - 1, 10 };

    engine.onChainChanged = [this] { chainChanged(); };
    if (engine.numSlots() == 0) engine.resetDefault();
    selectStartModule();
    syncWatchers();
    commitHistory();   // lo stato d'avvio è il primo passo

    setSize (1180, 760);
    startTimerHz (60);
}

RigComponent::~RigComponent()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (inputWatch.get());
    for (auto* p : watched) p->removeListener (paramWatch.get());
    watched.clear();
    engine.onChainChanged = nullptr;
    closeEditor();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void RigComponent::selectStartModule()
{
    for (int i = 0; i < engine.numSlots(); ++i)
        if (engine.slot (i).info->id == "amp") { select (&engine.slot (i)); return; }   // b-amp al centro
    if (engine.numSlots() > 0) select (&engine.slot (0));
}

// ---------------------------------------------------------------- preset

juce::File RigComponent::presetDir()
{
    if (presetDirOverride() != juce::File()) return presetDirOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("b-suite").getChildFile ("b-rig").getChildFile ("Presets");
}

namespace
{
    // colori dei preset: accesi, ben visibili sullo sfondo scuro (il bianco è riservato a "clean slate")
    const std::array<juce::uint32, 12> kPresetColours { { 0xfff87171, 0xfffb923c, 0xfffacc15, 0xffa3e635, 0xff4ade80, 0xff2dd4bf,
                                                          0xff22d3ee, 0xff60a5fa, 0xff818cf8, 0xffc084fc, 0xfff472b6, 0xfffda4af } };
    juce::File indexFile() { return RigComponent::presetDir().getChildFile ("index.xml"); }
    juce::ValueTree loadIndex()
    {
        if (auto x = juce::XmlDocument::parse (indexFile())) { auto t = juce::ValueTree::fromXml (*x); if (t.hasType ("PRESETS")) return t; }
        return juce::ValueTree ("PRESETS");
    }
    void saveIndex (const juce::ValueTree& t) { RigComponent::presetDir().createDirectory(); if (auto x = t.createXml()) x->writeTo (indexFile()); }
}

void RigComponent::indexSet (const juce::String& name, juce::int64 created, juce::Colour c)
{
    auto t = loadIndex();
    auto e = t.getChildWithProperty ("name", name);
    if (! e.isValid()) { e = juce::ValueTree ("P"); e.setProperty ("name", name, nullptr); t.appendChild (e, nullptr); }
    e.setProperty ("created", created, nullptr);
    e.setProperty ("colour", (juce::int64) c.getARGB(), nullptr);
    saveIndex (t);
}

void RigComponent::indexRemove (const juce::String& name)
{
    auto t = loadIndex();
    auto e = t.getChildWithProperty ("name", name);
    if (e.isValid()) { t.removeChild (e, nullptr); saveIndex (t); }
}

bool RigComponent::renamePreset (const juce::String& from, const juce::String& to)
{
    if (! presetFile (from).moveFileTo (presetFile (to))) return false;
    auto t = loadIndex();
    auto e = t.getChildWithProperty ("name", from);
    if (e.isValid()) e.setProperty ("name", to, nullptr);
    if (t["startup"].toString() == from) t.setProperty ("startup", to, nullptr);   // il preset d'avvio segue il nome nuovo
    saveIndex (t);
    return true;
}

juce::String RigComponent::startupPreset()
{
    const auto n = loadIndex()["startup"].toString();
    return n.isNotEmpty() ? n : juce::String (kDefaultPreset);
}

void RigComponent::setStartupPreset (const juce::String& name)
{
    auto t = loadIndex();
    t.setProperty ("startup", name, nullptr);
    saveIndex (t);
}

void RigComponent::loadStartup (RigEngine& e)
{
    const auto n = startupPreset();
    if (n == kCleanSlate) { e.clear(); return; }
    if (! loadPresetFile (e, presetFile (n))) e.resetDefault();
}

juce::String RigComponent::nextUserName()
{
    int n = 1;
    while (presetFile ("User " + juce::String (n)).exists()) ++n;
    return "User " + juce::String (n);
}

// Elenco: "clean slate" in cima (bianco), poi dal più recente al più vecchio. Ogni preset ha un colore casuale
// diverso dai due che lo seguono, così due colori uguali non sono mai a meno di due posizioni di distanza.
std::vector<RigComponent::PresetEntry> RigComponent::listPresets()
{
    auto t = loadIndex();
    bool changed = false;
    juce::StringArray names;
    for (auto& f : presetDir().findChildFiles (juce::File::findFiles, false, "*.brig"))
    {
        const auto n = f.getFileNameWithoutExtension();
        names.add (n);
        if (! t.getChildWithProperty ("name", n).isValid())
        {
            juce::ValueTree e ("P");
            e.setProperty ("name", n, nullptr);
            e.setProperty ("created", f.getCreationTime().toMilliseconds(), nullptr);
            e.setProperty ("colour", (juce::int64) 0, nullptr);
            t.appendChild (e, nullptr);
            changed = true;
        }
    }
    for (int k = t.getNumChildren(); --k >= 0;)
        if (! names.contains (t.getChild (k)["name"].toString())) { t.removeChild (k, nullptr); changed = true; }

    std::vector<PresetEntry> v;
    for (auto e : t) v.push_back ({ e["name"].toString(), juce::Colour ((juce::uint32) (juce::int64) e["colour"]), (juce::int64) e["created"] });
    std::sort (v.begin(), v.end(), [] (const PresetEntry& a, const PresetEntry& b) { return a.created != b.created ? a.created > b.created : a.name.compareNatural (b.name) < 0; });
    auto& rnd = juce::Random::getSystemRandom();
    for (int i = (int) v.size(); --i >= 0;)
    {
        std::vector<juce::uint32> avoid;
        for (int d = 1; d <= 2; ++d) if (i + d < (int) v.size()) avoid.push_back (v[(size_t) (i + d)].colour.getARGB());
        const auto c = v[(size_t) i].colour.getARGB();
        const bool valid = std::find (kPresetColours.begin(), kPresetColours.end(), c) != kPresetColours.end();
        if (valid && std::find (avoid.begin(), avoid.end(), c) == avoid.end()) continue;
        juce::uint32 pick;
        do pick = kPresetColours[(size_t) rnd.nextInt ((int) kPresetColours.size())];
        while (std::find (avoid.begin(), avoid.end(), pick) != avoid.end());
        v[(size_t) i].colour = juce::Colour (pick);
        t.getChildWithProperty ("name", v[(size_t) i].name).setProperty ("colour", (juce::int64) pick, nullptr);
        changed = true;
    }
    if (changed) saveIndex (t);
    v.insert (v.begin(), PresetEntry { kCleanSlate, juce::Colours::white, std::numeric_limits<juce::int64>::max() });
    return v;
}

bool RigComponent::loadPresetFile (RigEngine& e, const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    const auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("BRIG")) return false;
    e.loadRig (t);
    return true;
}

bool RigComponent::writeRig (const juce::File& f)
{
    auto t = engine.saveRig();
    t.setProperty ("selected", engine.indexOf (selected), nullptr);
    f.getParentDirectory().createDirectory();
    auto xml = t.createXml();
    return xml != nullptr && xml->writeTo (f);
}

void RigComponent::savePreset (const juce::String& name)
{
    const bool isNew = ! presetFile (name).exists();
    if (writeRig (presetFile (name)))
    {
        if (isNew) indexSet (name, juce::Time::currentTimeMillis(), juce::Colours::transparentBlack);
        currentPreset = name;
        clearDirty();
    }
    repaint (topArea());
}

juce::String RigComponent::saveAsUserPreset()
{
    const auto name = nextUserName();
    savePreset (name);
    return name;
}

RigComponent::RigSnapshot RigComponent::snapshot() const
{
    auto t = engine.saveRig();
    t.setProperty ("selected", engine.indexOf (selected), nullptr);
    return { t, currentPreset, dirty };
}

void RigComponent::restoreState (const RigSnapshot& s, bool rig)
{
    if (rig)
    {
        suppressUntilMs = juce::Time::getMillisecondCounterHiRes() + 800.0;
        select (nullptr);
        engine.loadRig (s.rig);
        const int sel = (int) s.rig.getProperty ("selected", -1);
        if (juce::isPositiveAndBelow (sel, engine.numSlots())) select (&engine.slot (sel)); else selectStartModule();
    }
    currentPreset = s.preset;
    if (s.dirty) markDirty(); else clearDirty();
    layoutTop();
    repaint();
}

// nuovo preset "User N" dallo stato attuale; l'undo lo toglie e torna com'era
juce::String RigComponent::createWithUndo()
{
    const auto snap = snapshot();
    const auto name = saveAsUserPreset();
    presets->pushUndo ([this, name, snap] { presetFile (name).deleteFile(); indexRemove (name); restoreState (snap, false); });
    return name;
}

void RigComponent::saveFromTopBar()
{
    showPresets();
    const auto name = createWithUndo();
    presets->refresh();
    presets->selectByName (name);
    presets->startRename (name);
}

bool RigComponent::isPresetRenaming() const { return presets->isRenaming(); }
void RigComponent::renamePresetForTest (const juce::String& n) { presets->renameForTest (n); }
juce::StringArray RigComponent::presetNamesForTest() const { juce::StringArray a; for (auto& e : presets->entries()) a.add (e.name); return a; }
std::vector<RigComponent::PresetEntry> RigComponent::presetEntriesForTest() const { return presets->entries(); }
void RigComponent::deletePresetForTest (int row) { presets->deleteRow (row); }
void RigComponent::undoPresetForTest() { presets->undo(); }
void RigComponent::selectPresetForTest (int row) { presets->selectRowForTest (row); }
void RigComponent::duplicatePresetForTest() { presets->duplicateForTest(); }
void RigComponent::loadPresetRowForTest (int row) { presets->loadRow (row); }
void RigComponent::setStartupForTest (int row) { presets->startupForTest (row); }

void RigComponent::markDirty()
{
    if (dirty) return;
    dirty = true;
    saveBtn.setEnabled (true);
    layoutTop();
    repaint (topArea());
    if (presets->isVisible()) presets->refresh();
}

void RigComponent::clearDirty()
{
    dirty = false;
    saveBtn.setEnabled (false);
    layoutTop();
    repaint (topArea());
}

void RigComponent::paramTouched()
{
    // solo i cambi fatti dall'utente: un gesto del mouse nell'ultimo secondo o un valore scritto a tastiera.
    // Il caricamento di un preset e i parametri che un modulo aggiorna da solo non contano.
    if (! juce::MessageManager::existsAndIsCurrentThread()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now < suppressUntilMs) return;
    const bool typing = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    if (typing || now - lastInputMs < 1000.0)
    {
        historyPending = true; historyChangeMs = now;   // a gesto finito diventa un passo di undo
        if (! dirty) markDirty();
    }
}

void RigComponent::syncWatchers()
{
    std::vector<juce::AudioProcessor*> now;
    for (int i = 0; i < engine.numSlots(); ++i) if (auto* p = engine.slot (i).proc.get()) now.push_back (p);
    // i moduli tolti dalla catena sono ancora vivi (si liberano dopo): si stacca l'ascolto adesso
    for (auto* p : watched) if (std::find (now.begin(), now.end(), p) == now.end()) p->removeListener (paramWatch.get());
    for (auto* p : now) if (std::find (watched.begin(), watched.end(), p) == watched.end()) p->addListener (paramWatch.get());
    watched = now;
}

void RigComponent::loadPreset (const juce::String& name)
{
    suppressUntilMs = juce::Time::getMillisecondCounterHiRes() + 800.0;
    select (nullptr);
    currentPreset = name;
    if (name == kCleanSlate) { engine.clear(); clearDirty(); repaint(); return; }   // catena vuota
    const auto f = presetFile (name);
    if (! loadPresetFile (engine, f))
        engine.resetDefault();   // "default" non ancora salvato: la catena di fabbrica
    int sel = -1;
    if (auto xml = juce::XmlDocument::parse (f)) sel = xml->getIntAttribute ("selected", -1);
    if (juce::isPositiveAndBelow (sel, engine.numSlots())) select (&engine.slot (sel));
    else selectStartModule();
    clearDirty();
    repaint();
}

// ---------------------------------------------------------------- layout

void RigComponent::resized()
{
    layoutTop();
    layoutLibrary();
    const auto t = tunerArea();
    muteBtn.setBounds (kPad, t.getY() + 41, 50, kControlH);
    chainContentW = engine.numSlots() * (kCardW + kConn) + kConn;
    chainScroll = juce::jlimit (0, juce::jmax (0, chainContentW - cardsViewport().getWidth()), chainScroll);
    for (auto* c : std::initializer_list<juce::Component*> { infoOverlay.get(), setup.get(), presets.get() })
        if (c != nullptr) c->setBounds (getLocalBounds());
    if (confirm != nullptr && confirm->isVisible()) confirm->setBounds (chainArea());
    rebuildEditor();
}

void RigComponent::layoutTop()
{
    const int y = (kTopH - kControlH) / 2;
    // logo a sinistra | presets e metronomo al centro | setup a destra
    setupBtn.setBounds (getWidth() - kMargin - setupBtn.idealWidth(), y, setupBtn.idealWidth(), kControlH);
    const int nameW = textW (dirty ? juce::String ("unsaved") : currentPreset, kControlTextSize, Face::SansSemiBold) + 2;
    const int saveW = saveBtn.idealWidth() + 10;
    const int total = presetsBtn.idealWidth() + 10 + nameW + 10 + saveW + 30 + kControlH + kGap + bpmBox.getIdealWidth() + kGap + sigBox.idealWidth() + kGap + tapBtn.idealWidth();
    int x = (getWidth() - total) / 2;
    auto place = [&] (juce::Component& c, int w, int gap) { c.setBounds (x, y, w, kControlH); x += w + gap; };
    place (presetsBtn, presetsBtn.idealWidth(), 10 + nameW + 10);
    place (saveBtn, saveBtn.idealWidth(), 10);
    x += 30;
    place (metroBtn, kControlH, kGap);
    place (bpmBox, bpmBox.getIdealWidth(), kGap);
    place (sigBox, sigBox.idealWidth(), kGap);
    place (tapBtn, tapBtn.idealWidth(), 0);
    // undo / redo: al centro tra il logo e "presets"
    const int logoRight = kMargin - 1 + (int) std::ceil (logoWidth ("rig", kLogoSize));
    const int pairW = 2 * kControlH + 6, cx = (logoRight + presetsBtn.getX()) / 2;
    undoBtn.setBounds (cx - pairW / 2, y, kControlH, kControlH);
    redoBtn.setBounds (cx - pairW / 2 + kControlH + 6, y, kControlH, kControlH);
}

void RigComponent::layoutLibrary()
{
    libRows.clear(); libCats.clear();
    int y = libArea().getY() + 10;
    for (auto cat : { Category::pre, Category::core, Category::post, Category::master })
    {
        libCats.push_back ({ categoryName (cat), y });
        y += kCatH;
        // in ogni categoria, nell'ordine in cui i moduli stanno nella catena (b-distort prima di b-amp...)
        std::vector<const ModuleInfo*> inCat;
        for (auto& m : modules()) if (m.category == cat) inCat.push_back (&m);
        std::stable_sort (inCat.begin(), inCat.end(), [] (const ModuleInfo* a, const ModuleInfo* b) { return a->order < b->order; });
        for (auto* mp : inCat)
        {
            auto& m = *mp;
            LibRow r;
            r.m = &m;
            r.row = { 0, y, kLibW, kRowH };
            r.add = { kLibW - kPad - kControlH, y + (kRowH - kControlH) / 2, kControlH, kControlH };
            r.info = r.add.translated (-(kControlH + 6), 0);
            libRows.push_back (r);
            y += kRowH;
        }
        y += 6;
    }
}

juce::Rectangle<int> RigComponent::cardsViewport() const
{
    const auto c = chainArea();
    const int l = c.getX() + kPad + 44, r = c.getRight() - kPad - 84;
    return { l, c.getY(), juce::jmax (0, r - l), c.getHeight() };
}

juce::Rectangle<int> RigComponent::cardRect (int i) const
{
    const auto vp = cardsViewport();
    return { vp.getX() + kConn + i * (kCardW + kConn) - chainScroll, vp.getCentreY() - kCardH / 2, kCardW, kCardH };
}

int RigComponent::gapAt (int x) const
{
    const auto vp = cardsViewport();
    const float content = (float) (x - vp.getX() + chainScroll) - (float) kConn * 0.5f;
    return juce::jlimit (0, engine.numSlots(), juce::roundToInt (content / (float) (kCardW + kConn)));
}

std::pair<int, RigComponent::Hit> RigComponent::chainHit (juce::Point<int> p) const
{
    if (! cardsViewport().contains (p)) return { -1, Hit::none };
    for (int i = 0; i < engine.numSlots(); ++i)
    {
        const auto r = cardRect (i);
        if (! r.contains (p)) continue;
        const auto& s = const_cast<RigEngine&> (engine).slot (i);
        const bool badge = s.info->safeArm && s.proc && ! s.enabled.load();
        if (juce::Rectangle<int> (r.getX(), r.getY(), 26, 26).contains (p)) return { i, Hit::led };
        if (juce::Rectangle<int> (r.getRight() - 22, r.getY() + 2, 22, 24).contains (p)) return { i, Hit::remove };
        if (badge && p.y > r.getY() + 26) return { i, Hit::badge };
        if (! badge && juce::Rectangle<int> (r.getRight() - 46, r.getY() + 26, 21, 22).contains (p)) return { i, Hit::left };
        if (! badge && juce::Rectangle<int> (r.getRight() - 25, r.getY() + 26, 21, 22).contains (p)) return { i, Hit::right };
        return { i, Hit::card };
    }
    return { -1, Hit::none };
}

// ---------------------------------------------------------------- catena e workspace

void RigComponent::closeEditor()
{
    // come un host che chiude la finestra: il modulo deve saperlo, o non potrà aprire un'altra interfaccia
    if (editor != nullptr && editorOwner != nullptr) editorOwner->editorBeingDeleted (editor.get());
    editor.reset();
    editorOwner = nullptr;
}

void RigComponent::select (Slot* s)
{
    if (s == selected && s != nullptr && (editor != nullptr || s->proc == nullptr)) return;
    suppressUntilMs = juce::jmax (suppressUntilMs, juce::Time::getMillisecondCounterHiRes() + 300.0);   // aprire un'interfaccia non è una modifica
    closeEditor();
    selected = s;
    if (s != nullptr && s->proc != nullptr)
    {
        editor.reset (s->proc->createEditorAndMakeActive());
        if (editor) { editorOwner = s->proc.get(); addAndMakeVisible (*editor); }
    }
    for (auto* o : std::initializer_list<juce::Component*> { infoOverlay.get(), confirm.get(), setup.get(), presets.get(), about.get() })
        if (o != nullptr && o->isVisible()) o->toFront (false);
    rebuildEditor();
    scrollToSlot (engine.indexOf (s));
    repaint();
}

void RigComponent::rebuildEditor()
{
    if (editor == nullptr) return;
    const auto ws = wsArea();
    const auto area = juce::Rectangle<int> (ws.getX() + kPad, ws.getY() + kWsHeadH, ws.getWidth() - 2 * kPad, ws.getHeight() - kWsHeadH - kPad);
    editor->setTransform ({});
    if (editor->isResizable())
    {
        int w = area.getWidth(), h = area.getHeight();
        if (auto* c = editor->getConstrainer())
        {
            w = juce::jlimit (c->getMinimumWidth(), juce::jmax (c->getMinimumWidth(), c->getMaximumWidth()), w);
            h = juce::jlimit (c->getMinimumHeight(), juce::jmax (c->getMinimumHeight(), c->getMaximumHeight()), h);
        }
        editor->setSize (w, h);
    }
    const int w = editor->getWidth(), h = editor->getHeight();
    editorScale = juce::jmin (1.0f, (float) area.getWidth() / (float) juce::jmax (1, w), (float) area.getHeight() / (float) juce::jmax (1, h));
    const float x = (float) area.getX() + ((float) area.getWidth() - (float) w * editorScale) * 0.5f;
    editor->setTopLeftPosition (0, 0);
    const float y = (float) area.getY() + ((float) area.getHeight() - (float) h * editorScale) * 0.5f;
    editor->setTransform (juce::AffineTransform::scale (editorScale).translated (std::round (x), std::round (y)));
}

void RigComponent::scrollToSlot (int i)
{
    if (i < 0) return;
    const auto vp = cardsViewport();
    const int x0 = kConn + i * (kCardW + kConn), x1 = x0 + kCardW + kConn;
    if (x0 - chainScroll < 0) chainScroll = x0 - kConn;
    else if (x1 - chainScroll > vp.getWidth()) chainScroll = x1 - vp.getWidth();
    chainScroll = juce::jlimit (0, juce::jmax (0, chainContentW - vp.getWidth()), chainScroll);
}

void RigComponent::chainChanged()
{
    historyPending = true; historyChangeMs = juce::Time::getMillisecondCounterHiRes();
    if (selected != nullptr && engine.indexOf (selected) < 0)
    {
        closeEditor();   // il processore è ancora vivo (si libera dopo): l'editor va chiuso prima
        selected = nullptr;
    }
    syncWatchers();
    resized();
    if (selected == nullptr && engine.numSlots() > 0) select (&engine.slot (0));
    scrollToSlot (engine.indexOf (selected));
    repaint();
}

void RigComponent::armOrToggle (Slot& s)
{
    const int i = engine.indexOf (&s);
    if (i < 0 || s.proc == nullptr) return;
    requestEnabled (i, ! s.enabled.load());
}

bool RigComponent::cabProtects (RigEngine& e, int index)
{
    if (! juce::isPositiveAndBelow (index, e.numSlots())) return false;
    const auto& s = e.slot (index);
    if (s.info->id != "blend" || ! s.enabled.load()) return false;
    for (int k = 0; k < index; ++k) if (e.slot (k).info->safeArm) return true;   // un ampli prima, a qualunque distanza
    return false;
}

void RigComponent::requestEnabled (int i, bool on)
{
    if (! juce::isPositiveAndBelow (i, engine.numSlots())) return;
    if (! on && cabProtects (engine, i)) return;   // b-blend dopo un ampli resta acceso: si può solo togliere o spostare
    if (engine.slot (i).enabled.load() != on) { markDirty(); historyPending = true; historyChangeMs = juce::Time::getMillisecondCounterHiRes(); }
    engine.setEnabled (i, on);
    repaint();
}

void RigComponent::showMetroMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("sound");
    for (int k = 0; k < RigEngine::kNumSounds; ++k) m.addItem (10 + k, RigEngine::kSoundNames[(size_t) k], true, engine.metroSound.load() == k);
    m.addSectionHeader ("volume");
    // slider del volume dentro il menu (trascinare o cliccare; il menu resta aperto)
    struct VolumeItem final : public juce::PopupMenu::CustomComponent
    {
        explicit VolumeItem (RigEngine& e) : juce::PopupMenu::CustomComponent (false), engine (e) {}
        void getIdealSize (int& w, int& h) override { w = 220; h = 30; }
        juce::Rectangle<float> track() const { return { 14.0f, (float) getHeight() * 0.5f - 2.0f, (float) getWidth() - 74.0f, 4.0f }; }
        void set (int x) { engine.metroVolume = juce::jlimit (0.0f, 1.0f, ((float) x - track().getX()) / track().getWidth()); repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { set (e.x); }
        void mouseDrag (const juce::MouseEvent& e) override { set (e.x); }
        void paint (juce::Graphics& g) override
        {
            const float v = engine.metroVolume.load();
            const auto t = track();
            g.setColour (valueFill); g.fillRect (t);
            g.setColour (accent); g.fillRect (t.withWidth (t.getWidth() * v));
            g.fillRect (t.getX() + t.getWidth() * v - 2.0f, t.getCentreY() - 6.0f, 4.0f, 12.0f);
            drawAtBaseline (g, juce::String (juce::roundToInt (v * 100.0f)) + " %", (float) getWidth() - 12.0f,
                            baselineFor ((float) getHeight() * 0.5f, kValueSize, Face::MonoMedium), valueText, kValueSize, juce::Justification::right, Face::MonoMedium);
        }
        RigEngine& engine;
    };
    m.addCustomItem (200, std::make_unique<VolumeItem> (engine), nullptr, "volume");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metroBtn).withStandardItemHeight (26), [this] (int r)
    {
        if (r >= 10 && r < 10 + RigEngine::kNumSounds) engine.metroSound = r - 10;
    });
}

void RigComponent::paintOverChildren (juce::Graphics& g)
{
    if (! libDragging || libPress == nullptr) return;
    const auto vp = cardsViewport();
    if (vp.contains (libDragPos))
    {
        const int gap = gapAt (libDragPos.x);
        const int gx = vp.getX() + gap * (kCardW + kConn) + kConn / 2 - chainScroll;
        g.setColour (libPress->colour);
        g.fillRect (gx - 1, vp.getCentreY() - kCardH / 2 - 4, 2, kCardH + 8);
    }
    // il modulo che segue il mouse
    const auto r = juce::Rectangle<int> (kCardW, kCardH).withCentre (libDragPos).toFloat();
    g.setColour (background.withAlpha (0.92f)); g.fillRect (r);
    g.setColour (libPress->colour); outline (g, r);
    g.fillRect (r.getX() + 9.0f, r.getY() + 9.0f, 10.0f, 10.0f);
    drawLabel (g, libPress->name, { (int) r.getX() + 26, (int) r.getY() + 4, kCardW - 30, 20 }, titleName, 13.0f, juce::Justification::centredLeft, Face::SansMedium);
    drawLabel (g, vp.contains (libDragPos) ? "drop to insert" : "drag onto the chain", { (int) r.getX() + 9, (int) r.getY() + 29, kCardW - 18, 14 }, info, 11.0f, juce::Justification::centredLeft, Face::SansMedium);
}

void RigComponent::commitHistory()
{
    historyPending = false;
    auto t = engine.saveRig();
    t.setProperty ("selected", engine.indexOf (selected), nullptr);
    const auto xml = t.toXmlString();
    if (juce::isPositiveAndBelow (historyPos, (int) history.size()) && history[(size_t) historyPos] == xml) return;
    history.resize ((size_t) (historyPos + 1));   // una modifica nuova cancella i redo
    history.push_back (xml);
    if (history.size() > 60) history.erase (history.begin());   // ultimi 60 passi
    historyPos = (int) history.size() - 1;
    undoBtn.setEnabled (historyPos > 0);
    redoBtn.setEnabled (false);
}

void RigComponent::applyHistory (const juce::String& xml)
{
    const auto t = juce::ValueTree::fromXml (xml);
    if (! t.hasType ("BRIG")) return;
    suppressUntilMs = juce::Time::getMillisecondCounterHiRes() + 800.0;
    // stessa catena (stessi moduli nello stesso ordine): si rimettono solo stati e on/off, senza ricreare nulla
    bool same = t.getNumChildren() == engine.numSlots();
    for (int i = 0; same && i < engine.numSlots(); ++i) same = t.getChild (i)["id"].toString() == engine.slot (i).info->id;
    if (same)
    {
        for (int i = 0; i < engine.numSlots(); ++i)
        {
            const auto c = t.getChild (i);
            auto& sl = engine.slot (i);
            if (sl.proc != nullptr && c.hasProperty ("state"))
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (c["state"].toString())) sl.proc->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            engine.setEnabled (i, (bool) c.getProperty ("enabled", true));
        }
    }
    else
    {
        select (nullptr);
        engine.loadRig (t);
        const int sel = (int) t.getProperty ("selected", -1);
        if (juce::isPositiveAndBelow (sel, engine.numSlots())) select (&engine.slot (sel)); else selectStartModule();
    }
    historyPending = false;
    markDirty();
    repaint();
}

void RigComponent::undoRig()
{
    if (historyPending) commitHistory();   // prima la modifica appena fatta
    if (historyPos <= 0) return;
    applyHistory (history[(size_t) --historyPos]);
    undoBtn.setEnabled (historyPos > 0);
    redoBtn.setEnabled (true);
}

void RigComponent::redoRig()
{
    if (historyPos + 1 >= (int) history.size()) return;
    applyHistory (history[(size_t) ++historyPos]);
    undoBtn.setEnabled (true);
    redoBtn.setEnabled (historyPos + 1 < (int) history.size());
}

bool RigComponent::keyPressed (const juce::KeyPress& k)
{
    // cmd+Z (ctrl su Windows) = undo, cmd+shift+Z = redo
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'z' || k.getKeyCode() == 'Z'))
    {
        if (k.getModifiers().isShiftDown()) redoRig(); else undoRig();
        return true;
    }
    return false;
}

// la x di una card: b-blend dopo un ampli si toglie solo dopo una conferma, gli altri subito
bool RigComponent::confirmVisibleForTest() const { return confirm->isVisible(); }
void RigComponent::closeScreensForTest() { for (auto* c : std::initializer_list<juce::Component*> { infoOverlay.get(), setup.get(), presets.get() }) if (c != nullptr) c->setVisible (false); }
void RigComponent::confirmForTest() { confirm->setVisible (false); if (confirm->onConfirm) confirm->onConfirm(); }

void RigComponent::requestRemove (int i)
{
    if (! juce::isPositiveAndBelow (i, engine.numSlots())) return;
    auto doRemove = [this, slot = &engine.slot (i)]
    {
        const int idx = engine.indexOf (slot);
        if (idx < 0) return;
        engine.removeSlot (idx);
        markDirty();
        repaint();
    };
    if (! cabProtects (engine, i)) { doRemove(); return; }
    confirm->title = "remove b-blend?";
    confirm->text = "b-amp comes before it: without the cab the amp goes raw to the speakers.";
    confirm->onConfirm = doRemove;
    confirm->show (chainArea());
}

bool RigComponent::inChain (const juce::String& id) const
{
    for (int i = 0; i < engine.numSlots(); ++i) if (engine.slot (i).info->id == id) return true;
    return false;
}

void RigComponent::addFromLibrary (const juce::String& id)
{
    const auto* m = findModule (id);
    if (m == nullptr || ! m->available() || inChain (id)) return;   // un solo modulo per tipo
    // nella posizione logica della catena (ordine della FX chain della suite)
    if (auto* s = engine.addModule (id)) { select (s); markDirty(); }
}

void RigComponent::showInfo (const juce::String& id)
{
    infoOverlay->m = findModule (id);
    infoOverlay->setBounds (getLocalBounds());
    infoOverlay->setVisible (true);
    infoOverlay->toFront (true);
    infoOverlay->repaint();
}

void RigComponent::showAudioSetup()
{
    if (! setup) return;
    infoOverlay->setVisible (false);
    setup->setBounds (getLocalBounds());
    setup->open();
}

void RigComponent::showPresets()
{
    infoOverlay->setVisible (false);
    presets->setBounds (getLocalBounds());
    presets->open();
}

void RigComponent::setTunerActive (bool on)
{
    // acceso = l'uscita si muta (si può riattivare col tasto mute); spento = nessun calcolo e audio di nuovo libero
    engine.tunerOn = on;
    engine.globalMute = on && engine.tunerMutes.load();   // muta solo se l'utente non ha scelto di sentire mentre accorda
    tuner.reset();
    muteBtn.setVisible (on);
    repaint (tunerArea());
}

void RigComponent::dragCardForTest (int from, int toGap)
{
    const int to = toGap > from ? toGap - 1 : toGap;
    if (to != from) { engine.moveSlotTo (from, to); markDirty(); }
}

// ---------------------------------------------------------------- mouse

void RigComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    pressIndex = -1; dragIndex = -1;
    if (tunerArea().contains (p)) { setTunerActive (! engine.tunerOn.load()); return; }
    for (auto& r : libRows)
    {
        if (! r.row.contains (p)) continue;
        if (r.info.expanded (3).contains (p)) { showInfo (r.m->id); return; }
        // il modulo entra con un clic sul + o trascinandolo sulla catena (tenere premuto non basta)
        if (r.m->available() && ! inChain (r.m->id)) { libPress = r.m; libDragging = false; libDragPos = p; }
        return;
    }
    const auto [i, hit] = chainHit (p);
    if (i < 0) return;
    auto& s = engine.slot (i);
    switch (hit)
    {
        case Hit::led:    armOrToggle (s); break;
        case Hit::badge:  armOrToggle (s); select (&s); break;
        case Hit::left:   engine.moveSlot (i, -1); markDirty(); break;
        case Hit::right:  engine.moveSlot (i, +1); markDirty(); break;
        case Hit::remove: requestRemove (i); break;
        case Hit::card:   pressIndex = i; dragGrabDx = p.x - cardRect (i).getX(); break;   // clic o trascinamento
        case Hit::none:   break;
    }
    repaint();
}

void RigComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (libPress != nullptr)
    {
        if (! libDragging && e.getDistanceFromDragStart() < 5) return;
        libDragging = true;
        libDragPos = e.getPosition();
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
        return;
    }
    if (pressIndex < 0) return;
    if (dragIndex < 0 && e.getDistanceFromDragStart() < 5) return;
    dragIndex = pressIndex;
    dragX = e.x;
    // vicino ai bordi la catena scorre da sola
    const auto vp = cardsViewport();
    const int maxScroll = juce::jmax (0, chainContentW - vp.getWidth());
    if (e.x < vp.getX() + 30) chainScroll = juce::jmax (0, chainScroll - 8);
    else if (e.x > vp.getRight() - 30) chainScroll = juce::jmin (maxScroll, chainScroll + 8);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    repaint (chainArea());
}

void RigComponent::mouseUp (const juce::MouseEvent& e)
{
    if (libPress != nullptr)
    {
        const auto* m = libPress;
        const bool dragged = libDragging;
        libPress = nullptr; libDragging = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (dragged)
        {
            if (cardsViewport().contains (e.getPosition()) && ! inChain (m->id))
                if (auto* s = engine.addModule (m->id, gapAt (e.x))) { select (s); markDirty(); }
        }
        else if (e.mouseWasClicked())
            for (auto& r : libRows)
                if (r.m == m && r.add.expanded (3).contains (e.getPosition())) addFromLibrary (m->id);
        repaint();
        return;
    }
    if (dragIndex >= 0)
    {
        const int from = dragIndex;
        dragIndex = -1; pressIndex = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        dragCardForTest (from, gapAt (e.x));
        repaint();
        return;
    }
    if (pressIndex >= 0 && e.mouseWasClicked()) select (&engine.slot (pressIndex));
    pressIndex = -1;
    if (e.mouseWasClicked() && wsToggleArea().contains (e.getPosition()) && selected != nullptr) armOrToggle (*selected);
}

void RigComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (auto& r : libRows)
        if (r.row.contains (e.getPosition()) && ! r.info.expanded (3).contains (e.getPosition()) && ! r.add.expanded (3).contains (e.getPosition()))
            addFromLibrary (r.m->id);
}

void RigComponent::mouseMove (const juce::MouseEvent& e)
{
    hover = e.getPosition();
    bool hand = wsToggleArea().contains (hover) || chainHit (hover).first >= 0 || tunerArea().contains (hover);
    for (auto& r : libRows) if (r.row.contains (hover)) hand = true;
    setMouseCursor (hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint (libArea()); repaint (chainArea()); repaint (wsToggleArea()); repaint (tunerArea());
}

void RigComponent::mouseExit (const juce::MouseEvent&)
{
    hover = { -1, -1 };
    repaint();
}

void RigComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! cardsViewport().contains (e.getPosition())) return;
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    chainScroll = juce::jlimit (0, juce::jmax (0, chainContentW - cardsViewport().getWidth()), chainScroll - juce::roundToInt (d * 240.0f));
    repaint (chainArea());
}

// ---------------------------------------------------------------- timer

void RigComponent::checkCabs()
{
    // un IR caricato in b-blend che funziona: b-amp prima di lui, se è in safe mute, si riattiva da solo
    std::map<const Slot*, bool> now;
    for (int i = 0; i < engine.numSlots(); ++i)
    {
        auto& b = engine.slot (i);
        if (! b.info->hasCab || b.proc == nullptr) continue;
        const bool works = b.info->hasCab (*b.proc);
        now[&b] = works;
        const auto it = cabWorking.find (&b);
        if (works && it != cabWorking.end() && ! it->second)
            for (int k = 0; k < i; ++k)
                if (engine.slot (k).info->safeArm && engine.slot (k).proc != nullptr && ! engine.slot (k).enabled.load())
                    engine.setEnabled (k, true);   // fade-in di 80 ms come l'armo a mano
    }
    cabWorking = std::move (now);
}

void RigComponent::timerCallback()
{
    engine.collectGarbage();
    if (++cabTick % 6 == 0) checkCabs();   // 10 volte al secondo
    if (historyPending && juce::Time::getMillisecondCounterHiRes() - historyChangeMs > 400.0
        && ! juce::Desktop::getInstance().getMainMouseSource().isDragging())
        commitHistory();
    if (engine.tunerOn.load()) { tuner.update (engine); repaint (tunerArea()); }

    const float pBpm = controls.get ("bpm");
    if (std::abs (pBpm - lastBpmParam) > 0.01f) { engine.bpm = pBpm; lastBpmParam = pBpm; }
    engine.sigIndex = juce::roundToInt (controls.get ("sig"));

    // metronomo spento: tempo e tap si scuriscono
    const bool off = ! engine.metroOn.load();
    if (sigBox.dim != off || tapBtn.dim != off) { sigBox.dim = tapBtn.dim = off; sigBox.repaint(); tapBtn.repaint(); }
    if (! off) metroBtn.repaint();
    if (muteBtn.isVisible()) muteBtn.repaint();
    if (dm != nullptr) cpuShown += 0.1f * ((float) dm->getCpuUsage() * 100.0f - cpuShown);

    repaint (chainArea());
    repaint (wsArea().withHeight (kWsHeadH));
}

// ---------------------------------------------------------------- disegno

void RigComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (frame);
    g.fillRect (0, kTopH, getWidth(), 1);
    g.fillRect (kLibW, kTopH + 1, 1, getHeight() - kTopH - 1);
    g.fillRect (0, tunerArea().getBottom(), kLibW, 1);
    g.fillRect (kLibW + 1, chainArea().getBottom(), getWidth() - kLibW - 1, 1);

    // ---- 1. title bar
    drawTitle (g, "rig", { kMargin - 1, 10, 200, 36 }, kLogoSize);
    // nome del preset sulla stessa linea di base del testo dei pulsanti della barra
    drawLabel (g, dirty ? juce::String ("unsaved") : currentPreset,
               { presetsBtn.getRight() + 10, presetsBtn.getY(), textW (dirty ? juce::String ("unsaved") : currentPreset, kControlTextSize, Face::SansSemiBold) + 4, presetsBtn.getHeight() },
               dirty ? kAmber : info, kControlTextSize, juce::Justification::centredLeft, Face::SansSemiBold);

    // ---- 2. tuner (spento finché non lo premi)
    {
        const auto t = tunerArea();
        const bool on = engine.tunerOn.load();
        const bool have = on && tuner.active();
        const bool inTune = have && std::abs (tuner.shownCents) < 3.0f;
        const auto col = inTune ? kGreen : kAmber;
        const bool hov = t.contains (hover);
        if (! on)
        {
            // spento: diapason nel suo riquadro, "tuner" e "press to activate", tutto centrato nel riquadro
            if (hov) { g.setColour (kRowHover); g.fillRect (t.withTrimmedRight (1)); }
            const float textW = juce::jmax (juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 16.0f), "tuner"),
                                            juce::GlyphArrangement::getStringWidth (face (Face::SansMedium, kInfoSize), "press to activate"));
            const float groupW = 36.0f + 14.0f + textW;
            const auto box = juce::Rectangle<float> (std::round (((float) t.getWidth() - groupW) * 0.5f), (float) t.getCentreY() - 18.0f, 36.0f, 36.0f);
            g.setColour (hov ? kGreen : text); outline (g, box);
            const float cx = box.getCentreX(), cy = box.getCentreY();
            g.setColour (hov ? kGreen : selector);
            g.drawLine (cx - 4.0f, cy - 11.0f, cx - 4.0f, cy - 1.0f, 1.5f);
            g.drawLine (cx + 4.0f, cy - 11.0f, cx + 4.0f, cy - 1.0f, 1.5f);
            juce::Path u; u.addCentredArc (cx, cy - 1.0f, 4.0f, 4.0f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi + juce::MathConstants<float>::halfPi, true);
            g.strokePath (u, juce::PathStrokeType (1.5f));
            g.drawLine (cx, cy + 3.0f, cx, cy + 10.0f, 1.5f);
            g.fillRect (cx - 2.0f, cy + 10.0f, 4.0f, 2.0f);
            const float tx = box.getRight() + 14.0f;
            drawAtBaseline (g, "tuner", tx, (float) t.getCentreY() - 3.0f, hov ? valueText : titleName, 16.0f, juce::Justification::left, Face::SansSemiBold);
            drawAtBaseline (g, "press to activate", tx, (float) t.getCentreY() + 14.0f, hov ? labelHover : info, kInfoSize, juce::Justification::left, Face::SansMedium);
        }
        else
        {
            // acceso: a sinistra la nota e il mute; a destra la barra (-50...+50 cent) con le tacche, cents e Hz sotto
            const float base = (float) t.getY() + 33.0f;
            const juce::String note = have ? juce::String (kNoteNames[(tuner.midi % 12 + 12) % 12]) : juce::String ("-");
            drawAtBaseline (g, note, (float) kPad, base, have ? (inTune ? kGreen : valueText) : frame, 32.0f, juce::Justification::left, Face::SansSemiBold);
            if (have)
            {
                const float nw = juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 32.0f), note);
                drawAtBaseline (g, juce::String (tuner.midi / 12 - 1), (float) kPad + nw + 2.0f, base, info, 13.0f, juce::Justification::left, Face::MonoMedium);
            }
            const auto bar = juce::Rectangle<float> (82.0f, (float) t.getY() + 13.0f, 108.0f, 14.0f);
            g.setColour (frame); outline (g, bar);
            const auto in = bar.reduced (2.0f);
            if (have)
            {
                g.saveState();
                g.reduceClipRegion (in.toNearestInt());
                g.setColour (col.withAlpha (0.25f));
                const float sp = 12.0f, off = std::fmod (tuner.strobe, sp) + (tuner.strobe < 0 ? sp : 0.0f);
                for (float x = in.getX() - sp + off; x < in.getRight(); x += sp) g.fillRect (x, in.getY(), 5.0f, in.getHeight());
                g.restoreState();
                const float cx = in.getCentreX() + juce::jlimit (-1.0f, 1.0f, tuner.shownCents / 50.0f) * in.getWidth() * 0.5f;
                g.setColour (col);
                g.fillRect (std::round (cx) - 1.0f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f);
            }
            // tacche a 0, ±10 e ±25 cent sotto la barra
            for (float c : { -25.0f, -10.0f, 0.0f, 10.0f, 25.0f })
            {
                g.setColour (c == 0.0f ? text : frame);
                g.fillRect (in.getCentreX() + c / 50.0f * in.getWidth() * 0.5f - 0.5f, bar.getBottom() + 2.0f, 1.0f, c == 0.0f ? 5.0f : 3.0f);
            }
            const float sub = (float) t.getY() + 52.0f;
            if (have)
            {
                const int ct = juce::roundToInt (tuner.shownCents);
                drawAtBaseline (g, (ct > 0 ? "+" : ct < 0 ? "-" : "") + juce::String (std::abs (ct)) + " ct", bar.getX(), sub, inTune ? kGreen : valueText, kAxisSize, juce::Justification::left, Face::MonoMedium);
                drawAtBaseline (g, num (tuner.hz, 1) + " Hz", bar.getRight(), sub, info, kAxisSize, juce::Justification::right, Face::MonoRegular);
            }
            else drawAtBaseline (g, "play a string", bar.getCentreX(), sub, info, kAxisSize, juce::Justification::horizontallyCentred, Face::SansMedium);
            cross (g, tunerCloseArea().toFloat().getCentre(), 4.0f, tunerCloseArea().contains (hover) ? labelHover : selector);
        }
    }

    // ---- 2. libreria
    for (auto& [name, y] : libCats) drawTag (g, name, (float) kPad, (float) y + 16.0f);
    for (auto& r : libRows)
    {
        const bool av = r.m->available();
        if (r.row.contains (hover)) { g.setColour (kRowHover); g.fillRect (r.row.withTrimmedLeft (1).withTrimmedRight (1)); }
        g.setColour (av ? r.m->colour : r.m->colour.withAlpha (0.35f));
        g.fillRect (kPad, r.row.getCentreY() - 4, 8, 8);
        drawLabel (g, r.m->name, { kPad + 18, r.row.getY(), 120, kRowH }, av ? titleName : frame, 14.0f, juce::Justification::centredLeft, Face::SansMedium);
        if (! av)
        {
            const int nameEnd = kPad + 18 + textW (r.m->name, 14.0f), tagW = textW ("SOON", kTagSize, Face::SansSemiBold) + 2;
            if (! r.row.contains (hover) && nameEnd + 8 < r.info.getX() - 8 - tagW)   // solo se c'è spazio (b-freshstrings è lungo)
                drawTag (g, "soon", (float) r.info.getX() - 8.0f, (float) r.row.getCentreY() + 4.0f, juce::Justification::right);
        }
        auto btn = [&] (juce::Rectangle<int> b, bool plus, bool enabled)
        {
            const bool h = enabled && b.expanded (3).contains (hover);
            g.setColour (! enabled ? kOff : h ? labelHover : text);
            outline (g, b.toFloat());
            if (plus)
            {
                const auto c = b.toFloat().getCentre();
                g.fillRect (c.x - 4.0f, c.y - 0.5f, 8.0f, 1.0f);
                g.fillRect (c.x - 0.5f, c.y - 4.0f, 1.0f, 8.0f);
            }
            else drawLabel (g, "?", b, h ? labelHover : selector, 13.0f, juce::Justification::centred, Face::SansSemiBold);
        };
        if (r.row.contains (hover) && ! libDragging) btn (r.info, false, true);   // il ? compare solo passando sopra la riga
        const bool there = inChain (r.m->id);
        btn (r.add, true, av && ! there);   // già nella catena: il + si scurisce (uno per tipo)
        if (there && r.row.contains (hover) && ! libDragging)   // e passandoci sopra si capisce perché
        {
            const int nameEnd = kPad + 18 + textW (r.m->name, 14.0f), tagW = textW ("IN CHAIN", kTagSize, Face::SansSemiBold) + 2;
            if (nameEnd + 8 < r.info.getX() - 8 - tagW)
                drawTag (g, "in chain", (float) r.info.getX() - 8.0f, (float) r.row.getCentreY() + 4.0f, juce::Justification::right);
        }
    }

    // ---- 3. catena
    {
        const auto c = chainArea();
        const auto vp = cardsViewport();
        const float cy = (float) c.getCentreY();
        drawTag (g, "di in", (float) c.getX() + kPad, cy + 4.0f);
        drawTag (g, "stereo out", (float) c.getRight() - kPad, cy + 4.0f, juce::Justification::right);
        g.setColour (frame);
        g.fillRect ((float) vp.getX() - 6.0f, cy, (float) vp.getWidth() + 12.0f, 1.0f);

        auto drawCard = [&] (int i, juce::Rectangle<int> r, bool ghost)
        {
            auto& s = engine.slot (i);
            const auto rf = r.toFloat();
            const bool av = s.proc != nullptr, on = av && s.enabled.load(), sel = &s == selected;
            const bool hov = ! ghost && dragIndex < 0 && r.contains (hover);
            g.setColour (background); g.fillRect (rf);
            if (sel) { g.setColour (s.info->colour.withAlpha (0.08f)); g.fillRect (rf); }
            g.setColour (ghost ? labelHover : sel ? s.info->colour : hov ? labelHover : frame);
            outline (g, rf);
            const auto led = juce::Rectangle<float> (rf.getX() + 9.0f, rf.getY() + 9.0f, 10.0f, 10.0f);
            if (on) { g.setColour (s.info->colour); g.fillRect (led); }
            else { g.setColour (juce::Colour (0xff262626)); g.fillRect (led); g.setColour (av ? text : juce::Colour (0xff3a3a3a)); outline (g, led); }
            drawLabel (g, s.info->name, { r.getX() + 26, r.getY() + 4, kCardW - 26 - 24, 20 }, av ? titleName : frame, 13.0f, juce::Justification::centredLeft, Face::SansMedium);
            auto icon = [&] (juce::Rectangle<int> b) { return ! ghost && b.contains (hover) ? labelHover : selector; };
            const juce::Rectangle<int> bx (r.getRight() - 22, r.getY() + 4, 18, 20);
            cross (g, bx.toFloat().getCentre(), 3.5f, icon (bx));
            const juce::Rectangle<int> st (r.getX() + 9, r.getY() + 30, kCardW - 18, 14);
            if (s.info->safeArm && av && ! on)
            {
                g.setColour (kAmber); g.fillRect (st);
                drawLabel (g, "SAFE MUTED - Click to Arm", st, onAccent, 10.0f, juce::Justification::centred, Face::SansSemiBold);
                return;
            }
            if (! av) drawLabel (g, "coming soon", st, frame, 11.0f, juce::Justification::centredLeft, Face::SansMedium);
            else drawLabel (g, ! on ? juce::String ("off") : cabProtects (engine, i) ? juce::String ("on - locked") : juce::String ("on"), st, on ? s.info->colour : info, 11.0f, juce::Justification::centredLeft, Face::SansSemiBold);
            const juce::Rectangle<int> bl (r.getRight() - 46, r.getY() + 26, 21, 22), br (r.getRight() - 25, r.getY() + 26, 21, 22);
            chevron (g, bl.toFloat(), -1, i > 0 ? icon (bl) : kOff);
            chevron (g, br.toFloat(), +1, i < engine.numSlots() - 1 ? icon (br) : kOff);
        };

        g.saveState();
        g.reduceClipRegion (vp);
        for (int i = 0; i < engine.numSlots(); ++i)
        {
            const auto r = cardRect (i);
            if (! r.intersects (vp)) continue;
            if (i == dragIndex) { g.setColour (background); g.fillRect (r); g.setColour (kOff); outline (g, r.toFloat()); continue; }
            drawCard (i, r, false);
        }
        if (dragIndex >= 0)
        {
            // punto di inserimento e card che segue il mouse
            const int gap = gapAt (dragX);
            const int gx = vp.getX() + gap * (kCardW + kConn) + kConn / 2 - chainScroll;
            g.setColour (engine.slot (dragIndex).info->colour);
            g.fillRect (gx - 1, cardRect (0).getY() - 4, 2, kCardH + 8);
            drawCard (dragIndex, cardRect (dragIndex).withX (dragX - dragGrabDx).translated (0, -3), true);
        }
        g.restoreState();
        if (chainContentW > vp.getWidth() && vp.getWidth() > 0)
        {
            const float tw = (float) vp.getWidth(), frac = tw / (float) chainContentW;
            const float bx = (float) vp.getX() + (float) chainScroll / (float) chainContentW * tw;
            g.setColour (juce::Colour (0xff262626)); g.fillRect ((float) vp.getX(), (float) c.getBottom() - 5.0f, tw, 2.0f);
            g.setColour (text); g.fillRect (bx, (float) c.getBottom() - 5.0f, tw * frac, 2.0f);
        }
    }

    // ---- 4. workspace
    {
        const auto ws = wsArea();
        const int hy = ws.getY();
        if (selected != nullptr)
        {
            const auto* m = selected->info;
            const int idx = engine.indexOf (selected);
            g.setColour (m->colour); g.fillRect (ws.getX() + kPad, hy + kWsHeadH / 2 - 5, 10, 10);
            drawLabel (g, m->name, { ws.getX() + kPad + 18, hy, 200, kWsHeadH }, titleName, 16.0f, juce::Justification::centredLeft, Face::SansSemiBold);
            const float nw = juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 16.0f), m->name);
            drawTag (g, categoryName (m->category) + "   slot " + juce::String (idx + 1) + " of " + juce::String (engine.numSlots()),
                     (float) ws.getX() + kPad + 18.0f + nw + 14.0f, baselineFor ((float) hy + kWsHeadH * 0.5f, kTagSize, Face::SansSemiBold));
            if (dm != nullptr)
                drawAtBaseline (g, "cpu " + num (cpuShown, 1) + " %", (float) wsToggleArea().getX() - 16.0f,
                                baselineFor ((float) hy + kWsHeadH * 0.5f, kAxisSize, Face::MonoRegular), info, kAxisSize, juce::Justification::right);
            const auto t = wsToggleArea().toFloat();
            const bool av = selected->proc != nullptr, on = av && selected->enabled.load();
            const bool hov = wsToggleArea().contains (hover);
            if (! av) { g.setColour (frame); outline (g, t); drawLabel (g, "SOON", wsToggleArea(), frame, 14.0f, juce::Justification::centred, Face::MonoSemiBold); }
            else if (on && cabProtects (engine, idx))
            {
                // b-blend dopo un ampli: non si spegne (si può solo togliere o spostare)
                g.setColour (m->colour); g.fillRect (t);
                drawLabel (g, "ON", wsToggleArea(), onAccent, 15.0f, juce::Justification::centred, Face::MonoSemiBold);
                drawAtBaseline (g, "locked: amp before it", t.getX() - 16.0f - (dm != nullptr ? 90.0f : 0.0f),
                                baselineFor (t.getCentreY(), kInfoSize), info, kInfoSize, juce::Justification::right, Face::SansMedium);
            }
            else if (on) { g.setColour (m->colour); g.fillRect (t); drawLabel (g, "ON", wsToggleArea(), onAccent, 15.0f, juce::Justification::centred, Face::MonoSemiBold); }
            else if (m->safeArm) { g.setColour (kAmber); outline (g, t); drawLabel (g, "ARM", wsToggleArea(), kAmber, 15.0f, juce::Justification::centred, Face::MonoSemiBold); }
            else { g.setColour (hov ? labelHover : text); outline (g, t); drawLabel (g, "OFF", wsToggleArea(), hov ? labelHover : selector, 15.0f, juce::Justification::centred, Face::MonoSemiBold); }

            if (editor != nullptr)
            {
                const auto eb = editor->getBoundsInParent().toFloat();
                g.setColour (on ? m->colour.withAlpha (0.5f) : frame);
                outline (g, eb.expanded (1.0f));
            }
            else
            {
                const auto body = ws.withTrimmedTop (kWsHeadH);
                drawLabel (g, m->name + ": coming soon", body.withSizeKeepingCentre (400, 24), selector, 17.0f, juce::Justification::centred, Face::SansMedium);
            }
        }
        else
            drawLabel (g, "add a module from the library", ws.withSizeKeepingCentre (400, 24), info, 15.0f, juce::Justification::centred);
    }
}
}

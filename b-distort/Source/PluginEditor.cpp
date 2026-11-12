#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 900, kHeight = 390, kMargin = 24;
    constexpr float kTitleSize = 30.0f;

    int textWidth (const juce::String& s, float size = ui::kLabelSize)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (ui::font (size), s));
    }
}

// ============================================================================ TransferDisplay
juce::Rectangle<int> TransferDisplay::plotArea() const
{
    return getLocalBounds().withTrimmedLeft (kLeftMargin).withTrimmedBottom (kBottomMargin)
                           .withTrimmedTop (kTopMargin).reduced (2, 0);
}

float TransferDisplay::getScaleLeft() const
{
    float left = (float) (kLeftMargin - 8);
    for (const char* s : { "+1", "-1" })   // la più larga delle etichette decide il bordo
    {
        const float advance = juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::MonoRegular, ui::kAxisSize), s);
        left = juce::jmin (left, (float) (kLeftMargin - 8) - advance + ui::makeInkText (s, ui::kAxisSize, ui::Face::MonoRegular).left);
    }
    return left;
}

void TransferDisplay::paint (juce::Graphics& g)
{
    const auto a = plotArea();
    const auto in = a.toFloat().reduced ((float) kInner);
    auto xFor = [&] (float v) { return in.getX() + (v + 1.0f) * 0.5f * in.getWidth(); };
    // curva di trasferimento reale (stessa formula del motore audio, senza il tono), campionata una volta
    const int nPx = juce::jmax (2, (int) in.getWidth());
    std::vector<float> ys ((size_t) nPx + 1);
    float yPeak = 0.0f;
    for (int px = 0; px <= nPx; ++px)
    {
        ys[(size_t) px] = proc.transfer (-1.0f + 2.0f * (float) px / (float) nPx);
        yPeak = juce::jmax (yPeak, std::abs (ys[(size_t) px]));
    }
    // asse Y a scatti: ±1 finché la curva ci sta, poi ±5, poi ±12 (con un po' di isteresi per non sfarfallare)
    const float lim = scaleY < 1.5f ? 1.02f : (scaleY < 6.0f ? 5.1f : 1.0e9f);
    const float down = scaleY > 6.0f ? 4.6f : (scaleY > 1.5f ? 0.95f : -1.0f);
    if (yPeak > lim || yPeak < down) scaleY = yPeak <= 1.02f ? 1.0f : (yPeak <= 5.1f ? 5.0f : 12.0f);
    const float S = scaleY;
    auto yFor = [&] (float v) { return in.getCentreY() - v / S * 0.5f * in.getHeight(); };

    // griglia: linee continue da 1 px, molto scure; con la scala allargata anche le linee di ±1 (riferimento)
    const float x0 = std::round (xFor (-1.0f)), x1 = std::round (xFor (1.0f)) + 1.0f;
    const float y0 = std::round (yFor (S)), y1 = std::round (yFor (-S)) + 1.0f;
    for (const float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        g.setColour (juce::exactlyEqual (v, 0.0f) ? ui::gridZero : ui::gridLine);
        g.fillRect (x0, std::round (yFor (v * S)), x1 - x0, ui::kLine);
        g.fillRect (std::round (xFor (v)), y0, ui::kLine, y1 - y0);
    }
    const juce::String top = "+" + juce::String ((int) S), bottom = "-" + juce::String ((int) S);
    for (const auto& [v, t] : { std::pair<float, juce::String> { S, top }, { 0.0f, "0" }, { -S, bottom } })
    {
        const int y = juce::roundToInt (yFor (v));
        ui::drawLabel (g, t, { 0, y - 8, kLeftMargin - 8, 16 }, S > 1.5f && ! juce::exactlyEqual (v, 0.0f) ? ui::accent : ui::info,
                       ui::kAxisSize, juce::Justification::centredRight, ui::Face::MonoRegular);
    }
    for (const auto& [v, t] : { std::pair<float, const char*> { 1.0f, "+1" }, { 0.0f, "0" }, { -1.0f, "-1" } })
        ui::drawAtBaseline (g, t, xFor (v), (float) getHeight() - getXLabelBaselineFromBottom(), ui::info, ui::kAxisSize,
                            juce::Justification::horizontallyCentred);
    if (S > 1.5f)
        for (const float v : { -1.0f, 1.0f })
        {
            g.setColour (ui::gridZero);
            for (float x = x0; x < x1; x += 6.0f) g.fillRect (x, std::round (yFor (v)), 3.0f, ui::kLine);   // ±1 tratteggiate
            ui::drawLabel (g, v > 0 ? "+1" : "-1", { 0, juce::roundToInt (yFor (v)) - 8, kLeftMargin - 8, 16 }, ui::info, ui::kAxisSmall,
                           juce::Justification::centredRight, ui::Face::MonoRegular);
        }
    // nomi degli assi, a 6 px dalle linee della griglia
    const float cap = ui::capHeight (ui::kTagSize, ui::Face::SansSemiBold);
    ui::drawTag (g, "out", xFor (-1.0f) + 6.0f, yFor (S) + 6.0f + cap);
    ui::drawTag (g, "in", xFor (1.0f) - 6.0f, yFor (-S) - 6.0f, juce::Justification::right);

    // riferimento: nessuna saturazione (diagonale)
    g.setColour (ui::gridZero);
    g.drawLine (xFor (-1.0f), yFor (-1.0f), xFor (1.0f), yFor (1.0f), 1.0f);

    juce::Path curve;
    for (int px = 0; px <= nPx; ++px)
    {
        const float x = -1.0f + 2.0f * (float) px / (float) nPx;
        if (px == 0) curve.startNewSubPath (xFor (x), yFor (ys[(size_t) px]));
        else         curve.lineTo (xFor (x), yFor (ys[(size_t) px]));
    }
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (a);
    g.setColour (ui::accent);
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    // punto luminoso: dove sta adesso il picco dell'ingresso sulla curva (alone tenue + punto pieno)
    const float px = juce::jlimit (0.0f, 1.0f, proc.inputPeak.load());
    if (px > 0.001f)
    {
        const juce::Point<float> c (xFor (px), yFor (proc.transfer (px)));
        g.setColour (ui::accent.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
        g.setColour (ui::accent.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
    }
}

// ============================================================================ SpectrumDisplay
juce::Rectangle<float> SpectrumDisplay::grid() const
{
    // stessa quota della griglia della curva (margini sopra/sotto e rientro identici)
    return getLocalBounds().withTrimmedTop (kTopMargin + kInner).withTrimmedBottom (kBottomMargin + kInner)
                           .reduced (kSide, 0).toFloat();
}

float SpectrumDisplay::xForFreq (double hz) const
{
    const auto a = grid();
    return a.getX() + (a.getWidth() - 1.0f) * (float) (std::log (hz / 20.0) / std::log (1000.0));
}

double SpectrumDisplay::freqForX (float x) const
{
    const auto a = grid();
    return 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / (a.getWidth() - 1.0f)));
}

void SpectrumDisplay::update (float fps)
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const bool a = inSpec.update (proc.inTap, sr, fps, -84.0f);
    const bool b = outSpec.update (proc.outTap, sr, fps, -84.0f);
    if (a || b || visible) repaint();   // in silenzio, a spettro sparito, nessun ridisegno
    visible = a || b;
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto a = grid();
    const float x0 = std::round (a.getX()), x1 = std::round (a.getX() + a.getWidth() - 1.0f) + 1.0f;
    const float y0 = std::round (a.getY()), y1 = std::round (a.getBottom()) + 1.0f;

    // griglia chiusa: 5 righe (0, -21, -42, -63, -84 dB) come la curva, verticali sulle frequenze
    g.setColour (ui::gridLine);
    for (int i = 0; i <= 4; ++i)
        g.fillRect (x0, std::round (a.getY() + a.getHeight() * (float) i / 4.0f), x1 - x0, ui::kLine);
    struct Mark { double hz; const char* text; float size; };
    const Mark marks[] = { { 20, "20", ui::kAxisSize }, { 50, "50", ui::kAxisSmall }, { 100, "100", ui::kAxisSize },
                           { 200, "200", ui::kAxisSmall }, { 500, "500", ui::kAxisSmall }, { 1000, "1k", ui::kAxisSize },
                           { 2000, "2k", ui::kAxisSmall }, { 5000, "5k", ui::kAxisSmall }, { 10000, "10k", ui::kAxisSize },
                           { 20000, "20k", ui::kAxisSize } };
    const float baseline = (float) getHeight() - ((float) (kBottomMargin - 12) - ui::capHeight (ui::kAxisSize, ui::Face::MonoRegular) * 0.5f);
    for (const auto& m : marks)
    {
        const float x = std::round (xForFreq (m.hz));
        g.setColour (ui::gridLine);
        g.fillRect (x, y0, ui::kLine, y1 - y0);
        ui::drawAtBaseline (g, m.text, x, baseline, ui::info, m.size, juce::Justification::horizontallyCentred);
    }

    // legenda: IN grigio, OUT nel colore di accento (stile delle etichette interne ai grafici)
    const float cap = ui::capHeight (ui::kTagSize, ui::Face::SansSemiBold);
    const float tagY = a.getY() + 6.0f + cap;
    ui::drawAtBaseline (g, "IN", a.getX() + 6.0f, tagY, ui::info, ui::kTagSize, juce::Justification::left, ui::Face::SansSemiBold, 0.08f);
    const float inW = juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::SansSemiBold, ui::kTagSize, 0.08f), "IN");
    ui::drawAtBaseline (g, "OUT", a.getX() + 6.0f + inW + 8.0f, tagY, ui::accent, ui::kTagSize, juce::Justification::left, ui::Face::SansSemiBold, 0.08f);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1).toNearestInt());
    auto f = [this] (float x) { return freqForX (x); };
    const auto area = juce::Rectangle<float>::leftTopRightBottom (x0, a.getY(), x1, a.getBottom());
    if (visible)
    {
        inSpec.draw (g, area, f, ui::text, 0.0f, -84.0f, 0.08f, 0.45f);
        outSpec.draw (g, area, f, ui::accent, 0.0f, -84.0f, 0.10f, 0.55f);
    }

    // risposta dei filtri d'uscita (tono, low cut), sempre visibile: 0 dB sulla riga in alto,
    // -24 dB su quella in basso (scala propria, 6 dB per riga)
    juce::Path resp;
    for (float x = x0; x <= x1; x += 1.0f)
    {
        const float db = juce::jlimit (-30.0f, 0.0f, proc.responseDb (freqForX (x)));
        const float y = a.getY() + (-db / 24.0f) * a.getHeight();
        if (x <= x0) resp.startNewSubPath (x, y); else resp.lineTo (x, y);
    }
    g.setColour (ui::accent);
    g.strokePath (resp, juce::PathStrokeType (1.5f));
}

// ============================================================================ BDistortEditor
BDistortEditor::BDistortEditor (BDistortProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), spectrum (p),
      typeChoice (*p.apvts.getParameter (BDistortIDs::type), { "tube", "drive", "rat", "fuzz", "fold" })
{
    addAndMakeVisible (typeChoice);
    auto& s = p.apvts;
    driveBox.setParameter (s.getParameter (BDistortIDs::drive));
    driveBox.format = [] (float v) { return ui::num (v, 1) + " dB"; };
    driveBox.step = [] (float v, int dir) { return std::round (v) + (float) dir; };
    driveBox.drag = [] (float start, float, float dx) { return std::round ((start + dx / 6.0f) * 10.0f) / 10.0f; };

    // tone: bilanciamento basse / alte (50 % = piatto); hicut: il vecchio "tone", passa-basso sul distorto
    tiltBox.setParameter (s.getParameter (BDistortIDs::tilt));
    tiltBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    tiltBox.step = [] (float v, int dir) { return std::round (v) + 5.0f * (float) dir; };
    tiltBox.drag = [] (float start, float, float dx) { return std::round (start + dx / 2.0f); };
    toneBox.setParameter (s.getParameter (BDistortIDs::tone));
    toneBox.format = [] (float v) { return ui::freqText (v); };
    toneBox.step = [] (float v, int dir) { return v * std::pow (2.0f, (float) dir / 6.0f); };
    toneBox.drag = [this] (float, float startNorm, float dx)
    {
        auto* prm = proc.apvts.getParameter (BDistortIDs::tone);
        return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.003f));
    };

    locutBox.setParameter (s.getParameter (BDistortIDs::locut));
    locutBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; };
    locutBox.step = [] (float v, int dir)
    {
        if (v < 20.0f) return dir > 0 ? 20.0f : 0.0f;                    // da 0 (spento) si parte da 20 Hz
        const float n = v * std::pow (2.0f, (float) dir / 6.0f);
        return n < 20.0f ? 0.0f : std::round (n);
    };
    locutBox.drag = [this] (float, float startNorm, float dx)
    {
        auto* prm = proc.apvts.getParameter (BDistortIDs::locut);
        return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.003f));
    };

    // memoria per tipo: drive e tono restano a ciascun tipo; doppio clic sul tipo = suoi default
    typeChoice.onUserSwitch = [this] (int from, int to) { proc.typeMemory.switchPreset (from, to); };
    typeChoice.onDoubleClick = [this] (int t) { proc.typeMemory.resetPreset (t); };
    driveBox.defaultValue = [this] { return proc.typeMemory.defaultFor (proc.apvts.getParameter (BDistortIDs::drive), 0); };
    toneBox.defaultValue = [this] { return proc.typeMemory.defaultFor (proc.apvts.getParameter (BDistortIDs::tone), 0); };
    tiltBox.defaultValue = [this] { return proc.typeMemory.defaultFor (proc.apvts.getParameter (BDistortIDs::tilt), 0); };

    mixBox.setParameter (s.getParameter (BDistortIDs::mix));
    mixBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    mixBox.step = [] (float v, int dir) { return std::round (v) + 5.0f * (float) dir; };
    mixBox.drag = [] (float start, float, float dx) { return std::round (start + dx / 2.0f); };

    outputBox.setParameter (s.getParameter (BDistortIDs::output));
    outputBox.format = [] (float v) { return ui::num (v, 1) + " dB"; };
    outputBox.step = [] (float v, int dir) { return std::round (v) + (float) dir; };
    outputBox.drag = [] (float start, float, float dx) { return std::round ((start + dx / 6.0f) * 10.0f) / 10.0f; };

    // misura fissa: ogni riquadro è largo quanto il valore più lungo possibile
    driveBox.sizeSamples  = { "36.0 dB", "88.8 dB" };
    tiltBox.sizeSamples   = { "100 %" };
    toneBox.sizeSamples   = { "20.0 kHz", "8.88 kHz", "888 Hz" };
    locutBox.sizeSamples  = { "800 Hz", "888 Hz" };
    mixBox.sizeSamples    = { "100 %" };
    outputBox.sizeSamples = { "-24.0 dB", "-88.8 dB" };
    for (auto* vb : { &driveBox, &tiltBox, &toneBox, &locutBox, &mixBox, &outputBox })
    {
        vb->onWidthChanged = [this] { resized(); };
        addAndMakeVisible (vb);
    }
    addAndMakeVisible (display);
    addAndMakeVisible (spectrum);
    setSize (kWidth, kHeight);
    proc.spectrumActive.store (true);   // il motore copia i campioni per lo spettro solo a interfaccia aperta
    startTimerHz (30);
}

BDistortEditor::~BDistortEditor()
{
    stopTimer();
    proc.spectrumActive.store (false);
}

juce::Rectangle<int> BDistortEditor::box() const { return { kMargin, 66, kWidth - 2 * kMargin, kHeight - 66 - kMargin }; }

// padding interno del riquadro: distanza tra la sua linea e il bordo della scala, uguale sui 4 lati
int BDistortEditor::framePad() const { return ui::kFramePad; }

void BDistortEditor::resized()
{
    using namespace ui;
    labels.clear();
    const auto b = box();
    const int pad = framePad();
    const int left = b.getX() + 1 + pad;
    const int graphRight = b.getRight() - 1 - pad;

    auto top = juce::Rectangle<int> (left, b.getY() + 1 + pad, graphRight - left, kControlH);
    const int dispBottom = juce::roundToInt ((float) (b.getBottom() - 1 - pad) + display.getXLabelBaselineFromBottom());
    const int dispX = left - juce::roundToInt (display.getScaleLeft());   // il bordo della scala cade esattamente a "left"
    // curva quadrata a sinistra (griglia larga quanto alta), spettro a destra fino al bordo della griglia
    const int dispTop = top.getBottom() + 6;
    const int dispH = dispBottom - dispTop;
    display.setBounds (dispX, dispTop, dispH + 6, dispH);   // larghezza = altezza + 6: griglia quadrata
    const int curveGridRight = dispX + dispH + 6 - 2 - TransferDisplay::kInner;
    spectrum.setBounds (juce::Rectangle<int>::leftTopRightBottom (curveGridRight + 2 * kGapGroup - SpectrumDisplay::kSide, dispTop,
                                                                  graphRight + 1 + SpectrumDisplay::kSide, dispBottom));

    auto add = [&] (const juce::String& name, ValueBox& vb, int gapAfter)
    {
        labels.push_back ({ name, top.removeFromLeft (textWidth (name)) });
        top.removeFromLeft (kGapLabel);
        vb.setBounds (top.removeFromLeft (vb.getIdealWidth()));
        top.removeFromLeft (gapAfter);
    };
    add ("drive", driveBox, kGapGroup);
    add ("tone", tiltBox, kGapGroup);
    add ("locut", locutBox, kGapGroup);
    add ("hicut", toneBox, kGapGroup);
    add ("mix", mixBox, kGapGroup);
    add ("output", outputBox, kGapGroup);
    modeArea = top;   // configurazione (mono/stereo) a destra nella riga dei controlli
    graphRightEdge = graphRight;

    // tipo di distorsione nell'header, sulla linea di base del titolo, fino al bordo della griglia
    auto head = juce::Rectangle<int> (left, headerRowY(), graphRight - left, kControlH);
    typeChoice.setBounds (head.removeFromRight (typeChoice.getIdealWidth()));
    head.removeFromRight (kGapLabel);
    labels.push_back ({ "type", head.removeFromRight (textWidth ("type")) });
    ui::centreHeaderInRig (getWidth(), { &typeChoice }, { &labels.back().area });
}

void BDistortEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);

    // header: "b" rosso + "-distort" grigio; la configurazione sulla stessa linea di base, sul bordo della griglia
    const juce::Rectangle<int> r (kMargin - 1, 16, 300, 36);   // centro y = 34
    ui::drawTitle (g, "distort", r, kTitleSize);
    const auto mode = getAudioProcessor()->getTotalNumInputChannels() == 1 ? "mono" : "stereo";
    ui::drawLabel (g, mode, modeArea, ui::info, ui::kInfoSize, juce::Justification::centredRight, ui::Face::SansRegular);

    g.setColour (ui::frame);
    ui::outline (g, box().toFloat());
    for (const auto& l : labels) ui::drawLabel (g, l.text, l.area, ui::labelStatic, ui::kLabelSize);
}

void BDistortEditor::timerCallback()
{
    spectrum.update (30.0f);
    if (const float pk = proc.inputPeak.load(); std::abs (pk - shownPeak) > 0.002f) { shownPeak = pk; display.repaint(); }
    juce::int64 sig = 17;
    for (auto* p : proc.getParameters()) sig = sig * 31 + (juce::int64) (p->getValue() * 1.0e6f);
    if (sig == lastSignature) return;
    lastSignature = sig;
    repaint();
    display.repaint();
    for (auto* vb : { &driveBox, &tiltBox, &toneBox, &locutBox, &mixBox, &outputBox }) vb->repaint();
    spectrum.repaint();
    typeChoice.repaint();
}

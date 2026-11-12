#include "PluginEditor.h"

using namespace ui;
namespace
{
    int tw (const juce::String& s) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (kLabelSize), s)); }
    void grid (juce::Graphics& g, juce::Rectangle<float> a) { g.setColour (gridLine); outline (g, a); }
    const juce::Colour kAmber { 0xffffb020 }, kRed { 0xffff4d4d };
}

// ============================================================================ dati dal FIFO
void MeterData::pull (BMeterProcessor& p)
{
    int s1, n1, s2, n2;
    const int ready = p.fifo.getNumReady();
    p.fifo.prepareToRead (ready, s1, n1, s2, n2);
    const int binLen = (int) (0.01 * (p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0));
    auto take = [&] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const float l = p.fifoBuf.getSample (0, start + i), r = p.fifoBuf.getSample (1, start + i);
            roll[0][(size_t) w] = l; roll[1][(size_t) w] = r; w = (w + 1) & (kRoll - 1);
            acc.pkL = juce::jmax (acc.pkL, std::abs (l)); acc.pkR = juce::jmax (acc.pkR, std::abs (r));
            accL += (double) l * l; accR += (double) r * r;
            if (++binN >= binLen)
            {
                acc.rmsL = (float) std::sqrt (accL / binN); acc.rmsR = (float) std::sqrt (accR / binN);
                bins[(size_t) binPos] = acc; binPos = (binPos + 1) % (int) bins.size();
                acc = {}; accL = accR = 0; binN = 0;
            }
        }
    };
    take (s1, n1); take (s2, n2);
    p.fifo.finishedRead (n1 + n2);
    fresh = n1 + n2;
}

// ============================================================================ auto
void AutoButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (proc.isAutoListening())
    {
        g.setColour (accent); g.fillRect (r);
        drawLabel (g, num (proc.autoRemaining(), 1) + " s", getLocalBounds(), onAccent, kControlTextSize, juce::Justification::centred, Face::MonoSemiBold);
    }
    else
    {
        g.setColour (isMouseOver() ? labelHover : text); outline (g, r);
        drawLabel (g, "auto", getLocalBounds(), selector, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    }
}

// ============================================================================ vista
MeterView::MeterView (BMeterProcessor& p)
    : proc (p),
      waveMode (*p.apvts.getParameter (BMeterIDs::waveMode), { "peak+rms", "l/r" }),
      specSpeed (*p.apvts.getParameter (BMeterIDs::specSpeed), { "fast", "med", "slow" }),
      specTilt (*p.apvts.getParameter (BMeterIDs::specTilt), { "flat", "3dB", "4.5dB" }),
      autoBtn (p)
{
    auto setup = [&] (ValueBox& vb, const char* id, std::function<juce::String (float)> f, float perPx)
    {
        auto* prm = p.apvts.getParameter (id);
        vb.setParameter (prm); vb.format = std::move (f);
        vb.step = [prm] (float, int d) { return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, prm->getValue() + 0.02f * (float) d)); };
        vb.drag = [prm, perPx] (float, float s, float dx) { return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * perPx)); };
        addChildComponent (vb);
    };
    setup (oscTime, BMeterIDs::oscTime, [] (float v) { return num (v, 1) + " ms"; }, 0.003f);
    setup (oscGain, BMeterIDs::oscGain, [] (float v) { return num (v, 1) + " dB"; }, 0.003f);
    setup (vecPersist, BMeterIDs::vecPersist, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, 0.003f);
    setup (vecScale, BMeterIDs::vecScale, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }, 0.003f);
    setup (waveWindow, BMeterIDs::waveWindow, [] (float v) { return num (v, 1) + " s"; }, 0.003f);
    setup (gainBox, BMeterIDs::gain, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; }, 0.002f);
    gainBox.setVisible (true);
    for (auto* c : { (juce::Component*) &waveMode, (juce::Component*) &specSpeed, (juce::Component*) &specTilt }) addChildComponent (c);
    addAndMakeVisible (autoBtn);
    for (int i = 0; i < kN; ++i)   // Blackman-Harris
    {
        const double x = juce::MathConstants<double>::twoPi * i / (double) (kN - 1);
        window[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
    }
    bands.fill (-100.0f);
}

juce::Rectangle<float> MeterView::graphArea() const
{
    const float leftW = std::round ((float) getWidth() * 0.75f) - (float) kGapGroup;
    return { 34.0f, (float) (kControlH + kGapInner), leftW - 34.0f, (float) getHeight() - (float) (kControlH + kGapInner) - 20.0f };
}

void MeterView::resized()
{
    labels.clear();
    const int v = view();
    for (auto* c : { (juce::Component*) &oscTime, (juce::Component*) &oscGain, (juce::Component*) &vecPersist, (juce::Component*) &vecScale,
                     (juce::Component*) &waveWindow, (juce::Component*) &waveMode, (juce::Component*) &specSpeed, (juce::Component*) &specTilt })
        c->setVisible (false);
    // i due parametri della vista attiva, in alto a sinistra
    struct P { const char* label; juce::Component* c; int w; };
    std::array<P, 2> ctl {};
    switch (v)
    {
        case 0:  ctl = { P { "time", &oscTime, oscTime.getIdealWidth() }, P { "gain", &oscGain, oscGain.getIdealWidth() } }; break;
        case 1:  ctl = { P { "persist", &vecPersist, vecPersist.getIdealWidth() }, P { "scale", &vecScale, vecScale.getIdealWidth() } }; break;
        case 2:  ctl = { P { "window", &waveWindow, waveWindow.getIdealWidth() }, P { "mode", &waveMode, waveMode.getIdealWidth() } }; break;
        default: ctl = { P { "speed", &specSpeed, specSpeed.getIdealWidth() }, P { "tilt", &specTilt, specTilt.getIdealWidth() } }; break;
    }
    auto row = juce::Rectangle<int> (0, 0, getWidth(), kControlH);
    for (auto& c : ctl)
    {
        labels.push_back ({ c.label, row.removeFromLeft (tw (c.label)) });
        row.removeFromLeft (kGapLabel);
        c.c->setBounds (row.removeFromLeft (c.w));
        c.c->setVisible (true);
        row.removeFromLeft (kGapGroup);
    }
    // colonna loudness: gain e auto in fondo
    const int x0 = (int) std::round ((float) getWidth() * 0.75f);
    auto bottom = juce::Rectangle<int> (x0, getHeight() - kControlH - 34, getWidth() - x0, kControlH);
    gainBox.setBounds (bottom.removeFromLeft (gainBox.getIdealWidth()));
    bottom.removeFromLeft (kGapInner);
    autoBtn.setBounds (bottom.removeFromLeft (54));
    lastView = v;
    staticLayer.invalidate();
}

// pesi della lisciatura: per ogni punto (log da 20 Hz a 20 kHz) una gaussiana in ottave (FWHM 1/3 d'ottava) sui bin;
// dove i bin sono più radi della finestra (bassi) si interpola linearmente tra i due bin vicini.
void MeterView::buildSpecWeights (double fs)
{
    weightsFs = fs;
    wStart.assign (kPts, 0); wCount.assign (kPts, 0); wBin.clear(); wVal.clear();
    const double binHz = fs / kN, sigma = (1.0 / 3.0) / 2.3548;
    for (int p = 0; p < kPts; ++p)
    {
        const double f = 20.0 * std::pow (1000.0, (double) p / (kPts - 1));
        wStart[(size_t) p] = (int) wBin.size();
        const int k0 = juce::jmax (1, (int) std::floor (f * std::pow (2.0, -2.5 * sigma) / binHz));
        const int k1 = juce::jmin (kN / 2 - 1, (int) std::ceil (f * std::pow (2.0, 2.5 * sigma) / binHz));
        std::vector<std::pair<int, double>> w;
        double sum = 0;
        for (int k = k0; k <= k1; ++k)
        {
            const double d = std::log2 (k * binHz / f) / sigma, v = std::exp (-0.5 * d * d);
            if (v > 1e-3) { w.push_back ({ k, v }); sum += v; }
        }
        if (w.size() < 3)   // bassi: interpolazione tra i bin attorno a f
        {
            w.clear();
            const double kf = juce::jlimit (1.0, (double) (kN / 2 - 2), f / binHz);
            const int ki = (int) kf; const double t = kf - ki;
            w.push_back ({ ki, 1.0 - t }); w.push_back ({ ki + 1, t }); sum = 1.0;
        }
        // media pesata x larghezza della banda in bin = potenza della banda (un tono puro resta al suo livello)
        const double band = juce::jmax (1.0, f * std::log (2.0) * sigma * std::sqrt (juce::MathConstants<double>::twoPi) / binHz);
        for (auto& [k, v] : w) { wBin.push_back (k); wVal.push_back ((float) (v / sum * juce::jmin (band, sum))); }
        wCount[(size_t) p] = (int) w.size();
    }
}

void MeterView::frame()
{
    data.pull (proc);
    const int v = view();
    if (v != lastView) resized();
    // vettorscopio: fosfori che svaniscono con "persist"; segmenti antialiasing tra i campioni (traccia continua)
    if (v == 1)
    {
        const auto ga = graphArea();
        const int side = (int) juce::jmin (ga.getWidth(), ga.getHeight() - 20.0f);
        if (side < 8) { repaint(); return; }   // non ancora dimensionata
        if (! phosphor.isValid() || phosphor.getWidth() != side) { phosphor = juce::Image (juce::Image::ARGB, side, side, true); lastVx = lastVy = (float) side * 0.5f; }
        const float persist = proc.apvts.getRawParameterValue (BMeterIDs::vecPersist)->load() * 0.001f;
        phosphor.multiplyAllAlphas (std::exp (-1.0f / (60.0f * persist)));   // prima di aprire il contesto grafico
        juce::Graphics pg (phosphor);
        const float sc = proc.apvts.getRawParameterValue (BMeterIDs::vecScale)->load() / 100.0f * (float) side * 0.5f;
        pg.setColour (accent.withAlpha (0.55f));
        const int cnt = juce::jmin (data.fresh, 4096);
        for (int i = cnt - 1; i >= 0; --i)
        {
            const float l = data.at (0, i), r = data.at (1, i);
            const float x = (float) side * 0.5f + (l - r) * 0.7071f * sc, y = (float) side * 0.5f - (l + r) * 0.7071f * sc;
            if (std::abs (x - lastVx) + std::abs (y - lastVy) < 0.35f) continue;   // punti sovrapposti: niente lavoro
            pg.drawLine (lastVx, lastVy, x, y, 0.9f);
            lastVx = x; lastVy = y;
        }
    }
    // spettro: due FFT (L, R), potenza media, lisciatura 1/3 d'ottava, tilt, salita morbida e caduta secondo "speed"
    if (v == 3)
    {
        const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        if (std::abs (fs - weightsFs) > 0.5) buildSpecWeights (fs);
        for (int i = 0; i < kN; ++i) { fftL[(size_t) i] = data.at (0, kN - 1 - i) * window[(size_t) i]; fftR[(size_t) i] = data.at (1, kN - 1 - i) * window[(size_t) i]; }
        std::fill (fftL.begin() + kN, fftL.end(), 0.0f); std::fill (fftR.begin() + kN, fftR.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftL.data(), true);
        fft.performFrequencyOnlyForwardTransform (fftR.data(), true);
        for (int k = 0; k < kN / 2; ++k) fftL[(size_t) k] = 0.5f * (fftL[(size_t) k] * fftL[(size_t) k] + fftR[(size_t) k] * fftR[(size_t) k]);
        const int tilt = (int) proc.apvts.getRawParameterValue (BMeterIDs::specTilt)->load();
        const float slope = tilt == 0 ? 0.0f : (tilt == 1 ? 3.0f : 4.5f);
        const float fall = std::array<float, 3> { 0.6f, 0.82f, 0.94f } [(size_t) juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (BMeterIDs::specSpeed)->load())];
        const float norm = 4.0f / ((float) (kN / 2) * (float) (kN / 2));
        for (int p = 0; p < kPts; ++p)
        {
            double pw = 0;
            const int s0 = wStart[(size_t) p], n0 = wCount[(size_t) p];
            for (int j = 0; j < n0; ++j) pw += (double) wVal[(size_t) (s0 + j)] * fftL[(size_t) wBin[(size_t) (s0 + j)]];
            const float oct = (float) p / (float) (kPts - 1) * 9.9658f;   // log2 (f / 20)
            const float db = 10.0f * std::log10 ((float) pw * norm + 1e-12f) + slope * (oct - 5.6439f);   // 0 dB di tilt a 1 kHz
            auto& b = bands[(size_t) p];
            b = db > b ? b + 0.65f * (db - b) : b * fall + db * (1.0f - fall);
        }
    }
    repaint (getLocalBounds().withTrimmedTop (kControlH + kGapInner - 2));
}

juce::int64 MeterView::staticKey (int v) const
{
    auto pv = [&] (const char* id) { return (juce::int64) (proc.apvts.getRawParameterValue (id)->load() * 10.0f); };
    juce::int64 k = v + 1;
    if (v == 0) k = k * 100003 + pv (BMeterIDs::oscTime);
    if (v == 2) k = (k * 100003 + pv (BMeterIDs::waveWindow)) * 7 + pv (BMeterIDs::waveMode);
    return k * 13 + pv (BMeterIDs::target);
}

// tutto ciò che non si muove a ogni frame: griglie, assi, etichette, scala e testi della colonna loudness
void MeterView::paintStatic (juce::Graphics& g, int v)
{
    for (auto& [t, r] : labels) drawLabel (g, t, r, labelStatic, kLabelSize);
    const auto a = graphArea();
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    auto yLabel = [&] (const juce::String& t, float y) { drawLabel (g, t, juce::Rectangle<float> (0.0f, y - 8.0f, a.getX() - 6.0f, 16.0f).toNearestInt(), info, kAxisSmall, juce::Justification::centredRight, Face::MonoRegular); };
    if (v == 0)
    {
        grid (g, a);
        auto yFor = [&] (float x) { return a.getCentreY() - x * a.getHeight() * 0.5f; };
        for (auto [val, t] : { std::pair<float, const char*> { 1.0f, "+1.0" }, { 0.5f, "-6 dB" }, { 0.25f, "-12 dB" }, { 0.0f, "0" }, { -0.25f, "" }, { -0.5f, "" }, { -1.0f, "-1.0" } })
        {
            g.setColour (juce::exactlyEqual (val, 0.0f) ? gridZero : gridLine);
            g.fillRect (a.getX(), std::round (yFor (val)), a.getWidth(), kLine);
            if (*t) yLabel (t, yFor (val));
        }
        const float ms = proc.apvts.getRawParameterValue (BMeterIDs::oscTime)->load();
        drawAtBaseline (g, "-" + num (ms * 0.5f, 1) + " ms", a.getX(), base, info, kAxisSmall, juce::Justification::left);
        drawAtBaseline (g, "+" + num (ms * 0.5f, 1) + " ms", a.getRight(), base, info, kAxisSmall, juce::Justification::right);
    }
    else if (v == 1)
    {
        const float side = juce::jmin (a.getWidth(), a.getHeight() - 20.0f);
        const auto sq = juce::Rectangle<float> (side, side).withCentre ({ a.getCentreX(), a.getY() + side * 0.5f });
        grid (g, sq);
        g.setColour (gridLine);
        g.drawLine (sq.getX(), sq.getY(), sq.getRight(), sq.getBottom(), 1.0f);
        g.drawLine (sq.getRight(), sq.getY(), sq.getX(), sq.getBottom(), 1.0f);
        g.setColour (gridZero);
        g.fillRect (std::round (sq.getCentreX()), sq.getY(), kLine, sq.getHeight());
        g.fillRect (sq.getX(), std::round (sq.getCentreY()), sq.getWidth(), kLine);
        const float cap = capHeight (kTagSize, Face::SansSemiBold);
        drawTag (g, "l", sq.getX() + 6.0f, sq.getY() + 6.0f + cap);
        drawTag (g, "r", sq.getRight() - 6.0f, sq.getY() + 6.0f + cap, juce::Justification::right);
        drawTag (g, "+m", sq.getCentreX() + 6.0f, sq.getY() + 6.0f + cap);
        drawTag (g, "-s", sq.getX() + 6.0f, sq.getCentreY() - 6.0f);
        const auto bar = juce::Rectangle<float> (sq.getX(), a.getBottom() - 8.0f, sq.getWidth(), 8.0f);
        g.setColour (text); outline (g, bar);
        g.setColour (gridZero); g.fillRect (std::round (bar.getCentreX()), bar.getY(), kLine, bar.getHeight());
        drawAtBaseline (g, "-1", bar.getX(), base, info, kAxisSmall, juce::Justification::left);
        drawAtBaseline (g, "0", bar.getCentreX(), base, info, kAxisSmall, juce::Justification::horizontallyCentred);
        drawAtBaseline (g, "+1", bar.getRight(), base, info, kAxisSmall, juce::Justification::right);
    }
    else if (v == 2)
    {
        grid (g, a);
        const bool lr = proc.apvts.getRawParameterValue (BMeterIDs::waveMode)->load() > 0.5f;
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float f = (db + 48.0f) / 48.0f;
            const float y = lr ? a.getCentreY() - f * a.getHeight() * 0.5f : a.getBottom() - f * a.getHeight();
            g.setColour (gridLine); g.fillRect (a.getX(), std::round (y), a.getWidth(), kLine);
            yLabel (juce::String ((int) db), y);
        }
        const float secs = proc.apvts.getRawParameterValue (BMeterIDs::waveWindow)->load();
        drawAtBaseline (g, "-" + num (secs, 1) + " s", a.getX(), base, info, kAxisSmall, juce::Justification::left);
        drawAtBaseline (g, lr ? "L up / R down" : "peak (light) / rms (solid)", a.getRight(), base, info, kAxisSmall, juce::Justification::right, Face::SansRegular);
    }
    else
    {
        grid (g, a);
        auto xFor = [&] (double hz) { return a.getX() + a.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
        auto yFor = [&] (float db) { return a.getY() + a.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            g.setColour (gridLine); g.fillRect (a.getX(), std::round (yFor (db)), a.getWidth(), kLine);
            yLabel (juce::String ((int) db), yFor (db));
        }
        for (auto [hz, t] : { std::pair<double, const char*> { 20, "20" }, { 50, "50" }, { 100, "100" }, { 250, "250" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } })
        {
            g.setColour (gridLine); g.fillRect (std::round (xFor (hz)), a.getY(), kLine, a.getHeight());
            drawAtBaseline (g, t, xFor (hz), base, info, kAxisSmall, juce::Justification::horizontallyCentred);
        }
    }
    // colonna loudness: scala, target, didascalie
    const float x0 = std::round ((float) getWidth() * 0.75f);
    const auto r = juce::Rectangle<float> (x0, 0.0f, (float) getWidth() - x0, (float) getHeight());
    const auto& t = proc.currentTarget();
    const auto bar = loudBar (r);
    auto yFor = [&] (float lufs) { return bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, -lufs / 36.0f); };
    drawTag (g, "lufs", r.getX(), r.getY() + capHeight (kTagSize, Face::SansSemiBold) + 4.0f);
    for (float val : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f })
        drawLabel (g, juce::String ((int) val), juce::Rectangle<float> (r.getX(), yFor (val) - 8.0f, 28.0f, 16.0f).toNearestInt(), info, kAxisSmall, juce::Justification::centredRight, Face::MonoRegular);
    g.setColour (accent.withAlpha (0.22f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getX(), yFor (t.lufs + 1.0f), bar.getRight(), yFor (t.lufs - 1.0f)));
    g.setColour (accent); g.fillRect (bar.getX() - 4.0f, std::round (yFor (t.lufs)), bar.getWidth() + 8.0f, kLine);
    g.setColour (text); outline (g, bar);
    const float tx = bar.getRight() + 16.0f;
    drawTag (g, "s", tx, bar.getY() + 20.0f); drawTag (g, "m", tx, bar.getY() + 42.0f); drawTag (g, "tp", tx, bar.getY() + 64.0f);
    drawTag (g, "target", tx, bar.getY() + 96.0f);
    drawAtBaseline (g, num (t.lufs, 1), tx, bar.getY() + 114.0f, accent, kValueSize, juce::Justification::left, Face::MonoMedium);
    drawAtBaseline (g, "tp " + num (t.truePeak, 1), tx, bar.getY() + 132.0f, info, kAxisSmall, juce::Justification::left, Face::MonoRegular);
    drawLabel (g, "gain", juce::Rectangle<int> ((int) r.getX(), (int) r.getBottom() - kControlH - 54, 40, 14), labelStatic, kLabelSize);
}

void MeterView::paint (juce::Graphics& g)
{
    const int v = view();
    staticLayer.draw (g, getLocalBounds(), staticKey (v), [this, v] (juce::Graphics& sg) { paintStatic (sg, v); });
    const auto ga = graphArea();
    switch (v)
    {
        case 0: paintOsc (g, ga); break;
        case 1: paintVec (g, ga); break;
        case 2: paintWave (g, ga); break;
        default: paintSpec (g, ga); break;
    }
    const float x0 = std::round ((float) getWidth() * 0.75f);
    paintLoudness (g, juce::Rectangle<float> (x0, 0.0f, (float) getWidth() - x0, (float) getHeight()));
}

// oscilloscopio: con pochi campioni per pixel curva spline (Catmull-Rom) tra i campioni; con molti campioni per
// pixel inviluppo min/max riempito (nessun aliasing, nessun picco perso)
void MeterView::paintOsc (juce::Graphics& g, juce::Rectangle<float> a)
{
    const float gainLin = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue (BMeterIDs::oscGain)->load());
    auto yFor = [&] (float v) { return a.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * a.getHeight() * 0.5f; };
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float ms = proc.apvts.getRawParameterValue (BMeterIDs::oscTime)->load();
    const int win = juce::jlimit (16, 8000, (int) (ms * 0.001 * fs));
    float pk = 1e-6f; for (int i = 0; i < win * 2; ++i) pk = juce::jmax (pk, std::abs (data.at (0, i)));
    if (pk * gainLin < 1e-4f) return;
    // trigger: fronte di salita sullo zero con isteresi, il più recente con mezza finestra dopo di sé
    const float h = 0.05f * pk;
    int trig = -1;
    for (int back = win / 2 + 1; back < win / 2 + 6000 && trig < 0; ++back)
        if (data.at (0, back) > h)
            for (int k = back + 1; k < back + 2000; ++k) { if (data.at (0, k) < -h) { trig = back; break; } if (data.at (0, k) > h) { back = k - 1; break; } }
    const int centre = trig < 0 ? win / 2 : trig;
    const int w = (int) a.getWidth();
    const float spp = (float) win / (float) w;
    auto sampleAt = [&] (float back)   // Catmull-Rom tra i campioni
    {
        const int i = (int) std::floor (back); const float t = back - (float) i;
        const float y0 = data.at (0, juce::jmax (0, i - 1)), y1 = data.at (0, juce::jmax (0, i)), y2 = data.at (0, juce::jmax (0, i + 1)), y3 = data.at (0, juce::jmax (0, i + 2));
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    };
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (a.toNearestInt());
    if (spp <= 2.5f)
    {
        std::vector<juce::Point<float>> pts; pts.reserve ((size_t) w * 2);
        const int sub = spp < 1.0f ? 2 : 1;   // pochi campioni: 2 punti per pixel sulla spline
        for (int x = 0; x < w * sub; ++x)
        {
            const float back = (float) (centre + win / 2) - (float) x / (float) (w * sub) * (float) win;
            pts.push_back ({ a.getX() + (float) x / (float) sub, yFor (sampleAt (juce::jmax (0.0f, back)) * gainLin) });
        }
        g.setColour (accent);
        g.strokePath (smoothPath (pts.data(), (int) pts.size()), juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }
    else
    {
        juce::Path env;
        std::vector<float> lo ((size_t) w);
        for (int x = 0; x < w; ++x)
        {
            const int b0 = centre + win / 2 - (int) ((float) x * spp), b1 = centre + win / 2 - (int) ((float) (x + 1) * spp);
            float mn = 1e9f, mx = -1e9f;
            for (int b = juce::jmax (0, b1); b <= juce::jmax (0, b0); ++b) { const float v = data.at (0, b); mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
            const float px = a.getX() + (float) x;
            if (x == 0) env.startNewSubPath (px, yFor (mx * gainLin)); else env.lineTo (px, yFor (mx * gainLin));
            lo[(size_t) x] = yFor (mn * gainLin);
        }
        for (int x = w - 1; x >= 0; --x) env.lineTo (a.getX() + (float) x, lo[(size_t) x] + 0.75f);
        env.closeSubPath();
        g.setColour (accent.withAlpha (0.9f)); g.fillPath (env);
        g.setColour (accent); g.strokePath (env, juce::PathStrokeType (0.75f));
    }
}

void MeterView::paintVec (juce::Graphics& g, juce::Rectangle<float> a)
{
    const float side = juce::jmin (a.getWidth(), a.getHeight() - 20.0f);
    const auto sq = juce::Rectangle<float> (side, side).withCentre ({ a.getCentreX(), a.getY() + side * 0.5f });
    if (phosphor.isValid()) g.drawImageAt (phosphor, (int) sq.getX(), (int) sq.getY());
    // correlazione di fase sulle ultime ~85 ms
    double lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < 4096; ++i) { const double l = data.at (0, i), r = data.at (1, i); lr += l * r; ll += l * l; rr += r * r; }
    const auto bar = juce::Rectangle<float> (sq.getX(), a.getBottom() - 8.0f, sq.getWidth(), 8.0f);
    if (ll + rr > 1e-9)
    {
        const float c = (float) juce::jlimit (-1.0, 1.0, lr / std::sqrt (ll * rr + 1e-20));
        g.setColour (c < 0 ? kRed : accent);
        g.fillRect (std::round (bar.getX() + (c + 1.0f) * 0.5f * (bar.getWidth() - 3.0f)), bar.getY() + 1.0f, 3.0f, bar.getHeight() - 2.0f);
    }
}

// storia: inviluppi come superfici piene (un vertice per pixel), interpolati tra i blocchi da 10 ms quando la
// finestra è corta, massimo dei blocchi quando è lunga
void MeterView::paintWave (juce::Graphics& g, juce::Rectangle<float> a)
{
    const bool lr = proc.apvts.getRawParameterValue (BMeterIDs::waveMode)->load() > 0.5f;
    auto norm = [] (float v) { return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (v + 1e-9f) + 48.0f) / 48.0f); };   // 0 / -48 dBFS
    const float secs = proc.apvts.getRawParameterValue (BMeterIDs::waveWindow)->load();
    const int nBins = (int) (secs * 100.0f), w = (int) a.getWidth() - 2, nb = (int) data.bins.size();
    auto binAt = [&] (int b) -> const MeterData::Bin& { return data.bins[(size_t) ((data.binPos - 1 - juce::jmax (0, b) + nb * 4) % nb)]; };
    std::vector<MeterData::Bin> col ((size_t) w);
    const float bpp = (float) nBins / (float) w;
    for (int x = 0; x < w; ++x)
    {
        const float fb = (float) (w - 1 - x) * bpp;
        MeterData::Bin m;
        if (bpp > 1.0f)
            for (int b = (int) fb, e = juce::jmax ((int) fb + 1, (int) (fb + bpp)); b < e; ++b)
            {
                const auto& q = binAt (b);
                m.pkL = juce::jmax (m.pkL, q.pkL); m.pkR = juce::jmax (m.pkR, q.pkR); m.rmsL = juce::jmax (m.rmsL, q.rmsL); m.rmsR = juce::jmax (m.rmsR, q.rmsR);
            }
        else
        {
            const int b = (int) fb; const float t = fb - (float) b;
            const auto& p = binAt (b); const auto& q = binAt (b + 1);
            m.pkL = p.pkL + t * (q.pkL - p.pkL); m.pkR = p.pkR + t * (q.pkR - p.pkR); m.rmsL = p.rmsL + t * (q.rmsL - p.rmsL); m.rmsR = p.rmsR + t * (q.rmsR - p.rmsR);
        }
        col[(size_t) x] = m;
    }
    auto area = [&] (auto value, float baseY, float dir, float scale)   // superficie da baseY verso dir
    {
        juce::Path p;
        p.startNewSubPath (a.getX() + 1.0f, baseY);
        for (int x = 0; x < w; ++x) p.lineTo (a.getX() + 1.0f + (float) x, baseY - dir * norm (value (col[(size_t) x])) * scale);
        p.lineTo (a.getX() + (float) w, baseY);
        p.closeSubPath();
        return p;
    };
    if (lr)
    {
        g.setColour (accent.withAlpha (0.8f));  g.fillPath (area ([] (const MeterData::Bin& m) { return m.pkL; }, a.getCentreY(), 1.0f, a.getHeight() * 0.5f));
        g.setColour (accent.withAlpha (0.45f)); g.fillPath (area ([] (const MeterData::Bin& m) { return m.pkR; }, a.getCentreY(), -1.0f, a.getHeight() * 0.5f));
    }
    else
    {
        g.setColour (accent.withAlpha (0.3f)); g.fillPath (area ([] (const MeterData::Bin& m) { return juce::jmax (m.pkL, m.pkR); }, a.getBottom(), 1.0f, a.getHeight()));
        g.setColour (accent);                  g.fillPath (area ([] (const MeterData::Bin& m) { return juce::jmax (m.rmsL, m.rmsR); }, a.getBottom(), 1.0f, a.getHeight()));
    }
}

void MeterView::paintSpec (juce::Graphics& g, juce::Rectangle<float> a)
{
    std::array<juce::Point<float>, kPts> pts;
    bool any = false;
    for (int p = 0; p < kPts; ++p)
    {
        const float db = bands[(size_t) p];
        any = any || db > -60.0f;
        pts[(size_t) p] = { a.getX() + a.getWidth() * (float) p / (float) (kPts - 1), a.getY() + a.getHeight() * juce::jlimit (-0.02f, 1.02f, -db / 60.0f) };
    }
    if (! any) return;   // a riposo: niente
    auto line = smoothPath (pts.data(), kPts);
    auto fill = line;
    fill.lineTo (a.getRight(), a.getBottom() + 2.0f); fill.lineTo (a.getX(), a.getBottom() + 2.0f); fill.closeSubPath();
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (a.toNearestInt());
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f), 0.0f, a.getY(), accent.withAlpha (0.04f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent); g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
}

juce::Rectangle<float> MeterView::loudBar (juce::Rectangle<float> r) const
{
    return { r.getX() + 34.0f, r.getY() + (float) (kControlH + kGapInner), 22.0f, r.getHeight() - (float) (kControlH + kGapInner) - 90.0f };
}

void MeterView::paintLoudness (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = proc.currentTarget();
    const auto bar = loudBar (r);
    auto yFor = [&] (float lufs) { return bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, -lufs / 36.0f); };
    const float st = proc.shortTerm.load(), mom = proc.momentary.load(), pk = proc.peakDb.load();
    auto colourFor = [&] (float v) { return v > t.lufs + 1.0f ? kAmber : (v < t.lufs - 1.0f ? valueText : accent); };
    if (st > -60.0f) { g.setColour (colourFor (st)); g.fillRect (bar.getX() - 6.0f, std::round (yFor (st)) - 1.0f, bar.getWidth() + 12.0f, 3.0f); }   // short-term: spessa
    if (mom > -60.0f) { g.setColour (colourFor (mom).withAlpha (0.8f)); g.fillRect (bar.getX(), std::round (yFor (mom)), bar.getWidth(), kLine); }     // momentary: sottile
    const float tx = bar.getRight() + 16.0f;
    auto line = [&] (float v, float y, juce::Colour c)
    { drawAtBaseline (g, v > -60.0f ? num (v, 1) : juce::String ("--"), tx + 30.0f, y, c, kValueSize, juce::Justification::left, Face::MonoMedium); };
    line (st, bar.getY() + 20.0f, st > -60.0f ? colourFor (st) : info);
    line (mom, bar.getY() + 42.0f, mom > -60.0f ? colourFor (mom) : info);
    line (pk, bar.getY() + 64.0f, pk > t.truePeak ? kRed : (pk > -60.0f ? valueText : info));
    const auto note = juce::Rectangle<float> (r.getX(), r.getBottom() - 30.0f, r.getWidth(), 30.0f).toNearestInt();
    g.setColour (proc.isAutoListening() ? accent : info);
    g.setFont (face (Face::SansRegular, kInfoSize - 1.0f));
    g.drawFittedText ("auto: play the loudest part (chorus / drop) to calibrate", note, juce::Justification::topLeft, 2);
}

BMeterEditor::BMeterEditor (BMeterProcessor& p)
    : ShellEditor (p, p.apvts, "meter", 900, 540), view (p)
{
    setHeaderChoice ("view", BMeterIDs::view, { "osc", "vec", "wave", "spec" });
    addChoice ("target", BMeterIDs::target, { "social", "youtube", "digital", "cd", "podcast" });
    setDisplay (view);
    displayRefreshesItself = true;   // la vista si ridisegna da sé a 60 fps
    resized();
    startTimerHz (60);
}

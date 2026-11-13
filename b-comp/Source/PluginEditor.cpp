#include "PluginEditor.h"

void CompGraph::paint (juce::Graphics& g)
{
    using namespace ui;
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const int st = juce::jlimit (0, BCompProcessor::kStyles - 1, (int) v (BCompIDs::style));
    const float t = v (BCompIDs::threshold), r = v (BCompIDs::ratio), k = st == BCompProcessor::fet ? juce::jmin (2.0f, v (BCompIDs::knee)) : v (BCompIDs::knee);
    const auto all = getLocalBounds().toFloat();

    // --- 1. la curva: ingresso (orizzontale) -> uscita (verticale), da -60 a 0 dB, quadrata
    const float side = juce::jmin (all.getHeight() - 26.0f, all.getWidth() * 0.4f);
    const auto curve = juce::Rectangle<float> (all.getX() + 34.0f, all.getY() + 6.0f, side, side);
    auto cx = [&] (float db) { return curve.getX() + curve.getWidth() * (db + 60.0f) / 60.0f; };
    auto cy = [&] (float db) { return curve.getBottom() - curve.getHeight() * (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f; };
    for (float db : { -48.0f, -36.0f, -24.0f, -12.0f })
    {
        g.setColour (gridLine);
        g.fillRect (std::round (cx (db)), curve.getY(), kLine, curve.getHeight());
        g.fillRect (curve.getX(), std::round (cy (db)), curve.getWidth(), kLine);
        drawAtBaseline (g, juce::String ((int) db), curve.getX() - 6.0f, cy (db) + 4.0f, info, kAxisSize, juce::Justification::right);
        drawAtBaseline (g, juce::String ((int) db), cx (db), curve.getBottom() + 16.0f, info, kAxisSize, juce::Justification::horizontallyCentred);
    }
    g.setColour (gridLine); outline (g, curve);
    drawTag (g, "in", curve.getRight(), curve.getBottom() + 16.0f, juce::Justification::right);
    drawTag (g, "out", curve.getX() + 6.0f, curve.getY() + 14.0f);
    // la diagonale 1:1 (nessuna compressione)
    g.setColour (gridZero);
    g.drawLine (curve.getX(), curve.getBottom(), curve.getRight(), curve.getY(), 1.0f);
    // la soglia
    g.setColour (accent.withAlpha (0.35f));
    g.fillRect (std::round (cx (t)), curve.getY(), kLine, curve.getHeight());
    {
        const juce::String thr = "thr " + num (t, 1) + " dB";
        const float tw = juce::GlyphArrangement::getStringWidth (face (Face::MonoRegular, kAxisSize), thr);
        if (cx (t) + 6.0f + tw < curve.getRight() - 4.0f) drawValueTag (g, thr, cx (t) + 6.0f, curve.getBottom() - 8.0f);
        else drawValueTag (g, thr, cx (t) - 6.0f, curve.getBottom() - 8.0f, juce::Justification::right);
    }
    // la curva (con lo stile multi, quella delle tre bande più tenui)
    auto drawCurve = [&] (float thr, float ratio, juce::Colour c, float w)
    {
        juce::Path pth;
        for (int i = 0; i <= 120; ++i)
        {
            const float x = -60.0f + 0.5f * (float) i;
            const float y = x + BCompProcessor::gainDb (x, thr, ratio, k);
            if (i == 0) pth.startNewSubPath (cx (x), cy (y)); else pth.lineTo (cx (x), cy (y));
        }
        g.setColour (c);
        g.strokePath (pth, juce::PathStrokeType (w));
    };
    if (st == BCompProcessor::multi)
    {
        const float amt[3] = { v (BCompIDs::low) / 100.0f, v (BCompIDs::mid) / 100.0f, v (BCompIDs::high) / 100.0f };
        for (int b = 0; b < 3; ++b) drawCurve (t + BCompProcessor::kBandThr[b], 1.0f + (r - 1.0f) * amt[b], accent.withAlpha (0.25f + 0.25f * (float) b), 1.0f);
    }
    else drawCurve (t, r, accent, 1.5f);
    // dove si trova il suono adesso: il punto sulla curva al livello d'ingresso
    {
        const float in = proc.inDb.load();
        if (in > -60.0f)
        {
            const float y = in + BCompProcessor::gainDb (in, t, r, k);
            g.setColour (valueText);
            g.fillEllipse (cx (in) - 3.5f, cy (y) - 3.5f, 7.0f, 7.0f);
        }
    }

    // --- 2. gli ultimi secondi, in due fasce: sopra i livelli (ingresso grigio, uscita lime, 0...-60 dB), sotto la
    //        riduzione che scende dall'alto (0...-24 dB). Solo i punti in cui c'era suono.
    const bool multi = st == BCompProcessor::multi;
    const float barsW = multi ? 3.0f * 14.0f + 2.0f * 8.0f + 12.0f : 0.0f;
    const auto hist = juce::Rectangle<float> (curve.getRight() + 40.0f, curve.getY(), all.getRight() - curve.getRight() - 40.0f - 46.0f - barsW, curve.getHeight());
    const auto lv = hist.withHeight (std::round (hist.getHeight() * 0.58f));
    const auto rd = hist.withTop (lv.getBottom() + 14.0f);
    auto ly = [&] (float db) { return lv.getY() + lv.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };
    auto ry = [&] (float gr) { return rd.getY() + rd.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 24.0f); };
    for (float db : { -12.0f, -24.0f, -36.0f, -48.0f })
    {
        g.setColour (gridLine); g.fillRect (lv.getX(), std::round (ly (db)), lv.getWidth(), kLine);
        drawAtBaseline (g, juce::String ((int) db), lv.getX() - 6.0f, ly (db) + 4.0f, info, kAxisSmall, juce::Justification::right);
    }
    for (float d : { 6.0f, 12.0f, 18.0f })
    {
        g.setColour (gridLine); g.fillRect (rd.getX(), std::round (ry (d)), rd.getWidth(), kLine);
        drawAtBaseline (g, "-" + juce::String ((int) d), rd.getX() - 6.0f, ry (d) + 4.0f, info, kAxisSmall, juce::Justification::right);
    }
    g.setColour (gridLine); outline (g, lv); outline (g, rd);
    const int last = proc.historyPos.load (std::memory_order_acquire);
    const int nPts = BCompProcessor::kHistory;
    juce::Path inFill, out, gr, grFill;
    bool inRun = false;
    float firstX = -1.0f, lastX = 0.0f;
    for (int j = 0; j < nPts; ++j)
    {
        const auto& f = proc.history[(size_t) ((last - (nPts - 1 - j)) & (nPts - 1))];
        const float x = lv.getX() + lv.getWidth() * (float) j / (float) (nPts - 1);
        if (f.in <= -90.0f && ! inRun) continue;   // nessun suono ancora
        if (! inRun)
        {
            inRun = true; firstX = x;
            inFill.startNewSubPath (x, lv.getBottom());
            out.startNewSubPath (x, ly (juce::jmax (-60.0f, f.out)));
            gr.startNewSubPath (x, ry (f.gr));
            grFill.startNewSubPath (x, rd.getY());
        }
        inFill.lineTo (x, ly (juce::jmax (-60.0f, f.in)));
        out.lineTo (x, ly (juce::jmax (-60.0f, f.out)));
        gr.lineTo (x, ry (f.gr));
        grFill.lineTo (x, ry (f.gr));
        lastX = x;
    }
    if (inRun)
    {
        inFill.lineTo (lastX, lv.getBottom()); inFill.closeSubPath();
        grFill.lineTo (lastX, rd.getY()); grFill.lineTo (firstX, rd.getY()); grFill.closeSubPath();
        g.saveState();
        g.reduceClipRegion (hist.toNearestInt());
        g.setColour (text.withAlpha (0.2f)); g.fillPath (inFill);
        g.setColour (accent); g.strokePath (out, juce::PathStrokeType (1.2f));
        g.setColour (accent.withAlpha (0.18f)); g.fillPath (grFill);
        g.setColour (accent.withAlpha (0.8f)); g.strokePath (gr, juce::PathStrokeType (1.0f));
        g.restoreState();
    }
    drawTag (g, "level", lv.getX() + 8.0f, lv.getY() + 14.0f);
    drawTag (g, "in", lv.getRight() - 46.0f, lv.getBottom() - 8.0f, juce::Justification::right);
    drawAtBaseline (g, "OUT", lv.getRight() - 8.0f, lv.getBottom() - 8.0f, accent, kTagSize, juce::Justification::right, Face::SansSemiBold, 0.08f);
    drawTag (g, "gain reduction", rd.getX() + 8.0f, rd.getBottom() - 8.0f);

    // --- 3. la riduzione adesso (numero) e, con multi, le tre bande
    const float grNow = proc.grDb.load();
    const auto meter = juce::Rectangle<float> (hist.getRight() + 16.0f, hist.getY(), 30.0f, hist.getHeight());
    g.setColour (gridLine); outline (g, meter);
    g.setColour (accent);
    g.fillRect (meter.reduced (1.0f).withHeight (juce::jmin (meter.getHeight() - 2.0f, (meter.getHeight() - 2.0f) * grNow / 24.0f)));
    drawAtBaseline (g, num (-grNow, 1), meter.getCentreX(), meter.getBottom() + 16.0f, grNow > 0.05f ? valueText : info, kAxisSize,
                    juce::Justification::horizontallyCentred, Face::MonoMedium);
    drawTag (g, "gr", meter.getCentreX(), meter.getBottom() + 30.0f, juce::Justification::horizontallyCentred);
    if (multi)
    {
        const char* names[3] = { "lo", "mid", "hi" };
        for (int b = 0; b < 3; ++b)
        {
            const auto bar = juce::Rectangle<float> (meter.getRight() + 12.0f + (float) b * 22.0f, hist.getY(), 14.0f, hist.getHeight());
            const float gb = proc.bandGr[(size_t) b].load();
            g.setColour (gridLine); outline (g, bar);
            g.setColour (accent.withAlpha (0.4f + 0.2f * (float) b));
            g.fillRect (bar.reduced (1.0f).withHeight (juce::jmin (bar.getHeight() - 2.0f, (bar.getHeight() - 2.0f) * gb / 24.0f)));
            drawTag (g, names[b], bar.getCentreX(), bar.getBottom() + 16.0f, juce::Justification::horizontallyCentred);
        }
    }
}

namespace
{
    void placeBig (juce::Component& c, juce::Rectangle<int> cell, int idealW)
    {
        const int w = juce::jlimit (juce::jmin (idealW, cell.getWidth() - 40), cell.getWidth() - 40, juce::jmax (idealW, 128));
        c.setBounds (cell.getCentreX() - w / 2, cell.getY(), w, ui::BigValue::kHeight);
    }
    int buttonTop (int rowY) { return rowY + ui::BigValue::kHeight - ui::BigValue::kBoxH / 2 - 14; }
    std::vector<int> placeSections (const std::vector<ui::Section*>& secs, juce::Rectangle<int> area)
    {
        std::vector<int> seps;
        const int n = (int) secs.size();
        for (int i = 0; i < n; ++i)
        {
            const int x0 = area.getX() + area.getWidth() * i / n, x1 = area.getX() + area.getWidth() * (i + 1) / n;
            const int w = secs[(size_t) i]->getIdealWidth();
            secs[(size_t) i]->setBounds ((x0 + x1) / 2 - w / 2, area.getY(), w, area.getHeight());
            if (i > 0) seps.push_back (x0);
        }
        return seps;
    }
    void setupBox (ui::ValueBox& b, juce::RangedAudioParameter* p, std::function<juce::String (float)> f, std::function<float (float, int)> st, juce::StringArray sizes)
    {
        b.setParameter (p);
        b.format = std::move (f);
        b.step = std::move (st);
        b.drag = [p] (float, float startNorm, float dx) { return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.003f)); };
        b.sizeSamples = std::move (sizes);
        b.setScale (ui::kRoomy);
    }
    void setupBig (ui::BigValue& b, const juce::String& label, juce::RangedAudioParameter* p, std::function<juce::String (float)> f, std::function<float (float, int)> st)
    {
        b.label = label; b.setParameter (p); b.format = std::move (f); b.step = std::move (st);
    }
}

BCompEditor::BCompEditor (BCompProcessor& p)
    : ShellEditor (p, p.apvts, "comp", 1100, 620), proc (p), graph (p),
      detSec ("detector"), parSec ("parallel", p.apvts.getParameter (BCompIDs::mixOn)), multiSec ("multiband"), outSec ("output")
{
    using namespace ui;
    auto prm = [&p] (const char* id) { return p.apvts.getParameter (id); };
    setHeaderChoice ("style", BCompIDs::style, { "clean", "opto", "fet", "squash", "multi" });
    // ogni stile ricorda i suoi valori; doppio clic su uno stile = i suoi default
    headerChoice->onUserSwitch = [&p] (int from, int to) { p.styleMemory.switchPreset (from, to); };
    headerChoice->onDoubleClick = [&p] (int st) { p.styleMemory.resetPreset (st); };
    infoText = {};
    auto db1 = [] (float v) { return num (v, 1) + " dB"; };
    auto stepDb = [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; };
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto ms = [] (float v) { return v < 10.0f ? num (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    auto msStep = [] (float v, int d) { return v * std::pow (2.0f, (float) d / 4.0f); };
    auto styleDef = [this] (const char* id) { return [this, id] { return BCompProcessor::styleDefault (id, juce::jlimit (0, BCompProcessor::kStyles - 1, (int) proc.apvts.getRawParameterValue (BCompIDs::style)->load())); }; };
    juce::ignoreUnused (styleDef);

    // 1. i valori principali, in grande
    setupBig (threshold, "threshold", prm (BCompIDs::threshold), db1, stepDb);
    setupBig (ratio, "ratio", prm (BCompIDs::ratio), [] (float v) { return num (v, 1) + " :1"; }, [] (float v, int d) { return juce::jlimit (1.0f, 20.0f, std::round ((v + 0.5f * (float) d) * 2.0f) / 2.0f); });
    setupBig (attack, "attack", prm (BCompIDs::attack), ms, msStep);
    setupBig (release, "release", prm (BCompIDs::release), ms, msStep);
    for (auto* b : { &threshold, &ratio, &attack, &release }) addAndMakeVisible (*b);
    autoGain.bindTo (*prm (BCompIDs::autoGain));
    autoGain.textSize = kRoomy.title;
    addAndMakeVisible (autoGain);

    // 3. i gruppi
    setupBox (knee, prm (BCompIDs::knee), db1, stepDb, { "18.0 dB" });
    setupBox (scHp, prm (BCompIDs::scHp), [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; }, [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); }, { "400 Hz" });
    setupBox (mix, prm (BCompIDs::mix), pct, stepPct, { "100 %" });
    setupBox (low, prm (BCompIDs::low), pct, stepPct, { "100 %" });
    setupBox (mid, prm (BCompIDs::mid), pct, stepPct, { "100 %" });
    setupBox (high, prm (BCompIDs::high), pct, stepPct, { "100 %" });
    setupBox (makeup, prm (BCompIDs::makeup), [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; }, stepDb, { "+24.0 dB", "-12.0 dB" });
    setupBox (output, prm (BCompIDs::output), [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; }, stepDb, { "+12.0 dB", "-24.0 dB" });
    detSec.add ("knee", knee, knee.getIdealWidth());
    detSec.add ("sc hp", scHp, scHp.getIdealWidth());
    parSec.add ("mix", mix, mix.getIdealWidth());
    multiSec.add ("low", low, low.getIdealWidth());
    multiSec.add ("mid", mid, mid.getIdealWidth());
    multiSec.add ("high", high, high.getIdealWidth());
    multiSec.isActive = [&p] { return (int) p.apvts.getRawParameterValue (BCompIDs::style)->load() == BCompProcessor::multi; };
    outSec.add ("makeup", makeup, makeup.getIdealWidth());
    outSec.add ("output", output, output.getIdealWidth());
    for (auto* sec : sections) { sec->setScale (kRoomy); addAndMakeVisible (*sec); sec->refresh(); }
    addAndMakeVisible (graph);
    tick();
    resized();
}

void BCompEditor::tick()
{
    for (auto* sec : sections) sec->refresh();
    // auto gain acceso: il makeup a mano non conta (grigio)
    makeup.setEnabled (proc.apvts.getRawParameterValue (BCompIDs::autoGain)->load() < 0.5f);
    graph.repaint();
}

void BCompEditor::resized()
{
    using namespace ui;
    ShellEditor::resized();
    const auto b = box().reduced (1 + kFramePad + 6);
    {
        const int n = 5;
        auto cell = [&] (int i) { return juce::Rectangle<int> (b.getX() + b.getWidth() * i / n, b.getY(), b.getWidth() / n, BigValue::kHeight); };
        placeBig (threshold, cell (0), threshold.getIdealWidth());
        placeBig (ratio, cell (1), ratio.getIdealWidth());
        placeBig (attack, cell (2), attack.getIdealWidth());
        placeBig (release, cell (3), release.getIdealWidth());
        const int aw = autoGain.getIdealWidth() + 16;
        autoGain.setBounds (cell (4).getCentreX() - aw / 2, buttonTop (b.getY()), aw, 28);
    }
    int secH = 0;
    for (auto* sec : sections) secH = juce::jmax (secH, sec->getIdealHeight());
    const int secY = b.getBottom() - secH;
    separators = placeSections ({ sections.begin(), sections.end() }, { b.getX(), secY, b.getWidth(), secH });
    const int gy = b.getY() + BigValue::kHeight + 26;
    graph.setBounds (b.getX(), gy, b.getWidth(), secY - 30 - gy);
}

void BCompEditor::paint (juce::Graphics& g)
{
    ShellEditor::paint (g);
    if (sections.front()->getBounds().isEmpty()) return;
    const auto r = sections.front()->getBounds();
    g.setColour (ui::separator);
    for (int sx : separators) g.fillRect (sx, r.getY(), 1, r.getHeight());
}

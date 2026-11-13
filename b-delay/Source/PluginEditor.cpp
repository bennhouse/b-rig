#include "PluginEditor.h"

void TapButton::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto r = getLocalBounds().toFloat();
    if (flash > 0) { g.setColour (accent); g.fillRect (r); }
    else { g.setColour (isMouseOver() ? labelHover : text); outline (g, r); }
    drawLabel (g, "tap", getLocalBounds(), flash > 0 ? onAccent : (isMouseOver() ? labelHover : selector), kRoomy.title,
               juce::Justification::centred, Face::SansSemiBold);
}

void DelayGraph::paint (juce::Graphics& g)
{
    using namespace ui;
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const auto all = getLocalBounds().toFloat();
    const auto plot = all.withTrimmedBottom (18.0f).withTrimmedLeft (18.0f).reduced (0.0f, 1.0f);
    // finestra dei tempi: la più corta (250 ms ... 2 s) che mostra almeno quattro ripetizioni del canale più lento
    const float slowest = juce::jmax (proc.msL.load(), proc.msR.load());
    float windowMs = 2000.0f;
    for (float w : { 250.0f, 500.0f, 1000.0f }) if (slowest * 4.0f <= w) { windowMs = w; break; }
    const int divisions = windowMs > 1500.0f ? 8 : 10;   // passi tondi: 25, 50, 100, 250 ms
    const float gridStep = windowMs / (float) divisions;
    auto xOf = [&] (float ms) { return plot.getX() + plot.getWidth() * ms / windowMs; };
    const float mid = std::round (plot.getCentreY()), half = plot.getHeight() * 0.5f - 6.0f;

    for (int k = 0; k <= divisions; ++k)
    {
        const float t = gridStep * (float) k;
        g.setColour (gridLine);
        g.fillRect (std::round (xOf (t)), plot.getY(), kLine, plot.getHeight());
        if (k % 2 == 0)
            drawAtBaseline (g, k == 0 ? juce::String ("0") : juce::String (juce::roundToInt (t)) + " ms", xOf (t), all.getBottom() - 4.0f, info, kAxisSize,
                            k == 0 ? juce::Justification::left : (k == divisions ? juce::Justification::right : juce::Justification::horizontallyCentred));
    }
    for (float f : { 0.5f, 1.0f })
    {
        g.setColour (gridLine);
        g.fillRect (plot.getX(), std::round (mid - half * f), plot.getWidth(), kLine);
        g.fillRect (plot.getX(), std::round (mid + half * f), plot.getWidth(), kLine);
    }
    g.setColour (gridZero);
    g.fillRect (plot.getX(), mid, plot.getWidth(), kLine);
    g.setColour (gridLine);
    outline (g, plot);
    drawAtBaseline (g, "L", all.getX() + 4.0f, mid - half * 0.5f + 4.0f, info, kAxisSize);
    drawAtBaseline (g, "R", all.getX() + 4.0f, mid + half * 0.5f + 4.0f, info, kAxisSize);

    // il suono che viaggia nella linea: il contenuto a ogni ritardo (L sopra, R sotto), ingresso grigio a 0 ms
    const int last = proc.ringPos.load (std::memory_order_acquire);
    const float fs = (float) (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    auto ringAt = [&] (const std::array<float, BDelayProcessor::kRing>& r, float ms)
    {
        const int back = (int) (ms * 0.001f * fs / (float) BDelayProcessor::kDecim);
        return back < BDelayProcessor::kRing ? r[(size_t) ((last - back) & (BDelayProcessor::kRing - 1))] : 0.0f;
    };
    const int cols = (int) plot.getWidth();
    for (int x = 0; x < cols; ++x)
    {
        const float ms = windowMs * (float) x / (float) cols;
        const float a = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringL, ms))), b = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringR, ms)));
        g.setColour (accent.withAlpha (0.28f));
        if (a > 0.01f) g.fillRect (plot.getX() + (float) x, mid - a * half, 1.0f, a * half);
        if (b > 0.01f) g.fillRect (plot.getX() + (float) x, mid + 1.0f, 1.0f, b * half);
    }
    {
        const float in = juce::jmin (1.0f, std::sqrt (ringAt (proc.ringIn, 0.0f)));
        g.setColour (text.withAlpha (0.6f));
        g.fillRect (plot.getX() + 1.0f, mid - in * half, 4.0f, 2.0f * in * half);
    }

    // tap previsti: posizione da time / offset / ping-pong, altezza dal feedback, campana più larga con lo smear
    const float tL = proc.msL.load(), tR = proc.msR.load(), fb = v (BDelayIDs::feedback) / 100.0f;
    const float smear = v (BDelayIDs::smearOn) > 0.5f ? v (BDelayIDs::smear) / 100.0f : 0.0f;   // gruppo spento: niente campane
    const bool pp = v (BDelayIDs::pingpong) > 0.5f;
    auto drawTap = [&] (float ms, float amp, bool left)
    {
        if (ms > windowMs || amp < 0.01f) return;
        const float x = xOf (ms), h = juce::jmin (1.0f, amp) * half;
        const float lit = juce::jmin (1.0f, 3.0f * ringAt (left ? proc.ringL : proc.ringR, ms));   // si accende quando l'eco suona
        g.setColour (accent.withAlpha (0.55f + 0.45f * lit));
        if (smear < 0.02f) g.fillRect (std::round (x) - 1.0f, left ? mid - h : mid + 1.0f, 2.0f, h);
        else
        {
            const float w = 3.0f + smear * 0.012f * plot.getWidth();
            juce::Path bell;
            bell.startNewSubPath (x - w, mid);
            for (int k = -12; k <= 12; ++k)
            {
                const float u = (float) k / 12.0f;
                bell.lineTo (x + u * w, mid + (left ? -1.0f : 1.0f) * h * std::exp (-4.0f * u * u));
            }
            bell.lineTo (x + w, mid); bell.closeSubPath();
            g.fillPath (bell);
        }
    };
    if (pp)
    {
        float t = 0.0f, amp = 1.0f;
        for (int k = 0; k < 64; ++k)
        {
            const bool left = (k % 2) == 0;
            t += left ? tL : tR;
            drawTap (t, amp, left);
            amp *= fb;
            if (t > windowMs) break;
        }
    }
    else
    {
        float amp = 1.0f;
        for (int k = 1; k < 64; ++k, amp *= fb) { drawTap (tL * (float) k, amp, true); drawTap (tR * (float) k, amp, false); if (tL * (float) k > windowMs && tR * (float) k > windowMs) break; }
    }
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    juce::String info2 = num (tL, tL < 100.0f ? 1 : 0) + " ms";
    if (std::abs (tR - tL) > 0.05f) info2 += " / " + num (tR, tR < 100.0f ? 1 : 0) + " ms";
    if (v (BDelayIDs::sync) > 0.5f) info2 = BDelayProcessor::divisionNames()[(int) v (BDelayIDs::division)] + " at " + juce::String (juce::roundToInt (proc.bpmNow.load())) + " bpm = " + info2;
    drawTag (g, "in", plot.getX() + 10.0f, plot.getY() + 8.0f + tagH);
    drawAtBaseline (g, "ECHO", plot.getX() + 32.0f, plot.getY() + 8.0f + tagH, accent, kTagSize, juce::Justification::left, Face::SansSemiBold, 0.08f);
    drawValueTag (g, info2, plot.getRight() - 8.0f, plot.getY() + 8.0f + tagH, juce::Justification::right);
    // tape e bbd: fin dove arrivano gli acuti delle ripetizioni (più il ritardo è lungo, più sono scure)
    const int type = juce::jlimit (0, 4, (int) v (BDelayIDs::type));
    if (const float hz = BDelayProcessor::characterHz (type, tL); hz > 0.0f)
        drawValueTag (g, (type == BDelayProcessor::Tape ? "tape to " : "bbd to ") + freqText (hz), plot.getRight() - 8.0f,
                      plot.getY() + 14.0f + 2.0f * tagH, juce::Justification::right);
}

namespace
{
    // colonne di uguale larghezza: il valore grande centrato nella sua colonna, con margine ai lati
    void placeBig (juce::Component& c, juce::Rectangle<int> cell, int idealW)
    {
        const int w = juce::jlimit (juce::jmin (idealW, cell.getWidth() - 40), cell.getWidth() - 40, juce::jmax (idealW, 128));
        c.setBounds (cell.getCentreX() - w / 2, cell.getY(), w, ui::BigValue::kHeight);
    }
    // pulsanti nella riga dei valori grandi: centrati in verticale sul numero
    int buttonTop (int rowY) { return rowY + ui::BigValue::kHeight - ui::BigValue::kBoxH / 2 - 14; }
    // gruppi in colonne di uguale larghezza, ognuno centrato nella sua; restituisce dove vanno le linee di separazione
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
}

namespace
{
    void setupBox (ui::ValueBox& b, juce::RangedAudioParameter* p, std::function<juce::String (float)> f, std::function<float (float, int)> st,
                   juce::StringArray sizes)
    {
        b.setParameter (p);
        b.format = std::move (f);
        b.step = std::move (st);
        b.drag = [p] (float, float startNorm, float dx) { return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * 0.003f)); };
        b.sizeSamples = std::move (sizes);
        b.setScale (ui::kRoomy);   // numeri ed etichette un poco più grandi, in equilibrio con i valori grandi
    }
}

BDelayEditor::BDelayEditor (BDelayProcessor& p)
    : ShellEditor (p, p.apvts, "delay", 1100, 620), proc (p), graph (p),
      toneSec ("tone"), modSec ("modulation", p.apvts.getParameter (BDelayIDs::modOn)),
      smearSec ("smear", p.apvts.getParameter (BDelayIDs::smearOn)), duckSec ("duck", p.apvts.getParameter (BDelayIDs::duckOn)),
      stereoSec ("stereo"), outSec ("output")
{
    using namespace ui;
    auto prm = [&p] (const char* id) { return p.apvts.getParameter (id); };
    setHeaderChoice ("type", BDelayIDs::type, { "digital", "tape", "bbd", "lofi", "reverse" });
    infoText = {};
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto oct6 = [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); };

    // 1. i valori principali, in grande
    time.label = "time"; time.setParameter (prm (BDelayIDs::time));
    time.format = [] (float v) { return v < 100.0f ? num (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    time.step = [] (float v, int d) { return v < 100.0f ? std::round (v) + (float) d : std::round (v / 10.0f + (float) d) * 10.0f; };
    time.normPerPixel = 0.002f;
    division.label = "time"; division.setParameter (prm (BDelayIDs::division));
    division.format = [] (float v) { return BDelayProcessor::divisionNames()[juce::jlimit (0, BDelayProcessor::kDivisions - 1, juce::roundToInt (v))]; };
    division.step = [] (float v, int d) { return (float) juce::jlimit (0, BDelayProcessor::kDivisions - 1, juce::roundToInt (v) + d); };
    division.normPerPixel = 0.006f;
    division.sizeSamples = { "2000 ms" };   // stessa larghezza del tempo in ms: il layout non si sposta
    feedback.label = "feedback"; feedback.setParameter (prm (BDelayIDs::feedback)); feedback.format = pct; feedback.step = stepPct;
    mix.label = "mix"; mix.setParameter (prm (BDelayIDs::mix)); mix.format = pct; mix.step = stepPct;
    addAndMakeVisible (time); addChildComponent (division); addAndMakeVisible (feedback); addAndMakeVisible (mix);
    sync.bindTo (*prm (BDelayIDs::sync));
    addAndMakeVisible (sync);
    tapBtn.onTap = [&p] { p.tap(); };
    tapBtn.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (tapBtn);

    // 3. i gruppi
    setupBox (locut, prm (BDelayIDs::locut), [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; }, oct6, { "800 Hz", "888 Hz" });
    setupBox (hicut, prm (BDelayIDs::hicut), [] (float v) { return freqText (v); }, oct6, { "20.0 kHz", "8.88 kHz" });
    setupBox (mod, prm (BDelayIDs::mod), pct, stepPct, { "100 %" });
    setupBox (smear, prm (BDelayIDs::smear), pct, stepPct, { "100 %" });
    setupBox (duck, prm (BDelayIDs::duck), pct, stepPct, { "100 %" });
    setupBox (offset, prm (BDelayIDs::offset), [] (float v) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; },
              stepPct, { "-50 %", "+50 %" });
    setupBox (output, prm (BDelayIDs::output), [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+12.0 dB", "-24.0 dB" });
    pingpong.bindTo (*prm (BDelayIDs::pingpong));
    trails.bindTo (*prm (BDelayIDs::trails));
    for (auto* t : { &sync, &pingpong, &trails }) t->textSize = kRoomy.title;
    toneSec.add ("locut", locut, locut.getIdealWidth());
    toneSec.add ("hicut", hicut, hicut.getIdealWidth());
    modSec.add ("depth", mod, mod.getIdealWidth());
    smearSec.add ("amount", smear, smear.getIdealWidth());
    duckSec.add ("amount", duck, duck.getIdealWidth());
    stereoSec.add ("offset", offset, offset.getIdealWidth());
    stereoSec.add ({}, pingpong, pingpong.getIdealWidth());
    outSec.add ("output", output, output.getIdealWidth());
    outSec.add ({}, trails, trails.getIdealWidth());
    for (auto* sec : sections) { sec->setScale (kRoomy); addAndMakeVisible (*sec); sec->refresh(); }
    addAndMakeVisible (graph);
    tick();
    resized();
}

void BDelayEditor::tick()
{
    const bool synced = proc.apvts.getRawParameterValue (BDelayIDs::sync)->load() > 0.5f;
    if (division.isVisible() != synced) { division.setVisible (synced); time.setVisible (! synced); }
    if (tapBtn.flash > 0) { --tapBtn.flash; tapBtn.repaint(); }
    for (auto* sec : sections) sec->refresh();
    graph.repaint();
}

void BDelayEditor::resized()
{
    using namespace ui;
    ShellEditor::resized();
    const auto b = box().reduced (1 + kFramePad + 6);   // un po' più d'aria del padding standard
    // 1. valori grandi in colonne uguali: time, sync + tap, feedback, mix
    {
        const int n = 4;
        auto cell = [&] (int i) { return juce::Rectangle<int> (b.getX() + b.getWidth() * i / n, b.getY(), b.getWidth() / n, BigValue::kHeight); };
        placeBig (time, cell (0), juce::jmax (time.getIdealWidth(), division.getIdealWidth()));
        division.setBounds (time.getBounds());
        const int sw = sync.getIdealWidth() + 8, tw = 60, pairW = sw + 8 + tw;
        const int px = cell (1).getCentreX() - pairW / 2;
        sync.setBounds (px, buttonTop (b.getY()), sw, 28);
        tapBtn.setBounds (px + sw + 8, buttonTop (b.getY()), tw, 28);
        placeBig (feedback, cell (2), feedback.getIdealWidth());
        placeBig (mix, cell (3), mix.getIdealWidth());
    }
    // 3. gruppi in basso
    int secH = 0;
    for (auto* sec : sections) secH = juce::jmax (secH, sec->getIdealHeight());
    const int secY = b.getBottom() - secH;
    separators = placeSections ({ sections.begin(), sections.end() }, { b.getX(), secY, b.getWidth(), secH });
    // 2. la timeline in mezzo, con lo stesso respiro sopra e sotto
    const int gy = b.getY() + BigValue::kHeight + 26;
    graph.setBounds (b.getX(), gy, b.getWidth(), secY - 30 - gy);
}

void BDelayEditor::paint (juce::Graphics& g)
{
    ShellEditor::paint (g);
    const auto r = sections.front()->getBounds();
    if (r.isEmpty()) return;
    g.setColour (ui::separator);
    for (int sx : separators) g.fillRect (sx, r.getY(), 1, r.getHeight());
}

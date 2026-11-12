#include "PluginEditor.h"
#include <complex>

void VerbGraph::paint (juce::Graphics& g)
{
    using namespace ui;
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const int type = juce::jlimit (0, 4, (int) v (BVerbIDs::type));
    const float pre = v (BVerbIDs::predelay) / 1000.0f, decay = v (BVerbIDs::decay), size = v (BVerbIDs::size) / 100.0f;
    const float early = v (BVerbIDs::earlyOn) > 0.5f ? v (BVerbIDs::early) / 100.0f : 0.0f, damp = v (BVerbIDs::damp);
    const bool freeze = v (BVerbIDs::freeze) > 0.5f;
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    const auto all = getLocalBounds().toFloat();
    // a destra i due livelli dal vivo (IN, WET), fuori dall'asse dei tempi: non si confondono con l'inviluppo
    constexpr float kMeterW = 46.0f;
    auto plot = all.withTrimmedLeft (34.0f).withTrimmedBottom (18.0f).withTrimmedTop (8.0f).withTrimmedRight (kMeterW + 12.0f);
    const auto meters = juce::Rectangle<float> (plot.getRight() + 12.0f, plot.getY(), kMeterW, plot.getHeight());
    // asse dei tempi: abbastanza per vedere tutta la coda (fino a 21 s)
    const float tMax = juce::jlimit (0.25f, 21.0f, (pre + (freeze ? 2.0f : decay)) * 1.15f);
    auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * t / tMax; };
    auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };

    // griglia: dB ogni 12, tempi a passi tondi; in secondi quando la coda supera il secondo
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        g.setColour (juce::exactlyEqual (db, 0.0f) ? gridZero : gridLine);
        g.fillRect (plot.getX(), std::round (yOf (db)), plot.getWidth(), kLine);
        drawAtBaseline (g, juce::String ((int) db), plot.getX() - 6.0f, yOf (db) + 4.0f, info, kAxisSize, juce::Justification::right);
    }
    float step = 0.05f;
    for (float c : { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f }) if (tMax / step > 8.0f) step = c;
    const bool seconds = tMax > 1.2f;
    for (int k = 0; (float) k * step <= tMax + 1.0e-4f; ++k)
    {
        const float t = (float) k * step;
        g.setColour (gridLine);
        g.fillRect (std::round (xOf (t)), plot.getY(), kLine, plot.getHeight());
        const juce::String lab = k == 0 ? juce::String ("0")
                               : seconds ? num (t, step < 1.0f ? (step < 0.5f ? 2 : 1) : 0) + " s"
                                         : juce::String (juce::roundToInt (t * 1000.0f)) + " ms";
        drawAtBaseline (g, lab, xOf (t), all.getBottom() - 4.0f, info, kAxisSize,
                        k == 0 ? juce::Justification::left : juce::Justification::horizontallyCentred);
    }
    g.setColour (gridLine);
    outline (g, plot);

    // prime riflessioni: pali verticali (spostati dal predelay, alti quanto "early")
    for (int k = 0; k < BVerbProcessor::kTaps; ++k)
    {
        const float t = pre + BVerbProcessor::tapTimeMs (type, size, k, false) / 1000.0f;
        const float lvl = early * BVerbProcessor::kTypeEarly[type] * BVerbProcessor::tapGain (k);
        if (lvl <= 0.001f) continue;
        const float y = yOf (20.0f * std::log10 (lvl));
        g.setColour (accent.withAlpha (0.8f));
        g.fillRect (std::round (xOf (t)) - 1.0f, y, 2.0f, plot.getBottom() - y);
    }
    // inviluppo del decadimento: il corpo come area piena e linea, le alte (8 kHz) come linea sottile; i tempi
    // vengono dalla stessa formula del loop (BVerbProcessor::rt60At)
    const float start = pre + 0.004f;
    if (freeze)
    {
        juce::Path body;
        body.addRectangle (xOf (start), yOf (-3.0f), plot.getRight() - xOf (start), plot.getBottom() - yOf (-3.0f));
        g.setColour (accent.withAlpha (0.10f)); g.fillPath (body);
        g.setColour (accent);
        g.fillRect (xOf (start), std::round (yOf (-3.0f)), plot.getRight() - xOf (start), 1.5f);
        drawTag (g, "freeze", plot.getRight() - 8.0f, yOf (-3.0f) - 6.0f, juce::Justification::right);
    }
    else
    {
        const float rtLo = (float) BVerbProcessor::rt60At (500.0, type, size, decay, damp, fs);
        const float rtHi = (float) BVerbProcessor::rt60At (8000.0, type, size, decay, damp, fs);
        juce::Path body;
        body.startNewSubPath (xOf (start), yOf (0.0f));
        body.lineTo (xOf (start + rtLo), yOf (-60.0f));
        body.lineTo (xOf (start), yOf (-60.0f));
        body.closeSubPath();
        g.setColour (accent.withAlpha (0.10f)); g.fillPath (body);
        g.setColour (accent.withAlpha (0.5f));
        g.drawLine (xOf (start), yOf (0.0f), xOf (start + rtHi), yOf (-60.0f), 1.0f);
        g.setColour (accent);
        g.drawLine (xOf (start), yOf (0.0f), xOf (start + rtLo), yOf (-60.0f), 1.5f);
        drawValueTag (g, "8 kHz " + num (rtHi, 2) + " s", xOf (start + rtHi * 0.5f) + 6.0f, yOf (-30.0f) - 4.0f);
    }

    // livelli dal vivo: picco degli ultimi ~20 ms, ingresso grigio e wet nel colore del plugin
    {
        const int last = proc.historyPos.load (std::memory_order_acquire);
        float in = 0.0f, wet = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            const auto& f = proc.history[(size_t) ((last - k) & (BVerbProcessor::kHistory - 1))];
            in = juce::jmax (in, f.in); wet = juce::jmax (wet, f.wet);
        }
        const float bw = 10.0f;
        const float xs[2] = { meters.getX() + 6.0f, meters.getRight() - 6.0f - bw };
        const float lv[2] = { in, wet };
        const char* names[2] = { "IN", "WET" };
        for (int k = 0; k < 2; ++k)
        {
            const auto track = juce::Rectangle<float> (xs[k], plot.getY(), bw, plot.getHeight());
            g.setColour (gridLine); outline (g, track);
            const float y = yOf (juce::jmax (-60.0f, sp::gainToDb (lv[k])));
            if (lv[k] > 0.001f) { g.setColour (k == 0 ? text.withAlpha (0.6f) : accent); g.fillRect (track.withTop (y).reduced (1.0f, 0.0f)); }
            drawAtBaseline (g, names[k], track.getCentreX(), all.getBottom() - 4.0f, k == 0 ? info : accent, kTagSize,
                            juce::Justification::horizontallyCentred, Face::SansSemiBold, 0.08f);
        }
    }
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
    }
    void setupBig (ui::BigValue& b, const juce::String& label, juce::RangedAudioParameter* p, std::function<juce::String (float)> f,
                   std::function<float (float, int)> st)
    {
        b.label = label; b.setParameter (p); b.format = std::move (f); b.step = std::move (st);
    }
}

BVerbEditor::BVerbEditor (BVerbProcessor& p)
    : ShellEditor (p, p.apvts, "verb", 1100, 640), graph (p),
      earlySec ("early", p.apvts.getParameter (BVerbIDs::earlyOn)), toneSec ("tone"),
      modSec ("modulation", p.apvts.getParameter (BVerbIDs::modOn)), duckSec ("duck", p.apvts.getParameter (BVerbIDs::duckOn)),
      outSec ("output")
{
    using namespace ui;
    auto prm = [&p] (const char* id) { return p.apvts.getParameter (id); };
    setHeaderChoice ("type", BVerbIDs::type, { "room", "plate", "hall", "spring", "shimmer" });
    infoText = {};
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    auto oct6 = [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); };
    auto hz = [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; };

    // 1. i valori principali, in grande
    setupBig (size, "size", prm (BVerbIDs::size), pct, stepPct);
    setupBig (decay, "decay", prm (BVerbIDs::decay), [] (float v) { return num (v, 2) + " s"; },
              [] (float v, int d) { return v * std::pow (2.0f, (float) d / 4.0f); });
    setupBig (predelay, "predelay", prm (BVerbIDs::predelay), [] (float v) { return num (v, 1) + " ms"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; });
    setupBig (mix, "mix", prm (BVerbIDs::mix), pct, stepPct);
    for (auto* b : { &size, &decay, &predelay, &mix }) addAndMakeVisible (*b);
    freeze.bindTo (*prm (BVerbIDs::freeze));
    addAndMakeVisible (freeze);

    // 3. i gruppi
    setupBox (early, prm (BVerbIDs::early), pct, stepPct, { "100 %" });
    setupBox (damp, prm (BVerbIDs::damp), [] (float v) { return freqText (v); }, oct6, { "16.0 kHz", "8.88 kHz" });
    setupBox (locut, prm (BVerbIDs::locut), hz, oct6, { "500 Hz", "888 Hz" });
    setupBox (hicut, prm (BVerbIDs::hicut), [] (float v) { return freqText (v); }, oct6, { "20.0 kHz", "8.88 kHz" });
    setupBox (mod, prm (BVerbIDs::mod), pct, stepPct, { "100 %" });
    setupBox (duck, prm (BVerbIDs::duck), pct, stepPct, { "100 %" });
    setupBox (width, prm (BVerbIDs::stereo), pct, stepPct, { "100 %" });
    setupBox (output, prm (BVerbIDs::output), [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "+12.0 dB", "-24.0 dB" });
    earlySec.add ("level", early, early.getIdealWidth());
    toneSec.add ("damp", damp, damp.getIdealWidth());
    toneSec.add ("locut", locut, locut.getIdealWidth());
    toneSec.add ("hicut", hicut, hicut.getIdealWidth());
    modSec.add ("depth", mod, mod.getIdealWidth());
    duckSec.add ("amount", duck, duck.getIdealWidth());
    outSec.add ("width", width, width.getIdealWidth());
    outSec.add ("output", output, output.getIdealWidth());
    for (auto* sec : sections) { addAndMakeVisible (*sec); sec->refresh(); }
    addAndMakeVisible (graph);
    resized();
}

void BVerbEditor::resized()
{
    using namespace ui;
    ShellEditor::resized();
    const auto b = box().reduced (1 + kFramePad + 6);   // un po' più d'aria del padding standard
    // 1. valori grandi in colonne uguali: size, decay, predelay, freeze, mix
    {
        const int n = 5;
        auto cell = [&] (int i) { return juce::Rectangle<int> (b.getX() + b.getWidth() * i / n, b.getY(), b.getWidth() / n, BigValue::kHeight); };
        placeBig (size, cell (0), size.getIdealWidth());
        placeBig (decay, cell (1), decay.getIdealWidth());
        placeBig (predelay, cell (2), predelay.getIdealWidth());
        const int fw = freeze.getIdealWidth() + 16;
        freeze.setBounds (cell (3).getCentreX() - fw / 2, buttonTop (b.getY()), fw, 28);
        placeBig (mix, cell (4), mix.getIdealWidth());
    }
    // 3. gruppi in basso
    int secH = 0;
    for (auto* sec : sections) secH = juce::jmax (secH, sec->getIdealHeight());
    const int secY = b.getBottom() - secH;
    separators = placeSections ({ sections.begin(), sections.end() }, { b.getX(), secY, b.getWidth(), secH });
    // 2. il grafico in mezzo, con lo stesso respiro sopra e sotto
    const int gy = b.getY() + BigValue::kHeight + 26;
    graph.setBounds (b.getX(), gy, b.getWidth(), secY - 30 - gy);
}

void BVerbEditor::paint (juce::Graphics& g)
{
    ShellEditor::paint (g);
    if (sections.front()->getBounds().isEmpty()) return;
    const auto r = sections.front()->getBounds();
    g.setColour (ui::separator);
    for (int sx : separators) g.fillRect (sx, r.getY(), 1, r.getHeight());
}

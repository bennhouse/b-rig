#include "PluginEditor.h"

// ============================================================================ IrLoader
namespace
{
    // testo accorciato con "..." finché non sta nella larghezza data
    juce::String fitText (const juce::String& s, const juce::Font& f, float w)
    {
        if (juce::GlyphArrangement::getStringWidth (f, s) <= w) return s;
        auto t = s;
        while (t.length() > 1 && juce::GlyphArrangement::getStringWidth (f, t + "...") > w) t = t.dropLastCharacters (1);
        return t.trimEnd() + "...";
    }
}

void IrLoader::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto r = getLocalBounds().toFloat();
    const char* letters[] = { "A", "B", "C", "D" };
    const auto name = proc.getSlotName (slot);
    if (proc.isLoading (slot))   // file scelto, lettura in corso sul thread del builder
    {
        g.setColour (accent); outline (g, r);
        drawLabel (g, "loading...", getLocalBounds(), accent, kControlTextSize, juce::Justification::centred, Face::SansRegular);
        return;
    }
    if (name.isEmpty())
    {
        g.setColour (isMouseOver() ? accent.brighter (0.15f) : accent);   // vuoto: tasto pieno "load"
        g.fillRect (r);
        drawLabel (g, juce::String ("load ") + letters[slot], getLocalBounds(), onAccent, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
        return;
    }
    // caricato: nome e ×; rosso se la frequenza di campionamento non è quella del progetto
    const double rate = proc.getSlotRate (slot), fs = proc.getSampleRate();
    const bool mismatch = fs > 0 && std::abs (rate - fs) > 1.0;
    const juce::Colour c = mismatch ? juce::Colour (0xffff4d4d) : text;
    g.setColour (c); outline (g, r);
    // lettera dello slot ben visibile (grassetto, colore del plugin), poi il nome accorciato prima della ×
    auto t = getLocalBounds().reduced (7, 0);
    t.removeFromRight (getHeight() - 2);
    drawLabel (g, letters[slot], t, mismatch ? c : accent, kControlTextSize, juce::Justification::centredLeft, Face::SansBold);
    t.removeFromLeft (16);
    juce::String caption = mismatch ? num (rate / 1000.0, 1) + "k!  " + name : name;
    caption = fitText (caption, face (Face::SansMedium, kInfoSize), (float) t.getWidth());
    drawLabel (g, caption, t, mismatch ? c : valueText, kInfoSize, juce::Justification::centredLeft, Face::SansMedium);
    const auto x = closeArea().toFloat().reduced (6.0f);
    g.setColour (closeArea().contains (getMouseXYRelative()) ? labelHover : c);
    g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.0f);
    g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.0f);
}

void IrLoader::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (proc.getSlotName (slot).isNotEmpty() && closeArea().contains (e.getPosition())) { proc.removeIr (slot); repaint(); return; }
    // slot vuoto: si sceglie una cartella; slot con un IR: il browser del plugin si apre subito nella cartella
    // di quell'IR (o nell'ultima aperta); il selettore di sistema solo se non se ne conosce nessuna
    if (onBrowse) onBrowse (slot, proc.getSlotName (slot).isNotEmpty() ? proc.getBrowseFolder (slot) : juce::File());
}

// ============================================================================ IrBrowser
namespace
{
    constexpr float kLoHz = 20.0f, kHiHz = 20000.0f, kRangeDb = 36.0f;
    const juce::Colour kWarn { 0xfff59e0b };
}

IrBrowser::IrBrowser (BBlendProcessor& p) : juce::Thread ("b-blend IR scan"), proc (p)
{
    formats.registerBasicFormats();
    setWantsKeyboardFocus (true);
}

IrBrowser::~IrBrowser() { close(); }

void IrBrowser::close()
{
    stopTimer();
    signalThreadShouldExit();
    stopThread (3000);
    setVisible (false);
    proc.setSoloSlot (-1);   // di nuovo il blend di tutti gli slot
}

int IrBrowser::usedBy (int i) const
{
    if (! juce::isPositiveAndBelow (i, (int) entries.size())) return -1;
    const int s = proc.slotUsing (entries[(size_t) i]->file);
    return s == slot ? -1 : s;
}

int IrBrowser::numAnalysed() const
{
    int n = 0;
    for (auto& e : entries) n += e->ready.load() ? 1 : 0;
    return n;
}

void IrBrowser::open (int s, const juce::File& dir)
{
    close();
    slot = s; folder = dir;
    entries.clear();
    // tutti i file audio della cartella e delle sottocartelle, in ordine naturale del percorso
    auto files = dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
    std::sort (files.begin(), files.end(), [&] (const juce::File& a, const juce::File& b)
               { return a.getRelativePathFrom (dir).compareNatural (b.getRelativePathFrom (dir)) < 0; });
    if (files.size() > 4000) files.removeRange (4000, files.size());
    const auto loaded = proc.getSlotFile (slot);
    current = -1;
    for (auto& f : files)
    {
        auto e = std::make_unique<Entry>();
        e->file = f;
        e->name = f.getRelativePathFrom (dir).upToLastOccurrenceOf (".", false, false);
        if (f == loaded) current = (int) entries.size();
        entries.push_back (std::move (e));
    }
    scroll = scrollTarget = 0;
    if (current >= 0) ensureVisible (current), scroll = scrollTarget;
    firstVisible = (int) (scroll / (float) kRowH);
    message.clear();
    proc.setSoloSlot (slot);   // si sente solo l'IR che si sta scegliendo
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    startThread (juce::Thread::Priority::low);
    startTimerHz (60);
    repaint();
}

void IrBrowser::run()
{
    // analizza prima le righe visibili, poi le altre: anche con cartelle grandi lo schermo si riempie subito
    juce::dsp::FFT fft (14);
    std::vector<float> buf (2 * 16384);
    while (! threadShouldExit())
    {
        const int n = (int) entries.size(), fv = juce::jlimit (0, juce::jmax (0, n - 1), firstVisible.load());
        int pick = -1;
        for (int k = 0; k < n && pick < 0; ++k)
        {
            const int i = (fv + k) % n;
            if (! entries[(size_t) i]->ready.load() && ! entries[(size_t) i]->failed) pick = i;
        }
        if (pick < 0) return;
        auto& e = *entries[(size_t) pick];
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (e.file));
        if (r == nullptr || r->lengthInSamples <= 0) { e.failed = true; e.ready = true; continue; }
        const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, 16384);
        juce::AudioBuffer<float> b ((int) r->numChannels, len);
        r->read (&b, 0, len, 0, true, true);
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < len; ++i) buf[(size_t) i] = b.getSample (0, i);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const double rate = r->sampleRate > 0 ? r->sampleRate : 48000.0;
        const double binHz = rate / 16384.0;
        float top = -200.0f;
        for (int k = 0; k < kPts; ++k)
        {
            // media della potenza nella banda di 1/10 di ottava attorno a ogni punto
            const double f = kLoHz * std::pow (kHiHz / kLoHz, (double) k / (kPts - 1));
            const int b0 = juce::jlimit (1, 8191, (int) (f / std::pow (2.0, 0.05) / binHz)), b1 = juce::jlimit (b0, 8191, (int) (f * std::pow (2.0, 0.05) / binHz));
            double pw = 0;
            for (int bin = b0; bin <= b1; ++bin) pw += (double) buf[(size_t) bin] * buf[(size_t) bin];
            const float db = (float) (10.0 * std::log10 (pw / (b1 - b0 + 1) + 1.0e-20));
            e.curve[(size_t) k] = db; top = std::max (top, db);
        }
        for (auto& v : e.curve) v = juce::jmax (-kRangeDb, v - top);
        e.detail = juce::String (rate / 1000.0, 1) + " kHz   " + juce::String (1000.0 * (double) r->lengthInSamples / rate, 0) + " ms"
                 + (r->numChannels > 1 ? "   stereo" : "");
        e.ready = true;
    }
}

void IrBrowser::timerCallback()
{
    // scorrimento morbido verso la posizione richiesta (rotella, trackpad, tastiera)
    const float before = scroll;
    scroll += (scrollTarget - scroll) * 0.35f;
    if (std::abs (scrollTarget - scroll) < 0.5f) scroll = scrollTarget;
    firstVisible = (int) (scroll / (float) kRowH);
    if (! juce::exactlyEqual (before, scroll) || isThreadRunning()) repaint();
}

juce::Rectangle<int> IrBrowser::folderArea() const
{
    const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (ui::face (ui::Face::SansSemiBold, ui::kControlTextSize), "folder")) + 18;
    return { closeArea().getX() - 16 - w, 25, w, ui::kControlH };
}

int IrBrowser::rowAt (juce::Point<int> p) const
{
    const auto l = listArea();
    if (! l.contains (p)) return -1;
    const int i = (int) ((p.y - l.getY() + scroll) / (float) kRowH);
    return juce::isPositiveAndBelow (i, (int) entries.size()) ? i : -1;
}

void IrBrowser::ensureVisible (int i)
{
    const float h = (float) listArea().getHeight(), y = (float) (i * kRowH);
    if (y < scrollTarget) scrollTarget = y;
    else if (y + kRowH > scrollTarget + h) scrollTarget = y + kRowH - h;
}

void IrBrowser::choose (int i, bool closeAfter)
{
    if (! juce::isPositiveAndBelow (i, (int) entries.size())) return;
    const char* letters[] = { "A", "B", "C", "D" };
    if (const int u = usedBy (i); u >= 0) { message = juce::String ("already loaded in ") + letters[u]; ensureVisible (i); repaint(); return; }
    message.clear();
    current = i;
    proc.loadIrFileAsync (slot, entries[(size_t) i]->file);
    ensureVisible (i);
    if (closeAfter) close();
    repaint();
}

void IrBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (closeArea().contains (e.getPosition())) { close(); return; }
    if (folderArea().contains (e.getPosition())) { if (onChangeFolder) onChangeFolder(); return; }
    choose (rowAt (e.getPosition()), false);
}

void IrBrowser::mouseDoubleClick (const juce::MouseEvent& e) { if (rowAt (e.getPosition()) >= 0) choose (rowAt (e.getPosition()), true); }

void IrBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float maxScroll = juce::jmax (0.0f, (float) ((int) entries.size() * kRowH - listArea().getHeight()));
    scrollTarget = juce::jlimit (0.0f, maxScroll, scrollTarget - w.deltaY * (w.isSmooth ? 400.0f : 160.0f));
    if (w.isSmooth) scroll = scrollTarget;   // trackpad: segue il dito
}

bool IrBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey) { close(); return true; }
    if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey)
    {
        const int dir = k == juce::KeyPress::downKey ? 1 : -1;
        int i = current + dir;
        while (juce::isPositiveAndBelow (i, (int) entries.size()) && usedBy (i) >= 0) i += dir;   // salta quelli già negli altri slot
        if (juce::isPositiveAndBelow (i, (int) entries.size())) choose (i, false);
        return true;
    }
    return false;
}

void IrBrowser::paint (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (background);
    const char* letters[] = { "A", "B", "C", "D" };
    drawLabel (g, juce::String ("load ") + letters[slot], { 24, 20, 120, 30 }, titleName, 22.0f, juce::Justification::centredLeft, Face::SansSemiBold);
    const float tw = juce::GlyphArrangement::getStringWidth (face (Face::SansSemiBold, 22.0f), juce::String ("load ") + letters[slot]);
    const int done = numAnalysed();
    drawLabel (g, folder.getFileName() + "   " + juce::String (entries.size()) + " IR" + (done < (int) entries.size() ? "   reading " + juce::String (done) + " / " + juce::String (entries.size()) : juce::String()),
               { 24 + (int) tw + 16, 20, folderArea().getX() - (int) tw - 56, 30 }, info, kInfoSize);
    // pulsante cartella e x
    const auto fa = folderArea(); const bool fh = fa.contains (getMouseXYRelative());
    g.setColour (fh ? labelHover : text); outline (g, fa.toFloat());
    drawLabel (g, "folder", fa, fh ? labelHover : selector, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
    {
        const auto c = closeArea().toFloat().getCentre();
        g.setColour (closeArea().contains (getMouseXYRelative()) ? labelHover : selector);
        g.drawLine (c.x - 6, c.y - 6, c.x + 6, c.y + 6, 1.2f); g.drawLine (c.x - 6, c.y + 6, c.x + 6, c.y - 6, 1.2f);
    }
    const auto l = listArea();
    drawLabel (g, message.isNotEmpty() ? message : juce::String ("while this list is open you hear only the selected IR"),
               { 24, 50, l.getWidth() - 400, 16 }, message.isNotEmpty() ? kWarn : info, kInfoSize, juce::Justification::centredLeft, Face::SansRegular);
    {   // assi della colonna dei grafici (una volta sola, sopra l'elenco)
        const auto ca = curveArea ({ l.getX(), 0, l.getWidth() - 8, kRowH });
        for (auto [hz, t] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
            drawAtBaseline (g, t, ca.getX() + ca.getWidth() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz), (float) l.getY() - 6.0f, info, kAxisSmall,
                            juce::Justification::horizontallyCentred);
    }
    g.setColour (frame); g.fillRect (l.getX(), l.getY() - 1, l.getWidth(), 1);
    if (entries.empty())
    {
        drawLabel (g, "no .wav, .aif or .flac files in this folder", l.withHeight (40), info, kInfoSize, juce::Justification::centred);
        return;
    }
    g.saveState();
    g.reduceClipRegion (l);
    const int first = juce::jmax (0, (int) (scroll / (float) kRowH)), last = juce::jmin ((int) entries.size() - 1, (int) ((scroll + (float) l.getHeight()) / (float) kRowH));
    const int hov = rowAt (getMouseXYRelative());
    for (int i = first; i <= last; ++i)
    {
        auto& e = *entries[(size_t) i];
        const auto r = juce::Rectangle<int> (l.getX(), l.getY() + juce::roundToInt ((float) (i * kRowH) - scroll), l.getWidth() - 8, kRowH);
        const bool cur = i == current;
        const int used = usedBy (i);
        if (cur) { g.setColour (accent.withAlpha (0.10f)); g.fillRect (r); g.setColour (accent); g.fillRect (r.getX(), r.getY() + 10, 3, kRowH - 20); }
        else if (used >= 0) { g.setColour (accent.withAlpha (0.05f)); g.fillRect (r); }
        else if (i == hov) { g.setColour (juce::Colour (0xff1a1a1a)); g.fillRect (r); }
        g.setColour (gridLine); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
        const auto ca = curveArea (r);
        auto textArea = r.withTrimmedLeft (14).withRight ((int) ca.getX() - 16);
        if (used >= 0)   // già in un altro slot: la sua lettera, e non si può scegliere
        {
            const auto badge = juce::Rectangle<int> (textArea.getX(), r.getCentreY() - 10, 20, 20);
            g.setColour (accent); g.fillRect (badge);
            drawLabel (g, letters[used], badge, onAccent, kControlTextSize, juce::Justification::centred, Face::SansBold);
            textArea.removeFromLeft (30);
        }
        const auto nameColour = cur ? titleName : used >= 0 ? info : selector;
        drawLabel (g, e.name, textArea.withHeight (30).translated (0, 8), nameColour, 14.0f, juce::Justification::centredLeft, Face::SansMedium);
        if (e.ready.load())
            drawLabel (g, e.failed ? juce::String ("not readable") : used >= 0 ? e.detail + "   in slot " + letters[used] : e.detail,
                       textArea.withTrimmedTop (38).withHeight (16), e.failed ? kWarn : info, kAxisSize, juce::Justification::centredLeft, Face::MonoRegular);
        // risposta in frequenza (20 Hz - 20 kHz, 36 dB) su una griglia appena visibile: decadi e passi di 12 dB
        g.setColour (juce::Colour (0xff191919));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
            g.fillRect (std::round (ca.getX() + ca.getWidth() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz)), ca.getY(), 1.0f, ca.getHeight());
        for (float db : { 12.0f, 24.0f })
            g.fillRect (ca.getX(), std::round (ca.getY() + ca.getHeight() * db / kRangeDb), ca.getWidth(), 1.0f);
        g.setColour (gridLine); g.drawRect (ca, 1.0f);
        if (e.ready.load() && ! e.failed)
        {
            std::array<juce::Point<float>, kPts> pts;
            for (int k = 0; k < kPts; ++k)
                pts[(size_t) k] = { ca.getX() + ca.getWidth() * (float) k / (kPts - 1),
                                    ca.getY() + ca.getHeight() * (-e.curve[(size_t) k] / kRangeDb) };
            g.setColour (cur ? accent : used >= 0 ? accent.withAlpha (0.45f) : (i == hov ? labelHover : selector));
            g.strokePath (smoothPath (pts.data(), kPts), juce::PathStrokeType (1.2f));
        }
    }
    // cursore della frequenza: una linea verticale che segue il mouse su tutti i grafici e, accanto al puntatore
    // (segue anche l'altezza del mouse, su qualunque riga), la frequenza puntata. Dove il testo tocca una curva
    // si vede in negativo (solo sui punti sovrapposti)
    {
        const bool fake = mouseForTest.x >= 0;
        const auto m = fake ? mouseForTest : getMouseXYRelative();
        const auto colArea = curveArea ({ l.getX(), 0, l.getWidth() - 8, kRowH });
        if ((fake || isMouseOver (true)) && l.contains (m) && m.x >= (int) colArea.getX() && m.x <= (int) colArea.getRight())
        {
            const float x = (float) m.x;
            const int hr = rowAt (m);
            auto rowRect = [&] (int i) { return juce::Rectangle<int> (l.getX(), l.getY() + juce::roundToInt ((float) (i * kRowH) - scroll), l.getWidth() - 8, kRowH); };
            for (int i = first; i <= last; ++i)
            {
                const auto ca = curveArea (rowRect (i));
                g.setColour (i == hr ? labelHover.withAlpha (0.8f) : labelHover.withAlpha (0.35f));
                g.fillRect (std::round (x), ca.getY() + 1.0f, 1.0f, ca.getHeight() - 2.0f);
            }
            const float hz = kLoHz * std::pow (kHiHz / kLoHz, juce::jlimit (0.0f, 1.0f, (x - colArea.getX()) / colArea.getWidth()));
            const juce::String freqLabel = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                                                    : juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0) + " kHz";
            const auto fnt = face (Face::MonoMedium, kAxisSize);
            const float labW = juce::GlyphArrangement::getStringWidth (fnt, freqLabel), capH = capHeight (kAxisSize, Face::MonoMedium);
            const bool leftSide = x + 6.0f + labW > colArea.getRight() - 2.0f;
            const float tx = leftSide ? x - 6.0f - labW : x + 6.0f;
            // centrata sull'altezza del mouse, sempre dentro l'elenco
            const float base = juce::jlimit ((float) l.getY() + 2.0f + capH, (float) l.getBottom() - 2.0f, (float) m.y + capH * 0.5f);
            drawAtBaseline (g, freqLabel, tx, base, valueText, kAxisSize, juce::Justification::left, Face::MonoMedium);
            // in negativo dove tocca le curve delle righe sotto l'etichetta (può stare a cavallo di due righe)
            juce::Path strokes;
            for (int i = first; i <= last; ++i)
            {
                auto& e = *entries[(size_t) i];
                const auto ca = curveArea (rowRect (i));
                if (! e.ready.load() || e.failed || base - capH > ca.getBottom() || base < ca.getY()) continue;
                std::array<juce::Point<float>, kPts> pts;
                for (int k = 0; k < kPts; ++k)
                    pts[(size_t) k] = { ca.getX() + ca.getWidth() * (float) k / (kPts - 1), ca.getY() + ca.getHeight() * (-e.curve[(size_t) k] / kRangeDb) };
                juce::Path one;
                juce::PathStrokeType (1.2f).createStrokedPath (one, smoothPath (pts.data(), kPts));
                strokes.addPath (one);
            }
            if (! strokes.isEmpty())
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (strokes);
                drawAtBaseline (g, freqLabel, tx, base, background, kAxisSize, juce::Justification::left, Face::MonoMedium);
            }
        }
    }
    g.restoreState();
    // barra di scorrimento
    const float total = (float) (entries.size() * kRowH);
    if (total > (float) l.getHeight())
    {
        const float h = (float) l.getHeight() * (float) l.getHeight() / total, y = (float) l.getY() + scroll / total * (float) l.getHeight();
        g.setColour (frame); g.fillRect ((float) l.getRight() - 3.0f, y, 3.0f, h);
    }
}

// ============================================================================ BlendView
// pad quadrato (forme regolari: triangolo equilatero, quadrato), centrato nella sua colonna con lo stesso
// margine su tutti i lati; il grafico della risposta parte alla stessa distanza dal pad
juce::Rectangle<float> BlendView::pad() const
{
    const float colW = (float) getHeight() - 20.0f;
    return juce::Rectangle<float> (0.0f, 0.0f, colW, colW).reduced (kPadMargin).translated (0.5f, 0.5f);
}

void BlendView::animate()
{
    // vertici: verso la forma attuale (punto, linea, triangolo, quadrato) in ~150 ms
    std::array<juce::Point<float>, 4> target;
    int count = 0; std::array<int, 4> idx {};
    for (int i = 0; i < 4; ++i) if (proc.getSlotName (i).isNotEmpty()) idx[(size_t) count++] = i;
    BBlendProcessor::shapeVertices (count, target);
    for (int i = 0; i < 4; ++i)
    {
        int k = -1; for (int j = 0; j < count; ++j) if (idx[(size_t) j] == i) k = j;
        const bool on = k >= 0;
        if (on)
        {
            if (firstFrame || alpha[(size_t) i] <= 0.01f) shown[(size_t) i] = target[(size_t) k];
            shown[(size_t) i] += (target[(size_t) k] - shown[(size_t) i]) * 0.3f;
        }
        alpha[(size_t) i] += ((on ? 1.0f : 0.0f) - alpha[(size_t) i]) * (firstFrame ? 1.0f : 0.3f);
    }
    firstFrame = false;
    repaint();
}

void BlendView::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto p = pad();
    auto toPx = [&] (juce::Point<float> v) { return juce::Point<float> (p.getX() + v.x * p.getWidth(), p.getBottom() - v.y * p.getHeight()); };
    int count = 0; std::array<int, 4> idx {};
    for (int i = 0; i < 4; ++i) if (proc.getSlotName (i).isNotEmpty()) idx[(size_t) count++] = i;

    // --- forma del blend
    if (count == 0)
    {
        // senza IR il plugin resta acceso ma l'uscita è muta: avviso ben visibile
        drawLabel (g, "no IR loaded: audio muted", p.toNearestInt().translated (0, -10), juce::Colour (0xfff59e0b), 15.0f, juce::Justification::centred, Face::SansSemiBold);
        drawLabel (g, "load an impulse response above", p.toNearestInt().translated (0, 14), info, kInfoSize, juce::Justification::centred, Face::SansRegular);
    }
    else
    {
        // lati della forma (in ordine: linea, triangolo, quadrato A-B-D-C)
        juce::Path edge;
        std::array<int, 4> order = count == 4 ? std::array<int, 4> { idx[0], idx[1], idx[3], idx[2] } : idx;
        for (int k = 0; k < count; ++k)
        {
            const auto q = toPx (shown[(size_t) order[(size_t) k]]);
            if (k == 0) edge.startNewSubPath (q); else edge.lineTo (q);
        }
        if (count >= 3) edge.closeSubPath();
        if (count >= 2) { g.setColour (text); g.strokePath (edge, juce::PathStrokeType (1.0f)); }
        const char* letters[] = { "A", "B", "C", "D" };
        for (int i = 0; i < 4; ++i)
        {
            if (alpha[(size_t) i] < 0.02f) continue;
            const auto q = toPx (shown[(size_t) i]);
            g.setColour (text.withAlpha (alpha[(size_t) i]));
            g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre (q));
            // nome dell'IR fuori dalla forma: sopra i vertici alti e quelli della linea, sotto quelli bassi;
            // a sinistra allineato a sinistra, a destra allineato a destra, al centro centrato (mai sopra i lati)
            // lettera in grassetto nel colore del plugin + nome chiaro (accorciato), come un'unica etichetta
            const auto lf = face (Face::SansBold, kControlTextSize), nf = face (Face::SansMedium, kInfoSize);
            const auto v = shown[(size_t) i];
            const float lw = juce::GlyphArrangement::getStringWidth (lf, letters[i]) + 6.0f;
            // due etichette sulla stessa riga (linea, base del triangolo, quadrato): ognuna al massimo metà pad
            const float maxW = count == 1 || (v.x > 0.4f && v.x < 0.6f) ? p.getWidth() + 2.0f * kPadMargin - 8.0f : p.getWidth() * 0.5f - 6.0f;
            // nomi lunghi: due righe (a capo tra le parole), sempre entro la stessa larghezza
            const auto full = proc.getSlotName (i);
            const float avail = maxW - lw;
            juce::String l1 = full, l2;
            if (juce::GlyphArrangement::getStringWidth (nf, full) > avail)
            {
                const auto words = juce::StringArray::fromTokens (full, " _-", "");
                l1 = {};
                int w = 0;
                for (; w < words.size(); ++w)
                {
                    const auto t = l1.isEmpty() ? words[w] : l1 + " " + words[w];
                    if (l1.isNotEmpty() && juce::GlyphArrangement::getStringWidth (nf, t) > avail) break;
                    l1 = t;
                }
                for (int k = w; k < words.size(); ++k) l2 += (l2.isEmpty() ? "" : " ") + words[k];
                l1 = fitText (l1, nf, avail); l2 = fitText (l2, nf, avail);
            }
            const bool below = count >= 3 && v.y < 0.4f;
            // sopra un vertice alto due righe possono uscire dal riquadro (quadrato, triangolo): in quel caso
            // una riga sola, accorciata, e comunque mai sopra il bordo
            const float capTop = capHeight (kControlTextSize, Face::SansBold) + 2.0f;
            if (! below && l2.isNotEmpty() && q.y - 10.0f - 16.0f < capTop) { l1 = fitText (full, nf, avail); l2 = {}; }
            const auto nm = l1;
            const float tw = lw + juce::jmax (juce::GlyphArrangement::getStringWidth (nf, l1), juce::GlyphArrangement::getStringWidth (nf, l2));
            const auto just = count == 1 || (v.x > 0.4f && v.x < 0.6f) ? juce::Justification::horizontallyCentred
                                                                      : (v.x >= 0.6f ? juce::Justification::right : juce::Justification::left);
            const float x0 = just == juce::Justification::right ? q.x + 4.0f - tw : (just == juce::Justification::left ? q.x - 4.0f : q.x - tw * 0.5f);
            const float lineH = 16.0f;
            float by = below ? q.y + 20.0f : q.y - 10.0f - (l2.isEmpty() ? 0.0f : lineH);   // sopra: la 2ª riga resta vicino al vertice
            if (! below) by = juce::jmax (by, capTop);
            else by = juce::jmin (by, (float) getHeight() - 4.0f - (l2.isEmpty() ? 0.0f : lineH));
            drawAtBaseline (g, letters[i], x0, by, accent.withAlpha (alpha[(size_t) i]), kControlTextSize, juce::Justification::left, Face::SansBold);
            drawAtBaseline (g, nm, x0 + lw, by, valueText.withAlpha (alpha[(size_t) i]), kInfoSize, juce::Justification::left, Face::SansMedium);
            if (l2.isNotEmpty()) drawAtBaseline (g, l2, x0 + lw, by + lineH, valueText.withAlpha (alpha[(size_t) i]), kInfoSize, juce::Justification::left, Face::SansMedium);
        }
        // nodo (dentro la forma)
        if (count >= 2)
        {
            const auto c = BBlendProcessor::constrain (proc.apvts.getRawParameterValue (BBlendIDs::x)->load(),
                                                       proc.apvts.getRawParameterValue (BBlendIDs::y)->load(), count);
            const auto node = toPx (c);
            g.setColour (accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (14.0f, 14.0f).withCentre (node));
            g.setColour (accent);
            g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre (node));
        }
    }

    // --- risposta: IR composito (normalizzato a 0 dB) + thump dinamico, air e tagli
    const auto a = juce::Rectangle<float>::leftTopRightBottom (p.getRight() + kPadMargin + 40.0f, 0.5f, (float) getWidth() - 12.0f, (float) getHeight() - 20.5f);
    auto xFor = [&] (double hz) { return a.getX() + a.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (float db) { return a.getY() + a.getHeight() * (6.0f - db) / 42.0f; };   // +6 .. -36 dB
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f })
    {
        g.setColour (juce::exactlyEqual (db, 0.0f) ? gridZero : gridLine);
        g.fillRect (a.getX(), std::round (yFor (db)), a.getWidth(), kLine);
        drawLabel (g, juce::String ((int) db), juce::Rectangle<float> (a.getX() - 40.0f, yFor (db) - 8.0f, 32.0f, 16.0f).toNearestInt(),
                   info, kAxisSize, juce::Justification::centredRight, Face::MonoRegular);
    }
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    for (auto [hz, t] : { std::pair<double, const char*> { 20, "20" }, { 100, "100" }, { 1000, "1k" }, { 8000, "8k" }, { 20000, "20k" } })
    {
        g.setColour (gridLine);
        g.fillRect (std::round (xFor (hz)), a.getY(), kLine, a.getHeight());
        drawAtBaseline (g, t, xFor (hz), base, info, kAxisSize, juce::Justification::horizontallyCentred);
    }
    if (count == 0) return;   // a riposo: nessuna curva
    // curva ricalcolata solo quando cambia qualcosa (nuovo IR composito, filtri, thump dinamico)
    const juce::int64 key = ((((juce::int64) proc.getResponseVersion() * 1000003 + (juce::int64) (proc.thumpDb.load() * 20.0f)) * 1009
                             + (juce::int64) proc.apvts.getRawParameterValue (BBlendIDs::air)->load()) * 100003
                             + (juce::int64) proc.apvts.getRawParameterValue (BBlendIDs::locut)->load()) * 100003
                             + (juce::int64) proc.apvts.getRawParameterValue (BBlendIDs::hicut)->load() + (juce::int64) a.getWidth() * 7919;
    if (key != curveKey)
    {
        curveKey = key;
        curve.clear();
        for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth()));
            const float y = yFor (juce::jlimit (-40.0f, 8.0f, proc.irResponseDb (hz) + proc.filtersDb (hz)));
            if (x <= a.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
    }
    const auto& path = curve;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (a.toNearestInt());
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

void BlendView::setXY (juce::Point<float> q)
{
    const auto p = pad();
    proc.apvts.getParameter (BBlendIDs::x)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (q.x - p.getX()) / p.getWidth()));
    proc.apvts.getParameter (BBlendIDs::y)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (p.getBottom() - q.y) / p.getHeight()));
    repaint();
}

void BlendView::mouseDown (const juce::MouseEvent& e)
{
    if (! pad().expanded (8.0f).contains (e.position)) return;
    dragging = true;
    proc.apvts.getParameter (BBlendIDs::x)->beginChangeGesture();
    proc.apvts.getParameter (BBlendIDs::y)->beginChangeGesture();
    setXY (e.position);
}

void BlendView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setXY (e.position); }

// doppio clic sul nodo: torna al centro geometrico della forma (linea: metà, triangolo: baricentro, quadrato: centro)
void BlendView::mouseDoubleClick (const juce::MouseEvent& e)
{
    int count = 0; for (int i = 0; i < 4; ++i) if (proc.getSlotName (i).isNotEmpty()) ++count;
    if (count < 2) return;
    const auto p = pad();
    const auto c = BBlendProcessor::constrain (proc.apvts.getRawParameterValue (BBlendIDs::x)->load(),
                                               proc.apvts.getRawParameterValue (BBlendIDs::y)->load(), count);
    const juce::Point<float> node (p.getX() + c.x * p.getWidth(), p.getBottom() - c.y * p.getHeight());
    if (node.getDistanceFrom (e.position) > 14.0f) return;
    const auto ctr = BBlendProcessor::centreOf (count);
    for (auto [id, v] : { std::pair<const char*, float> { BBlendIDs::x, ctr.x }, { BBlendIDs::y, ctr.y } })
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (v); prm->endChangeGesture();
    }
    repaint();
}

void BlendView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    proc.apvts.getParameter (BBlendIDs::x)->endChangeGesture();
    proc.apvts.getParameter (BBlendIDs::y)->endChangeGesture();
}

// ============================================================================ editor
void BBlendEditor::tick()
{
    view.animate();
    for (auto& l : loaders) l->repaint();
}

void BBlendEditor::chooseFolder (int slot)
{
    // si riapre nell'ultima cartella scelta (o in quella dell'IR già caricato)
    auto start = proc.getLastFolder();
    if (proc.getSlotFile (slot).existsAsFile()) start = proc.getSlotFile (slot).getParentDirectory();
    folderChooser = std::make_unique<juce::FileChooser> ("Choose a folder of impulse responses", start);
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [this, slot] (const juce::FileChooser& fc)
                                {
                                    const auto d = fc.getResult();
                                    if (! d.isDirectory()) return;
                                    proc.setBrowseFolder (slot, d);
                                    browser.setBounds (getLocalBounds());
                                    browser.onChangeFolder = [this, slot] { chooseFolder (slot); };
                                    browser.open (slot, d);
                                });
}

void BBlendEditor::resized()
{
    ShellEditor::resized();
    browser.setBounds (getLocalBounds());
    // loader allineati a destra sulla riga del titolo, fino al bordo destro del riquadro
    const auto b = box();
    const int right = b.getRight() - 1 - ui::kFramePad, w = 118, gap = ui::kGapLabel;
    const int y = ui::headerRowY();
    for (int i = 0; i < 4; ++i)
        loaders[(size_t) i]->setBounds (right - (4 - i) * w - (3 - i) * gap, y, w, ui::kControlH);
    ui::centreHeaderInRig (getWidth(), { loaders[0].get(), loaders[1].get(), loaders[2].get(), loaders[3].get() });
}

BBlendEditor::BBlendEditor (BBlendProcessor& p)
    : ShellEditor (p, p.apvts, "blend", 760, 430), proc (p), view (p), browser (p)
{
    for (int i = 0; i < 4; ++i)
    {
        loaders[(size_t) i] = std::make_unique<IrLoader> (p, i);
        loaders[(size_t) i]->onBrowse = [this] (int s, const juce::File& dir)
        {
            if (! dir.isDirectory()) { chooseFolder (s); return; }
            browser.setBounds (getLocalBounds());
            browser.onChangeFolder = [this, s] { chooseFolder (s); };
            browser.open (s, dir);
        };
        addAndMakeVisible (*loaders[(size_t) i]);
    }
    addChildComponent (browser);
    using namespace ui;
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto stepPct = [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
    addValue ("thump", BBlendIDs::thump, pct, stepPct, { "100 %" });
    addValue ("air", BBlendIDs::air, pct, stepPct, { "100 %" });
    addValue ("locut", BBlendIDs::locut, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; },
              [] (float v, int d) { return v < 20.0f ? (d > 0 ? 20.0f : 0.0f) : std::round (v * std::pow (2.0f, (float) d / 6.0f)); }, { "400 Hz" });
    addValue ("hicut", BBlendIDs::hicut, [] (float v) { return freqText (v); },
              [] (float v, int d) { return v * std::pow (2.0f, (float) d / 6.0f); }, { "20.0 kHz", "8.88 kHz" });
    infoText = p.getTotalNumInputChannels() == 1 ? (p.getTotalNumOutputChannels() == 1 ? "mono" : "mono > stereo") : "stereo";
    setDisplay (view);
    resized();
}

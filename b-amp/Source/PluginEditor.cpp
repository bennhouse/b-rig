#include "PluginEditor.h"

using namespace ui;
namespace
{
    const juce::Colour kBg { 0xff0d0d0f };
    const juce::Colour kPattern { 0xff242428 };   // stessa luminosità media per le tre griglie (pesi visivi uguali)
    constexpr int kW = 1000, kH = 492, kMargin = 24;
    int tw (const juce::String& s) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (kLabelSize), s)); }
    const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::Colour BAmpEditor::channelColour (int ch) { return ch == 0 ? juce::Colour (0xff38bdf8) : (ch == 1 ? juce::Colour (0xfff97316) : juce::Colour (0xffe11d48)); }

// ============================================================================ testata
// corpo simmetrico; sopra la maniglia, sotto i piedini: stessa posizione e misura nelle tre testate
juce::Rectangle<float> AmpHead::bodyArea() const
{
    auto b = getLocalBounds().toFloat().reduced (1.0f, 0.0f);
    b.removeFromTop (kHandleH); b.removeFromBottom (kFeetH);
    return b.reduced (0.0f, 0.5f);
}

juce::Rectangle<float> AmpHead::grilleArea() const { return bodyArea().reduced (16.0f, 14.0f); }

juce::Rectangle<float> AmpHead::ledArea() const
{
    const auto grille = grilleArea();
    return juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ grille.getX() + 14.0f, grille.getY() + 14.0f });   // spia in alto a sinistra
}

void AmpHead::mouseDown (const juce::MouseEvent& e)
{
    if (back) { back = false; repaint(); return; }
    if (ledArea().expanded (9.0f).contains (e.position)) { back = true; repaint(); }
}

void AmpHead::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (back || ledArea().expanded (9.0f).contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void AmpHead::paintCabinet (juce::Graphics& g, int ch)
{
    const auto col = BAmpEditor::channelColour (ch);
    const auto body = bodyArea();
    const float cx = body.getCentreX();
    // maniglia: un semplice ponte centrato sopra il corpo
    {
        const float hw = 56.0f, top = body.getY() - kHandleH + 3.0f;
        juce::Path h;
        h.startNewSubPath (cx - hw, body.getY()); h.lineTo (cx - hw, top); h.lineTo (cx + hw, top); h.lineTo (cx + hw, body.getY());
        g.setColour (frame); g.strokePath (h, juce::PathStrokeType (1.0f));
    }
    // piedini
    for (float fx : { body.getX() + 22.0f, body.getRight() - 22.0f - 30.0f })
    {
        const juce::Rectangle<float> foot (fx, body.getBottom(), 30.0f, kFeetH - 1.0f);
        g.setColour (gridZero); g.fillRect (foot);
        g.setColour (frame); outline (g, foot);
    }
    // corpo e seconda cornice interna (stesso peso visivo nelle tre)
    g.setColour (frame);
    outline (g, body);
    g.setColour (ch == 1 ? col.withAlpha (0.35f) : gridZero);
    outline (g, body.reduced (5.0f));
}

void AmpHead::paintStatic (juce::Graphics& g, int ch)
{
    const auto col = BAmpEditor::channelColour (ch);
    const auto grille = grilleArea();
    const float cx = grille.getCentreX(), cy = grille.getCentreY();
    paintCabinet (g, ch);
    g.setColour (gridZero); outline (g, grille);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (grille.toNearestInt());
        g.setColour (kPattern);
        if (ch == 0)   // clean: lamelle verticali, una centrata sull'asse
        {
            for (float k = 0; cx + k * 9.0f - 1.5f < grille.getRight(); ++k)
            {
                g.fillRect (cx + k * 9.0f - 1.5f, grille.getY(), 3.0f, grille.getHeight());
                if (k > 0) g.fillRect (cx - k * 9.0f - 1.5f, grille.getY(), 3.0f, grille.getHeight());
            }
        }
        else if (ch == 1)   // crunch: tela intrecciata, incrocio centrato sull'asse
        {
            const float h = grille.getHeight();
            for (float d = -grille.getWidth() - h; d < grille.getWidth() + h; d += 9.0f)
            {
                g.drawLine (cx + d - h * 0.5f, grille.getY(), cx + d + h * 0.5f, grille.getBottom(), 1.0f);
                g.drawLine (cx - d + h * 0.5f, grille.getY(), cx - d - h * 0.5f, grille.getBottom(), 1.0f);
            }
        }
        else   // lead: metallo forato, file sfalsate centrate sull'asse
        {
            int row = 0;
            for (float y = cy - std::floor ((cy - grille.getY()) / 9.0f) * 9.0f; y < grille.getBottom(); y += 9.0f, ++row)
            {
                const float off = (row % 2 == 0) ? 0.0f : 4.5f;
                for (float k = 0; cx + off + k * 9.0f < grille.getRight(); ++k)
                {
                    g.fillEllipse (cx + off + k * 9.0f - 1.9f, y - 1.9f, 3.8f, 3.8f);
                    g.fillEllipse (cx - off - k * 9.0f - 1.9f, y - 1.9f, 3.8f, 3.8f);
                }
            }
        }
    }
    // nome del canale direttamente sulla griglia, in basso a destra (senza sfondo)
    const char* names[3] = { "clean  /  high headroom", "crunch  /  british", "lead  /  modern high gain" };
    drawAtBaseline (g, names[ch], grille.getRight() - 12.0f, grille.getBottom() - 10.0f, col.withAlpha (0.9f), kTagSize, juce::Justification::right, Face::SansSemiBold);
}

// valvole dietro la fessura, in proporzioni reali e appoggiate sullo chassis:
//   finali EL34 / 6L6GC: vetro Ø 32 mm, alte ~110 mm (zoccolo octal in bachelite compreso) -> 1 : 3,3
//   preamplificatrici ECC83 / 12AX7 (noval): vetro Ø 22 mm, alte ~56 mm, senza zoccolo     -> 1 : 2,5
// (una finale è circa 1,5 volte più larga e 2 volte più alta di una preamplificatrice)
std::vector<juce::Rectangle<float>> AmpHead::tubeRects (int ch) const
{
    const auto slot = backSlot();
    std::vector<juce::Rectangle<float>> r;
    const int nPre = ch == 2 ? 5 : 4;
    const float floorY = slot.getBottom() - 8.0f;            // piano dello chassis
    const float pw = 32.0f, ph = 106.0f, sw = 21.0f, sh = 52.0f;
    float x = slot.getX() + 74.0f;                            // a sinistra il trasformatore di alimentazione
    for (int i = 0; i < 4; ++i, x += pw + 7.0f) r.push_back ({ x, floorY - ph, pw, ph });
    x = slot.getRight() - 74.0f - (float) nPre * (sw + 5.0f) + 5.0f;   // a destra il trasformatore d'uscita
    for (int i = 0; i < nPre; ++i, x += sw + 5.0f) r.push_back ({ x, floorY - sh, sw, sh });
    return r;
}

void AmpHead::paintBackStatic (juce::Graphics& g, int ch)
{
    paintCabinet (g, ch);
    // interno visto dal retro: chassis, trasformatori e valvole (i filamenti si accendono nel paint)
    const auto slot = backSlot();
    g.setColour (juce::Colour (0xff09090b)); g.fillRect (slot);
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (slot.toNearestInt());
    g.setColour (juce::Colour (0xff1a1a1d)); g.fillRect (slot.withTrimmedTop (slot.getHeight() - 8.0f));
    for (auto tr : { juce::Rectangle<float> (slot.getX() + 12.0f, slot.getBottom() - 8.0f - 72.0f, 50.0f, 72.0f),
                     juce::Rectangle<float> (slot.getRight() - 62.0f, slot.getBottom() - 8.0f - 60.0f, 50.0f, 60.0f) })
    {
        g.setColour (juce::Colour (0xff1f1f23)); g.fillRect (tr);
        g.setColour (frame); outline (g, tr);
        g.setColour (gridZero);
        for (float yy = tr.getY() + 6.0f; yy < tr.getBottom() - 4.0f; yy += 4.0f) g.fillRect (tr.getX() + 6.0f, yy, tr.getWidth() - 12.0f, 1.0f);
    }
    const auto tubes = tubeRects (ch);
    for (size_t i = 0; i < tubes.size(); ++i)
    {
        const auto t = tubes[i];
        const bool power = i < 4;
        // finale: zoccolo octal in bachelite (più stretto del vetro, ~20 % dell'altezza); preamp: niente zoccolo
        auto glass = t;
        if (power)
        {
            const auto base = glass.removeFromBottom (t.getHeight() * 0.2f).reduced (2.0f, 0.0f);
            g.setColour (juce::Colour (0xff1c1c1f)); g.fillRect (base);
            g.setColour (frame); outline (g, base);
        }
        else g.setColour (frame), g.fillRect (t.getX() + 3.0f, t.getBottom() - 2.0f, t.getWidth() - 6.0f, 2.0f);   // piedini
        g.setColour (juce::Colour (0xff141417)); g.fillRect (glass);
        g.setColour (frame); outline (g, glass);
        // punta di vetro sigillata in cima
        g.fillRect (glass.getCentreX() - 1.5f, glass.getY() - 3.0f, 3.0f, 3.0f);
        // placca (dentro il vetro) e, nelle preamp, il getter argentato in cima
        const auto plate = power ? glass.reduced (glass.getWidth() * 0.2f, 0.0f).withTrimmedTop (glass.getHeight() * 0.18f).withTrimmedBottom (glass.getHeight() * 0.12f)
                                 : glass.reduced (glass.getWidth() * 0.22f, 0.0f).withTrimmedTop (glass.getHeight() * 0.3f).withTrimmedBottom (glass.getHeight() * 0.12f);
        g.setColour (gridZero); outline (g, plate);
        if (! power) { g.setColour (juce::Colour (0xff2e2e33)); g.fillRect (glass.getX() + 1.0f, glass.getY() + 1.0f, glass.getWidth() - 2.0f, glass.getHeight() * 0.16f); }
    }
}

// copertura del retro, ispirata alle testate di riferimento (nessuna scritta):
//   clean: pannello posteriore pieno sulla metà bassa, si vedono le cime delle valvole
//   crunch: gabbia di sbarre verticali davanti a tutto
//   lead: pannello a feritoie orizzontali alternate (metà aperto, metà chiuso)
void AmpHead::paintBackCover (juce::Graphics& g, int ch)
{
    const auto slot = backSlot();
    const juce::Colour panel (0xff17171a);
    if (ch == 0)
    {
        const auto low = slot.withTop (slot.getBottom() - 8.0f - 26.0f);   // copre metà delle preamp e la base delle finali
        g.setColour (panel); g.fillRect (low);
        g.setColour (frame); g.fillRect (low.getX(), low.getY(), low.getWidth(), kLine);
        g.setColour (gridZero);
        for (float x : { low.getX() + 18.0f, low.getRight() - 18.0f }) { g.fillEllipse (x - 2.0f, low.getCentreY() - 2.0f, 4.0f, 4.0f); }
    }
    else if (ch == 1)
    {
        g.setColour (juce::Colour (0xff2a2a2e));
        for (float x = slot.getX() + 6.0f; x < slot.getRight() - 2.0f; x += 11.0f) g.fillRect (x, slot.getY(), 3.0f, slot.getHeight());
        g.fillRect (slot.getX(), slot.getCentreY() - 1.5f, slot.getWidth(), 3.0f);
    }
    else
    {
        const float h = slot.getHeight() / 9.0f;
        for (int k = 0; k < 9; ++k)
        {
            if (k % 2 == 0) continue;   // feritoie aperte alternate
            const juce::Rectangle<float> slat (slot.getX(), slot.getY() + h * (float) k, slot.getWidth(), h);
            g.setColour (panel); g.fillRect (slat);
            g.setColour (gridZero); g.fillRect (slat.getX(), slat.getY(), slat.getWidth(), kLine);
        }
    }
    g.setColour (frame); outline (g, slot);
}

void AmpHead::paint (juce::Graphics& g)
{
    const int ch = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (BAmpIDs::channel)->load());
    const auto col = BAmpEditor::channelColour (ch);
    if (back)
    {
        statics.draw (g, getLocalBounds(), ch + 10, [this, ch] (juce::Graphics& sg) { paintBackStatic (sg, ch); });
        // filamenti accesi: sempre un minimo (ampli acceso), più luminosi con la saturazione
        {
            const auto slot = backSlot();
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (slot.toNearestInt());
            const auto tubes = tubeRects (ch);
            for (size_t i = 0; i < tubes.size(); ++i)
            {
                const auto t = i < 4 ? tubes[i].withTrimmedBottom (tubes[i].getHeight() * 0.2f) : tubes[i].withTrimmedTop (tubes[i].getHeight() * 0.16f);
                const auto glowR = t.reduced (t.getWidth() * 0.3f, 0.0f).withTrimmedTop (t.getHeight() * 0.35f).withTrimmedBottom (t.getHeight() * 0.3f);
                juce::ColourGradient gr (col.withAlpha (0.35f + 0.5f * glow), glowR.getCentreX(), glowR.getCentreY(), col.withAlpha (0.0f), glowR.getCentreX(), t.getY(), true);
                g.setGradientFill (gr); g.fillRect (t.reduced (1.0f));
                g.setColour (col.withAlpha (0.55f + 0.45f * glow)); g.fillRect (glowR.getCentreX() - 1.0f, glowR.getY(), 2.0f, glowR.getHeight());
            }
        }
        cover.draw (g, getLocalBounds(), ch + 20, [this, ch] (juce::Graphics& sg) { paintBackCover (sg, ch); });
        return;
    }
    statics.draw (g, getLocalBounds(), ch, [this, ch] (juce::Graphics& sg) { paintStatic (sg, ch); });
    const auto grille = grilleArea();
    // bagliore interno: proporzionale alla saturazione (clip degli stadi)
    if (glow > 0.01f)
    {
        juce::ColourGradient grad (col.withAlpha (0.28f * glow), grille.getCentreX(), grille.getCentreY(), col.withAlpha (0.0f), grille.getX(), grille.getY(), true);
        g.setGradientFill (grad); g.fillRect (grille);
    }
    // spia del gate in alto a sinistra (accesa = gate aperto; gate disattivato = sempre accesa); cliccabile
    g.setColour (col.withAlpha (0.25f + 0.75f * proc.gateGain.load())); g.fillEllipse (ledArea());
}

// ============================================================================ tuner
void TunerOverlay::analyse()
{
    int s1, n1, s2, n2;
    const int ready = proc.tunerFifo.getNumReady();
    proc.tunerFifo.prepareToRead (ready, s1, n1, s2, n2);
    auto take = [&] (int st, int c) { for (int i = 0; i < c; ++i) { win[(size_t) wpos] = proc.tunerBuf[(size_t) (st + i)]; wpos = (wpos + 1) & 4095; } filled = juce::jmin (4096, filled + c); };
    take (s1, n1); take (s2, n2);
    proc.tunerFifo.finishedRead (n1 + n2);
    if (filled < 2048 || ! isVisible()) return;
    static thread_local std::array<float, 2048> lin;
    for (int i = 0; i < 2048; ++i) lin[(size_t) i] = win[(size_t) ((wpos - 2048 + i) & 4095)];
    float pk = 0; for (float v : lin) pk = juce::jmax (pk, std::abs (v));
    if (pk < 0.002f) { hz = 0; clarity = 0; midi = -1; return; }
    const double fsd = proc.tunerRate();
    const float f = amp::mpmFrequency (lin.data(), 2048, fsd, (int) (fsd / 27.5) + 2, clarity);
    if (f < 27.0f || f > 1320.0f) { hz = 0; midi = -1; return; }
    hz = hz > 0 ? hz + 0.5f * (f - hz) : f;
    const float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    midi = juce::roundToInt (m);
    cents = (m - (float) midi) * 100.0f;
    shownCents += 0.35f * (cents - shownCents);
}

void TunerOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (muteArea().contains (e.getPosition())) { proc.tunerMute.store (! proc.tunerMute.load()); repaint(); return; }
    if (onClose) onClose();
}

void TunerOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf20d0d0f));
    const auto a = getLocalBounds().toFloat();
    const auto col = accentOf (*this);
    const bool have = hz > 0 && midi >= 0;
    const juce::String note = have ? juce::String (kNoteNames[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1) : "--";
    const bool inTune = have && std::abs (shownCents) < 3.0f;
    drawAtBaseline (g, note, a.getCentreX(), a.getCentreY() - 40.0f, inTune ? col : valueText, 96.0f, juce::Justification::horizontallyCentred, Face::SansBold);
    drawAtBaseline (g, have ? num (hz, 1) + " Hz" : juce::String ("play a string"), a.getCentreX(), a.getCentreY() + 4.0f, info, kValueSize, juce::Justification::horizontallyCentred, Face::MonoMedium);
    // barra: -50 ... +50 cents, ago; strobe = tacche che scorrono con lo scarto
    const auto bar = juce::Rectangle<float> (a.getCentreX() - 220.0f, a.getCentreY() + 40.0f, 440.0f, 28.0f);
    g.setColour (gridZero); outline (g, bar);
    for (int c = -50; c <= 50; c += 10) { const float x = bar.getX() + (c + 50) / 100.0f * bar.getWidth(); g.setColour (c == 0 ? text : gridZero); g.fillRect (std::round (x), bar.getY(), kLine, bar.getHeight()); }
    if (have)
    {
        const float x = bar.getX() + juce::jlimit (0.0f, 1.0f, (shownCents + 50.0f) / 100.0f) * bar.getWidth();
        g.setColour (inTune ? col : (std::abs (shownCents) < 10.0f ? col.withAlpha (0.7f) : juce::Colour (0xffff4d4d)));
        g.fillRect (std::round (x) - 1.5f, bar.getY() - 6.0f, 3.0f, bar.getHeight() + 12.0f);
        const float ph = std::fmod ((float) juce::Time::getMillisecondCounter() * 0.001f * shownCents * 0.6f, 12.0f);   // strobe
        g.setColour (col.withAlpha (0.35f));
        for (float sx = bar.getX() + ph; sx < bar.getRight(); sx += 12.0f) g.fillRect (sx, bar.getBottom() + 10.0f, 6.0f, 3.0f);
        drawAtBaseline (g, (shownCents >= 0 ? "+" : "") + num (shownCents, 1) + " ct", a.getCentreX(), bar.getBottom() + 36.0f, info, kAxisSize, juce::Justification::horizontallyCentred, Face::MonoRegular);
    }
    drawAtBaseline (g, "-50", bar.getX(), bar.getY() - 8.0f, info, kAxisSmall, juce::Justification::left);
    drawAtBaseline (g, "+50", bar.getRight(), bar.getY() - 8.0f, info, kAxisSmall, juce::Justification::right);
    const bool m = proc.tunerMute.load();
    const auto mb = muteArea().toFloat();
    if (m) { g.setColour (col); g.fillRect (mb); drawLabel (g, "mute", muteArea(), onAccent, kControlTextSize, juce::Justification::centred, Face::SansSemiBold); }
    else { g.setColour (text); outline (g, mb); drawLabel (g, "mute", muteArea(), selector, kControlTextSize, juce::Justification::centred, Face::SansSemiBold); }
    drawAtBaseline (g, "click anywhere to close", a.getCentreX(), a.getBottom() - 16.0f, info, kAxisSmall, juce::Justification::horizontallyCentred, Face::SansRegular);
}

// ============================================================================ pulsanti
void SmallButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat(); const auto col = accentOf (*this);
    const bool on = isOn && isOn();
    if (on) { g.setColour (col); g.fillRect (r); }
    else { g.setColour (isMouseOver() ? labelHover : text); outline (g, r); }
    if (caption == "tuner")   // icona: diapason
    {
        const float cx = r.getCentreX(), cy = r.getCentreY();
        g.setColour (on ? onAccent : (isMouseOver() ? labelHover : selector));
        g.drawLine (cx, cy - 1.0f, cx, cy + 6.0f, 1.5f);
        g.drawLine (cx - 3.5f, cy - 6.0f, cx - 3.5f, cy - 1.0f, 1.5f); g.drawLine (cx + 3.5f, cy - 6.0f, cx + 3.5f, cy - 1.0f, 1.5f);
        g.drawLine (cx - 3.5f, cy - 1.0f, cx + 3.5f, cy - 1.0f, 1.5f);
    }
    else drawLabel (g, caption, getLocalBounds(), on ? onAccent : selector, kControlTextSize, juce::Justification::centred, on ? Face::MonoSemiBold : Face::SansSemiBold);
}

// ============================================================================ editor
namespace { const char* kBoostNames[3] = { "off", "vol", "vol + eq" }; constexpr int kBoostW = 76; }

BAmpEditor::BAmpEditor (BAmpProcessor& p)
    : AudioProcessorEditor (&p), proc (p), head (p), tuner (p),
      channelChoice (*p.apvts.getParameter (BAmpIDs::channel), { "clean", "crunch", "lead" }),
      osChoice (*p.apvts.getParameter (BAmpIDs::os), { "2x", "4x" })
{
    const char* ids[10] = { BAmpIDs::gate, BAmpIDs::tight, BAmpIDs::gain, BAmpIDs::bass, BAmpIDs::mid, BAmpIDs::treble, BAmpIDs::presence, BAmpIDs::resonance, BAmpIDs::master, BAmpIDs::level };
    for (int i = 0; i < 10; ++i)
    {
        auto& vb = boxes[(size_t) i];
        auto* prm = p.apvts.getParameter (ids[i]);
        vb.setParameter (prm);
        const bool db = i == 0 || i == 9;
        vb.format = db ? std::function<juce::String (float)> ([] (float v) { return num (v, 1) + " dB"; }) : [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
        vb.step = db ? std::function<float (float, int)> ([] (float v, int d) { return std::round (v) + (float) d; }) : [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
        vb.drag = [prm] (float, float s, float dx) { return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * 0.003f)); };
        vb.sizeSamples = db ? juce::StringArray { "-100.0 dB" } : juce::StringArray { "100 %" };
        // i controlli di ogni testata tornano al default di quella testata (doppio clic)
        if (i >= 1 && i <= 8) vb.defaultValue = [this, id = ids[i]] { return BAmpProcessor::headDefault (id, juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (BAmpIDs::channel)->load())); };
        addAndMakeVisible (vb);
    }
    addAndMakeVisible (head); head.toBack();
    addAndMakeVisible (channelChoice); addAndMakeVisible (osChoice);
    // ogni testata ha i suoi controlli: al cambio si salvano quelli lasciati e tornano quelli della nuova
    channelChoice.onUserSwitch = [this] (int from, int to) { proc.headMemory.switchPreset (from, to); };
    channelChoice.onDoubleClick = [this] (int ch) { proc.headMemory.resetPreset (ch); };
    tunerBtn.caption = "tuner"; tunerBtn.isOn = [this] { return tuner.isVisible(); };
    // il tuner si apre sempre con il mute acceso (si può spegnere)
    tunerBtn.onClick = [this] { const bool show = ! tuner.isVisible(); tuner.setVisible (show); proc.tunerMute.store (show); tuner.toFront (false); };
    autoBtn.caption = "auto"; autoBtn.isOn = [&p] { return p.isAutoListening(); }; autoBtn.onClick = [&p] { p.toggleAuto(); };
    boostBtn.caption = kBoostNames[0];
    boostBtn.isOn = [&p] { return p.apvts.getRawParameterValue (BAmpIDs::boost)->load() > 0.5f; };
    boostBtn.onClick = [&p, this]   // a rotazione: off -> vol -> vol + eq -> off
    {
        auto* prm = p.apvts.getParameter (BAmpIDs::boost);
        const int next = ((int) p.apvts.getRawParameterValue (BAmpIDs::boost)->load() + 1) % 3;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) next)); prm->endChangeGesture();
        boostBtn.caption = kBoostNames[next];
    };
    addAndMakeVisible (tunerBtn); addAndMakeVisible (autoBtn); addAndMakeVisible (boostBtn);
    tunerBtn.setVisible (! ui::kInRig);   // in b-rig c'è il tuner dell'app
    gateCheck.bindTo (*p.apvts.getParameter (BAmpIDs::gateOn));
    addAndMakeVisible (gateCheck);
    addChildComponent (tuner);
    tuner.onClose = [this] { tuner.setVisible (false); proc.tunerMute.store (false); tunerBtn.repaint(); };
    about.titleColour = [this] { return channelColour (shownChannel < 0 ? 1 : shownChannel); };
    setSize (kW, kH);
    startTimerHz (60);
}

void BAmpEditor::resized()
{
    // in alto a destra: channel [ clean | crunch | lead ]   oversampling [ 2x | 4x ]   [tuner], allineati al titolo
    auto hdr = juce::Rectangle<int> (kMargin, headerRowY(), kW - 2 * kMargin, kControlH);
    tunerBtn.setBounds (hdr.removeFromRight (40)); hdr.removeFromRight (kGapGroup);
    osChoice.setBounds (hdr.removeFromRight (osChoice.getIdealWidth())); hdr.removeFromRight (kGapLabel);
    osLabel = hdr.removeFromRight (tw ("oversampling")); hdr.removeFromRight (kGapGroup);
    channelChoice.setBounds (hdr.removeFromRight (channelChoice.getIdealWidth()));
    {
        auto chLabel = channelChoice.getBounds().translated (-tw ("channel") - kGapLabel, 0).withWidth (tw ("channel"));   // disegnata in paint accanto al selettore
        centreHeaderInRig (kW, { &channelChoice, &osChoice, &tunerBtn }, { &osLabel, &chLabel });
    }
    // testata al centro: 50 % della larghezza, proporzioni di una vera testata (circa 2,1 : 1), più maniglia e piedini
    const int hw = juce::roundToInt (kW * 0.5f), hh = juce::roundToInt ((float) hw / 2.1f) + (int) (AmpHead::kHandleH + AmpHead::kFeetH);
    head.setBounds ((kW - hw) / 2, 70, hw, hh);
    tuner.setBounds (getLocalBounds());
    layoutControls();
}

// tre righe sotto la testata:
//   1. noise gate [x] + soglia, boost [ off | vol | vol + eq ]
//   2. gain, tight  |  bass, mid, treble  |  presence, resonance   (gruppi distanziati, con un separatore)
//   3. allineati a destra: master, level, [auto]
void BAmpEditor::layoutControls()
{
    const int left = kMargin, right = kW - kMargin, groupGap = 40;
    int y = head.getBottom() + 26;
    auto put = [&] (int i, int& x)
    {
        labelAreas[(size_t) i] = { x, y, tw (kLabels[i]), kControlH }; x += tw (kLabels[i]) + kGapLabel;
        boxes[(size_t) i].setBounds (x, y, boxes[(size_t) i].getIdealWidth(), kControlH); x += boxes[(size_t) i].getIdealWidth();
    };
    // riga 1
    {
        int x = left;
        gateLabel = { x, y, tw ("noise gate"), kControlH }; x += tw ("noise gate") + kGapLabel;
        gateCheck.setBounds (juce::Rectangle<int> (x, y, kBoxSize, kControlH).withSizeKeepingCentre (kBoxSize, kBoxSize).expanded (kHitPad)); x += kBoxSize + kGapGroup;
        put (0, x); x += groupGap;
        boostLabel = { x, y, tw ("boost"), kControlH }; x += tw ("boost") + kGapLabel;
        boostBtn.setBounds (x, y, kBoostW, kControlH);
    }
    // riga 2
    y += kControlH + kGapInner;
    {
        int x = left;
        jassert ([&] { int w = 0; for (int i = 1; i <= 7; ++i) w += tw (kLabels[i]) + kGapLabel + boxes[(size_t) i].getIdealWidth(); return w + 4 * kGapGroup + 2 * groupGap; }() <= right - left);
        put (2, x); x += kGapGroup; put (1, x); groupDividers[0] = { x + groupGap / 2, y }; x += groupGap;
        put (3, x); x += kGapGroup; put (4, x); x += kGapGroup; put (5, x); groupDividers[1] = { x + groupGap / 2, y }; x += groupGap;
        put (6, x); x += kGapGroup; put (7, x);
    }
    // riga 3, allineata a destra
    y += kControlH + kGapInner;
    {
        int w = tw (kLabels[8]) + kGapLabel + boxes[8].getIdealWidth() + kGapGroup + tw (kLabels[9]) + kGapLabel + boxes[9].getIdealWidth() + kGapInner + 54;
        int x = right - w;
        put (8, x); x += kGapGroup; put (9, x); x += kGapInner;
        autoBtn.setBounds (x, y, 54, kControlH);
    }
}

void BAmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto col = channelColour (shownChannel < 0 ? 1 : shownChannel);
    drawTitle (g, "amp", { kMargin - 1, 16, 300, 36 }, kLogoSize, col);   // la "b" prende il colore della testata
    drawLabel (g, "channel", channelChoice.getBounds().translated (-tw ("channel") - kGapLabel, 0).withWidth (tw ("channel")), labelStatic, kLabelSize);
    drawLabel (g, "oversampling", osLabel, labelStatic, kLabelSize);
    for (int i = 0; i < 10; ++i) drawLabel (g, kLabels[i], labelAreas[(size_t) i], labelStatic, kLabelSize);
    drawLabel (g, "noise gate", gateLabel, labelStatic, kLabelSize);
    drawLabel (g, "boost", boostLabel, labelStatic, kLabelSize);
    // separatori tra i gruppi gain-tight / bass-mid-treble / presence-resonance: un trattino verticale discreto
    g.setColour (frame);
    for (auto d : groupDividers) g.fillRect ((float) d.x, (float) d.y + 4.0f, kLine, (float) kControlH - 8.0f);
    // in basso: a sinistra lo stato di auto (mentre misura), a destra il livello d'uscita
    const float base = (float) kH - 14.0f;
    const float o = proc.outRmsDb.load();
    if (proc.isAutoListening())
        drawAtBaseline (g, "auto: play normally for " + num (proc.autoRemaining(), 1) + " s (pauses are ignored)", (float) kMargin, base, col, kAxisSize, juce::Justification::left, Face::MonoRegular);
    drawAtBaseline (g, "out " + (o > -90.0f ? num (o, 1) : juce::String ("--")) + " dBFS rms", (float) kW - kMargin, base, info, kAxisSize, juce::Justification::right, Face::MonoRegular);
}

void BAmpEditor::timerCallback()
{
    const int ch = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (BAmpIDs::channel)->load());
    if (ch != shownChannel)
    {
        shownChannel = ch;
        head.showFront();   // cambiando testata si torna al fronte
        setColour (kAccentColourId, channelColour (ch));   // i controlli ereditano l'accento del canale
        for (auto* c : getChildren()) c->repaint();
        repaint();
    }
    boxes[0].setEnabled (proc.apvts.getRawParameterValue (BAmpIDs::gateOn)->load() > 0.5f);   // gate spento: soglia grigia
    if (const juce::String bc = kBoostNames[juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (BAmpIDs::boost)->load())]; bc != boostBtn.caption)
    { boostBtn.caption = bc; boostBtn.repaint(); }
    head.glow += 0.25f * (juce::jlimit (0.0f, 1.0f, proc.satEnv.load() * 1.2f) - head.glow);
    head.repaint();
    if (tuner.isVisible()) { tuner.analyse(); tuner.repaint(); }
    else tuner.analyse();   // svuota comunque il FIFO
    juce::int64 sig = 17;
    for (auto* q : proc.getParameters()) sig = sig * 31 + (juce::int64) (q->getValue() * 1.0e6f);
    sig = sig * 31 + (juce::int64) (proc.outRmsDb.load() * 10.0f) + (proc.isAutoListening() ? 7 : 0) + (juce::int64) (proc.autoRemaining() * 10.0f);
    if (sig != lastSig) { lastSig = sig; repaint (0, kH - 30, kW, 30); for (auto& b : boxes) b.repaint(); autoBtn.repaint(); boostBtn.repaint(); }
}

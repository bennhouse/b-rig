#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 760, kMargin = 24;
    constexpr float kTitleSize = 30.0f;
    // Compensazione ottica in basso a destra: il gruppo spread sta fuori dal riquadro e, a parità
    // di quota, sembra più alto della riga band 3 (che sta dentro l'estensione).
    constexpr int kSpreadSegmentLift = 5;    // tratto orizzontale sopra lo spread: 5 px più in alto
    constexpr int kSpreadOpticalOffset = 2;  // gruppo spread: 2 px più in basso

    int textWidth (const juce::String& s, float size = ui::kLabelSize)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (ui::font (size), s));
    }
}

BAftercabEditor::BAftercabEditor (BAftercabProcessor& p)
    : AudioProcessorEditor (&p), proc (p), shownMode (p.getMode()),
      phaseChoice (*p.apvts.getParameter (ParamIDs::eqMode), { "zero latency", "linear phase" }),
      qualityChoice (*p.apvts.getParameter (ParamIDs::eqQuality), { "low", "mid", "high" }),
      eqDisplay (p),
      revTypeChoice (*p.apvts.getParameter (ParamIDs::revType), { "room", "plate", "hall" }),
      smartButton (p), tempoBox (p), tapButton (p),
      outSection (p),
      compModeChoice (*p.apvts.getParameter (ParamIDs::compMode), { "rhythm", "lead", "clean" })
{
    eqCheck.bindTo (*p.apvts.getParameter (ParamIDs::eqOn));
    spreadCheck.bindTo (*p.apvts.getParameter (ParamIDs::spreadOn));
    precisionCheck.getState = [this] { return proc.getUiFlag (ui::EqDisplay::precisionId); };
    precisionCheck.setState = [this] (bool on)
    {
        proc.setUiFlag (ui::EqDisplay::precisionId, on);
        eqDisplay.repaint();
    };

    // controlli della banda selezionata (nell'estensione del riquadro)
    // delay: l'uso manuale resta sui ms interi (,00); solo l'allineamento dà decimali
    delayBox.setParameter (p.apvts.getParameter (ParamIDs::delayMs));
    delayBox.format = [] (float v) { return ui::num (v, 2) + " ms"; };
    delayBox.step = [] (float v, int dir) { return dir > 0 ? std::floor (v + 0.001f) + 1.0f : std::ceil (v - 0.001f) - 1.0f; };
    delayBox.drag = [] (float start, float, float dx) { return std::round (std::round (start) + dx / 8.0f); };

    // banda selezionata: frequenza a passi di 1/6 di ottava, Q a passi del 12%
    auto normDrag = [this] (const char* what, float perPixel)
    {
        return [this, what, perPixel] (float, float startNorm, float dx)
        {
            auto* prm = proc.apvts.getParameter (ParamIDs::band (boundBand, what));
            return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startNorm + dx * perPixel));
        };
    };
    freqBox.format = [] (float v) { return ui::freqText (v); };
    freqBox.step = [] (float v, int dir) { return v * std::pow (2.0f, (float) dir / 6.0f); };
    freqBox.drag = normDrag ("freq", 0.003f);
    // guadagno della banda: in precision si regola tra ±4 dB, altrimenti ±15; un valore già oltre ±4
    // (impostato in modalità normale) resta com'è passando a precision, finché non lo si tocca
    gainBox.format = [] (float v) { return (v > 0.004f ? "+" : "") + ui::num (v, 1) + " dB"; };
    gainBox.sizeSamples = { "-15.0 dB", "+15.0 dB" };
    gainBox.step = [this] (float v, int dir)
    {
        const float r = proc.getUiFlag (ui::EqDisplay::precisionId) ? 4.0f : BAftercabProcessor::kMaxEqGainDb;
        return juce::jlimit (-r, r, std::round (juce::jlimit (-r, r, v) * 2.0f + (float) dir) / 2.0f);
    };
    gainBox.drag = [this] (float start, float, float dx)
    {
        const float r = proc.getUiFlag (ui::EqDisplay::precisionId) ? 4.0f : BAftercabProcessor::kMaxEqGainDb;
        return juce::jlimit (-r, r, std::round ((juce::jlimit (-r, r, start) + dx / (r > 5.0f ? 6.0f : 16.0f)) * 10.0f) / 10.0f);
    };
    qBox.format = [] (float v) { return ui::num (v, 2); };
    qBox.step = [] (float v, int dir) { return v * std::pow (1.12f, (float) dir); };
    qBox.drag = normDrag ("q", 0.004f);
    // delverb (delay + riverbero): on/off, timbro, un solo mix (%)
    revCheck.bindTo (*p.apvts.getParameter (ParamIDs::revOn));
    revMixBox.setParameter (p.apvts.getParameter (ParamIDs::revMix));
    revMixBox.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    revMixBox.step = [] (float v, int dir) { return std::round (v) + (float) dir; };
    revMixBox.drag = [] (float start, float, float dx) { return std::round (start + dx / 3.0f); };

    // bilanciamento delay <-> riverbero: -100 = solo delay, +100 = solo riverbero
    revBalanceBox.setParameter (p.apvts.getParameter (ParamIDs::revBalance));
    revBalanceBox.format = [] (float v)   // quota delay / quota riverbero (%)
    {
        const int i = juce::roundToInt (v);
        return juce::String (i) + " / " + juce::String (100 - i);
    };
    // stesso verso della barra di riempimento: trascinare a destra (o >) la sposta a destra
    revBalanceBox.step = [] (float v, int dir) { return std::round (v) + 5.0f * (float) dir; };
    revBalanceBox.drag = [] (float start, float, float dx) { return std::round (start + dx / 2.0f); };

    // scegliendo plate o hall il bilanciamento torna al preset pensato per quel timbro
    // compressore: on/off, preset (rhythm / lead / clean), soglia
    compCheck.bindTo (*p.apvts.getParameter (ParamIDs::compOn));
    // la checkbox del comp è anche il meter: scala logaritmica (0,5 dB ~ 1/4, 3 dB ~ 2/3, 12 dB = pieno),
    // così anche una compressione leggera si vede
    compCheck.meter = [this]
    {
        if (compMeterDb < 0.001f) return 0.0f;   // anche una compressione minima accende una riga
        return std::log1p (compMeterDb / 0.5f) / std::log1p (kMeterRangeDb / 0.5f);
    };
    compThrBox.setParameter (p.apvts.getParameter (ParamIDs::compThr));
    compThrBox.format = [] (float v) { return ui::num (v, 1) + " dB"; };
    compThrBox.step = [] (float v, int dir) { return std::round (v) + (float) dir; };
    compThrBox.drag = [] (float start, float, float dx) { return std::round ((start + dx / 4.0f) * 2.0f) / 2.0f; };
    compThrBox.onWidthChanged = [this] { resized(); };
    // memoria per preset (solo sessione): cambiando tipo di compressione o timbro del delverb i valori
    // modificati restano a quel preset; il doppio clic sul preset li riporta ai suoi default
    auto choiceIndex = [&p] (const char* id) { return (int) p.apvts.getRawParameterValue (id)->load(); };
    compModeChoice.onUserSwitch = [this] (int from, int to) { proc.compMemory.switchPreset (from, to); };
    compModeChoice.onDoubleClick = [this] (int m) { proc.compMemory.resetPreset (m); };
    revTypeChoice.onUserSwitch = [this] (int from, int to) { proc.revMemory.switchPreset (from, to); };
    revTypeChoice.onDoubleClick = [this] (int t) { proc.revMemory.resetPreset (t); };
    compThrBox.defaultValue = [this, choiceIndex]
    { return proc.compMemory.defaultFor (proc.apvts.getParameter (ParamIDs::compThr), choiceIndex (ParamIDs::compMode)); };
    revMixBox.defaultValue = [this, choiceIndex]
    { return proc.revMemory.defaultFor (proc.apvts.getParameter (ParamIDs::revMix), choiceIndex (ParamIDs::revType)); };
    revBalanceBox.defaultValue = [this, choiceIndex]
    { return proc.revMemory.defaultFor (proc.apvts.getParameter (ParamIDs::revBalance), choiceIndex (ParamIDs::revType)); };

    for (auto* vb : { &delayBox, &freqBox, &gainBox, &qBox, &revMixBox, &revBalanceBox })
        vb->onWidthChanged = [this] { resized(); };
    // riquadri un po' più larghi del valore più lungo, e fissi
    for (auto* vb : { &delayBox, &freqBox, &gainBox, &qBox, &revMixBox, &revBalanceBox, &compThrBox })
        vb->extraWidth = 8;
    eqDisplay.onSelectionChanged = [this] (int b) { bindBand (b); };
    bindBand (eqDisplay.getSelected());

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &eqCheck, &precisionCheck, &phaseChoice, &qualityChoice, &eqDisplay,
             &bandOnCheck, &freqBox, &gainBox, &qBox,
             &spreadCheck, &delayBox, &smartButton,
             &revCheck, &revTypeChoice, &revMixBox, &revBalanceBox, &tempoBox, &tapButton,
             &compCheck, &compModeChoice, &compThrBox, &outSection })
        addAndMakeVisible (c);

    setSize (kWidth + ui::OutputSection::getIdealWidth() + kMargin, idealHeight());   // a destra la colonna OUT
    updateForMode();
    proc.spectrumActive.store (true);   // il motore copia il segnale per lo spettro solo a interfaccia aperta
    startTimerHz (30);
}

BAftercabEditor::~BAftercabEditor()
{
    stopTimer();
    proc.spectrumActive.store (false);
}

// Righe degli effetti sopra il riquadro EQ (stessa x di inizio/fine della griglia, calcolata in resized):
//   riga 0 = delverb, sulla linea di base del titolo; riga 1 = compressore, subito sotto.
juce::Rectangle<int> BAftercabEditor::headerRow (int index) const
{
    const int y0 = ui::headerRowY();
    return { 0, y0 + index * (ui::kControlH + ui::kGapInner), kWidth, ui::kControlH };
}

// Il riquadro EQ comincia un padding sotto la riga del compressore.
juce::Rectangle<int> BAftercabEditor::eqBox() const
{
    return { kMargin, headerRow (1).getBottom() + ui::kFramePad, kWidth - 2 * kMargin, 298 };
}

// Riga dei controlli sotto l'EQ (banda selezionata a sinistra, spread a destra): calcolata in resized().
juce::Rectangle<int> BAftercabEditor::navbar() const { return navRow; }

// Estensione attaccata sotto al riquadro EQ, in basso a sinistra: calcolata in resized().
juce::Rectangle<int> BAftercabEditor::bandTab() const { return tabRect; }

void BAftercabEditor::bindBand (int b)
{
    boundBand = b;
    auto& s = proc.apvts;
    bandOnCheck.bindTo (*s.getParameter (ParamIDs::band (b, "on")));
    freqBox.setParameter (s.getParameter (ParamIDs::band (b, "freq")));
    gainBox.setParameter (s.getParameter (ParamIDs::band (b, "gain")));
    qBox.setParameter (s.getParameter (ParamIDs::band (b, "q")));
    bandOnCheck.repaint();
    resized();          // l'etichetta "band N" e la larghezza dell'estensione seguono la selezione
    repaint();
}

void BAftercabEditor::updateForMode()
{
    shownMode = proc.getMode();
    resized();
    const bool split = shownMode == BAftercabProcessor::Mode::MonoToStereo;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &spreadCheck, &delayBox, &smartButton })
        c->setVisible (split);
    qualityChoice.setEnabled (proc.apvts.getParameter (ParamIDs::eqMode)->getValue() > 0.5f);
    updateDelverbEnablement();
    updateSpreadEnablement();
    repaint();
}

// spread spento: delay e allineamento non servono (grigi, non cliccabili) e lo stato sparisce
void BAftercabEditor::updateSpreadEnablement()
{
    const bool on = proc.apvts.getParameter (ParamIDs::spreadOn)->getValue() > 0.5f;
    if (delayBox.isEnabled() == on) return;
    delayBox.setEnabled (on);
    smartButton.setEnabled (on);
    if (! on)
    {
        using S = BAftercabProcessor::AlignState;
        const auto st = proc.getAlignState();
        if (st == S::Listening || st == S::Analysing) proc.cancelAlignment();
    }
    repaint();
}

// balance: plate (delay <-> verb) e hall (shimmer <-> verb); tempo solo dove c'è il delay (plate)
void BAftercabEditor::updateDelverbEnablement()
{
    const int type = (int) proc.apvts.getRawParameterValue (ParamIDs::revType)->load();
    const bool hasBalance = type != 0, hasDelay = type == 1;   // hall: shim <-> verb, senza delay
    if (revBalanceBox.isEnabled() != hasBalance) revBalanceBox.setEnabled (hasBalance);
    if (tempoBox.isEnabled() != hasDelay) { tempoBox.setEnabled (hasDelay); tapButton.setEnabled (hasDelay); }
}

// Geometria comune: il padding interno del riquadro EQ è la distanza tra la sua linea
// e il bordo della scala dB; lo stesso valore vale sui 4 lati, nell'estensione e tra le righe in basso.
int BAftercabEditor::framePad() const { return ui::kFramePad; }

int BAftercabEditor::idealHeight() const
{
    const int pad = framePad();
    // sotto il riquadro: una riga (banda + spread) e il fondo dell'estensione, poi il margine
    return eqBox().getBottom() + pad + ui::kControlH + pad + 1 + kMargin;
}

void BAftercabEditor::resized()
{
    using namespace ui;
    labels.clear();
    auto labelLeft = [this] (juce::Rectangle<int>& row, const juce::String& s, bool nav)
    {
        labels.push_back ({ s, row.removeFromLeft (textWidth (s)), nav });
        row.removeFromLeft (kGapLabel);
    };
    auto labelRight = [this] (juce::Rectangle<int>& row, const juce::String& s, bool nav, int gapBefore)
    {
        labels.push_back ({ s, row.removeFromRight (textWidth (s)), nav });
        row.removeFromRight (gapBefore);
    };
    auto take = [] (juce::Rectangle<int>& row, int w, int gapAfter)
    {
        auto r = row.removeFromLeft (w);
        row.removeFromLeft (gapAfter);
        return r;
    };
    auto takeRight = [] (juce::Rectangle<int>& row, int w, int gapBefore)
    {
        auto r = row.removeFromRight (w);
        row.removeFromRight (gapBefore);
        return r;
    };
    auto square = [] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r.expanded (kHitPad)); };   // area cliccabile più ampia

    // --- riquadro EQ: stesso padding sui 4 lati (sinistra = scala dB, destra = bordo griglia,
    //     sopra = riga dei controlli, sotto = linea di base delle frequenze)
    const auto eb = eqBox();
    const int pad = framePad();
    const int left = eb.getX() + 1 + pad;
    const int graphRight = eb.getRight() - 1 - pad;

    auto top = juce::Rectangle<int> (left, eb.getY() + 1 + pad, graphRight - left, kControlH);
    const int dispTop = top.getBottom() + 6;
    const int dispBottom = juce::roundToInt ((float) (eb.getBottom() - 1 - pad) + eqDisplay.getXLabelBaselineFromBottom());
    // il grafico si posiziona in modo che il bordo della scala dB cada esattamente a "left"
    const int dispX = left - juce::roundToInt (eqDisplay.getScaleLeft());
    eqDisplay.setBounds (juce::Rectangle<int>::leftTopRightBottom (dispX, dispTop,
                                                                   graphRight + 2, dispBottom));   // +2: margine interno del grafico

    // --- header: il delverb condivide la linea di base del titolo e finisce sul bordo della griglia
    {
        // la riga arriva fino al bordo destro della colonna OUT (il tap si allinea al tasto auto)
        auto head = headerRow (0).withLeft (left).withRight (eb.getRight() + kGapGroup + OutputSection::getIdealWidth());
        tapButton.setBounds (takeRight (head, 40, kGapLabel));                       // tap a destra
        tempoBox.setBounds (takeRight (head, tempoBox.getIdealWidth(), kGapGroup));   // BPM / sync a sinistra
        revMixBox.setBounds (takeRight (head, revMixBox.getIdealWidth(), kGapLabel));
        labelRight (head, "mix", false, kGapGroup);
        labelRight (head, "verb", false, kGapLabel);
        revBalanceBox.setBounds (takeRight (head, revBalanceBox.getIdealWidth(), kGapLabel));
        labelRight (head, "shim", false, kGapGroup);   // "del" (plate) o "shim" (hall): spazio per il più largo
        revTypeChoice.setBounds (takeRight (head, revTypeChoice.getIdealWidth(), kGapInner));
        square (revCheck, takeRight (head, kBoxSize, kGapLabel));
        labelRight (head, "delverb", false, 0);
    }

    // --- riga EQ
    labelLeft (top, "eq", false);
    square (eqCheck, take (top, kBoxSize, kGapGroup));
    labelLeft (top, "precision", false);
    square (precisionCheck, take (top, kBoxSize, kGapGroup));
    labelLeft (top, "phase", false);
    phaseChoice.setBounds (take (top, phaseChoice.getIdealWidth(), kGapGroup));
    labelLeft (top, "quality", false);
    qualityChoice.setBounds (take (top, qualityChoice.getIdealWidth(), kGapGroup));
    modeArea = top;   // configurazione (mono > stereo), allineata a destra sul bordo della griglia

    // --- righe in basso: stessa distanza (pad) tra riquadro, prima riga, seconda riga
    navRow = { kMargin, eb.getBottom() + pad, kWidth - 2 * kMargin, kControlH };

    // estensione: band 3 [on]  freq < 1,00 khz >  q < 1,00 >
    auto tab = navRow.withX (left).withRight (kWidth);
    labelLeft (tab, "band " + juce::String (juce::jmax (0, boundBand) + 1), false);
    square (bandOnCheck, take (tab, kBoxSize, kGapGroup));
    labelLeft (tab, "freq", false);
    freqBox.setBounds (take (tab, freqBox.getIdealWidth(), kGapGroup));
    labelLeft (tab, "gain", false);
    gainBox.setBounds (take (tab, gainBox.getIdealWidth(), kGapGroup));
    labelLeft (tab, "q", false);
    qBox.setBounds (take (tab, qBox.getIdealWidth(), 0));
    const int tabTop = eb.getBottom() - 1;                    // condivide la linea di fondo del riquadro EQ
    const int tabRight = qBox.getRight() + pad + 1;
    const int tabBottom = navRow.getBottom() + pad + 1;       // la sua linea di fondo coincide con l'inizio della seconda riga
    tabRect = { eb.getX(), tabTop, tabRight - eb.getX(), tabBottom - tabTop };

    // spread [ ] < 15,00 ms > [=]   (solo mono > stereo)
    // compensazione ottica: lo spread scende di kSpreadOpticalOffset px rispetto alla riga band 3
    auto nav = navRow.withRight (graphRight).translated (0, kSpreadOpticalOffset);
    square (smartButton, takeRight (nav, kBoxSize, kGapInner));
    delayBox.setBounds (takeRight (nav, delayBox.getIdealWidth(), kGapInner));
    square (spreadCheck, takeRight (nav, kBoxSize, kGapLabel));
    labelRight (nav, "spread", true, kGapGroup);
    // stato dell'allineamento: sotto i controlli dello spread, allineato a destra sul bordo della griglia
    // (tra l'estensione con freq/gain/q e lo spread non c'è più spazio)
    statusArea = juce::Rectangle<int>::leftTopRightBottom (tabRect.getRight() + kGapGroup, navRow.getBottom() + 4,
                                                           graphRight, navRow.getBottom() + 4 + kControlH);

    // colonna OUT a destra del riquadro EQ: OUT, barra, valore e tasto auto ravvicinati e centrati
    // in verticale sulla parte destra del riquadro (dalla linea in alto al gradino sopra lo spread)
    {
        const int x = eb.getRight() + kGapGroup, w = OutputSection::getIdealWidth();
        const auto grid = eqDisplay.getGridYRange();
        const int barH = juce::roundToInt (grid.getLength()) + 1;          // alta quanto la griglia EQ
        const int tagH = juce::roundToInt (capHeight (kTagSize, Face::SansSemiBold));
        const int valH = juce::roundToInt (capHeight (kAxisSize, Face::MonoRegular));
        const int groupH = tagH + kGapLabel + barH + kGapLabel + valH + kGapInner + kControlH;
        const int partTop = eb.getY(), partBottom = eb.getBottom() - 1 - kSpreadSegmentLift;
        const int y0 = partTop + (partBottom - partTop - groupH) / 2;
        outSection.setBounds (x, y0, w, groupH);
        outSection.tagBaseline = (float) tagH;
        outSection.barArea = juce::Rectangle<int> (w / 2 - kBoxSize / 2, tagH + kGapLabel, kBoxSize, barH);
        outSection.valueBaseline = (float) (outSection.barArea.getBottom() + kGapLabel + valH);
        outSection.autoArea = juce::Rectangle<int> (0, groupH - kControlH, w, kControlH);
    }

    // comp [ ] [rhythm|lead|clean]  thr < -12,0 db >  gr 0,0 db  -- sotto il delverb, sopra il riquadro EQ.
    // Incolonnato col delverb: stessa x per le checkbox e per i selettori; il resto finisce sul bordo della griglia.
    {
        auto row = headerRow (1).withLeft (left).withRight (graphRight);
        const int checkX = revCheck.getBounds().reduced (kHitPad).getX();
        const int choiceX = revTypeChoice.getX();
        const auto checkR = juce::Rectangle<int> (checkX, row.getY(), kBoxSize, kControlH);
        square (compCheck, checkR);
        labels.push_back ({ "comp", juce::Rectangle<int> (checkX - kGapLabel - textWidth ("comp"), row.getY(), textWidth ("comp"), kControlH), false });
        compModeChoice.setBounds (choiceX, row.getY(), compModeChoice.getIdealWidth(), kControlH);
        // thr subito dopo il selettore (stesso spazio tra gruppi del resto dell'interfaccia)
        auto rest = row.withLeft (compModeChoice.getRight() + kGapGroup);
        labelLeft (rest, "thr", false);
        compThrBox.setBounds (take (rest, compThrBox.getIdealWidth(), 0));
    }
    // in b-rig (senza titolo) le due righe del delverb e del compressore si centrano insieme
    if (ui::kInRig)
    {
        std::vector<juce::Rectangle<int>*> headLabels;
        for (auto& l : labels) if (l.area.getBottom() <= headerRow (1).getBottom()) headLabels.push_back (&l.area);
        ui::centreHeaderInRig (getWidth(), { &tapButton, &tempoBox, &revMixBox, &revBalanceBox, &revTypeChoice, &revCheck,
                                             &compCheck, &compModeChoice, &compThrBox }, headLabels);
    }
}

void BAftercabEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);

    // sync spento: suggerimento discreto sopra il contatore dei BPM
    if (! proc.isSync() && tempoBox.isEnabled())
        ui::drawAtBaseline (g, "tap to sync", (float) tempoBox.getBounds().getCentreX(), (float) tempoBox.getY() - 5.0f,
                            juce::Colour (0xff5c5c5c), ui::kAxisSmall, juce::Justification::horizontallyCentred, ui::Face::SansRegular);

    // --- header: "b" giallo + "-aftercab" grigio, senza contenitori
    {
        const float size = kTitleSize;
        const juce::Rectangle<int> r (kMargin - 1, 16, 300, 36);   // centro y = 34
        ui::drawTitle (g, "aftercab", r, size);

        juce::String mode;
        switch (shownMode)
        {
            case BAftercabProcessor::Mode::MonoToMono:     mode = "mono > mono"; break;
            case BAftercabProcessor::Mode::MonoToStereo:   mode = "mono > stereo"; break;
            case BAftercabProcessor::Mode::StereoToStereo: mode = "stereo > stereo"; break;
        }
        // configurazione accanto al titolo (a destra c'è il riverbero)
        ui::drawLabel (g, mode, modeArea, ui::info, ui::kInfoSize, juce::Justification::centredRight, ui::Face::SansRegular);
    }

    // --- riquadro EQ: linea continua da 1 px, grigio intermedio
    // contorno unico di riquadro + estensione in basso a sinistra (nessuna linea tra i due)
    {
        g.setColour (ui::frame);
        const auto b = eqBox();
        const auto t = bandTab();
        g.fillRect (b.getX(), b.getY(), b.getWidth(), 1);                                  // alto
        // compensazione ottica: il tratto sopra lo spread è alzato di kSpreadSegmentLift px
        const int segY = b.getBottom() - 1 - kSpreadSegmentLift;
        g.fillRect (b.getRight() - 1, b.getY(), 1, segY - b.getY() + 1);                    // destra
        g.fillRect (t.getRight() - 1, segY, b.getRight() - t.getRight() + 1, 1);            // basso (parte destra)
        g.fillRect (t.getRight() - 1, segY, 1, t.getBottom() - segY);                       // gradino: lato destro estensione
        g.fillRect (t.getX(), t.getBottom() - 1, t.getWidth(), 1);                          // fondo estensione
        g.fillRect (b.getX(), b.getY(), 1, t.getBottom() - b.getY());                       // sinistra (fino in fondo)
    }

    // --- etichette
    const bool split = shownMode == BAftercabProcessor::Mode::MonoToStereo;
    for (const auto& l : labels)
    {
        if (l.navbar && ! split) continue;
        // balance del delverb: il lato che si sta abbassando si scurisce progressivamente (mai del tutto)
        if (l.text == "shim" || l.text == "verb")
        {
            const int type = (int) proc.apvts.getRawParameterValue (ParamIDs::revType)->load();
            const float share = proc.apvts.getRawParameterValue (ParamIDs::revBalance)->load() / 100.0f;   // quota delay / shimmer
            const float w = l.text == "verb" ? juce::jlimit (0.0f, 1.0f, (1.0f - share) * 2.0f) : juce::jlimit (0.0f, 1.0f, share * 2.0f);
            const auto c = ui::labelStatic.interpolatedWith (ui::background, type == 0 ? 0.0f : 0.7f * (1.0f - w));
            ui::drawLabel (g, l.text == "verb" ? juce::String ("verb") : juce::String (type == 2 ? "shim" : "del"), l.area, c, ui::kLabelSize,
                           l.text == "verb" ? juce::Justification::centredLeft : juce::Justification::centredRight);
            continue;
        }
        ui::drawLabel (g, l.text, l.area, ui::labelStatic, ui::kLabelSize);
    }
    if (split)
        if (delayBox.isEnabled())
            ui::drawLabel (g, statusText(), statusArea, ui::info, ui::kInfoSize, juce::Justification::centredRight, ui::Face::SansRegular);
    // la riduzione di guadagno del compressore è mostrata dalla sua checkbox (meter)
}

juce::String BAftercabEditor::statusText() const
{
    using S = BAftercabProcessor::AlignState;
    switch (proc.getAlignState())
    {
        case S::Listening:
        {
            const float pr = proc.getAlignProgress();
            return pr <= 0.0f ? juce::String ("listening... play the track")
                              : "listening " + juce::String (juce::roundToInt (pr * 100.0f)) + "%";
        }
        case S::Analysing: return "analysing...";
        case S::Done:
        {
            const auto r = proc.getLastAlignResult();
            return "aligned   corr " + ui::num (r.corrBefore, 2) + " > " + ui::num (r.corrAfter, 2);
        }
        case S::Failed: return "no usable signal";
        case S::Idle:
        default: return {};
    }
}

void BAftercabEditor::timerCallback()
{
    if (proc.getMode() != shownMode)
    {
        updateForMode();
        return;
    }

    // la qualità conta solo in linear phase
    const bool linear = proc.apvts.getParameter (ParamIDs::eqMode)->getValue() > 0.5f;
    if (qualityChoice.isEnabled() != linear)
    {
        qualityChoice.setEnabled (linear);
        repaint();
    }
    updateDelverbEnablement();
    updateSpreadEnablement();
    eqDisplay.updateSpectrum (30.0f);

    // Ridisegna solo se qualcosa è cambiato (parametri, automazione, stato analisi).
    juce::int64 sig = 17;
    for (auto* p : proc.getParameters())
        sig = sig * 31 + (juce::int64) (p->getValue() * 1.0e6f);
    sig = sig * 31 + (juce::int64) proc.getAlignState();
    sig = sig * 31 + (juce::int64) (proc.getAlignProgress() * 100.0f);
    sig = sig * 31 + (proc.getUiFlag (ui::EqDisplay::precisionId) ? 1 : 0);
    if (eqDisplay.getSelected() != boundBand) bindBand (eqDisplay.getSelected());   // es. dopo il caricamento di un progetto
    {
        // bande 1 e 5 in modalità taglio: niente guadagno
        auto* tp = proc.apvts.getParameter (ParamIDs::band (boundBand, "type"));
        const bool cut = tp != nullptr && tp->getValue() < 0.5f;
        // banda spenta: i suoi controlli in grigio come il punto nel grafico
        const bool bandOn = proc.apvts.getParameter (ParamIDs::band (boundBand, "on"))->getValue() > 0.5f;
        if (gainBox.isEnabled() != (bandOn && ! cut)) gainBox.setEnabled (bandOn && ! cut);
        if (freqBox.isEnabled() != bandOn) { freqBox.setEnabled (bandOn); qBox.setEnabled (bandOn); }
    }
    sig = sig * 31 + boundBand;
    sig = sig * 31 + proc.getLatencySamples();
    sig = sig * 31 + (juce::int64) (proc.getAutoOutDb() * 100.0f);
    // meter del compressore: sale subito, scende di ~24 dB/s (leggibile, non nervoso)
    {
        const float gr = proc.takeCompGainReductionPeak();   // picco dall'ultimo frame
        const float shown = gr >= compMeterDb ? gr : juce::jmax (gr, compMeterDb - 0.8f);
        if (std::abs (shown - compMeterDb) > 0.01f) { compMeterDb = shown; compCheck.repaint(); }
    }

    if (sig != lastSignature)
    {
        lastSignature = sig;
        // un valore cambiato (es. automazione) può cambiare la larghezza dei box
        for (auto* vb : { &delayBox, &freqBox, &gainBox, &qBox, &revMixBox, &revBalanceBox })
            if (vb->getIdealWidth() != vb->getWidth()) { resized(); break; }
        repaint();
        for (auto* c : getChildren()) c->repaint();   // (anche la colonna OUT)
    }
}

#pragma once
#include <map>
// Interfaccia di b-rig: 4 zone fisse, nessuna finestra fluttuante.
//   1. barra in alto (56 px): logo (about) | presets e metronomo al centro | setup a destra
//   2. colonna a sinistra (220 px): tuner (spento finché non lo premi) e module library
//   3. active signal chain (68 px): card trascinabili. A sinistra il tasto dello split (una linea: catena unica;
//      due linee: due rami paralleli) e, con lo split, il selettore del ramo [A|B] che si sta guardando.
//   4. workspace: l'editor completo del modulo selezionato
// Setup e presets si aprono come schermate a tutta finestra (si chiudono con la x).
#include "Engine/RigEngine.h"
#include "smp/Widgets.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace rig
{
// Parametri della UI di b-rig (bpm, tempo): un piccolo "processore" senza audio, così i box del
// design system (ValueBox) funzionano come nei plugin.
class RigControls final : public juce::AudioProcessor
{
public:
    RigControls();
    const juce::String getName() const override { return "b-rig"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter& p (const char* id) { return *apvts.getParameter (id); }
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void set (const char* id, float v) { auto& q = p (id); q.setValueNotifyingHost (q.convertTo0to1 (v)); }
};

// Tema delle parti JUCE standard (menu a tendina, liste, pulsanti): stesso font, colori e angoli vivi della suite.
class RigLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel();
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 1; }
    void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height, bool isScrollUpArrow) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isMouseOverButton, bool isButtonDown) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    juce::Font getLabelFont (juce::Label&) override;
};

// Pulsante piatto [ testo ]: acceso = pieno nel colore indicato; "dim" = attenuato ma cliccabile.
class FlatButton final : public juce::Component
{
public:
    juce::String caption;
    std::function<bool()> isOn;
    std::function<void()> onClick;
    std::function<juce::Colour()> onColour;
    bool dim = false;
    int idealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && e.mouseWasClicked() && onClick) onClick(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

// Metronomo: piccolo simbolo stilizzato (corpo a trapezio e asta), acceso = pieno nel colore di accento.
class MetroButton final : public juce::Component
{
public:
    std::function<bool()> isOn;
    std::function<void()> onClick;
    std::function<float()> swing;   // -1..1: posizione dell'asta (segue il battito)
    std::function<void()> onRightClick;   // menu: suono e volume
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
        else if (onClick) onClick();
        repaint();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

// Menu a tendina del design system: [ testo  v ], larghezza fissa (la voce più larga), menu col tema della suite.
class Dropdown final : public juce::Component
{
public:
    std::function<juce::StringArray()> items;
    std::function<int()> current;
    std::function<void (int)> onChoose;
    juce::StringArray sizeSamples;     // per la larghezza fissa
    bool dim = false;
    int idealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    juce::String currentText() const;
};

// Analisi del tuner (MPM di b-amp sulla DI decimata 8x): testabile senza interfaccia.
struct TunerAnalyser
{
    void update (RigEngine&);           // 60 fps: legge il FIFO e stima la nota
    void reset() { hz = cents = shownCents = 0; midi = -1; filled = 0; lastValidMs = 0; floorPk = 0.0f; }
    float hz = 0, cents = 0, shownCents = 0, strobe = 0;
    int midi = -1;
    double lastValidMs = 0;
    bool active() const;
    // soglia: almeno -56 dBFS e 10 dB sopra il fruscio misurato della DI (si adatta alla scheda)
    static constexpr float kGatePeak = 0.0016f;
    static constexpr float kMinClarity = 0.85f;   // periodicità minima (MPM) per mostrare una nota
    float floorPk = 0.0f;                         // picco del fruscio (finestre senza intonazione), 0 = non ancora misurato
    std::array<float, 4096> win {}; int wpos = 0, filled = 0;
};

class RigComponent final : public juce::Component, private juce::Timer
{
public:
    RigComponent (RigEngine&, juce::AudioDeviceManager*);
    ~RigComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // preset (file .brig in ~/Library/Application Support/b-suite/b-rig/Presets, fuori dall'app: aggiornarla o
    // reinstallarla non li tocca). Si scrivono su un file temporaneo e poi si sostituiscono, così un'interruzione non
    // lascia mai un preset a metà; quelli eliminati finiscono nel Cestino. All'avvio si carica quello scelto
    // (clean slate, la catena vuota, se non se n'è scelto uno).
    static juce::File presetDir();
    static juce::File& presetDirOverride() { static juce::File f; return f; }   // i test usano una cartella temporanea
    static juce::File presetFile (const juce::String& name) { return presetDir().getChildFile (name + ".brig"); }
    static constexpr const char* kCleanSlate = "clean slate";   // in cima, bianco: catena vuota
    struct PresetEntry { juce::String name; juce::Colour colour; juce::int64 created = 0; };
    static std::vector<PresetEntry> listPresets();   // clean slate, poi dal più recente; colori sistemati
    static void indexSet (const juce::String& name, juce::int64 created, juce::Colour);
    static void indexRemove (const juce::String& name);
    static bool renamePreset (const juce::String& from, const juce::String& to);
    static juce::String nextUserName();
    static juce::String startupPreset();                     // preset che si carica all'avvio (anche "clean slate")
    static void setStartupPreset (const juce::String&);   // vuoto = nessuna scelta: all'avvio la sessione di prima
    static bool hasStartupChoice();
    // sessione: alla chiusura la catena com'è (anche non salvata) e il preset da cui viene; all'avvio si riapre
    static juce::File sessionFile();
    void saveSession();
    bool saveSessionOnClose = false;   // solo l'app (non i test)
    static juce::String& bootPreset() { static juce::String s; return s; }
    static bool& bootDirty() { static bool d = false; return d; }
    static void loadStartup (RigEngine&);                    // all'avvio: il preset scelto, altrimenti la catena vuota
    void setStartupForTest (int row);
    struct RigSnapshot { juce::ValueTree rig; juce::String preset; bool dirty = false; };
    RigSnapshot snapshot() const;
    void restoreState (const RigSnapshot&, bool rigToo);
    juce::String createWithUndo();
    static bool loadPresetFile (RigEngine&, const juce::File&);
    juce::String currentPreset { startupPreset() };
    // stato temporaneo: qualunque modifica dopo l'ultimo caricamento/salvataggio ("unsaved" + save nella barra)
    bool dirty = false;
    void markDirty();
    void clearDirty();
    juce::String saveAsUserPreset();   // salva come "User N" (il primo numero libero) e lo rende corrente
    juce::StringArray presetNamesForTest() const;
    std::vector<PresetEntry> presetEntriesForTest() const;
    void deletePresetForTest (int row);
    void undoPresetForTest();
    void selectPresetForTest (int row);
    void duplicatePresetForTest();
    void loadPresetRowForTest (int row);
    void saveFromTopBar();             // new nella barra: nuovo preset e nome da scrivere nella schermata dei preset
    void saveCurrent();                // save nella barra: aggiorna il preset caricato
    bool saveEnabledForTest() const { return saveBtn.isEnabled() && newBtn.isEnabled(); }
    bool isPresetRenaming() const;
    void renamePresetForTest (const juce::String&);
    void paramTouchedForTest() { lastInputMs = juce::Time::getMillisecondCounterHiRes(); paramTouched(); }
    void loadPreset (const juce::String& name);
    bool writeRig (const juce::File&);
    void selectStartModule();   // b-amp se c'è, altrimenti il primo
    bool inChain (const juce::String& id) const;   // nella catena che si vede (un solo modulo per tipo in ogni ramo)
    int viewIndexOf (const juce::String& id) const;   // indice nell'engine del modulo di quel tipo nella vista, -1 se manca
    void requestRemove (int index);                // x sulla card (b-blend dopo un ampli: con conferma)
    bool confirmVisibleForTest() const;
    void confirmForTest();
    void closeScreensForTest();
    // undo / redo del rig (catena, on/off, parametri dei moduli): una voce dopo ogni modifica, a gesto finito
    void undoRig();
    void redoRig();
    void commitHistory();          // registra lo stato attuale se è cambiato
    void applyHistory (const juce::String& xml);
    bool keyPressed (const juce::KeyPress&) override;
    std::vector<juce::String> history;
    int historyPos = -1;
    bool historyPending = false;
    double historyChangeMs = 0;
    FlatButton undoBtn, redoBtn;   // come i tasti della schermata dei preset
    void checkCabs();           // b-blend con un IR che funziona: riattiva b-amp in safe mute
    std::map<const Slot*, bool> cabWorking;
    int cabTick = 0;
    void savePreset (const juce::String& name);

    // usati dai test
    void select (Slot*);
    Slot* getSelected() const { return selected; }
    juce::AudioProcessorEditor* getEditor() const { return editor.get(); }
    void addFromLibrary (const juce::String& id);
    void showInfo (const juce::String& id);
    void showAudioSetup();
    void showPresets();
    void setTunerActive (bool);
    bool isTunerActive() const { return engine.tunerOn.load(); }
    void dragCardForTest (int from, int toGap);   // indici della vista
    // trascina la card della vista nel ramo (con shift: nell'altro ramo) alla posizione pos; true se si è spostata
    bool dragToLaneForTest (int viewIndex, bool shift, int pos);
    // per gli screenshot: la card della vista trascinata fino alla x (in coordinate del componente), con o senza shift
    void previewDragForTest (int viewIndex, int x, bool shift)
    {
        const auto v = viewSlots();
        dragHomeLane = engine.isSplit() ? viewLane : 0;
        pressSlot = dragSlot = &engine.slot (v[(size_t) viewIndex]);
        dragGrabDx = kCardW / 2; dragGrabDy = kCardH / 2; dragX = x; dragY = cardsViewport().getCentreY() + 40;
        dragShift (shift);
        repaint();
    }
    void cancelDragForTest() { dragSlot = pressSlot = nullptr; endDragView (false); repaint(); }
    int chainXForTest (int contentX) const { return cardsViewport().getX() + contentX - chainScroll; }
    // split: ramo che si sta guardando (1 = A, 2 = B) quando la catena è divisa
    int viewLane = 1;
    void toggleSplit();            // divide, o riunisce (con conferma se l'altro ramo ha dei moduli)
    void showLane (int lane);
    std::vector<int> viewSlots() const;   // indici nell'engine dei moduli che si vedono: prima dello split, poi il ramo
    // primo avvio: finestra che spiega che b-rig non contiene IR di casse
    void showWelcome();
    bool welcomeVisibleForTest() const;
    void tickForTest() { timerCallback(); }
    void requestEnabled (int index, bool on);   // b-blend dopo un ampli non si spegne
    static bool cabProtects (RigEngine&, int index);   // b-blend acceso con un ampli prima, a qualunque distanza
    TunerAnalyser tuner;
    RigControls controls;

    static constexpr int kTopH = 56, kLibW = 220, kChainH = 68, kTunerH = kChainH, kWsHeadH = 44, kMargin = 24;
    static constexpr int kRowH = 28, kCatH = 24, kCardW = 150, kCardH = 50, kConn = 10, kMarkW = 34;

private:
    void timerCallback() override;
    void paramTouched();
    void syncWatchers();
    void chainChanged();
    void layoutTop();
    void rebuildEditor();
    void closeEditor();
    void armOrToggle (Slot&);
    void scrollToSlot (int index);
    void showMetroMenu();
    void paintOverChildren (juce::Graphics&) override;

    // geometria
    juce::Rectangle<int> topArea() const { return { 0, 0, getWidth(), kTopH }; }
    juce::Rectangle<int> tunerArea() const { return { 0, kTopH + 1, kLibW, kTunerH }; }
    juce::Rectangle<int> tunerCloseArea() const { auto t = tunerArea(); return { t.getRight() - 25, t.getY() + 10, 20, 20 }; }
    juce::Rectangle<int> libArea() const { return { 0, kTopH + kTunerH + 2, kLibW, getHeight() - kTopH - kTunerH - 2 }; }
    juce::Rectangle<int> chainArea() const { return { kLibW + 1, kTopH + 1, getWidth() - kLibW - 1, kChainH }; }
    juce::Rectangle<int> wsArea() const { auto c = chainArea(); return { c.getX(), c.getBottom() + 1, c.getWidth(), getHeight() - c.getBottom() - 1 }; }
    juce::Rectangle<int> wsToggleArea() const { auto w = wsArea(); return { w.getRight() - 16 - 104, w.getY() + (kWsHeadH - 26) / 2, 104, 26 }; }
    juce::Rectangle<int> splitBtnArea() const;   // tasto dello split, a sinistra nella catena
    juce::Rectangle<int> laneBtn (int lane) const;   // con lo split: i pulsanti "a" e "b" accanto
    float laneEndX() const;                      // dove finisce la linea del ramo (poi la sua lettera)
    static constexpr int kBtn = 24, kLaneOff = 15;   // misura dei tasti; la linea di un ramo: 15 px sopra (A) o sotto (B) il centro
    juce::Rectangle<int> cardsViewport() const;
    juce::Rectangle<int> cardRect (int viewIndex) const;   // in coordinate del componente (con lo scroll)
    juce::Rectangle<int> markerRect() const;               // il punto di split tra le card
    int viewTrunk() const;                                 // card prima dello split (tutte, se la catena è unica)
    int itemX (int viewIndex) const;                       // x della card nel contenuto (senza scroll)
    int markerX() const;
    // punto di inserimento durante il trascinamento: ramo, posizione nel ramo, x nel contenuto
    struct Gap { int lane = 0, pos = 0, x = 0; };
    Gap gapAt (int x) const;
    bool dropSlot (int index, Gap);   // false se non si è spostato
    void moveByArrow (int index, int dir);
    void hint (const juce::String&);   // messaggio breve nella testata del workspace
    juce::String hintText;
    double hintUntilMs = 0;

    // libreria
    struct LibRow { const ModuleInfo* m; juce::Rectangle<int> row, info, add; };
    std::vector<LibRow> libRows;
    std::vector<std::pair<juce::String, int>> libCats;   // etichetta, y
    void layoutLibrary();

    // catena
    enum class Hit { none, led, card, left, right, remove, badge, marker };
    std::pair<int, Hit> chainHit (juce::Point<int>) const;
    int chainScroll = 0, chainContentW = 0;
    Slot* pressSlot = nullptr;   // card premuta (clic o inizio di un trascinamento)
    Slot* dragSlot = nullptr;    // card che si sta trascinando
    int dragX = 0, dragY = 0, dragGrabDx = 0, dragGrabDy = 0;
    void paintCard (juce::Graphics&, int index, juce::Rectangle<int>, bool ghost, juce::Rectangle<int> frame);
    // trascinamento con lo split: il ramo da cui è partito; shift tenuto premuto mostra l'altro ramo
    int dragHomeLane = 0;
    void dragShift (bool shift);
    void endDragView (bool moved);
    void setViewLane (int lane);   // solo la vista (durante un trascinamento la selezione non cambia)
    void modifierKeysChanged (const juce::ModifierKeys&) override;
    // dove cadrebbe un modulo trascinato e se può entrare lì (un modulo per tipo in ogni ramo, la parte comune una volta)
    juce::String dropProblem (const juce::String& id, Gap, const Slot* except) const;
    void paintDrop (juce::Graphics&, int x, const ModuleInfo&, const Slot* except);
    juce::String dropHint (int x, const ModuleInfo&, const Slot* except) const;
    bool markerPress = false, markerDragging = false;
    int markerDragX = 0;
    // trascinamento dalla libreria: il modulo entra solo se lo lasci sulla catena
    const ModuleInfo* libPress = nullptr;
    bool libDragging = false;
    juce::Point<int> libDragPos;

    // l'aspetto (dichiarato per primo: deve vivere più a lungo di tutti i componenti)
    RigLookAndFeel laf;

    RigEngine& engine;
    juce::AudioDeviceManager* dm;
    Slot* selected = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::AudioProcessor* editorOwner = nullptr;
    float editorScale = 1.0f;
    juce::Point<int> hover { -1, -1 };

    // modifiche: parametri dei moduli toccati dall'utente (non dal caricamento o dai moduli stessi)
    struct ParamWatch;
    struct InputWatch;
    std::unique_ptr<ParamWatch> paramWatch;
    std::unique_ptr<InputWatch> inputWatch;
    std::vector<juce::AudioProcessor*> watched;
    double lastInputMs = 0, suppressUntilMs = 0;

    // title bar e tuner
    FlatButton saveBtn, newBtn, presetsBtn, setupBtn, tapBtn, muteBtn, stereoBtn, widenBtn;
    int sideBtnW() const;   // larghezza comune di stereo e widen
    MetroButton metroBtn;
    ui::ValueBox bpmBox;
    Dropdown sigBox;
    std::vector<double> taps;
    float lastBpmParam = 120.0f;
    float cpuShown = 0;

    // schermate (dentro la finestra)
    class InfoOverlay;
    class ConfirmOverlay;
    std::unique_ptr<ConfirmOverlay> confirm;
    class SetupScreen;
    class PresetScreen;
    class WelcomeScreen;
    std::unique_ptr<WelcomeScreen> welcome;
    std::unique_ptr<InfoOverlay> infoOverlay;
    std::unique_ptr<SetupScreen> setup;
    std::unique_ptr<PresetScreen> presets;
    std::unique_ptr<ui::AboutOverlay> about;
};
}

#pragma once
#include <map>
// Interfaccia di b-rig: 4 zone fisse, nessuna finestra fluttuante.
//   1. barra in alto (56 px): logo (about) | presets e metronomo al centro | setup a destra
//   2. colonna a sinistra (220 px): tuner (spento finché non lo premi) e module library
//   3. active signal chain (68 px): card trascinabili
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

// Freccia di undo / redo (arco con la punta), nella barra in alto. Spenta = grigia e non cliccabile.
class ArrowButton final : public juce::Component
{
public:
    explicit ArrowButton (bool isRedo) : redo (isRedo) {}
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && e.mouseWasClicked() && onClick) onClick(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void enablementChanged() override { repaint(); }
private:
    bool redo;
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

    // preset (file .brig nella cartella dell'app); all'avvio si carica quello scelto ("default" se non se n'è scelto uno)
    static juce::File presetDir();
    static juce::File& presetDirOverride() { static juce::File f; return f; }   // i test usano una cartella temporanea
    static juce::File presetFile (const juce::String& name) { return presetDir().getChildFile (name + ".brig"); }
    static constexpr const char* kDefaultPreset = "default";
    static constexpr const char* kCleanSlate = "clean slate";   // in cima, bianco: catena vuota
    struct PresetEntry { juce::String name; juce::Colour colour; juce::int64 created = 0; };
    static std::vector<PresetEntry> listPresets();   // clean slate, poi dal più recente; colori sistemati
    static void indexSet (const juce::String& name, juce::int64 created, juce::Colour);
    static void indexRemove (const juce::String& name);
    static bool renamePreset (const juce::String& from, const juce::String& to);
    static juce::String nextUserName();
    static juce::String startupPreset();                     // preset che si carica all'avvio (anche "clean slate")
    static void setStartupPreset (const juce::String&);
    static void loadStartup (RigEngine&);                    // all'avvio: il preset scelto, altrimenti la catena di fabbrica
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
    void saveFromTopBar();             // save nella barra: nuovo preset e nome da scrivere nella schermata dei preset
    bool isPresetRenaming() const;
    void renamePresetForTest (const juce::String&);
    void paramTouchedForTest() { lastInputMs = juce::Time::getMillisecondCounterHiRes(); paramTouched(); }
    void loadPreset (const juce::String& name);
    bool writeRig (const juce::File&);
    void selectStartModule();   // b-amp se c'è, altrimenti il primo
    bool inChain (const juce::String& id) const;   // un solo modulo per tipo
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
    ArrowButton undoBtn { false }, redoBtn { true };
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
    void dragCardForTest (int from, int toGap);
    void tickForTest() { timerCallback(); }
    void requestEnabled (int index, bool on);   // b-blend dopo un ampli non si spegne
    static bool cabProtects (RigEngine&, int index);   // b-blend acceso con un ampli prima, a qualunque distanza
    TunerAnalyser tuner;
    RigControls controls;

    static constexpr int kTopH = 56, kLibW = 220, kChainH = 68, kTunerH = kChainH, kWsHeadH = 44, kMargin = 24;
    static constexpr int kRowH = 28, kCatH = 24, kCardW = 150, kCardH = 50, kConn = 10;

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
    juce::Rectangle<int> cardsViewport() const;
    juce::Rectangle<int> cardRect (int index) const;   // in coordinate del componente (con lo scroll)
    int gapAt (int x) const;                           // punto di inserimento (0..n) durante il trascinamento

    // libreria
    struct LibRow { const ModuleInfo* m; juce::Rectangle<int> row, info, add; };
    std::vector<LibRow> libRows;
    std::vector<std::pair<juce::String, int>> libCats;   // etichetta, y
    void layoutLibrary();

    // catena
    enum class Hit { none, led, card, left, right, remove, badge };
    std::pair<int, Hit> chainHit (juce::Point<int>) const;
    int chainScroll = 0, chainContentW = 0;
    int pressIndex = -1, dragIndex = -1, dragX = 0, dragGrabDx = 0;
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
    FlatButton saveBtn, presetsBtn, setupBtn, tapBtn, muteBtn;
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
    std::unique_ptr<InfoOverlay> infoOverlay;
    std::unique_ptr<SetupScreen> setup;
    std::unique_ptr<PresetScreen> presets;
    std::unique_ptr<ui::AboutOverlay> about;
};
}

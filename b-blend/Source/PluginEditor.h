#pragma once
// Interfaccia b-blend (accento teal):
//   b-blend                                  [load A] [load B] [cab C ×] [cab D ×]
//   ┌───────────────────────────────────────────────────────────────────┐
//     thump < 0 % >  air < 0 % >  locut < 0 Hz >  hicut < 20.0 kHz >   mono > stereo
//     [ blend: punto / linea / triangolo / quadrato ]  [ risposta dell'IR composito + filtri ]
//   └───────────────────────────────────────────────────────────────────┘
// Loader in alto a destra: pieni (accento) quando vuoti; con un IR mostrano nome e ×; in rosso se il
// file ha un'altra frequenza di campionamento rispetto al progetto. Si può trascinare un file sopra.
// Un clic chiede una cartella e apre il browser degli IR: tutti i file della cartella (anche nelle
// sottocartelle) con la loro risposta in frequenza, analizzata in background a partire dalle righe visibili.
// La forma del blend segue quanti IR ci sono e cambia con un'animazione breve.
#include "PluginProcessor.h"
#include "smp/Shell.h"

class IrLoader final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    IrLoader (BBlendProcessor& p, int s) : proc (p), slot (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    std::function<void (int slot, const juce::File& folder)> onBrowse;
    void filesDropped (const juce::StringArray& f, int, int) override { if (! f.isEmpty()) proc.loadIrFileAsync (slot, juce::File (f[0])); repaint(); }   // già in un altro slot: rifiutato
private:
    juce::Rectangle<int> closeArea() const { return getLocalBounds().removeFromRight (getHeight()); }
    BBlendProcessor& proc;
    int slot;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Browser degli IR di una cartella, sopra tutta l'interfaccia. Clic = carica nello slot (si può ascoltare
// e passare al successivo), doppio clic o Invio = carica e chiude, frecce su/giù = IR precedente/successivo.
// Mentre è aperto si sente solo l'IR scelto (il blend con gli altri slot torna alla chiusura). Gli IR già
// caricati negli altri slot sono segnati con la loro lettera e non si possono scegliere una seconda volta.
class IrBrowser final : public juce::Component, private juce::Thread, private juce::Timer
{
public:
    explicit IrBrowser (BBlendProcessor&);
    ~IrBrowser() override;
    void open (int slot, const juce::File& folder);
    void close();
    std::function<void()> onChangeFolder;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    int numEntries() const { return (int) entries.size(); }
    int numAnalysed() const;
    static constexpr int kPts = 96, kRowH = 72;
    juce::Point<int> mouseForTest { -1, -1 };   // test: cursore della frequenza senza il mouse vero
private:
    void run() override;
    void timerCallback() override;
    void choose (int index, bool closeAfter);
    void ensureVisible (int index);
    juce::Rectangle<int> listArea() const { return getLocalBounds().reduced (24).withTrimmedTop (62); }
    juce::Rectangle<float> curveArea (juce::Rectangle<int> row) const
    { return { (float) row.getRight() - 372.0f, (float) row.getY() + 9.0f, 360.0f, (float) kRowH - 18.0f }; }
    int usedBy (int index) const;   // altro slot che ha già questo IR (-1 = libero)
    juce::Rectangle<int> closeArea() const { return { getWidth() - 24 - 30, 20, 30, 30 }; }
    juce::Rectangle<int> folderArea() const;
    int rowAt (juce::Point<int>) const;
    struct Entry
    {
        juce::File file;
        juce::String name, detail;
        std::array<float, kPts> curve {};   // dB, 0 = massimo
        std::atomic<bool> ready { false };
        bool failed = false;
    };
    BBlendProcessor& proc;
    int slot = 0;
    juce::File folder;
    std::vector<std::unique_ptr<Entry>> entries;
    std::atomic<int> firstVisible { 0 };
    float scroll = 0, scrollTarget = 0;
    int current = -1;
    juce::String message;
    juce::AudioFormatManager formats;
};

class BlendView final : public juce::Component
{
public:
    explicit BlendView (BBlendProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void animate();
    static constexpr float kPadMargin = 30.0f;   // margine uguale attorno al pad (spazio per i nomi degli IR)
private:
    juce::Rectangle<float> pad() const;
    void setXY (juce::Point<float>);
    BBlendProcessor& proc;
    bool dragging = false;
    std::array<juce::Point<float>, 4> shown {};   // vertici animati (normalizzati), per slot
    std::array<float, 4> alpha {};                // comparsa / scomparsa dei vertici
    bool firstFrame = true;
    juce::Path curve; juce::int64 curveKey = -1;   // risposta in cache
};

class BBlendEditor final : public ui::ShellEditor
{
public:
    explicit BBlendEditor (BBlendProcessor&);
    void resized() override;
private:
    void tick() override;
    BBlendProcessor& proc;
    BlendView view;
    std::array<std::unique_ptr<IrLoader>, 4> loaders;
    IrBrowser browser;
    std::unique_ptr<juce::FileChooser> folderChooser;
    void chooseFolder (int slot);
public:
    IrBrowser& getBrowser() { return browser; }   // test
};

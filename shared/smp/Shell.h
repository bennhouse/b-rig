#pragma once
// Struttura comune della maggior parte dei plugin: titolo, selettori in alto a destra sulla linea di base del
// titolo, riquadro con una o più righe di controlli, colonne facoltative ai lati e il grafico.
// Stesse regole di b-distort / b-focus: padding 21 px, gruppi a 24 px, etichette a 8 px dai controlli.
#include "Widgets.h"

namespace ui
{

class ShellEditor : public juce::AudioProcessorEditor, protected juce::Timer
{
public:
    ShellEditor (juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, const juce::String& name, int width, int height);
    ~ShellEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    // Controlli della riga, da sinistra a destra, nell'ordine in cui si aggiungono.
    ValueBox& addValue (const juce::String& label, const char* paramId, std::function<juce::String (float)> format,
                        std::function<float (float, int)> step, juce::StringArray sizes, float normPerPixel = 0.003f);
    PixelCheckbox& addCheck (const juce::String& label, const char* paramId);
    SegmentedChoice& addChoice (const juce::String& label, const char* paramId, juce::StringArray items);
    void setHeaderChoice (const juce::String& label, const char* paramId, juce::StringArray items);
    // Altri controlli nella riga in alto, a sinistra del selettore principale (in ordine di aggiunta,
    // tutto il gruppo allineato a destra). Per i plugin con più selettori in alto (b-space, b-drop...).
    SegmentedChoice& addHeaderChoice (const juce::String& label, const char* paramId, juce::StringArray items);
    PixelCheckbox& addHeaderCheck (const juce::String& label, const char* paramId);
    // componente libero nella riga (es. un pulsante), con la sua larghezza
    void addComponent (const juce::String& caption, juce::Component& c, int width)
    { addAndMakeVisible (c); (column != 0 ? colItems : items).push_back ({ caption, &c, width, false, false, column }); }
    void newRow() { items.push_back ({ {}, nullptr, 0, false }); }   // i controlli seguenti vanno su una nuova riga
    // Colonne ai lati del grafico (b-verb, b-delay): dopo beginColumn (1 = sinistra, 2 = destra) i controlli
    // aggiunti vanno in colonna, uno sotto l'altro (etichette a sinistra, controlli allineati); endColumn() torna
    // alla riga. Il grafico prende lo spazio tra le due colonne.
    void beginColumn (int side) { column = side; }
    void endColumn() { column = 0; }
    void alignLastRight() { if (! items.empty()) items.back().right = true; }   // l'ultimo controllo aggiunto va a destra nella sua riga
    void setDisplay (juce::Component& c) { display = &c; addAndMakeVisible (c); }
    virtual void tick() {}                        // 30 volte al secondo
    juce::Rectangle<int> box() const;
    juce::String infoText;                        // a destra della riga dei controlli (es. "stereo")
    juce::String titleText;                       // facoltativo: nome più corto nel titolo (il pannello about usa quello vero)
    juce::Colour frameColour = frame;             // colore del riquadro (es. avviso)
    bool displayRefreshesItself = false;          // true: al cambio di un parametro non si ridisegna il grafico (lo fa lui)

    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<SegmentedChoice> headerChoice;

private:
    void timerCallback() override;
    struct Item { juce::String label; juce::Component* comp; int width; bool square; bool right = false; int col = 0; };
    int column = 0;
    std::vector<Item> colItems;   // controlli nelle colonne laterali
    std::vector<Item> items, headerItems;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::Component* display = nullptr;
    juce::String name, headerLabel;
    struct TextItem { juce::String text; juce::Rectangle<int> area; };
    std::vector<TextItem> labels;
    juce::Rectangle<int> infoArea;
    juce::int64 lastSignature = 0;
    std::unique_ptr<AboutOverlay> about;
};

} // namespace ui

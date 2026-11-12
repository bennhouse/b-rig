#pragma once
// Controlli generici condivisi: checkbox (anche come meter), selettore a segmenti, box valore.
#include "Theme.h"
#include "About.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{

// Modifica a tastiera sovrapposta a un controllo (doppio clic): il testo parte tutto selezionato,
// evidenziato in grigio chiaro neutro. Invio, Tab o clic fuori = conferma, ma solo se si è scritto
// qualcosa (altrimenti resta il valore com'era); Esc = annulla.
class InlineEditor final : public juce::TextEditor
{
public:
    InlineEditor();
    void start (juce::Component& owner, juce::Rectangle<int> area, const juce::String& initial);
    std::function<void (const juce::String&)> onCommit;
    // Numero da un testo scritto: virgola o punto, "k" = migliaia (es. "2.5k" = 2500).
    static float parseNumber (const juce::String& typed);

    ~InlineEditor() override;

private:
    void finish (bool commit);
    bool editing = false, typed = false;
    // ascolta i clic in tutta la finestra: un clic fuori dal campo chiude la modifica
    struct OutsideClick final : juce::MouseListener
    {
        InlineEditor& ed;
        explicit OutsideClick (InlineEditor& e) : ed (e) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (ed.editing && ! ed.getScreenBounds().contains (e.getScreenPosition())) ed.finish (true);
        }
    } outside { *this };
};

// Checkbox quadrata: spenta = perimetro sottile tratteggiato, accesa = quadrato giallo pieno.
class PixelCheckbox final : public juce::Component
{
public:
    std::function<bool()> getState;
    std::function<void (bool)> setState;
    // Facoltativo: modalità meter (0..1). Accesa = solo bordo di accento e l'interno si
    // riempie dall'alto in proporzione al valore (es. riduzione di guadagno del compressore).
    std::function<float()> meter;

    void bindTo (juce::RangedAudioParameter& p);   // collega a un parametro booleano

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

// Selettore a segmenti per un parametro a scelta: [ l | r ], [ zero | linear ] ...
// Il segmento scelto è pieno di giallo; da disabilitato è attenuato.
class SegmentedChoice final : public juce::Component
{
public:
    SegmentedChoice (juce::RangedAudioParameter& choiceParam, juce::StringArray labels);
    std::function<void (int)> onUserChange;   // solo per i clic dell'utente (non per automazione o caricamento)
    std::function<void (int from, int to)> onUserSwitch;   // come onUserChange, con il segmento di prima
    std::function<void (int)> onDoubleClick;  // doppio clic su un segmento (es. valori del preset ai default)
    // Parametri con uno stato "spento" prima delle scelte (es. tuning: off, standard, drop, baritone):
    // il segmento i corrisponde al valore i + valueOffset; i valori sotto l'offset = nessun segmento acceso.
    // Con allowDeselect un clic sul segmento acceso torna al valore 0 (spento).
    int valueOffset = 0;
    bool allowDeselect = false;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void enablementChanged() override { repaint(); }

    int getIdealWidth() const;

private:
    int segmentAt (int x) const;
    int segmentNatural (int i) const;
    int edgeAt (int i) const;
    int selected() const;

    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    int hover = -1;
};

// Valore numerico modificabile:   <  [ 15,00 ]  >
// Il riquadro avvolge solo il valore, che è equidistante dai 4 bordi; le frecce
// stanno subito fuori, alla stessa distanza. Frecce/rotella = un passo,
// trascinamento orizzontale sul numero = scorrimento continuo.
// La larghezza segue il testo: quando cambia, onWidthChanged avvisa l'editor.
class ValueBox final : public juce::Component
{
public:
    std::function<juce::String (float)> format;                          // valore -> testo
    std::function<float (float value, int dir)> step;                    // un passo di freccia
    std::function<float (float startValue, float startNorm, float dx)> drag;  // valore durante il trascinamento
    std::function<void()> onWidthChanged;
    // Facoltativi: default diverso da quello del parametro (es. dipende dal preset scelto)
    // e lettura del testo scritto a tastiera (di base: InlineEditor::parseNumber).
    std::function<float()> defaultValue;
    std::function<float (const juce::String&)> parse;
    // Testi d'esempio dei valori più larghi possibili: il riquadro prende la misura del più largo
    // e non si ridimensiona mai mentre il valore cambia (cifre Plex Mono a larghezza fissa).
    juce::StringArray sizeSamples;
    int extraWidth = 0;   // margine in più (px) oltre al valore più largo: riquadro più comodo, sempre fisso

    void setParameter (juce::RangedAudioParameter* p);
    juce::RangedAudioParameter* getParameter() const { return param; }
    int getIdealWidth() const;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // doppio clic sul numero: torna al default e apre la modifica a tastiera (testo selezionato);
    // se non si scrive nulla resta il default
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void enablementChanged() override { hover = Zone::None; repaint(); }

    static constexpr int kArrowW = 6;     // larghezza della freccia

private:
    enum class Zone { None, Left, Number, Right };
    Zone zoneAt (juce::Point<int>) const;
    juce::String currentText() const;
    float currentValue() const;
    void setValue (float v);
    void doStep (int dir);
    void checkWidth();

    juce::RangedAudioParameter* param = nullptr;
    Zone hover = Zone::None;
    bool dragging = false;
    float dragStartValue = 0.0f, dragStartNorm = 0.0f;
    InlineEditor editor;
    juce::Rectangle<int> boxArea() const;

    // cache: i contorni dei glifi si ricostruiscono solo quando il testo cambia
    const InkText& inkFor (const juce::String& s) const;
    mutable juce::String cachedText;
    mutable InkText cachedInk;
    mutable float cachedSamplesWidth = -1.0f;
    mutable int cachedSamplesHash = 0;
};

// Dentro b-rig il titolo del plugin non c'è: i controlli della riga in alto (e le aree delle loro etichette)
// si spostano in orizzontale per stare al centro dell'editor. Fuori da b-rig non fa nulla.
inline void centreHeaderInRig (int editorWidth, std::vector<juce::Component*> comps, std::vector<juce::Rectangle<int>*> areas = {})
{
    if (! kInRig) return;
    juce::Rectangle<int> u;
    auto add = [&u] (juce::Rectangle<int> r) { u = u.isEmpty() ? r : u.getUnion (r); };
    for (auto* c : comps) if (c != nullptr && c->isVisible()) add (c->getBounds());
    for (auto* a : areas) if (a != nullptr && ! a->isEmpty()) add (*a);
    if (u.isEmpty()) return;
    const int dx = (editorWidth - u.getWidth()) / 2 - u.getX();
    for (auto* c : comps) if (c != nullptr) c->setBounds (c->getBounds().translated (dx, 0));
    for (auto* a : areas) if (a != nullptr) *a = a->translated (dx, 0);
}

// Logo premibile + pannello "about" (uguale in tutti i plugin): un clic sul logotipo in alto a sinistra apre
// sopra a tutta la finestra una breve descrizione del plugin, la firma e "crafted in Italy, built with Claude".
// Un clic ovunque (o Esc) lo chiude. Si aggancia all'editor da solo e ne segue le dimensioni.
class AboutOverlay final : public juce::Component, private juce::ComponentListener
{
public:
    AboutOverlay (juce::Component& editor, const juce::String& pluginName);   // testo da About.h
    ~AboutOverlay() override;
    void show();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    std::function<juce::Colour()> titleColour;   // facoltativo: colore della "b" (es. b-amp, per canale)
    juce::Point<int> logoAt { 23, 16 };          // angolo del logotipo premibile (b-rig lo ha nella colonna della libreria)

private:
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void place();
    struct Hotspot final : juce::Component
    {
        AboutOverlay& owner;
        explicit Hotspot (AboutOverlay& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity (false); }
        void mouseDown (const juce::MouseEvent&) override { owner.show(); }
    } hotspot { *this };
    juce::Component& ed;
    juce::String name, text;
};

} // namespace ui

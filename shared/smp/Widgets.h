#pragma once
// Controlli generici condivisi: checkbox (anche come meter), selettore a segmenti, box valore.
#include "Theme.h"
#include "About.h"
#include "Updates.h"
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

// Valore principale in grande (i due o tre controlli che contano di più in un plugin): etichetta sopra e, sotto,
// un riquadro fisso con il numero grande in Plex Mono e il riempimento grigio come nei box; il bordo inferiore del
// riquadro è la linea del valore, nel colore del plugin dal minimo fin dove arriva il parametro. Il numero è allineato a destra in uno spazio largo
// quanto il valore più lungo possibile, centrato nel riquadro: l'unità resta ferma e quando cambiano le cifre il
// numero non si sposta (crescono solo verso sinistra).
// Trascinamento (destra o su = più), rotella = un passo, doppio clic = default e modifica a tastiera.
// Larghezza fissa dal caso peggiore, come ValueBox.
class BigValue final : public juce::Component
{
public:
    juce::String label;
    std::function<juce::String (float)> format;
    std::function<float (float value, int dir)> step;
    juce::StringArray sizeSamples;
    float normPerPixel = 0.004f;
    static constexpr float kNumberSize = 24.0f, kUnitSize = kValueSize;   // poche misure di testo: 24 e quelle dei box
    static constexpr int kBoxH = 40, kHeight = 22 + kBoxH;   // etichetta + riquadro

    void setParameter (juce::RangedAudioParameter* p) { param = p; repaint(); }
    juce::RangedAudioParameter* getParameter() const { return param; }
    int getIdealWidth() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void setValue (float v);
    float numberBaseline() const;
    juce::Rectangle<float> boxArea() const { return { 0.5f, (float) (kHeight - kBoxH) + 0.5f, (float) getWidth() - 1.0f, (float) kBoxH - 1.0f }; }
    float widestInk() const;   // inchiostro del valore più largo (caso peggiore, in cache)
    mutable float cachedWidest = -1.0f;
    mutable juce::RangedAudioParameter* cachedFor = nullptr;
    juce::RangedAudioParameter* param = nullptr;
    bool dragging = false;
    float startNorm = 0.0f;
    InlineEditor editor;
};

// Pulsante acceso / spento con il nome scritto (freeze, ping-pong, trails...): spento = bordo e testo chiaro,
// acceso = pieno nel colore del plugin con il testo scuro, come un segmento scelto.
class TextToggle final : public juce::Component
{
public:
    explicit TextToggle (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void bindTo (juce::RangedAudioParameter& p) { param = &p; }
    bool isOn() const { return param != nullptr && param->getValue() > 0.5f; }
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    juce::String text;
    juce::RangedAudioParameter* param = nullptr;
};

// Gruppo di controlli con un titolo (b-verb, b-delay): "[■] modulation" e sotto le sue righe (etichetta a sinistra,
// controllo allineato). Con un parametro on/off la casella accanto al titolo accende o spegne il gruppo: spento,
// titolo ed etichette diventano grigio scuro e i controlli si disabilitano (si vede subito cosa non sta lavorando).
class Section final : public juce::Component
{
public:
    explicit Section (juce::String title, juce::RangedAudioParameter* onOff = nullptr);
    void add (const juce::String& rowLabel, juce::Component& c, int width);
    int getIdealWidth() const;
    int getIdealHeight() const;
    bool isOn() const { return onParam == nullptr || onParam->getValue() > 0.5f; }
    void refresh();   // dal timer dell'editor: abilita / disabilita le righe quando cambia l'on/off
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kTitleH = 22, kRowGap = 10;
private:
    struct Row { juce::String label; juce::Component* comp; int width; };
    juce::String title;
    juce::RangedAudioParameter* onParam = nullptr;
    PixelCheckbox power;
    std::vector<Row> rows;
    bool lastOn = true;
    int labelWidth() const;
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
// sopra a tutta la finestra una breve descrizione del plugin, la firma, "free and open source, built with Claude Code"
// e la versione. Un clic ovunque (o Esc) lo chiude. Si aggancia all'editor da solo e ne segue le dimensioni.
// Aggiornamenti (Updates.h): se c'è una versione più nuova compare un piccolo avviso nell'angolo in basso a
// destra (un clic apre questo pannello, la x lo chiude) e resta un quadratino nel colore del plugin accanto al
// logotipo; nel pannello la nuova versione e il tasto "download" che apre la pagina della release.
class AboutOverlay final : public juce::Component, private juce::ComponentListener
{
public:
    AboutOverlay (juce::Component& editor, const juce::String& pluginName);   // testo da About.h
    ~AboutOverlay() override;
    void show();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    std::function<juce::Colour()> titleColour;   // facoltativo: colore della "b" (es. b-amp, per canale)
    juce::Point<int> logoAt { 23, 16 };          // angolo del logotipo premibile (b-rig lo ha nella colonna della libreria)
    void setLogoText (const juce::String& t) { if (t != logoText) { logoText = t; place(); } }   // nome più corto nel logotipo (b-fs)
    void setUpdateForTest (const updates::Available& a) { setUpdate (a); }
    bool toastVisibleForTest() const { return toast.isVisible(); }
    void clickToastForTest (bool onClose);

private:
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void place();
    void setUpdate (const updates::Available&);
    juce::Rectangle<int> downloadArea;   // tasto "download" nel pannello (vuoto se non c'è un aggiornamento)
    struct Hotspot final : juce::Component
    {
        AboutOverlay& owner;
        explicit Hotspot (AboutOverlay& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity (false); }
        void mouseDown (const juce::MouseEvent&) override { owner.show(); }
        void paint (juce::Graphics&) override;
    } hotspot { *this };
    // avviso nell'angolo in basso a destra: "update available" e la nuova versione
    struct Toast final : juce::Component
    {
        AboutOverlay& owner;
        explicit Toast (AboutOverlay& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity (true); }
        juce::Rectangle<int> closeArea() const { return getLocalBounds().removeFromRight (34); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::String line() const;
    } toast { *this };
    juce::Component& ed;
    juce::String name, text, logoText;
    updates::Available update;
    std::unique_ptr<updates::Watcher> watcher;
};

} // namespace ui

#pragma once
// Design system condiviso dei Self Made Plugins (b-aftercab, b-distort, b-focus).
// Minimal e geometrico: nessun angolo arrotondato, nessuna linea tratteggiata, solo linee
// continue da 1 px. Tipografia: IBM Plex Sans (nomi, titoli, selettori) e IBM Plex Mono
// (valori, assi, contatori: cifre a larghezza fissa, i box non "ballano" quando i valori cambiano).

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <map>
#include "BinaryData.h"

// SMP_IN_RIG = 1 quando il plugin è compilato dentro b-rig (l'app standalone): titolo, about e strumenti
// che l'app ha già (tuner, metronomo) vanno nascosti. I plugin nuovi controllano ui::kInRig.
#ifndef SMP_IN_RIG
 #define SMP_IN_RIG 0
#endif

#ifndef SMP_ACCENT_ARGB
 #error "Definire SMP_ACCENT_ARGB nel CMakeLists del plugin (es. 0xffffa200)"
#endif

namespace ui
{
    constexpr bool kInRig = SMP_IN_RIG != 0;

    // --- Palette
    inline const juce::Colour background  { 0xff121212 };   // sfondo
    inline const juce::Colour onAccent    { 0xff121212 };   // testo sopra il colore di accento
    inline const juce::Colour labelStatic { 0xff8e8e8e };   // etichette descrittive (delverb, freq, mix...)
    inline const juce::Colour selector    { 0xffcccccc };   // testo dei selettori inattivi
    inline const juce::Colour labelHover  { 0xffe6e6e6 };
    inline const juce::Colour valueText   { 0xfff0f0f0 };   // valori numerici nei box
    inline const juce::Colour unitText    { 0xff888888 };   // unità di misura nei box
    inline const juce::Colour titleDash   { 0xff666666 };   // trattino del logotipo
    inline const juce::Colour titleName   { 0xffe0e0e0 };   // nome del plugin nel logotipo
    inline const juce::Colour label       { 0xffa3a3a3 };   // testi secondari (es. info della banda)
    inline const juce::Colour text        { 0xffa3a3a3 };   // bordi dei controlli
    inline const juce::Colour info        { 0xff808080 };   // testi informativi (assi, stato): 4,7:1 sullo sfondo (AA)
    inline const juce::Colour frame       { 0xff525252 };   // riquadri, funzioni disattivate
    inline const juce::Colour gridLine    { 0xff1f1f1f };   // griglia interna, molto scura
    inline const juce::Colour gridZero    { 0xff303030 };   // linea dello 0
    inline const juce::Colour valueFill   { 0xff2c2c2c };   // barra di riempimento dei controlli numerici (min a sinistra, max a destra)
    inline const juce::Colour valueFillOff{ 0xff1c1c1c };   // la stessa barra con il controllo disattivato
    inline const juce::Colour editHighlight { 0xffc8c8c8 }; // selezione del testo nella modifica a tastiera (chiaro neutro)
    // Colore di accento: lo definisce ogni plugin nel suo CMakeLists (SMP_ACCENT_ARGB).
    inline const juce::Colour accent      { (juce::uint32) SMP_ACCENT_ARGB };
    // Accento per componente: un editor che cambia colore (es. b-amp per canale) imposta
    // setColour (kAccentColourId, c) su di sé e i controlli figli lo ereditano; altrimenti vale "accent".
    constexpr int kAccentColourId = 0x5b4d5001;
    inline juce::Colour accentOf (const juce::Component& c)
    {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (p->isColourSpecified (kAccentColourId)) return p->findColour (kAccentColourId);
        return accent;
    }

    constexpr float kLine = 1.0f;          // spessore di tutte le linee
    constexpr int kBoxSize = 15;           // checkbox e pulsante smart phase
    constexpr int kControlH = 19;          // altezza di box valore e selettori
    constexpr float kValueSize = 14.0f;    // valori numerici (Plex Mono Medium)
    constexpr float kUnitSize = 13.0f;     // unità di misura nei box (1 px più piccole)
    constexpr float kControlTextSize = 14.0f;  // selettori e pulsanti
    constexpr float kLabelSize = 13.0f;    // etichette descrittive (1 px meno dei controlli)
    constexpr float kInfoSize  = 13.0f;    // testi informativi
    constexpr float kAxisSize  = 12.0f;    // numeri sugli assi e dei nodi
    constexpr float kAxisSmall = 11.0f;    // numeri intermedi sugli assi (2k, 4k...)
    constexpr float kTagSize   = 11.0f;    // etichette interne ai grafici (OUT, IN)

    // Spaziature (unica scala per tutta l'interfaccia)
    constexpr int kGapLabel = 8;    // etichetta -> suo controllo
    constexpr int kGapInner = 12;   // tra controlli dello stesso gruppo (checkbox -> selettore)
    constexpr int kGapGroup = 24;   // tra gruppi diversi
    constexpr int kFramePad = 21;   // padding interno dei riquadri, uguale sui 4 lati (e tra le righe in basso)
    constexpr int kHitPad   = 4;    // area cliccabile in più attorno a checkbox e pulsanti quadrati

    // --- Caratteri IBM Plex (licenza OFL)
    enum class Face { SansLight, SansRegular, SansMedium, SansSemiBold, SansBold, MonoRegular, MonoMedium, MonoSemiBold };

    // I typeface vivono in un singleton "DeletedAtShutdown": JUCE li libera durante il proprio
    // spegnimento (quando l'host chiude l'ultima istanza), non dopo, nei distruttori statici.
    struct FontCache final : juce::DeletedAtShutdown
    {
        std::array<juce::Typeface::Ptr, 8> faces;
        ~FontCache() override { clearSingletonInstance(); }
        JUCE_DECLARE_SINGLETON_INLINE (FontCache, false)
    };

    inline juce::Typeface::Ptr typeface (Face f)
    {
        auto& tf = FontCache::getInstance()->faces[(size_t) f];
        if (tf == nullptr)
        {
            auto load = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); };
            switch (f)
            {
                case Face::SansLight:    tf = load (BinaryData::IBMPlexSansLight_ttf,    BinaryData::IBMPlexSansLight_ttfSize); break;
                case Face::SansRegular:  tf = load (BinaryData::IBMPlexSansRegular_ttf,  BinaryData::IBMPlexSansRegular_ttfSize); break;
                case Face::SansMedium:   tf = load (BinaryData::IBMPlexSansMedium_ttf,   BinaryData::IBMPlexSansMedium_ttfSize); break;
                case Face::SansSemiBold: tf = load (BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize); break;
                case Face::SansBold:     tf = load (BinaryData::IBMPlexSansBold_ttf,     BinaryData::IBMPlexSansBold_ttfSize); break;
                case Face::MonoRegular:  tf = load (BinaryData::IBMPlexMonoRegular_ttf,  BinaryData::IBMPlexMonoRegular_ttfSize); break;
                case Face::MonoMedium:   tf = load (BinaryData::IBMPlexMonoMedium_ttf,   BinaryData::IBMPlexMonoMedium_ttfSize); break;
                case Face::MonoSemiBold: tf = load (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize); break;
            }
        }
        return tf;
    }

    // In JUCE l'altezza del font è ascendente + discendente (1,3 em per Plex): "em" converte il tracking.
    constexpr float kEmPerHeight = 1.0f / 1.3f;

    inline juce::Font face (Face f, float height, float trackingEm = 0.0f)
    {
        return juce::Font (juce::FontOptions (typeface (f)).withHeight (height).withKerningFactor (trackingEm * kEmPerHeight));
    }

    // Font delle etichette descrittive (anche per misurarne la larghezza nei layout).
    inline juce::Font font (float height) { return face (Face::SansMedium, height); }

    // Numeri con il punto decimale (es. -12.0)
    inline juce::String num (double v, int decimals) { return juce::String (v, decimals); }

    inline juce::String freqText (double hz)
    {
        return hz >= 1000.0 ? num (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : num (hz, 0) + " Hz";
    }

    // Altezza delle cifre per una dimensione (Plex Sans e Plex Mono condividono le stesse
    // proporzioni): serve a centrare il testo sull'inchiostro, così una riga ha una sola linea di base.
    inline float capHeight (float size, Face f = Face::SansMedium)
    {
        static std::map<int, float> cache;
        const int key = juce::roundToInt (size * 100.0f) * 16 + (int) f;
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        juce::GlyphArrangement ga;
        ga.addLineOfText (face (f, size), "0", 0.0f, 0.0f);
        juce::Path p;
        ga.createPath (p);
        return cache[key] = -p.getBounds().getY();
    }

    // Testo trasformato in tracciato, con le misure dell'inchiostro reale: serve a centrare un
    // valore nel suo riquadro. Un valore può avere un'unità (es. "15.42" + "ms") con un altro stile.
    struct InkText
    {
        juce::Path path;       // numero, baseline a y = 0
        juce::Path unitPath;   // unità di misura (vuota se non c'è)
        float width = 0;       // larghezza dell'inchiostro di tutto il testo
        float left = 0;        // x dell'inchiostro rispetto all'origine
        float capHeight = 0;   // altezza delle cifre (dalla baseline in su)
    };

    inline InkText makeInkText (const juce::String& s, float size, Face f = Face::MonoMedium)
    {
        InkText t;
        juce::GlyphArrangement ga;
        ga.addLineOfText (face (f, size), s, 0.0f, 0.0f);
        ga.createPath (t.path);
        const auto b = t.path.getBounds();
        t.left = b.getX();
        t.width = b.getWidth();
        t.capHeight = capHeight (size, f);
        return t;
    }

    // Valore con unità: numero in Plex Mono Medium, unità in Plex Mono Regular 1 px più piccola.
    // "15.42 ms" -> "15.42" + "ms"; "61 / 39" resta tutto numero.
    inline InkText makeValueInk (const juce::String& s)
    {
        const int sp = s.lastIndexOfChar (' ');
        const auto tail = sp > 0 ? s.substring (sp + 1) : juce::String();
        const bool hasUnit = tail.isNotEmpty() && ! tail.containsAnyOf ("0123456789");
        if (! hasUnit) return makeInkText (s, kValueSize, Face::MonoMedium);

        InkText t = makeInkText (s.substring (0, sp), kValueSize, Face::MonoMedium);
        const float numberAdvance = juce::GlyphArrangement::getStringWidth (face (Face::MonoMedium, kValueSize), s.substring (0, sp + 1));
        juce::GlyphArrangement ga;
        ga.addLineOfText (face (Face::MonoRegular, kUnitSize), tail, numberAdvance, 0.0f);
        ga.createPath (t.unitPath);
        const auto all = t.path.getBounds().getUnion (t.unitPath.getBounds());
        t.left = all.getX();
        t.width = all.getWidth();
        return t;
    }

    // Contorno continuo da 1 px, angoli vivi, allineato ai pixel.
    inline void outline (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.drawRect (r.toNearestInt(), 1);
    }

    // Testo centrato verticalmente sull'altezza delle cifre; orizzontalmente secondo j.
    inline void drawLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                           juce::Colour c, float size = kLabelSize,
                           juce::Justification j = juce::Justification::centredLeft,
                           Face f = Face::SansMedium, float trackingEm = 0.0f)
    {
        g.setColour (c);
        g.setFont (face (f, size, trackingEm));
        const int baseline = juce::roundToInt ((float) r.getCentreY() + capHeight (size, f) * 0.5f);
        if (j.testFlags (juce::Justification::right))
            g.drawSingleLineText (s, r.getRight(), baseline, juce::Justification::right);
        else if (j.testFlags (juce::Justification::horizontallyCentred))
            g.drawSingleLineText (s, r.getCentreX(), baseline, juce::Justification::horizontallyCentred);
        else
            g.drawSingleLineText (s, r.getX(), baseline, juce::Justification::left);
    }

    // Testo su una linea di base data (numeri degli assi: Plex Mono Regular di default).
    inline void drawAtBaseline (juce::Graphics& g, const juce::String& s, float x, float baseline,
                                juce::Colour c, float size, juce::Justification j = juce::Justification::left,
                                Face f = Face::MonoRegular, float trackingEm = 0.0f)
    {
        g.setColour (c);
        g.setFont (face (f, size, trackingEm));
        g.drawSingleLineText (s, juce::roundToInt (x), juce::roundToInt (baseline), j);
    }

    // Etichetta interna a un grafico (es. OUT, IN): maiuscolo, SemiBold, piccola, spaziata.
    inline void drawTag (juce::Graphics& g, const juce::String& s, float x, float baseline,
                         juce::Justification j = juce::Justification::left)
    {
        drawAtBaseline (g, s.toUpperCase(), x, baseline, info, kTagSize, j, Face::SansSemiBold, 0.08f);
    }

    // Valore con unità dentro un grafico (es. "8 kHz 0.53 s", "380 ms"): Plex Mono, mai in maiuscolo,
    // così le unità restano scritte giuste (Hz, kHz, ms, dB).
    inline void drawValueTag (juce::Graphics& g, const juce::String& s, float x, float baseline,
                              juce::Justification j = juce::Justification::left)
    {
        drawAtBaseline (g, s, x, baseline, info, kAxisSize, j, Face::MonoRegular);
    }

    // Linea di base che drawLabel userebbe per un rettangolo centrato in y.
    inline float baselineFor (float centreY, float size, Face f = Face::SansMedium) { return centreY + capHeight (size, f) * 0.5f; }

    // Logotipo: "b" Bold nel colore del plugin, "-" Light grigio, nome Medium chiaro con tracking -0.02em.
    inline void drawTitleAlways (juce::Graphics& g, const juce::String& name, juce::Rectangle<int> r, float size,
                                 juce::Colour bColour = accent)
    {
        const float baseline = std::round (baselineFor ((float) r.getCentreY(), size, Face::SansBold));
        float x = (float) r.getX();
        auto put = [&] (const juce::String& s, Face f, juce::Colour c, float tracking)
        {
            const auto fnt = face (f, size, tracking);
            g.setColour (c);
            g.setFont (fnt);
            g.drawSingleLineText (s, juce::roundToInt (x), juce::roundToInt (baseline));
            x += juce::GlyphArrangement::getStringWidth (fnt, s);
        };
        put ("b", Face::SansBold, bColour, 0.0f);
        put ("-", Face::SansLight, titleDash, 0.0f);
        put (name, Face::SansMedium, titleName, -0.02f);
    }

    // Logotipo del plugin; dentro b-rig non si disegna (il nome del modulo è già nell'app).
    inline void drawTitle (juce::Graphics& g, const juce::String& name, juce::Rectangle<int> r, float size,
                           juce::Colour bColour = accent)
    {
        if (! kInRig) drawTitleAlways (g, name, r, size, bColour);
    }

    // Logotipo in alto a sinistra (uguale in tutti i plugin): area {kMargin - 1, 16, w, 36}, corpo 30.
    constexpr float kLogoSize = 30.0f;
    inline float logoBaseline() { return std::round (baselineFor (34.0f, kLogoSize, Face::SansBold)); }
    // Riga di controlli sulla stessa linea del titolo (selettori in alto a destra): centrata in verticale
    // sull'altezza delle lettere del titolo, così box e nome del plugin hanno lo stesso asse.
    inline int headerRowY (int height = kControlH)
    {
        const float centre = logoBaseline() - capHeight (kLogoSize, Face::SansBold) * 0.5f;
        return juce::roundToInt (centre - (float) height * 0.5f);
    }
    inline float logoWidth (const juce::String& name, float size = kLogoSize)
    {
        return juce::GlyphArrangement::getStringWidth (face (Face::SansBold, size), "b")
             + juce::GlyphArrangement::getStringWidth (face (Face::SansLight, size), "-")
             + juce::GlyphArrangement::getStringWidth (face (Face::SansMedium, size, -0.02f), name);
    }

    // Freccia angolare sottile: dir = -1 (<) o +1 (>)
    inline void chevron (juce::Graphics& g, juce::Rectangle<float> r, int dir, juce::Colour c)
    {
        const auto ctr = r.getCentre();
        const float w = 3.5f, h = 4.5f;
        juce::Path p;
        p.startNewSubPath (ctr.x - (float) dir * w * 0.5f, ctr.y - h);
        p.lineTo (ctr.x + (float) dir * w * 0.5f, ctr.y);
        p.lineTo (ctr.x - (float) dir * w * 0.5f, ctr.y + h);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    }

    // Curva morbida (Catmull-Rom -> Bézier cubiche) attraverso i punti: per spettri e inviluppi senza spigoli.
    inline juce::Path smoothPath (const juce::Point<float>* pts, int n)
    {
        juce::Path p;
        if (n <= 0) return p;
        p.startNewSubPath (pts[0]);
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto p0 = pts[i > 0 ? i - 1 : 0], p1 = pts[i], p2 = pts[i + 1], p3 = pts[i + 2 < n ? i + 2 : n - 1];
            p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        return p;
    }

    // Strato statico in cache (griglie, assi, etichette fisse): si ridisegna solo quando cambia la chiave
    // (dimensioni, scala dello schermo, parametri che lo modificano); a ogni frame si copia l'immagine.
    struct LayerCache
    {
        juce::Image img;
        juce::int64 key = -1;
        template <typename Painter>
        void draw (juce::Graphics& g, juce::Rectangle<int> area, juce::int64 newKey, Painter&& painter)
        {
            const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
            newKey = newKey * 31 + (juce::int64) (sc * 100.0f) * 7 + area.getWidth() * 131 + area.getHeight() * 1009;
            if (newKey != key || ! img.isValid())
            {
                key = newKey;
                img = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * sc)),
                                   juce::jmax (1, juce::roundToInt ((float) area.getHeight() * sc)), true);
                juce::Graphics ig (img);
                ig.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (sc));
                painter (ig);
            }
            g.drawImage (img, area.toFloat());
        }
        void invalidate() { key = -1; }
    };
}

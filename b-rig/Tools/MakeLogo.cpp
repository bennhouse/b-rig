// Genera il logo della b-suite (SVG) in tre formati:
//   b-suite-banner.svg     1280 x 420   per il README di GitHub
//   b-suite-fullscreen.svg 1920 x 1080  sfondo, video, presentazioni
//   b-suite-icon.svg       1024 x 1024  icona quadrata (avatar, social)
// Logotipo "b-suite" (b Plex Sans Bold e nome Medium nello stesso colore chiaro, trattino Light grigio), sotto
// una fila di quadrati, uno per plugin nel suo colore, in ordine di catena; in basso a destra la firma.
// Il testo è convertito in tracciati: l'SVG si vede uguale anche senza i font IBM Plex.
//   ./build/BSuiteLogo_artefacts/Release/BSuiteLogo ../docs [cartella per le anteprime PNG]
// e la vetrina dei plugin (vedi showcase più sotto).
#include "smp/Theme.h"
#include <cstdio>

namespace
{
using namespace ui;

const juce::uint32 kChain[] = { 0xffc084fc /* drop */, 0xff3d8bff /* focus */, 0xfffb923c /* fs */, 0xff84cc16 /* tight */,
                                0xff6366f1 /* wire */, 0xfffb7185 /* starve */, 0xffff4d4d /* distort */, 0xffe11d48 /* amp */,
                                0xff14b8a6 /* blend */, 0xff22d3ee /* double */, 0xffffa200 /* aftercab */, 0xfffacc15 /* dynaverb */,
                                0xff818cf8 /* delay */, 0xff06b6d4 /* verb */, 0xffa78bfa /* space */, 0xfff1f5f9 /* scenery */,
                                0xffde50c9 /* adapt */, 0xff38bdf8 /* meter */, 0xffe2e8f0 /* synthesia */ };
constexpr int kSquares = (int) std::size (kChain);

juce::String hex (juce::Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }

juce::String svgPath (const juce::Path& p)
{
    juce::String d;
    auto n = [] (float v) { return juce::String (v, 2); };
    juce::Path::Iterator it (p);
    while (it.next())
    {
        using E = juce::Path::Iterator::PathElementType;
        switch (it.elementType)
        {
            case E::startNewSubPath: d << "M" << n (it.x1) << " " << n (it.y1); break;
            case E::lineTo:          d << "L" << n (it.x1) << " " << n (it.y1); break;
            case E::quadraticTo:     d << "Q" << n (it.x1) << " " << n (it.y1) << " " << n (it.x2) << " " << n (it.y2); break;
            case E::cubicTo:         d << "C" << n (it.x1) << " " << n (it.y1) << " " << n (it.x2) << " " << n (it.y2) << " " << n (it.x3) << " " << n (it.y3); break;
            case E::closePath:       d << "Z"; break;
        }
    }
    return d;
}

// testo -> tracciato con la linea di base in y = 0; restituisce l'avanzamento orizzontale
float textPath (juce::Path& out, const juce::String& s, Face f, float size, float x = 0.0f, float tracking = 0.0f)
{
    const auto fnt = face (f, size, tracking);
    juce::GlyphArrangement ga;
    ga.addLineOfText (fnt, s, x, 0.0f);
    ga.createPath (out);
    return juce::GlyphArrangement::getStringWidth (fnt, s);
}

juce::Path moved (juce::Path p, float dx, float dy) { p.applyTransform (juce::AffineTransform::translation (dx, dy)); return p; }

// una scena: forme piene, ognuna col suo colore
struct Scene
{
    float w, h;
    std::vector<std::pair<juce::Path, juce::Colour>> shapes;
    void add (juce::Path p, juce::Colour c) { shapes.emplace_back (std::move (p), c); }

    juce::String svg() const
    {
        juce::String s;
        s << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << (int) w << " " << (int) h << "\" width=\"" << (int) w
          << "\" height=\"" << (int) h << "\" role=\"img\" aria-label=\"b-suite\">\n"
          << "<rect width=\"100%\" height=\"100%\" fill=\"" << hex (background) << "\"/>\n";
        for (auto& [p, c] : shapes) s << "<path fill=\"" << hex (c) << "\" d=\"" << svgPath (p) << "\"/>\n";
        return s << "</svg>\n";
    }
    juce::Image image() const
    {
        juce::Image img (juce::Image::ARGB, (int) w, (int) h, true);
        juce::Graphics g (img);
        g.fillAll (background);
        for (auto& [p, c] : shapes) { g.setColour (c); g.fillPath (p); }
        return img;
    }
};

// fila di quadrati centrata in x, con il bordo alto in y
void addStrip (Scene& s, float cx, float y, float width, float gap)
{
    const float sq = (width - gap * (float) (kSquares - 1)) / (float) kSquares, left = cx - width * 0.5f;
    for (int i = 0; i < kSquares; ++i)
    {
        juce::Path r; r.addRectangle (left + (float) i * (sq + gap), y, sq, sq);
        s.add (r, juce::Colour (kChain[i]));
    }
}

// firma in basso a destra
void addSignature (Scene& s, float size, float margin)
{
    juce::Path p; textPath (p, juce::String (juce::CharPointer_UTF8 ("free and open source  \xc2\xb7  built with Claude Code")), Face::SansRegular, size);
    const auto b = p.getBounds();
    s.add (moved (p, s.w - margin - b.getRight(), s.h - margin - b.getBottom()), info);
}

// logotipo + fila di colori + sottotitolo, centrati nella scena
void addLockup (Scene& s, float size, float stripW, float gap, float tagSize, float yShift = 0.0f)
{
    juce::Path b, dash, name;
    float x = 0.0f;
    x += textPath (b, "b", Face::SansBold, size, x);
    x += textPath (dash, "-", Face::SansLight, size, x);
    textPath (name, "suite", Face::SansMedium, size, x, -0.02f);
    juce::Path all; all.addPath (b); all.addPath (dash); all.addPath (name);
    const auto ink = all.getBounds();

    juce::Path tag; textPath (tag, "guitar plugins and a standalone rig", Face::SansRegular, tagSize);
    const auto tb = tag.getBounds();
    const float sq = (stripW - gap * (float) (kSquares - 1)) / (float) kSquares;
    const float gapA = size * 0.30f, gapB = tagSize * 1.35f;
    const float top = (s.h - (ink.getHeight() + gapA + sq + gapB + tb.getHeight())) * 0.5f + yShift, cx = s.w * 0.5f;
    const float dx = cx - ink.getCentreX(), dy = top - ink.getY();
    s.add (moved (b, dx, dy), titleName);
    s.add (moved (dash, dx, dy), titleDash);
    s.add (moved (name, dx, dy), titleName);
    const float stripY = top + ink.getHeight() + gapA;
    addStrip (s, cx, stripY, stripW, gap);
    s.add (moved (tag, cx - tb.getCentreX(), stripY + sq + gapB - tb.getY()), info);
}

Scene banner()
{
    Scene s { 1280.0f, 420.0f, {} };
    addLockup (s, 160.0f, 820.0f, 8.0f, 30.0f, -6.0f);
    addSignature (s, 17.0f, 26.0f);
    return s;
}

Scene fullscreen()
{
    Scene s { 1920.0f, 1080.0f, {} };
    addLockup (s, 260.0f, 1240.0f, 12.0f, 44.0f);
    addSignature (s, 24.0f, 44.0f);
    return s;
}

// icona: la "b" grande e sotto la fila di colori
Scene icon()
{
    Scene s { 1024.0f, 1024.0f, {} };
    juce::Path b; textPath (b, "b", Face::SansBold, 600.0f);
    const auto bb = b.getBounds();
    const float stripW = 760.0f, gap = 10.0f, gapA = 80.0f;
    const float sq = (stripW - gap * (float) (kSquares - 1)) / (float) kSquares;
    const float top = (s.h - (bb.getHeight() + gapA + sq)) * 0.5f;
    s.add (moved (b, s.w * 0.5f - bb.getCentreX(), top - bb.getY()), titleName);
    addStrip (s, s.w * 0.5f, top + bb.getHeight() + gapA, stripW, gap);
    return s;
}
}

// Vetrina a schermo intero: le schermate dei plugin a mosaico (righe giustificate: ogni riga riempie la larghezza e
// le immagini mantengono le proporzioni), margini uguali, sfondo grigio come quello dei plugin.
//   BSuiteLogo --showcase uscita.png larghezza altezza [--logo] img1.png img2.png ...
// Con --logo i plugin stanno intorno al logotipo b-suite, al centro della riga di mezzo.
int showcase (int argc, char** argv)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto out = cwd.getChildFile (argv[2]);
    const int W = juce::String (argv[3]).getIntValue(), H = juce::String (argv[4]).getIntValue();
    int first = 5;
    const bool withLogo = argc > 5 && juce::String (argv[5]) == "--logo";
    if (withLogo) ++first;
    std::vector<juce::Image> imgs;
    for (int i = first; i < argc; ++i)
    {
        auto im = juce::ImageFileFormat::loadFrom (cwd.getChildFile (argv[i]));
        if (im.isValid()) imgs.push_back (im); else std::printf ("salto %s\n", argv[i]);
    }
    if (imgs.empty()) return 1;
    const float margin = (float) W * 0.03f, gap = (float) W * 0.008f;
    const float availW = (float) W - 2.0f * margin, availH = (float) H - 2.0f * margin;
    constexpr int kLogo = -1;   // la casella del logo nel mosaico
    constexpr float kLogoAspect = 2.4f;
    auto aspect = [&] (int i) { return i == kLogo ? kLogoAspect : (float) imgs[(size_t) i].getWidth() / (float) imgs[(size_t) i].getHeight(); };
    const int n = (int) imgs.size();

    // righe: con il logo, un numero dispari di righe con lo stesso numero di plugin e il logo al centro di quella di
    // mezzo; senza, la divisione in righe con le proporzioni più simili e l'altezza più vicina allo spazio
    std::vector<std::vector<int>> best; float bestErr = 1.0e9f;
    auto rowHeight = [&] (const std::vector<int>& r) { float sa = 0; for (int i : r) sa += aspect (i); return (availW - gap * (float) (r.size() - 1)) / sa; };
    auto evaluate = [&] (const std::vector<std::vector<int>>& part)
    {
        float h = gap * (float) (part.size() - 1);
        for (auto& r : part) h += rowHeight (r);
        const float err = h > availH ? (h - availH) * 1.5f : availH - h;
        if (err < bestErr) { bestErr = err; best = part; }
    };
    if (withLogo)
    {
        for (int rows : { 3, 5, 7 })
        {
            std::vector<std::vector<int>> part ((size_t) rows);
            int i = 0;
            for (int r = 0; r < rows; ++r)
            {
                const int take = (n - i) / (rows - r);   // plugin rimasti divisi in parti uguali
                for (int k = 0; k < take; ++k)
                {
                    if (r == rows / 2 && k == take / 2) part[(size_t) r].push_back (kLogo);
                    part[(size_t) r].push_back (i++);
                }
                if (r == rows / 2 && take == 0) part[(size_t) r].push_back (kLogo);
            }
            evaluate (part);
        }
    }
    else
    {
        for (int rows = 2; rows <= 7; ++rows)
        {
            float total = 0; for (int i = 0; i < n; ++i) total += aspect (i);
            std::vector<std::vector<int>> part (1);
            float acc = 0;
            for (int i = 0; i < n; ++i)
            {
                const float a = aspect (i);
                if (! part.back().empty() && (int) part.size() < rows && acc + a * 0.5f > total / (float) rows * (float) part.size())
                    part.emplace_back();
                part.back().push_back (i); acc += a;
            }
            evaluate (part);
        }
    }
    std::vector<float> rowH; float totalH = gap * (float) (best.size() - 1);
    for (auto& r : best) { rowH.push_back (rowHeight (r)); totalH += rowH.back(); }
    const float k = juce::jmin (1.0f, availH / totalH);   // una scala sola se non entra in altezza, poi tutto centrato

    juce::Image canvas (juce::Image::RGB, W, H, true);
    {
        juce::Graphics g (canvas);
        g.fillAll (ui::background);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        float y = ((float) H - totalH * k) * 0.5f;
        for (size_t r = 0; r < best.size(); ++r)
        {
            const float h = rowH[r] * k;
            float rowW = gap * (float) (best[r].size() - 1);
            for (int i : best[r]) rowW += aspect (i) * h;
            float x = ((float) W - rowW) * 0.5f;
            for (int i : best[r])
            {
                const auto dst = juce::Rectangle<float> (x, y, aspect (i) * h, h);
                if (i == kLogo)
                {
                    // logotipo b-suite, senza i quadratini: b e nome chiari, trattino grigio
                    juce::Path pb, pd, pn;
                    const float size = dst.getHeight() * 0.42f;
                    float tx = 0.0f;
                    tx += textPath (pb, "b", ui::Face::SansBold, size, tx);
                    tx += textPath (pd, "-", ui::Face::SansLight, size, tx);
                    textPath (pn, "suite", ui::Face::SansMedium, size, tx, -0.02f);
                    juce::Path all; all.addPath (pb); all.addPath (pd); all.addPath (pn);
                    const auto ink = all.getBounds();
                    const auto t = juce::AffineTransform::translation (dst.getCentreX() - ink.getCentreX(), dst.getCentreY() - ink.getCentreY());
                    g.setColour (ui::titleName); g.fillPath (pb, t); g.fillPath (pn, t);
                    g.setColour (ui::titleDash); g.fillPath (pd, t);
                }
                else
                {
                    g.drawImage (imgs[(size_t) i], dst);
                    g.setColour (ui::separator); g.drawRect (dst.expanded (1.0f), 1.0f);   // il bordo separa le schermate dal fondo
                }
                x += dst.getWidth() + gap;
            }
            y += h + gap;
        }
    }
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (canvas, os);
    std::printf ("%s %s (%d immagini, %d righe%s)\n", ok ? "scritto" : "ERRORE", out.getFullPathName().toRawUTF8(), n, (int) best.size(), withLogo ? ", logo" : "");
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc > 4 && juce::String (argv[1]) == "--showcase") { juce::ScopedJuceInitialiser_GUI gui; return showcase (argc, argv); }
    juce::ScopedJuceInitialiser_GUI init;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto dir = cwd.getChildFile (argc > 1 ? argv[1] : "docs");
    const juce::File preview = argc > 2 ? cwd.getChildFile (argv[2]) : juce::File();
    dir.createDirectory();
    bool ok = true;
    const std::pair<const char*, Scene> scenes[] = { { "b-suite-banner", banner() }, { "b-suite-fullscreen", fullscreen() },
                                                     { "b-suite-icon", icon() } };
    for (const auto& [name, scene] : scenes)
    {
        const auto f = dir.getChildFile (juce::String (name) + ".svg");
        const bool written = f.replaceWithText (scene.svg(), false, false, "\n");
        ok = ok && written;
        std::printf ("%s %s\n", written ? "scritto" : "ERRORE", f.getFullPathName().toRawUTF8());
        if (preview != juce::File())   // anteprima PNG facoltativa, solo per controllarlo a occhio
        {
            preview.createDirectory();
            auto png = preview.getChildFile (juce::String (name) + ".png");
            png.deleteFile();
            juce::FileOutputStream os (png);
            juce::PNGImageFormat().writeImageToStream (scene.image(), os);
        }
    }
    return ok ? 0 : 1;
}

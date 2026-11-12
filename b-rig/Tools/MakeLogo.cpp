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

// Vetrina a schermo intero: le schermate dei plugin incastrate in righe giustificate (ogni riga riempie la larghezza,
// le immagini mantengono le proporzioni), margini uguali, sfondo che sfuma dall'alto verso il basso.
//   BSuiteLogo --showcase uscita.png larghezza altezza img1.png img2.png ...
int showcase (int argc, char** argv)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto out = cwd.getChildFile (argv[2]);
    const int W = juce::String (argv[3]).getIntValue(), H = juce::String (argv[4]).getIntValue();
    std::vector<juce::Image> imgs;
    for (int i = 5; i < argc; ++i)
    {
        auto im = juce::ImageFileFormat::loadFrom (cwd.getChildFile (argv[i]));
        if (im.isValid()) imgs.push_back (im); else std::printf ("salto %s\n", argv[i]);
    }
    if (imgs.empty()) return 1;
    const float margin = (float) W * 0.035f, gap = (float) W * 0.009f;
    const float availW = (float) W - 2.0f * margin, availH = (float) H - 2.0f * margin;
    const int n = (int) imgs.size();
    auto aspect = [&] (int i) { return (float) imgs[(size_t) i].getWidth() / (float) imgs[(size_t) i].getHeight(); };
    // righe: per ogni numero di righe si dividono le immagini (in ordine) con somme di proporzioni simili,
    // ogni riga alta quanto serve per riempire la larghezza; si sceglie l'altezza totale più vicina allo spazio
    std::vector<std::vector<int>> best; float bestErr = 1.0e9f;
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
        float h = 0;
        for (auto& r : part) { float sa = 0; for (int i : r) sa += aspect (i); h += (availW - gap * (float) (r.size() - 1)) / sa; }
        h += gap * (float) (part.size() - 1);
        const float err = h > availH ? (h - availH) * 1.5f : availH - h;   // meglio un po' più piccolo che tagliato
        if (err < bestErr) { bestErr = err; best = part; }
    }
    // scala unica se le righe non entrano in altezza, poi tutto centrato
    std::vector<float> rowH; float totalH = 0;
    for (auto& r : best) { float sa = 0; for (int i : r) sa += aspect (i); rowH.push_back ((availW - gap * (float) (r.size() - 1)) / sa); totalH += rowH.back(); }
    totalH += gap * (float) (best.size() - 1);
    const float k = juce::jmin (1.0f, availH / totalH);
    juce::Image canvas (juce::Image::RGB, W, H, true);
    {
        juce::Graphics g (canvas);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), 0.0f, 0.0f, juce::Colour (0xff080808), 0.0f, (float) H, false));
        g.fillAll();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        float y = ((float) H - totalH * k) * 0.5f;
        for (size_t r = 0; r < best.size(); ++r)
        {
            const float h = rowH[r] * k;
            float rowW = 0; for (int i : best[r]) rowW += aspect (i) * h;
            rowW += gap * (float) (best[r].size() - 1);
            float x = ((float) W - rowW) * 0.5f;
            for (int i : best[r])
            {
                const auto dst = juce::Rectangle<float> (x, y, aspect (i) * h, h);
                juce::DropShadow (juce::Colours::black.withAlpha (0.45f), (int) (gap * 1.2f), { 0, (int) (gap * 0.3f) }).drawForRectangle (g, dst.toNearestInt());
                g.drawImage (imgs[(size_t) i], dst);
                g.setColour (juce::Colour (0xff2a2a2a)); g.drawRect (dst, 1.0f);
                x += dst.getWidth() + gap;
            }
            y += h + gap;
        }
    }
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (canvas, os);
    std::printf ("%s %s (%d immagini, %d righe)\n", ok ? "scritto" : "ERRORE", out.getFullPathName().toRawUTF8(), n, (int) best.size());
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
        const bool written = f.replaceWithText (scene.svg());
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

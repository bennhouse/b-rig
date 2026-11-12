// Genera il logo della b-suite per il README (SVG): il logotipo "b-suite" come nei plugin (b Plex Sans Bold,
// trattino Light grigio, nome Medium chiaro) e sotto una fila di quadrati, uno per plugin nel suo colore, in
// ordine di catena. Testo convertito in tracciati: su GitHub l'SVG si vede uguale anche senza i font IBM Plex.
//   ./build/BSuiteLogo_artefacts/Release/BSuiteLogo ../docs/b-suite-logo.svg [anteprima.png]
#include "smp/Theme.h"
#include <cstdio>

namespace
{
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

juce::String hex (juce::Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }

// testo -> tracciato con la linea di base in y = 0; restituisce l'avanzamento orizzontale
float textPath (juce::Path& out, const juce::String& s, ui::Face f, float size, float x, float tracking = 0.0f)
{
    const auto fnt = ui::face (f, size, tracking);
    juce::GlyphArrangement ga;
    ga.addLineOfText (fnt, s, x, 0.0f);
    ga.createPath (out);
    return juce::GlyphArrangement::getStringWidth (fnt, s);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace ui;

    // colori dei plugin, in ordine di catena (dal DI al master)
    const juce::uint32 chain[] = { 0xffc084fc /* drop */, 0xff3d8bff /* focus */, 0xfffb923c /* fs */, 0xff84cc16 /* tight */,
                                   0xff6366f1 /* wire */, 0xfffb7185 /* starve */, 0xffff4d4d /* distort */, 0xffe11d48 /* amp */,
                                   0xff14b8a6 /* blend */, 0xff22d3ee /* double */, 0xffffa200 /* aftercab */, 0xfffacc15 /* dynaverb */,
                                   0xff818cf8 /* delay */, 0xff06b6d4 /* verb */, 0xffa78bfa /* space */, 0xfff1f5f9 /* scenery */,
                                   0xffde50c9 /* adapt */, 0xff38bdf8 /* meter */, 0xffe2e8f0 /* synthesia */ };
    const int nSq = (int) std::size (chain);
    const juce::Colour rig (0xffe9d8a6);

    const float W = 1280.0f, H = 420.0f, size = 160.0f;

    // logotipo
    juce::Path pb, pd, pn;
    float x = 0.0f;
    x += textPath (pb, "b", Face::SansBold, size, x);
    x += textPath (pd, "-", Face::SansLight, size, x);
    textPath (pn, "suite", Face::SansMedium, size, x, -0.02f);
    juce::Path all; all.addPath (pb); all.addPath (pd); all.addPath (pn);
    const auto ink = all.getBounds();

    // fila di quadrati, più larga del logotipo: è la parte colorata
    const float stripW = 820.0f, gap = 8.0f, sq = (stripW - gap * (float) (nSq - 1)) / (float) nSq;
    juce::Path tag;
    textPath (tag, "guitar plugins and a standalone rig", Face::SansRegular, 30.0f, 0.0f);
    const auto tb = tag.getBounds();

    const float blockH = ink.getHeight() + 48.0f + sq + 40.0f + tb.getHeight();
    const float top = (H - blockH) * 0.5f;
    const auto place = juce::AffineTransform::translation ((W - ink.getWidth()) * 0.5f - ink.getX(), top - ink.getY());
    for (auto* p : { &pb, &pd, &pn }) p->applyTransform (place);
    const float left = (W - stripW) * 0.5f, sqY = top + ink.getHeight() + 48.0f;
    tag.applyTransform (juce::AffineTransform::translation ((W - tb.getWidth()) * 0.5f - tb.getX(), sqY + sq + 40.0f - tb.getY()));

    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << (int) W << " " << (int) H << "\" width=\"" << (int) W
        << "\" height=\"" << (int) H << "\" role=\"img\" aria-label=\"b-suite\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"" << hex (background) << "\"/>\n"
        << "<path fill=\"" << hex (rig) << "\" d=\"" << svgPath (pb) << "\"/>\n"
        << "<path fill=\"" << hex (titleDash) << "\" d=\"" << svgPath (pd) << "\"/>\n"
        << "<path fill=\"" << hex (titleName) << "\" d=\"" << svgPath (pn) << "\"/>\n";
    for (int i = 0; i < nSq; ++i)
        svg << "<rect x=\"" << juce::String (left + (float) i * (sq + gap), 2) << "\" y=\"" << juce::String (sqY, 2) << "\" width=\""
            << juce::String (sq, 2) << "\" height=\"" << juce::String (sq, 2) << "\" fill=\"" << hex (juce::Colour (chain[i])) << "\"/>\n";
    svg << "<path fill=\"" << hex (info) << "\" d=\"" << svgPath (tag) << "\"/>\n</svg>\n";

    const auto out = juce::File (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File ("b-suite-logo.svg"));
    out.getParentDirectory().createDirectory();
    const bool ok = out.replaceWithText (svg);

    // anteprima PNG facoltativa (solo per controllarlo a occhio, non va nel repo)
    if (argc < 3) { std::printf ("%s %s\n", ok ? "scritto" : "ERRORE", out.getFullPathName().toRawUTF8()); return ok ? 0 : 1; }
    juce::Image img (juce::Image::ARGB, (int) W, (int) H, true);
    {
        juce::Graphics g (img);
        g.fillAll (background);
        g.setColour (rig); g.fillPath (pb);
        g.setColour (titleDash); g.fillPath (pd);
        g.setColour (titleName); g.fillPath (pn);
        for (int i = 0; i < nSq; ++i) { g.setColour (juce::Colour (chain[i])); g.fillRect (left + (float) i * (sq + gap), sqY, sq, sq); }
        g.setColour (info); g.fillPath (tag);
    }
    auto png = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s %s\n", ok ? "scritto" : "ERRORE", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

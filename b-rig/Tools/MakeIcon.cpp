// Genera Resources/AppIcon.png (1024 x 1024): forma standard delle icone di macOS (quadrato arrotondato 824 px
// con l'ombra del modello Apple), il grigio di sfondo dell'app e al centro la "b" del logotipo, nel suo colore.
#include "smp/Theme.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int S = 1024;
    juce::Image img (juce::Image::ARGB, S, S, true);
    {
        juce::Graphics g (img);
        const auto body = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
        juce::Path shape;
        shape.addRoundedRectangle (body, 185.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 28, { 0, 12 }).drawForPath (g, shape);
        g.setColour (ui::background);
        g.fillPath (shape);
        g.setColour (juce::Colour (0xff2a2a2a));   // bordo sottile: la forma si vede anche su un Dock scuro
        g.strokePath (shape, juce::PathStrokeType (3.0f));
        // "b" in Plex Sans Bold, centrata sull'inchiostro reale
        juce::GlyphArrangement ga;
        ga.addLineOfText (ui::face (ui::Face::SansBold, 640.0f), "b", 0.0f, 0.0f);
        juce::Path b; ga.createPath (b);
        const auto bb = b.getBounds();
        b.applyTransform (juce::AffineTransform::translation (body.getCentreX() - bb.getCentreX(), body.getCentreY() - bb.getCentreY()));
        g.setColour (ui::accent);
        g.fillPath (b);
    }
    const auto out = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String ("AppIcon.png"));
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s %s\n", ok ? "scritto" : "ERRORE", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

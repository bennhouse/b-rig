#include "PluginEditor.h"

void TightScope::paint (juce::Graphics& g)
{
    using namespace ui;
    const auto a = getLocalBounds().toFloat().withTrimmedBottom (20.0f);
    // griglia: centro, ±50 %, contorno chiuso
    g.setColour (gridLine);
    for (float f : { 0.25f, 0.75f }) g.fillRect (a.getX(), std::round (a.getY() + a.getHeight() * f), a.getWidth(), kLine);
    g.setColour (gridZero);
    g.fillRect (a.getX(), std::round (a.getCentreY()), a.getWidth(), kLine);
    g.setColour (gridLine);
    outline (g, a);
    // legenda: cosa mostra ogni parte del grafico
    const float tagH = capHeight (kTagSize, Face::SansSemiBold);
    drawTag (g, "palm-mute clamp: low end reduced", a.getX() + 6.0f, a.getY() + 6.0f + tagH);
    drawTag (g, "pick bite: attack boosted / softened", a.getX() + 6.0f, a.getBottom() - 6.0f);
    drawTag (g, "output", a.getRight() - 6.0f, a.getCentreY() - 6.0f, juce::Justification::right);
    const float base = (float) getHeight() - 8.0f + capHeight (kAxisSize, Face::MonoRegular) * 0.5f;
    const double secs = (a.getWidth() - 2.0) * 256.0 / (proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    drawAtBaseline (g, "-" + num (secs, 1) + " s", a.getX(), base, info, kAxisSize, juce::Justification::left);
    drawAtBaseline (g, "now", a.getRight(), base, info, kAxisSize, juce::Justification::right);
    if (proc.apvts.getRawParameterValue (BTightIDs::autoGain)->load() > 0.5f)
    {
        const float gdb = proc.appliedGainDb.load();
        drawAtBaseline (g, "autogain " + juce::String (gdb >= 0.05f ? "+" : "") + num (gdb, 1) + " dB", a.getCentreX(), base, accent, kAxisSize,
                        juce::Justification::horizontallyCentred, Face::MonoMedium);
    }

    // nessun tuning scelto: il plugin non fa nulla, e lo dice
    if (proc.apvts.getRawParameterValue (BTightIDs::tuning)->load() < 0.5f)
        drawAtBaseline (g, "off: choose a tuning to switch b-tight on", a.getCentreX(), a.getY() + 30.0f, info, kInfoSize,
                        juce::Justification::horizontallyCentred, Face::SansRegular);
    const int w = (int) a.getWidth() - 2;
    const int last = proc.historyPos.load (std::memory_order_acquire);
    const float h2 = a.getHeight() * 0.5f - 2.0f;
    for (int x = 0; x < w; ++x)
    {
        const auto& f = proc.history[(size_t) ((last - (w - 1 - x)) & (BTightProcessor::kHistory - 1))];
        const float px = a.getX() + 1.0f + (float) x;
        // uscita: inviluppo simmetrico tenue
        const float pk = juce::jlimit (0.0f, 1.0f, std::sqrt (f.peak)) * h2;
        g.setColour (text.withAlpha (0.35f));
        g.fillRect (px, a.getCentreY() - pk, 1.0f, 2.0f * pk);
        // riduzione sui palm-mute (dall'alto, scuro): 12 dB = metà altezza
        if (f.clampDb > 0.05f)
        {
            g.setColour (accent.withAlpha (0.30f));
            g.fillRect (px, a.getY() + 1.0f, 1.0f, juce::jmin (1.0f, f.clampDb / 12.0f) * h2);
        }
        // attacchi esaltati (o smorzati) dal basso, pieni
        if (std::abs (f.attackDb) > 0.1f)
        {
            g.setColour (f.attackDb > 0 ? accent : accent.withAlpha (0.5f));
            const float bh = juce::jmin (1.0f, std::abs (f.attackDb) / 9.0f) * h2;
            g.fillRect (px, a.getBottom() - 1.0f - bh, 1.0f, bh);
        }
    }
}

BTightEditor::BTightEditor (BTightProcessor& p)
    : ShellEditor (p, p.apvts, "tight", 760, 421), scope (p)
{
    using namespace ui;
    // tuning: nessun segmento acceso = off (il plugin non fa nulla); un clic carica il preset di quel tuning,
    // un secondo clic sullo stesso lo spegne
    setHeaderChoice ("tuning", BTightIDs::tuning, { "standard", "drop", "baritone" });
    headerChoice->valueOffset = 1;
    headerChoice->allowDeselect = true;
    headerChoice->onUserChange = [&p] (int seg) { p.applyTuningPreset (seg + 1); };
    addValue ("palm-mute clamp", BTightIDs::clamp, [] (float v) { return num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v) + (float) d; }, { "-48.0 dB" });
    addCheck ("auto", BTightIDs::autoMu);
    addValue ("pick bite", BTightIDs::bite, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
              [] (float v, int d) { return std::round (v) + 5.0f * (float) d; }, { "-100 %" });
    addValue ("mid push", BTightIDs::push, [] (float v) { return num (v, 1) + " dB"; },
              [] (float v, int d) { return std::round (v * 2.0f + (float) d) / 2.0f; }, { "12.0 dB" });
    newRow();
    gainBox = &addValue ("gain", BTightIDs::gain, [] (float v) { return (v > 0.04f ? "+" : "") + num (v, 1) + " dB"; },
                         [] (float v, int d) { return std::round (v) + (float) d; }, { "+24.0 dB", "-24.0 dB" });
    addCheck ("autogain", BTightIDs::autoGain);
    setDisplay (scope);
    resized();
}

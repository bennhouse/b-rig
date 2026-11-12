// Generato: b-rig compila b-blend direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_blend, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB 0xff14b8a6

namespace rig_blend
{
#include "smp/Widgets.cpp"
#include "smp/Shell.cpp"
#include "b-blend/Source/PluginProcessor.cpp"
#include "b-blend/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_blend() { return std::make_unique<rig_blend::BBlendProcessor>(); }
bool rigBlendHasIr (juce::AudioProcessor& p)
{
    auto& b = static_cast<rig_blend::BBlendProcessor&> (p);
    // un IR che funziona: caricato e, se viene da un file, con il file ancora al suo posto (collegato)
    for (int k = 0; k < 4; ++k)
        if (b.getSlotRate (k) > 0.0)
        {
            const auto f = b.getSlotFile (k);
            if (f == juce::File() || f.existsAsFile()) return true;
        }
    return false;
}
// test: un IR sintetico (impulso con una breve coda) nello slot A
void rigBlendLoadTestIr (juce::AudioProcessor& p)
{
    juce::AudioBuffer<float> ir (1, 256);
    ir.clear();
    for (int i = 0; i < 256; ++i) ir.setSample (0, i, (i == 0 ? 1.0f : 0.0f) + 0.2f * std::exp (-(float) i / 30.0f) * (i % 2 ? -1.0f : 1.0f));
    static_cast<rig_blend::BBlendProcessor&> (p).loadIr (0, ir, 48000.0, "test");
}

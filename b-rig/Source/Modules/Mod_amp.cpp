// Generato: b-rig compila b-amp direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_amp, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB BSUITE_ACCENT_AMP

namespace rig_amp
{
#include "smp/Widgets.cpp"
#include "b-amp/Source/PluginProcessor.cpp"
#include "b-amp/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_amp() { return std::make_unique<rig_amp::BAmpProcessor>(); }

// Il mini tuner di b-rig usa lo stesso McLeod Pitch Method di b-amp (una sola implementazione).
float rigMpmFrequency (const float* x, int n, double fsDec, int maxLag, float& clarity) noexcept
{
    return rig_amp::amp::mpmFrequency (x, n, fsDec, maxLag, clarity);
}

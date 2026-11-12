// Generato: b-rig compila b-adapt direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_adapt, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB 0xffde50c9

namespace rig_adapt
{
#include "smp/Widgets.cpp"
#include "smp/Shell.cpp"
#include "b-adapt/Source/PluginProcessor.cpp"
#include "b-adapt/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_adapt() { return std::make_unique<rig_adapt::BAdaptProcessor>(); }

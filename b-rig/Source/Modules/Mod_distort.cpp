// Generato: b-rig compila b-distort direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_distort, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB 0xffff4d4d

namespace rig_distort
{
#include "smp/Widgets.cpp"
#include "b-distort/Source/PluginProcessor.cpp"
#include "b-distort/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_distort() { return std::make_unique<rig_distort::BDistortProcessor>(); }

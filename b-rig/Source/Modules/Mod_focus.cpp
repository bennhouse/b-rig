// Generato: b-rig compila b-focus direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_focus, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB BSUITE_ACCENT_FOCUS

namespace rig_focus
{
#include "smp/Widgets.cpp"
#include "b-focus/Source/PluginProcessor.cpp"
#include "b-focus/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_focus() { return std::make_unique<rig_focus::BFocusProcessor>(); }

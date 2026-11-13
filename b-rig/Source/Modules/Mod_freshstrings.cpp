// Generato: b-rig compila b-freshstrings direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_freshstrings, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB BSUITE_ACCENT_FRESHSTRINGS

namespace rig_freshstrings
{
#include "smp/Widgets.cpp"
#include "smp/Shell.cpp"
#include "b-freshstrings/Source/PluginProcessor.cpp"
#include "b-freshstrings/Source/PluginEditor.cpp"

}

std::unique_ptr<juce::AudioProcessor> rigMake_freshstrings() { return std::make_unique<rig_freshstrings::BFreshstringsProcessor>(); }

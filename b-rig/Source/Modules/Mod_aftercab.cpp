// Generato: b-rig compila b-aftercab direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_aftercab, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB 0xffffa200

namespace rig_aftercab
{
#include "smp/Widgets.cpp"
#include "b-aftercab/Source/PluginProcessor.cpp"
#include "b-aftercab/Source/PluginEditor.cpp"
#include "b-aftercab/Source/UI/AftercabWidgets.cpp"
#include "b-aftercab/Source/UI/EqDisplay.cpp"
#include "b-aftercab/Source/DSP/PhaseAligner.cpp"
}

std::unique_ptr<juce::AudioProcessor> rigMake_aftercab() { return std::make_unique<rig_aftercab::BAftercabProcessor>(); }

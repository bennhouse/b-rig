// Generato: b-rig compila b-aftercab direttamente dai suoi sorgenti (nessuna copia), dentro il namespace
// rig_aftercab, con il suo colore di accento e una copia privata del design system condiviso. Ogni modifica
// al plugin entra in b-rig alla prossima build.
#include "ModulePrelude.h"
#define SMP_IN_RIG 1   // titolo, about e tuner del plugin nascosti: in b-rig ci sono già
#undef SMP_ACCENT_ARGB
#define SMP_ACCENT_ARGB BSUITE_ACCENT_AFTERCAB

namespace rig_aftercab
{
#include "smp/Widgets.cpp"
#include "b-aftercab/Source/PluginProcessor.cpp"
#include "b-aftercab/Source/PluginEditor.cpp"
#include "b-aftercab/Source/UI/AftercabWidgets.cpp"
#include "b-aftercab/Source/UI/EqDisplay.cpp"
#include "b-aftercab/Source/DSP/PhaseAligner.cpp"
}

// in b-rig b-aftercab lavora mono -> stereo (dopo la cassa il segnale è mono): così spread (il raddoppio stereo)
// e l'allineamento di fase funzionano come nel plugin con una traccia mono
std::unique_ptr<juce::AudioProcessor> rigMake_aftercab()
{
    auto p = std::make_unique<rig_aftercab::BAftercabProcessor>();
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    p->setBusesLayout (l);
    return p;
}

#pragma once
// Catalogo dei moduli di b-rig: ogni plugin della suite (compilato dai suoi sorgenti) più i segnaposto
// dei moduli non ancora pronti, mostrati in grigio in libreria.
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>

namespace rig
{
enum class Category { pre, core, post, master };

struct ModuleInfo
{
    juce::String id;              // "amp"
    juce::String name;            // "b-amp"
    juce::Colour colour;          // colore identificativo del plugin
    Category category;
    int order;                    // posizione logica nella catena (inserimento automatico)
    juce::String info;            // 2-3 righe: cosa fa, controlli chiave, dove metterlo
    std::function<std::unique_ptr<juce::AudioProcessor>()> make;   // nullptr = segnaposto (non ancora pronto)
    bool safeArm = false;         // parte sempre spento (b-amp): va armato a mano, con fade-in di 80 ms
    // cassa (b-blend): true se ha almeno un IR caricato. Un ampli con una cassa caricata dopo di sé, in un
    // preset, riparte già acceso
    std::function<bool (juce::AudioProcessor&)> hasCab;

    bool available() const { return make != nullptr; }
};

const std::vector<ModuleInfo>& modules();
const ModuleInfo* findModule (const juce::String& id);
juce::String categoryName (Category);
}

// McLeod Pitch Method di b-amp (vedi Mod_amp.cpp), usato dal mini tuner.
float rigMpmFrequency (const float* x, int n, double fsDec, int maxLag, float& clarity) noexcept;

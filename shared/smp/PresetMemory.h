#pragma once
// Memoria per preset, solo per la sessione: quando l'utente cambia preset (tipo di compressione,
// timbro del delverb, tipo di distorsione...) i valori modificati del preset lasciato vengono
// ricordati e quelli del nuovo preset tornano come li aveva lasciati (o ai suoi default).
// Non viene salvata col progetto: ricaricando il plugin si riparte dai default.
// Da usare solo sul message thread.
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

namespace ui
{

class PresetMemory
{
public:
    // param: parametro che dipende dal preset; def: il suo default per ciascun preset.
    void add (juce::RangedAudioParameter* param, std::function<float (int preset)> def)
    {
        entries.push_back ({ param, std::move (def) });
    }

    float defaultFor (const juce::RangedAudioParameter* param, int preset) const
    {
        for (const auto& e : entries) if (e.param == param) return e.def (preset);
        return param->convertFrom0to1 (param->getDefaultValue());
    }

    // Cambio di preset fatto dall'utente: salva i valori del vecchio, applica quelli del nuovo.
    void switchPreset (int from, int to)
    {
        if (from == to) return;
        auto& saved = stored[from];
        saved.clear();
        for (const auto& e : entries) saved.push_back (e.param->convertFrom0to1 (e.param->getValue()));
        const auto it = stored.find (to);
        for (size_t i = 0; i < entries.size(); ++i)
            set (entries[i].param, it != stored.end() && i < it->second.size() ? it->second[i] : entries[i].def (to));
    }

    // Doppio clic sul preset: i suoi valori tornano ai default e il ricordo si cancella.
    void resetPreset (int preset)
    {
        stored.erase (preset);
        for (const auto& e : entries) set (e.param, e.def (preset));
    }

private:
    static void set (juce::RangedAudioParameter* p, float v)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }

    struct Entry { juce::RangedAudioParameter* param; std::function<float (int)> def; };
    std::vector<Entry> entries;
    std::map<int, std::vector<float>> stored;
};

} // namespace ui

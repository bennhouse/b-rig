#pragma once
// Testi del pannello "about" (clic sul logo) di tutti i plugin della suite, in un solo posto:
// quando si cambia la descrizione di un plugin, si cambia qui e vale ovunque.
#include <juce_core/juce_core.h>

namespace ui
{
inline juce::String aboutText (const juce::String& name)
{
    static const std::pair<const char*, const char*> texts[] = {
        { "focus",     "A zero-latency denoiser for the guitar DI. Four bands with their own detectors cut hiss, fizz and hum "
                       "between and under the notes, so the amp sim gets real silence while every pick attack passes untouched." },
        { "tight",     "Sculpts the DI before the amp: a tuning-aware low cut, a palm-mute clamp on the low band, "
                       "pick bite on the attack and a mid push. Transparent until you pick a tuning." },
        { "distort",   "Five distortions (tube, drive, rat, fuzz, fold) with anti-aliased waveshaping, a tone control "
                       "that balances lows and highs, low cut, hi cut and mix. Each type remembers its own settings while the plugin is open." },
        { "wire", "A sub-harmonic synth and fuzz driven by an analog-style phase-locked loop: the guitar is tracked and "
                       "re-synthesised as raw square waves an octave or two below, with zero latency." },
        { "starve", "An under-powered analog circuit plus three digital datamosh effects in series: stutter, bit masks "
                       "and dropouts, each with its own intensity, volume and mix, all click-free." },
        { "amp",       "A three-channel reference amp head: clean, crunch and lead, with real tone-stack values, "
                       "tube-style gain stages, a DI-keyed noise gate, a boost and a built-in tuner. "
                       "Every head keeps its own settings. Click the pilot light to look at the back." },
        { "blend",     "Blends up to four cabinet IRs, phase-aligned, on a single pad, and adds the physical "
                       "thump and air of a real speaker in the room." },
        { "double",    "Turns a mono guitar into a wide, phase-safe stereo double with humanised timing and pitch, "
                       "so it still collapses cleanly to mono." },
        { "aftercab",  "The post-cab channel strip: EQ, compression, the delverb (delay, verb, shimmer) and an output "
                       "stage with automatic level matching." },
        { "scenery", "Procedural ambience and lo-fi that breathe with your guitar: vinyl, amp hum and rain synthesised "
                       "in real time that open up as you play harder, plus a lo-fi tape stage." },
        { "adapt", "Gets a guitar part or a mix ready for phones and small speakers: mono-safe lows, perceived bass, "
                       "tamed harshness and true peaks under -1 dBTP, with a phone and laptop monitor." },
        { "meter",     "The last slot of the chain: spectrum, stereo phase and loudness, with one-click targets "
                       "for Instagram, YouTube and digital masters." },
        { "rig",       "The b-suite as a standalone app: the plugins' own engines and interfaces in one window, with a tuner, "
                       "a metronome and a safety limiter on the output." },
        { "drop",      "Tunes the guitar down or up in real time, from -12 to +12 semitones, without FFT. The pick click "
                       "passes at zero latency while the shifted note follows a few milliseconds later." },
        { "freshstrings", "New strings, a different pick and another pickup position on the clean DI: brightness only on "
                       "the attack, pick material, string gauge and pickup resonance, at zero latency." },
        { "space",     "Puts the amp in a room from its geometry: the room and its materials, amp type, where the amp and the "
                       "listener stand and where the amp points. No reverb knobs, zero latency on the direct sound." },
        { "dynaverb",  "Hears when you play lead lines on the high strings and only then adds a mid boost and a stereo "
                       "delay. Riffs on the low strings pass untouched." },
        { "verb",      "A reverb for any source: room, plate, hall, spring and shimmer, with early reflections, an "
                       "eight-line feedback network, freeze and ducking. Zero latency." },
        { "delay",     "A delay with its character inside the feedback loop (digital, tape, bucket brigade, lo-fi, "
                       "reverse), tempo sync, tap, stereo offset and ping-pong. Changing the time bends the pitch like tape." },
        { "synthesia", "An audio-reactive visual generator for playthrough videos: generative scenes that follow the "
                       "guitar, chroma-key backgrounds and a frame capture synced to the DAW. The audio passes untouched." },
    };
    for (const auto& t : texts) if (name == t.first) return t.second;
    return {};
}
}

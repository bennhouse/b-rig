#include "Modules.h"

#define RIG_DECLARE(id) std::unique_ptr<juce::AudioProcessor> rigMake_##id();
RIG_DECLARE (adapt) RIG_DECLARE (aftercab) RIG_DECLARE (amp) RIG_DECLARE (blend) RIG_DECLARE (delay) RIG_DECLARE (distort) RIG_DECLARE (verb)
RIG_DECLARE (drop) RIG_DECLARE (dynaverb) RIG_DECLARE (focus) RIG_DECLARE (freshstrings) RIG_DECLARE (meter) RIG_DECLARE (space) RIG_DECLARE (starve)
RIG_DECLARE (tight) RIG_DECLARE (wire)
#undef RIG_DECLARE
bool rigBlendHasIr (juce::AudioProcessor&);

namespace rig
{
const std::vector<ModuleInfo>& modules()
{
    using C = Category;
    static const std::vector<ModuleInfo> list = [] {
        std::vector<ModuleInfo> v;
        auto add = [&] (const char* id, juce::uint32 argb, C cat, int order, const char* info,
                        std::function<std::unique_ptr<juce::AudioProcessor>()> make, bool safe = false)
        {
            v.push_back ({ id, juce::String ("b-") + id, juce::Colour (argb), cat, order, info, std::move (make), safe, {} });
        };
        // PRE / DI
        add ("focus", 0xff3d8bff, C::pre, 10,
             "Zero-latency denoiser for the DI: four bands cut hiss, fizz and hum between the notes.\nControls: threshold, strength, release.\nFirst slot on the clean DI.", rigMake_focus);
        add ("drop", 0xffc084fc, C::pre, 5,
             "Tunes the guitar down (or up) in real time, from -12 to +12 semitones. The pick click passes at zero latency, the shifted note follows about 8 ms later.\nControls: detune, range, zero-lag pick, semitones, fine, tension, mix.\nFirst slot on the DI.", rigMake_drop);
        add ("freshstrings", 0xfffb923c, C::pre, 20,
             "New-string brightness on the pick attack, pick material, string gauge and pickup position, on the clean DI.\nControls: pick, gauge, pickup, freshness, attack, tension, output.\nAfter b-focus, before any drive or amp.", rigMake_freshstrings);
        v.back().name = "b-fs";   // nome corto ovunque nell'app (quello completo è b-freshstrings)
        add ("tight", 0xff84cc16, C::pre, 30,
             "Shapes the DI before the amp: a low cut that follows your tuning, a palm-mute clamp, pick bite and a mid push.\nControls: tuning, clamp, bite, push, gain.\nAfter b-focus, before any drive or amp.", rigMake_tight);
        // CORE / AMP
        add ("amp", 0xffe11d48, C::core, 50,
             "Three-channel amp head (clean, crunch, lead) with noise gate and boost. It starts muted: click to arm it.\nControls: channel, gain, tight, bass, mid, treble, presence, resonance, master.\nAfter the DI tools, before b-blend.", rigMake_amp, true);
        add ("blend", 0xff14b8a6, C::core, 60,
             "Blends up to four phase-aligned cab IRs on one pad and adds speaker thump and air.\nControls: the pad, thump, air, low cut, high cut.\nRight after the amp.", rigMake_blend);
        v.back().hasCab = rigBlendHasIr;
        add ("distort", 0xffff4d4d, C::core, 44,
             "Five distortions: tube, drive, rat, fuzz, fold.\nControls: type, drive, tone, low cut, hi cut, mix, output.\nBefore the amp, like a pedal.", rigMake_distort);
        add ("wire", 0xff6366f1, C::core, 40,
             "Sub-octave synth and fuzz that tracks the guitar with an analog-style PLL, with an adaptive ring mod.\nControls: tracking, fuzz, glitch, filter, ring, mix, level.\nBefore the amp, on a clean signal.", rigMake_wire);
        add ("starve", 0xfffb7185, C::core, 42,
             "An under-powered circuit followed by stutter, bit masks and dropouts.\nControls: intensity, volume and mix for each effect, output.\nBefore the amp, or after the cab.", rigMake_starve);
        // POST / SPACE
        add ("aftercab", 0xffffa200, C::post, 80,
             "Post-cab strip: EQ, compressor, delverb (delay, verb, shimmer) and output level matching.\nControls: EQ, comp mode and threshold, verb type and mix, output.\nAfter b-blend.", rigMake_aftercab);
        add ("delay", 0xff818cf8, C::post, 83,
             "Delay with five characters inside the feedback loop: digital, tape, bbd, lofi, reverse. Tempo sync, tap, stereo offset and ping-pong.\nControls: type, time, sync, tap, feedback, low cut, hi cut, mix, offset, ping-pong, mod, smear, duck, output, trails.\nAfter the cab.", rigMake_delay);
        add ("verb", 0xff06b6d4, C::post, 84,
             "Reverb: room, plate, hall, spring and shimmer, with early reflections, an 8-line feedback network, freeze and ducking.\nControls: type, decay, predelay, size, low cut, hi cut, mix, early, damp, mod, duck, stereo, freeze, output.\nAfter the cab and the delay.", rigMake_verb);
        add ("space", 0xffa78bfa, C::post, 85,
             "Puts the amp in a room: size, walls, amp type, where the amp and the listener stand and where the amp points.\nControls: space, walls, source, dist comp, width, depth, ceiling, angle, air.\nAfter the cab, at the end of the guitar chain.", rigMake_space);
        add ("dynaverb", 0xfffacc15, C::post, 82,
             "Hears when you play lead lines on the high strings and only then adds a mid boost and a stereo delay. Riffs on the low strings pass untouched.\nControls: crossover, duck delay, sync, split, boost, delay, space, speed.\nAfter the cab.", rigMake_dynaverb);
        // MASTER / UTIL
        add ("adapt", 0xffde50c9, C::master, 100,
             "Prepares a part for phones and small speakers: mono-safe lows, perceived bass, a fixed EQ plus narrow cuts on harsh resonances, true peaks under -1 dBTP.\nControls: safe width, thump, deharsh, monitor.\nAt the end. Check on the monitor, then switch it off before export.", rigMake_adapt);
        add ("meter", 0xff38bdf8, C::master, 110,
             "Spectrum, stereo phase and loudness, with LUFS targets for social, YouTube, digital, CD and podcast.\nControls: view, target, gain, auto.\nLast slot.", rigMake_meter);
        return v;
    }();
    return list;
}

const ModuleInfo* findModule (const juce::String& id)
{
    for (auto& m : modules()) if (m.id == id) return &m;
    return nullptr;
}

juce::String categoryName (Category c)
{
    switch (c)
    {
        case Category::pre:    return "PRE / DI";
        case Category::core:   return "CORE / AMP";
        case Category::post:   return "POST / SPACE";
        case Category::master: return "MASTER / UTIL";
    }
    return {};
}
}

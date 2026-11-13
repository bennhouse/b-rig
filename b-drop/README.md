# b-drop

Tunes the guitar down (or up) in real time, from -12 to +12 semitones. Put it in the first slot, on the DI.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency reported to the host:** 0 samples (the shifted note is one analysis frame behind, about 22 ms at 48 kHz; the pick click and the dry signal are not) · **Channels:** mono in, mono or stereo out · **CPU:** about 0.3 % of one core

## How it works

A polyphonic shifter that never guesses the note: the pitch ratio is a fixed number, 2^((semitones + cents/100) / 12), so chords and single notes are shifted the same way and the tuning never hunts.

1. **Pre-filter.** A 2nd-order Butterworth high-pass at 52 Hz on the shifter's input, so the mechanical rumble of the picking hand is not shifted down into the low end.
2. **Phase vocoder with locked peaks (Laroche and Dolson).** FFT of 1024 points at 44.1/48 kHz (2048 at 88.2/96 kHz), synthesis hop 256 (4 times overlap), Hann window for analysis and synthesis. It stretches time by the ratio (analysis hop = 256 / ratio) and a Hermite resampler reads it back at the speed of the ratio: the length is the same as before and the pitch moves exactly, at any frequency. For each frame it finds the magnitude and the true frequency of every bin (from the unwrapped phase advance), the local peaks and the region of each peak (the bins closer to it than to any other peak). Only the peaks advance their synthesis phase; every bin in a region keeps, against its peak, the phase relation it had at the input (identity phase locking). Each harmonic of each string moves as one rigid body: no tremolo on chords, no phasiness.
3. **Attack.** A transient detector on the DI (fast envelope well above the slow one) resets all synthesis phases to the input phases at each new pick, so the shifted note starts aligned with the attack. `zero-lag pick` lets the part of the DI above 2.8 kHz through directly, at zero latency, for 5 ms: you hear the pick at once and the shifted note follows one frame later.
4. **Tension.** Only when going down, in proportion to the semitones (full at -12) and to `tension`: a cut of up to 4.5 dB at 160 Hz takes the mud off, and a shelf of up to +2 dB at 3.2 kHz gives back the brightness a lower tuning loses, like a baritone with tight strings.
5. **Mix and on/off.** The dry signal has no delay. `detune` crossfades over 10 ms; when it is off, or at 0 semitones (the default), the output is the input, bit for bit.

## Controls

- `detune`: on/off.
- `range`: `shift` (from 0 up or down, -12…+12), `octave` (fixed at -12: the roller is locked), `whammy` (the whole range, with a 60 ms glide between values).
- `zero-lag pick`: on by default.
- `tension` (0-100 %, default 65 %): how much the low end is tightened when you tune down. Lowering the pitch also lowers the lows of every palm mute, which turn into mud; tension adds, in proportion to the semitones down, a high-pass rising from 45 to 95 Hz, a cut of up to 6 dB at 165 Hz and up to 3 dB above 3 kHz, so a guitar dropped a fifth sounds like a tight baritone instead of a slack string. Going up it does nothing.
- `mix` (0-100 %; at 50 % it works as an octaver).
- The semitones are on a roller left of the graph, starting at 0: drag it up or down (30 px per semitone). The roller follows the mouse smoothly and the value only moves to the next semitone once the roller has gone past 0.65 of the way, so stopping halfway never makes it jump back and forth. The mouse wheel moves one semitone per notch; a trackpad one semitone per stretch of scrolling, not per event. Double-click goes back to 0 and opens the number for typing (`-5`, `+3`, `7`; Return confirms, Esc or a click outside keeps 0), like the other values. The further from 0, the more the bar next to it fills (up or down, full at an octave) and the more the number takes the plugin's colour. The `semitones` parameter stays automatable from the host; `fine` (±50 cents) only from the host.
- On the right the DI (grey) and output (violet) waveforms overlaid, with the phase-lock quality and a "PICK 0 MS" flash on each attack.

Value boxes: drag horizontally, click the arrows or use the mouse wheel. Double-click a value to reset it and type a new one.

## Measured (see `Tests/Tests.cpp`)

- 440 Hz at -2 st comes out at 392.00 Hz; 220 Hz at -12 st at 110.06 Hz; +7 st +30 cents at 335.39 Hz (exact).
- Eight notes from 82 to 659 Hz at -2, -5, -12 and +5 semitones: worst error 0 cents (within the 1-cent measurement step).
- The shifted note is audible 22.5 ms after the note starts (196 Hz, one frame).
- A pick click comes out in the first millisecond with `zero-lag pick` (-19 dB), nothing without it.
- A two-note chord at -5 st: level within 0.4 dB over 40 ms windows (no tremolo).
- At -5 st the shifted note keeps the input level (within 0.01 dB).
- At -12 st with tension 100 %, 160 Hz is 4.5 dB lower than with tension 0 %.
- `detune` off, or 0 semitones (the default): output identical to the input.
- CPU: about 0.6 % of one core.

## About panel

Click the b-drop logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDropTests_artefacts/Release/BDropTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

# b-drop

Tunes the guitar down (or up) in real time, from -12 to +12 semitones. Put it in the first slot, on the DI.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency reported to the host:** 0 samples (the shifted note is about 7-9 ms behind, the pick click is not) · **Channels:** mono in, mono or stereo out · **CPU:** about 0.3 % of one core

## How it works

No FFT phase vocoder: those need 40-85 ms of audio. Everything happens in the time domain:

1. **Two-head shifter.** A circular delay line is read by two heads that turn over a 14 ms grain (672 samples at 48 kHz), half a grain apart, each weighted by a Hann window (the two windows always add up to 1).
2. **Phase lock.** When a head starts a new grain it looks for the point whose recent waveform best matches the other head: normalised correlation over 256 samples taken every 2nd sample, searched over one period of the lowest note (640 samples at 48 kHz), then a parabola through the best three points for the fractional sample. Among near-equal matches it takes the shortest delay, so high notes have less latency. This keeps the pitch exact and chords free of tremolo.
3. **Zero-lag pick.** A detector with 0.1 ms attack and 6 ms release on the band above 3.5 kHz (the pick click, which has no pitch) spots each attack and lets that band through directly, at zero latency, for 4 ms. You hear the pick at once; the shifted note follows.
4. **Tension.** Only when going down, in proportion to the semitones (full at -12): a high-pass from 45 to 95 Hz, a cut of up to 6 dB at 165 Hz (Q 1.4) and up to 3 dB of presence above 3 kHz, so palm mutes in low tunings stay tight.
5. **Mix and on/off.** The dry signal has no delay. `detune` crossfades over 10 ms; when it is off, or at 0 semitones (the default), the output is the input, bit for bit (no head delay either).

## Controls

- `detune`: on/off.
- `range`: `shift` (from 0 up or down, -12…+12), `octave` (fixed at -12: the roller is locked), `whammy` (the whole range, with a 60 ms glide between values).
- `zero-lag pick`: on by default.
- `tension` (0-100 %, default 65 %): how much the low end is tightened when you tune down. Lowering the pitch also lowers the lows of every palm mute, which turn into mud; tension adds, in proportion to the semitones down, a high-pass rising from 45 to 95 Hz, a cut of up to 6 dB at 165 Hz and up to 3 dB above 3 kHz, so a guitar dropped a fifth sounds like a tight baritone instead of a slack string. Going up it does nothing.
- `mix` (0-100 %; at 50 % it works as an octaver).
- The semitones are on a roller left of the graph, starting at 0: drag it up or down (it moves half a step with the mouse, then clicks to the next number, with no animation), or use the mouse wheel; double-click goes back to 0. The further from 0, the more the bar next to it fills (up or down, full at an octave) and the more the number takes the plugin's colour. The `semitones` parameter stays automatable from the host; `fine` (±50 cents) only from the host.
- On the right the DI (grey) and output (violet) waveforms overlaid, with the phase-lock quality and a "PICK 0 MS" flash on each attack.

Value boxes: drag horizontally, click the arrows or use the mouse wheel. Double-click a value to reset it and type a new one.

## Measured (see `Tests/Tests.cpp`)

- 440 Hz at -2 st comes out at 392.00 Hz; 220 Hz at -12 st at 110.00 Hz; +7 st +30 cents within 1 cent.
- Eight notes from 82 to 659 Hz at -2, -5, -12 and +5 semitones: worst error 4 cents.
- The shifted note is audible 6.5 ms after the note starts (196 Hz).
- A pick click comes out in the first millisecond with `zero-lag pick` (-19 dB), nothing without it.
- A two-note chord at -5 st: level within 0.4 dB over 40 ms windows (no tremolo).
- At -12 st, 50 Hz in the output is 11.7 dB lower with tension 100 % than with 0 %.
- `detune` off, or 0 semitones (the default): output identical to the input.

## About panel

Click the b-drop logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDropTests_artefacts/Release/BDropTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

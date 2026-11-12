# b-starve

An under-powered analog circuit plus three digital "datamosh" effects for guitar, in series. Each effect has its own on/off, `intensity`, `volume` and `mix` (top to bottom). All four start **off** when the plugin is opened. Every jump (stutter, bit mask change, dropout) uses a 64-sample cosine crossfade, so there are no digital clicks. All buffers (≥ 250 ms, power-of-two sizes with mask indexing) are allocated in `prepareToPlay`.

**One-shot auto level.** The first time you switch an effect on after loading the plugin, it measures the RMS going into that effect and coming out of it (0.25 s to settle, then 0.4 s of actual playing, only above −60 dBFS) and sets that effect's `volume` (−24…+24 dB) so the level stays the same; the card shows "auto..." meanwhile. It happens once per effect per load: turning the effect off and on again keeps your volume, and an effect that is already on in a saved project is never recalibrated.

1. **Starve**: a low-band sag envelope (8/140 ms) pushes the bias off-centre; a dead-zone "velcro" gate; asymmetric saturation.
2. **Mosh**: at random times (16–80 ms) it freezes and repeats windows from 60 ms down to 6 ms, reversed 25 % of the time.
3. **Corrupt**: samples quantised to 16-bit with XOR on the low bits (random mask, up to 11 bits) and rhythmic 3–15 ms dropouts with 1 ms ramps.
4. **Melt**: a sample & hold whose rate collapses as the note decays: `sr / (1 + intensity · 28 · (1 − env))`. A wobbling resonant low-pass follows (coefficients at control rate, every 16 samples).

Starve uses a fast Padé tanh; an effect that is off and faded out costs nothing.

`output` sets the final level.

The four cards each show a mini scope of the waveform after that effect. Inside each card, everything sits on the same inner margin on all four sides.

Value boxes have a fixed width computed from the widest value the parameter can show, never resize, and show a light fill bar for their position in the range. **Double-click** a value to reset it and select it for typing (click outside to keep the default).

## Measured (see `Tests/Tests.cpp`)

- Everything off (the default): output identical to the input, bit for bit.
- First switch-on of starve: volume set to +7.6 dB, output minus input −0.04 dB; switching it off and on again does not recalibrate.
- Mosh at 100 %: largest sample-to-sample jump 0.010 on a 220 Hz sine (a clean one is 0.009), so no clicks.
- CPU: about 0.5 % of one core, stereo.

## About panel

Click the b-starve logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-suite (all the plugins are released together, with one installer and one version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BStarveTests_artefacts/Release/BStarveTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

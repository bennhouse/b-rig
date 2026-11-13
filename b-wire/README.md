# b-wire

A sub-harmonic synth and fuzz driven by an analog-style phase-locked loop (PLL). Zero latency.

1. **Detection**: a 65–900 Hz band-pass (detection path only), a fast RMS envelope (2/25 ms) and a Schmitt trigger with adaptive thresholds (±0.18 × RMS), gated below −54 dB. The period between rising edges is measured with sub-sample interpolation and followed with the `tracking` time (2 ms tight … 45 ms glide, default 5 ms).
2. **Oscillator**: a master phase with a PolyBLEP square, plus −1 and −2 octave flip-flops (also PolyBLEP on their edges).
3. **Fuzz, XOR & ring**: a fixed +18 dB fuzz (fast Padé tanh; the amount is set by `fuzz`), "glitch" = fuzz × sub −1 (an analog XOR gate) and **adaptive ring mod** (`ring`): the DI multiplied by a sine whose frequency follows the fundamental locked by the PLL, so the ring tones stay in key with the note (a 110 Hz note gives 220 Hz, the fundamental itself disappears). The five voices are mixed with `fuzz`, `sub 1`, `sub 2`, `glitch` and `ring` (0 % = off).
4. **VCA & filter**: the DI envelope gates the oscillators (they only sound while you play), then a resonant SVF low-pass (`filter` 400 Hz–12 kHz, default 3 kHz, Q 1.4).
5. **Output**: `mix` (dry/wet, default 100 %) sits right above `level`. `level` is either fixed (−24…+12 dB) or **`auto`**: the output RMS follows the input RMS while you play (400 ms windows, 250 ms smoothing), and the applied gain is shown under the scope.

There is no `drive` control any more and no mono → stereo layout: the plugin runs mono → mono or stereo → stereo.

The graph is a stepped scope of the main square and both sub-octaves (about the last 21 ms), with the locked frequency shown.

Value boxes have a fixed width computed from the widest value the parameter can show, never resize, and show a light fill bar for their position in the range. **Double-click** a value to reset it and select it for typing (click outside to keep the default).

## Measured (see `Tests/Tests.cpp`)

- A held 110 Hz note locks at 110.0 Hz.
- With only `sub 1`: 55 Hz sits about 37 dB above 110 Hz.
- In silence the output is digital silence after the release.
- `level auto`: output minus input = +0.08 dB.
- `ring` alone on a 110 Hz sine: 220 Hz is 112 dB above 110 Hz (the carrier really follows the note).
- CPU: about 0.1 % of one core.

## About panel

Click the b-wire logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BWireTests_artefacts/Release/BWireTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

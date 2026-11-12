# b-scenery

Procedural ambience (Foley) and lo-fi that breathe with your guitar. No WAV files: everything is synthesised (well under 1 MB of RAM, nothing to loop).

- **vinyl**: crackle like a real record: Poisson clicks (xorshift32, about 40 per second) with power-law amplitudes (many tiny ticks, a few loud ones), each an impulse through a 1.2 kHz high-pass and a 3.5 kHz resonance, panned at random; rare pops (0.4/s) with a body under 600 Hz; stereo surface hiss (1–9 kHz) that breathes with the rotation (33⅓ rpm = 0.56 Hz); turntable rumble under 30 Hz.
- **amp hum**: a real amp hum is mostly not the mains tone: a weak 50/60 Hz fundamental (`mains`), the rectifier ripple (|sin|: 100/120 Hz and its even harmonics, the "buzz"), a little odd content from the transformer (sin³), a slow amplitude sway, plus the amp's own hiss (2–9 kHz). All from quadrature oscillators: no trigonometry per sample.
- **rain**: a decorrelated stereo pink wash (200 Hz–7 kHz) with slow gusts, about 260 drops per second on three fixed resonances (1.8 / 3.2 / 5.5 kHz, 1.5 ms decays, random pan) and rare heavy drops at 900 Hz. No filter coefficients are computed per drop.
- **lo-fi**: processes the whole bus (guitar + textures): sample & hold down to 14 kHz, wow & flutter (1.2 Hz, up to 1.8 ms, with jitter, cubic interpolation), and a soft telephone band (180 Hz–4.2 kHz).

A layer that is off or at 0 % costs nothing: its whole DSP block is skipped. The textures run only while the transport plays or the guitar is above −65 dBFS; otherwise they fade to true silence in about 35 ms.

`react` ducks the textures against the guitar's RMS envelope (4/220 ms): pick hard and the scene steps back; hold a chord or stop and it fills the space. `texture` sets the texture level.

**balance**: press it and play for 2.5 s (the graph says "play your guitar..."). It sets `texture` so the textures sit 16 dB under the guitar. The result depends on how hard you play during the measurement.

The graph is a scrolling history (about 5 s, one vertex per pixel) on a single dBFS scale (0 to −72):
- **guitar**: the grey surface from the bottom;
- **texture**: the white line (RMS of each frame); the dashed line is where the texture would be without `react`, and the shaded band between the two is the inverse ducking, opening as you play harder;
- on the right, four thin vertical bars show how loud each layer is right now (vinyl, hum, rain, lo-fi), grey when the layer is off.
Grid, scale, legend and layer names are drawn once into a cached image.

Value boxes have a fixed width computed from the widest value the parameter can show, never resize, and show a light fill bar for their position in the range. **Double-click** a value to reset it and select it for typing (click outside to keep the default).

## Measured (see `Tests/Tests.cpp`)

- No transport and no guitar: digital silence.
- `react` 100 %: the textures drop about 41 dB under a loud note compared with a quiet one.
- balance: textures land at −16.1 dB relative to the guitar.
- lo-fi at 100 % cuts 8 kHz by about 10 dB (with the decimator fold-back).
- CPU: about 0.4 % of one core with all four scenes.

## About panel

Click the b-scenery logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-suite (all the plugins are released together, with one installer and one version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BSceneryTests_artefacts/Release/BSceneryTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

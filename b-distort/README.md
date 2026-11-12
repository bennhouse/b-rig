# b-distort

A distortion plugin with five distinct circuit-inspired types, a tone control that balances lows and highs, low cut and hi cut, dry/wet mix and output level. Each type has its own filtering before, between and after the clipping stages, as in the circuits it is modeled on. The nonlinear stage runs at 4× oversampling.

**Formats:** AU, VST3 · **Platforms:** macOS (universal: Apple silicon + Intel); Windows/Linux via the same CMake project · **Latency:** 6 samples at 48 kHz (oversampling filters, reported to the host) · **Channels:** mono or stereo

## Types

Similar voices were merged (soft into `tube`, hard into `rat`) so that every type sounds clearly different:

| Type | Inspired by | Circuit | Character |
|---|---|---|---|
| `tube` | two cascaded triode stages | 30 Hz coupling, biased stage → 9 kHz Miller → 20 Hz coupling → stage biased the other way | warm, even + odd harmonics, cleans up at low drive |
| `drive` | green mid-hump overdrive | only the part above 720 Hz goes into the diodes; the clean signal is added back; 5 kHz low-pass | tight lows, mids forward, smooth |
| `rat` | op-amp distortion with silicon diodes | 60 Hz high-pass; gain rises ~12 dB above 1.5 kHz; op-amp bandwidth narrows as gain goes up; silicon diodes to ground | hard, aggressive, odd harmonics |
| `fuzz` | transistor fuzz with a scooped tone stack | two asymmetric saturated stages (100 Hz coupling, 6 kHz between them), 600 Hz low-pass + 1.8 kHz high-pass blend, 7 kHz low-pass | near-square wall of fuzz, scooped mids, even + odd harmonics |
| `fold` | wavefolder | sine fold | metallic, synthetic |

**Levels are matched.** A volume table (type × drive, measured with a simulated guitar DI and K-weighted loudness) keeps every type at every drive as loud as the input, within 0.2 dB. Drive changes the character, not the loudness, and type changes can be compared directly. With output at 0 dB the plugin is level-neutral.

## Controls

`type` · `drive` (0–36 dB) · `tone` (0–100 %, default 50 % = flat: the balance between lows and highs around 800 Hz, like the tone knob of a green overdrive; from 0 % dark, lows +6 dB and highs −6 dB, to 100 % bright, the opposite) · `locut` (12 dB/octave high-pass on the output, 0–800 Hz; 0 = off, the default) · `hicut` (low-pass on the distorted signal, 500 Hz–20 kHz; it was called `tone` before, projects keep its value) · `mix` (0–100 %, the dry path is delay-compensated so the blend never comb-filters) · `output` (−24…+12 dB, default 0 dB)

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range (left = minimum, right = maximum). **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing, in light grey. Type a new value and press Enter; click anywhere outside to deselect and keep the default. Boxes have a fixed width (sized for the longest possible value), so they never resize while the value changes.

Preset memory: each preset remembers your changes for as long as the plugin stays loaded. Switch away and back and your values are still there. Double-clicking the preset (or a value) restores its defaults. This memory is not saved with the project; the project itself recalls the current settings as usual. Each type remembers its drive, tone, hicut, mix and low cut (everything except output).

The left graph shows the shape of the transfer curve (normalized, the real level is matched separately). Its Y axis ranges itself: it stays at ±1 while the curve fits, then switches to ±5 and to ±12 when `mix` and `output` push the curve past it (with a little hysteresis so it does not flicker); with a wider scale the ±1 lines stay as dashed references and the scale labels turn red. The curve's normalization peak is cached and recomputed only when type or drive change. A glowing dot moves along the curve in real time, at the input's current peak level (about 150 ms release). The right graph shows the live spectrum of input (grey) and output (red): 20 Hz–20 kHz, 8192-point FFT, +4.5 dB/octave tilt, computed only while the window is open. Over it, the always-visible red line is the frequency response of the output filters: `tone` and `hicut` on the distorted signal, blended by `mix`, and `locut` on everything. It uses its own scale, 0 dB on the top line and −24 dB on the bottom one. The tests check it against the audio within 0.1 dB.

Projects saved with the earlier six-type version load with the closest new type (soft/tube → `tube`, hard/rat → `rat`).

## Measured (see `Tests/Tests.cpp`)

- `tone`: lows minus highs (200 Hz against 4 kHz) +9.1 dB at 0 %, +0.1 dB at 50 %, −8.8 dB at 100 %.

- `drive`, `rat` and `fold` produce no even harmonics (below −180 dB); `tube` and `fuzz` produce a 2nd harmonic at about −33 / −19 dB.
- `drive`: at 18 dB drive, distortion at 100 Hz is −37 dB and at 1.6 kHz −16 dB (clean lows).
- `rat`: at the same input level, distortion at 1.6 kHz is about 10 dB higher than at 200 Hz.
- `fuzz`: the 800–1250 Hz band sits about 10 dB lower, relative to lows and highs, than with `drive` or `rat`.
- Aliasing with a 5 kHz tone at 30 dB drive is 15–34 dB lower than the same curve without oversampling.
- CPU: under 1 % of one core per stereo instance at 48 kHz / 128 samples.

## About panel

Click the b-distort logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls and font are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDistortTests_artefacts/Release/BDistortTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing. Circuit names are used only to describe the sound; this project is not affiliated with their makers.

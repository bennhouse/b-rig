# b-tight

A pick-attack and palm-mute shaper for guitar DI. Put it **before** the amp sim or distortion. It tightens the low end of palm mutes and lets you push or soften the pick attack.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo (linked detection) · **CPU:** about 0.2 % of one core

## How it works

Four stages in series:

1. **Mud cut**: 12 dB/octave Butterworth high-pass; `tuning` picks the corner: `standard` 80 Hz, `drop` 62 Hz, `baritone` 45 Hz.
2. **Chug clamp**: a Linkwitz-Riley 24 dB/octave crossover. The split follows the tuning: 220 Hz in standard, 190 Hz in drop, 160 Hz in baritone. A fast compressor works only on the low band: RMS detection, 3 ms attack, 65 ms release, 4:1, at most 12 dB of reduction. With `auto` on, the make-up follows the average reduction over about 1 s and gives the low end back the level the clamp took. A long palm mute ends up within 0.4 dB of its input level, while the chug keeps its tighter dynamics.
3. **Pick attack**: the detector listens to the pick "click" above 1 kHz, so it reacts the same with every tuning. A fast peak envelope (0.5/15 ms) is compared with a slow one (15/60 ms). During the attack the high band rises (`bite` > 0) or falls (`bite` < 0), by up to 9 dB. The gain is smoothed (0.2 ms up, 8 ms down), so it never clicks or distorts.
4. **Focus tilt**: a 1.4 kHz bell (Q 0.9) from 0 to +12 dB (`push`), with the output trimmed by −push/2 so the amp isn't hit much harder.
5. **Output**: `gain` (−24…+24 dB) after the whole effect, and **`autogain`**: when on, the output RMS follows the RMS of the incoming DI (about 1 s windows, only while you play, 30 ms ramps), the `gain` box greys out and the applied gain is shown under the graph. At 0 dB with nothing else active the plugin is still bit-transparent.

Per-sample level conversions use fast log2/exp2 instead of log10/pow.

## Controls

`tuning` · `palm-mute clamp` (−48…0 dB, low-band threshold) · `auto` (make-up for the clamp) · `pick bite` (−100…+100 %) · `mid push` (0…12 dB) · second row: `gain` (−24…+24 dB) · `autogain`

**It starts doing nothing.** With no tuning selected and all controls at zero, the output is the input, bit for bit (not even the crossover runs), and the graph says so (`off: choose a tuning to switch b-tight on`). Picking `standard`, `drop` or `baritone` switches on the matching high-pass and loads a preset:

| Tuning | High-pass | Clamp | Bite | Push |
|---|---|---|---|---|
| standard | 80 Hz | −24 dB | +25 % | +3 dB |
| drop | 62 Hz | −26 dB | +30 % | +4 dB |
| baritone | 45 Hz | −28 dB | +35 % | +4.5 dB |

Clicking the selected tuning again switches the high-pass off.

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range. **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing. Type a new value and press Enter; click anywhere outside to deselect and keep the default.

The graph scrolls the last few seconds, with a legend on each part:
- the output envelope (grey);
- low-band reduction on palm mutes (dim lime, from the top, 12 dB = half height);
- pick attacks boosted or cut (lime bars from the bottom, 9 dB = half height).

## Measured (see `Tests/Tests.cpp`)

- `gain` −6 dB: exactly −6.0 dB at the output, even with everything else off.
- `autogain` with the standard preset and 12 dB of push: output minus input goes from −9.4 dB to +0.06 dB.
- With neutral settings, 1 kHz passes unchanged; 30 Hz is cut by the 80 Hz high-pass.
- Clamp at −36 dB: a strong 110 Hz note is reduced by about 11.7 dB (12 dB max), 2 kHz is untouched.
- Bite +100 % / −100 %: the first 5 ms of a pick are about +9 / −6 dB compared with bite 0.
- Bite +100 % on each tuning's lowest open string: standard +9.0 dB, drop +8.8 dB, baritone +9.1 dB.
- Clamp −36 dB with `auto`: a long 110 Hz palm mute comes back to −0.4 dB of its input level (−10.6 dB without auto).
- Two steady tones with clamp and bite active: spurious components at −55 dB (no audible modulation).
- Push +12 dB: +6 dB at 1.4 kHz after the −6 dB trim.

## About panel

Click the b-tight logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BTightTests_artefacts/Release/BTightTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

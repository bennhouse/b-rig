# b-meter

A precision analyser with a loudness meter and gain. It is transparent apart from `gain` and has zero latency.

The audio thread only applies the gain (read once per block, 10 ms multiplicative ramp, SIMD multiply when steady), measures loudness (EBU R128 / ITU-R BS.1770-4: exact K-weighting filters, momentary 400 ms, short-term 3 s, 100 ms blocks) and the sample peak. It copies the samples into a lock-free FIFO (`juce::AbstractFifo`). FFTs, scopes and history all run on the UI thread at 60 fps. Grids, axes and labels are rendered once into a cached image (redrawn only when the size, view or a label-changing parameter changes), and only the moving parts are drawn each frame, so dragging `gain` stays immediate even while the analyser runs.

## Views (`view`)

- `osc`: triggered oscilloscope (rising zero crossing with hysteresis, steady waveform). With few samples per pixel the trace is a Catmull-Rom spline through the samples; with many, a filled min/max envelope (no aliasing, no lost peaks). Y −1…+1 with −6 / −12 dB references. Controls: `time` (1–50 ms), `gain` (vertical zoom).
- `vec`: vectorscope rotated 45° (M up, S across) with phosphor persistence; consecutive samples are joined with anti-aliased segments, so the trace is continuous. Plus an L/R correlation bar (−1…+1). Controls: `persist`, `scale`.
- `wave`: scrolling peak/RMS history in dBFS (0, −6, −12, −24, −48), drawn as filled surfaces with one vertex per pixel (interpolated between the 10 ms blocks on short windows). Controls: `window` (2–15 s), `mode` (peak+rms or L/R).
- `spec`: smooth analyser: two 8192-point FFTs (L and R, Blackman-Harris), 200 log-spaced points from 20 Hz to 20 kHz with Gaussian 1/3-octave smoothing (interpolated between bins in the lows), soft rise and speed-dependent fall, drawn as a Catmull-Rom curve with a gradient fill. 0 to −60 dB. Controls: `speed` (fast / med / slow), `tilt` (flat, +3 or +4.5 dB/octave, 0 dB at 1 kHz).

Segmented selectors size each segment to its own label (measured in the bold face of the lit segment), so no label is ever truncated (e.g. `peak+rms`).

## Loudness (right column)

- **`target`**:
  - social (−14 LUFS, TP −1)
  - youtube (−14, −1)
  - digital (−10.5, −0.5)
  - cd (−8.5, −0.3)
  - podcast (−16, −1)
- **The bar** runs from 0 to −36 LUFS. The target zone (±1 LU) is lit; short-term is the thick line and momentary the thin one. Values turn amber above the target, and the peak turns red above the target's limit.
- **`gain`** (−24…+24 dB) moves the signal into the target.
- **`auto`** listens for 5 s: play the loudest part (chorus or drop). It then sets `gain = target − loudest short-term`, lowered if needed so the peak stays under the target's limit.

The peak is the sample peak, not a true peak.

Value boxes have a fixed width computed from the widest value the parameter can show, never resize, and show a light fill bar for their position in the range. **Double-click** a value to reset it and select it for typing (click outside to keep the default).

## Measured (see `Tests/Tests.cpp`)

- Gain 0 dB: output identical to the input.
- 1 kHz at −20 dBFS stereo: −19.99 LUFS momentary and short-term.
- `auto` on that signal with the social target: +6.00 dB, after which the short-term reads −13.99 LUFS.
- The window can be closed and reopened on the same instance (tested 4 times).
- Audio-thread CPU: about 0.05 % of one core.

## About panel

Click the b-meter logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BMeterTests_artefacts/Release/BMeterTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

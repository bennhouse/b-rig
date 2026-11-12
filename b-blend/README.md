# b-blend

A vector mixer for four cabinet impulse responses. It phase-aligns them automatically and adds a dynamic speaker resonance, so a static IR reacts to palm mutes.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono → mono, mono → stereo, stereo → stereo · **CPU:** about 0.75 % of one core (four 0.1 s IRs)

## How it works

1. **Auto phase alignment**: each IR is aligned to the reference (A, or the first one loaded).
   - First, by the onset (first sample above 10 % of the peak).
   - Then, if the two IRs are similar (normalised correlation ≥ 0.6), the cross-correlation computed via FFT refines it within ±0.25 ms, below one sample (parabolic interpolation).
   - Cabs with very different tone have ambiguous correlation, so they stay aligned on the onset.
   - The shift is applied as a linear phase ramp in the FFT domain. IRs are resampled to the session rate, trimmed to 0.5 s and normalised to the same energy.
2. **XY vector mixer**: equal-power corner weights (A top-left, B top-right, C bottom-left, D bottom-right); empty corners are skipped and the weights redistributed. A single composite IR is built on a background thread whenever the pad moves and run through one zero-latency convolution, not four. It is normalised on its average gain for a guitar-like spectrum (pink, 80 Hz–6 kHz) rather than on total energy, so a cab's resonance doesn't make it louder.
3. **Dynamic speaker thump & sag**: the RMS of the 60–150 Hz band, read on the input before the cab, drives a 98 Hz bell (Q 2.0): `thump` × min(12 dB, RMS × 36). Palm mutes make the cone "move".
4. **Input trim**: the input is lowered by a fixed 7 dB (after an amp sim such as Neural DSP the level reaches the cab too hot and clips what follows). No dynamic gain.
5. **Air** (+12 dB high shelf at 7 kHz at 100 %), then the final `locut` (0 = off) and `hicut` (20 kHz = off). The cuts are TPT state-variable filters (Cytomic/Zavalishin): the cutoff glides in octaves (~30 ms, coefficients every 16 samples) and the filter state is never reset, so sweeping `hicut` makes no zipper noise or "sparkle". Switching a cut on or off at the end of its range crossfades between dry and filtered; a cut that is off costs nothing.

IR audio is stored inside the project, so it reopens even without the original files.

## Controls

`thump` · `air` · `locut` (0–400 Hz) · `hicut` (2–20 kHz) · XY pad (X, Y)

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range. **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing. Type a new value and press Enter; click anywhere outside to deselect and keep the default.

**Loaders** sit at the top right, next to the title:
- Empty: a filled "load A…D" button. Clicking it asks for a **folder**, then opens the IR browser over the plugin.
- Loaded: the slot letter (bold, teal), the file name (shortened with "..." so it never runs under the ×) and a × to remove it. Clicking it opens the plugin's IR browser in the folder that IR was chosen from, or its own folder, or the last folder you opened (all saved with the project). The system folder picker only appears when none of these is known. You can also drop a file onto a loader.
- If the file's sample rate differs from the session's, the loader turns red and shows the file rate (for example "44.1k!"). The IR is still resampled, but the warning tells you which file to check.
- **IR browser**: every .wav, .aif and .flac in the folder and its subfolders, in natural order, each with its frequency response (20 Hz–20 kHz, 36 dB, from a 16384-point FFT), sample rate and length. The analysis runs on a background thread starting from the rows on screen, so the list fills in immediately even with large folders (14 IRs in 17 ms in the test); scrolling is smooth and follows the trackpad. Hovering the curves shows a vertical line that follows the mouse across every graph, and next to the pointer the frequency it points at ("356 Hz", "1.2 kHz"). The label follows the mouse up and down too, over any row or the gap between rows; where it touches a curve, those pixels turn negative so it stays readable. Rows are 72 px tall and each curve sits on a faint grid (100 Hz, 1 kHz, 10 kHz; −12 and −24 dB) labelled once above the list, so the differences between IRs are easy to read. **While the browser is open you hear only the IR being chosen**, without the blend with the other slots (it comes back when the browser closes). IRs already loaded in another slot are marked with that slot's letter and cannot be loaded a second time (clicking one says so; the arrows skip them; a file dropped onto a second loader is refused). Click a row to load it into the slot and hear it (the browser stays open), use the up/down arrows to step through the folder, double-click or Return to load and close, `folder` to pick another folder, `x` or Esc to close.
- Reading and decoding a chosen or dropped file happens on the background builder thread (the loader shows "loading..."), so the interface never stalls; the file dialog reopens in the last folder used. (The first opening of the macOS file panel inside a host can still take a moment: that is the system dialog itself.)

**Blend shape** (left) follows how many IRs are loaded, with a short animated transition. The pad is a true square, centred in its column with the same margin on every side (and the same distance to the response graph):
- 1 IR: no pad, just that IR;
- 2: a horizontal line through the centre;
- 3: an equilateral triangle (side = pad width, centred; barycentric equal-power weights);
- 4: the square.

Each vertex shows its slot letter (bold, teal) and its IR name in light text outside the shape (above the top vertices and the line, below the bottom ones), never over an edge. Names on the same row get at most half the pad width each; a longer name wraps onto a second line (between words) and is shortened with "..." only beyond that. Above a top vertex (square, triangle) where two lines would leave the interface, the name stays on one shortened line, and no name is ever drawn past the edges. Drag the node to blend; **double-click the node** to send it back to the geometric centre (middle of the line, centroid of the triangle = three equal parts, centre of the square). The response curve is recomputed only when the composite IR, a filter or the thump level changes.

Right: the magnitude response of the current composite IR (normalised to 0 dB), including the live thump bulge at 98 Hz, air and cuts. Frequency axis: 20, 100, 1k, 8k, 20k.

## Measured (see `Tests/Tests.cpp`)

- An IR delayed by 7.3 samples is found at 7.56; one delayed by 30.6 at 30.76.
- At corner A the output peak sits at the IR's own onset (zero latency); corner B lands within one sample of A.
- Blend at the centre vs at a corner: same energy (0.01 dB).
- A strong palm mute raises the 98 Hz thump by +7.5 dB (limit 12 dB).
- IRs survive save/reload of the project state.
- With 1, 2 or 3 IRs, every vertex of the shape outputs exactly that IR, in phase.
- **No IR loaded: the output is muted.** The plugin stays on and says so in amber in the middle ("no IR loaded: audio muted"): an amp without a cab would reach your ears raw and loud.
- Input trim: exactly −7 dB, and the level is constant over time (no dynamic gain).
- `hicut` jumping between 2 kHz and 19 kHz on every block over a 150 Hz tone: the energy of the sample-to-sample difference is −33.9 dB, the same as the clean tone itself (no zipper, no sparkle).

## About panel

Click the b-blend logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BBlendTests_artefacts/Release/BBlendTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

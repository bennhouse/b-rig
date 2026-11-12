# b-double

Turns a mono guitar into a realistic double-tracked stereo pair, and keeps it from falling apart when the mix is summed to mono.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono → stereo or stereo → stereo (input summed to mono) · **CPU:** about 0.3 % of one core

## How it works

1. **Delay**: manual by default. `delay` (6–40 ms, default 16) sets a fixed tap; `detune` (0–20 cents, default 6) a fixed pitch offset. In center-wide the two doubles use delay ∓ 3 ms and ∓ detune.
   - **`dynamic`** (off by default) humanizes it: at every pick attack (fast/slow envelope ratio) each double draws a new tap around `delay` and a new pitch around `detune`. `tightness` sets how far: a timing jitter from ±0.5 ms (100 %) to ±8 ms (0 %), plus a little extra pitch jitter when loose; it also sets the depth of the slow 0.35 Hz drift. (`tightness` is greyed out when `dynamic` is off.)
   - **The delay never glides.** Sliding a delay by milliseconds is an audible pitch bend (that was the burst heard at playback start and at every pick). A new tap is reached with a 40 ms cosine crossfade between two fixed taps; manual changes of `delay` work the same way.
2. **Doubler**: a power-of-two delay line (mask indexing, cleared in `prepareToPlay`), 4-point Hermite interpolation and a two-head pitch shifter whose sin² / cos² windows always sum to exactly 1 (no trigonometry per sample), so there is no discontinuity.
3. **Variation**: a different guitar, not just a different level: a tilt of ±4.5 dB around 1.2 kHz in opposite directions on the two doubles, and two all-pass filters (900 / 2600 Hz) blended at 50 % for a different tone.
4. **Mono-safe**: there is no automatic mono detection (a plugin can't know if the master is summed). Raise `mono-safe` while listening in mono until the cancellations go away. On the generated track it blends in four first-order all-pass filters (180/650/1900/4200 Hz), raises a high-pass up to 300 Hz and lowers the track by up to 6 dB in sustained passages.
5. **Phase guard** (dynamic only):
   - 30 ms after each pick, within ±1.5 ms of the drawn tap, b-double picks the tap at which the note just played is least correlated with itself (relative to the dry: 2 ms in hard-pan, 0 in center-wide), then crossfades to it;
   - every ~43 ms it measures the dry/double correlation in the 80–600 Hz band, where the fundamentals live. If it goes clearly negative (< −0.3, the double is cancelling the dry), an all-pass tuned to the dominant frequency (estimated from zero crossings) is faded in and turns the double's phase by 90° right there, until the correlation is positive again. "phase guard" appears under the detune readout while it acts.

## On/off

The `double` box switches the effect on or off. Off, the original goes to both channels (with a 20 ms crossfade). The goniometer stays live either way. With no audio for half a second, the delay and pitch readouts go to rest.

## Modes

- `hard-pan`: left = the original, delayed by a fixed 2 ms; right = the double.
- `center-wide`: the original in the centre, two doubles left and right.

## Controls

`mode` · `double` · `delay` (6–40 ms) · `detune` (0–20 cents) · `dynamic` · second row: `tightness` (0–100 %) · `variation` (0–100 %) · `mono-safe` (0–100 %)

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range. **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing. Type a new value and press Enter; click anywhere outside to deselect and keep the default.

The graph is a goniometer (M vertical, S horizontal), with the current delay (ms) on the left and pitch offset (cents) on the right.

## Measured (see `Tests/Tests.cpp`)

- Hard-pan: the left channel is the input delayed by exactly 2 ms.
- Dynamic, tightness 100 %: average L/R delay 16 ms.
- No clicks: at startup, at every attack in dynamic with tightness 0 %, and changing delay and detune, the largest sample-to-sample jump of the double stays at or below that of the clean 220 Hz sine (0.0079–0.0085 vs 0.0086).
- Tightness really works: over 5 s of picking the tap moves by 15.6 ms at 0 % and 2.7 ms at 100 %.
- Mono-safe really works: a 110 Hz note summed to mono swings by 17.5 dB as dry and double drift in and out of phase with mono-safe 0 %, and by 0.6 dB with 100 %.
- CPU: about 0.3 % of one core (center-wide).

## About panel

Click the b-double logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-suite (all the plugins are released together, with one installer and one version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDoubleTests_artefacts/Release/BDoubleTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

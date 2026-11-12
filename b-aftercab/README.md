# b-aftercab

A post-cab processor for electric guitar: EQ, compressor, stereo doubler and delay/reverb in one plugin, tuned for modern heavy guitar tones. Insert it after an amp sim / cab IR.

**Formats:** AU, VST3 · **Platforms:** macOS (universal: Apple silicon + Intel); Windows/Linux via the same CMake project · **Latency:** 0 samples (except linear-phase EQ, reported to the host)

## Signal chain

```
in -> EQ -> compressor -> spread (doubler) -> delverb -> out
```

## Features

- **5-band EQ**: low/high bands switch between shelf and cut; three bells. Zero-latency (biquad) or linear-phase (FIR, three quality levels). The drawn curve is the measured response of the filter actually running, also in linear-phase mode. In linear-phase mode the FIR is rebuilt on a background thread, so dragging a point stays smooth. The reported latency is half the nominal FIR length. The convolution runs on large partitions (its own latency B), and the FIR is shortened by 2B and centred so the total stays the same; this costs about a fifth of a zero-latency convolution. If the host's buffer is too large for that, it falls back to the zero-latency engine.
- **Live spectrum**: the EQ graph shows the post-EQ spectrum (8192-point FFT, +4.5 dB/octave tilt so a guitar or mix reads flat, peaks fall at 18 dB/s). It is computed only while the window is open; with the window closed it costs nothing.
- **Compressor**: three presets with their own ratio and timing: `rhythm`, `lead`, `clean`. Feed-forward with a soft knee and linked detection. The compressor itself applies no make-up gain; the level it takes away is given back by OUT (`auto`). The on/off box is also the gain-reduction meter, on a log scale: even a tiny amount of reduction lights at least one row of pixels.
- **Output (OUT)**: a vertical fader to the right of the EQ, with an `auto` button below it (on by default). With `auto` on, OUT keeps the output at the input's level whatever is switched on:
  - **EQ:** how much it changes the level of a typical post-cab guitar (100 Hz–4 kHz, power average). Boost every band by 4 dB and OUT goes to about −3.3 dB.
  - **Compressor:** the gain reduction it really applies to the signal passing through (input vs output energy over about 2 s, measured only above −55 dBFS, so pauses don't count). Until 0.15 s of playing has been measured after a type or threshold change, there is no makeup (a fixed estimate could be several dB too high for a quieter guitar and caused a volume peak). In `auto` the level rises slowly (350 ms) and falls fast (30 ms): switching the compressor on, the volume first dips a little and then comes back up, never above the final level (tested: at most +0.00 dB above it). Tested at −43 and −29 dBFS input: output within 0.05 dB of the input.
  - **Delverb and spread:** already constant-power.

  With `auto` on, the fader and its frame turn yellow. With `auto` off, only the top edge of the filled part is yellow, as a handle to drag. Dragging the fader, using the wheel or typing a value switches to manual.
- **Spread** (mono in → stereo out): sends the dry signal left and a 1–30 ms delayed copy right (Haas). The delay is a 32-tap windowed-sinc fractional delay, so the dry side stays sample-exact.
- **Phase align** (`=`): listens for a few seconds and fine-tunes the delay to the value with the least comb filtering, with sub-sample accuracy. The analysis runs off the audio thread.
- **Delverb**: a filtered, ducked delay feeding an 8-line FDN reverb (delay first, then reverb). Delay and reverb always share the same channels and are never split left/right:
  - With a stereo output (stereo in, or mono in → stereo out, with or without spread) each channel has its own delay with the same timing and the reverb is stereo.
  - With a mono output the whole delverb is mono.
  - Switching between the two is crossfaded.
  - `room`: short reverb for rhythm parts (default mix 10 %).
  - `plate`: dotted-1/8 delay, made for leads.
  - `hall`: no delay, only a full hall reverb and a shimmer verb. The balance goes from `shim` (left side) to `verb`: at 0 % shimmer it is a pure hall; turning towards `shim`, an octave-up copy of the tail (above 1.2 kHz) is fed back into the reverb (up to 0.6) and also heard directly, until the tail is mostly the octave above. The level stays constant (within 0.2 dB) along the whole balance, and nothing saturates.
  - One mix control plus a balance that keeps the total level constant: `del … verb` on the plate, `shim … verb` on the hall. The side you are turning down dims progressively (never completely), so you can see which way you are moving. The balance box moves the same way as its bar: drag right (or `>`) for more delay / shimmer. Tempo and tap only apply to the plate (the only timbre with a delay).
  - Delay time: two controls side by side.
    - **BPM box** (left), a value box like the others: a single click switches DAW sync on/off. With sync on it is filled yellow and reads "sync"; with sync off it is grey and shows the BPM in yellow, with a faint "tap to sync" hint just above it. Drag, arrows or wheel set the BPM, which also switches sync off. A fast double-click (under 250 ms) lets you type the BPM.
    - **tap** (right): yellow with black text while sync is off; tap tempo starts at the 4th tap if the last four fall within 3 s. Dimmed and inactive while sync is on.
- **Channel layouts**: 1→1, 1→2, 2→2. The UI adapts; the spread section only appears for mono→stereo. With spread off, the delay box and `=` are disabled and the alignment status is hidden.

## Defaults

A new instance starts with the EQ on and flat, compressor, delverb and spread off, and the delay time synced to the DAW tempo.

## Controls

- Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range (left = minimum, right = maximum). **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing, in light grey. Type a new value and press Enter; click anywhere outside to deselect and keep the default. Boxes have a fixed width (sized for the longest possible value), so they never resize while the value changes.
- Preset memory: each preset remembers your changes for as long as the plugin stays loaded. Switch away and back and your values are still there. Double-clicking the preset (or a value) restores its defaults. This memory is not saved with the project; the project itself recalls the current settings as usual. Remembered per preset: compressor threshold per type; delverb mix and balance per timbre.
- EQ: the scale is ±15 dB (±4 dB with `precision`). The selected band shows `freq`, `gain` and `q` boxes. A band that is switched off is grey everywhere, even when selected: its point, its selection frame, its number and its `freq` / `gain` / `q` boxes (greyed and inactive until the band is on again). In precision the gain box adjusts within ±4 dB; a value set beyond that in normal mode stays as it is until you touch it. The point is drawn on the edge of the scale and, when grabbed, continues from there.
- EQ points:
  - a single click switches the band off or on once the double-click time has passed, so the point stays where it is while you click a second time;
  - a double-click resets the band to its defaults (frequency, gain, Q, type) and leaves on/off as it was;
  - dragging (beyond 4 px) changes frequency and gain only, never on/off, so a small hand movement still counts as a click;
  - mouse wheel for Q; right-click on bands 1 and 5 to switch shelf/cut.

## Performance

`BAftercabTests --bench`: share of one core, 48 kHz, 128-sample buffer, mono → stereo unless noted, best of 5 runs (Apple silicon):

| Feature | CPU |
|---|---|
| everything off / spread only | 0.06 % |
| EQ zero latency, 5 bands active | 0.17 % |
| EQ linear phase low / mid / high | 0.16 / 0.20 / 0.14 % |
| compressor | 0.08 % |
| delverb room / plate / hall | 0.20 / 0.23 / 0.26 % |
| everything on (linear high, comp, spread, hall) | 0.40 % |
| stereo: EQ 5 bands + comp + hall | 0.44 % |
| UI, full frame | 0.5 ms |

The audio thread takes no locks and allocates nothing. FIR design, response analysis and phase alignment run on their own threads. The spectrum is copied only while the window is open, and the EQ curve is recomputed only when a parameter changes.

## About panel

Click the b-aftercab logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BAftercabTests_artefacts/Release/BAftercabTests   # DSP + UI tests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. To skip that, pass `-DBAFTERCAB_COPY_AFTER_BUILD=OFF`.

## Layout

```
Source/DSP/   Biquad, FractionalDelay, PhaseAligner, Compressor, Reverb, Delverb
Source/UI/    EqDisplay, b-aftercab-specific widgets (shared theme and controls live in ../shared/smp)
Tests/        DSP tests (frequency response vs. drawn curve, latency, levels, CPU) and UI snapshots
```

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

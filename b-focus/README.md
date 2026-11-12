# b-focus

A zero-latency de-noiser for guitar and bass DI tracks. It removes fizz, EMI, mid-range hash and amp-sim hiss between and under notes, closes the whole spectrum in the pauses, and passes the signal through bit for bit while you play.

**Formats:** AU, VST3 · **Platforms:** macOS (universal: Apple silicon + Intel); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo (linked detection) · **CPU:** about 0.2 % of one core per stereo instance

## How it works

No FFT, no latency. The audio path is four bands made of three cascaded shelves plus a final gain. Each shelf uses a two-pole low-pass (two first-order TPT, zero-delay feedback stages, Q 0.5):

```
lp = TPT(TPT(x));   y = lp + m · (x − lp)
```

- **Open** (`m = 1`): the output *is* the input sample. No phase rotation and no change in crest factor; the tests check bit-exact equality.
- **Closing** (`m < 1`): the shelf cuts everything above its corner by `m`, with a 12 dB/octave transition.

The gain of each band is the product of the multipliers above it:

| Band | Range | Gain | Max reduction (strength 100 %) |
|---|---|---|---|
| 1 · fizz / air | > 2.8 kHz | mA · mB · mC · mD | −42 dB |
| 2 · upper-mid buzz | 0.9–2.8 kHz | mB · mC · mD | −42 dB |
| 3 · midrange hash | 300–900 Hz | mC · mD | −36 dB (85 % of the expander's reduction) |
| 4 · low / fundamentals | < 300 Hz | mD | −48 dB, only while the strings are silent |

Bands 1–3 are monotonic from the top: a higher band is never more open than a lower one, so when real harmonics are present in a high band the bands below open at least as much and nothing musical is lost. Band 4 is free to close further than the others when everything stops.

The control path reads the input (linked on all channels) and is updated every 8 samples; the multipliers are smoothed per sample (0.1 ms opening, 1 ms closing) and snap to exactly 1.0 when open:

- **Per-band detectors**: a two-pole crossover (SVF, 2800 / 900 / 300 Hz) splits the side-chain into the 4 bands; each has its own peak envelope and its own threshold `thr[k] = threshold + offset[k]` (offsets come from the learned noise profile, or typical DI values without learn). A 1:10 downward expander with a 6 dB soft knee per band (almost a gate) brings a noisy band to its floor as soon as it sits 4–5 dB under its threshold: before a high-gain amp, a "half-reduced" noise would be raised by 50–60 dB and still be heard. Each band has 4 dB of hysteresis and a 20 ms hold (or the release, if shorter), so a tail hovering around the threshold doesn't make the band open and close over and over (that flutter is very audible through distortion).
- **"Shutup" logic**: while the body (bands 3 + 4) rings above its gate, band 4 stays fully open and bands 1–3 open and close independently, each with its own energy. Holding a low note with no harmonics above 900 Hz, bands 1 and 2 close and the fizz *under* the note goes away. When you stop (or mute the strings), all four bands close over the whole spectrum, 20 Hz–20 kHz, so the amp sim after it gets real silence, mids included.
- **Transient guard**: a fast envelope (0.1/12 ms) against a slow one (20/120 ms) on the highs. A pick attack, legato or tap opens the filter even below the threshold, so ghost notes don't get dull.
- **Adaptive release**: with the strings ringing your release time is used; when the body falls under its gate (staccato, djent stops), release drops to 15 ms (or your release, if shorter) and everything closes at once.

## Auto-Learn (4-band noise profiler)

Press `learn`, leave the strings muted for a moment, then play a few notes (a light one and one left ringing). The plugin listens for 4.5 s while the button counts down; click again to cancel.

The 4.5 s window is split into 50 ms frames (the peak of each of the 4 bands, the body level and the transient activity of each frame, kept in a fixed array). At the end the frames are sorted into two clusters by their body level:

- **Cluster A, muted strings** (body < −58 dB): the 95th percentile of the noise in each band separately (the **noise profile**) and the body noise floor.
- **Cluster B, playing** (body ≥ −52 dB): the quietest high-band level of the notes you played, and the natural decay rate of the sustain.

If no frame is below −58 dB (mains hum or a noisy DI), the clusters become relative to the quietest frame: A is within 6 dB of it, B at least 12 dB above, and pauses and notes must be at least 20 dB apart.

From the profile b-focus stores an offset for each band's threshold (`noise[k] − noise[1]`): if your guitar has an EMI bump between 400 Hz and 1.2 kHz, only that band's threshold rises and the cut in the mids becomes surgical. Offsets and profile are saved with the project.

| Setting | How it is set |
|---|---|
| `threshold` | Just above the high-band noise, below your lightest note: min(noise + 6 dB, quietest note − 3 dB), never less than noise + 2 dB. |
| band offsets | From the 4-band noise profile. |
| body gate | Loudest of the band 3 / band 4 noise + 6 dB. |

`strength` and `release` stay where you left them. The analysis runs on the audio thread without allocations; results are applied on the message thread, so the DAW and the UI update immediately.

The graph shows what learn is doing: waiting for audio (if the transport is stopped), listening, the values it set, or why it failed (`no pause`: mute the strings for a moment and retry; `no audio received`: press play or arm the track).

**Tip.** On open chords and long ringing notes, the filter can open and close over and over while the strings still sound. When b-focus sees this it shows a short grey hint in the graph, "tip: works best on tight rhythm parts".

With no audio (below −100 dBFS) the meter and the graph show no action.

## Controls

`threshold` (−100…−10 dB, default −85 dB: real passive DIs often sit at −85/−95 dBFS above 1.4 kHz) · `strength` (0–100 %; 0 % is a bit-exact bypass) · `release` (2–1500 ms, log scale, default 15 ms: shorter releases can flutter audibly through high gain)

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range (left = minimum, right = maximum). **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing, in light grey. Type a new value and press Enter; click anywhere outside to deselect and keep the default. Boxes have a fixed width (sized for the longest possible value), so they never resize while the value changes.

`learn` runs the Auto-Learn calibration. The header meter (`gr`) shows the deepest reduction among the 4 bands in real time. The graph draws the real frequency response of the 4 bands from the multipliers the audio engine is using (it drops on the mids and lows too when the gate closes completely, scale 0 to −48 dB, corners at 300 / 900 / 2.8k marked), plus a faint dashed grey **noise profile** captured by learn (normalised to the noisiest band).

## Measured (see `Tests/Tests.cpp`)

- Above the threshold, and at strength 0 %, the output is identical to the input, bit for bit (latency 0).
- Noise only (strings silent, −60 dBFS): −42 dB above 5 kHz, −38 dB at 350–800 Hz, −38.5 dB at 40–250 Hz with strength 100 % (−29 / −27 / −27.5 dB at 70 %).
- A held 82 Hz note with noise: fizz at 5–15 kHz −44 dB, fundamental within 0.1 dB.
- From a closed state, a pick attack becomes bit-transparent within 0.9 ms.
- A ghost note below the threshold reopens the filter fully (42 dB less reduction than the noise floor).
- With release set to 1000 ms, a muted stop closes the buzz to −20 dB in about 64 ms; a sustained note keeps the long release (about 440 ms).
- With release at 2 ms, the buzz closes almost immediately even while the strings ring.
- Auto-Learn sets the controls and the 4-band offsets with clean pauses, with a 60 Hz hum at −45 dBFS on top, and refuses (with a message) a take with no pauses or no audio.
- **Before a high-gain amp** (DI with noise at about −80 dBFS → b-focus after `learn` → +50 dB of distortion): noise in the pauses goes from −36 to −78 dBFS, hiss above 4 kHz in a decaying tail from −16 to −56 dB (relative to the 60 Hz–1 kHz body), and the high band closes once on the tail instead of fluttering.
- CPU: about 0.1 % of one core per stereo instance.

## About panel

Click the b-focus logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls and font are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BFocusTests_artefacts/Release/BFocusTests
```

The DSP engine is a single header (`Source/DSP/FocusEngine.h`, no JUCE dependency): real-time safe, `noexcept`, no allocations in the audio path.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

# b-verb

A reverb for any source (guitar, voice, synth): room, plate, hall, spring and shimmer.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.5 % of one core (shimmer)

## How it works

1. **Predelay** (0-200 ms): one circular buffer per channel; the early reflections are read from the same buffer.
2. **Early reflections:** 10 taps per channel with a different pattern left and right, spread by `size` and set by `early`. Their level depends on the type: strong in `room`, light in `hall` and `shimmer`, almost none in `plate`, none in `spring`. Walls and air absorb the highs, so the reflections pass a low-pass that depends on the type: 4.5 kHz in a hall, 6.5 kHz in a room, 12 kHz on a plate.
3. **Diffusion:** 4 Schroeder all-passes in series, denser in `plate` (instant, dense attack), softer in `hall` (the tail builds up). `spring` instead runs the input through 24 first-order all-passes in cascade (dispersion, the "drip/chirp" of real springs) and a 4.5 kHz low-pass.
4. **Tail:** a feedback delay network of 8 lines mixed by an 8×8 Hadamard matrix (a fast transform: only sums and differences). The line lengths are mutually prime and scale with `size` and the type. Each line ends in a first-order absorbent shelf: below `damp` its gain gives exactly −60 dB in `decay` seconds, above `damp` it is the square of that, so the highs last exactly half as long, as in a real room. `mod` slowly moves the line lengths (quadrature oscillators, no sine per sample) against metallic ringing; the lines are read with 4-point Hermite interpolation, so moving them does not dull the highs on every pass as linear interpolation would.
5. **Shimmer:** inside the loop, a two-head pitch shifter (60 ms grains) adds the octave above at 25 %, so every pass climbs higher.
6. **Freeze:** feedback 1, no damping and no new input: the tail holds as an endless pad.
7. **Output:** `locut` and `hicut` on the wet, `width` (mid/side), `duck` (the wet drops by up to 17 dB while the dry is loud and comes back in the pauses), `mix` (the dry stays full up to 50 %) and `output`.

All controls glide (about 15 ms), so moving them does not click.

## Controls

The window has three levels, from the most important down:

- **Top right:** `type` (room, plate, hall, spring, shimmer).
- **Big values**, each centred in its own column in a fixed box: the number never moves when its digits change (it is right-aligned in a slot as wide as the longest value), the grey fill and the box's bottom edge in the plugin's colour show where the value sits in its range: `size` (0-100 %), `decay` (0.10-20.00 s), `predelay` (0-200 ms), the `freeze` button and `mix`. Drag (right or up for more), use the wheel, or double-click to reset and type a value.
- **The graph** (0 to −60 dB over time, in seconds once the tail passes 1 s): above it, a bracket from 0 to the start of the reverb labelled with the `predelay`; the early reflections as vertical bars (moved by `predelay`, as tall as `early`, labelled `EARLY`); the decay of the body as a filled area and, as a thin line, the decay at 8 kHz (shorter because of `damp`), both from the same formula as the loop. Where each line reaches −60 dB its time is written next to it (`body 2.39 s`, `highs 8 kHz 1.51 s`), in the free space past the end of the line. On the right, outside the time axis, two live meters: `IN` (grey) and `WET` (cyan).
- **Groups** at the bottom, in equal columns with a thin line between them. Their numbers (15 px), labels (14 px) and boxes (22 px high) are a little larger than in the other plugins, and the big values a little smaller (20 px in 34 px boxes), so the two levels stay in balance. The optional ones have an on/off box next to the title; when a group is off its title and labels turn dark grey and its controls are disabled:
  - **early** (on/off): `level` of the early reflections.
  - **tone**: `damp` (1-16 kHz), `locut` (20-500 Hz), `hicut` (1-20 kHz).
  - **modulation** (on/off): `depth`.
  - **duck** (on/off, off by default): `amount`, how much the wet drops while you play.
  - **output**: `width`, `output`.

Switching a group on or off glides, so it never clicks. Projects saved before the on/off boxes open with `duck` on if it was already in use.

## Measured (see `Tests/Tests.cpp`)

- RT60 measured around 500 Hz: 0.82 s for `decay` 0.8 s, 2.33 s for 2.4 s, 5.94 s for 6 s (within 3 %).
- `damp` 3 kHz, `decay` 3 s: RT60 at 6 kHz measured 1.64 s, predicted 1.67 s.
- `predelay` 50 ms: nothing comes out before 50 ms.
- `mix` 0: the output is identical to the input.
- `freeze`: the tail is within 2 dB after 5 s.
- Every type at `decay` 20 s and `mod` 100 %: stable, the level keeps falling.
- `shimmer`: the octave above in the tail is 31 dB stronger than with `hall`.
- `duck` 100 %: the output is 2.2 dB lower while a loud steady note plays (mix 50 %).

## About panel

Click the b-verb logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BVerbTests_artefacts/Release/BVerbTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

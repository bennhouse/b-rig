# b-space

Puts an amp (or any instrument) in a room, from the room's geometry. There are no reverb controls (no decay, wet/dry or pre-delay): you choose the room, the walls, the source and where the amp and the listener stand. Put it at the end of the guitar chain, after the cab.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples (with `dist comp` on) · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.2 % of one core

## How it works

1. **Geometry, only when a control changes.** A rectangular room (width, depth, ceiling) and first-order image sources: the direct sound, the left, right, front and back walls and the ceiling. For each of the 6 paths: distance, delay, gain (d0 / d times the wall reflection), arrival direction (interaural delay up to 0.6 ms and head shadow above 1.5 kHz on the far ear, plus a level difference), the amp's directivity (a one-pole low-pass from 12 kHz on axis to 1.8 kHz from behind for a 4x12, softer for a combo, none for omni) and air absorption over the distance.
2. **Distance compensation.** With `dist comp` on the direct sound has zero delay and the reflections keep their delay relative to it (d - d0): zero latency. Off, the direct sound arrives after d0 / 343 m/s.
3. **One buffer.** All paths are read from one circular buffer (2^16 samples, bit-mask indexing). Delays, gains and filters glide to new values in about 30 ms, so dragging the amp does not click. No `sin`, `cos`, `atan2` or `sqrt` in the audio loop.
4. **Tail.** A 4-line feedback delay network with a Hadamard matrix (sums and differences only). Line lengths are scaled on the mean free path (4V/S), the reverb time comes from Sabine's formula with the wall absorption, the damping from the wall material and the air, and the tail level from the distance to the amp against the room's critical distance.

## Controls

- `space` (studio, garage, club, hall): loads the room's size. `walls` (treated, wood, brick, concrete), `source` (cab 4x12, combo, omni), `dist comp` (on by default).
- `width` (2.5-30 m), `depth` (2.5-40 m), `ceiling` (2.2-12 m), `angle` (where the amp points, ±180°), `air` (0-100 %).
- The plan: drag the amp and the listener; the mouse wheel on the amp turns it; a double-click on either puts it back where it started (a line at the top of the plan says so). Lines show the direct sound and the four wall reflections, with dots where they hit the walls. Under the plan: the direct distance, the first reflection and the tail time.

## Measured (see `Tests/Tests.cpp`)

- `dist comp` on: the direct sound comes out at sample 0.
- `dist comp` off: the direct sound arrives at the sample the geometry gives (3.28 m, sample 458 at 48 kHz); the first reflection (the ceiling, +243 samples) arrives exactly where expected.
- Amp to the right of the listener: the right channel arrives 29 samples earlier and 10.6 dB louder.
- Amp turned around (180°): the direct sound loses 11.5 dB at 8 kHz.
- Concrete walls leave a much louder tail than treated walls (200-600 ms after the note); a concrete hall decays below -100 dB within 5 s.

## About panel

Click the b-space logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-suite (all the plugins are released together, with one installer and one version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BSpaceTests_artefacts/Release/BSpaceTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

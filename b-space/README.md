# b-space

Puts an amp (or any instrument) in a room, from the room's geometry. There are no reverb controls (no decay or pre-delay): you choose the room (its size and its materials come together), the source and where the amp and the listener stand, and `mix` sets how much of the room you hear. Put it at the end of the guitar chain, after the cab.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples (with `dist comp` on) · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.2 % of one core

## How it works

1. **Geometry, only when a control changes.** A rectangular room (width, depth, ceiling) and first-order image sources: the direct sound, the left, right, front and back walls, the ceiling and the floor (the amp stands 0.6 m from it, the ears are at 1.5 m: it is usually the first reflection). For each of the 7 paths: distance, delay, gain (d0 / d times how much the surface reflects), arrival direction (interaural delay up to 0.6 ms and head shadow above 1.5 kHz on the far ear, plus a level difference), the amp's directivity (a one-pole low-pass from 12 kHz on axis to 1.8 kHz from behind for a 4x12, softer for a combo, none for omni), air absorption over the distance, and the material: each bounce loses the highs above the frequency of the surface it comes from (treated walls 2.5 kHz, wood 4.5 kHz, brick 6 kHz, concrete 8 kHz).
   **Walls close to the amp** add low end: under 160 Hz a nearby wall sends the wave back almost in phase. Against a wall the amp gains about 6 dB at 80 Hz compared with the middle of the room (tested), more in a corner; beyond a metre the effect is gone.
2. **Distance compensation.** With `dist comp` on the direct sound has zero delay and the reflections keep their delay relative to it (d - d0): zero latency. Off, the direct sound arrives after d0 / 343 m/s.
3. **One buffer.** All paths are read from one circular buffer (2^16 samples, bit-mask indexing). Delays, gains and filters glide to new values in about 30 ms, so dragging the amp does not click. No `sin`, `cos`, `atan2` or `sqrt` in the audio loop.
4. **Tail.** A 4-line feedback delay network with a Hadamard matrix (sums and differences only). Line lengths are scaled on the mean free path (4V/S), the reverb time comes from Sabine's formula with the room's absorption, the damping from its materials and the air, and the tail level from the distance to the amp against the room's critical distance. The tail is fed and read through the materials' filter: a treated room has a dark, short tail, concrete a bright, long one.
5. **Mix.** `mix` blends the signal that comes in (0 %) with the room (100 %, the default): at 0 the output is the input, untouched.

## Controls

- `room` (top right): six rooms, each with its size and its materials, shown at the top right of the plan.

  | room | size (m) | walls and ceiling | floor | tail (air 50 %) |
  |---|---|---|---|---|
  | studio | 5.5 × 7 × 3 | treated | wood | 0.2 s |
  | live | 8 × 10 × 4 | wood | wood | 0.6 s |
  | garage | 6 × 7.5 × 2.6 | bare concrete | concrete | 1.7 s |
  | club | 12 × 18 × 5 | brick, people | wood | 1.2 s |
  | hall | 22 × 34 × 11 | wood panels, seats | seats | 2.1 s |
  | church | 20 × 40 × 16 | stone | stone | 4 s |

  Choosing a room loads its size; you can then change `width`, `depth` and `ceiling`, while the materials stay those of the room. (They used to be two controls, `space` and `walls`; projects saved before open with the matching room.)
- `source` (cab 4x12, combo, omni), `dist comp` (on by default), `mix` (0-100 %, 100 by default).
- `width` (2.5-30 m), `depth` (2.5-40 m), `ceiling` (2.2-12 m), `angle` (where the amp points, ±180°), `air` (0-100 %).
- The plan: drag the amp and the listener; the mouse wheel on the amp turns it; a double-click puts it back where it started, and on the amp also straightens it towards the listener again (a line at the top of the plan says so). Lines show the direct sound and the four wall reflections, with dots where they hit the walls. Under the plan: the direct distance, the first reflection and the tail time.

## Measured (see `Tests/Tests.cpp`)

- `dist comp` on: the direct sound comes out at sample 0.
- `dist comp` off: the direct sound arrives at the sample the geometry gives (3.28 m, sample 458 at 48 kHz); the first reflection (the ceiling, +243 samples) arrives exactly where expected.
- Amp to the right of the listener: the right channel arrives 29 samples earlier and 10.6 dB louder.
- Amp turned around (180°): the direct sound loses 11.5 dB at 8 kHz.
- The concrete garage leaves a much louder tail than the treated studio (200-600 ms after the note: −31.7 against −84.8 dB); the church decays below −100 dB within 5 s.
- Materials: in the bounces, energy above 4 kHz against below 1 kHz is −2.4 dB in the studio and +4.4 dB in the garage.
- Amp against the front wall of the garage: 80 Hz is 6.6 dB louder (against 2 kHz) than with the amp in the middle of the room.
- `mix` at 0: the output is the input, identical. A project saved with `space` garage opens with the garage room.

## About panel

Click the b-space logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

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

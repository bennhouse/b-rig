# b-verb

A reverb for any source (guitar, voice, synth): room, plate, hall, spring and shimmer.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.4 % of one core (shimmer)

## How it works

1. **Predelay** (0-200 ms): one circular buffer per channel; the early reflections are read from the same buffer.
2. **Early reflections:** 10 taps per channel with a different pattern left and right, spread by `size` and set by `early`. Their level depends on the type: strong in `room`, light in `hall` and `shimmer`, almost none in `plate`, none in `spring`.
3. **Diffusion:** 4 Schroeder all-passes in series, denser in `plate` (instant, dense attack), softer in `hall` (the tail builds up). `spring` instead runs the input through 24 first-order all-passes in cascade (dispersion, the "drip/chirp" of real springs) and a 4.5 kHz low-pass.
4. **Tail:** a feedback delay network of 8 lines mixed by an 8×8 Hadamard matrix (a fast transform: only sums and differences). The line lengths are mutually prime and scale with `size` and the type. Each line's feedback gives exactly −60 dB in `decay` seconds. `damp` is an absorbent filter inside the loop: above that frequency the tail lasts half as long, as in a real room. `mod` slowly moves the line lengths (quadrature oscillators, no sine per sample) against metallic ringing.
5. **Shimmer:** inside the loop, a two-head pitch shifter (60 ms grains) adds the octave above at 25 %, so every pass climbs higher.
6. **Freeze:** feedback 1, no damping and no new input: the tail holds as an endless pad.
7. **Output:** `locut` and `hicut` on the wet, `stereo` width (mid/side), `duck` (the wet drops by up to 17 dB while the dry is loud and comes back in the pauses), `mix` (the dry stays full up to 50 %) and `output`.

All controls glide (about 15 ms), so moving them does not click.

## Controls

- `type` (room, plate, hall, spring, shimmer), top right.
- Top row in the frame: `decay` (0.10-20.00 s), `predelay` (0-200 ms), `size` (0-100 %), `locut` (20-500 Hz), `hicut` (1-20 kHz), `mix`.
- Left of the graph: `early`, `damp` (1-16 kHz), `mod`. Right of the graph: `duck`, `stereo`, `freeze`, `output`.
- The graph (0 to −60 dB over time): the early reflections as vertical bars (moved by `predelay`, as tall as `early`), the decay line of the body and, lighter, the decay at 8 kHz (shorter because of `damp`), the wet band, and the live input (grey) and wet level (cyan).

## Measured (see `Tests/Tests.cpp`)

- RT60 measured around 700 Hz: 0.80 s for `decay` 0.8 s, 2.29 s for 2.4 s, 5.65 s for 6 s.
- `predelay` 50 ms: nothing comes out before 50 ms.
- `mix` 0: the output is identical to the input.
- `freeze`: the tail is within 2 dB after 5 s.
- Every type at `decay` 20 s and `mod` 100 %: stable, the level keeps falling.
- `shimmer`: the octave above in the tail is 31 dB stronger than with `hall`.
- `duck` 100 %: the output is 2.2 dB lower while a loud steady note plays (mix 50 %).

## About panel

Click the b-verb logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BVerbTests_artefacts/Release/BVerbTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

# b-delay

A delay for any source with its character inside the feedback loop, so each repeat wears a little more than the one before.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.3 % of one core

## How it works

- One circular buffer per channel (allocated before playback), read with 4-point Hermite interpolation. The delay time glides in about 80 ms, so changing it bends the pitch of the repeats like tape, without clicks.
- **Types**, all inside the loop:
  - `digital`: clean repeats.
  - `tape`: soft saturation (peaks compressed), a 9 kHz low-pass, wow (0.5 Hz) and flutter (6 Hz).
  - `bbd`: bucket brigade, a 2.8 kHz second-order low-pass and a light saturation: warm and dark.
  - `lofi`: samples held at about 12 kHz and 8 bits, then a 300 Hz-3 kHz band.
  - `reverse`: two heads read grains as long as the delay time backwards, crossfaded with a cosine window.
- `locut` and `hicut` are in the loop too; `smear` passes the repeats through 4 all-passes, so the attacks soften a bit more on each pass and the delay turns into a pad; `mod` is a slow chorus/vibrato on the repeats (up to ±3 ms, left and right in quadrature).
- A soft clipper in the feedback: up to 110 % the delay oscillates without blowing up.
- `duck` lowers the repeats while the dry is loud. `trails` (on by default): when the host bypasses the plugin the tail keeps fading out; off, it stops.

## Controls

- `type` (digital, tape, bbd, lofi, reverse), top right.
- Top row in the frame: `time` (1-2000 ms; with `sync` a note value instead: 1/1 to 1/16, dotted D and triplet T, on the song tempo), `sync`, `tap` (the average of the last clicks; it switches sync off), `feedback` (0-110 %), `locut` (20-800 Hz), `hicut` (0.8-20 kHz), `mix`.
- Left of the graph: `offset` (the right channel's time ±50 % against the left), `ping-pong`, `mod`. Right: `smear`, `duck`, `output`, `trails`.
- The graph (0-2000 ms, left above the centre line, right below): the predicted repeats (position from time, offset and ping-pong, height from feedback, wider bells with smear), the sound travelling along the delay line in indigo, the input at 0 ms in grey, and the repeats lighting up when they sound.

## Measured (see `Tests/Tests.cpp`)

- 250 ms, feedback 50 %: repeats at 12000 and 24001 samples (48 kHz), the second 6.2 dB below the first.
- `offset` +20 %: the right repeat at 300 ms. `ping-pong`: the first repeat on the left only, the second on the right.
- `sync`: dotted 1/8 at 120 bpm = 375 ms. `tap` at 500 ms intervals: 504 ms.
- Every type at feedback 110 %: it self-oscillates with peaks under 1.1.
- Changing the time while playing: the repeats bend in pitch with no discontinuity (second difference at most 0.27 of the first).
- Bypass with `trails`: the tail keeps going (−22 dB); without: silence.
- `mix` 0: the output is identical to the input.

## About panel

Click the b-delay logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDelayTests_artefacts/Release/BDelayTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

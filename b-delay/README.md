# b-delay

A delay for any source with its character inside the feedback loop, so each repeat wears a little more than the one before.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.3 % of one core

## How it works

- One circular buffer per channel (allocated before playback), read with 4-point Hermite interpolation. The delay time glides in about 80 ms, so changing it bends the pitch of the repeats like tape, without clicks.
- **Types**, all inside the loop:
  - `digital`: clean repeats.
  - `tape`: soft saturation (peaks compressed), wow (0.5 Hz) and flutter (6 Hz), and a second-order low-pass that follows the tape speed: a tape echo makes a longer delay by running the tape slower, which loses highs at the head. 14 kHz up to 120 ms, 6.6 kHz at 400 ms, 4.5 kHz at 800 ms.
  - `bbd`: a bucket brigade chip with 4096 cells. A longer delay means a slower clock, so the band gets narrower: a fourth-order low-pass at 0.4 × the clock, from 10 kHz on short slapbacks to 2.7 kHz at 300 ms and 900 Hz at 1 s, plus a light saturation.
  - `lofi`: samples held at about 12 kHz and 8 bits, then a 300 Hz-3 kHz band.
  - `reverse`: two heads read grains as long as the delay time backwards, crossfaded with a cosine window.
- `locut` and `hicut` are in the loop too; `smear` passes the repeats through 4 all-passes, so the attacks soften a bit more on each pass and the delay turns into a pad; `mod` is a slow chorus/vibrato on the repeats (up to ±3 ms, left and right in quadrature).
- A soft clipper in the feedback: up to 110 % the delay oscillates without blowing up.
- `duck` lowers the repeats while the dry is loud. `trails` (on by default): when the host bypasses the plugin the tail keeps fading out; off, it stops.

## Controls

The window has three levels, from the most important down:

- **Top right:** `type` (digital, tape, bbd, lofi, reverse).
- **Big values**, each centred in its own column in a fixed box: the number never moves when its digits change (it is right-aligned in a slot as wide as the longest value), the grey fill and the box's bottom edge in the plugin's colour show where the value sits in its range: `time` (1-2000 ms; with `sync` the note value instead: 1/1 to 1/16, dotted D and triplet T, on the song tempo), the `sync` and `tap` buttons (`tap` sets the time from the fourth click, averaging the last clicks, and switches sync off), `feedback` (0-110 %) and `mix`. Drag (right or up for more), use the wheel, or double-click to reset and type a value.
- **The timeline** (left above the centre line, right below): the predicted repeats (position from time, offset and ping-pong, height from feedback, wider bells with smear), the sound travelling along the delay line, the input at 0 ms in grey, and the repeats lighting up when they sound. The window adapts to the delay (250 ms, 500 ms, 1 s or 2 s, the shortest that shows at least four repeats). With `tape` or `bbd` a tag says how far the highs of the repeats reach at the current time.
- **Groups** at the bottom, in equal columns with a thin line between them. Their numbers (15 px), labels (14 px) and boxes (22 px high) are a little larger than in the other plugins, and the big values a little smaller (20 px in 34 px boxes), so the two levels stay in balance. The optional ones have an on/off box next to the title; when a group is off its title and labels turn dark grey and its controls are disabled:
  - **tone**: `locut` (20-800 Hz), `hicut` (0.8-20 kHz).
  - **modulation** (on/off): `depth`, a slow chorus on the repeats.
  - **smear** (on/off, off by default): `amount` of diffusion.
  - **duck** (on/off, off by default): `amount`, how much the repeats drop while you play.
  - **stereo**: `offset` (the right channel's time ±50 % against the left) and the `ping-pong` button.
  - **output**: `output` and the `trails` button.

Switching a group on or off glides, so it never clicks (tested). Projects saved before the on/off boxes open with `smear` and `duck` on if they were already in use (tested).

## Measured (see `Tests/Tests.cpp`)

- 250 ms, feedback 50 %: repeats at 12000 and 24001 samples (48 kHz), the second 6.2 dB below the first.
- `offset` +20 %: the right repeat at 300 ms. `ping-pong`: the first repeat on the left only, the second on the right.
- `sync`: dotted 1/8 at 120 bpm = 375 ms. `tap` at 500 ms intervals: three clicks leave the time as it is, the fourth sets it to 500 ms.
- Every type at feedback 110 %: it self-oscillates with peaks under 1.1.
- Longer delays sound darker, as on the real machines (energy at 4 kHz against 500 Hz in the first repeat of a noise burst): `bbd` +7.6 dB at 100 ms, −14.9 dB at 800 ms; `tape` +7.7 dB at 100 ms, +4.0 dB at 800 ms.
- Changing the time while playing: the repeats bend in pitch with no discontinuity (second difference at most 0.27 of the first).
- Bypass with `trails`: the tail keeps going (−22 dB); without: silence.
- `mix` 0: the output is identical to the input.

## About panel

Click the b-delay logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDelayTests_artefacts/Release/BDelayTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

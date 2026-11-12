# b-synthesia

An audio-reactive visual generator for guitar playthrough videos (YouTube, Instagram Reels, TikTok): brutalist vector graphics and telemetry HUDs at 60 fps, with chroma-key backgrounds and a frame capture synced to the DAW. The audio passes through untouched, bit for bit, with zero latency.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo · **CPU (audio thread):** about 0.06 % of one core

## How it works

The audio thread does almost nothing: it copies the samples into a lock-free FIFO (`juce::AbstractFifo`) and computes four atomic descriptors:
- `sub_energy`: RMS of the 60–180 Hz band (palm mutes), 5/80 ms envelope;
- `transient_hit`: the rise of a fast envelope above 1.5 kHz over a slow one (fires on every pick);
- `level` (block peak) and `stereo_width` (L/R correlation).

Everything else runs in the interface, only while the window is open: a 4096-point FFT, 48 log bands from 40 Hz to 12 kHz, the five strongest peaks (at least 1/3 octave apart) and the dominant frequency and note. `decay` (20–1000 ms) is the visual trail of every envelope, `reactivity` how strongly the graphics react.

## Scenes (`[ tunnel | flow | terrain | prism | attractor ]`)

Generative scenes in the spirit of TouchDesigner. The ones with trails (tunnel, flow, prism, attractor) run in a small internal buffer (640 px on the long side, transparent ARGB, so they also work on chroma and alpha backgrounds) updated once per frame and scaled to the canvas or to the exported frame. `decay` sets how long the trails last.

- **tunnel** (colour): video feedback. Each frame the previous one is zoomed (more with palm mutes) and rotated (more with picks and `chaos`), and a neon polygon is drawn at the centre: the note being played picks its shape (3 to 8 sides), the level its size. Picks fire rays. An endless, hue-shifting tunnel.
- **flow** (colour): 2600 particles drift through a smooth flow field; speed follows the level, every pick pushes them out from the centre, and their colour shifts with speed. Their trails pile up like light.
- **terrain** (mono, the evolution of iso-mesh): a 3D landscape in true perspective. The spectrum is mirrored from the centre (lows in the middle, highs at the sides) and its history scrolls towards you; filled faces hide the ridges behind, the front ridges light up on picks, palm mutes send ripples through the ground, and a scanline sun pulses on the horizon.
- **prism** (RGB): a six-way kaleidoscope of a polar oscilloscope, drawn in red, green and blue with a chromatic split that opens on picks; with `chaos`, hard picks tear horizontal slices of the image sideways (datamosh).
- **attractor** (mono): a Clifford strange attractor of 30 000 points per frame; lows, mids, highs and palm mutes bend its four parameters, so the figure folds and blooms with the playing.

## Controls

`reactivity` (0–100 %) · `decay` (20–1000 ms) · `chaos` (rotation, jitter, slice glitches, 0–100 %) · `grid` (grid lines and the telemetry line at the bottom left, 0–100 %) · `color` (the suite's colours: white, amber, crimson, indigo, lime, cyan, teal) · `aspect` (`full`, `16:9`, `9:16`: the canvas is letterboxed to that shape) · `bg`.

All boxes have a fixed width. The window can be resized (640×480 to 2560×1600).

## Backgrounds and capture

- **`bg`**: `dark` (#0D0D0F), `green` (#00FF00) or `magenta` (#FF00FF) pure chroma keys (drawn without soft blending on the outer edges, so the keyer finds no dirty halo), or `alpha` (transparent in the exported frames; on screen it shows a faint checkerboard).
- **`clean view`**: hides the header and all controls and leaves only the canvas, for direct screen capture. Click anywhere to bring them back.
- **`rec sync`**: while armed, every time the DAW plays, the canvas is rendered off-screen at 1280×720 (16:9), 720×1280 (9:16) or 1280 wide (full) about 30 times a second. A low-priority background thread writes the frames as PNG files (real alpha with `bg alpha`) into `~/Movies/b-synthesia/<date_time>/`. Each file name carries the frame number and the DAW position in ms (`frame_000123_t000004100ms.png`), so the sequence lines up with your timeline in DaVinci Resolve, Premiere or Final Cut (import as an image sequence at 30 fps).

**Why this design.** Encoding video (H.264/ProRes) inside a plugin would need extra libraries or system frameworks and real CPU on the machine running the DAW. Writing frames synchronously at 60 fps risks dropouts and freezes. So the capture never touches the audio thread, renders at 30 fps (plenty for an overlay), and hands the frames to a writer thread through a short bounded queue: if the disk is slow a frame is skipped (the counter shows it), but the interface and the audio never wait. For the lightest workflow, `clean view` plus a screen recorder is also fine. Rendering uses `juce::Graphics` rather than OpenGL: OpenGL is deprecated on macOS and unreliable inside some hosts, and these vector scenes draw comfortably at 60 fps.

## Measured (see `Tests/Tests.cpp`)

- Audio output identical to the input, bit for bit.
- `sub_energy` reacts 22× more to a 90 Hz tone than to a 2 kHz tone; `transient_hit` fires on picks; a mono signal reads width +1.
- The analysis finds the peaks of a 220 + 660 Hz chord.
- An exported frame with `bg alpha` has a transparent background.
- Audio-thread CPU: about 0.06 % of one core.

## About panel

Click the b-synthesia logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-suite (all the plugins are released together, with one installer and one version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BSynthesiaTests_artefacts/Release/BSynthesiaTests
```

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

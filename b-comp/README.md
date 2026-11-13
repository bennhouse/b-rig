# b-comp

A compressor tuned for guitar, with five styles, a three-band mode set up for guitar and a graph that shows what it is doing. Put it before the amp to even out the picking, or after the cab to hold the level. It replaces b-tight.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo (one detector for both channels) · **CPU:** about 0.3 % of one core (multi, stereo)

## How it works

1. **Sidechain**: the detector reads the input after a high-pass (`sc hp`, 80 Hz by default), so the low end of a palm mute does not make the whole sound pump. In stereo one detector follows the louder channel.
2. **Level**: a peak follower (instant attack, 10 ms release, so it does not fall between two peaks of the wave), or RMS over 10 ms for `squash`.
3. **Gain computer** in dB with a soft knee (`knee`): nothing below the threshold, 1/ratio above it, a smooth curve across the knee.
4. **Times**: `attack` and `release` smooth the gain reduction in dB.
5. **Gain**: `auto gain` (on by default) gives back what the compressor takes off a guitar peaking at −10 dBFS; with it off, `makeup` sets it by hand.
6. **Parallel**: when on, `mix` blends the dry and the compressed signal.
7. **Output**.

## Styles

Each style starts from its own threshold, ratio, times and knee, and remembers your changes while the plugin is open. Double-click a style to reset it.

| style | character | threshold | ratio | attack | release | knee |
|---|---|---|---|---|---|---|
| clean | transparent VCA | −18 dB | 3:1 | 10 ms | 120 ms | 6 dB |
| opto | slow and smooth: attack at least 10 ms; the release gets longer (up to 4 times) the more and the longer it compresses, like an optical cell | −20 dB | 4:1 | 15 ms | 300 ms | 10 dB |
| fet | fast, hard knee, and a soft saturation that grows with the gain reduction | −16 dB | 8:1 | 1 ms | 80 ms | 2 dB |
| squash | RMS detection: heavy but smooth compression, the sustain of country and funk | −28 dB | 12:1 | 0.5 ms | 200 ms | 4 dB |
| multi | three bands for guitar (below) | −20 dB | 4:1 | 8 ms | 150 ms | 6 dB |

**multi** splits the signal with Linkwitz-Riley crossovers (24 dB/octave) at 250 Hz and 2.5 kHz. The low band starts compressing 3 dB earlier, with times 1.5 times slower, so palm mutes stay firm. The high band starts 3 dB later and is faster, so the pick stays alive. `low`, `mid` and `high` (multiband group, active only in this style) set how much each band works: at 0 % a band is not compressed, at 100 % it uses the full ratio. With no compression the three bands add back to the input within 0.09 dB (60 Hz to 12 kHz, tested).

## Interface

- **Top right:** `style`.
- **Big values:** `threshold` (−60…0 dB), `ratio` (1…20:1), `attack` (0.1…100 ms), `release` (10…2000 ms), and the `auto gain` button.
- **The graph:**
  - on the left, the compressor's curve (input across, output up, −60…0 dB), with the 1:1 diagonal, the threshold line and a dot where the sound is right now; in `multi` the three band curves;
  - in the middle, the last seconds: the level (input in grey, output in lime) above and the gain reduction hanging from the top of the strip below;
  - on the right, the gain reduction now, with its value, and in `multi` one bar per band.
- **Groups:** `detector` (`knee`, `sc hp`), `parallel` (on/off, `mix`), `multiband` (`low`, `mid`, `high`), `output` (`makeup`, grey while `auto gain` is on, and `output`).

Value boxes: drag horizontally, click the arrows or use the mouse wheel. Double-click a value to reset it and type a new one.

## Measured (see `Tests/Tests.cpp`)

- Threshold −20 dB, ratio 4, knee 0, a sine peaking at −6 dBFS: −10.48 dB (expected −10.5).
- Below the threshold the output is identical to the input.
- A step from −40 to −6 dBFS with 10 ms attack and 100 ms release: 63 % of the reduction after 10.0 ms, 63 % released after 106 ms.
- Every style compresses a loud pluck, without NaN.
- Multiband with no compression: flat within 0.09 dB.
- Parallel at 50 %: half dry and half compressed.
- Auto gain (clean): a guitar peaking at −10 dBFS comes out within 1.4 dB of its input level.

## About panel

Click the b-comp logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BCompTests_artefacts/Release/BCompTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

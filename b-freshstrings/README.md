# b-freshstrings (b-fs)

New strings, a different pick and another pickup position, on the clean DI. Put it right after b-focus, before any drive or amp.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono in, mono or stereo out · **CPU:** about 0.25 % of one core

## How it works

Filters only (no FFT, no convolution), coefficients updated every 32 samples. A detector compares a fast envelope (0.3 / 8 ms) with a slow one (8 / 80 ms) above 1.5 kHz to find the pick attack; under -56 dBFS (DI hiss) it never fires.

1. **Fresh strings.** The band above 2.2 kHz goes through a soft saturation (odd and even harmonics, cleaned above 3 kHz), and a shelf at 3.2 kHz (up to +6 dB) opens on the attack and closes on the decay (60 ms). The hiss of the DI is not raised.
2. **Pick.** The harder the pick, the shorter its contact with the string and the more highs reach the attack. `nylon` is flexible: it lets go in about 4 ms and softens above 4.5 kHz. `tortex` (acetal) lets go in about 2 ms and pushes 1.8 kHz. `carbon` is stiff: about 1 ms, a dry attack at 3.4 kHz, and the lows under 180 Hz held back on the attack. `metal` (steel) has the shortest release (about 0.7 ms) and the resonant scrape at 5.2 kHz. `attack` sets the push.
3. **Gauge.** A thicker string tuned to the same note carries more tension and is stiffer: more inharmonicity, a stronger fundamental and highs that die sooner. A thinner one has less tension, swings wider on the low notes (the "flubby" sound) and is brighter. So: four first-order all-pass filters (phase dispersion, more for thick strings; the tone does not change), a 110 Hz shelf (thin strings are dried when the lows ring loud, `.011` and `heavy` get more weight) and a 4 kHz shelf (+1 dB for `.009` down to -1.5 dB for `heavy`). `tension` sets how much of all this is applied (50 % = as listed, 100 % = double).
4. **Pickup.** The resonance of a magnetic pickup: `bridge` 3.4 kHz (Q 2.2, +3 dB), `both` 2.6 kHz, `neck` 1.9 kHz (Q 1.4) with a 0.45 ms comb that scoops 1.1 kHz, then the roll-off above (12 / 10 / 8 kHz). Near the bridge the string moves less, so there is less fundamental (-2.5 dB below 250 Hz); near the neck there is more (+2 dB).

## Controls

- `pick` (nylon, tortex, carbon, metal), `gauge` (.009, .010, .011, heavy), `pickup` (bridge, both, neck), top right next to the title. The title reads `b-fs` (the full name would leave no room for the three selectors); the file and the about panel keep `b-freshstrings`.
- `freshness` (0-100 %, default 50 %), `attack` (±6 dB, default +2 dB), `tension` (0-100 %, default 50 %), `output` (±12 dB).
- The display: a string that moves only while you play (speed and width from the DI level, still and straight in silence) and gets brighter on the attacks, and the pickup and string curve; the copper area is the shelf that opens on the attack.

## Measured (see `Tests/Tests.cpp`)

- Silence in, silence out.
- DI hiss at -70 dBFS: the highs change by 0.00 dB with freshness at 100 %.
- A plucked note: above 3 kHz +3.7 dB in the attack, -0.2 dB in the tail (400-600 ms).
- First 3 ms above 4.5 kHz: `metal` 5.2 dB brighter than `nylon`.
- 1.1 kHz is 5.8 dB lower with `neck` than with `bridge`; `bridge` is +3.0 dB at 3.4 kHz.
- At 80 Hz, `heavy` has 4.3 dB more than `.009`.

## About panel

Click the b-freshstrings logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button, which asks GitHub at once and says whether a newer version exists. Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BFreshstringsTests_artefacts/Release/BFreshstringsTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

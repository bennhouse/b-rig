# b-adapt

Gets a guitar part (or a mix) ready for social media and small speakers. It keeps the low end mono-safe and adds perceived bass where phones can't play it. It also tames harshness (mostly with a fixed EQ, plus narrow cuts on resonances) and keeps true peaks under −1 dBTP so AAC re-encoding doesn't clip. A monitor mode lets you hear it as on a phone or a laptop.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 6 samples (oversampling) + 48 samples (1 ms limiter lookahead) at 48 kHz, reported to the host · **Channels:** mono or stereo · **CPU:** about 1.2 % of one core

## How it works

Five stages in series:

1. **Mono guard**: M/S; a 12 dB/octave high-pass at 250 Hz on the side makes the bass mono. If the L/R correlation goes below zero, the side is lowered by up to 12 dB (`safe width`). The amount is shown live on the `side` meter.
2. **Phantom thump**: the 75–155 Hz band feeds a full-wave rectifier (even harmonics, DC-blocked) and a fast Padé tanh (odd harmonics). Their mix, band-passed to 170–420 Hz, is added to the signal (`thump`, up to 6× the harmonic band), so the bass is "heard" on speakers that can't reproduce it.
3. **De-harsh**: it works like an EQ more than a multiband compressor. 70 % is a fixed EQ on the hot spots: −5 dB at 3 kHz (Q 2) and −4 dB at 5 kHz (Q 2.2) at 100 %. 30 % is dynamic and very focused, in the spirit of resonance suppressors: four narrow bands (2.5, 3.4, 4.8 and 6.8 kHz, Q 5), each with a 1 ms attack / 40 ms release detector; a band comes down only when it sticks out more than 4 dB above the average of the others (a resonance, not loudness), by at most 2 dB. `de-harsh` scales both parts.
4. **Codec shield**: a 13.5 kHz low-pass, then:
   - a soft clipper running at 4× oversampling;
   - a limiter with 1 ms lookahead whose detector reads 4× interpolated peaks. Gain comes down along a linear ramp and reaches its target right on the peak.

   Output stays under −1 dBTP.
5. **Monitor** (listening only):
   - `phone`: mono, 4th-order high-pass at 400 Hz, +5 dB at 2.8 kHz, 4th-order low-pass at 9 kHz. Phone speakers lose output quickly below about 200–300 Hz; this curve sits at the narrow end, for a mid-range phone;
   - `laptop`: 4th-order high-pass at 180 Hz, +3.5 dB at 2.2 kHz, low-pass at 12 kHz.

   **Switch it off before exporting.** While it is on, the whole frame turns green and "AUDITION ACTIVE - BYPASS BEFORE EXPORT" is shown over the graph, on a solid background so the curve never runs through it (the side and correlation meters have one too).

## Controls

`monitor` (off / phone / laptop) · `safe width` (0–100 %) · `thump` (0–100 %) · `de-harsh` (0–100 %)

Value boxes: drag horizontally, click the arrows or use the mouse wheel. A light bar inside each box shows where the value sits in its range. **Double-click** a value: it goes back to its default and the whole value (number and unit) is selected for typing. Type a new value and press Enter; click anywhere outside to deselect and keep the default.

The graph shows the live output spectrum (grey). The "hot" zones (2–5 kHz and 6–10 kHz, where the ear is most sensitive) turn red when they stand more than 3 dB above the 200 Hz–2 kHz level. Over it is the composite response: the added harmonics band (indicative), the fixed de-harsh EQ and the narrow dynamic cuts as they move, the codec low-pass and the monitor filters. At the bottom right are the `side` meter (how much safe width is lowering the side right now, 12 dB = full) and the L/R correlation meter, which turns red below zero. With no audio both are at rest.

## Measured (see `Tests/Tests.cpp`)

- +6 dBFS input (an 11 kHz sine plus noise): output true peak −1.16 dBTP, measured with 8× oversampling.
- Side content at 60 Hz: −26 dB; signal at 2 kHz: unchanged.
- Thump 100 % on a 110 Hz note adds a strong 2nd harmonic at 220 Hz.
- De-harsh 100 % on noise (no resonance): −4.7 dB at 3 kHz, −0.3 dB at 1 kHz, no dynamic cut. With a resonance at 4.8 kHz on top: only that band is cut, by 2.0 dB (2.5 kHz: 0 dB).
- Monitor phone: L = R, 100 Hz below −20 dB.

## About panel

Click the b-adapt logo for a short description of the plugin, signed b-suite (free and open source, built with Claude Code). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the GitHub releases whether there is a newer b-rig (the plugins come with the b-rig installer and share its version). The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BAdaptTests_artefacts/Release/BAdaptTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

# b-dynaverb

Hears whether you are playing a riff on the low strings or a lead line on the high ones and, only on the lead notes, switches on a lead channel (a mid boost and a stereo delay). No automation needed. Put it after the cab.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** 0 samples · **Channels:** mono or stereo in, stereo out · **CPU:** about 0.15 % of one core

## How it works

1. **Classifier, every 32 samples.** No FFT and no pitch tracker. Two band-passes on the input: the riff band (75 Hz to 0.8 × `split`) and the lead band (1.06 × `split` to 1.8 kHz), with their energies smoothed over ~10 ms, plus the zero crossings below 2.5 kHz. When the lead band is above the riff band (1.5 dB of hysteresis) and the zero crossings say the note is above `split`, for at least 5 ms, the lead amount moves towards 1 in `speed` ms; otherwise it returns to 0 in 0.6 × `speed` (a palm mute closes it at once). Below -60 dBFS nothing changes.
2. **Lead focus.** A 1.1 kHz bell (Q 0.8) up to `boost` dB and up to 1.5 dB more output, both dosed by the lead amount.
3. **Ping-pong delay.** The lead amount opens the delay's *send*, not its output: back on the riff, the last note of the solo fades out on its own. A 250 Hz-3.2 kHz band-pass in the feedback; `space` sets the send and the feedback. `duck delay` lowers the repeats by up to 12 dB while you play. `sync` snaps the time to the nearest of 1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2 at the song tempo (in b-rig, the metronome tempo).

With the lead amount at 0 and no repeats left, the signal passes bit for bit.

## Controls

- `crossover` (low-mid 260 Hz, mid 320 Hz, high-neck 420 Hz): sets `split`. `duck delay` (on by default), `sync`.
- `split` (150-800 Hz, default 320 Hz), `boost` (0-6 dB, default 3.5 dB), `delay` (40-1200 ms, default 380 ms), `space` (0-100 %, default 35 %), `speed` (5-100 ms, default 25 ms).
- The display: a bar split into RHYTHM ZONE (grey) and LEAD ZONE (gold) with a cursor that moves as you play and its trail; under it, how much the lead channel is on and the delay time in use.

## Measured (see `Tests/Tests.cpp`)

- A palm-muted riff on the low E: output identical to the input, lead amount 0.
- A high note (659 Hz): lead amount 1; 1.1 kHz goes up by 5.0 dB (3.5 dB boost + 1.5 dB output).
- From riff to lead: 63 % in 33 ms; back to the riff: 37 % in 21 ms.
- After the last high note the repeats keep going (-27 dB, 200-600 ms later).
- `sync`: 380 ms at 120 bpm becomes a dotted 1/8 (375 ms).

## About panel

Click the b-dynaverb logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close. Inside b-rig the plugin has no title or about panel and its top row is centred.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls, fonts and the common window layout are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BDynaverbTests_artefacts/Release/BDynaverbTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build. The plugin is also a module of b-rig (`../b-rig`).

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

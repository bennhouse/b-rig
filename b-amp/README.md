# b-amp

The amp head of the suite: three channels (clean, British crunch, modern high gain), a two-level boost, a DI-keyed two-stage noise gate built in, and a fullscreen tuner. Every head keeps its own settings.

**Formats:** AU, VST3 · **Platforms:** macOS (universal); Windows/Linux via the same CMake project · **Latency:** a few samples of oversampling filters, reported to the host · **Channels:** mono → mono, stereo → stereo (summed) · **CPU:** about 3.3 % of one core (lead, 2x), 6 % at 4x

## Channels (`[ clean | crunch | lead ]`, top right)

- **clean** (ice blue): 2 gain stages with lots of headroom, a scooped-but-present tone stack, almost no sag (a very stiff power amp). `tight` works as a bright/punch control before the gain.
- **crunch** (orange, British style): 4 stages; the gain curve speeds up in its top half, so it goes from a classic crunch at noon (34 dB across the stages) to enough gain for djent at full (66 dB). `tight` moves the high-passes *between* the stages: high (> 60 %) cuts lows up to 180 Hz plus a 750 Hz peak for a tight, hot-rodded British crunch; low (< 40 %) drops the cut to 55 Hz and lets the lows saturate into a dense wall of sound.
- **lead** (crimson, modern high gain): a built-in tight boost (progressive first-order high-pass up to 180 Hz + upper-mid push at 1.8 kHz, Q 0.9, 3 to 9 dB, both on `tight`), then 5 stages with the gains of a real high-gain triode preamp (each 12AX7 stage gives 30-35 dB, partly lost in the divider to the next one): 64 dB across the stages at 30 %, 71 dB at 50 %, 88 dB at full, so it is a wall of gain already at 30 %; the 3rd is the classic "cold clipper" (strongly asymmetric clipping, preceded by a 220 Hz high-pass that removes intermodulation on low-tuned power chords). After the preamp, the lead voicing: low mids scooped at 420 Hz (−2.5 dB), full 6L6 lows. Its `resonance` is wider and deeper (up to +14 dB at 82 Hz, Q 1.3) and `presence` reaches +10 dB at 3.2 kHz, so the head can go from tight to very deep.

**Each head keeps its own controls**: `tight`, `gain`, `bass`, `mid`, `treble`, `presence`, `resonance` and `master` are remembered per channel while the plugin is open (gate, boost and `level` are shared). Each head starts from its own defaults (gain 10 % clean, 50 % crunch, 30 % lead; tight 0 % on all three); reloading the plugin starts from the defaults again. Double-click a channel to reset that head; double-click a value to return it to that head's default.

**Gain at 0 is silence**, like the gain pot of a real amp: over the first 10 % of its travel the signal fades in from nothing (an S curve), and from 10 % up the gain works on the stages in dB, like a logarithmic pot (every 10 % adds the same dB). At 0 the output is exact digital silence.

Switching channel (or oversampling) ramps down in about 7.5 ms, resets the inactive filter states, then ramps up again: no pops (tested).

## Voicing

- **Tone stacks with real component values**: crunch = the classic British high-gain stack (220k treble, 1M bass, 22k mid, 33k slope; 470p / 22n / 22n); lead = the classic American high-gain stack (250k / 250k / 25k / 47k; 250p / 47n / 22n). The clean is not a scooped stack but a passive network with full mids: it uses a dedicated EQ (bass shelf at 120 Hz, mid at 650 Hz, treble shelf at 2.2 kHz, nearly flat at noon with a slightly glassy top).
- **Bright cap on the gain pot** (crunch 470p, lead 250p): with the gain low the highs are lifted up to +9 dB; turning the gain up the effect fades, and at full gain it is gone.
- **Coupling capacitors between stages are first-order high-passes** (6 dB/octave, as in the circuit), not steeper filters.
- **Power tubes**: EL34 (clean and crunch) = a little upper-mid bite at 1.6 kHz and earlier breakup; 6L6 (lead) = firmer lows and 15 % more headroom.
- **Input anti-hiss** only where the gain needs it: 16 kHz on clean (full sparkle), 8 kHz on crunch, 6.5 kHz on lead.

Small-signal response at noon, relative to 1 kHz (tests):

| channel | 80 Hz | 150 | 300 | 500 | 700 | 2 k | 4 k | 8 k |
|---|---|---|---|---|---|---|---|---|
| clean | −2.5 | −0.5 | −0.3 | −1.4 | −1.0 | +2.2 | +0.9 | −6.1 |
| crunch | −10.4 | −1.3 | +0.3 | −1.8 | −2.4 | +6.2 | +4.8 | −7.7 |
| lead (boost, tight 50 %) | −17.7 | −5.6 | −2.8 | −4.1 | −3.6 | +10.2 | +6.6 | −10.1 |

Full mids on the clean, the British upper-mid push on the crunch, a tight low end and aggressive upper mids on the boosted lead.

## Gain stages

Each stage, at 2x oversampling (4x optional, `[ 2x | 4x ]`):
1. a state-variable high-pass that decides how much low end enters the distortion;
2. **grid conduction and blocking**: positive peaks above about 0.8 make the grid conduct; they are compressed softly (about 18 %) and charge the coupling capacitor, which shifts the operating point towards cutoff (1.5 ms attack, 30 ms recovery). As on a real preamp, a hard hit gets dirtier and more compressed for a moment, then opens up again;
3. an asymmetric waveshaper (softer positive half, harder negative half) with **first-order antiderivative anti-aliasing (ADAA)**, using a numerically stable log-cosh antiderivative and an analytic fallback when the input barely moves;
4. a **15 Hz DC blocker** on every stage (asymmetric clipping makes DC, which would otherwise push the next stage into blocking distortion);
5. a "Miller capacitance" low-pass whose cutoff falls from 10.5 kHz to 4.2 kHz as the stage clips harder (no digital fizz);
6. **phase inversion**: like every common-cathode triode, each stage inverts the signal, so each stage's asymmetry hits the opposite half-wave of the next one, as in a real cascade.

## Tone stack and power amp

- **Tone stack**: the real passive 3-band R-C circuit (closed-form transfer function, bilinear transform), with component values per channel; coefficients are only recomputed when a knob moves.
- **Channel volume** (fixed, before the master, like the channel volumes of a real multi-channel head): the clean channel has +10 dB so it plays only a little quieter than the lead (as a real high-headroom clean would) and breaks up the power amp only with the master high.
- **Cathode follower** (crunch, lead): after the last stage, as in the high-gain circuits that drive the tone stack, positive half-waves are softly compressed (with its own DC blocker), inside the oversampled section.
- **Master**, then **power amp sag**: an RMS envelope (10/150 ms) lowers the headroom of an ADAA soft clipper by up to 15 % on heavy chords (about 2 % on clean).
- **Push-pull imbalance**: the two halves of a real push-pull pair never match, so the power amp gets a slight offset (more on EL34 than 6L6) that adds a little 2nd harmonic as it saturates; the offset is removed again and a DC blocker follows.
- **Output transformer**: the lows (under ~110 Hz) saturate first at high volume, the bloom and low-end compression of a real power section.
- **Speaker interaction** without one-sample feedback loops: `resonance` = a 96 Hz bell (Q 2.4, up to +9 dB), `presence` = a high shelf at 2.8 kHz (up to +8 dB), then an 11.5 kHz safety low-pass so no ultrasonic hash reaches the cab IRs in b-blend.

## Noise: DI-keyed dual-stage gate

A real high-gain amp raises the DI noise by up to 60 dB. The whole system switches on and off with `noise gate` (on by default; off = no gate and no hum notch); `threshold` (−90…−20 dB) sets the gate:
1. **Input guard**: a 35 Hz Butterworth high-pass (DC and rumble) and, when `gate` is above −85 dB, two very narrow notches (Q 40) at 50 and 100 Hz against mains hum.
2. **One detector, two actions**: the detector reads only the clean DI (before the notches, which would ring), and drives
   - **A**, a VCA before the preamp, so noise never enters the gain stages;
   - **B**, a VCA plus a dynamic two-pole low-pass after the last stage and before the tone stack.
3. **No chatter**: 4 dB of hysteresis on a fast envelope with a 20 ms hold. While the fundamentals (80–600 Hz) dominate the hiss (> 2.5 kHz), long notes fade out with a slow 180 ms release.
4. **Palm-mute / stop clamp**: when the envelope collapses (more than 14 dB in about 8 ms) the gate closes at once with an 8 ms release, and reopens only on a new rising pick.
5. **Automatic threshold**: with a lot of gain on the audio interface the DI hiss can sit above `threshold` and keep the gate open; the amp then raises it by 30–50 dB and squashes it while you play, which sounds like a noise that ducks under the notes. The gate recognises the hiss (more energy above 2.5 kHz than between 80 and 600 Hz, the opposite of a guitar), measures its level on 50 ms blocks and keeps the working threshold 8 dB above it. It drops at once and rises by 0.5 dB every 50 ms, so a stray pick does not move it. `threshold` is still the minimum.
6. **Hiss under the tails**: the gate measures the noise floor of the hum-free DI by itself (in the pauses) and the post-preamp low-pass follows the signal-to-noise ratio: it stays at 12 kHz while the note is well above the noise and closes towards 3.5 kHz as a tail sinks into the hiss.
7. **Input anti-hiss**: a fixed two-pole 6.5 kHz low-pass on the DI before the gain stages. A pickup produces almost nothing up there, while 80 % of white DI noise lives there and would become the fizz under every tail.

## Boost

`boost` (row 1, after the gate) is one button that cycles `off` → `vol` → `vol + eq`. It sits after gate A, like a pedal in front of the amp, so it never raises the hiss in the pauses. Every change ramps in 30 ms.
- **vol**: +6 dB into the amp.
- **vol + eq**: +6 dB, a first-order 90 Hz high-pass (tighter lows in the preamp), +4.5 dB at 1.6 kHz (Q 0.8, where note definition and articulation live), −3 dB above 5.5 kHz (no extra fizz) and a very light ADAA drive (about 1 dB of peak compression) for a bit more air.

## Level

`level` (−24…+12 dB) and **`[ auto ]`**: 3 s of measurement that only counts the moments when the DI is above −55 dBFS (you are playing), so pauses never make it turn up the noise. Then it sets `level` so the output RMS equals the DI's.

## Tuner

The tuning-fork button opens a fullscreen tuner over the whole window. It reads the DI at the very first sample, low-passes it under 500 Hz, decimates by 8 and runs the **McLeod Pitch Method** (normalised square difference, first peak above 0.8 × the highest, parabolic interpolation), 27.5–1318 Hz. Big note name, Hz, a ±50 cent bar with a needle and a strobe, and a `[ mute ]` button, which is on every time the tuner opens (click it to hear the guitar). A click anywhere else closes it.

## Interface

Borderless: one continuous #0D0D0F surface, no boxes. At the top, the title (its "b" takes the colour of the active head), `channel`, `oversampling [ 2x | 4x ]` (2x is lighter on the CPU, 4x lowers aliasing further) and the tuner, all centred on the title's letters. In the centre, 50 % of the window wide, a minimal, fully symmetric vector front view of the active head: vertical slats for clean, a woven cloth with double piping for crunch, perforated metal for lead, the same simple handle on top and the same feet below on all three. It glows from inside in the channel colour as the stages saturate; a pilot light in the top-left corner shows the gate, and the channel name sits in the bottom-right corner, directly on the grille.

**Click the pilot light** and the head turns around (no animation): the back shows the chassis, the transformers and the tubes in real proportions (EL34 / 6L6GC about 32 × 110 mm with their octal base, ECC83 / 12AX7 about 22 × 56 mm), their filaments glowing harder as the amp saturates. Each head has its own back: clean a solid lower back panel, crunch a cage of vertical bars, lead alternate horizontal louvres. No text on the back. Click anywhere on the back, change channel or reopen the plugin to see the front again.

Below the head, three rows:
1. `noise gate` on/off and its `threshold`, then `boost`;
2. `gain`, `tight` | `bass`, `mid`, `treble` | `presence`, `resonance` (a faint vertical divider between the groups);
3. aligned right: `master`, `level` and `[ auto ]`.

At the bottom, the output level on the right (the auto status appears on the left while it measures).

Sliders have `< >` arrows and fixed-width boxes (sized on the longest value, e.g. "-100.0 dB", "100 %"). Every control takes the colour of the active channel.

## Measured (see `Tests/Tests.cpp`)

**Does it behave like a real amp?** A 220 Hz sine at −30 / −20 / −10 dBFS (soft, normal, hard picking), tone controls at noon, presence and resonance at 0:

| channel, gain | THD | H2 vs H3 | output | compression (dB lost over 20 dB of input) |
|---|---|---|---|---|
| clean 60 % | 0.4 / 0.4 / 1.7 % | +1.9 / +8.5 / −12.9 dB | −29 / −19 / −11 dBFS | 1.5 dB |
| crunch 60 % | 15.1 / 18.6 / 22.2 % | +3.5 / −4.3 / −4.7 dB | −9 / −8 / −8 dBFS | 18.7 dB |
| lead 60 % | 22.8 / 32.5 / 34.8 % | +2.5 / +15.7 / +12.1 dB | −7 / −7 / −8 dBFS | 20.8 dB |
| crunch 30 % | 4.2 / 11.3 / 16.7 % | +7.3 / +2.9 / −3.0 dB | −17 / −10 / −9 dBFS | 12.1 dB |

- The clean stays clean and, when it starts to break up, the 2nd harmonic leads (single-ended triode character).
- The crunch at 30 % cleans up when you roll back the guitar volume (THD from 17 % to 4 % with 20 dB less input), like a real non-master crunch; turned up it saturates fully.
- **How much gain** (palm mute on the low E at −50 and −20 dBFS, i.e. 30 dB of picking dynamics, and the harmonics of a note played softly at −40 dBFS): lead 30 % → 4.1 dB of output change, lead 50 % → 1.3 dB, lead 100 % → 0.1 dB (soft and hard palm mutes are equally saturated); crunch 50 % → 22.9 dB (still dynamic), crunch 100 % → 0.6 dB.
- The lead compresses almost completely (sustain).
- Aliasing at 2x, measured with a 3.1 kHz tone at −14 dBFS (the worst case for high gain) with the gate off: −74 dB under the harmonics on the clean, −54 to −60 dB on the crunch, −52 to −54 dB on the lead (lower at 4x).
- Sag on a held chord with the master at 90 %: the clean loses 1.6 dB after the attack; crunch and lead move by less than 0.5 dB, since they are already compressed by the preamp.

- Pluck: clean (gain 40 %) −14.0 dBFS rms (crest 10.9 dB), crunch (70 %) −4.5 (4.4 dB), lead (70 %) −2.3 (3.7 dB).
- Gate in the lead channel at gain 80 %, DI noise −70 dBFS: −37 dBFS out with the gate off, digital silence with `gate` −55 dB; a note passes untouched.
- After a note stops, the output is below −60 dBFS within 49 ms.
- **Noise study** (lead, gain 80 %, DI hiss at −90 / −80 / −70 dBFS + 50 Hz hum at −66 dBFS, default gate): pauses and the space between notes are digital silence (−140 dBFS or lower), no spurious gate openings, the noise floor is found by itself (−86 / −77 / −67 dBFS). Extra hiss above 5 kHz in a decaying tail compared with the same note without noise: +1.5 / +4.5 / +9.7 dB (it was +5.6 / +6.5 / +10.6 before the input anti-hiss filter).
- Tone stack: treble moves 5 kHz by 17.7 dB, bass moves 80 Hz by 11 dB; the impulse response decays (stable).
- Tuner: 82.41 Hz → −0.02 cents, 196 Hz → −0.16 cents, 440 Hz → −0.65 cents.
- `auto` level with pauses in the take: output minus input +0.09 dB.
- `noise gate` off: the amplified DI noise passes (−33.7 dBFS); on: digital silence.
- Automatic threshold: DI hiss at −55 dBFS (above the −60 dB threshold) in the lead: the note comes out at −3.2 dBFS, the pause at −136 dBFS. Before, the gate stayed open and the hiss came out at −19 dBFS.
- Gain 0 in the lead: exact digital silence; gain 10: −3.7 dBFS.
- The waveshapers' anti-aliasing (ADAA) is computed in double precision with the exact tanh in both branches.
- Switching clean → lead while playing: the largest sample-to-sample jump during the switch (0.016) stays below the steady lead sound's own edges (0.019): no click. The ramp holds silence until the next block, so samples computed with the old channel never come back.

- Boost: `vol` +6.0 dB; `vol + eq` against `vol`: 100 Hz −2.5 dB, 1.6 kHz +4.4 dB, 7 kHz −2.8 dB.
- Heads are independent: switching from crunch (gain changed to 77 %) to lead gives the lead default (30 %); back on the crunch, its 77 % returns.

## About panel

Click the b-amp logo for a short description of the plugin, signed b-suite (crafted in Italy, built with Claude). Click anywhere or press Esc to close.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, the plugin asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the other b-suite plugins and b-rig, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in the plugin's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. Inside b-rig the modules never check, because b-rig updates as one app.

## Build

Requires CMake ≥ 3.22, a C++20 compiler and JUCE 9. JUCE is expected in `../libs/JUCE` (shared with the other plugins in this repo); if it is missing it is fetched automatically. Theme, controls and fonts are shared from `../shared`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BAmpTests_artefacts/Release/BAmpTests
```

On macOS the AU/VST3 are copied to `~/Library/Audio/Plug-Ins` after the build.

## Credits

Tone stack equations after D. T. Yeh and J. O. Smith, DAFx 2006. Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com), which is dual-licensed: AGPLv3 or commercial. Choose this project's license accordingly before publishing.

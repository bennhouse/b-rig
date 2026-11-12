<p align="center"><img src="docs/b-suite-banner.svg" alt="b-suite" width="760"></p>

Plugins (VST3 / AU) and a standalone app for people who record electric guitar in a bedroom and want it to sound like it wasn't. From the raw DI to a mix that still holds up on a phone speaker.

Crafted in Italy with love 🇮🇹 · **Status:** beta, so expect the odd rough edge.

## How it was made

Full disclosure: I didn't write a single line of this C++ myself. The whole suite was written with Claude Code. The ideas, the ears and the stubbornness are mine.

Every plugin started as a small annoyance while recording: a DI that hisses, palm mutes that turn to mush, a cab that sounds like a cab in a box, a guitar that Instagram's compression flattens into a kazoo. So each one does one job, and shows you what it's doing.

No fake knobs, no rusty screws, no tubes glowing for decoration. Dark, flat, one colour per plugin and a live display. Click any logo to find out what that plugin is for.

## The plugins

### Before the amp

- **b-drop**: drop tuning without touching the tuning pegs, from -12 to +12 semitones. The pick attack goes through right away; the shifted note follows a few milliseconds later, so it still feels like playing.
- **b-focus**: makes the DI shut up between notes. Hiss and hum go, the notes stay. Hit `learn`, let it listen to your noise for a few seconds, done.
- **b-fs** (b-freshstrings): new strings without the trip to the shop, plus a different pick and another pickup position.
- **b-tight**: for when the low end gets out of hand. Pick your tuning and it sets the low cut, keeps palm mutes tight and sharpens the attack. With no tuning chosen it stays out of the way.

### Amp, cab and effects

- **b-amp**: an amp head with three channels: clean, crunch and a lead that goes all the way to djent. Noise gate and tuner built in. Click the pilot light to look at the back.
- **b-distort**: five distortions (tube, drive, rat, fuzz, fold) matched in loudness, so you compare the sound, not the volume.
- **b-wire**: follows your notes and plays them back as a raw synth one or two octaves down. A bass player you don't have to pay.
- **b-starve**: a circuit running out of power, then three ways to break the sound on purpose: freezes, bit errors and a melting filter.
- **b-blend**: up to four cab IRs, lined up and mixed on a pad. The built-in browser shows every IR in a folder as a curve, and you hear each one as you click it.

### After the cab

- **b-double**: one take becomes two guitars, wide in stereo and still fine in mono.
- **b-aftercab**: everything after the cab in one place: EQ, compressor, delay and reverb, and an output that keeps the level where it was.
- **b-dynaverb**: notices when you go for a solo on the high strings and only then adds a mid boost and a delay. Riffs stay dry.
- **b-space**: puts the amp in a real room. You pick the room, the walls and where the amp and you are standing, instead of turning reverb knobs.
- **b-delay**: five kinds of repeats (digital, tape, bucket brigade, lo-fi, reverse), tempo sync and tap.
- **b-verb**: room, plate, hall, spring and shimmer, plus freeze for endless pads.
- **b-scenery**: vinyl crackle, amp hum and rain, generated live, that duck under your playing so they never get in the way.

### On the master

- **b-adapt**: makes the mix survive phone speakers and social media compression. Its phone monitor lets you hear the worst case before your audience does (switch it off before you export).
- **b-meter**: the last slot. Spectrum, stereo and loudness, and one click to hit the level Instagram, YouTube or a streaming release expects.
- **b-synthesia**: visuals that move with the guitar, for playthrough videos. The audio isn't touched.

Every plugin folder has its own `README.md` with every control and the numbers measured in the tests.

## b-rig

The whole suite as one app: plug in the guitar and play, no DAW. Tuner, metronome, presets and undo. b-amp stays quiet until there's a cab after it, so a raw amp never screams into your headphones, and a limiter guards the output.

Installers for macOS (Apple Silicon) and Windows (x64 and ARM). More in `b-rig/README.md`.

## Suggested order

On the guitar track:

```text
DI → b-drop → b-focus → b-fs → b-tight → (b-wire / b-starve / b-distort)
   → b-amp → b-blend → b-double → b-aftercab → b-dynaverb
   → b-delay / b-verb → b-space → b-scenery
```

On the master: `b-adapt → b-meter`, with b-synthesia wherever you like.

Everything in brackets, and everything after b-aftercab, is optional. It's a suggestion, not a law.

## Build

JUCE 9 and C++20, built with CMake. JUCE is a submodule, so clone with `--recursive` (or run `git submodule update --init`).

```bash
cd b-aftercab        # or any plugin folder, or b-rig
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On macOS the plugins are copied to `~/Library/Audio/Plug-Ins` after the build and show up in Logic Pro under "b-suite". Each folder also builds a test program that measures the sound and saves screenshots of the interface.

## Releases and updates

- New b-rig version: `tools/release.sh b-rig 0.2.0`. It sets the version, tags it and pushes; GitHub builds the installers and publishes the release.
- b-rig and the plugins check for a new version once a day. If there is one, a small notice shows up in the corner; click it and the about panel opens with a download button.

## Logo

`docs/` has the logo in three sizes: the banner above, a full-screen version (1920 × 1080) and a square icon (1024 × 1024). One square per plugin, in the order of the chain.

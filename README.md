<p align="center"><img src="docs/b-suite-banner.svg" alt="b-suite" width="760"></p>

Free and open-source plugins (VST3 / AU) and a standalone app for recording electric guitar: from the raw DI to a mix that still holds up on a phone speaker.

Use them, study them, change them, ship your own versions. Everything is here: the DSP, the interfaces, the shared design system, the tests and the installer scripts.

**License:** [GNU AGPLv3](LICENSE) · **Status:** beta

## What you get

- 19 plugins and a standalone app that runs them without a DAW.
- Each plugin does one job and shows what it is doing on a live display. No fake knobs or screws: dark, flat, one colour per plugin.
- Readable code: one folder per plugin, a short `README.md` with every control and the numbers measured by its tests.
- A shared design system in `shared/smp` (fonts, colours, controls, window layout), so a new plugin looks like the rest from the first build.

The code was written with Claude Code, and so can your changes: the repository is laid out so that a person or an AI assistant can find their way around quickly.

<p align="center"><img src="docs/b-suite-showcase-all.png" alt="All the b-suite plugins" width="100%"></p>

## The plugins

### Before the amp

- **b-drop**: tunes the guitar down or up while you play, from -12 to +12 semitones. The pick attack goes through right away; the shifted note follows a few milliseconds later, so it still feels like playing.
- **b-focus**: removes hiss and hum from the DI between notes and leaves the notes alone. `learn` listens to your noise for a few seconds and sets itself.
- **b-fs** (b-freshstrings): the sound of new strings, a different pick and another pickup position.
- **b-tight**: tightens the low end before the amp. Pick your tuning and it sets the low cut, keeps palm mutes tight and sharpens the attack. With no tuning chosen it stays out of the way.

### Amp, cab and effects

- **b-amp**: an amp head with three channels: clean, crunch and a lead that goes all the way to djent. Noise gate and tuner built in. Click the pilot light to look at the back.
- **b-distort**: five distortions (tube, drive, rat, fuzz, fold) matched in loudness, so you compare the sound, not the volume.
- **b-wire**: follows your notes and plays them back as a raw synth one or two octaves down.
- **b-starve**: a circuit running out of power, then three glitch effects: freezes, bit errors and a melting filter.
- **b-blend**: up to four cab IRs, lined up and mixed on a pad. The built-in browser shows every IR in a folder as a curve, and you hear each one as you click it.

### After the cab

- **b-double**: turns one take into two guitars, wide in stereo and still fine in mono.
- **b-aftercab**: everything after the cab in one place: EQ, compressor, delay and reverb, and an output that keeps the level where it was.
- **b-dynaverb**: notices when you play a lead line on the high strings and only then adds a mid boost and a delay. Riffs stay dry.
- **b-space**: puts the amp in a real room. You pick the room, the walls and where the amp and you are standing, instead of turning reverb knobs.
- **b-delay**: five kinds of repeats (digital, tape, bucket brigade, lo-fi, reverse), tempo sync and tap.
- **b-verb**: room, plate, hall, spring and shimmer, with freeze.
- **b-scenery**: vinyl crackle, amp hum and rain, generated live, that duck under your playing so they never get in the way.

### On the master

- **b-adapt**: makes the mix survive phone speakers and social media compression. A phone monitor lets you hear how it will sound there (switch it off before you export).
- **b-meter**: the last slot. Spectrum, stereo and loudness, and one click to hit the level Instagram, YouTube or a streaming release expects.
- **b-synthesia**: visuals that move with the guitar, for playthrough videos. The audio isn't touched.

Every plugin folder has its own `README.md` with every control and the numbers measured in the tests.

## b-rig

The whole suite as one app: plug in the guitar and play, no DAW. Tuner, metronome, presets and undo. b-amp stays quiet until there's a cab after it, and a limiter protects the output.

Installers for macOS (Apple Silicon) and Windows (x64 and ARM). More in `b-rig/README.md`.

## Suggested order

On the guitar track:

```text
DI → b-drop → b-focus → b-fs → b-tight → (b-wire / b-starve / b-distort)
   → b-amp → b-blend → b-double → b-aftercab → b-dynaverb
   → b-delay / b-verb → b-space → b-scenery
```

On the master: `b-adapt → b-meter`, with b-synthesia wherever you like.

Everything in brackets, and everything after b-aftercab, is optional.

## Build

JUCE 9 and C++20, built with CMake. JUCE is a submodule, so clone with `--recursive` (or run `git submodule update --init`).

```bash
cd b-aftercab        # or any plugin folder, or b-rig
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On macOS the plugins are copied to `~/Library/Audio/Plug-Ins` after the build and show up in Logic Pro under "b-suite". Each folder also builds a test program that measures the sound and saves screenshots of the interface.

## Make it yours

- **Change a plugin:** edit its `Source/`, rebuild, run its test program. The tests measure the sound (levels, timing, decay, CPU) and save screenshots of the interface to `build/`.
- **Make a new plugin:** copy a small one (b-tight, b-verb) and rename it. `shared/smp/Shell.h` gives you the title, the frame, the value boxes and the about panel; `shared/smp/Filters.h` the common filters.
- **Put it in b-rig:** add `b-rig/Source/Modules/Mod_<name>.cpp` like the others and an entry in `Modules.cpp`. b-rig builds every module from the plugin's own sources, so a plugin change reaches the app at the next build.
- **Contribute back:** issues and pull requests are welcome. Keep one change per pull request and run the tests of what you touched.
- **On Windows:** [docs/WINDOWS.md](docs/WINDOWS.md) lists the tools to install and how to build, test and make the Windows installers.

## License

b-suite is free software under the [GNU Affero General Public License v3](LICENSE): you can use it, modify it and redistribute it, also commercially, as long as what you distribute stays under the same license with its source available. JUCE is used under its AGPLv3 option. The IBM Plex fonts are under the SIL Open Font License (`shared/Resources/OFL-IBMPlex.txt`).

## Releases and updates

- Two products, each with its own tag and installers on the GitHub releases page (pre-releases during the beta):
  - **b-suite** (`b-suite-v0.2.0`): every plugin in one installer, all with the same version. macOS: a universal `.pkg` with Audio Unit and VST3, where "Customise" lets you leave plugins out. Windows x64 and ARM64: a VST3 setup with one component per plugin.
  - **b-rig** (`b-rig-v0.2.0`): the standalone app, for macOS (Apple Silicon) and Windows (x64 and ARM64).
- To publish, on a Mac: `tools/release.sh b-suite 0.2.0` or `tools/release.sh b-rig 0.2.0`. It sets the version, builds and tests everything locally, makes the macOS installer, then tags, pushes and creates the release with it. The Windows installers are added to the same release from a Windows PC with `tools/release-windows.ps1` (see [docs/WINDOWS.md](docs/WINDOWS.md)), or by the GitHub Action of the tag once the repository variable `WINDOWS_CI` is `true`.
- To build every plugin at once locally: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --target bsuite_plugins`.
- b-rig checks for new b-rig releases, the plugins for new b-suite releases, once a day. If there is one, a small notice shows up in the corner; click it and the about panel opens with a download button. The check is an anonymous request to the GitHub releases.

## Logo and images

`docs/` has the logo in three sizes (the banner above, a full-screen version at 1920 × 1080 and a square icon at 1024 × 1024, one square per plugin in the order of the chain) and two showcases at 3840 × 2160: every plugin around the b-suite logo (`b-suite-showcase.png`, good as the repository's social preview) and every plugin on its own (`b-suite-showcase-all.png`). All of them are generated: the logos by `BSuiteLogo` in b-rig, the showcases by `tools/make-showcase.sh` from the screenshots the tests save. Use them freely.

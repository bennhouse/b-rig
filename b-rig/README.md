# b-rig

Standalone app with the b-suite's DSP engines and interfaces compiled inside. It does not scan or load external VST3/AU plugins.

**Platforms:** macOS (universal); Windows from the same CMake project (not tested yet) · **Latency:** the audio interface buffer only (the chain adds 0 samples) · **Size:** about 22 MB

## Modules

Each module is compiled from its plugin folder (`../b-amp/Source`, `../b-blend/Source`...) inside its own C++ namespace (`Source/Modules/Mod_*.cpp`), with its accent colour and its own copy of the shared design system. A change to a plugin reaches b-rig at the next build.

Modules: b-drop, b-focus, b-fs (b-freshstrings), b-tight, b-wire, b-starve, b-distort, b-amp, b-blend, b-aftercab, b-dynaverb, b-delay, b-verb, b-space, b-adapt, b-meter. Each module can be in the chain only once: while it is there its `+` in the library is dimmed, and hovering the row shows `in chain`. In the library each category lists its modules in chain order (b-distort right before b-amp). b-double, b-scenery and b-synthesia are plugin-only.

Inside b-rig the plugins hide their own title, about panel and the tools the app already has (b-amp's tuner), and the controls of their top row move to the centre to fill the space left by the title. The switch is `ui::kInRig` in `shared/smp/Theme.h`, true when a plugin is compiled with `SMP_IN_RIG=1`: new plugins check it for anything that should not appear in the app.

## Safety

- **b-amp starts muted** in the factory chain and when added from the library. Its card shows `SAFE MUTED - Click to Arm`; arming it fades it in over 80 ms. From a preset it starts already on only if it was on when saved and a b-blend with an IR loaded comes after it (the cab is there); otherwise it starts muted. As soon as that b-blend is removed or moved before the amp, the amp mutes again and has to be re-armed. When an IR that works is loaded into a b-blend after a muted amp, the amp arms by itself (fade-in of 80 ms).
- **b-blend without an IR mutes the output** (it stays on and says so), so an amp never reaches the speakers without a cab.
- **b-blend after an amp cannot be switched off.** When an amp is anywhere before it, b-blend stays on (card: `on - locked`), with or without an IR: without the cab the amp's output is harsh and loud. It can be moved, and removed after a confirmation that opens inside the chain box (`remove b-blend?` with `remove` and `cancel`; Return confirms, Esc cancels).
- **Limiter** on the output at -0.5 dBFS: instant attack (no latency), 80 ms release, then a clip at the same ceiling. NaN or infinity from a module becomes silence.
- **Switching modules:** a module that is off is skipped (its `processBlock()` is not called). On/off crossfades over 10 ms (cosine), without clicks.
- **No allocation on the audio thread:** modules are created and prepared on the message thread, then the new chain is published through an atomic pointer. Old chains and removed modules are freed after the audio thread has left them.
- **Mono and stereo:** the DI enters in mono; mono modules (amp, tight, wire) get the L/R sum, b-blend makes it stereo, the output is always stereo.

## Interface

Window 1180 × 760 px, resizable. The macOS title bar has the app's colour and no title. The app icon is the standard macOS shape in the app's grey with the coloured "b" of the logo (`Resources/AppIcon.png`, made by `Tools/MakeIcon.cpp`). Drop-down menus use the suite's font, colours and square corners, inside the modules too.

**Top bar**
- Left: the b-rig logo (about panel); between it and `presets`, undo and redo arrows (also cmd+Z and cmd+shift+Z, ctrl on Windows). They step through every change to the rig: modules added, removed or moved, on/off, and every module's controls (one step per gesture, up to 60).
- Centre: `presets`, the current preset name (`unsaved` after a change) and `save` (always there, dimmed until you change something), then the metronome: a metronome symbol (on/off; the arm swings with the beat), `< 120 bpm >`, the time signature menu (2/4, 3/4, 4/4, 5/4, 6/8, 7/8) and `tap`. With the metronome off, time signature and tap are dimmed. Right-click the symbol for sound (beep, hat, wood) and a volume slider (50 % at first). On/off, sound, volume, tempo and time signature are kept between sessions, whatever preset is loaded. The click is completely separate from the guitar chain: it is added after the guitar's limiter, so a loud guitar never squashes it and the modules never see it. The metronome tempo is also the tempo the modules see, so synced delays follow it.
- Right: `setup` opens the audio settings screen: output and input device, the input the guitar is on, sample rate, buffer size (with latency in ms; 64 samples on first launch and whenever you pick another device), and two meters kept clearly apart: `level in` (the guitar DI, grey) and `level out` (after the chain, in the app's colour), each with its peak in dB and a clip LED. The settings are kept between sessions.

Setup and presets are full-window screens; close them with the `x` level with the title, or Esc.

**Left column**
- Tuner, the same height as the chain bar. Off by default (a tuning fork, `tuner` and `press to activate`, centred); it does no work while off. Click it to turn it on: the output mutes (`mute` under the note unmutes; your choice is kept, also between sessions). Click it again or the `x` to turn it off. On the left the note, on the right the strobe bar (−50…+50 cents, marks at 0, ±10, ±25) with cents and Hz under it; green within ±3 cents, amber otherwise. It reads the clean DI before the chain (low-passed at 1 kHz, decimated 4x, 128 ms window, b-amp's McLeod pitch detector). It responds from −56 dBFS and keeps 10 dB above the DI hiss it measures by itself, so soft picking works and background noise shows no note.
- Module library by category (`PRE / DI`, `CORE / AMP`, `POST / SPACE`, `MASTER / UTIL`). A click on `+` adds the module at its place in the chain; drag the row or the `+` onto the chain to drop it where you want (holding the button alone adds nothing). `?` appears on hover and shows what the module does, its controls and where it goes.

**Chain**
`DI IN` to `STEREO OUT`, one card per module: the coloured square switches it on or off, a click opens it below, drag the card to move it (a coloured line shows where it lands, the chain scrolls near the edges), `< >` move it by one, `x` removes it. Long chains scroll with the wheel or trackpad.

**Workspace**
The selected module's interface, centred, at its own size (scaled down only if the window is too small). Top right: `ON / OFF` (`ARM` for a muted b-amp), the same switch as the card's square, and the audio thread's CPU load.

## Presets

Stored in `~/Library/Application Support/b-suite/b-rig/Presets` (`.brig` files: chain order, on/off and every module's settings; `index.xml` keeps each preset's creation time and colour).
- Any change you make (a module's control, the chain, on/off) switches the top bar to `unsaved` and lights `save`. `save` creates `User 1`, `User 2`... (the first free number), opens the presets screen and selects the new name so you can type over it.
- The list shows `clean slate` on top (white, an empty chain), then the presets from the newest to the oldest. Each preset gets a random colour, visible only on this screen, never the same as the two presets around it. `load at start-up` (above the list) chooses the preset the app opens with, `clean slate` included; that row shows `loads at start-up`. Until you choose one it is `default`, whose factory chain (`b-drop (off) -> b-amp (off) -> b-blend (on) -> b-aftercab (on)`) is only what it holds before you save over it. The start-up choice follows a rename; if you delete that preset the app starts from `clean slate` (undo brings it back).
- One click selects a preset; a double click (or Return) loads it and closes the screen.
- Buttons above the list: `+` (new preset from the current rig), `save` (overwrite the loaded preset, active after a change), `save as new` (copy of the selected preset), `import`, `export` (the selected preset), `export all` (every preset into a folder).
- Hovering any saved preset (`default` too) shows a pencil (rename: the name field opens on the right half of the row) and an `x` (delete). After a delete, `deleted` shows for 3 seconds.
- `undo` appears at the bottom after the first action and stays until you close the screen: like Cmd+Z (which works too), it steps back through everything done there (delete, rename, new, save, load, import).

## Measured (see `Tests/Tests.cpp`)

- One module per type: a second click on `+` adds nothing. Undo brings back a removed module, redo removes it again.
- The metronome click is the same with a guitar driving the limiter as alone (0.234 both).
- b-blend after an amp: locked on with or without an IR; loading an IR arms the muted amp; the `x` asks for confirmation and removes b-blend only after it.
- Tuner: once the user switches the mute off, the tuner starts unmuted next time.
- Factory chain and states as above (b-drop is a real module, off); a muted b-amp is never processed; b-amp added from the library starts muted, and from a preset too unless a b-blend with an IR follows it; b-distort added from the library lands right before b-amp.
- A preset with b-amp on and a b-blend holding an IR after it loads with the amp on; removing that b-blend, or moving it before the amp, mutes the amp again; moving another module does not.
- `load at start-up`: the chosen preset loads at start-up; renamed, the choice follows it; deleted, the app starts from `clean slate`; undo restores it.
- b-blend after b-amp does not switch off; placed before the amp, it does.
- b-distort on/off on a 220 Hz sine: the largest step between samples during the crossfade is 0.99–1.01× the steady-state one. Once faded out, the module is not processed.
- Arming b-amp: about 2 % of the change in the first 10 ms, fully on after 80 ms.
- Limiter: a +12 dBFS sine comes out at -0.5 dBFS peak; a -12 dBFS sine is untouched; NaN in, silence out.
- Metronome: one click every 24 000 samples at 120 bpm / 48 kHz, one per eighth note in 6/8; beep, click and wood all play; volume 25 % = 0.25× the level.
- Tuner: no work while off; A2 + 1.9 cents read as A2, +1.9 cents; noise at -46 dBFS shows no note. Turning it on mutes the output, turning it off unmutes.
- The 16 modules run inside b-rig (editors from 640 × 390 to 1000 × 600) and each interface reopens every time you return to it.
- Dragging a card moves it; a rig written to a `.brig` file loads back the same.
- A change made by the user switches to `unsaved`; `save` writes `User 1` and opens its name for editing; renaming moves the file.
- Presets: `clean slate` on top and white, then newest first; no colour repeats within two rows; one click only selects; `clean slate` empties the chain; undo brings back the previous rig and a deleted preset; `save as new` copies the selected preset to the top.
- Tuner: a note played softly (−44 dBFS) is recognised within 200 ms at 82 and 659 Hz.
- Metronome: beep, hat and wood play; starts at 50 % volume.
- Audio-thread CPU, factory chain with b-amp armed: about 2 % of one core.

## Updates

The about panel shows the version (the whole b-suite is in beta). Once a day at most, while the window is open, b-rig asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the b-suite plugins, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in b-rig's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. The modules inside b-rig never check on their own: b-rig updates as one app.

## Build

CMake ≥ 3.22, a C++20 compiler and JUCE 9 (`../libs/JUCE`, downloaded if missing). The plugin folders must sit next to `b-rig`, as in this repo.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BRigTests_artefacts/Release/BRigTests
open build/BRig_artefacts/Release/b-rig.app
```

On macOS the build signs the app (ad hoc, with its Info.plist), so the system remembers the microphone permission. A new build counts as a new app and asks once more.

### Installers and releases

The repository's GitHub Actions workflow (`.github/workflows/b-rig.yml`) builds three installers: macOS Apple Silicon (`.dmg`, the app plus an Applications link), Windows x64 and Windows ARM64 (Inno Setup, `b-rig/Installer/b-rig.iss`). It runs by hand (Actions > b-rig > Run workflow, installers attached to the run) or when a tag like `b-rig-v0.2.0` is pushed, which also creates a release with the three files; the version comes from the tag. It does not run on every push, to save macOS minutes. Tests run on macOS and Windows x64.

The macOS app is signed ad hoc, not notarized: the first time, open it from System Settings > Privacy & Security > "Open Anyway". On Windows the installers are unsigned, so SmartScreen asks for confirmation. The Windows build uses the static runtime, so no Visual C++ redistributable is needed.

The b-suite logo (`docs/`: banner for the README, full screen 1920 × 1080 and square icon 1024 × 1024, one square per plugin in its colour, in chain order) is generated by `BSuiteLogo`: `./build/BSuiteLogo_artefacts/Release/BSuiteLogo ../docs`.

To add a plugin as a module: create `Source/Modules/Mod_<name>.cpp` like the others and give it an entry in `Source/Modules/Modules.cpp`.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

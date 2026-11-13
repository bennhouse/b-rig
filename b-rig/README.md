# b-rig

Standalone app with the b-suite's DSP engines and interfaces compiled inside, also available as an AU and VST3 plugin for your DAW. It does not scan or load external VST3/AU plugins.

**Formats:** standalone app, AU, VST3 · **Platforms:** macOS (universal); Windows from the same CMake project (not tested yet) · **Latency:** the app adds nothing to the audio interface buffer; the plugin reports the latency of the modules that are on (for example b-amp's oversampling) so the DAW can compensate it · **Size:** about 22 MB

## Modules

Each module is compiled from its plugin folder (`../b-amp/Source`, `../b-blend/Source`...) inside its own C++ namespace (`Source/Modules/Mod_*.cpp`), with its accent colour and its own copy of the shared design system. A change to a plugin reaches b-rig at the next build.

Modules: b-drop, b-focus, b-fs (b-freshstrings), b-comp, b-wire, b-starve, b-distort, b-amp, b-blend, b-aftercab, b-dynaverb, b-delay, b-verb, b-space, b-adapt, b-meter. Each module can be in the chain only once (once per lane when the chain is split): while it is there its `+` in the library turns into a dark `-`, which lights up on hover and removes the module; hovering the row shows `in chain`. In the library each category lists its modules in chain order (b-distort right before b-amp). b-double, b-scenery and b-synthesia are plugin-only.

Inside b-rig the plugins hide their own title, about panel and the tools the app already has (b-amp's tuner), and the controls of their top row move to the centre to fill the space left by the title. The switch is `ui::kInRig` in `shared/smp/Theme.h`, true when a plugin is compiled with `SMP_IN_RIG=1`: new plugins check it for anything that should not appear in the app.

## b-rig as a plugin

The same rig inside a DAW (`Source/Plugin/RigPlugin.cpp`, target `BRigPlugin`): the chain, the split, the presets and the module interfaces are those of the app.
- **Mono in, stereo out**, always. On a stereo track (some DAWs only have those) the two input channels are summed to mono.
- **Presets:** the same folder as the app, so a preset saved in one shows up in the other. Each plugin instance keeps its chain, the preset it came from and whether it was changed in the DAW project.
- **A new instance starts where the last one stopped:** the plugin keeps its last state (chain, controls, the preset it came from and its unsaved changes) in `last-plugin-session.brig`, next to the Presets folder. It is rewritten only when something changes, and no preset is ever created by it. A new instance opens that state; if the app has a preset chosen with `load at start-up`, that preset instead; the first time, `clean slate`. A DAW project reopened and closed without changes does not overwrite it.
- **Tempo:** b-rig has no metronome inside a DAW: the metronome, the tempo box, `tap` and `setup` disappear from the top bar. Modules with a tempo setting keep their own choice, as in the single plugins: their own time and tap, or sync to the project tempo.
- No first-run window, no keyboard shortcuts (the keys stay with the DAW). The tuner works as in the app.
- Tested with `auval` (Audio Unit validation, mono to stereo render included).

## Cab IRs

b-rig does not include any cabinet impulse response. b-amp needs b-blend after it with your own IR files (`.wav`): click `load A` in b-blend and choose the folder with your IRs. Until b-blend follows it, b-amp stays muted; b-blend without an IR plays nothing, so you hear the amp once an IR is loaded. The first time the app opens, a window says this; it does not appear again.

## Safety

- **b-amp is muted until b-blend follows it.** Added with no b-blend after it, its card shows `SAFE MUTED - Click to Arm`. As soon as a b-blend is after it (added, dragged there, or already there), the amp arms by itself with a fade-in of 80 ms, with or without an IR: b-blend without an IR blocks the sound on its own. The same from a preset. As soon as that b-blend is removed or moved before the amp, the amp mutes again.
- **`SAFE MUTED` only means the protection.** If you switch b-amp off yourself it is simply off, like any other module (`off` on the card, `OFF` in the workspace), and loading an IR does not switch it back on.
- **b-blend without an IR mutes the output** (it stays on and says so), so an amp never reaches the speakers without a cab.
- **b-blend after an amp cannot be switched off.** When an amp is anywhere before it, b-blend stays on (card: `on - locked`), with or without an IR: without the cab the amp's output is harsh and loud. It can be moved, and removed after a confirmation that opens inside the chain box (`remove b-blend?` with `remove` and `cancel`; Return confirms, Esc cancels).
- **Limiter** on the output at -0.5 dBFS: instant attack (no latency), 80 ms release, then a clip at the same ceiling. NaN or infinity from a module becomes silence.
- **Switching modules:** a module that is off is skipped (its `processBlock()` is not called). On/off crossfades over 10 ms (cosine), without clicks.
- **No allocation on the audio thread:** modules are created and prepared on the message thread, then the new chain is published through an atomic pointer. Old chains and removed modules are freed after the audio thread has left them.
- **Mono and stereo:** the DI enters in mono; mono modules (amp, wire) get the L/R sum, b-blend makes it stereo, b-aftercab works mono → stereo (so its `spread` works), the output is always stereo.

## Interface

Window 1180 × 760 px, resizable. The background is #0D0D0F everywhere, the same as inside the modules, and every module has its colour from one palette (`shared/smp/Palette.h`), grouped by role: cold and precise before the amp, warm around the amp, air and water after the cab, neon on the master. The macOS title bar has the app's colour and no title. The app icon is the standard macOS shape in the app's grey with the coloured "b" of the logo (`Resources/AppIcon.png`, made by `Tools/MakeIcon.cpp`). Drop-down menus use the suite's font, colours and square corners, inside the modules too.

**Top bar**
- Four groups at the same distance from each other, with twice that distance after the logo: `undo` `redo` | `presets`, the preset name, `save`, `new` | the metronome | `setup`. In the plugin only the first two.
- Left: the b-rig logo (about panel), then the `undo` and `redo` buttons, drawn like the buttons of the presets screen (also cmd+Z and cmd+shift+Z, ctrl on Windows). They step through every change to the rig: modules added, removed or moved, on/off, and every module's controls (one step per gesture, up to 60).
- Centre: `presets`, the current preset name (yellow after a change: you are still on that preset), `save` and `new` (both dimmed until you change something), then the metronome: a metronome symbol (on/off; the arm swings with the beat), `< 120 bpm >`, the time signature menu (2/4, 3/4, 4/4, 5/4, 6/8, 7/8) and `tap`. With the metronome off, time signature and tap are dimmed. Right-click the symbol for sound (beep, hat, wood), volume (50 % by default; up to 50 % it scales as before, above it rises up to +14 dB at 100 % with a click up to three times longer: about 16 dB louder than at 50 %, still under 0 dBFS) and pitch (−12…+12 semitones: beep and wood change note, hat gets darker or brighter). The pitch is read at the start of each click, so changing it never cuts a click in half, and the click is added to the guitar through a soft limit above 0.85, so a loud click on a loud guitar never crackles. On/off, sound, volume, pitch, tempo and time signature are kept between sessions, whatever preset is loaded. The click is completely separate from the guitar chain: it is added after the guitar's limiter, so a loud guitar never squashes it and the modules never see it. The metronome tempo is also the tempo the modules see, so synced delays follow it. `tap` sets the tempo from the fourth tap (the average of the last taps; a pause longer than 2 s starts again).
- Right: `setup` opens the audio settings screen: output and input device, the input the guitar is on, sample rate, buffer size (with latency in ms; 64 samples on first launch and whenever you pick another device), and two meters kept clearly apart: `level in` (the guitar DI, grey) and `level out` (after the chain, in the app's colour), each with its peak in dB and a clip LED. The settings are kept between sessions.

Setup and presets are full-window screens; close them with the `x` level with the title, or Esc.

**Keyboard (app)**
- `m` or `k`: metronome on/off.
- `l`: tap tempo, like the `tap` button (the tempo changes from the fourth tap).
- cmd+Z / cmd+shift+Z: undo / redo.

**Left column**
- Tuner, the same height as the chain bar. Off by default (a tuning fork, `tuner` and `press to activate`, centred); it does no work while off. Click it to turn it on: the output mutes (`mute` under the note unmutes; your choice is kept, also between sessions). Click it again or the `x` to turn it off. On the left the note, on the right the strobe bar (−50…+50 cents, marks at 0, ±10, ±25) with cents and Hz under it; green within ±3 cents, amber otherwise. It reads the clean DI before the chain (low-passed at 1 kHz, decimated 4x, 128 ms window, b-amp's McLeod pitch detector). It responds from −56 dBFS and keeps 10 dB above the DI hiss it measures by itself, so soft picking works and background noise shows no note.
- Module library by category (`PRE / DI`, `CORE / AMP`, `POST / SPACE`, `MASTER / UTIL`). A click on `+` adds the module at its place in the chain; drag the row or the `+` onto the chain to drop it where you want (holding the button alone adds nothing). Hovering the `+` shows in the chain where the module would go (a light box and an insertion line) and the line above the workspace says it (`click to add b-verb: slot 4 of 4, lane b`). After the click the new card lights up in the module's colour for a moment, the chain scrolls to it and the same line says where it went. `?` appears on hover and shows what the module does, its controls and where it goes.

**Chain**
From the DI (left) to `STEREO OUT`, one card per module: the coloured square switches it on or off, a click opens it below, drag the card to move it (a line shows where it lands, the chain scrolls near the edges), `x` removes it. Long chains scroll with the wheel or trackpad. A card cut by the edge of the chain while it scrolls keeps its outline closed on all four sides, on the visible part; the scroll bar is a thin line under the chain, shorter than it.

**Split**
The square button at the left of the chain shows a dash: the chain is a single lane, and the signal line runs straight through the cards. Click it and the dash becomes the split symbol: the guitar now goes through two parallel lanes, a and b, and at the end they are added together.
- Splitting starts from the DI: everything goes into lane A and lane B starts as a copy of it, so the sound does not change until you change one of them.
- The `a` and `b` buttons next to it choose the lane you see. The chain bar keeps its size: it shows the modules before the split, then the modules of that lane. The signal line never breaks: it runs through the shared modules at the centre, rises (lane a) or drops (lane b) in the space after them, runs through the lane's modules above or below their centre and ends with the lane's letter.
- The split point (where the line rises or drops) can be dragged. Moving it to the right puts the first modules of the lane into the shared part (the same module in the other lane is removed); moving it to the left moves shared modules into the lane you see, with a copy in the other one, so the sound stays the same.
- **Expand:** the button after `a` and `b` (an arrow pointing down) doubles the height of the chain downwards until you press it again (the arrow then points up). Lane a stays where it is, lane b appears under it with its cards at the same height as those of a, and the signal line forks at the split point to both. The fork is one diagonal to each lane. The expanded chain covers the top of the open module, which stays still and dark underneath; the top bar, the library and the `stereo` and `balance` buttons stay as they are. The line above the workspace moves under the chain. A click on the dark module, or merging the lanes, closes it.
- **Dragging with the split on:** the card you drag floats above the whole window. A light box shows where it lands, around the shared part (before the split: both lanes play it) or around a lane, and the line above the workspace says it (`into the shared part`, `into lane a`). To move a module to the other lane, expand the chain and drop it on the other row; with the chain not expanded the line reminds you (`expand the chain to reach lane b`). If the module cannot go there the box turns dark grey, no insertion line appears and the line says why (for example `b-distort is in lane a: it cannot be in the shared part too`). After a drop the view follows the module.
- **Copy to the other lane:** hovering a card in a lane shows a copy button left of its `x`. It puts the same module, with the same settings, in the other lane at its place in the chain. It does not appear on modules before the split, when the other lane already has that module, or without the split.
- Each lane can hold each module once; a module before the split is shared and cannot also be in a lane. The library follows the lane on screen: switching lane, a module that is only in the other lane is free again (`+`), one in the shared part stays `-`. Adding from the library goes into the lane you see (or before the split, if that is its place and the other lane does not have it).
- On the right of the chain, two buttons one above the other:
  - `stereo` / `mono` (it always looks on: it says how the lanes come out). `stereo`, the default when you split: lane a on the left, lane b on the right, and widen works by itself: the first time you play, b-rig records both lanes for about a second and delays lane b by the time between 5 and 25 ms at which the two are most alike (the highest correlation), so summed to mono (a phone, a club PA) they reinforce each other instead of cancelling. With two equal lanes and a 110 Hz note it picks 18.19 ms, two periods of the note, and the mono sum keeps the level of a single chain (tested). `mono`: the lanes are summed at -6 dB each, with no widen delay.
  - `balance`, in stereo and in mono: click it and play for a second. b-rig lines up the phase of the two lanes (a delay of up to 5 ms on the lane that is ahead, and the polarity of lane b if it is inverted) and evens out their volume. The values stay fixed until you click it again (off) and once more (a new measurement); the line above the workspace shows them. While it measures the button works as a progress bar: outline and light text as usual, filling with the app's colour from left to right as the recording advances. Tested: lane b arriving 1 ms late, inverted and 6 dB quieter is lined up (a delayed by 1 ms), flipped back and brought to the same level.
- Safety follows the lanes: an amp in a lane needs a cab after it in the same lane; an amp before the split needs a cab in both lanes.
- Click the button again to merge: the lane you see stays, the other one is removed (with a confirmation if it has modules). Undo, redo and presets keep the split.

**Workspace**
The selected module's interface, centred, at its own size (scaled down only if the window is too small), with no outline around it and on the same background as the app, so the module and b-rig read as one interface. Top right: `ON / OFF` (`ARM` for a muted b-amp), the same switch as the card's square, and the audio thread's CPU load.

## Presets

Stored in `~/Library/Application Support/b-suite/b-rig/Presets` (`.brig` files: chain order, lanes, on/off and every module's settings; `index.xml` keeps each preset's creation time and colour). The folder is outside the app, so updating, reinstalling or removing b-rig does not touch it. Every preset is written to a temporary file first and then swapped in, so a crash or a power cut never leaves half a preset. A deleted preset goes to the Trash.
- Loading a preset and changing it keeps you on that preset: its name in the top bar turns yellow and `save` and `new` light up. `save` updates the loaded preset (from `clean slate`, which cannot be overwritten, it makes a new one). `new` creates `User 1`, `User 2`... (the first free number) from the current rig, opens the presets screen and selects the new name so you can type over it. While the loaded preset is unchanged, both are dimmed, `clean slate` included.
- **The app reopens where you left it:** at closing it keeps the rig as it is, unsaved changes included, and the preset it came from (`last-session.brig`, next to the Presets folder). The first time it starts from `clean slate`, an empty chain, and comes with no presets: the list holds only the ones you save.
- The list shows `clean slate` on top (white, an empty chain), then the presets from the newest to the oldest. Each preset gets a random colour, visible only on this screen, never the same as the two presets around it. `load at start-up` (above the list) makes the app open a chosen preset instead of the last session; that row shows `loads at start-up`. The choice follows a rename; if you delete that preset the app goes back to reopening the last session (undo brings it back).
- One click selects a preset; a double click (or Return) loads it and closes the screen.
- Buttons above the list: `+` (new preset from the current rig), `save` (overwrite the loaded preset, active after a change), `save as new` (copy of the selected preset), `import`, `export` (the selected preset), `export all` (every preset into a folder).
- Hovering any saved preset shows a pencil (rename: the name field opens on the name itself, left-aligned, so the text is already where it will stay) and an `x` (delete). After a delete, `deleted` shows for 3 seconds.
- `undo` appears at the bottom after the first action and stays until you close the screen: like Cmd+Z (which works too), it steps back through everything done there (delete, rename, new, save, load, import).

## Measured (see `Tests/Tests.cpp`)

- `save` updates the loaded preset and creates nothing new; `save` and `new` are dimmed on an unchanged preset. The last session reopens as it was (preset name, unsaved changes and modules).
- b-amp added with a b-blend after it arms by itself, also from a preset and with no IR in b-blend.

- A first install starts from `clean slate` and lists no presets.
- `clean slate`, then modules added from the library: no crash (before, the interface touched modules that had already been freed).
- b-amp switched off by the user is only `off`, and loading an IR does not switch it on; removing the b-blend after an amp that is on shows `SAFE MUTED`.
- Dragging with the split: in the expanded chain b-delay moves from lane a to lane b and the view follows it; the library then shows it free in lane a and present in lane b; b-distort, in both lanes, cannot be dragged into the shared part; b-delay, in one lane only, can. Merging the lanes closes the expanded chain.
- Copy: b-delay in lane a only can be copied (the button exists), b-comp before the split and b-distort in both lanes cannot; the copy lands in the other lane with the same settings, and then the button is gone.
- The `+` of the library with the split adds to the lane on screen, and hovering it shows where.
- Keys: `m` and `k` switch the metronome; three taps on `l` leave the tempo as it is, the fourth sets it (500 ms apart: 119 bpm, the timer of the test machine included).
- A changed preset keeps its name (in yellow).
- Expanded chain: switching lane does not move `stereo` and `balance`; a click on the module underneath closes it.
- Metronome at 100 %: +16.3 dB against 50 %, peak 0.999; at 25 % half of 50 %.
- Plugin, last state: a new instance reopens the chain and the changed preset of the last one closed, without creating presets; a project opened and closed without changes leaves it as it was.
- Plugin: mono in, stereo out (a stereo input is accepted and summed, a mono output is not); the guitar comes out on both sides; the modules see the DAW tempo (87 bpm in the test); chain, preset name and changed state come back from the project; the interface shows them.
- Split: lane B starts as a copy and the output is identical to the single chain (difference below 0.0001); a module added from the library goes into the lane on screen; moving the split point after b-comp makes it shared, moving it back to the DI puts it in both lanes; a preset keeps the lanes; merging asks for confirmation and keeps lane A. `wide`: lane A on the left, lane B on the right. An amp before the split is covered only with a cab in both lanes.

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
- A change made by the user turns the preset name yellow; `save` writes `User 1` and opens its name for editing; renaming moves the file.
- Presets: `clean slate` on top and white, then newest first; no colour repeats within two rows; one click only selects; `clean slate` empties the chain; undo brings back the previous rig and a deleted preset; `save as new` copies the selected preset to the top.
- Tuner: a note played softly (−44 dBFS) is recognised within 200 ms at 82 and 659 Hz.
- Metronome: beep, hat and wood play; starts at 50 % volume.
- Audio-thread CPU, factory chain with b-amp armed: about 2 % of one core.

## Updates

The about panel shows the version (the whole b-suite is in beta) and a `check for updates` button: it asks GitHub at once and says "you have the latest version", or shows the new version with its `download` button, or "could not reach GitHub, try again later". Once a day at most, while the window is open, b-rig asks the b-suite GitHub releases whether there is a newer version. The answer is shared with the b-suite plugins, so they make one request a day between them. If there is one, a small notice appears in the bottom-right corner ("update available" and the new version). Clicking it opens the about panel, where a `download` button opens the release page; its x closes it. A small square in b-rig's colour stays next to the logo, so the update can still be found from the logo. The request is anonymous and carries no data about you. Offline or with the releases unreachable, nothing is shown. The modules inside b-rig never check on their own: b-rig updates as one app.

## Build

CMake ≥ 3.22, a C++20 compiler and JUCE 9 (`../libs/JUCE`, downloaded if missing). The plugin folders must sit next to `b-rig`, as in this repo.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BRigTests_artefacts/Release/BRigTests
open build/BRig_artefacts/Release/b-rig.app
```

The plugin: `cmake --build build --target BRigPlugin_AU BRigPlugin_VST3` (in `build/BRigPlugin_artefacts/Release`; copied to `~/Library/Audio/Plug-Ins` unless `-DBSUITE_COPY_AFTER_BUILD=OFF`).

On macOS the build signs the app (ad hoc, with its Info.plist), so the system remembers the microphone permission. A new build counts as a new app and asks once more.

### Installers and releases

macOS (Apple Silicon): a `.pkg` made by `tools/installer/make-pkg.sh`, delivered inside a `.dmg` (`make-dmg.sh`) that keeps the b-rig icon on the volume and on the installer. The "Installation Type" page has three lines: `b-rig standalone` (the app, in Applications, ticked by default), `b-rig plugin` and `b-suite plugins` (all the plugins of the suite). The last two open to `Audio Unit` and `VST3`: ticking the line takes both formats. Plugins go to `/Library/Audio/Plug-Ins`. Its background, light or dark with the system theme, shows the b-rig logotype and the plugins' colours on two rows, inside the column of the steps, so the text box and the buttons never cover it. `tools/release.sh 0.4.0` builds the app, b-rig as a plugin and every plugin, runs all the tests, makes the `.pkg` and the `.dmg` and publishes the release; app and plugins share the version in `../VERSION.txt`. Windows installers (`b-rig/Installer/b-rig.iss`, `.github/workflows/b-rig.yml`) are being reworked.

The macOS app is signed ad hoc, not notarized: the first time, open it from System Settings > Privacy & Security > "Open Anyway". On Windows the installers are unsigned, so SmartScreen asks for confirmation. The Windows build uses the static runtime, so no Visual C++ redistributable is needed.

The b-rig logo (`docs/`: banner for the README, full screen 1920 × 1080 and square icon 1024 × 1024, one square per plugin in its colour, in chain order) is generated by `BSuiteLogo`: `./build/BSuiteLogo_artefacts/Release/BSuiteLogo ../docs`; `BSuiteLogo --installer ../tools/installer/resources` makes the installer backgrounds.

To add a plugin as a module: create `Source/Modules/Mod_<name>.cpp` like the others and give it an entry in `Source/Modules/Modules.cpp`.

## Credits

Fonts: [IBM Plex Sans and IBM Plex Mono](https://github.com/IBM/plex), SIL Open Font License (`../shared/Resources/OFL-IBMPlex.txt`). Built with [JUCE](https://juce.com) (AGPLv3). b-suite is free software under the GNU AGPLv3: see `../LICENSE`.

# Changelog

All notable changes to b-suite and b-rig. Versions follow `MAJOR.MINOR.PATCH`; every plugin shares the b-suite version, b-rig has its own.

## 0.2.0 beta

### Added
- b-rig: split the chain into two parallel lanes (a and b) from any point. `stereo` (on by default) puts a on the left and b on the right; `widen` delays lane b by the time it measures as the most mono-safe. While dragging, a box shows whether the module lands in the shared part or in the lane, the dragged card floats above the window, and holding shift shows the other lane.
- b-rig: a window at the first launch explains that b-rig includes no cab IRs and b-amp needs b-blend with your own IR.
- b-rig and plugins: `check for updates` in the about panel.
- b-amp: `tone` for the `vol + eq` boost; oversampling `off` (now `off | 2x | 4x`; older projects keep their setting).
- b-space: `mix`; floor reflection; materials filter the bounces and the tail; more low end with the amp near a wall.
- Installer: plugin formats chosen once for all plugins, and `Plugins for your DAW` ticks all of them or one by one.

### Changed
- b-rig starts from `clean slate`, with no presets. `undo` and `redo` are text buttons like the presets screen. In the library a module already in the chain shows a dark `-` that removes it. `SAFE MUTED` only appears when the protection mutes b-amp, not when you switch it off. Deleted presets go to the Trash.
- b-amp: every control is per head (gate, boost, tone, level and oversampling too) and saved with the project; noise gate off by default; `auto` removed from `level`.
- b-aftercab: OUT `auto` only corrects the level after you change something; it no longer follows the input.
- b-drop: the semitone roller follows the mouse smoothly and changes value with a margin, the wheel and trackpad move one semitone at a time, double-click opens the number for typing.
- b-space: one `room` control (studio, live, garage, club, hall, church) with size and materials, instead of `space` and `walls`. Double-click on the amp also straightens it.
- b-verb and b-delay: smaller big values, larger small controls; b-verb's graph labels predelay, early reflections and the decay times of body and highs.

### Fixed
- b-rig: adding a module after loading `clean slate` could close the app.

## 0.1.0 beta

### Added
- One product, b-rig: the macOS installer (Apple Silicon) installs the app and, if you want, any of the 19 plugins as Audio Unit and/or VST3. App and plugins share one version (`VERSION.txt`).
- Update check for the app and the plugins against the b-rig releases; a notice in the corner opens the about panel with a download button.
- b-verb and b-delay: big fixed value boxes for the main controls, groups with on/off switches, buttons for freeze, sync, ping-pong and trails.

### Changed
- b-verb: exact RT60 (within 3 %), highs decay exactly twice as fast above `damp`, Hermite interpolation on the modulated lines, darker early reflections.
- b-delay: `bbd` and `tape` repeats get darker as the delay grows, as on the real machines.
- License: GNU AGPLv3.

# Changelog

All notable changes to b-suite and b-rig. Versions follow `MAJOR.MINOR.PATCH`; every plugin shares the b-suite version, b-rig has its own.

## 0.3.0 beta

### Added
- b-comp, a compressor tuned for guitar, replaces b-tight: clean, opto, fet, squash and multi (three bands set up for guitar) styles, with a graph of the curve, the level and the gain reduction.
- b-rig: `save` and `new` in the top bar (save updates the loaded preset); the app reopens the last session, unsaved changes included.
- b-rig: at the end of a split chain `stereo` / `mono`, with the widen delay measured by itself in stereo, and `balance`, which lines up the phase and evens out the volume of the two lanes.
- b-rig: metronome pitch (−12…+12 semitones) and up to twice the volume, with a soft limit against crackles.

### Changed
- b-drop: new engine, a phase vocoder with locked peaks (Laroche and Dolson), time stretch plus resampling, a phase reset at each pick and the pick above 2.8 kHz at zero latency: exact tuning on chords and low notes, no tremolo. The shifted note now comes about 22 ms after the pick.
- b-amp: the noise gate no longer cuts long low notes (the stop detector compares 25 ms peaks instead of 8 ms); `threshold` goes down to −110 dB, where only the threshold works.
- b-rig: b-amp arms by itself as soon as a b-blend is after it, with or without an IR.
- b-rig: the four groups of the top bar at the same distance, card outlines closed while the chain scrolls, a thinner and shorter scroll bar, no outline around the open module, the rename field on the preset name.
- b-aftercab runs mono → stereo inside b-rig, so `spread` works there.
- One colour palette for the whole suite (`shared/smp/Palette.h`), grouped by role, and the background #0D0D0F everywhere.

## 0.2.0 beta

### Added
- b-rig: split the chain into two parallel lanes (a and b) from any point. `stereo` (on by default) puts a on the left and b on the right; `widen` delays lane b by the time it measures as the most mono-safe. While dragging, a box shows whether the module lands in the shared part or in the lane, the dragged card floats above the window, and holding shift shows the other lane.
- b-rig: a window at the first launch explains that b-rig includes no cab IRs and b-amp needs b-blend with your own IR.
- b-rig and plugins: `check for updates` in the about panel.
- b-amp: `tone` for the `vol + eq` boost; oversampling `off` (now `off | 2x | 4x`; older projects keep their setting).
- b-space: `mix`; floor reflection; materials filter the bounces and the tail; more low end with the amp near a wall.
- Installer: two steps on the Installation Type page: `Add the plugins for your DAW` (all of them), then inside it the format (Audio Unit, VST3) and, if you like, the plugins one by one; the plugins stay grey until a format is ticked.

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

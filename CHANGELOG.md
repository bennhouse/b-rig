# Changelog

All notable changes to b-suite and b-rig. Versions follow `MAJOR.MINOR.PATCH`; every plugin shares the b-suite version, b-rig has its own.

## Unreleased

### Added
- Suite installer: one macOS `.pkg` (Audio Unit and VST3, universal) and Windows x64 / ARM64 VST3 setups for all 19 plugins.
- Update check: b-rig follows b-rig releases, the plugins follow b-suite releases; a notice in the corner opens the about panel with a download button.
- b-verb and b-delay: big fixed value boxes for the main controls, groups with on/off switches, buttons for freeze, sync, ping-pong and trails.

### Changed
- b-verb: exact RT60 (within 3 %), highs decay exactly twice as fast above `damp`, Hermite interpolation on the modulated lines, darker early reflections.
- b-delay: `bbd` and `tape` repeats get darker as the delay grows, as on the real machines.
- License: GNU AGPLv3.

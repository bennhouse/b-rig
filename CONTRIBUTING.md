# Contributing to b-suite

Thanks for helping. b-suite is free software under the GNU AGPLv3; by contributing you agree that your changes are released under the same license.

## Getting the code

```bash
git clone --recursive https://github.com/bennhouse/b-suite.git
```

JUCE is a submodule in `libs/JUCE`; if you cloned without `--recursive`, run `git submodule update --init --recursive`.

- macOS: CMake, Ninja (or Make) and the Xcode command line tools.
- Windows: see [docs/WINDOWS.md](docs/WINDOWS.md).

## Building and testing

Each plugin builds on its own from its folder, and builds a test program next to it:

```bash
cd b-verb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/BVerbTests_artefacts/Release/BVerbTests
```

The tests measure the sound (levels, timing, decay, CPU) and save screenshots of the interface to `build/`. They must all pass before a change goes in. All the plugins at once: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build build --target bsuite_plugins bsuite_tests` from the repository root. b-rig builds from `b-rig/`.

## Making a change

1. Open an issue first for anything bigger than a small fix, so we can agree on the approach.
2. Work on a branch: `git switch -c fix/short-description`.
3. Keep one change per pull request. Run the tests of every plugin you touched (and b-rig, which compiles the plugins' sources).
4. Update the plugin's `README.md` when you change controls, ranges or behaviour, and add a line to `CHANGELOG.md`.
5. Open a pull request towards `main` and describe what you tested.

`main` is the only long-lived branch. Platform differences go in CMake (`shared/cmake/Suite.cmake`) or behind `#if JUCE_MAC` / `#if JUCE_WINDOWS`, never in a separate branch.

## Code and interface

- Audio thread: no allocations, no locks, no logging; buffers are allocated in `prepareToPlay()`.
- Shared design system in `shared/smp`: use its controls (`ValueBox`, `BigValue`, `Section`, `TextToggle`, `SegmentedChoice`) and theme tokens rather than custom ones.
- The interface rules (colours, typography, fixed-width value boxes, spacing) are in `.claude/skills/plugin-ui-guidelines/SKILL.md`; the fixed-width rule is checked by every test suite.
- Keep parameter IDs stable: renaming one breaks saved projects and presets.

## Releases

Maintainers publish with `tools/release.sh <version>` on a Mac; see the README. Windows releases are being reworked.

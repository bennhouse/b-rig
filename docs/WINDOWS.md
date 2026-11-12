# Working on b-suite from Windows

Everything you need to build, test and fix the plugins and b-rig on a Windows PC, and to make the Windows installers for a release.

## 1. Install the tools (once)

1. **Visual Studio 2022** (Community is free) or **Build Tools for Visual Studio 2022**. In the installer choose the workload **Desktop development with C++**, and under *Individual components* add:
   - **MSVC v143 - VS 2022 C++ ARM64/ARM64EC build tools** (for the ARM64 installers),
   - **C++ CMake tools for Windows** (gives you CMake; or install CMake from cmake.org).
2. **Git for Windows**: https://git-scm.com/download/win
3. **Inno Setup 6** (makes the setup .exe): https://jrsoftware.org/isdl.php
4. **GitHub CLI**: https://cli.github.com, then sign in once:

   ```powershell
   gh auth login
   ```

Open a new **Developer PowerShell for VS 2022** after installing, so everything is on the PATH.

## 2. Get the code

You need write access to the repository (the owner invites you as a collaborator; accept the e-mail from GitHub).

```powershell
git clone --recursive https://github.com/bennhouse/b-suite.git
cd b-suite
```

`--recursive` also downloads JUCE (the `libs/JUCE` submodule). If you forgot it: `git submodule update --init --recursive`.

The repository has no `.gitignore`, so tell your local Git to ignore the build folders (once, from the repository root):

```powershell
Add-Content .git\info\exclude "build/`nbuild-*/`ndist/`n.vs/`nout/"
```

## 3. Build and test

One plugin, from its folder:

```powershell
cd b-verb
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
.\build\BVerbTests_artefacts\Release\BVerbTests.exe
```

The test program measures the sound and saves screenshots of the interface in `build\`. The VST3 is in `build\BVerb_artefacts\Release\VST3\` and is also copied to `C:\Program Files\Common Files\VST3` after the build (run the shell as administrator for that, or add `-DBSUITE_COPY_AFTER_BUILD=OFF`).

All the plugins at once, from the repository root:

```powershell
cmake -S . -B build -A x64 -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_LTO=OFF
cmake --build build --config Release --parallel --target bsuite_plugins bsuite_tests
```

b-rig:

```powershell
cmake -S b-rig -B b-rig\build -A x64
cmake --build b-rig\build --config Release --parallel --target BRig BRigTests
cd b-rig; .\build\BRigTests_artefacts\Release\BRigTests.exe
```

For ARM64 use `-A ARM64` (it builds on an x64 PC, but the test programs only run on an ARM64 machine).

## 4. Fixing things

- Work on a branch and open a pull request towards `main`:

  ```powershell
  git switch -c fix/b-verb-windows
  # ...edit, build, run the tests of what you touched...
  git commit -am "b-verb: fix ... on Windows"
  git push -u origin fix/b-verb-windows
  gh pr create --fill
  ```

- One change per pull request; say in the description what you tested.
- The code is the same on Windows and macOS: platform differences go in CMake (`shared/cmake/Suite.cmake`) or behind `#if JUCE_WINDOWS` / `#if JUCE_MAC`, never in a separate branch.
- Each plugin's `README.md` describes its controls and what its tests measure.

## 5. Windows installers for a release

The release is started on a Mac with `tools/release.sh b-suite 0.2.0` (or `b-rig 0.2.0`): it builds and tests the macOS version, creates the tag `b-suite-v0.2.0` and the release with the macOS installer. Then, on Windows:

```powershell
git switch main; git pull
powershell -ExecutionPolicy Bypass -File tools\release-windows.ps1 -Product b-suite -Version 0.2.0
```

The script checks out the code of that tag, builds x64 and ARM64, runs the tests on x64, makes the two setup files with Inno Setup and uploads them to the release. It goes back to your branch at the end. Options:

- `-NoUpload`: stop at the files in `dist\` (to try them first).
- `-Arch x64` or `-Arch ARM64`: one architecture only.

Try the setup on your PC before uploading if something changed in the installer (`tools\installer\b-suite.iss`, `b-rig\Installer\b-rig.iss`).

Later the same job can run on GitHub instead (`.github/workflows/b-suite.yml` and `b-rig.yml`): set the repository variable `WINDOWS_CI` to `true` (Settings > Secrets and variables > Actions > Variables) and the release tag builds the Windows installers by itself.

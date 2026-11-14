# Working on b-rig from Windows

Everything you need to build, test and fix b-rig and the plugins on a Windows PC, and to make the Windows installers for a release.

## 1. Install the tools (once)

1. **Visual Studio 2022** (Community is free) or **Build Tools for Visual Studio 2022**. In the installer choose the workload **Desktop development with C++**, and under *Individual components* add:
   - **MSVC v143 - VS 2022 C++ ARM64/ARM64EC build tools** (for the ARM64 installer),
   - **C++ CMake tools for Windows** (gives you CMake and Ninja; or install CMake from cmake.org).
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
git clone --recursive https://github.com/bennhouse/b-rig.git
cd b-rig
```

`--recursive` also downloads JUCE (the `libs/JUCE` submodule). If you forgot it: `git submodule update --init --recursive`.

## 3. Build and test

Everything at once (the app, b-rig as a plugin, every plugin and all the test programs), from the repository root. Ninja is the fastest and keeps one compiler per job:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_LTO=OFF
cmake --build build --target brig_release bsuite_tests
```

With the Visual Studio generator instead (no Developer PowerShell needed), always give `--parallel` a number:

```powershell
cmake -S . -B build -A x64 -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_LTO=OFF
cmake --build build --config Release --parallel 4 --target brig_release bsuite_tests
```

Each Visual Studio project compiles several files at once (`/MP`, capped in `shared/cmake/Suite.cmake`), and `--parallel` without a number also builds one project per core at the same time: on a 16-core PC that was up to 256 compilers and 60 GB of RAM.

Run the tests from each folder (they save screenshots of the interface in `<folder>\build\`):

```powershell
cd b-rig;  ..\build\b-rig\BRigTests_artefacts\Release\BRigTests.exe;  cd ..
cd b-verb; ..\build\b-verb\BVerbTests_artefacts\Release\BVerbTests.exe; cd ..
```

What you get:

- the app: `build\b-rig\BRig_artefacts\Release\b-rig.exe`
- b-rig as a plugin: `build\b-rig\BRigPlugin_artefacts\Release\VST3\b-rig.vst3`
- each plugin: `build\<plugin>\<Target>_artefacts\Release\VST3\<plugin>.vst3`

Without `-DBSUITE_COPY_AFTER_BUILD=OFF` the VST3s are also copied to `C:\Program Files\Common Files\VST3` after the build (run the shell as administrator for that). One plugin can also be built on its own from its folder (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`).

For ARM64 use `-A ARM64` (it builds on an x64 PC, but the test programs only run on an ARM64 machine).

The version comes from `VERSION.txt`; an existing build folder follows it when it changes.

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

The release is started on a Mac with `tools/release.sh 0.4.1`: it builds and tests the macOS version, creates the tag `b-rig-v0.4.1` and the release with the macOS installer. Then, on Windows:

```powershell
git switch main; git pull
powershell -ExecutionPolicy Bypass -File tools\release-windows.ps1 -Version 0.4.1
```

The script checks out the code of that tag, builds the app, b-rig as a plugin and every plugin for x64 and ARM64, runs all the tests on x64, makes one setup per architecture with Inno Setup (`b-rig\Installer\b-rig.iss`) and uploads them to the release. It goes back to your branch at the end. The setup has the same three choices as the macOS installer: **b-rig standalone** (the app, in `C:\Program Files\b-suite\b-rig`), **b-rig plugin** and **b-suite plugins** (VST3, in `C:\Program Files\Common Files\VST3\b-suite`). Options:

- `-NoUpload`: stop at the files in `dist\` (to try them first).
- `-Arch x64` or `-Arch ARM64`: one architecture only.
- `-Ref <branch or commit>`: build that instead of the tag, to try the installer before a release (never uploads).

Try the setup on your PC before uploading if something changed in the installer.

Later the same job can run on GitHub instead (`.github/workflows/b-rig.yml`): set the repository variable `WINDOWS_CI` to `true` (Settings > Secrets and variables > Actions > Variables) and the release tag builds the Windows installers by itself.

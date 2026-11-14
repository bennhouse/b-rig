﻿# Installer Windows di una release di b-rig, compilati su un PC Windows (al posto della GitHub Action):
#   powershell -ExecutionPolicy Bypass -File tools\release-windows.ps1 -Version 0.4.1
# Prende il codice del tag b-rig-v<versione> (lo crea chi fa la release sul Mac con tools/release.sh), compila
# dalla radice l'app, b-rig come plugin e tutti i plugin (brig_release) per x64 e ARM64, lancia tutti i test (solo
# x64: il PC non esegue ARM64), crea un setup per architettura con Inno Setup (b-rig\Installer\b-rig.iss: app,
# b-rig plugin, b-suite plugins) e li carica nella release su GitHub.
# -NoUpload: si ferma ai file in dist\. -Arch x64 o ARM64: una sola architettura. -Ref: un altro commit o branch al
# posto del tag (per provare gli installer prima di una release; implica -NoUpload).
# Serve: Visual Studio 2022 (o Build Tools) con C++ e ARM64, CMake, Git, Inno Setup 6, GitHub CLI collegato
# (gh auth login) con accesso in scrittura al repository. Guida: docs/WINDOWS.md
param(
    [Parameter(Mandatory)] [ValidatePattern('^\d+\.\d+\.\d+$')] [string] $Version,
    [ValidateSet('x64', 'ARM64')] [string[]] $Arch = @('x64', 'ARM64'),
    [string] $Ref,
    [switch] $NoUpload
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$tag = "b-rig-v$Version"
if ($Ref) { $NoUpload = $true }

function Need ($cmd, $hint) { if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "manca ${cmd}: $hint" } }
function Run { & $args[0] $args[1..($args.Count - 1)]; if ($LASTEXITCODE -ne 0) { throw "fallito: $($args -join ' ')" } }

Need git 'installa Git for Windows'
Need cmake 'installa CMake (o il componente "C++ CMake tools" di Visual Studio) e apri la Developer PowerShell'
if (-not $NoUpload) { Need gh 'installa GitHub CLI e fai gh auth login' }
# Inno Setup: per tutti gli utenti (Program Files) o solo per te (AppData, come lo installa winget), o nel PATH
$iscc = @((Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'), (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
          (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'manca Inno Setup 6: winget install JRSoftware.InnoSetup (o https://jrsoftware.org/isdl.php)' }
if (git status --porcelain --untracked-files=no) { throw 'ci sono modifiche non salvate: fai commit o stash prima' }

# il codice esatto della release
$branch = git rev-parse --abbrev-ref HEAD
Run git fetch --tags origin
Run git checkout --quiet $(if ($Ref) { $Ref } else { $tag })
Run git submodule update --init --recursive

try {
    if (Test-Path dist) { Remove-Item dist -Recurse -Force }
    New-Item -ItemType Directory dist | Out-Null
    $plugins = Get-Content tools\installer\plugins.txt | Where-Object { $_ -match '^b-' } | ForEach-Object { , ($_.Trim() -split '\s+') }

    foreach ($a in $Arch) {
        $issArch = if ($a -eq 'ARM64') { 'arm64' } else { 'x64' }
        $tests = $a -eq 'x64'
        $build = "build-win-$a"
        Write-Host "== b-rig $Version $a" -ForegroundColor Cyan

        # --parallel 4: quattro progetti alla volta, ognuno con /MP limitato (shared/cmake/Suite.cmake). Senza
        # numero MSBuild ne lancia uno per core e la RAM esplode.
        Run cmake -S . -B $build -A $a -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_LTO=OFF "-DBSUITE_VERSION=$Version"
        $targets = @('brig_release'); if ($tests) { $targets += 'bsuite_tests' }
        Run cmake --build $build --config Release --parallel 4 --target @targets

        if ($tests) {
            # ogni test gira dalla cartella del suo plugin (le schermate vanno in <cartella>\build)
            $all = @($plugins | ForEach-Object { , @($_[0], "$($_[1])Tests") }) + , @('b-rig', 'BRigTests')
            foreach ($t in $all) {
                $dir, $exe = $t
                New-Item -ItemType Directory -Force "$dir\build" | Out-Null
                Push-Location $dir
                & "..\$build\$dir\${exe}_artefacts\Release\$exe.exe" | Out-Null
                $ok = $LASTEXITCODE -eq 0
                Pop-Location
                if (-not $ok) { throw "test falliti: $dir (lancia ..\$build\$dir\${exe}_artefacts\Release\$exe.exe da $dir per i dettagli)" }
                Write-Host "test ok  $dir"
            }
        }

        # cartella per l'installer: l'app, b-rig plugin, i plugin della suite
        $stage = "$build\stage"
        if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
        New-Item -ItemType Directory "$stage\rig", "$stage\suite" | Out-Null
        Copy-Item "$build\b-rig\BRig_artefacts\Release\b-rig.exe" $stage
        Copy-Item -Recurse "$build\b-rig\BRigPlugin_artefacts\Release\VST3\b-rig.vst3" "$stage\rig"
        foreach ($p in $plugins) {
            $dir, $target = $p
            Copy-Item -Recurse "$build\$dir\${target}_artefacts\Release\VST3\$dir.vst3" "$stage\suite"
        }
        $icon = (Resolve-Path "$build\b-rig\BRig_artefacts\JuceLibraryCode\icon.ico").Path
        Run $iscc /Q "/DAppVersion=$Version" "/DArch=$issArch" "/DStageDir=$((Resolve-Path $stage).Path)" `
            "/DIconFile=$icon" "/DOutputDir=$((Resolve-Path dist).Path)" b-rig\Installer\b-rig.iss
    }

    $files = Get-ChildItem dist\*.exe | ForEach-Object { $_.FullName }
    Write-Host "installer:`n  $($files -join "`n  ")" -ForegroundColor Green
    if (-not $NoUpload) {
        # la release la crea tools/release.sh sul Mac; se non c'è ancora la si crea qui
        gh release view $tag *> $null
        if ($LASTEXITCODE -ne 0) {
            Run gh release create $tag --title "b-rig $Version beta" --notes-file tools\installer\notes-b-rig.md
        }
        Run gh release upload $tag @files --clobber
        Write-Host "caricati nella release $tag" -ForegroundColor Green
    }
}
finally {
    git checkout --quiet $branch
    git submodule update --init --recursive | Out-Null
}

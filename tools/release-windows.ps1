# Installer Windows di una release, compilati su un PC Windows (al posto della GitHub Action):
#   powershell -ExecutionPolicy Bypass -File tools\release-windows.ps1 -Product b-suite -Version 0.2.0
#   powershell -ExecutionPolicy Bypass -File tools\release-windows.ps1 -Product b-rig -Version 0.2.0
# Prende il codice del tag <prodotto>-v<versione> (lo crea chi fa la release sul Mac con tools/release.sh), compila
# x64 e ARM64, lancia i test (solo x64: il PC non esegue ARM64), crea i due setup con Inno Setup e li carica nella
# release su GitHub. -NoUpload: si ferma ai file in dist\. -Arch x64 o ARM64: una sola architettura.
# Serve: Visual Studio 2022 (o Build Tools) con C++ e ARM64, CMake, Git, Inno Setup 6, GitHub CLI collegato
# (gh auth login) con accesso in scrittura al repository. Guida: docs/WINDOWS.md
param(
    [Parameter(Mandatory)] [ValidateSet('b-suite', 'b-rig')] [string] $Product,
    [Parameter(Mandatory)] [ValidatePattern('^\d+\.\d+\.\d+$')] [string] $Version,
    [ValidateSet('x64', 'ARM64')] [string[]] $Arch = @('x64', 'ARM64'),
    [switch] $NoUpload
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$tag = "$Product-v$Version"

function Need ($cmd, $hint) { if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "manca $cmd: $hint" } }
function Run { & $args[0] $args[1..($args.Count - 1)]; if ($LASTEXITCODE -ne 0) { throw "fallito: $($args -join ' ')" } }

Need git 'installa Git for Windows'
Need cmake 'installa CMake (o il componente "C++ CMake tools" di Visual Studio)'
if (-not $NoUpload) { Need gh 'installa GitHub CLI e fai gh auth login' }
$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) { throw "manca Inno Setup 6 ($iscc)" }
if (git status --porcelain --untracked-files=no) { throw 'ci sono modifiche non salvate: fai commit o stash prima' }

# il codice esatto della release
$branch = git rev-parse --abbrev-ref HEAD
Run git fetch --tags origin
Run git checkout --quiet $tag
Run git submodule update --init --recursive

try {
    if (Test-Path dist) { Remove-Item dist -Recurse -Force }
    New-Item -ItemType Directory dist | Out-Null
    $plugins = Get-Content tools\installer\plugins.txt | Where-Object { $_ -match '^b-' } | ForEach-Object { , ($_.Trim() -split '\s+') }

    foreach ($a in $Arch) {
        $issArch = if ($a -eq 'ARM64') { 'arm64' } else { 'x64' }
        $tests = $a -eq 'x64'
        Write-Host "== $Product $Version $a" -ForegroundColor Cyan
        if ($Product -eq 'b-suite') {
            $build = "build-win-$a"
            Run cmake -S . -B $build -A $a -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_LTO=OFF "-DBSUITE_VERSION=$Version"
            $targets = @('bsuite_plugins'); if ($tests) { $targets += 'bsuite_tests' }
            Run cmake --build $build --config Release --parallel --target @targets
            if ($tests) {
                foreach ($p in $plugins) {
                    $dir, $target = $p
                    New-Item -ItemType Directory -Force "$dir\build" | Out-Null
                    Push-Location $dir
                    & "..\$build\$dir\${target}Tests_artefacts\Release\${target}Tests.exe" | Out-Null
                    $ok = $LASTEXITCODE -eq 0
                    Pop-Location
                    if (-not $ok) { throw "test falliti: $dir (lancia ..\$build\$dir\${target}Tests_artefacts\Release\${target}Tests.exe da $dir per i dettagli)" }
                    Write-Host "test ok  $dir"
                }
            }
            $vst3 = "$build\vst3"
            if (Test-Path $vst3) { Remove-Item $vst3 -Recurse -Force }
            New-Item -ItemType Directory $vst3 | Out-Null
            foreach ($p in $plugins) {
                $dir, $target = $p
                Copy-Item -Recurse "$build\$dir\${target}_artefacts\Release\VST3\$dir.vst3" $vst3
            }
            Run $iscc "/DAppVersion=$Version" "/DArch=$issArch" "/DSourceDir=$((Resolve-Path $vst3).Path)" "/DOutputDir=$((Resolve-Path dist).Path)" tools\installer\b-suite.iss
        }
        else {
            $build = "b-rig\build-win-$a"
            Run cmake -S b-rig -B $build -A $a "-DBRIG_VERSION=$Version"
            $targets = @('BRig'); if ($tests) { $targets += 'BRigTests' }
            Run cmake --build $build --config Release --parallel --target @targets
            if ($tests) {
                Push-Location b-rig
                & ".\build-win-$a\BRigTests_artefacts\Release\BRigTests.exe" | Out-Null
                $ok = $LASTEXITCODE -eq 0
                Pop-Location
                if (-not $ok) { throw 'test di b-rig falliti' }
                Write-Host 'test ok  b-rig'
            }
            $exe = (Resolve-Path "$build\BRig_artefacts\Release\b-rig.exe").Path
            Run $iscc "/DAppVersion=$Version" "/DArch=$issArch" "/DSourceExe=$exe" "/DOutputDir=$((Resolve-Path dist).Path)" b-rig\Installer\b-rig.iss
        }
    }

    $files = Get-ChildItem dist\*.exe | ForEach-Object { $_.FullName }
    Write-Host "installer:`n  $($files -join "`n  ")" -ForegroundColor Green
    if (-not $NoUpload) {
        # la release la crea di solito tools/release.sh sul Mac; se non c'è ancora la si crea qui
        gh release view $tag *> $null
        if ($LASTEXITCODE -ne 0) {
            Run gh release create $tag --title "$Product $Version beta" --prerelease --notes-file "tools\installer\notes-$Product.md"
        }
        Run gh release upload $tag @files --clobber
        Write-Host "caricati nella release $tag" -ForegroundColor Green
    }
}
finally {
    git checkout --quiet $branch
    git submodule update --init --recursive | Out-Null
}

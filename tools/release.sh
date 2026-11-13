#!/bin/bash
# Nuova release di b-rig (l'app e i plugin che il suo installer può aggiungere, tutti con la stessa versione):
#   tools/release.sh 0.4.1   (da main, con le pull request già unite)
# Sul Mac, in locale: scrive la versione in VERSION.txt, compila app e plugin (Apple Silicon), lancia tutti i test e
# crea l'installer (tools/installer/make-pkg.sh) dentro un'immagine disco con l'icona (make-dmg.sh). Se tutto passa
# fa il commit, crea il tag b-rig-v<versione>, fa il push e pubblica la release come "Latest", con le note di
# tools/installer/notes-b-rig.md e la sezione della versione in CHANGELOG.md (durante la beta il
# titolo dice "beta": le pre-release GitHub non le mostra nella pagina del repository).
# App e plugin già installati la vedono con il controllo aggiornamenti (shared/smp/Updates.h).
set -euo pipefail
cd "$(dirname "$0")/.."

version="${1:-}"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "uso: tools/release.sh <x.y.z>   (es. tools/release.sh 0.2.0)" >&2; exit 1; }
tag="b-rig-v$version"
gh="$(command -v gh || echo "$HOME/.local/bin/gh")"
[[ -x "$gh" ]] || { echo "serve GitHub CLI (gh) collegato al tuo account" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && { echo "il tag $tag esiste già" >&2; exit 1; }
[[ "$(git branch --show-current)" == "main" ]] || { echo "le release partono da main: prima unisci la pull request e passa a main" >&2; exit 1; }
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "ci sono modifiche non salvate: fai prima il commit" >&2; exit 1; }
git submodule update --init --recursive

# generatore: Ninja se c'è (molto più veloce), altrimenti Makefile
gen=(); jobs=()
if command -v ninja >/dev/null || [[ -x "$HOME/Library/Python/3.9/bin/ninja" ]]; then
    export PATH="$PATH:$HOME/Library/Python/3.9/bin"; gen=(-G Ninja)
else
    jobs=(-- -j "$(sysctl -n hw.ncpu)")
fi

# 1. versione (se la build o i test falliscono resta da annullare con "git checkout VERSION.txt")
printf '%s\n' "$version" > VERSION.txt

# 2. build (Apple Silicon), test e installer
cmake -S . -B build-release ${gen[@]+"${gen[@]}"} -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_VERSION="$version"
cmake --build build-release --target brig_release bsuite_tests ${jobs[@]+"${jobs[@]}"}
run_test () { (cd "$1" && "$2" > /dev/null) || { echo "test falliti: $1 (rilancia $2 da $1 per i dettagli)" >&2; exit 1; }; echo "test ok  $1"; }
while read -r dir target; do
    target="${target%$'\r'}"
    [[ -z "${dir:-}" || "$dir" == \#* ]] && continue
    mkdir -p "$dir/build"
    run_test "$dir" "../build-release/$dir/${target}Tests_artefacts/Release/${target}Tests"
done < tools/installer/plugins.txt
mkdir -p b-rig/build
run_test b-rig ../build-release/b-rig/BRigTests_artefacts/Release/BRigTests
rm -rf dist
bash tools/installer/make-pkg.sh build-release "$version" dist
bash tools/installer/make-dmg.sh "dist/b-rig-$version-macos-arm64.pkg" "$version" dist
dmg="dist/b-rig-$version-macos-arm64.dmg"

# 3. commit, tag, push e release
git add VERSION.txt
git diff --cached --quiet || git commit -m "b-rig $version"
git tag -a "$tag" -m "b-rig $version"
git push origin HEAD "$tag"
# note: come installare, poi la sezione "## <versione>" di CHANGELOG.md
notes="$(mktemp)"
{ cat tools/installer/notes-b-rig.md; echo; echo "## What's new in $version"; echo
  awk -v v="## $version" 'index($0, v) == 1 { on = 1; next } on && /^## / { exit } on' CHANGELOG.md; } > "$notes"
"$gh" release create "$tag" "$dmg" --title "b-rig $version beta" --latest --notes-file "$notes"
rm -f "$notes"
echo "pubblicato $tag con $dmg"

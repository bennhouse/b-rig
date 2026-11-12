#!/bin/bash
# Nuova release di un prodotto della b-suite:
#   tools/release.sh b-suite 0.2.0    tutti i plugin, un solo installer (stessa versione per tutti)
#   tools/release.sh b-rig 0.2.0      l'app b-rig
# Sul Mac, in locale: imposta la versione, compila, lancia i test e crea l'installer macOS (.pkg della suite,
# .dmg di b-rig). Se tutto passa fa il commit, crea il tag <prodotto>-v<versione>, fa il push e pubblica la release
# (pre-release durante la beta) con l'installer macOS. Il tag avvia la GitHub Action che aggiunge gli installer
# Windows (dal Mac non si compila per Windows). App e plugin già installati vedono la nuova versione con il
# controllo aggiornamenti (shared/smp/Updates.h).
set -euo pipefail
cd "$(dirname "$0")/.."

product="${1:-}"; version="${2:-}"
if [[ ! "$product" =~ ^(b-suite|b-rig)$ || ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "uso: tools/release.sh b-suite|b-rig <x.y.z>   (es. tools/release.sh b-suite 0.2.0)" >&2
    exit 1
fi
tag="$product-v$version"
gh="$(command -v gh || echo "$HOME/.local/bin/gh")"
[[ -x "$gh" ]] || { echo "serve GitHub CLI (gh) collegato al tuo account" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && { echo "il tag $tag esiste già" >&2; exit 1; }
[[ -z "$(git status --porcelain)" ]] || { echo "ci sono modifiche non salvate: fai prima il commit" >&2; exit 1; }
git submodule update --init --recursive

# generatore: Ninja se c'è (molto più veloce), altrimenti Makefile
gen=(); jobs=()
if command -v ninja >/dev/null || [[ -x "$HOME/Library/Python/3.9/bin/ninja" ]]; then
    export PATH="$PATH:$HOME/Library/Python/3.9/bin"; gen=(-G Ninja)
else
    jobs=(-- -j "$(sysctl -n hw.ncpu)")
fi

# 1. versione (resta da annullare a mano con "git checkout ." se la build o i test falliscono)
if [[ "$product" == b-suite ]]; then versionFile=shared/cmake/Suite.cmake; else versionFile=b-rig/CMakeLists.txt; fi
sed -i '' -E "s/set\((BRIG|BSUITE)_VERSION \"[0-9.]+\"/set(\1_VERSION \"$version\"/" "$versionFile"

# 2. build, test e installer macOS
rm -rf dist; mkdir -p dist
if [[ "$product" == b-suite ]]; then
    cmake -S . -B build-release "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DBSUITE_COPY_AFTER_BUILD=OFF -DBSUITE_VERSION="$version"
    cmake --build build-release --target bsuite_plugins bsuite_tests "${jobs[@]}"
    while read -r dir target; do
        [[ -z "${dir:-}" || "$dir" == \#* ]] && continue
        mkdir -p "$dir/build"
        (cd "$dir" && "../build-release/$dir/${target}Tests_artefacts/Release/${target}Tests" > /dev/null) \
            || { echo "test falliti: $dir (rilancia i test del plugin per i dettagli)" >&2; exit 1; }
        echo "test ok  $dir"
    done < tools/installer/plugins.txt
    bash tools/installer/make-suite-pkg.sh build-release "$version" dist
    asset="dist/b-suite-$version-macos.pkg"
else
    cmake -S b-rig -B b-rig/build-release "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBRIG_VERSION="$version"
    cmake --build b-rig/build-release --target BRig BRigTests "${jobs[@]}"
    (cd b-rig && ./build-release/BRigTests_artefacts/Release/BRigTests > /dev/null) || { echo "test di b-rig falliti" >&2; exit 1; }
    bash b-rig/Installer/make-dmg.sh "b-rig/build-release/BRig_artefacts/Release/b-rig.app" "$version" dist
    asset="dist/b-rig-$version-macos-arm64.dmg"
fi

# 3. commit, tag, push e release con l'installer macOS (Windows lo aggiunge la GitHub Action del tag)
git add "$versionFile"
git diff --cached --quiet || git commit -m "$product $version"
git tag -a "$tag" -m "$product $version"
git push origin HEAD "$tag"
"$gh" release create "$tag" "$asset" --title "$product $version beta" --prerelease --notes-file "tools/installer/notes-$product.md"
echo "pubblicato $tag con $asset; gli installer Windows arrivano dalla GitHub Action:  $gh run watch"

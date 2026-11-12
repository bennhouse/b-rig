#!/bin/bash
# Nuova release di un prodotto della b-suite:
#   tools/release.sh b-rig 0.2.0
# Aggiorna la versione nel CMakeLists, fa il commit, crea il tag <prodotto>-v<versione> e fa il push.
# Il tag avvia la GitHub Action che compila, testa e pubblica gli installer (per ora solo b-rig ne ha una);
# app e plugin già installati vedono la nuova versione con il controllo aggiornamenti (shared/smp/Updates.h).
set -euo pipefail
cd "$(dirname "$0")/.."

product="${1:-}"; version="${2:-}"
if [[ -z "$product" || ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "uso: tools/release.sh <prodotto> <x.y.z>   (es. tools/release.sh b-rig 0.2.0)" >&2
    exit 1
fi
[[ -f "$product/CMakeLists.txt" ]] || { echo "nessun prodotto '$product'" >&2; exit 1; }
[[ -f ".github/workflows/$product.yml" ]] || { echo "manca .github/workflows/$product.yml: '$product' non ha ancora installer" >&2; exit 1; }
tag="$product-v$version"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && { echo "il tag $tag esiste già" >&2; exit 1; }
[[ -z "$(git status --porcelain)" ]] || { echo "ci sono modifiche non salvate: fai prima il commit" >&2; exit 1; }

# versione nel CMakeLists (b-rig: BRIG_VERSION; plugin: project(... VERSION x.y.z ...))
if grep -q 'set(BRIG_VERSION' "$product/CMakeLists.txt"; then
    sed -i '' -E "s/set\(BRIG_VERSION \"[0-9.]+\"/set(BRIG_VERSION \"$version\"/" "$product/CMakeLists.txt"
else
    sed -i '' -E "s/^(project\([A-Za-z]+ VERSION )[0-9.]+/\1$version/" "$product/CMakeLists.txt"
fi

git add "$product/CMakeLists.txt"
git diff --cached --quiet || git commit -m "$product $version"
git tag -a "$tag" -m "$product $version"
git push origin HEAD "$tag"
echo "pubblicato $tag: segui la build con  gh run watch"

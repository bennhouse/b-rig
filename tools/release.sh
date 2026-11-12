#!/bin/bash
# Nuova release di un prodotto della b-suite:
#   tools/release.sh b-rig 0.2.0      l'app b-rig
#   tools/release.sh b-suite 0.2.0    tutti i plugin, un solo installer (stessa versione per tutti)
# Aggiorna la versione nel CMakeLists, fa il commit, crea il tag <prodotto>-v<versione> e fa il push.
# Il tag avvia la GitHub Action che compila, testa e pubblica gli installer;
# app e plugin già installati vedono la nuova versione con il controllo aggiornamenti (shared/smp/Updates.h).
set -euo pipefail
cd "$(dirname "$0")/.."

product="${1:-}"; version="${2:-}"
if [[ -z "$product" || ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "uso: tools/release.sh <prodotto> <x.y.z>   (es. tools/release.sh b-rig 0.2.0)" >&2
    exit 1
fi
if [[ "$product" == b-suite ]]; then versionFile=shared/cmake/Suite.cmake; else versionFile="$product/CMakeLists.txt"; fi
[[ -f "$versionFile" ]] || { echo "nessun prodotto '$product'" >&2; exit 1; }
[[ -f ".github/workflows/$product.yml" ]] || { echo "manca .github/workflows/$product.yml: '$product' non ha ancora installer" >&2; exit 1; }
tag="$product-v$version"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && { echo "il tag $tag esiste già" >&2; exit 1; }
[[ -z "$(git status --porcelain)" ]] || { echo "ci sono modifiche non salvate: fai prima il commit" >&2; exit 1; }

# versione: b-rig in BRIG_VERSION, la suite in BSUITE_VERSION
sed -i '' -E "s/set\((BRIG|BSUITE)_VERSION \"[0-9.]+\"/set(\1_VERSION \"$version\"/" "$versionFile"

git add "$versionFile"
git diff --cached --quiet || git commit -m "$product $version"
git tag -a "$tag" -m "$product $version"
git push origin HEAD "$tag"
echo "pubblicato $tag: segui la build con  gh run watch"

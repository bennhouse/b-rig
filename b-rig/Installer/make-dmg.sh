#!/bin/bash
# Immagine disco di b-rig per macOS: l'app e un collegamento ad Applicazioni (si trascina l'una sull'altra).
#   make-dmg.sh <percorso di b-rig.app> <versione> <cartella di uscita>
set -euo pipefail
app="$1"; version="$2"; out="$3"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
cp -R "$app" "$stage/b-rig.app"
ln -s /Applications "$stage/Applications"
mkdir -p "$out"
dmg="$out/b-rig-$version-macos-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "b-rig $version" -srcfolder "$stage" -ov -format UDZO "$dmg"
echo "$dmg"

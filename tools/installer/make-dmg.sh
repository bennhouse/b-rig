#!/bin/bash
# Immagine disco da scaricare: dentro c'è "Install b-rig <versione>.pkg" con l'icona di b-rig (un .pkg scaricato da
# solo perde l'icona personalizzata, dentro un'immagine disco la conserva); anche il volume ha l'icona di b-rig.
#   make-dmg.sh <pkg> <versione> <cartella di uscita>
set -euo pipefail
pkg="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; version="$2"; mkdir -p "$3"; out="$(cd "$3" && pwd)"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"; trap 'hdiutil detach -quiet "$work/mnt" 2>/dev/null || true; rm -rf "$work"' EXIT

# icona .icns dall'icona dell'app (1024 px)
iconset="$work/b-rig.iconset"; mkdir -p "$iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s "$root/b-rig/Resources/AppIcon.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$root/b-rig/Resources/AppIcon.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$work/b-rig.icns"

# contenuto: il pacchetto con l'icona di b-rig
stage="$work/stage"; mkdir -p "$stage"
inner="$stage/Install b-rig $version.pkg"
cp "$pkg" "$inner"
osascript -l JavaScript -e "ObjC.import('AppKit'); \$.NSWorkspace.sharedWorkspace.setIconForFileOptions(\$.NSImage.alloc.initWithContentsOfFile('$work/b-rig.icns'), '$inner', 0)" >/dev/null
cp "$work/b-rig.icns" "$stage/.VolumeIcon.icns"

# immagine scrivibile -> attributo "icona personalizzata" sul volume -> immagine compressa
hdiutil create -quiet -fs HFS+ -volname "b-rig $version" -srcfolder "$stage" -format UDRW "$work/rw.dmg"
mkdir -p "$work/mnt"
hdiutil attach -quiet -nobrowse -noverify -noautoopen -mountpoint "$work/mnt" "$work/rw.dmg"
SetFile -a C "$work/mnt"
hdiutil detach -quiet "$work/mnt"
dmg="$out/b-rig-$version-macos-arm64.dmg"
rm -f "$dmg"
hdiutil convert -quiet "$work/rw.dmg" -format UDZO -imagekey zlib-level=9 -o "$dmg"
echo "$dmg"

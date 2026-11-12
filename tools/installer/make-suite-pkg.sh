#!/bin/bash
# Installer macOS della suite: un .pkg con AU e VST3 di ogni plugin (binari universali), un pacchetto per plugin che
# si può togliere in "Personalizza". Installa in /Library/Audio/Plug-Ins/{Components,VST3}.
#   make-suite-pkg.sh <cartella di build della radice> <versione> <cartella di uscita>
set -euo pipefail
build="$(cd "$1" && pwd)"; version="$2"; mkdir -p "$3"; out="$(cd "$3" && pwd)"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkgs"
outline=""; choices=""; refs=""

pack ()   # bundle, cartella d'installazione, identificativo, nome del .pkg
{
    local stage="$work/stage-$3"
    mkdir -p "$stage"; cp -R "$1" "$stage/"
    codesign --force --deep --sign - "$stage/$(basename "$1")"     # firma ad hoc di tutto il bundle
    pkgbuild --analyze --root "$stage" "$work/$3.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/$3.plist"   # sempre nella cartella di sistema
    pkgbuild --root "$stage" --component-plist "$work/$3.plist" --install-location "$2" \
             --identifier "$3" --version "$version" "$work/pkgs/$4" >/dev/null
}

while read -r dir target; do
    target="${target%$'\r'}"
    [[ -z "${dir:-}" || "$dir" == \#* ]] && continue
    art="$build/$dir/${target}_artefacts/Release"
    [[ -d "$art/AU/$dir.component" && -d "$art/VST3/$dir.vst3" ]] || { echo "manca $dir in $art" >&2; exit 1; }
    id="com.bsuite.pkg.${dir#b-}"
    pack "$art/AU/$dir.component" /Library/Audio/Plug-Ins/Components "$id.au" "$dir-au.pkg"
    pack "$art/VST3/$dir.vst3" /Library/Audio/Plug-Ins/VST3 "$id.vst3" "$dir-vst3.pkg"
    outline+="<line choice=\"$dir\"/>"
    choices+="<choice id=\"$dir\" title=\"$dir\" description=\"$dir: Audio Unit and VST3\"><pkg-ref id=\"$id.au\"/><pkg-ref id=\"$id.vst3\"/></choice>"
    refs+="<pkg-ref id=\"$id.au\" version=\"$version\">$dir-au.pkg</pkg-ref><pkg-ref id=\"$id.vst3\" version=\"$version\">$dir-vst3.pkg</pkg-ref>"
    echo "  $dir"
done < "$root/tools/installer/plugins.txt"

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>b-suite $version</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <license file="LICENSE"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>$outline</choices-outline>
    $choices
    $refs
</installer-gui-script>
XML
mkdir -p "$work/res"
cp "$root/LICENSE" "$work/res/LICENSE"
printf 'b-suite %s (beta)\n\nThe b-suite plugins as Audio Unit and VST3, for Apple Silicon and Intel Macs. They go to /Library/Audio/Plug-Ins and show up in your DAW under "b-suite".\n\nChoose "Customise" to leave out the plugins you do not want.\n\nFree software under the GNU AGPLv3.\n' "$version" > "$work/res/welcome.txt"
pkg="$out/b-suite-$version-macos.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" --package-path "$work/pkgs" "$pkg"
echo "$pkg"

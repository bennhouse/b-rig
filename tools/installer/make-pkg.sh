#!/bin/bash
# Installer macOS di b-rig (.pkg): l'app in /Applications (sempre) e, a scelta, i plugin per la DAW in
# /Library/Audio/Plug-Ins. La pagina "Tipo di installazione" si legge in due passi:
#   b-rig                                l'app (sempre installata)
#   Add the plugins for your DAW         1. vuoi anche i plugin? (spuntarla prende tutto, nei due formati)
#       Format                           2. in che formato: Audio Unit, VST3 o entrambi
#           Audio Unit / VST3
#       Plugins                          solo se vuoi sceglierli uno per uno: grigi finché non c'è un formato
#           b-drop ... b-synthesia
# Per ogni plugin e formato c'è una scelta nascosta che si accende da sola quando sono accesi sia il plugin sia il
# formato (espressione "selected" valutata dall'Installer a ogni clic).
# Sfondo chiaro e scuro con il logotipo e i colori dei plugin (tools/installer/resources, generati da BSuiteLogo).
#   make-pkg.sh <cartella di build della radice> <versione> <cartella di uscita>
set -euo pipefail
build="$(cd "$1" && pwd)"; version="$2"; mkdir -p "$3"; out="$(cd "$3" && pwd)"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkgs" "$work/res"

pack ()   # bundle, cartella d'installazione, identificativo, nome del .pkg
{
    local stage="$work/stage-$3"
    mkdir -p "$stage"; cp -R "$1" "$stage/"
    codesign --force --deep --sign - "$stage/$(basename "$1")"     # firma ad hoc di tutto il bundle
    pkgbuild --analyze --root "$stage" "$work/$3.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/$3.plist"   # sempre nella cartella prevista
    pkgbuild --root "$stage" --component-plist "$work/$3.plist" --install-location "$2" \
             --identifier "$3" --version "$version" "$work/pkgs/$4" >/dev/null
}

# l'app
app="$build/b-rig/BRig_artefacts/Release/b-rig.app"
[[ -d "$app" ]] || { echo "manca $app" >&2; exit 1; }
pack "$app" /Applications com.bsuite.brig.app b-rig-app.pkg
refs="<pkg-ref id=\"com.bsuite.brig.app\" version=\"$version\">b-rig-app.pkg</pkg-ref>"
pluginLines=""; hiddenLines=""; choices=""

# una riga di descrizione per plugin, presa dall'elenco del README ("- **b-amp**: an amp head ...")
describe () { python3 - "$root/README.md" "$1" <<'PY'
import re, sys, html
name = sys.argv[2]
for line in open(sys.argv[1], encoding="utf-8"):
    m = re.match(r"- \*\*([^*]+)\*\*(?: \(([^)]*)\))?: (.*)", line.strip())   # "- **b-fs** (b-freshstrings): ..."
    if m and name in (m.group(1), m.group(2)):
        sentences = re.sub(r"[`*]", "", m.group(3)).split(". ")
        text = sentences[0].rstrip(".")
        if len(text) < 40 and len(sentences) > 1: text += ". " + sentences[1].rstrip(".")   # "the last slot." da solo non dice nulla
        text = text[0].upper() + text[1:]
        print(html.escape(text + ".", quote=True)); break
PY
}

while read -r dir target; do
    target="${target%$'\r'}"
    [[ -z "${dir:-}" || "$dir" == \#* ]] && continue
    art="$build/$dir/${target}_artefacts/Release"
    [[ -d "$art/AU/$dir.component" && -d "$art/VST3/$dir.vst3" ]] || { echo "manca $dir in $art" >&2; exit 1; }
    id="com.bsuite.pkg.${dir#b-}"
    pack "$art/AU/$dir.component" /Library/Audio/Plug-Ins/Components "$id.au" "$dir-au.pkg"
    pack "$art/VST3/$dir.vst3" /Library/Audio/Plug-Ins/VST3 "$id.vst3" "$dir-vst3.pkg"
    desc="$(describe "$dir")"
    pluginLines+="<line choice=\"$dir\"/>"
    hiddenLines+="<line choice=\"$dir.au\"/><line choice=\"$dir.vst3\"/>"
    choices+="<choice id=\"$dir\" title=\"$dir\" description=\"$desc\" start_selected=\"false\" enabled=\"choices['fmt.au'].selected || choices['fmt.vst3'].selected\"/>"
    # nascoste: si installano quando sono accesi il plugin e il formato
    choices+="<choice id=\"$dir.au\" title=\"$dir Audio Unit\" visible=\"false\" start_selected=\"false\" selected=\"choices['$dir'].selected &amp;&amp; choices['fmt.au'].selected\"><pkg-ref id=\"$id.au\"/></choice>"
    choices+="<choice id=\"$dir.vst3\" title=\"$dir VST3\" visible=\"false\" start_selected=\"false\" selected=\"choices['$dir'].selected &amp;&amp; choices['fmt.vst3'].selected\"><pkg-ref id=\"$id.vst3\"/></choice>"
    refs+="<pkg-ref id=\"$id.au\" version=\"$version\">$dir-au.pkg</pkg-ref><pkg-ref id=\"$id.vst3\" version=\"$version\">$dir-vst3.pkg</pkg-ref>"
    echo "  $dir"
done < "$root/tools/installer/plugins.txt"

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>b-rig $version</title>
    <background file="background.png" alignment="bottomleft" scaling="proportional"/>
    <background-darkAqua file="background-dark.png" alignment="bottomleft" scaling="proportional"/>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="LICENSE" mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="always" require-scripts="false" hostArchitectures="arm64"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="app"/>
        <line choice="addplugins">
            <line choice="formats"><line choice="fmt.au"/><line choice="fmt.vst3"/></line>
            <line choice="plugins">$pluginLines</line>
        </line>
        $hiddenLines
    </choices-outline>
    <choice id="app" title="b-rig" description="The app: the whole chain in one window, with tuner, metronome and presets. Always installed, in Applications." enabled="false" start_selected="true"><pkg-ref id="com.bsuite.brig.app"/></choice>
    <choice id="addplugins" title="Add the plugins for your DAW" description="Do you also want the effects as plugins for your music software? Tick this line for all of them, as Audio Unit and VST3. Open it to choose the format and, if you like, only some plugins." start_selected="false"/>
    <choice id="formats" title="Format" description="Which format your music software uses. Pick at least one: the plugins below can only be chosen once a format is ticked." start_selected="false"/>
    <choice id="fmt.au" title="Audio Unit" description="For Logic Pro, GarageBand and the other Audio Unit hosts." start_selected="false"/>
    <choice id="fmt.vst3" title="VST3" description="For Ableton Live, Reaper, Cubase, Studio One and the other VST3 hosts." start_selected="false"/>
    <choice id="plugins" title="Plugins" description="All the plugins, or open this line and tick only the ones you want. Choose a format above first." start_selected="false" enabled="choices['fmt.au'].selected || choices['fmt.vst3'].selected"/>
    $choices
    $refs
</installer-gui-script>
XML

# icona dell'app nelle pagine (incorporata: productbuild tiene solo i file citati dalla distribuzione)
icon="data:image/png;base64,$(sips -z 144 144 "$root/b-rig/Resources/AppIcon.png" --out "$work/icon.png" >/dev/null && base64 -i "$work/icon.png" | tr -d '\n')"

# testi: stile della suite, chiari o scuri con il tema del sistema
style='<style>:root{color-scheme:light dark}body{font:13px -apple-system,"Helvetica Neue",sans-serif;line-height:1.5;color:#1c1c1c;margin:0 40px 0 0}
h1{font-size:22px;font-weight:600;letter-spacing:-0.02em;margin:0 0 12px}h1 b{font-weight:700}h1 span{color:#9a9a9a;font-weight:300}
p,li{color:#3a3a3a}code{font:12px ui-monospace,Menlo,monospace}.dim{color:#808080}
@media (prefers-color-scheme:dark){body{color:#e0e0e0}p,li{color:#a3a3a3}h1 span{color:#666}}</style>'
cat > "$work/res/welcome.html" <<HTML
<html><head><meta charset="utf-8">$style</head><body>
<img src="$icon" width="72" height="72" alt="" style="float:left;margin:2px 18px 8px 0">
<h1><b>b</b><span>-</span>rig $version</h1>
<p>A guitar rig in one app: plug in the guitar and play through amp, cab and effects, with tuner, metronome and presets. No DAW needed.</p>
<p>The app is always installed. On the <b>Installation Type</b> page you can also add <b>the plugins</b> for your DAW: tick <b>Add the plugins for your DAW</b> to get all of them, or open it, pick the <b>format</b> (Audio Unit, VST3 or both) and then the plugins you want.</p>
<p class="dim">Beta. Free and open source under the GNU AGPLv3.</p>
</body></html>
HTML
cat > "$work/res/conclusion.html" <<HTML
<html><head><meta charset="utf-8">$style</head><body>
<img src="$icon" width="72" height="72" alt="" style="float:left;margin:2px 18px 8px 0">
<h1><b>b</b><span>-</span>rig is ready</h1>
<p>Open <b>b-rig</b> from Applications and choose your audio interface in <code>setup</code>.</p>
<p>The plugins you picked are in <code>/Library/Audio/Plug-Ins</code>: Audio Unit in <code>Components</code>, VST3 in <code>VST3</code>. Your DAW lists them under <b>b-suite</b>; if it was open, rescan the plugins or restart it.</p>
<p class="dim">To add or remove plugins later, run this installer again or delete them from those folders.</p>
</body></html>
HTML
cp "$root/LICENSE" "$work/res/LICENSE"
cp "$root/tools/installer/resources/background.png" "$root/tools/installer/resources/background-dark.png" "$work/res/"

pkg="$out/b-rig-$version-macos-arm64.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" --package-path "$work/pkgs" "$pkg"
echo "$pkg"

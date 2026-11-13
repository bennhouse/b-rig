#!/bin/bash
# Installer macOS di b-rig (.pkg). La pagina "Tipo di installazione" ha tre righe, ognuna con i suoi formati:
#   b-rig standalone             l'app in /Applications (spuntata di default)
#   b-rig plugin                 b-rig dentro la DAW, in /Library/Audio/Plug-Ins
#       Audio Unit / VST3
#   b-suite plugins              tutti i plugin della suite, in /Library/Audio/Plug-Ins
#       Audio Unit / VST3
# Spuntare una riga con i formati prende tutti e due i formati; aprendola se ne sceglie uno.
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

# b-rig come plugin
rigArt="$build/b-rig/BRigPlugin_artefacts/Release"
[[ -d "$rigArt/AU/b-rig.component" && -d "$rigArt/VST3/b-rig.vst3" ]] || { echo "manca b-rig plugin in $rigArt" >&2; exit 1; }
pack "$rigArt/AU/b-rig.component" /Library/Audio/Plug-Ins/Components com.bsuite.pkg.brig.au b-rig-au.pkg
pack "$rigArt/VST3/b-rig.vst3" /Library/Audio/Plug-Ins/VST3 com.bsuite.pkg.brig.vst3 b-rig-vst3.pkg
refs+="<pkg-ref id=\"com.bsuite.pkg.brig.au\" version=\"$version\">b-rig-au.pkg</pkg-ref><pkg-ref id=\"com.bsuite.pkg.brig.vst3\" version=\"$version\">b-rig-vst3.pkg</pkg-ref>"
suiteAu=""; suiteVst3=""

while read -r dir target; do
    target="${target%$'\r'}"
    [[ -z "${dir:-}" || "$dir" == \#* ]] && continue
    art="$build/$dir/${target}_artefacts/Release"
    [[ -d "$art/AU/$dir.component" && -d "$art/VST3/$dir.vst3" ]] || { echo "manca $dir in $art" >&2; exit 1; }
    id="com.bsuite.pkg.${dir#b-}"
    pack "$art/AU/$dir.component" /Library/Audio/Plug-Ins/Components "$id.au" "$dir-au.pkg"
    pack "$art/VST3/$dir.vst3" /Library/Audio/Plug-Ins/VST3 "$id.vst3" "$dir-vst3.pkg"
    suiteAu+="<pkg-ref id=\"$id.au\"/>"; suiteVst3+="<pkg-ref id=\"$id.vst3\"/>"
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
        <line choice="rig"><line choice="rig.au"/><line choice="rig.vst3"/></line>
        <line choice="suite"><line choice="suite.au"/><line choice="suite.vst3"/></line>
    </choices-outline>
    <choice id="app" title="b-rig standalone" description="The app: the whole chain in one window, with tuner, metronome and presets. Goes in Applications." start_selected="true"><pkg-ref id="com.bsuite.brig.app"/></choice>
    <choice id="rig" title="b-rig plugin" description="b-rig inside your DAW: the same chain and the same presets as the app, mono in and stereo out. Tick it for both formats, or open it and pick one." start_selected="false"/>
    <choice id="rig.au" title="Audio Unit" description="For Logic Pro, GarageBand and the other Audio Unit hosts." start_selected="false"><pkg-ref id="com.bsuite.pkg.brig.au"/></choice>
    <choice id="rig.vst3" title="VST3" description="For Ableton Live, Reaper, Cubase, Studio One and the other VST3 hosts." start_selected="false"><pkg-ref id="com.bsuite.pkg.brig.vst3"/></choice>
    <choice id="suite" title="b-suite plugins" description="Every effect of the suite as its own plugin for your DAW. Tick it for both formats, or open it and pick one." start_selected="false"/>
    <choice id="suite.au" title="Audio Unit" description="For Logic Pro, GarageBand and the other Audio Unit hosts." start_selected="false">$suiteAu</choice>
    <choice id="suite.vst3" title="VST3" description="For Ableton Live, Reaper, Cubase, Studio One and the other VST3 hosts." start_selected="false">$suiteVst3</choice>
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
<p>On the <b>Installation Type</b> page choose what to install: <b>b-rig standalone</b> (the app), <b>b-rig plugin</b> (b-rig inside your DAW) and <b>b-suite plugins</b> (every effect as its own plugin). Tick a line for both formats, or open it and pick Audio Unit or VST3.</p>
<p class="dim">Beta. Free and open source under the GNU AGPLv3.</p>
</body></html>
HTML
cat > "$work/res/conclusion.html" <<HTML
<html><head><meta charset="utf-8">$style</head><body>
<img src="$icon" width="72" height="72" alt="" style="float:left;margin:2px 18px 8px 0">
<h1><b>b</b><span>-</span>rig is ready</h1>
<p>The app is in Applications: open <b>b-rig</b> and choose your audio interface in <code>setup</code>.</p>
<p>The plugins you picked are in <code>/Library/Audio/Plug-Ins</code>: Audio Unit in <code>Components</code>, VST3 in <code>VST3</code>. Your DAW lists them under <b>b-suite</b>; if it was open, rescan the plugins or restart it. The b-rig plugin uses the same presets as the app.</p>
<p class="dim">To add or remove plugins later, run this installer again or delete them from those folders.</p>
</body></html>
HTML
cp "$root/LICENSE" "$work/res/LICENSE"
cp "$root/tools/installer/resources/background.png" "$root/tools/installer/resources/background-dark.png" "$work/res/"

pkg="$out/b-rig-$version-macos-arm64.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" --package-path "$work/pkgs" "$pkg"
echo "$pkg"

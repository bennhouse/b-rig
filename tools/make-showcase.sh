#!/bin/bash
# Vetrina della suite (docs/b-suite-showcase.png, 3840 x 2160): le schermate dei test di ogni plugin, in ordine di
# catena, incastrate su uno sfondo che sfuma dall'alto verso il basso. Prima compila i plugin e lancia i loro test
# (scrivono le schermate in <plugin>/build), poi lo strumento BSuiteLogo di b-rig.
set -euo pipefail
cd "$(dirname "$0")/.."
tool=b-rig/build/BSuiteLogo_artefacts/Release/BSuiteLogo
[[ -x "$tool" ]] || { echo "manca $tool: compila b-rig (target BSuiteLogo)" >&2; exit 1; }
shots=(
    b-drop/build/b-drop.png
    b-focus/build/gui_bfocus.png
    b-freshstrings/build/b-freshstrings.png
    b-tight/build/gui_btight.png
    b-wire/build/gui_bwire.png
    b-starve/build/gui_bstarve.png
    b-distort/build/gui_bdistort.png
    b-amp/build/gui_bamp_2.png
    b-blend/build/gui_bblend.png
    b-double/build/gui_bdouble.png
    b-aftercab/build/gui_stereo.png
    b-dynaverb/build/b-dynaverb.png
    b-delay/build/b-delay.png
    b-verb/build/b-verb.png
    b-space/build/b-space.png
    b-scenery/build/gui_bscenery.png
    b-adapt/build/gui_badapt.png
    b-meter/build/gui_bmeter_spec.png
    b-synthesia/build/gui_bsyn_tunnel.png
    b-rig/build/gui_brig_default.png
)
"$tool" --showcase docs/b-suite-showcase.png 3840 2160 "${shots[@]}"

#pragma once
// Colori di accento di tutta la suite, in un solo posto. Li usano i plugin (CMakeLists: SMP_ACCENT_ARGB=BSUITE_ACCENT_...),
// i moduli di b-rig (Mod_*.cpp) e la libreria e la catena dell'app (Modules.cpp).
// Divisi per ruolo nella catena, pensati per lo sfondo #0D0D0F:
//   PRE / DI       tecnici, freddi, precisi
//   CORE / AMP     caldi, incandescenti, saturi
//   POST / SPACE   atmosferici: acqua, aria
//   MASTER / UTIL  neon, analisi digitale
// Solo macro (nessun simbolo): si può includere anche dentro i namespace dei moduli di b-rig.

// PRE / DI
#define BSUITE_ACCENT_FOCUS        0xff3b82f6   // ice blue
#define BSUITE_ACCENT_DROP         0xff8b5cf6   // deep violet
#define BSUITE_ACCENT_COMP         0xff84cc16   // acid lime
#define BSUITE_ACCENT_FRESHSTRINGS 0xfff59e0b   // amber
// CORE / AMP
#define BSUITE_ACCENT_AMP          0xffe11d48   // crimson
#define BSUITE_ACCENT_DISTORT      0xffef4444   // vermilion red
#define BSUITE_ACCENT_STARVE       0xfffb7185   // coral pink
#define BSUITE_ACCENT_WIRE         0xff6366f1   // electric indigo
#define BSUITE_ACCENT_BLEND        0xff14b8a6   // teal
// POST / SPACE
#define BSUITE_ACCENT_AFTERCAB     0xffeab308   // golden yellow
#define BSUITE_ACCENT_DELAY        0xff60a5fa   // cobalt blue
#define BSUITE_ACCENT_DYNAVERB     0xff06b6d4   // deep cyan
#define BSUITE_ACCENT_VERB         0xff22d3ee   // soft turquoise
#define BSUITE_ACCENT_SPACE        0xffa78bfa   // lavender
// MASTER / UTIL
#define BSUITE_ACCENT_METER        0xff38bdf8   // sky blue
#define BSUITE_ACCENT_ADAPT        0xffd946ef   // neon magenta
// solo plugin (non sono moduli di b-rig)
#define BSUITE_ACCENT_DOUBLE       0xff22d3ee
#define BSUITE_ACCENT_SCENERY      0xfff1f5f9
#define BSUITE_ACCENT_SYNTHESIA    0xffe2e8f0
// l'app
#define BSUITE_ACCENT_RIG          0xffe9d8a6

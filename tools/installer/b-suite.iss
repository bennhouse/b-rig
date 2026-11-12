; Installer Windows della suite (Inno Setup 6.3+): i VST3 di tutti i plugin, uno per componente (si possono
; togliere), in C:\Program Files\Common Files\VST3\b-suite. Lo compila la CI:
;   ISCC /DAppVersion=0.1.0 /DArch=x64 /DSourceDir=cartella\con\i\vst3 /DOutputDir=dist b-suite.iss
; Arch: x64 oppure arm64. La lista dei plugin viene da tools/installer/plugins.txt (rigenerare se cambia).

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef SourceDir
  #error SourceDir non definito
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif
#if Arch == "arm64"
  #define ArchAllowed "arm64"
#else
  #define ArchAllowed "x64os"
#endif

[Setup]
AppId={{3F8E2B71-9C4D-4A62-B0E5-7D1C6A2F9B48}
AppName=b-suite
AppVersion={#AppVersion}
AppVerName=b-suite {#AppVersion}
AppPublisher=b-suite
DefaultDirName={commoncf64}\VST3\b-suite
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed={#ArchAllowed}
ArchitecturesInstallIn64BitMode={#ArchAllowed}
OutputDir={#OutputDir}
OutputBaseFilename=b-suite-{#AppVersion}-windows-{#Arch}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE
UninstallDisplayName=b-suite plugins

[Types]
Name: "full"; Description: "All plugins"
Name: "custom"; Description: "Choose the plugins"; Flags: iscustom

[Components]
Name: "drop"; Description: "b-drop"; Types: full custom
Name: "focus"; Description: "b-focus"; Types: full custom
Name: "freshstrings"; Description: "b-freshstrings"; Types: full custom
Name: "tight"; Description: "b-tight"; Types: full custom
Name: "wire"; Description: "b-wire"; Types: full custom
Name: "starve"; Description: "b-starve"; Types: full custom
Name: "distort"; Description: "b-distort"; Types: full custom
Name: "amp"; Description: "b-amp"; Types: full custom
Name: "blend"; Description: "b-blend"; Types: full custom
Name: "double"; Description: "b-double"; Types: full custom
Name: "aftercab"; Description: "b-aftercab"; Types: full custom
Name: "dynaverb"; Description: "b-dynaverb"; Types: full custom
Name: "delay"; Description: "b-delay"; Types: full custom
Name: "verb"; Description: "b-verb"; Types: full custom
Name: "space"; Description: "b-space"; Types: full custom
Name: "scenery"; Description: "b-scenery"; Types: full custom
Name: "adapt"; Description: "b-adapt"; Types: full custom
Name: "meter"; Description: "b-meter"; Types: full custom
Name: "synthesia"; Description: "b-synthesia"; Types: full custom

[Files]
Source: "{#SourceDir}\b-drop.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-drop.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: drop
Source: "{#SourceDir}\b-focus.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-focus.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: focus
Source: "{#SourceDir}\b-freshstrings.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-freshstrings.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: freshstrings
Source: "{#SourceDir}\b-tight.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-tight.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: tight
Source: "{#SourceDir}\b-wire.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-wire.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: wire
Source: "{#SourceDir}\b-starve.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-starve.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: starve
Source: "{#SourceDir}\b-distort.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-distort.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: distort
Source: "{#SourceDir}\b-amp.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-amp.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: amp
Source: "{#SourceDir}\b-blend.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-blend.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: blend
Source: "{#SourceDir}\b-double.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-double.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: double
Source: "{#SourceDir}\b-aftercab.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-aftercab.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: aftercab
Source: "{#SourceDir}\b-dynaverb.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-dynaverb.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: dynaverb
Source: "{#SourceDir}\b-delay.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-delay.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: delay
Source: "{#SourceDir}\b-verb.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-verb.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: verb
Source: "{#SourceDir}\b-space.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-space.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: space
Source: "{#SourceDir}\b-scenery.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-scenery.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: scenery
Source: "{#SourceDir}\b-adapt.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-adapt.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: adapt
Source: "{#SourceDir}\b-meter.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-meter.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: meter
Source: "{#SourceDir}\b-synthesia.vst3\*"; DestDir: "{commoncf64}\VST3\b-suite\b-synthesia.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: synthesia

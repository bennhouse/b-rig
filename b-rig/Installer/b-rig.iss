; Installer Windows di b-rig (Inno Setup 6.3+). Come il .pkg di macOS (tools/installer/make-pkg.sh) ha tre righe:
;   b-rig standalone   l'app in C:\Program Files\b-suite\b-rig (spuntata di default)
;   b-rig plugin       b-rig dentro la DAW (VST3)
;   b-suite plugins    tutti i plugin della suite (VST3)
; I VST3 vanno in C:\Program Files\Common Files\VST3\b-suite, quindi serve l'amministratore.
; Lo compila tools/release-windows.ps1 (o la CI) da una cartella già preparata:
;   <StageDir>\b-rig.exe   <StageDir>\rig\b-rig.vst3   <StageDir>\suite\<plugin>.vst3 ...
;   ISCC /DAppVersion=0.4.1 /DArch=x64 /DStageDir=path\to\stage /DOutputDir=dist b-rig.iss
; Arch: x64 oppure arm64 (un installer per architettura). IconFile (facoltativo): l'icona .ico dell'app.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef StageDir
  #error StageDir non definito
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
; stesso AppId degli installer precedenti della sola app: la nuova versione li aggiorna
AppId={{6B3C1E0A-4F7D-4B8E-9C2A-B5E1D7A9C301}
AppName=b-rig
AppVersion={#AppVersion}
AppVerName=b-rig {#AppVersion}
AppPublisher=b-suite
AppPublisherURL=https://github.com/bennhouse/b-rig
DefaultDirName={autopf}\b-suite\b-rig
DefaultGroupName=b-suite
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\b-rig.exe
ArchitecturesAllowed={#ArchAllowed}
ArchitecturesInstallIn64BitMode={#ArchAllowed}
; i VST3 vanno nella cartella di sistema dei plugin
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=b-rig-{#AppVersion}-windows-{#Arch}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE
#ifdef IconFile
SetupIconFile={#IconFile}
#endif

[Types]
Name: "app"; Description: "b-rig standalone"
Name: "everything"; Description: "b-rig standalone, b-rig plugin and b-suite plugins"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "app"; Description: "b-rig standalone: the whole chain in one window, with tuner, metronome and presets"; Types: app everything custom
Name: "rig"; Description: "b-rig plugin (VST3): b-rig inside your DAW, with the same presets as the app"; Types: everything
Name: "suite"; Description: "b-suite plugins (VST3): every effect of the suite as its own plugin"; Types: everything

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: app; Flags: unchecked

[Files]
Source: "{#StageDir}\b-rig.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app
Source: "{#StageDir}\rig\*"; DestDir: "{commoncf64}\VST3\b-suite"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: rig
Source: "{#StageDir}\suite\*"; DestDir: "{commoncf64}\VST3\b-suite"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: suite

[Icons]
Name: "{autoprograms}\b-rig"; Filename: "{app}\b-rig.exe"; Components: app
Name: "{autodesktop}\b-rig"; Filename: "{app}\b-rig.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\b-rig.exe"; Description: "{cm:LaunchProgram,b-rig}"; Components: app; Flags: nowait postinstall skipifsilent runasoriginaluser

; Installer Windows di b-rig (Inno Setup 6.3+). Lo compila la CI:
;   ISCC /DAppVersion=0.1.0 /DArch=x64 /DSourceExe=path\to\b-rig.exe /DOutputDir=dist b-rig.iss
; Arch: x64 oppure arm64 (un installer per architettura).

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef SourceExe
  #error SourceExe non definito
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
AppId={{6B3C1E0A-4F7D-4B8E-9C2A-B5E1D7A9C301}
AppName=b-rig
AppVersion={#AppVersion}
AppVerName=b-rig {#AppVersion}
AppPublisher=b-suite
DefaultDirName={autopf}\b-suite\b-rig
DefaultGroupName=b-suite
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\b-rig.exe
ArchitecturesAllowed={#ArchAllowed}
ArchitecturesInstallIn64BitMode={#ArchAllowed}
OutputDir={#OutputDir}
OutputBaseFilename=b-rig-{#AppVersion}-windows-{#Arch}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "b-rig.exe"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\b-rig"; Filename: "{app}\b-rig.exe"
Name: "{autodesktop}\b-rig"; Filename: "{app}\b-rig.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\b-rig.exe"; Description: "{cm:LaunchProgram,b-rig}"; Flags: nowait postinstall skipifsilent

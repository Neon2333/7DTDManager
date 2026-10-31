; SevenDaysManager - Inno Setup 6 packaging script
; Usage:
;   1. Build Release: cmake --build build --config Release
;   2. Deploy Qt and VC++ runtime to dist\app (see README "Packaging")
;   3. Compile: D:\Inno Setup 6\ISCC.exe installer\SevenDaysManager.iss

#define MyAppName "7 Days to Die Version & Save Manager"
#define MyAppVersion "0.1"
#define MyAppExeName "SevenDaysManager.exe"

[Setup]
; AppId identifies the product for upgrades/uninstall; do not change it.
AppId={{3BE2427A-B555-4E8D-88A6-DB81E13E099E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoVersion=0.1.0.0
DefaultDirName={localappdata}\Programs\7DTDManager
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user install: no UAC prompt, installs under %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist\installer
OutputBaseFilename=SevenDaysManager-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; dist\app must contain SevenDaysManager.exe, Qt DLLs/plugins and VC++ runtime.
Source: "..\dist\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

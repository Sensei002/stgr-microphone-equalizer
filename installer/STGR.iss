; STGR Microphone Equalizer - Inno Setup installer script.
; Built in CI with Inno Setup 6 (iscc) which is preinstalled on the
; GitHub Actions windows-latest runner.

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

#define MyAppName "STGR Microphone Equalizer"
#define MyAppPublisher "STGR"
#define MyAppExeName "STGRMicrophoneEqualizer.exe"
#define MyAppAssocName MyAppName + " File"

[Setup]
AppId={{5F1A2B3C-4D5E-4F6A-8B7C-9D0E1F2A3B4C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\STGR
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
OutputDir=dist
OutputBaseFilename=STGR-Microphone-Equalizer-{#MyAppVersion}-x64
SetupIconFile=..\assets\icons\installer.ico
WizardImageFile=..\assets\branding\installer-wizard.bmp
WizardSmallImageFile=..\assets\branding\installer-small.bmp
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
LicenseFile=..\LICENSE
; The APO DLL must be signed for production use; see docs/INSTALLATION.md.
; The CI produces unsigned binaries (tests/dev builds are fine).

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startwithwindows"; Description: "Start STGR with Windows"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
Source: "..\build\bin\Release\STGRMicrophoneEqualizer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\STGRTray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\STGRAudioServer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\STGRAdmin.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\STGRScan.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\stgr_apo.dll"; DestDir: "{app}"; Flags: ignoreversion regserver
Source: "..\build\bin\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs
Source: "..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\assets\branding\stgr-logo.png"; DestDir: "{app}\assets"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Start with Windows (user-level Run key).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "STGRTray"; ValueData: """{app}\STGRTray.exe"""; Tasks: startwithwindows
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "STGRAudioServer"; ValueData: """{app}\STGRAudioServer.exe"""; Tasks: startwithwindows

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\STGRAdmin.exe"; Parameters: "--unregister-apo"; Flags: runhidden waituntilterminated

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    if MsgBox('Remove STGR microphone processing from all microphones?', mbConfirmation, MB_YESNO) = IDYES then
      Exec(ExpandConstant('{app}\STGRAdmin.exe'), '--unregister-apo', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

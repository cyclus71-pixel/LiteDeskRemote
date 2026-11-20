; litedesk.iss — installer Inno Setup untuk LiteDesk Remote (FASE 8 opsional).
; Dijalankan oleh scripts/package.ps1 -Installer (butuh Inno Setup 6).
; Kompilasi manual: ISCC.exe /DAppVersion=0.1.0 installer\litedesk.iss

#ifndef AppVersion
#define AppVersion "0.1.0"
#endif

#define AppName "LiteDesk Remote"
#define StageDir "..\dist\LiteDeskRemote-Portable-" + AppVersion

[Setup]
AppId={{7A5C2E1F-4B8D-4E3A-9C21-A1B2C3D4E5F6}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=LiteDesk Remote
DefaultDirName={autopf}\LiteDeskRemote
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\ldhost.exe
OutputDir=Output
OutputBaseFilename=LiteDeskRemote-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupIconFile=..\assets\litedesk.ico

[Languages]
Name: "indonesian"; MessagesFile: "compiler:Languages\Indonesian.isl"

[Files]
Source: "{#StageDir}\ldhost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ldviewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\LiteDesk Host"; Filename: "{app}\ldhost.exe"
Name: "{group}\LiteDesk Viewer"; Filename: "{app}\ldviewer.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

; Firewall rule opsional (pengguna dapat menonaktifkan centang)
[Run]
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""LiteDesk Host TCP"" dir=in action=allow protocol=TCP localport=48500 profile=private"; Flags: runhidden skipifdoesntexist; Tasks: firewall
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""LiteDesk Discovery UDP"" dir=in action=allow protocol=UDP localport=48501 profile=private"; Flags: runhidden skipifdoesntexist; Tasks: firewall

[Tasks]
Name: "firewall"; Description: "Tambah aturan firewall untuk koneksi LAN (profil Private)"; Check: IsAdmin

[UninstallRun]
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""LiteDesk Host TCP"""; Flags: runhidden; RunOnceId: "DelFw1"
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""LiteDesk Discovery UDP"""; Flags: runhidden; RunOnceId: "DelFw2"

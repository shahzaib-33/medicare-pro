; Inno Setup script for MediCare Pro (Windows 10/11, 64-bit)
; Build with: iscc installer\MediCarePro.iss  (after running build_windows.bat)

#define AppName "MediCare Pro"
#define AppVersion "1.0.0"
#define Publisher "SoftzenAgency"
#define DeployDir "..\deploy"

[Setup]
AppId={{6E0B7C1E-4E8B-4C55-9C1A-3B7D0B6C2F11}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\SoftzenAgency\MediCare Pro
DefaultGroupName={#AppName}
OutputDir=..\dist
OutputBaseFilename=MediCare_Pro_Setup
SetupIconFile=..\resources\icons\medicare.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Per user data lives in %LOCALAPPDATA%, so no admin rights are needed to run the app.
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=yes
UninstallDisplayIcon={app}\MediCarePro.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

; Your pharmacy data in %LOCALAPPDATA%\SoftzenAgency\MediCarePro is never removed by uninstall or update.

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\resources\fonts\OFL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\resources\icons\medicare.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\icons\NOTICE_emoji.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\resources\icons\ui\LICENSE_lucide.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\MediCarePro.exe"; IconFilename: "{app}\medicare.ico"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\MediCarePro.exe"; IconFilename: "{app}\medicare.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\MediCarePro.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; After a silent update, start the app again so it can confirm the new version.
Filename: "{app}\MediCarePro.exe"; Flags: nowait; Check: WizardSilent

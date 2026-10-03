; LuxDim 1.3-beta - Inno Setup installer script (bilingual RU/EN)
; Build: ISCC.exe installer.iss

#define MyAppName      "LuxDim"
#define MyAppVersion   "1.3-beta"
#define MyAppPublisher "LuxDim"
#define MyAppExeName   "luxdim.exe"
#define MyAppId        "e6b02827-e9af-4cba-be36-743b92ed6551"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Auto-detect OS language: Russian locale -> Russian, anything else -> English
ShowLanguageDialog=auto
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
OutputDir=output
OutputBaseFilename=LuxDim_Setup
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=luxdim.ico

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
en.CreateDesktopIcon=Create a &desktop icon
ru.CreateDesktopIcon=Создать значок на &Рабочем столе
en.AutoStartTask=Launch {#MyAppName} at Windows &startup
ru.AutoStartTask=Запускать программу при старте &Windows
en.AutoStartGroup=Autostart:
ru.AutoStartGroup=Автозагрузка:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
Name: "autostart";  Description: "{cm:AutoStartTask}";      GroupDescription: "{cm:AutoStartGroup}";  Flags: checkedonce

[Files]
Source: "{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Autostart task: LuxDim reads this HKCU Run value itself (Settings -> "Launch at Windows startup")
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// The app normally sits in the tray, which locks luxdim.exe.
// Kill any running instance before installing or uninstalling,
// otherwise file operations fail with "Access denied" and the
// uninstaller leaves the exe behind.
procedure KillRunningInstance();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#MyAppExeName}',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(500);
end;

function InitializeSetup(): Boolean;
begin
  KillRunningInstance();
  Result := True;
end;

function InitializeUninstall(): Boolean;
begin
  KillRunningInstance();
  Result := True;
end;

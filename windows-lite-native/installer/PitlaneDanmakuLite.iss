#define AppName "Pitlane Danmaku Lite"
#define AppVersion "0.1.0-lite"
#define AppPublisher "FoxBai"
#define AppExeName "pitlane_lite.exe"

[Setup]
AppId={{D1D89D40-BAE5-4FE1-8B67-87ACCD6AC6E7}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\PitlaneDanmakuLite
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=output
OutputBaseFilename=PitlaneDanmakuLite-Lite-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "在桌面创建快捷方式"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
Source: "..\dist\PitlaneDanmakuLite\pitlane_lite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\PitlaneDanmakuLite\README-lite.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\PitlaneDanmakuLite\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\PitlaneDanmakuLite\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent

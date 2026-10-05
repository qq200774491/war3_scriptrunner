#ifndef AppVersion
  #error AppVersion must be supplied by package_installer.ps1
#endif

[Setup]
AppId={{B57628C4-D781-4A35-9D12-5AE5C43108F5}
AppName=ScriptRoom
AppVersion={#AppVersion}
AppPublisher=ScriptRoom
DefaultDirName={localappdata}\Programs\ScriptRoom
DefaultGroupName=ScriptRoom
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0
OutputBaseFilename=ScriptRoom-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
AllowNoIcons=yes
UninstallDisplayIcon={app}\ScriptRoom.exe
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=no
UsePreviousLanguage=no
LanguageDetectionMethod=none

[Languages]
Name: "chinesesimplified"; MessagesFile: "{#ProjectRoot}\third_party\inno\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

[Files]
#include FileList
Source: "{#ProjectRoot}\本地测试配置.example.json"; DestDir: "{app}"; DestName: "本地测试配置.json"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\ScriptRoom"; Filename: "{app}\ScriptRoom.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ScriptRoom"; Filename: "{app}\ScriptRoom.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ScriptRoom.exe"; Description: "启动 ScriptRoom"; Flags: nowait postinstall skipifsilent

; Inno Setup Script for obs-midikeyboard plugin
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
; Build: iscc /dBuildConfig=RelWithDebInfo setup.iss

#define MyAppName "obs-midikeyboard"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "obs-midikeyboard"
#define MyAppURL "https://github.com/hack1exe/obs-midikeyboard"
#ifndef BuildConfig
#define BuildConfig "RelWithDebInfo"
#endif

[Setup]
AppId={{F9A8B3C2-D5E6-4F7A-8B1C-2D3E4F5A6B7C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={code:GetOBSInstallDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\build_x64\installer
OutputBaseFilename=obs-midikeyboard-{#MyAppVersion}-windows-x64
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\obs-plugins\64bit\obs-midikeyboard.dll
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
; Plugin DLL
Source: "..\release\{#BuildConfig}\obs-midikeyboard\bin\64bit\obs-midikeyboard.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion

; Locale files
Source: "..\release\{#BuildConfig}\obs-midikeyboard\data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\obs-midikeyboard\locale"; Flags: ignoreversion
Source: "..\release\{#BuildConfig}\obs-midikeyboard\data\locale\ru-RU.ini"; DestDir: "{app}\data\obs-plugins\obs-midikeyboard\locale"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Code]
function GetOBSInstallDir(Param: string): string;
var
  InstallPath: string;
begin
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1', 'InstallLocation', InstallPath) or
     RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio_is1', 'InstallLocation', InstallPath) or
     RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', 'InstallLocation', InstallPath) then
  begin
    Result := InstallPath;
  end
  else
  begin
    if IsWin64 then
      Result := ExpandConstant('{pf64}\obs-studio')
    else
      Result := ExpandConstant('{pf32}\obs-studio');
  end;
end;

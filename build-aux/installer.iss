; InnoSetup installer script for OBS MIDI Keyboard plugin
; Defines passed via iscc.exe /D from Package-Windows.ps1

#ifndef PluginName
  #define PluginName "obs-midikeyboard"
#endif
#ifndef MyAppVersion
  #define MyAppVersion "1.0.1"
#endif
#ifndef MyAppSourceDir
  #define MyAppSourceDir "."
#endif
#ifndef MyAppOutputDir
  #define MyAppOutputDir "."
#endif

#define MyAppName "MIDI Piano Keyboard"
#define MyAppPublisher "axiomsound"
#define MyAppURL "https://github.com/axiomsound/obs-midikeyboard"

[Setup]
AppId={{F9A8B3C2-D5E6-4F7A-8B1C-2D3E4F5A6B7C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={code:GetOBSInstallDir}
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir={#MyAppOutputDir}
OutputBaseFilename=obs-midikeyboard-{#MyAppVersion}-windows-x64
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\obs-plugins\64bit\{#PluginName}.dll
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "compact"; Description: "Compact installation (without PDB symbols)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "plugin"; Description: "Plugin files"; Types: full compact custom; Flags: fixed
Name: "symbols"; Description: "Debug symbols (PDB)"; Types: custom

[Files]
Source: "{#MyAppSourceDir}\{#PluginName}\bin\64bit\{#PluginName}.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion; Components: plugin
Source: "{#MyAppSourceDir}\{#PluginName}\data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\{#PluginName}\locale"; Flags: ignoreversion; Components: plugin
Source: "{#MyAppSourceDir}\{#PluginName}\data\locale\ru-RU.ini"; DestDir: "{app}\data\obs-plugins\{#PluginName}\locale"; Flags: ignoreversion; Components: plugin
Source: "{#MyAppSourceDir}\{#PluginName}\bin\64bit\{#PluginName}.pdb"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion; Components: symbols

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

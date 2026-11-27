#ifndef BuildVersion
  #define BuildVersion "1.1.0"
#endif
[Setup]
AppId=ArtisanAxis.XuanDieuCNC
AppName=ArtisanAxis CAM Pro
AppPublisher=Xuan Dieu CNC
SetupIconFile=artisanaxis.ico
VersionInfoProductName=ArtisanAxis CAM Pro
AppVersion={#BuildVersion}
VersionInfoVersion={#BuildVersion}
DefaultDirName={localappdata}\Programs\ArtisanAxis
DefaultGroupName=ArtisanAxis CAM Pro
OutputDir=..\dist
OutputBaseFilename=Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\ArtisanAxis.exe
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vcredist\*"
Source: "{#StageDir}\vcredist\vc_redist.x64.exe"; Flags: dontcopy
[Icons]
Name: "{autodesktop}\ArtisanAxis CAM Pro"; Filename: "{app}\ArtisanAxis.exe"; IconFilename: "{app}\ArtisanAxis.exe"
Name: "{group}\ArtisanAxis CAM Pro"; Filename: "{app}\ArtisanAxis.exe"; IconFilename: "{app}\ArtisanAxis.exe"

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Redist, InstalledRuntime: String;
  InstalledVersion, RequiredVersion: Int64;
  Code: Integer;
begin
  Result := '';
  ExtractTemporaryFile('vc_redist.x64.exe');
  Redist := ExpandConstant('{tmp}\vc_redist.x64.exe');
  InstalledRuntime := ExpandConstant('{sys}\msvcp140.dll');
  if GetPackedVersion(InstalledRuntime, InstalledVersion) and
     GetPackedVersion(Redist, RequiredVersion) and
     (ComparePackedVersion(InstalledVersion, RequiredVersion) >= 0) and
     FileExists(ExpandConstant('{sys}\vcruntime140_1.dll')) then
    Exit;
  if not ShellExec('runas', Redist, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Result := 'Khong mo duoc Microsoft Visual C++ Runtime: ' + SysErrorMessage(Code)
  else if (Code = 3010) or (Code = 1641) then
    NeedsRestart := True
  else if (Code <> 0) and (Code <> 1638) then
    Result := 'Microsoft Visual C++ Runtime: ' + IntToStr(Code);
end;

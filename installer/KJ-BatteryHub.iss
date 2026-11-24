; Inno Setup script for KJ-BatteryHub (https://jrsoftware.org/isinfo.php).
; Per-user install by default: no administrator rights needed, installs to %LOCALAPPDATA%\Programs.
; Setup also offers "Install for all users" (Program Files, needs administrator rights).
; Build with installer\build-installer.ps1 (or: ISCC /DAppVersion=1.2.3 installer\KJ-BatteryHub.iss).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."
#define AppExe "KJ-BatteryHub.exe"

[Setup]
; Same AppId as every earlier version, so this replaces the installed copy and its uninstall entry.
AppId={{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}
AppName=KJ-BatteryHub
AppVersion={#AppVersion}
AppVerName=KJ-BatteryHub {#AppVersion}
AppPublisher=Kjerne
AppPublisherURL=https://github.com/Kjerne/KJ-BatteryHub
AppSupportURL=https://github.com/Kjerne/KJ-BatteryHub/issues
AppUpdatesURL=https://github.com/Kjerne/KJ-BatteryHub/releases
AppCopyright=KJ-BatteryHub by Kjerne, a fork of BatteryHub by krisgfx9 and contributors. GNU GPL v3.
; Up to 1.2.x the folder was "BatteryHub" or "Wireless Battery". Upgrades move to the new folder and
; remove the old one (RemoveOldInstall below).
DefaultDirName={autopf}\KJ-BatteryHub
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=KJ-BatteryHub-Setup-{#AppVersion}
SetupIconFile={#Root}\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=KJ-BatteryHub
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
; English, or Simplified Chinese on a Chinese Windows (PR #43); no language prompt.
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "Languages\ChineseSimplified.isl"

[CustomMessages]
english.AutostartTask=Start KJ-BatteryHub when I sign in to Windows
chinesesimp.AutostartTask=登录 Windows 时启动 KJ-BatteryHub
english.UpdatesTask=Check for updates automatically (once a day, from GitHub)
chinesesimp.UpdatesTask=自动检查更新（每天一次，来自 GitHub）
english.DesktopTask=Create a desktop shortcut
chinesesimp.DesktopTask=创建桌面快捷方式
english.StartNow=Start KJ-BatteryHub now
chinesesimp.StartNow=立即启动 KJ-BatteryHub
english.CouldNotClose=KJ-BatteryHub could not close. Close it and try updating again.
chinesesimp.CouldNotClose=无法关闭 KJ-BatteryHub。请关闭它后再试一次更新。

[Tasks]
Name: "autostart"; Description: "{cm:AutostartTask}"
Name: "autoupdates"; Description: "{cm:UpdatesTask}"
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked

[Files]
Source: "{#Root}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\third_party\headsetcontrol\headsetcontrol.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\installer\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Exes of earlier versions, should someone install into the old folder on purpose.
Type: files; Name: "{app}\WirelessBattery.exe"
Type: files; Name: "{app}\BatteryHub.exe"
; Start menu shortcuts of earlier versions (before 1.1.0, and up to 1.1.2).
Type: files; Name: "{autoprograms}\Wireless Battery.lnk"
Type: files; Name: "{autoprograms}\BatteryHub.lnk"

[Icons]
Name: "{autoprograms}\KJ-BatteryHub"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\KJ-BatteryHub"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:StartNow}"; Flags: nowait postinstall skipifsilent; Check: not IsUpdate

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitApp"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'KJ-BatteryHub';
  OldRunValue = 'WirelessBattery';  { up to 1.2.x }
  SettingsKey = 'Software\KJ-BatteryHub';
  OldSettingsKey = 'Software\WirelessBattery';

var
  OldDir: String;  { the folder of the copy this replaces, when it is not the new one }

function IsUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:WBUPDATE|0}') = '1';
end;

function SameDir(A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(A), RemoveBackslashUnlessRoot(B)) = 0;
end;

function Quoted(Path: String): String;
begin
  Result := '"' + Path + '"';
end;

{ Any version's exe closes the running copy with --quit (all versions share the window class). }
function QuitWith(Exe: String; var Ok: Boolean): Boolean;
var
  Code: Integer;
begin
  Result := FileExists(Exe);
  if Result then
    Ok := Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

function QuitRunningApp(): Boolean;
var
  Dirs: array[0..1] of String;
  I: Integer;
  Ok: Boolean;
begin
  Result := True;
  Ok := True;
  Dirs[0] := ExpandConstant('{app}');
  Dirs[1] := OldDir;
  for I := 0 to 1 do
    if Dirs[I] <> '' then
      if QuitWith(AddBackslash(Dirs[I]) + '{#AppExe}', Ok) or
         QuitWith(AddBackslash(Dirs[I]) + 'BatteryHub.exe', Ok) or
         QuitWith(AddBackslash(Dirs[I]) + 'WirelessBattery.exe', Ok) then
      begin
        Result := Ok;
        exit;
      end;
end;

{ The previous installation, if it was in another folder (always, when upgrading from 1.2.x). }
procedure FindOldDir();
begin
  OldDir := WizardForm.PrevAppDir;
  if (OldDir <> '') and SameDir(OldDir, ExpandConstant('{app}')) then
    OldDir := '';
end;

{ Only the files earlier versions installed; the folder goes only if nothing else is in it. }
procedure RemoveOldInstall();
var
  D: String;
begin
  if (OldDir = '') or not DirExists(OldDir) then
    exit;
  D := AddBackslash(OldDir);
  DeleteFile(D + 'BatteryHub.exe');
  DeleteFile(D + 'WirelessBattery.exe');
  DeleteFile(D + 'headsetcontrol.exe');
  DeleteFile(D + 'LICENSE.txt');
  DeleteFile(D + 'THIRD_PARTY_NOTICES.txt');
  DelTree(D + 'unins*.*', False, True, False);  { its uninstaller: the new folder has its own now }
  RemoveDir(OldDir);
end;

function InstalledRunValue(): String;
begin
  Result := Quoted(ExpandConstant('{app}\{#AppExe}'));
end;

{ True if a startup entry starts this installation (any version's exe, old or new folder). A portable
  copy's "Start with Windows" entry points elsewhere and is left alone. }
function StartsThisCopy(Value: String): Boolean;
var
  Dirs: array[0..1] of String;
  I: Integer;
begin
  Result := False;
  Dirs[0] := ExpandConstant('{app}');
  Dirs[1] := OldDir;
  for I := 0 to 1 do
    if Dirs[I] <> '' then
      if (CompareText(Value, Quoted(AddBackslash(Dirs[I]) + '{#AppExe}')) = 0) or
         (CompareText(Value, Quoted(AddBackslash(Dirs[I]) + 'BatteryHub.exe')) = 0) or
         (CompareText(Value, Quoted(AddBackslash(Dirs[I]) + 'WirelessBattery.exe')) = 0) then
        Result := True;
end;

procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, RunValue, Value) and StartsThisCopy(Value) then
    RegDeleteValue(HKCU, RunKey, RunValue);
  if RegQueryStringValue(HKCU, RunKey, OldRunValue, Value) and StartsThisCopy(Value) then
    RegDeleteValue(HKCU, RunKey, OldRunValue);
end;

{ A 1.2.x entry of this copy becomes the new entry, pointing at the new exe (also on silent updates). }
procedure MigrateAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, OldRunValue, Value) and StartsThisCopy(Value) then
  begin
    RegWriteStringValue(HKCU, RunKey, RunValue, InstalledRunValue());
    RegDeleteValue(HKCU, RunKey, OldRunValue);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  FindOldDir();
  if IsUpdate() and not QuitRunningApp() then
    Result := CustomMessage('CouldNotClose');
end;

{ True if this account can create the chosen folder (or write to it if it already exists).
  Without administrator rights, folders such as D:\Programs or Program Files are often read-only. }
function CanWriteTo(Dir: String): Boolean;
var
  Parent, Probe: String;
begin
  Dir := RemoveBackslashUnlessRoot(Dir);
  if DirExists(Dir) then
  begin
    Probe := AddBackslash(Dir) + '~kj-batteryhub-write-test.tmp';
    Result := SaveStringToFile(Probe, '', False);
    DeleteFile(Probe);
    exit;
  end;
  { Find the nearest folder that exists and check that a new folder can be created in it. }
  Parent := Dir;
  while not DirExists(Parent) do
  begin
    if ExtractFileDir(Parent) = Parent then
    begin
      Result := False;  { the drive itself does not exist }
      exit;
    end;
    Parent := ExtractFileDir(Parent);
  end;
  Probe := AddBackslash(Parent) + '~kj-batteryhub-write-test';
  Result := CreateDir(Probe);
  if Result then
    RemoveDir(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  { Not in silent installs: there the wizard would wait on a hidden page forever. }
  if (CurPageID = wpSelectDir) and not WizardSilent() and not CanWriteTo(WizardDirValue()) then
  begin
    if IsAdminInstallMode() then
      MsgBox('Setup cannot create or write to this folder:' + #13#10 + WizardDirValue() + #13#10#13#10 +
        'Please choose a different folder.', mbError, MB_OK)
    else
      MsgBox('Your account cannot create or write to this folder without administrator rights:' + #13#10 +
        WizardDirValue() + #13#10#13#10 +
        'Choose a folder you own (the default one always works), or close Setup, run it again and ' +
        'pick "Install for all users".', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssInstall) and not IsUpdate() then
    QuitRunningApp();  { upgrade: close the old version so its files can be replaced or removed }
  if CurStep = ssPostInstall then
  begin
    MigrateAutostart();
    RemoveOldInstall();
    if not IsUpdate() then
    begin
      { Same value the app's own "Start with Windows" menu item writes, so the two stay in sync. }
      if WizardIsTaskSelected('autostart') then
        RegWriteStringValue(HKCU, RunKey, RunValue, InstalledRunValue())
      else
        RemoveOwnAutostart();
      { The app's "Check for updates automatically" setting. A 1.2.x setting, imported when the app
        first starts, takes precedence. }
      if WizardIsTaskSelected('autoupdates') then
        RegWriteDWordValue(HKCU, SettingsKey, 'AutoCheckUpdates', 1)
      else
        RegWriteDWordValue(HKCU, SettingsKey, 'AutoCheckUpdates', 0);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOwnAutostart();
    RegDeleteKeyIncludingSubkeys(HKCU, SettingsKey);
    RegDeleteKeyIncludingSubkeys(HKCU, OldSettingsKey);
  end;
end;

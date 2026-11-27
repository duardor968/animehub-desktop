; AnimeHub for the current user: no administrator, under %LOCALAPPDATA%\Programs. The library and
; settings live in %LOCALAPPDATA%\AnimeHub, so updates keep them; uninstalling asks before
; removing them and never touches the downloaded videos.
;
;   ISCC /DAppVersion=0.4.0 /DSourceDir=<packaged AnimeHub folder> /DOutputDir=<folder> AnimeHub.iss
;
; scripts\package.ps1 prepares the folder and calls this.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<packaged AnimeHub folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

; Read the bundled engines/.pins at compile time so NeedsMpvUpdate can compare the mpv pin
; without extracting files or overwriting {app}\engines\.pins first.
#define NewEnginesPins ""
#if FileExists(SourceDir + "\engines\.pins")
  #define PinsFile FileOpen(SourceDir + "\engines\.pins")
  #if PinsFile
    #define NewEnginesPins FileRead(PinsFile)
    #expr FileClose(PinsFile)
  #endif
#endif

[Setup]
AppId={{D47C98AF-073F-4E55-A850-C8AACADF48F6}
AppName=AnimeHub
AppVersion={#AppVersion}
AppVerName=AnimeHub {#AppVersion}
AppPublisher=AnimeHub
AppPublisherURL=https://github.com/duardor968/animehub-desktop
AppSupportURL=https://github.com/duardor968/animehub-desktop/issues
AppUpdatesURL=https://github.com/duardor968/animehub-desktop/releases
VersionInfoVersion={#AppVersion}
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\AnimeHub
DisableWelcomePage=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Qt 6.11 runs on Windows 10 1809 and later.
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=AnimeHub-{#AppVersion}-setup
SetupIconFile=..\assets\animehub.ico
UninstallDisplayIcon={app}\AnimeHub.exe
UninstallDisplayName=AnimeHub
WizardStyle=modern dynamic
Compression=lzma2/max
SolidCompression=yes
; Close AnimeHub and aria2 through Restart Manager, never an open mpv video player.
CloseApplications=yes
CloseApplicationsFilter=AnimeHub.exe,Qt6Core.dll,aria2c.exe
RestartApplications=no

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; Flags: unchecked
Name: "startup"; Description: "Abrir AnimeHub en la bandeja al iniciar Windows"; Flags: unchecked

[InstallDelete]
; What a previous version deployed and this one may not: Qt's plugins, QML, aria2, and updater
; staging. Keep {app}\engines\mpv untouched unless its pin actually changed (see NeedsMpvUpdate).
Type: filesandordirs; Name: "{app}\.update"
Type: filesandordirs; Name: "{app}\engines\aria2"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: files; Name: "{app}\*.dll"

[Files]
; Main application files (including AnimeHubUpdater.exe when staged), excluding engines\mpv and
; engines\.pins which are handled below to avoid disturbing a running mpv.exe.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\engines\mpv\*,\engines\.pins"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\engines\mpv\*"; DestDir: "{app}\engines\mpv"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: NeedsMpvUpdate
Source: "{#SourceDir}\engines\.pins"; DestDir: "{app}\engines"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\AnimeHub"; Filename: "{app}\AnimeHub.exe"
Name: "{autodesktop}\AnimeHub"; Filename: "{app}\AnimeHub.exe"; Tasks: desktopicon

[Registry]
; The same entry AnimeHub writes from Ajustes -> General -> Al iniciar Windows.
; Guarded in [Code] so upgrades always honor the live state from Settings instead of stale tasks.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "AnimeHub"; ValueData: """{app}\AnimeHub.exe"" --tray"; Check: ShouldInstallStartupEntry

[Run]
Filename: "{app}\AnimeHub.exe"; Description: "Abrir AnimeHub"; Flags: nowait postinstall skipifsilent
; Silent in-app updater fallback: re-launch only when explicitly requested via /LAUNCH=1.
Filename: "{app}\AnimeHub.exe"; Flags: nowait skipifnotsilent; Check: ShouldLaunchSilent

[UninstallDelete]
; Remove post-update files added by the in-app updater, its staging folder, and renamed mpv backups.
Type: filesandordirs; Name: "{app}\.update"
Type: filesandordirs; Name: "{app}\engines"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\AnimeHubUpdater.exe"
Type: files; Name: "{app}\AnimeHub.exe.old"
Type: files; Name: "{app}\AnimeHubUpdater.exe.old"
Type: dirifempty; Name: "{app}"

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{D47C98AF-073F-4E55-A850-C8AACADF48F6}_is1';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  StartupApprovedKey = 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run';
  StartupTaskDesc = 'Abrir AnimeHub en la bandeja al iniciar Windows';
  NewEnginesPins = '{#NewEnginesPins}';

type
  TInstallMode = (imFresh, imUpgrade, imReinstall, imDowngrade);

var
  InstallMode: TInstallMode;
  InstalledVersion: String;
  InstalledDir: String;
  HasLiveAutostart: Boolean;
  LiveAutostartCmd: String;
  StartupTaskSynced: Boolean;
  MpvUpdateNeeded: Boolean;
  MpvBackupDir: String;

function HasCmdSwitch(const Name: String): Boolean;
var
  I: Integer;
  Arg, SwitchExact, SwitchPrefix: String;
begin
  Result := False;
  SwitchExact := Uppercase('/' + Name);
  SwitchPrefix := SwitchExact + '=';
  for I := 1 to ParamCount do
  begin
    Arg := Uppercase(ParamStr(I));
    if (Arg = SwitchExact) or (Pos(SwitchPrefix, Arg) = 1) then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function IsAutostartDisabledInTaskManager(): Boolean;
var
  Data: AnsiString;
begin
  Result := False;
  if RegQueryBinaryValue(HKCU, StartupApprovedKey, 'AnimeHub', Data) and (Length(Data) >= 1) then
    Result := (Ord(Data[1]) and $01) <> 0;
end;

procedure ReadLiveAutostartState();
begin
  LiveAutostartCmd := '';
  HasLiveAutostart := False;
  if RegQueryStringValue(HKCU, RunKey, 'AnimeHub', LiveAutostartCmd) and
     (Trim(LiveAutostartCmd) <> '') and
     (not IsAutostartDisabledInTaskManager()) then
    HasLiveAutostart := True;
end;

function ExtractMpvPin(const Pins: String): String;
var
  Clean: String;
  Sep: Integer;
begin
  Clean := Trim(Pins);
  Sep := Pos(';', Clean);
  if Sep > 0 then
    Result := Copy(Clean, Sep + 1, Length(Clean))
  else
    Result := Clean;
end;

procedure SyncStartupTaskCheckbox();
var
  I: Integer;
begin
  if StartupTaskSynced then
    Exit;
  for I := 0 to WizardForm.TasksList.Items.Count - 1 do
  begin
    if WizardForm.TasksList.ItemCaption[I] = StartupTaskDesc then
    begin
      WizardForm.TasksList.Checked[I] := HasLiveAutostart;
      StartupTaskSynced := True;
      Exit;
    end;
  end;
end;

function FindFreeMpvBackupDir(const EnginesDir: String): String;
var
  Candidate: String;
  Index: Integer;
begin
  Candidate := EnginesDir + '\mpv.old';
  if DirExists(Candidate) then
    DelTree(Candidate, True, True, True);
  if not DirExists(Candidate) then
  begin
    Result := Candidate;
    Exit;
  end;
  Index := 1;
  while DirExists(EnginesDir + '\mpv.old.' + IntToStr(Index)) do
    Index := Index + 1;
  Result := EnginesDir + '\mpv.old.' + IntToStr(Index);
end;

procedure MoveMpvAside(const MpvDir, BackupDir: String);
var
  FindRec: TFindRec;
begin
  if not DirExists(MpvDir) then
    Exit;
  // Renaming the folder works when mpv is idle; if mpv.exe is running, NTFS rejects renaming an
  // ancestor folder but allows moving the individual mapped executables/DLLs into a sibling folder.
  if RenameFile(MpvDir, BackupDir) then
    Exit;
  if not ForceDirectories(BackupDir) then
    Exit;
  if FindFirst(MpvDir + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') and
           ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) then
          RenameFile(MpvDir + '\' + FindRec.Name, BackupDir + '\' + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CleanupStaleMpvBackups(const EnginesDir: String);
var
  FindRec: TFindRec;
begin
  if not DirExists(EnginesDir) then
    Exit;
  if FindFirst(EnginesDir + '\mpv.old*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') and
           ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) then
          DelTree(EnginesDir + '\' + FindRec.Name, True, True, True);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function InitializeSetup(): Boolean;
var
  InstalledPacked, NewPacked: Int64;
  Cmp: Integer;
  DowngradeMsg: String;
begin
  Result := True;
  InstallMode := imFresh;
  InstalledVersion := '';
  InstalledDir := '';
  StartupTaskSynced := False;
  MpvUpdateNeeded := True;
  MpvBackupDir := '';

  ReadLiveAutostartState();

  if RegQueryStringValue(HKCU, UninstallKey, 'DisplayVersion', InstalledVersion) and
     (Trim(InstalledVersion) <> '') then
  begin
    if not RegQueryStringValue(HKCU, UninstallKey, 'InstallLocation', InstalledDir) then
      RegQueryStringValue(HKCU, UninstallKey, 'Inno Setup: App Path', InstalledDir);
    InstalledDir := RemoveBackslashUnlessRoot(Trim(InstalledDir));

    if StrToVersion(InstalledVersion, InstalledPacked) and
       StrToVersion('{#AppVersion}', NewPacked) then
    begin
      Cmp := ComparePackedVersion(NewPacked, InstalledPacked);
      if Cmp > 0 then
        InstallMode := imUpgrade
      else if Cmp = 0 then
        InstallMode := imReinstall
      else
        InstallMode := imDowngrade;
    end
    else
      InstallMode := imUpgrade;
  end;

  if (InstallMode = imDowngrade) and (not HasCmdSwitch('ALLOWDOWNGRADE')) then
  begin
    if not WizardSilent then
    begin
      DowngradeMsg := FmtMessage(
        'Ya está instalada una versión más reciente de AnimeHub (%1). No se puede instalar la versión anterior (%2).' + #13#10#13#10 +
        'Desinstala la versión actual primero o ejecuta el instalador con /ALLOWDOWNGRADE.', [InstalledVersion, '{#AppVersion}']);
      SuppressibleMsgBox(DowngradeMsg, mbError, MB_OK, IDOK);
    end;
    Result := False;
  end;
end;

procedure InitializeWizard();
begin
  case InstallMode of
    imUpgrade:
      begin
        WizardForm.WelcomeLabel1.Caption :=
          FmtMessage('Actualizar AnimeHub de %1 a %2', [InstalledVersion, '{#AppVersion}']);
        WizardForm.WelcomeLabel2.Caption :=
          'El instalador actualizará tu copia actual de AnimeHub conservando tu biblioteca, el progreso y los ajustes.' + #13#10#13#10 +
          'Si estás viendo un vídeo en mpv, puedes continuar sin cerrarlo.' + #13#10#13#10 +
          'Haz clic en Siguiente para continuar o en Cancelar para salir.';
      end;
    imReinstall:
      begin
        WizardForm.WelcomeLabel1.Caption := 'Reparar o reinstalar AnimeHub {#AppVersion}';
        WizardForm.WelcomeLabel2.Caption :=
          'AnimeHub {#AppVersion} ya está instalado en este equipo. El instalador volverá a copiar los archivos del programa conservando tu biblioteca, el progreso y los ajustes.' + #13#10#13#10 +
          'Haz clic en Siguiente para continuar o en Cancelar para salir.';
      end;
    imDowngrade:
      begin
        WizardForm.WelcomeLabel1.Caption :=
          FmtMessage('Instalar versión anterior de AnimeHub (%1 a %2)', [InstalledVersion, '{#AppVersion}']);
        WizardForm.WelcomeLabel2.Caption :=
          'Se sustituirá la versión instalada por AnimeHub {#AppVersion} (permitido mediante /ALLOWDOWNGRADE), conservando tu biblioteca y ajustes.' + #13#10#13#10 +
          'Haz clic en Siguiente para continuar o en Cancelar para salir.';
      end;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if InstallMode = imUpgrade then
  begin
    if (PageID = wpSelectDir) or (PageID = wpSelectTasks) then
      Result := True;
  end
  else if InstallMode = imReinstall then
  begin
    if PageID = wpSelectDir then
      Result := True;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectTasks) and (InstallMode <> imFresh) then
    SyncStartupTaskCheckbox()
  else if (CurPageID = wpReady) and (InstallMode = imUpgrade) then
    SyncStartupTaskCheckbox();
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  TargetDir: String;
  MatchesInstalledDir: Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    TargetDir := RemoveBackslashUnlessRoot(Trim(WizardDirValue));
    MatchesInstalledDir := (InstalledDir <> '') and (CompareText(InstalledDir, TargetDir) = 0);
    if FileExists(TargetDir + '\AnimeHub.exe') and
       (not FileExists(TargetDir + '\unins000.exe')) and
       (not MatchesInstalledDir) then
    begin
      if SuppressibleMsgBox(
           'La carpeta seleccionada ya contiene una copia portable de AnimeHub (sin desinstalador).' + #13#10#13#10 +
           'Si continúas, sus archivos del programa se sustituirán por esta instalación. ¿Quieres continuar?',
           mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) <> IDYES then
        Result := False;
    end;
  end;
end;

function NeedsMpvUpdate(): Boolean;
begin
  Result := MpvUpdateNeeded;
end;

function ShouldInstallStartupEntry(): Boolean;
begin
  if InstallMode = imUpgrade then
  begin
    // Settings always wins on upgrade: never resurrect a disabled autostart entry, and preserve
    // an already-valid Run entry unless the installation directory changed.
    Result := HasLiveAutostart and
              (Pos(Lowercase(ExpandConstant('{app}\AnimeHub.exe')), Lowercase(LiveAutostartCmd)) = 0);
  end
  else
    Result := WizardIsTaskSelected('startup');
end;

function ShouldLaunchSilent(): Boolean;
begin
  Result := ExpandConstant('{param:LAUNCH|0}') = '1';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  EnginesDir, MpvDir, PinsPath: String;
  InstalledPinsRaw: AnsiString;
  NewMpvPin, InstalledMpvPin, Command: String;
begin
  if CurStep = ssInstall then
  begin
    EnginesDir := ExpandConstant('{app}\engines');
    MpvDir := EnginesDir + '\mpv';
    PinsPath := EnginesDir + '\.pins';
    CleanupStaleMpvBackups(EnginesDir);

    NewMpvPin := ExtractMpvPin(NewEnginesPins);
    if not FileExists(MpvDir + '\mpv.exe') then
      MpvUpdateNeeded := True
    else if NewMpvPin = '' then
      MpvUpdateNeeded := True
    else if LoadStringFromFile(PinsPath, InstalledPinsRaw) then
    begin
      InstalledMpvPin := ExtractMpvPin(String(InstalledPinsRaw));
      MpvUpdateNeeded := (InstalledMpvPin <> NewMpvPin);
    end
    else
      MpvUpdateNeeded := True;

    if MpvUpdateNeeded and DirExists(MpvDir) then
    begin
      MpvBackupDir := FindFreeMpvBackupDir(EnginesDir);
      MoveMpvAside(MpvDir, MpvBackupDir);
    end;
  end
  else if CurStep = ssPostInstall then
  begin
    // Best-effort cleanup of renamed mpv backup (succeeds immediately when mpv was not running).
    if (MpvBackupDir <> '') and DirExists(MpvBackupDir) then
      DelTree(MpvBackupDir, True, True, True);

    if InstallMode <> imUpgrade then
    begin
      if WizardIsTaskSelected('startup') then
      begin
        // Clear any Task Manager disabled override when the user explicitly selects startup here.
        RegDeleteValue(HKCU, StartupApprovedKey, 'AnimeHub');
      end
      else if (InstallMode = imReinstall) and HasLiveAutostart then
      begin
        if RegQueryStringValue(HKCU, RunKey, 'AnimeHub', Command) and
           (Pos(Lowercase(ExpandConstant('{app}\AnimeHub.exe')), Lowercase(Command)) > 0) then
        begin
          RegDeleteValue(HKCU, RunKey, 'AnimeHub');
          RegDeleteValue(HKCU, StartupApprovedKey, 'AnimeHub');
        end;
      end;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // The start entry goes with this copy, whether the installer or the app wrote it.
  if RegQueryStringValue(HKCU, RunKey, 'AnimeHub', Command) and
     (Pos(Lowercase(ExpandConstant('{app}\AnimeHub.exe')), Lowercase(Command)) > 0) then
  begin
    RegDeleteValue(HKCU, RunKey, 'AnimeHub');
    RegDeleteValue(HKCU, StartupApprovedKey, 'AnimeHub');
  end;
  // Silent uninstalls keep the library.
  if SuppressibleMsgBox('¿Borrar también la biblioteca, el progreso y los ajustes de AnimeHub?' + #13#10#13#10 +
                        'Los vídeos descargados no se borran.',
                        mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
    DelTree(ExpandConstant('{localappdata}\AnimeHub'), True, True, True);
end;

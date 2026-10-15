; AnimeHub for the current user: no administrator, under %LOCALAPPDATA%\Programs. The library and
; settings live in %LOCALAPPDATA%\AnimeHub, so updates keep them; uninstalling asks before
; removing them and never touches the downloaded videos.
;
;   ISCC /DAppVersion=0.2.0 /DSourceDir=<packaged AnimeHub folder> /DOutputDir=<folder> AnimeHub.iss
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
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A running AnimeHub (or its aria2) is closed through the Restart Manager before files change.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; Flags: unchecked
Name: "startup"; Description: "Abrir AnimeHub en la bandeja al iniciar Windows"; Flags: unchecked

[InstallDelete]
; What a previous version deployed and this one may not: Qt's plugins, QML and the engines.
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

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\AnimeHub"; Filename: "{app}\AnimeHub.exe"
Name: "{autodesktop}\AnimeHub"; Filename: "{app}\AnimeHub.exe"; Tasks: desktopicon

[Registry]
; The same entry AnimeHub writes from Ajustes → General → Al iniciar Windows.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "AnimeHub"; ValueData: """{app}\AnimeHub.exe"" --tray"; Tasks: startup

[Run]
Filename: "{app}\AnimeHub.exe"; Description: "Abrir AnimeHub"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // The start entry goes with this copy, whether the installer or the app wrote it.
  if RegQueryStringValue(HKCU, RunKey, 'AnimeHub', Command) and
     (Pos(Lowercase(ExpandConstant('{app}\AnimeHub.exe')), Lowercase(Command)) > 0) then
    RegDeleteValue(HKCU, RunKey, 'AnimeHub');
  // Silent uninstalls keep the library.
  if SuppressibleMsgBox('¿Borrar también la biblioteca, el progreso y los ajustes de AnimeHub?' + #13#10#13#10 +
                        'Los vídeos descargados no se borran.',
                        mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
    DelTree(ExpandConstant('{localappdata}\AnimeHub'), True, True, True);
end;

; Installateur Tuneforge — Inno Setup 6.
;
; Ne pas compiler a la main : .\package.ps1 transmet la version, lue dans
; CMakeLists.txt, et le dossier des binaires. Une version saisie ici finirait
; par diverger de celle qu'affichent l'application et ses executables.

#ifndef AppVersion
  #error "Compilez via package.ps1, qui transmet la version (/DAppVersion=...)."
#endif
#ifndef BinDir
  #define BinDir "..\build\bin"
#endif

#define AppName "Tuneforge"
#define RepoUrl "https://github.com/CR43K3L/Tuneforge"

[Setup]
; Identifiant stable : c'est lui qui fait qu'une nouvelle version remplace
; l'ancienne au lieu de s'installer a cote. Ne jamais le changer.
AppId={{02A192FF-3632-4D70-99F0-30395AE52792}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=CR43K3L
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription=Installateur de {#AppName}

DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
InfoBeforeFile=avant-installation.txt
OutputDir=..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=..\resources\tuneforge.ico
UninstallDisplayIcon={app}\tuneforge-gui.exe
UninstallDisplayName={#AppName}

; Tuneforge exige deja les droits administrateur a chaque lancement : il
; n'aurait rien a gagner a s'installer hors de Program Files. La surcharge
; en ligne de commande (/CURRENTUSER) ne sert qu'aux essais automatises.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; Une version ouverte verrouille ses fichiers. Le gestionnaire de redemarrage
; de Windows la ferme proprement, ce qui fait tourner son destructeur : les
; ventilateurs sont rendus au pilote avant d'etre remplaces.
CloseApplications=yes
RestartApplications=no

Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
french.RestoreShortcut=Tuneforge - restauration d'urgence
french.RestoreComment=Remet dans leur état d'origine tous les réglages modifiés par Tuneforge

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BinDir}\tuneforge-gui.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\tuneforge.exe";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\tuneforge-reset.exe"; DestDir: "{app}"; Flags: ignoreversion
; La GPL impose de remettre la licence avec le programme.
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\tuneforge-gui.exe"
Name: "{autoprograms}\{cm:RestoreShortcut}"; Filename: "{app}\tuneforge-reset.exe"; Comment: "{cm:RestoreComment}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\tuneforge-gui.exe"; Tasks: desktopicon

[Run]
; « shellexec » : l'executable exige l'elevation. Lance depuis un installateur
; qui ne l'aurait pas, un CreateProcess nu echouerait au lieu de la demander.
Filename: "{app}\tuneforge-gui.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent shellexec

[Code]
const
  WindowClass = 'TuneforgeWindow';

var
  KeepTweaks: Boolean;

function HasSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;

  // /KEEPTWEAKS : desinstaller sans rien restaurer. Sert aux essais de
  // l'installateur sur une machine dont on ne veut pas annuler les reglages.
  KeepTweaks := HasSwitch('/KEEPTWEAKS');
  if KeepTweaks then
    Log('Tuneforge : /KEEPTWEAKS present, aucune restauration')
  else
    Log('Tuneforge : restauration proposee');

  // Ouvert, Tuneforge tiendrait encore la courbe de ventilateur : il
  // reecrirait un niveau deux secondes apres que la restauration a rendu la
  // main au pilote. On demande donc de le fermer avant tout.
  while FindWindowByClassName(WindowClass) <> 0 do
  begin
    if SuppressibleMsgBox('Tuneforge est ouvert. Fermez-le, puis cliquez sur Réessayer.',
         mbInformation, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResetTool: String;
  Code: Integer;
begin
  // usUninstall : juste avant la suppression des fichiers. Apres, l'outil de
  // restauration n'existerait plus.
  if CurUninstallStep <> usUninstall then Exit;
  if KeepTweaks then Exit;

  ResetTool := ExpandConstant('{app}\tuneforge-reset.exe');
  if not FileExists(ResetTool) then Exit;

  // En desinstallation silencieuse la reponse par defaut est NON : une
  // operation lancee sans ecran ne doit rien changer au systeme au-dela du
  // retrait de ses propres fichiers.
  if SuppressibleMsgBox(
       'Remettre dans leur état d''origine les réglages que Tuneforge a modifiés ?' + #13#10#13#10 +
       'Si vous répondez non, ils resteront appliqués. Réinstaller Tuneforge ' +
       'permettra toujours de les annuler plus tard.',
       mbConfirmation, MB_YESNO, IDNO) <> IDYES then
  begin
    Log('Tuneforge : restauration refusee');
    Exit;
  end;

  // tuneforge-reset renvoie 1 des qu'un reglage n'a pas pu etre restaure.
  if not Exec(ResetTool, '--yes', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    SuppressibleMsgBox(
      'Certains réglages n''ont pas pu être restaurés.' + #13#10#13#10 +
      'Leur état d''origine reste enregistré : réinstaller Tuneforge permettra de réessayer. ' +
      'Le détail est dans %LOCALAPPDATA%\Tuneforge\tuneforge.log.',
      mbError, MB_OK, IDOK)
  else
    Log('Tuneforge : restauration terminee');
end;

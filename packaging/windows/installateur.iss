; Installateur Windows d'un logiciel de MevSuite (Inno Setup 6), fabriqué par packaging/windows.sh à
; partir du dossier de la version portable :
;
;   ISCC.exe /DAppNom=MeVReg /DAppExe=mevreg.exe /DAppVersion=0.3.2 /DAppId=<GUID> /DSource=<dossier>
;            /DSortie=<dossier> /DIcone=<fichier .ico> [/DExtension=phy] installateur.iss
;
; Installation pour tous les utilisateurs dans Program Files\MeVSuite\<Nom>, raccourci dans le menu
; Démarrer (dossier MeVSuite), désinstallation par les Paramètres de Windows. Déploiement sur un parc
; (WAPT, GPO), sans fenêtre ni question :
;
;   <Nom>-<version>-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
;   "C:\Program Files\MeVSuite\<Nom>\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
;
; L'identifiant (AppId) ne change pas d'une version à l'autre : une nouvelle version remplace
; l'ancienne. Les réglages des utilisateurs (%APPDATA%\MeVSuite) ne sont pas touchés.

#ifndef Extension
#define Extension ""
#endif

[Setup]
AppId={{{#AppId}
AppName={#AppNom}
AppVersion={#AppVersion}
AppVerName={#AppNom} {#AppVersion}
AppPublisher=Rémi MEVAERE
AppPublisherURL=https://github.com/melectronvolt/MevSuite
AppSupportURL=https://github.com/melectronvolt/MevSuite
AppContact=suite@sciences-physiques.net
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppNom}
DefaultDirName={autopf}\MeVSuite\{#AppNom}
DefaultGroupName=MeVSuite
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Sortie}
OutputBaseFilename={#AppNom}-{#AppVersion}-windows-x64-setup
SetupIconFile={#Icone}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppNom} {#AppVersion}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
ShowLanguageDialog=no
#if Extension != ""
ChangesAssociations=yes
#endif

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "bureau"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; fichiers d'une version précédente qui n'existeraient plus
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{app}\ffmpeg"
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppNom}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppNom}"; Filename: "{app}\{#AppExe}"; Tasks: bureau

#if Extension != ""
[Registry]
; documents du logiciel (.{#Extension}) : ouverts par un double-clic
Root: HKA; Subkey: "Software\Classes\.{#Extension}"; ValueType: string; ValueName: ""; ValueData: "{#AppNom}.Document"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#AppNom}.Document"; ValueType: string; ValueName: ""; ValueData: "Document {#AppNom}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#AppNom}.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#AppNom}.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
#endif

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppNom}}"; Flags: nowait postinstall skipifsilent

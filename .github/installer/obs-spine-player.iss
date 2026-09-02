#ifndef AppVersion
  #error AppVersion must be provided by the build workflow
#endif

#ifndef SourceDir
  #error SourceDir must be provided by the build workflow
#endif

#ifndef OutputDir
  #error OutputDir must be provided by the build workflow
#endif

[Setup]
AppId={{431E68A8-C14D-4628-ABF6-0A6A788B193F}
AppName=OBS Spine Player
AppVersion={#AppVersion}
AppPublisher=niizam
AppPublisherURL=https://github.com/niizam/obs-spine-player
AppSupportURL=https://github.com/niizam/obs-spine-player/issues
AppUpdatesURL=https://github.com/niizam/obs-spine-player/releases
DefaultDirName={autopf}\obs-studio
DisableProgramGroupPage=yes
DirExistsWarning=no
OutputDir={#OutputDir}
OutputBaseFilename=obs-spine-player-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=OBS Spine Player
UninstallFilesDir={app}\uninstall\obs-spine-player
VersionInfoVersion={#AppVersion}
VersionInfoCompany=niizam
VersionInfoDescription=OBS Spine Player installer
VersionInfoProductName=OBS Spine Player
VersionInfoProductVersion={#AppVersion}

[Files]
Source: "{#SourceDir}\obs-plugins\64bit\obs-spine-player.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceDir}\data\obs-plugins\obs-spine-player\*"; DestDir: "{app}\data\obs-plugins\obs-spine-player"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
SelectDirDesc=Select the OBS Studio installation folder.
SelectDirLabel3=Setup will install OBS Spine Player into this OBS Studio folder.

[Run]
Filename: "{app}\bin\64bit\obs64.exe"; Description: "Launch OBS Studio"; Flags: nowait postinstall skipifsilent skipifdoesntexist unchecked

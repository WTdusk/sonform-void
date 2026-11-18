; SPDX-License-Identifier: AGPL-3.0-only
#define DefaultInstallDir "{autopf}\WTdusk\SONFORM Void"
[Setup]
AppId=WTdusk.SONFORM.Void
AppName=SONFORM Void
AppVersion={#ReleaseVersion}
AppPublisher=WTdusk
VersionInfoCompany=WTdusk
VersionInfoVersion={#ReleaseVersion}
VersionInfoDescription=SONFORM Void Windows Setup
DefaultDirName={#DefaultInstallDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=SONFORM Void
UninstallFilesDir={app}\Installer
WizardStyle=modern
OutputDir={#SetupOutput}
OutputBaseFilename=SONFORM Void {#ReleaseVersion} Windows Setup
Compression=lzma2
SolidCompression=yes

[Types]
Name: "full"; Description: "VST3 and Standalone"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 instrument for DAWs (64-bit)"; Types: full
Name: "standalone"; Description: "Standalone instrument (64-bit)"; Types: full

[Files]
Source: "{#Payload}\SONFORM Void.vst3\Contents\x86_64-win\SONFORM Void.vst3"; DestDir: "{code:Vst3Path}\SONFORM Void.vst3\Contents\x86_64-win"; Components: vst3; Flags: ignoreversion
Source: "{#Payload}\SONFORM Void.vst3\Contents\Resources\moduleinfo.json"; DestDir: "{code:Vst3Path}\SONFORM Void.vst3\Contents\Resources"; Components: vst3; Flags: ignoreversion
Source: "{#Payload}\SONFORM Void {#ReleaseVersion}.exe"; DestDir: "{code:StandalonePath}"; DestName: "SONFORM Void.exe"; Components: standalone; Flags: ignoreversion
Source: "{#Payload}\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\Source.zip"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SONFORM Void"; Filename: "{code:StandalonePath}\SONFORM Void.exe"; WorkingDir: "{code:StandalonePath}"; Components: standalone

[Code]
var
  Vst3Page, StandalonePage: TInputDirWizardPage;

procedure InitializeWizard;
var
  PluginDefault: String;
begin
  if IsAdminInstallMode then
    PluginDefault := ExpandConstant('{commoncf64}\VST3')
  else
    PluginDefault := ExpandConstant('{localappdata}\Programs\Common\VST3');

  Vst3Page := CreateInputDirPage(wpSelectComponents, 'VST3 folder',
    'Choose the folder that your DAW scans for VST3 plug-ins.',
    'Setup will create the SONFORM Void.vst3 bundle inside this folder.', False, '');
  Vst3Page.Add('');
  Vst3Page.Values[0] := ExpandConstant('{param:VST3DIR|}');
  if Vst3Page.Values[0] = '' then
    Vst3Page.Values[0] := GetPreviousData('Vst3Path', PluginDefault);

  StandalonePage := CreateInputDirPage(Vst3Page.ID, 'Standalone folder',
    'Choose where to install the standalone instrument.',
    'Setup will place SONFORM Void.exe inside this folder.', False, '');
  StandalonePage.Add('');
  StandalonePage.Values[0] := ExpandConstant('{param:STANDALONEDIR|}');
  // The final application directory is not initialized while creating the wizard.
  if StandalonePage.Values[0] = '' then
    StandalonePage.Values[0] := GetPreviousData('StandalonePath', ExpandConstant('{#DefaultInstallDir}'));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = Vst3Page.ID) and not WizardIsComponentSelected('vst3')) or
    ((PageID = StandalonePage.ID) and not WizardIsComponentSelected('standalone'));
end;

function Vst3Path(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Vst3Page.Values[0]));
end;

function StandalonePath(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(StandalonePage.Values[0]));
end;

function SelectionError: String;
begin
  Result := '';
  if not (WizardIsComponentSelected('vst3') or WizardIsComponentSelected('standalone')) then
    Result := 'Select at least one component.'
  else if WizardIsComponentSelected('vst3') and
    ((ExtractFileDrive(Vst3Path('')) = '') or
      (CompareText(ExtractFileExt(Vst3Path('')), '.vst3') = 0)) then
    Result := 'Choose an absolute path to the folder that will contain the .vst3 bundle.'
  else if WizardIsComponentSelected('standalone') and
    (ExtractFileDrive(StandalonePath('')) = '') then
    Result := 'Choose an absolute path for the standalone instrument.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and
    not (WizardIsComponentSelected('vst3') or WizardIsComponentSelected('standalone')) then
    ErrorText := 'Select at least one component.'
  else if (CurPageID = Vst3Page.ID) and
    (CompareText(ExtractFileExt(Vst3Path('')), '.vst3') = 0) then
    ErrorText := 'Choose the parent folder that will contain the .vst3 bundle.'
  else
    ErrorText := '';
  if ErrorText <> '' then
  begin
    SuppressibleMsgBox(ErrorText, mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := SelectionError;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Install locations' + NewLine;
  if WizardIsComponentSelected('vst3') then
    Result := Result + Space + 'VST3: ' + Vst3Path('') + NewLine;
  if WizardIsComponentSelected('standalone') then
    Result := Result + Space + 'Standalone: ' + StandalonePath('') + NewLine;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if WizardIsComponentSelected('vst3') then
    SetPreviousData(PreviousDataKey, 'Vst3Path', Vst3Path(''));
  if WizardIsComponentSelected('standalone') then
    SetPreviousData(PreviousDataKey, 'StandalonePath', StandalonePath(''));
end;

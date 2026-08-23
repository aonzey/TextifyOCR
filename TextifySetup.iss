; TextifyOCR installer script for Inno Setup
; Build:  "C:\Program\ISCC.exe" E:\Textify_Project\TextifySetup.iss
; Output: E:\Textify_Project\installer\TextifyOCR_Setup.exe

#define MyAppName "TextifyOCR"
#define MyAppVersion "2.1.0"
#define MyAppPublisher "TextifyOCR (m417z) + RapidOCR"
#define MyAppExeName "TextifyOCR.exe"
#define MyAppURL "https://github.com/m417z/Textify"

[Setup]
AppId={{B6F3B7C2-9E1A-4C6D-8A5F-TEXTIFYOCR1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=E:\Textify_Project\LICENSE
OutputDir=E:\Textify_Project\installer
OutputBaseFilename=TextifyOCR_Setup
SetupIconFile=E:\Textify_Project\res\Textify.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
; If a previous TextifyOCR.ini exists, keep the user's settings
; (handled by "onlyifdoesntexist" flags below)

[Languages]
Name: "english"; MessagesFile: "C:\Program\Default.isl"
Name: "chinesesimplified"; MessagesFile: "C:\Program\Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "runatstartup"; Description: "Run TextifyOCR when Windows starts"; GroupDescription: "Startup:"

[Files]
; Main application
Source: "E:\Textify_Project\build_x64\TextifyOCR.exe"; DestDir: "{app}"; Flags: ignoreversion
; External manifest (Common-Controls v6 dependency) - the exe also has it
; embedded, but ship the file too so even a rebuilt exe without embedding works
Source: "E:\Textify_Project\build_x64\TextifyOCR.exe.manifest"; DestDir: "{app}"; Flags: ignoreversion
; Default config (with web translation buttons) - only installed if the user has no config yet
; DestName must match the exe name: the app derives the ini path from its own exe path
Source: "E:\Textify_Project\res\Textify.ini"; DestDir: "{app}"; DestName: "TextifyOCR.ini"; Flags: onlyifdoesntexist; Permissions: users-modify
; Web button icons (referenced by TextifyOCR.ini as icons\*.ico, relative to the exe)
Source: "E:\Textify_Project\res\icons\*"; DestDir: "{app}\icons"; Flags: ignoreversion
; Optional python-mode helper (tiny, kept for users who have their own Python)
Source: "E:\Textify_Project\build_x64\ocr_helper.py"; DestDir: "{app}"; Flags: ignoreversion
; Self-contained OCR engine (PyInstaller onedir, includes ONNX models, ~260 MB)
Source: "E:\Textify_Project\build_x64\ocr_helper\*"; DestDir: "{app}\ocr_helper"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Run at startup (optional task)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "TextifyOCR"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: runatstartup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// Close any running TextifyOCR.exe before installing/upgrading.
// Without this, setup.exe may fail to overwrite TextifyOCR.exe while it's
// locked, leaving the user running an outdated copy that throws
// "Ordinal not found in TextifyOCR.exe" or other loader errors.
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  // /T=3 = WM_CLOSE, /F = force. Wait 5s for graceful shutdown.
  if Exec(
    'taskkill.exe',
    '/F /IM ' + '{#MyAppExeName}',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Log('Closed running {#MyAppExeName} (exit ' + IntToStr(ResultCode) + ')');
  end;
  Sleep(1000);  // let file handles release
end;

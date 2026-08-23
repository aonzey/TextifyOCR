@echo off
rem ============================================================
rem  TextifyOCR manual build script (x64 Release)
rem  Builds without MSBuild by invoking cl.exe / rc.exe / link.exe
rem  Requires: VS2022 BuildTools (MSVC v143 + ATL) and Win10 SDK
rem ============================================================
setlocal

set "PROJ=%~dp0"
set "MSVC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "WTL=%PROJ%packages\wtl.10.0.10320\lib\native\include"

set "INCLUDE=%PROJ%;%PROJ%build_x64;%WTL%;%MSVC%\include;%MSVC%\atlmfc\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%MSVC%\lib\x64;%MSVC%\atlmfc\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

if not exist "%PROJ%build_x64" mkdir "%PROJ%build_x64"
cd /d "%PROJ%"

echo [1/4] Compiling C++ sources...
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /c /std:c++20 /EHsc /MT /DNDEBUG /DWIN32 /D_WINDOWS /DSTRICT /DPSAPI_VERSION=1 /DUNICODE /D_UNICODE /Fo"build_x64\" stdafx.cpp async_internet.cpp Functions.cpp MainDlg.cpp MouseGlobalHook.cpp OcrCapture.cpp SettingsDlg.cpp Textify.cpp TextDlg.cpp update.cpp URLEncode.cpp UserConfig.cpp WebAppLaunch.cpp
if errorlevel 1 goto fail

echo [2/4] Compiling resources...
"%SDK%\bin\%SDKVER%\x64\rc.exe" /nologo /d NDEBUG /d _UNICODE /d UNICODE /fo"build_x64\Textify.res" Textify.rc
if errorlevel 1 goto fail

echo [3/4] Linking...
"%MSVC%\bin\Hostx64\x64\link.exe" /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF /OUT:"build_x64\TextifyOCR.exe" build_x64\stdafx.obj build_x64\async_internet.obj build_x64\Functions.obj build_x64\MainDlg.obj build_x64\MouseGlobalHook.obj build_x64\OcrCapture.obj build_x64\SettingsDlg.obj build_x64\Textify.obj build_x64\TextDlg.obj build_x64\update.obj build_x64\URLEncode.obj build_x64\UserConfig.obj build_x64\WebAppLaunch.obj build_x64\Textify.res oleacc.lib psapi.lib wininet.lib gdiplus.lib msimg32.lib kernel32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comctl32.lib shlwapi.lib
if errorlevel 1 goto fail

echo [4/4] Embedding manifest (Common-Controls v6)...
rem CRITICAL: without this embedded manifest the app loads comctl32 v5
rem and fails with "ordinal 381 not found" on machines where the external
rem TextifyOCR.exe.manifest is missing (e.g. installed by setup.exe).
"%SDK%\bin\%SDKVER%\x64\mt.exe" -nologo -manifest "build_x64\TextifyOCR.exe.manifest" -outputresource:"build_x64\TextifyOCR.exe";1
if errorlevel 1 goto fail

echo.
echo BUILD OK: %PROJ%build_x64\TextifyOCR.exe
exit /b 0

:fail
echo BUILD FAILED
exit /b 1

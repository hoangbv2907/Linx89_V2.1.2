@echo off
rem [2026-11-16 R31] Build bản Release x64 và đóng gói.
rem   dist\LinxController.exe        - chạy trực tiếp (portable), liên kết tĩnh CRT (/MT): máy đích KHÔNG cần cài VC++ Redistributable
rem   dist\LinxController_Setup.exe  - bộ cài (IExpress): chép vào %%LOCALAPPDATA%%\Programs\LinxController + tạo shortcut
rem Chạy từ "Developer Command Prompt for VS" (cần cl.exe). iexpress.exe có sẵn trong Windows.
setlocal
set ROOT=%~dp0
set SRC=%ROOT%Linx v2.1.2
set DIST=%ROOT%dist
set OBJ=%TEMP%\linx_release_obj
set PKG=%TEMP%\linx_release_pkg

if exist "%OBJ%" rmdir /s /q "%OBJ%"
if exist "%PKG%" rmdir /s /q "%PKG%"
mkdir "%OBJ%" "%PKG%"
if not exist "%DIST%" mkdir "%DIST%"

echo === Bien dich Release x64 (/O2 /MT) ===
pushd "%SRC%"
cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /GL /DNDEBUG /DUNICODE /D_UNICODE /Iinclude /Fo"%OBJ%\\" ^
   src/main.cpp src/controller/AppController.cpp src/rci/RciClient.cpp src/rci/RciWarnings.cpp src/controller/PrintPersistStorage.cpp ^
   src/ui/UIManager.cpp src/ui/WindowManager.cpp src/ui/MessageLogger.cpp src/ui/ModernButton.cpp src/ui/ToggleSwitch.cpp ^
   src/license/LicenseCore.cpp src/ui/dialogs/LicenseDialog.cpp src/ui/dialogs/LogViewerDialog.cpp src/settings/AppSettings.cpp src/ui/dialogs/SettingsDialog.cpp src/auth/AuthCore.cpp src/ui/dialogs/PinDialog.cpp ^
   /Fe"%PKG%\LinxController.exe" ^
   /link /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" ^
   user32.lib gdi32.lib comctl32.lib uxtheme.lib shell32.lib ws2_32.lib advapi32.lib ole32.lib comdlg32.lib bcrypt.lib
if errorlevel 1 ( popd & echo BUILD THAT BAI & exit /b 1 )
popd

copy /y "%PKG%\LinxController.exe" "%DIST%\LinxController.exe" >nul
copy /y "%ROOT%installer\install.cmd" "%PKG%\" >nul
copy /y "%ROOT%installer\uninstall.cmd" "%PKG%\" >nul

echo === Tao bo cai (IExpress) ===
set SED=%PKG%\setup.sed
> "%SED%"  echo [Version]
>> "%SED%" echo Class=IEXPRESS
>> "%SED%" echo SEDVersion=3
>> "%SED%" echo [Options]
>> "%SED%" echo PackagePurpose=InstallApp
>> "%SED%" echo ShowInstallProgramWindow=1
>> "%SED%" echo HideExtractAnimation=1
>> "%SED%" echo UseLongFileName=1
>> "%SED%" echo InsideCompressed=0
>> "%SED%" echo CAB_FixedSize=0
>> "%SED%" echo CAB_ResvCodeSigning=0
>> "%SED%" echo RebootMode=N
>> "%SED%" echo InstallPrompt=
>> "%SED%" echo DisplayLicense=
>> "%SED%" echo FinishMessage=
>> "%SED%" echo TargetName=%PKG%\LinxController_Setup.exe
>> "%SED%" echo FriendlyName=Linx Controller
>> "%SED%" echo AppLaunched=cmd /c install.cmd
>> "%SED%" echo PostInstallCmd=^<None^>
>> "%SED%" echo AdminQuietInstCmd=
>> "%SED%" echo UserQuietInstCmd=
>> "%SED%" echo SourceFiles=SourceFiles
>> "%SED%" echo [SourceFiles]
>> "%SED%" echo SourceFiles0=%PKG%\
>> "%SED%" echo [SourceFiles0]
>> "%SED%" echo LinxController.exe=
>> "%SED%" echo install.cmd=
>> "%SED%" echo uninstall.cmd=

rem iexpress tra ve ngay khi goi truc tiep -> phai "start /wait" de doi no tao xong file
rem iexpress KHONG nhan duong dan .sed co dau nhay -> vao thu muc va dung ten tuong doi
pushd "%PKG%"
start "" /wait "%SystemRoot%\System32\iexpress.exe" /N /Q setup.sed
popd
if not exist "%PKG%\LinxController_Setup.exe" ( echo TAO BO CAI THAT BAI & exit /b 1 )
copy /y "%PKG%\LinxController_Setup.exe" "%DIST%\LinxController_Setup.exe" >nul

echo.
echo XONG:
dir /b "%DIST%"
exit /b 0

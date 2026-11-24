@echo off
rem [R50] Build công cụ cấp mã (nhà cung cấp): LicenseKeygenGui.exe (cửa sổ) và LicenseKeygen.exe (dòng lệnh).
rem Chạy từ "Developer Command Prompt for VS". Kết quả: tools\out\*.exe. Chép vào thư mục chứa license_private.key.
setlocal
set ROOT=%~dp0..
set INC=%ROOT%\Linx v2.1.2\include
set LIC=%ROOT%\Linx v2.1.2\src\license\LicenseCore.cpp
if not exist "%~dp0out" mkdir "%~dp0out"
pushd "%~dp0out"
cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I"%INC%" "%~dp0LicenseKeygenGui.cpp" "%LIC%" /Fe:LicenseKeygenGui.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib advapi32.lib bcrypt.lib || ( popd & exit /b 1 )
cl /nologo /std:c++17 /EHsc /W3 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I"%INC%" "%~dp0LicenseKeygen.cpp" "%LIC%" /Fe:LicenseKeygen.exe /link advapi32.lib bcrypt.lib || ( popd & exit /b 1 )
del /q *.obj 2>nul
popd
echo OK: %~dp0out

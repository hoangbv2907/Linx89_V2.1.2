@echo off
rem [2026-11-16 R31] Cai dat Linx Controller cho nguoi dung hien tai (khong can quyen Administrator).
rem Thu muc cai: %LOCALAPPDATA%\Programs\LinxController  (ghi duoc, vi chuong trinh tao file log canh file exe)
setlocal
set "DEST=%LOCALAPPDATA%\Programs\LinxController"
if not exist "%DEST%" mkdir "%DEST%"

copy /y "%~dp0LinxController.exe" "%DEST%\LinxController.exe" >nul
if errorlevel 1 (
    echo.
    echo KHONG CAI DUOC: khong ghi duoc "%DEST%\LinxController.exe".
    echo Hay dong Linx Controller neu dang chay roi cai lai.
    pause
    exit /b 1
)
copy /y "%~dp0uninstall.cmd" "%DEST%\uninstall.cmd" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$d = Join-Path $env:LOCALAPPDATA 'Programs\LinxController';" ^
  "$w = New-Object -ComObject WScript.Shell;" ^
  "foreach ($f in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {" ^
  "  $l = $w.CreateShortcut((Join-Path $f 'Linx Controller.lnk'));" ^
  "  $l.TargetPath = (Join-Path $d 'LinxController.exe');" ^
  "  $l.WorkingDirectory = $d;" ^
  "  $l.Description = 'Linx Controller';" ^
  "  $l.Save() }"

echo.
echo Da cai dat Linx Controller vao:
echo   %DEST%
echo Shortcut "Linx Controller" da duoc tao tren Desktop va Start Menu.
echo Go cai dat: chay "%DEST%\uninstall.cmd"
echo.
pause
exit /b 0

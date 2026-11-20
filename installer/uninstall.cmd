@echo off
rem [2026-11-16 R31] Go cai dat Linx Controller (nguoi dung hien tai).
rem Du lieu in da luu (%LOCALAPPDATA%\LinxController\last_print_data.json) duoc GIU LAI.
setlocal
set "DEST=%LOCALAPPDATA%\Programs\LinxController"
echo Go cai dat Linx Controller khoi "%DEST%" ?
echo Nhan Ctrl+C de huy, hoac
pause

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "foreach ($f in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {" ^
  "  $p = Join-Path $f 'Linx Controller.lnk'; if (Test-Path $p) { Remove-Item $p -Force } }"

del /q "%DEST%\LinxController.exe" 2>nul
if exist "%DEST%\LinxController.exe" (
    echo KHONG XOA DUOC: hay dong Linx Controller roi chay lai.
    pause
    exit /b 1
)
del /q "%DEST%\*.log" 2>nul
echo Da go cai dat. (Co the xoa thu muc "%DEST%" bang tay.)
pause
rem tu xoa chinh file nay va thu muc
(goto) 2>nul & del /q "%~f0" & rmdir "%DEST%" 2>nul

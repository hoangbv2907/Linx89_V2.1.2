@echo off
rem Build + chạy unit test và test tích hợp (máy in giả) ở chế độ DEBUG.
rem [2026-11-13 R17] /Od /Zi /RTC1 (kiểm tra stack/biến chưa khởi tạo) + AddressSanitizer (tràn bộ nhớ, use-after-free).
rem Chạy từ "Developer Command Prompt for VS". Đặt NOASAN=1 để tắt AddressSanitizer.
setlocal
cd /d "%~dp0"
set SRC=..\Linx v2.1.2
set FLAGS=/nologo /std:c++17 /EHsc /W3 /Od /Zi /RTC1 /DUNICODE /D_UNICODE /MDd
if not defined NOASAN set FLAGS=%FLAGS% /fsanitize=address
rem Đặt TEST_PORT=<cổng> để chạy test ở cổng khác 29043 (vd khi đang chạy simulator thật).
if defined TEST_PORT set FLAGS=%FLAGS% /DLINX_RCI_PORT=%TEST_PORT%

cl %FLAGS% /Fe:RciFrameTests.exe RciFrameTests.cpp "%SRC%\RciClient.cpp" "%SRC%\RciWarnings.cpp" ws2_32.lib || exit /b 1
cl %FLAGS% /Fe:RciSimTests.exe RciSimTests.cpp "%SRC%\RciClient.cpp" "%SRC%\RciWarnings.cpp" ws2_32.lib || exit /b 1

cl %FLAGS% /Fe:AppControllerTests.exe AppControllerTests.cpp "%SRC%\AppController.cpp" "%SRC%\RciClient.cpp" "%SRC%\RciWarnings.cpp" ws2_32.lib user32.lib gdi32.lib || exit /b 1

cl %FLAGS% /Fe:ManualFlowTests.exe ManualFlowTests.cpp "%SRC%\AppController.cpp" "%SRC%\RciClient.cpp" "%SRC%\RciWarnings.cpp" ws2_32.lib user32.lib gdi32.lib || exit /b 1

set RC=0
echo ===== RciFrameTests =====
"%~dp0RciFrameTests.exe" || set RC=1
echo ===== RciSimTests =====
"%~dp0RciSimTests.exe" || set RC=1
echo ===== AppControllerTests (can cong 127.0.0.1:29043 trong) =====
"%~dp0AppControllerTests.exe" || set RC=1
echo ===== ManualFlowTests (test case theo manual, xem TEST_CASES.md) =====
"%~dp0ManualFlowTests.exe" || set RC=1
exit /b %RC%

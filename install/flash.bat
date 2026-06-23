@echo off
setlocal
for %%f in ("%~dp0esp32matrix-*-merged.bin") do set "MERGED=%%f"
if not defined MERGED ( echo Merged .bin not found next to this script. & pause & exit /b 1 )
echo Flashing %MERGED% ...
"%~dp0esptool.exe" --chip esp32s3 --baud 921600 write_flash 0x0 "%MERGED%"
echo.
echo Done. The board will reboot. Find it at http://esp32matrix.local
pause

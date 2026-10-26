@echo off
setlocal
echo Compiling RumbleVersio firmware for Desmodus Versio...
set "PATH=%~dp0toolchain\xpack-arm-none-eabi-gcc-13.3.1-1.1\bin;%~dp0toolchain\xpack-windows-build-tools-4.4.1-3\bin;%PATH%"

make -j8
if %ERRORLEVEL% EQU 0 (
    copy /Y build\RumbleVersio.bin RumbleVersio.bin >nul
    echo.
    echo =======================================================
    echo   BUILD SUCCESSFUL!
    echo   Firmware ready: RumbleVersio.bin
    echo =======================================================
) else (
    echo.
    echo BUILD FAILED!
)
pause

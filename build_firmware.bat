@echo off
set "PATH=%~dp0toolchain\xpack-arm-none-eabi-gcc-13.3.1-1.1\bin;%~dp0toolchain\xpack-windows-build-tools-4.4.1-3\bin;%PATH%"
make VERSION=2 -j8

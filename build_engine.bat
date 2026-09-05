@echo off
setlocal
echo ===================================================
echo   Building Alamy Engine v2 (PC side, C++20)
echo ===================================================

set GXX=C:\msys64\ucrt64\bin\g++.exe
if not exist "%GXX%" set GXX=g++.exe

if not exist "engine-v2\bin" mkdir "engine-v2\bin"

"%GXX%" -std=c++20 -O3 -Wall ^
    engine-v2\src\main.cpp ^
    engine-v2\src\server.cpp ^
    engine-v2\src\input_injector.cpp ^
    -lws2_32 -luser32 -lgdi32 ^
    -o engine-v2\bin\alamy2.exe

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Binary created at engine-v2\bin\alamy2.exe
) else (
    echo [FAILED] Compilation failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

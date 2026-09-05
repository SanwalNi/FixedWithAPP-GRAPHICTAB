@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Building Alamy - High Speed Mobile Tablet Engine
echo ===================================================

set GXX="C:\msys64\ucrt64\bin\g++.exe"
if not exist %GXX% (
    set GXX=g++.exe
)

if not exist "bin" mkdir bin

echo Compiling C++20 source files with -O3 optimization...
%GXX% -std=c++20 -O3 -Wall ^
    src\main.cpp ^
    src\input_injector.cpp ^
    src\socket_server.cpp ^
    src\adb_manager.cpp ^
    -lws2_32 -luser32 -lgdi32 -liphlpapi ^
    -o bin\alamy.exe

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Binary created at bin\alamy.exe
) else (
    echo [FAILED] Compilation failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo Build complete.

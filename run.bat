@echo off
setlocal
echo ===================================================
echo   Alamy - one-click start (PC engine + Android app)
echo ===================================================
rem The engine does everything automatically:
rem   1. Finds adb and your connected Android device
rem   2. Sets up the USB reverse tunnel (adb reverse tcp:8080)
rem   3. Installs the Alamy Tablet APK if it is missing
rem   4. Launches the app on the phone
rem   5. Starts the injection engine
rem
rem Pass --install to force a reinstall of the APK (after rebuilding it).

if not exist "engine-v2\bin\alamy2.exe" (
    echo Engine binary missing - building...
    call build_engine.bat
    if errorlevel 1 exit /b 1
)

echo Starting Alamy Engine v2...
cd /d "%~dp0"
engine-v2\bin\alamy2.exe %*


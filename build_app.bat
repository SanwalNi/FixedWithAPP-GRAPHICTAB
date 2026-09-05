@echo off
setlocal
echo ===================================================
echo   Building Alamy Tablet (Android APK)
echo ===================================================

rem --- Toolchain (one-time setup: run setup_sdk.bat) ---
set "ANDROID_HOME=E:\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
set "GRADLE=E:\Android\gradle-8.7\bin\gradle.bat"
set "PATH=%JAVA_HOME%\bin;%PATH%"

if not exist "%GRADLE%" (
    echo [FAILED] Gradle not found at %GRADLE% - run setup_sdk.bat first.
    exit /b 1
)

pushd "%~dp0android-app"
call "%GRADLE%" assembleDebug --no-daemon
popd

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] APK: android-app\app\build\outputs\apk\debug\app-debug.apk
) else (
    echo [FAILED] Gradle build failed.
    exit /b %ERRORLEVEL%
)

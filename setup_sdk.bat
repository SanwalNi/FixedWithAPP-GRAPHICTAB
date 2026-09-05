@echo off
setlocal
echo ===================================================
echo   One-time Android SDK setup for Alamy Tablet
echo   (Downloads each component individually, all ^< 500 MB)
echo ===================================================

set "DL=%~dp0downloads"
set "SDK=E:\Android\Sdk"
set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
set "PATH=%JAVA_HOME%\bin;%PATH%"

if not exist "%DL%" mkdir "%DL%"

rem 1. Gradle build tool
if not exist "E:\Android\gradle-8.7\bin\gradle.bat" (
    echo [1/3] Downloading Gradle 8.7 ...
    curl.exe -sSL --retry 10 -o "%DL%\gradle-8.7-bin.zip" https://services.gradle.org/distributions/gradle-8.7-bin.zip
    tar -xf "%DL%\gradle-8.7-bin.zip" -C "E:\Android\"
)

rem 2. SDK command-line tools
if not exist "E:\Android\cmdline-tools\latest\bin\sdkmanager.bat" (
    echo [2/3] Downloading Android SDK command-line tools ...
    curl.exe -sSL --retry 10 -o "%DL%\commandlinetools-win-11076708_latest.zip" https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip
    if exist "E:\Android\cmdline-tools\latest" rd /s /q "E:\Android\cmdline-tools\latest"
    tar -xf "%DL%\commandlinetools-win-11076708_latest.zip" -C "E:\Android\cmdline-tools"
    ren "E:\Android\cmdline-tools\cmdline-tools" latest
)

rem 3. SDK platform + build tools (~120 MB combined)
set "SDKM=E:\Android\cmdline-tools\latest\bin\sdkmanager.bat"
if exist "%SDKM%" (
    echo [3/3] Installing platform + build-tools (accepting licenses) ...
    (for /L %%i in (1,1,20) do @echo y) > "%DL%\yes.txt"
    type "%DL%\yes.txt" | call "%SDKM%" --sdk_root=%SDK% --licenses >nul 2>&1
    call "%SDKM%" --sdk_root=%SDK% "platforms;android-34" "build-tools;34.0.0" "platform-tools"
)

echo.
echo [DONE] SDK ready. Run build_app.bat to compile the APK.
pause

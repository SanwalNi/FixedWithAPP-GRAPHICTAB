# Alamy v2 System Requirements

## 🖥️ PC (Engine v2 + build)
1. **OS**: Windows 10 / 11
2. **Compiler**: MinGW-w64 g++ with C++20 (MSYS2 UCRT64; `build_engine.bat` looks at `C:\msys64\ucrt64\bin\g++.exe`, falls back to `g++` on PATH)
3. **JDK 17** (Temurin 17.0.19 confirmed working) - needed by Gradle/AGP for the Android build
4. **Android SDK command-line tools + Gradle 8.7 + platforms;android-34 + build-tools;34.0.0** - install once with `setup_sdk.bat` (installs to `E:\Android`)
5. **ADB** - found automatically in: `E:\platform-tools-latest-windows\platform-tools\`, `E:\Android\Sdk\platform-tools\`, PATH, or `%LOCALAPPDATA%\Android\Sdk\platform-tools\`

## 📱 Android device
1. Android 8.0+ (API 26 minimum, compiled against API 34)
2. USB Debugging enabled (Settings > Developer Options)
3. High-refresh panel (60/90 Hz) is used automatically - the app requests the highest supported mode

## 🔌 Hardware
- Data-capable USB cable (USB 2.0 is sufficient: the wire protocol peaks at ~8 KB/s at 240 Hz)

## 📦 App dependencies
- **Zero external libraries** on both sides: the Android app uses only framework APIs (no AndroidX), the engine only Win32 + Winsock.

## 🚀 Build & run
1. `setup_sdk.bat` - one-time SDK install
2. `build_engine.bat` -> `engine-v2\bin\alamy2.exe`
3. `build_app.bat` -> `android-app\app\build\outputs\apk\debug\app-debug.apk`
4. `run.bat` - starts the engine; it auto-detects the device, opens the USB tunnel, installs/launches the app (`run.bat --install` forces APK reinstall)

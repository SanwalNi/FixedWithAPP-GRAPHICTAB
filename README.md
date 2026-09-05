# 🎨 Alamy v2 — Native Android Graphics Tablet for PC

**Alamy v2** turns your Android phone into an ultra-low-latency graphics tablet for Windows over **USB (ADB)** — with **no browser involved**.

## 🏗 Architecture

```
┌─────────────────────────┐         USB (adb reverse)        ┌──────────────────────────┐
│  ANDROID APP (Kotlin)   │   raw TCP 127.0.0.1:8080         │  ALAMY ENGINE v2 (C++20) │
│  android-app/           │  ─────────────────────────────►  │  engine-v2/              │
│  • Raw MotionEvent      │   16-byte packets, TCP_NODELAY   │  • Raw TCP server        │
│    capture @ 120–240 Hz │  ◄─────────────────────────────  │  • 1€ jitter filter      │
│  • Historical samples   │   PING / CONFIG_SYNC (2-way)     │  • Pen injection (Ink)   │
│  • 90 Hz forced display │                                  │  • Sub-stepped strokes   │
│  • Stylus gap bridging  │                                  │  • Console dashboard     │
│  • Coalesce-never-drop  │                                  └──────────────────────────┘
└─────────────────────────┘
```

### Why v2 replaced the Chrome/WebSocket pipeline (v1)

| v1 problem (Chrome) | v2 fix (native app) |
|---|---|
| Pointer events batched to browser vsync (~16 ms) + GC pauses | Raw `MotionEvent` pipeline, every digitizer sample incl. **historical** (120–240 Hz) |
| Browser ignores the phone's 90 Hz mode | App forces the high-refresh display mode via `preferredDisplayModeId` |
| JS drops MOVE packets under WS backpressure → **broken lines** | Coalesce-never-drop writer; alternating-MOVE thinning keeps segments continuous |
| Browser `pointercancel` steals gestures mid-stroke | Immersive fullscreen + foreground activity, no gesture interception |
| Capacitive stylus contact loss → UP/DOWN → line breaks | **Gap bridging**: deferred UP (30–80 ms, configurable); stroke silently continues |
| WS framing overhead | Raw 16-byte binary packets over TCP_NODELAY on both ends |
| Pen injection jump artifacts | PC sub-steps interpolated ≤24 px per injected point + final glide to UP |

## 🚀 Quick start

### One-time setup
1. JDK 17 (installed: Temurin 17.0.19)
2. Android SDK command-line tools + Gradle 8.7 → `E:\Android\...` (run `setup_sdk.bat`)
3. MinGW-w64 g++ (already present at `C:\msys64\ucrt64\bin\g++.exe`)

### Build
```cmd
build_engine.bat   :: engine-v2\bin\alamy2.exe
build_app.bat      :: android-app\app\build\outputs\apk\debug\app-debug.apk
```

### Run
```cmd
run.bat
```
The engine automatically: detects the phone over USB → sets up `adb reverse tcp:8080` → installs the APK if missing → launches the app → starts injecting input. Pass `run.bat --install` to force a reinstall after rebuilding the APK.

## ⌨ PC hotkeys
| Key | Action |
|---|---|
| `[1-9]` / `[0]` | Target monitor / all monitors |
| `[M]` | Cycle mode: Tablet → Pen+Pressure → Trackpad |
| `[F]` | Toggle 1€ jitter filter |
| `[A]` | Toggle aspect-ratio lock |
| `[R]` | Re-detect device + tunnel |
| `[O]` | Relaunch app on phone |
| `[Q]` | Quit |

## 📱 App UI
- Top bar: connection status · packet-rate + latency HUD · mode · monitor · settings
- Quick actions: Undo / Redo / Eraser / Right-click
- Settings: pressure emulation (Constant / Velocity — capacitive styli have no real pressure), ink feedback on/off, stylus gap-bridge window, clear trail
- Live ink feedback drawn at the panel's native 90 Hz

## 📁 Layout
```
android-app/        Native Android app (Kotlin, zero dependencies)
engine-v2/          PC engine (C++20, zero dependencies)
src/ web_client/    v1 Chrome pipeline (kept as fallback, still functional)
build_engine.bat    Build PC engine
build_app.bat       Build APK
setup_sdk.bat       One-time SDK setup
run.bat             One-click start
```

## ⚙ Requirements
- Windows 10/11, JDK 17, MinGW g++ (C++20), ADB (`E:\platform-tools-latest-windows\platform-tools\adb.exe` or on PATH)
- Android 8.0+ (API 26) device with USB debugging enabled; high-refresh mode used automatically when available


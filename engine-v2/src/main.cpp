// ============================================================================
//  Alamy Engine v2 - PC side entry point
//  Pairs with the native Android app (android-app/) over an adb reverse
//  USB tunnel. Raw 16-byte binary packets, no browser involved.
// ============================================================================
#include "protocol.hpp"
#include "display.hpp"
#include "input_injector.hpp"
#include "server.hpp"
#include "adb.hpp"

#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_CYAN    "\033[36m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_RED     "\033[31m"

static void enable_vt() {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

static void set_dpi_aware() {
    using Fn = BOOL(WINAPI*)(DWORD);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    auto fn = (Fn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (fn) fn((DWORD)-4 /*PER_MONITOR_AWARE_V2*/);
}

static const char* mode_name(TabletMode m) {
    switch (m) {
        case TabletMode::PEN_PRESSURE:   return "Pen + Pressure (Windows Ink)";
        case TabletMode::RELATIVE_MOUSE: return "Trackpad (Relative)";
        default:                         return "Tablet (1:1 Absolute)";
    }
}

int main(int argc, char* argv[]) {
    enable_vt();
    set_dpi_aware();
    SetConsoleTitleA("Alamy Engine v2");

    std::cout << C_BOLD << C_CYAN <<
        "  _    _        _   _                    _____ _                     \n"
        " | |  | |      | | | |                  / ____| |                    \n"
        " | |  | | __ _| |_| |__   ___  _ __   | (___ | |_ ___ _ __ ___  ___ \n"
        " | |  | |/ _` | __| '_ \\ / _ \\| '_ \\   \\___ \\| __/ _ \\ '_ ` _ \\/ __|\n"
        " | |__| | (_| | |_| | | | (_) | | | |  ____) | ||  __/ | | | | \\__ \\\n"
        "  \\____/ \\__,_|\\__|_| |_|\\___/|_| |_| |_____/ \\__\\___|_| |_| |_|___/\n"
        << C_RESET;
    std::cout << C_BOLD << "  Native Android Tablet -> PC  |  Engine v2.0\n" << C_RESET;
    std::cout << "  ---------------------------------------------------------\n\n";

    bool force_install = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--install")) force_install = true;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cout << C_RED "[FATAL] WSAStartup failed.\n" C_RESET;
        return 1;
    }

    auto injector = std::make_shared<InputInjector>();
    auto server   = std::make_unique<TcpServer>(8080, injector);
    Adb adb;

    if (!adb.find_adb()) {
        std::cout << C_YELLOW "[!] adb.exe not found - app auto-install/launch disabled.\n"
                  << "    Connect manually: adb reverse tcp:8080 tcp:8080\n" C_RESET;
    } else {
        std::cout << C_GREEN "[OK] ADB: " << adb.path() << C_RESET "\n";
    }

    // APK location (built by android-app/gradlew)
    std::filesystem::path apk = std::filesystem::path(argv[0]).parent_path()
        / ".." / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk";

    if (!server->start()) {
        std::cout << C_RED "[!] Could not start engine server on port 8080.\n" C_RESET;
        return 1;
    }
    std::cout << C_GREEN "[OK] Engine listening on tcp:8080 (loopback)\n\n" C_RESET;

    // Device setup: reverse tunnel + app install/launch
    std::string serial;
    auto setup_device = [&]() {
        auto devices = adb.available() ? adb.list_devices() : std::vector<std::string>{};
        if (devices.empty()) {
            std::cout << C_YELLOW "[..] No Android device yet - plug in USB and enable USB debugging.\n" C_RESET;
            serial.clear();
            return;
        }
        serial = devices[0];
        std::cout << C_GREEN "[OK] Device: " << serial << C_RESET "\n";
        if (adb.reverse(8080)) {
            std::cout << C_GREEN "[OK] USB reverse tunnel: phone:8080 -> PC:8080\n" C_RESET;
        } else {
            std::cout << C_RED "[!] adb reverse failed.\n" C_RESET;
        }
        bool installed = adb.is_app_installed();
        if (force_install || !installed) {
            std::cout << "[..] Installing Alamy Tablet app...\n";
            if (adb.install_apk(apk.string())) {
                std::cout << C_GREEN "[OK] App installed.\n" C_RESET;
            } else {
                std::cout << C_RED "[!] App install failed (build the APK first - see build_app.bat).\n"
                          << "    Expected at: " << apk.string() << "\n" C_RESET;
            }
        }
        if (adb.launch_app()) {
            std::cout << C_GREEN "[OK] Alamy Tablet launched on device.\n" C_RESET;
        }
    };
    setup_device();

    std::cout << "\n" C_BOLD "=== Controls ===\n" C_RESET
              "  [1-9] monitor | [0] all monitors | [M] mode | [F] filter | [A] aspect\n"
              "  [R] re-detect device | [O] relaunch app | [Q] quit\n\n";

    bool running = true;
    auto last_hud = std::chrono::steady_clock::now();
    while (running) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 0xE0) { _getch(); continue; }
            char ch = (char)c;
            auto cfg = injector->get_config();
            if (ch >= '0' && ch <= '9') {
                cfg.target_monitor = ch - '0';
                injector->set_config(cfg);
                server->push_config(injector->build_config_sync());
            } else if (ch == 'm' || ch == 'M') {
                cfg.mode = (TabletMode)(((int)cfg.mode + 1) % 3);
                injector->set_config(cfg);
                server->push_config(injector->build_config_sync());
            } else if (ch == 'f' || ch == 'F') {
                cfg.enable_filter = !cfg.enable_filter;
                injector->set_config(cfg);
            } else if (ch == 'a' || ch == 'A') {
                cfg.keep_aspect_ratio = !cfg.keep_aspect_ratio;
                injector->set_config(cfg);
            } else if (ch == 'r' || ch == 'R') {
                setup_device();
            } else if (ch == 'o' || ch == 'O') {
                adb.launch_app();
            } else if (ch == 'q' || ch == 'Q') {
                running = false;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_hud).count() >= 200) {
            last_hud = now;
            auto st = server->get_stats();
            auto cfg = injector->get_config();
            std::cout << "\033[2K\r" C_BOLD "Status: " C_RESET
                      << (st.client_connected ? C_GREEN "\xe2\x97\x8f LIVE" C_RESET
                                              : C_RED "\xe2\x97\x8b WAITING FOR PHONE" C_RESET)
                      << " | " C_CYAN "Rate: " C_RESET << std::setw(4) << st.packets_per_sec << " Hz"
                      << " | " C_YELLOW "Mode: " C_RESET << mode_name(cfg.mode)
                      << " | " C_CYAN "Mon: " C_RESET
                      << (cfg.target_monitor == 0 ? "ALL" : std::to_string(cfg.target_monitor))
                      << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    std::cout << "\n\n" C_CYAN "[*] Shutting down Alamy Engine v2...\n" C_RESET;
    server->stop();
    WSACleanup();
    return 0;
}

#include "protocol.hpp"
#include "filter.hpp"
#include "monitor_helper.hpp"
#include "input_injector.hpp"
#include "socket_server.hpp"
#include "adb_manager.hpp"
#include "net_helper.hpp"

#include <iostream>
#include <iomanip>
#include <memory>
#include <thread>
#include <chrono>
#include <conio.h>
#include <filesystem>
#include <winsock2.h>

// ANSI Colors for console UI
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_BG_BLUE "\033[44m"

void enable_virtual_terminal() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
}

void print_banner() {
    std::cout << COLOR_CYAN << COLOR_BOLD;
    std::cout << R"(
    █████╗ ██╗      █████╗ ███╗   ███╗██╗   ██╗
   ██╔══██╗██║     ██╔══██╗████╗ ████║╚██╗ ██╔╝
   ███████║██║     ███████║██╔████╔██║ ╚████╔╝ 
   ██╔══██║██║     ██╔══██║██║╚██╔╝██║  ╚██╔╝  
   ██║  ██║███████╗██║  ██║██║ ╚═╝ ██║   ██║   
   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝   ╚═╝   
)" << COLOR_RESET;
    std::cout << COLOR_BOLD << "   Ultra-Low-Latency Mobile Graphics Tablet Engine v1.0\n" << COLOR_RESET;
    std::cout << "   ------------------------------------------------------\n\n";
}

int main(int argc, char* argv[]) {
    // Initialize Winsock FIRST — before any networking calls
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[FATAL] WSAStartup failed.\n";
        return 1;
    }

    enable_virtual_terminal();
    SetConsoleTitleA("Alamy - High Speed Mobile Graphics Tablet");

    print_banner();

    // 1. Detect Local IPs (Hotspot / Wi-Fi / USB)
    auto local_ips = NetworkHelper::get_local_ipv4_addresses();
    std::string primary_ip = "127.0.0.1";
    if (!local_ips.empty()) {
        primary_ip = local_ips[0];
    }

    // 2. Initialize Components
    auto injector = std::make_shared<InputInjector>();
    ADBManager adb;

    std::string exe_path = std::filesystem::current_path().string();
    std::string web_root = exe_path + "\\web_client";
    if (!std::filesystem::exists(web_root)) {
        web_root = ".\\web_client";
    }

    int server_port = 8080;
    auto server = std::make_unique<SocketServer>(server_port, web_root, injector);

    if (!server->start()) {
        std::cout << COLOR_RED << "[!] Failed to start local server on port " << server_port << COLOR_RESET << "\n";
        return 1;
    }
    std::cout << COLOR_GREEN << "[✓] Local Engine Server listening on port " << server_port << COLOR_RESET << "\n";
    std::cout << "    • Primary URL: " << COLOR_BOLD << COLOR_GREEN << "http://" << primary_ip << ":" << server_port << COLOR_RESET << "\n";
    for (size_t i = 1; i < local_ips.size(); ++i) {
        std::cout << "    • Alt URL:     http://" << local_ips[i] << ":" << server_port << "\n";
    }
    std::cout << "    • USB Loopback: http://localhost:" << server_port << "\n\n";

    // 3. Check ADB & Connected Devices
    std::cout << COLOR_CYAN << "[*] Searching for ADB and Android devices..." << COLOR_RESET << "\n";
    if (!adb.find_adb()) {
        std::cout << COLOR_YELLOW << "[!] ADB not found automatically. Using system PATH fallback." << COLOR_RESET << "\n";
    } else {
        std::cout << COLOR_GREEN << "[✓] Found ADB: " << adb.get_adb_path() << COLOR_RESET << "\n";
    }

    auto devices = adb.list_devices();
    std::string active_serial = "";
    if (devices.empty()) {
        std::cout << COLOR_YELLOW << "[!] No Android devices detected via USB debugging yet.\n";
        std::cout << "    Please open the URL below in Chrome on your phone:\n" << COLOR_RESET;
        std::cout << "    --> " << COLOR_BOLD << COLOR_GREEN << "http://" << primary_ip << ":" << server_port << COLOR_RESET << "\n";
    } else {
        active_serial = devices[0].serial;
        std::cout << COLOR_GREEN << "[✓] Connected Device: " << devices[0].model << " (" << active_serial << ")" << COLOR_RESET << "\n";
        
        // Setup ADB reverse port forwarding
        // FIX: prefer the USB reverse tunnel (localhost on the phone) - it routes
        // through adb itself, so Windows Firewall can never block it. Previously
        // the phone was sent the LAN/hotspot IP, which fails silently when the
        // firewall blocks inbound 8080 (very common: canvas never connects, or
        // reconnects mid-stroke = broken lines).
        bool tunnel_ok = adb.reverse_port(active_serial, server_port);
        
        // Launch primary Hotspot IP on phone screen
        std::string target_url = tunnel_ok
            ? ("http://localhost:" + std::to_string(server_port))
            : ("http://" + primary_ip + ":" + std::to_string(server_port));
        std::cout << COLOR_CYAN << "[*] Launching drawing surface on phone: " << target_url << "..." << COLOR_RESET << "\n";
        adb.open_url_on_device(active_serial, target_url);
    }

    // 3. Display Info
    auto monitors = DisplayManager::get_monitors();
    std::cout << "\n" << COLOR_BOLD << "=== Active Displays ===" << COLOR_RESET << "\n";
    for (const auto& mon : monitors) {
        std::cout << "  [" << mon.index << "] " << mon.device_name 
                  << " (" << mon.width << "x" << mon.height << ")"
                  << (mon.is_primary ? " [PRIMARY]" : "") << "\n";
    }
    std::cout << "  [0] Virtual Desktop (All Monitors Combined)\n\n";

    std::cout << COLOR_BOLD << "=== Controls & Hotkeys ===" << COLOR_RESET << "\n";
    std::cout << "  " << COLOR_CYAN << "[1-" << monitors.size() << "]" << COLOR_RESET << " Select Target Monitor  |  "
              << COLOR_CYAN << "[0]" << COLOR_RESET << " Span All Monitors\n";
    std::cout << "  " << COLOR_CYAN << "[M]" << COLOR_RESET << " Cycle Mode (Tablet / Pen Pressure / Trackpad)\n";
    std::cout << "  " << COLOR_CYAN << "[F]" << COLOR_RESET << " Toggle 1€ Jitter Filter\n";
    std::cout << "  " << COLOR_CYAN << "[A]" << COLOR_RESET << " Toggle Aspect Ratio Lock\n";
    std::cout << "  " << COLOR_CYAN << "[R]" << COLOR_RESET << " Refresh Devices & ADB Tunnel\n";
    std::cout << "  " << COLOR_CYAN << "[O]" << COLOR_RESET << " Re-Open Screen on Phone\n";
    std::cout << "  " << COLOR_CYAN << "[Q]" << COLOR_RESET << " Exit Program\n\n";

    // 4. Interactive Main Loop & Real-Time Dashboard
    bool running = true;
    auto last_hud_update = std::chrono::steady_clock::now();

    while (running) {
        // Check keyboard input without blocking
        if (_kbhit()) {
            int raw_ch = _getch();

            // Filter extended key prefixes (arrow keys, F-keys emit 0x00 or 0xE0 followed by scan code)
            if (raw_ch == 0 || raw_ch == 0xE0) {
                _getch(); // consume the second byte of the extended sequence
                continue; // ignore extended keys entirely
            }

            char ch = static_cast<char>(raw_ch);
            auto cfg = injector->get_config();

            int max_mon = static_cast<int>(monitors.size());
            if (max_mon > 9) max_mon = 9; // Clamp to single-digit range

            if (ch >= '0' && ch <= ('0' + max_mon)) {
                cfg.target_monitor = ch - '0';
                injector->set_config(cfg);
            } else if (ch == 'm' || ch == 'M') {
                if (cfg.mode == TabletMode::ABSOLUTE_TABLET) {
                    cfg.mode = TabletMode::PEN_PRESSURE;
                } else if (cfg.mode == TabletMode::PEN_PRESSURE) {
                    cfg.mode = TabletMode::RELATIVE_MOUSE;
                } else {
                    cfg.mode = TabletMode::ABSOLUTE_TABLET;
                }
                injector->set_config(cfg);
            } else if (ch == 'f' || ch == 'F') {
                cfg.enable_filter = !cfg.enable_filter;
                injector->set_config(cfg);
            } else if (ch == 'a' || ch == 'A') {
                cfg.keep_aspect_ratio = !cfg.keep_aspect_ratio;
                injector->set_config(cfg);
            } else if (ch == 'r' || ch == 'R') {
                devices = adb.list_devices();
                if (!devices.empty()) {
                    active_serial = devices[0].serial;
                    adb.reverse_port(active_serial, server_port);
                }
            } else if (ch == 'o' || ch == 'O') {
                if (!active_serial.empty()) {
                    adb.open_url_on_device(active_serial, "http://localhost:" + std::to_string(server_port));
                }
            } else if (ch == 'q' || ch == 'Q') {
                running = false;
                break;
            }
        }

        // Live Dashboard HUD update every 200ms
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_hud_update).count() >= 200) {
            last_hud_update = now;

            auto stats = server->get_stats();
            auto cfg = injector->get_config();

            std::string mode_str = "Tablet (1:1 Absolute)";
            if (cfg.mode == TabletMode::PEN_PRESSURE) mode_str = "Pen + Pressure (WinInk)";
            else if (cfg.mode == TabletMode::RELATIVE_MOUSE) mode_str = "Trackpad (Relative)";

            std::cout << "\033[2K\r"
                      << COLOR_BOLD << "Status: " << COLOR_RESET 
                      << (stats.is_client_connected ? (std::string(COLOR_GREEN) + "● LIVE [USB ACTIVE]" + COLOR_RESET) : (std::string(COLOR_RED) + "○ WAITING FOR PHONE" + COLOR_RESET))
                      << " | " << COLOR_CYAN << "Rate: " << COLOR_RESET << std::setw(4) << stats.packets_per_sec << " Hz"
                      << " | " << COLOR_MAGENTA << "Latency: " << COLOR_RESET << std::fixed << std::setprecision(2) << stats.current_latency_ms << " ms"
                      << " | " << COLOR_YELLOW << "Mode: " << COLOR_RESET << mode_str
                      << " | " << COLOR_CYAN << "Mon: " << COLOR_RESET << (cfg.target_monitor == 0 ? "ALL" : std::to_string(cfg.target_monitor))
                      << " | " << COLOR_GREEN << "Filter: " << COLOR_RESET << (cfg.enable_filter ? "1€ ON" : "OFF")
                      << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    std::cout << "\n\n" << COLOR_CYAN << "[*] Shutting down Alamy engine..." << COLOR_RESET << "\n";
    server->stop();
    WSACleanup();
    return 0;
}

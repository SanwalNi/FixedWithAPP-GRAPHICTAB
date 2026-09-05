#pragma once
// ============================================================================
//  Alamy Engine v2 - Input injection
//
//  v1 -> v2 continuity fixes (why lines used to break on the PC side):
//   * Sub-stepped interpolation: large jumps between consecutive samples are
//     split into <=24 px steps before injection. Many creative apps rasterize
//     pen input per injected point; a 300 px jump shows as a broken segment.
//   * Guaranteed final move: a TOUCH_UP always injects a final UPDATE at the
//     up position before lifting, so the stroke reaches its true end point.
//   * Pen mode uses the Windows Synthetic Pointer API (real WM_POINTER pen
//     with pressure/tilt/eraser) with automatic SendInput fallback.
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "protocol.hpp"
#include "filter.hpp"
#include "display.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

class InputInjector {
public:
    struct Config {
        TabletMode mode = TabletMode::ABSOLUTE_TABLET;
        int target_monitor = 1;      // 1 = first monitor, 0 = all monitors
        bool keep_aspect_ratio = true;
        double tablet_aspect = 1.7777; // auto-synced from the app's view size
        bool enable_filter = true;
        double filter_min_cutoff = 2.0;
        double filter_beta = 0.010;
        double relative_speed = 1.5;
    };

    InputInjector();
    ~InputInjector();

    void set_config(const Config& cfg);
    Config get_config() const;

    void process_packet(const TouchPacket& packet);
    void refresh_display_info();
    void reset_state();

    // Push current config to the connected app (called by the server when
    // the console hotkeys change something). Builds a CONFIG_SYNC packet.
    TouchPacket build_config_sync() const;

private:
    mutable std::mutex config_mutex_;
    Config config_;
    PointFilter2D filter_;

    std::atomic<bool> touching_{false};
    std::atomic<int>  active_pointer_{-1};
    std::atomic<int>  last_pressure_{0};

    // last injected screen position (for sub-stepping)
    int last_x_ = 0;
    int last_y_ = 0;
    bool have_last_ = false;

    // cached display data
    std::vector<MonitorInfo> monitors_;
    RECT virtual_rect_{};

    // synthetic pen (loaded dynamically from user32.dll)
    void* pen_device_ = nullptr;   // HSYNTHETICPOINTERDEVICE
    bool pen_available_ = false;

    // internal pipeline
    void handle_down(const TouchPacket& p, const Config& cfg);
    void handle_move(const TouchPacket& p, const Config& cfg, bool in_contact);
    void handle_up(const TouchPacket& p, const Config& cfg);
    void handle_scroll(const TouchPacket& p, const Config& cfg);
    void handle_button(const TouchPacket& p, bool down);

    // inject one interpolated segment
    void inject_to(int x, int y, uint16_t pressure, ToolType tool, bool in_contact,
                   const Config& cfg);
    void inject_pen(int x, int y, uint16_t pressure, ToolType tool,
                    bool in_contact, bool is_up, bool is_down);
    void inject_mouse_abs(int x, int y);
    void inject_mouse_rel(int dx, int dy);
    void send_mouse_button(bool right, bool down);
    void send_key_combo(WORD vk1, WORD vk2 = 0);

    static double steady_now_sec();
};

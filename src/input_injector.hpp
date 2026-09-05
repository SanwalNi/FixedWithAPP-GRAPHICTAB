#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "protocol.hpp"
#include "filter.hpp"
#include "monitor_helper.hpp"
#include <atomic>
#include <chrono>
#include <mutex>

class InputInjector {
public:
    struct Config {
        TabletMode mode = TabletMode::ABSOLUTE_TABLET;
        int target_monitor = 1; // 1 = First Monitor, 0 = Virtual Desktop (all)
        bool keep_aspect_ratio = true;
        double tablet_aspect = 16.0 / 9.0; // Standard landscape aspect
        bool enable_filter = true;
        // FIX (shutter lag): old defaults (1.2 Hz / 0.006) made slow strokes feel
        // rubber-banded ~300ms behind the finger. These keep jitter removal but
        // cut perceived lag by more than half.
        double filter_min_cutoff = 2.0;
        double filter_beta = 0.010;
        double relative_speed = 1.5;
        bool invert_scroll = false;
    };

private:
    mutable std::mutex config_mutex;
    Config config;
    PointFilter2D filter;
    std::atomic<bool> is_touching{false};
    std::atomic<bool> is_right_down{false};
    // FIX (broken lines): palm/multi-touch rejection - the FIRST pointer that
    // touches down owns the stroke; other fingers are ignored until it lifts.
    std::atomic<int> active_pointer_id{-1};
    int last_x = 0;
    int last_y = 0;
    bool has_prev_relative = false;
    double prev_rel_x = 0;
    double prev_rel_y = 0;

    // Cached display info (avoid per-frame syscalls — PERF-01/02)
    std::vector<MonitorInfo> cached_monitors;
    RECT cached_virtual_rect = {};
    double cached_virt_w = 1920.0;
    double cached_virt_h = 1080.0;

    // Synthetic Pen API handles (loaded dynamically from user32.dll for max compatibility)
    HSYNTHETICPOINTERDEVICE hSyntheticPen = nullptr;
    bool synthetic_pen_available = false;

    void init_synthetic_pen();
    void destroy_synthetic_pen();

    // Internal helpers that take a config copy (no locking inside)
    void handle_touch_down_impl(const TouchPacket& packet, const Config& cfg);
    void handle_touch_move_impl(const TouchPacket& packet, const Config& cfg, bool in_contact);
    void handle_touch_up_impl(const TouchPacket& packet, const Config& cfg);
    void handle_scroll_impl(int delta_y, const Config& cfg);
    void handle_button_impl(uint8_t button_id, bool is_down);

    // Send a keyboard shortcut via SendInput
    void send_key_combo(WORD vk1, WORD vk2 = 0);

public:
    InputInjector();
    ~InputInjector();

    void set_config(const Config& new_config);
    Config get_config() const;

    // Refresh cached monitor/display data (call on config change or monitor hotplug)
    void refresh_display_info();

    void process_packet(const TouchPacket& packet);
    void handle_touch_down(const TouchPacket& packet);
    void handle_touch_move(const TouchPacket& packet);
    void handle_touch_up(const TouchPacket& packet);
    void handle_scroll(int delta_y);
    void handle_button(uint8_t button_id, bool is_down);

    void reset_state();
};

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>
#include <iostream>

struct MonitorInfo {
    int index;
    HMONITOR handle;
    RECT rect; // Absolute screen coordinates
    int width;
    int height;
    bool is_primary;
    std::string device_name;
};

class DisplayManager {
public:
    static std::vector<MonitorInfo> get_monitors() {
        std::vector<MonitorInfo> monitors;
        
        EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMon, HDC hdc, LPRECT lprc, LPARAM pData) -> BOOL {
            auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(pData);
            MONITORINFOEXA mi;
            mi.cbSize = sizeof(MONITORINFOEXA);
            
            if (GetMonitorInfoA(hMon, &mi)) {
                MonitorInfo info;
                info.index = static_cast<int>(list->size() + 1);
                info.handle = hMon;
                info.rect = mi.rcMonitor;
                info.width = mi.rcMonitor.right - mi.rcMonitor.left;
                info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
                info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                info.device_name = mi.szDevice;
                list->push_back(info);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

        return monitors;
    }

    static RECT get_virtual_desktop_rect() {
        RECT r;
        r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return r;
    }

    // Convert normalized [0..65535] tablet coordinate to absolute Windows mouse coordinate [0..65535]
    // Accepts pre-fetched monitor data to avoid per-call syscalls (PERF-01/02 fix)
    static void map_coordinate_to_target(
        double norm_x, double norm_y, // 0.0 to 1.0
        int target_monitor_idx,       // 0 = All/Virtual Desktop, 1..N = specific monitor
        bool keep_aspect_ratio,
        double tablet_aspect,         // e.g. width / height of phone (e.g. 19.5/9 or 16/9)
        int& out_abs_x, int& out_abs_y,
        const std::vector<MonitorInfo>& monitors, // Pre-fetched monitor list
        const RECT& v_rect                        // Pre-fetched virtual desktop rect
    ) {
        RECT target_rect;

        if (target_monitor_idx >= 1 && target_monitor_idx <= static_cast<int>(monitors.size())) {
            target_rect = monitors[target_monitor_idx - 1].rect;
        } else {
            target_rect = v_rect;
        }

        double tgt_w = target_rect.right - target_rect.left;
        double tgt_h = target_rect.bottom - target_rect.top;

        double final_x = norm_x;
        double final_y = norm_y;

        if (keep_aspect_ratio && tablet_aspect > 0.01 && tgt_h > 0.01) {
            double mon_aspect = tgt_w / tgt_h;
            if (tablet_aspect > mon_aspect) {
                // Phone is wider than monitor -> letterbox top/bottom
                double scale = mon_aspect / tablet_aspect;
                double offset = (1.0 - scale) / 2.0;
                final_y = (norm_y - offset) / scale;
                if (final_y < 0.0) final_y = 0.0;
                if (final_y > 1.0) final_y = 1.0;
            } else {
                // Phone is taller than monitor -> pillarbox left/right
                double scale = tablet_aspect / mon_aspect;
                double offset = (1.0 - scale) / 2.0;
                final_x = (norm_x - offset) / scale;
                if (final_x < 0.0) final_x = 0.0;
                if (final_x > 1.0) final_x = 1.0;
            }
        }

        // Map to absolute pixel position on virtual screen
        double pixel_x = target_rect.left + final_x * tgt_w;
        double pixel_y = target_rect.top + final_y * tgt_h;

        // Convert pixel position to Windows SendInput 0..65535 normalized virtual space
        double v_w = v_rect.right - v_rect.left;
        double v_h = v_rect.bottom - v_rect.top;

        if (v_w <= 0) v_w = 1920;
        if (v_h <= 0) v_h = 1080;

        out_abs_x = static_cast<int>(((pixel_x - v_rect.left) * 65535.0) / v_w);
        out_abs_y = static_cast<int>(((pixel_y - v_rect.top) * 65535.0) / v_h);

        if (out_abs_x < 0) out_abs_x = 0;
        if (out_abs_x > 65535) out_abs_x = 65535;
        if (out_abs_y < 0) out_abs_y = 0;
        if (out_abs_y > 65535) out_abs_y = 65535;
    }
};

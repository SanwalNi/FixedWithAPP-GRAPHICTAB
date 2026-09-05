#pragma once
// ============================================================================
//  Display enumeration + tablet->screen coordinate mapping
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cmath>
#include <string>
#include <vector>

struct MonitorInfo {
    int index = 0;
    std::string device_name;
    RECT rect = {};          // virtual-desktop coordinates
    int width = 0;
    int height = 0;
    bool is_primary = false;
};

class DisplayManager {
public:
    static std::vector<MonitorInfo> get_monitors() {
        std::vector<MonitorInfo> out;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lparam);
            MONITORINFOEXA mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoA(hmon, &mi)) {
                MonitorInfo info;
                info.index = (int)list->size() + 1;
                info.device_name = mi.szDevice;
                info.rect = mi.rcMonitor;
                info.width = mi.rcMonitor.right - mi.rcMonitor.left;
                info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
                info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                list->push_back(info);
            }
            return TRUE;
        }, (LPARAM)&out);
        return out;
    }

    static RECT get_virtual_rect() {
        RECT r{};
        r.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        r.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        return r;
    }

    /**
     * Map normalized (0..1) tablet coordinates onto the target monitor.
     * target_monitor: 0 = entire virtual desktop, 1..N = that monitor.
     * keep_aspect: letterbox the tablet area inside the target using
     *              tablet_aspect (width/height), else stretch to fill.
     */
    static void map_coordinate_to_target(
        double nx, double ny,
        int target_monitor, bool keep_aspect, double tablet_aspect,
        int& out_x, int& out_y,
        const std::vector<MonitorInfo>& monitors, const RECT& virtual_rect)
    {
        double tx = 0, ty = 0, tw = 0, th = 0;
        if (target_monitor <= 0 || target_monitor > (int)monitors.size()) {
            tx = virtual_rect.left;  ty = virtual_rect.top;
            tw = virtual_rect.right - virtual_rect.left;
            th = virtual_rect.bottom - virtual_rect.top;
        } else {
            const RECT& r = monitors[target_monitor - 1].rect;
            tx = r.left; ty = r.top;
            tw = r.right - r.left;
            th = r.bottom - r.top;
        }
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;

        if (keep_aspect && tablet_aspect > 0.0) {
            double scale = (tw / tablet_aspect < th) ? (tw / tablet_aspect) : th;
            double cw = tablet_aspect * scale;
            double ch = scale;
            tx += (tw - cw) * 0.5;
            ty += (th - ch) * 0.5;
            tw = cw;
            th = ch;
        }

        out_x = (int)lround(tx + nx * (tw - 1));
        out_y = (int)lround(ty + ny * (th - 1));
    }
};

#include "input_injector.hpp"
#include <iostream>
#include <cmath>

// Structure definitions for Synthetic Pointer API
#ifndef POINTER_FLAG_NONE
#define POINTER_FLAG_NONE               0x00000000
#define POINTER_FLAG_NEW                0x00000001
#define POINTER_FLAG_INRANGE            0x00000002
#define POINTER_FLAG_INCONTACT          0x00000004
#define POINTER_FLAG_FIRSTBUTTON        0x00000010
#define POINTER_FLAG_SECONDBUTTON       0x00000020
#define POINTER_FLAG_PRIMARY            0x00002000
#define POINTER_FLAG_CONFIDENCE         0x00004000
#define POINTER_FLAG_DOWN               0x00010000
#define POINTER_FLAG_UPDATE             0x00020000
#define POINTER_FLAG_UP                 0x00040000
#define PEN_FLAG_NONE                   0x00000000
#define PEN_FLAG_BARREL                 0x00000001
#define PEN_FLAG_INVERTED               0x00000002
#define PEN_FLAG_ERASER                 0x00000004
#define PEN_MASK_NONE                   0x00000000
#define PEN_MASK_PRESSURE               0x00000001
#define PEN_MASK_ROTATION               0x00000002
#define PEN_MASK_TILT_X                 0x00000004
#define PEN_MASK_TILT_Y                 0x00000008
#endif

// Generic function pointers with DWORD/UINT types to avoid enum type mismatches across SDK versions
typedef HSYNTHETICPOINTERDEVICE (WINAPI *pfnCreateSyntheticPointerDevice)(POINTER_INPUT_TYPE pointerType, ULONG maxCount, DWORD mode);
typedef BOOL (WINAPI *pfnInjectSyntheticPointerInput)(HSYNTHETICPOINTERDEVICE device, const POINTER_TYPE_INFO* pointerInfo, UINT32 count);
typedef VOID (WINAPI *pfnDestroySyntheticPointerDevice)(HSYNTHETICPOINTERDEVICE device);

static pfnCreateSyntheticPointerDevice g_CreateSyntheticPointerDevice = nullptr;
static pfnInjectSyntheticPointerInput g_InjectSyntheticPointerInput = nullptr;
static pfnDestroySyntheticPointerDevice g_DestroySyntheticPointerDevice = nullptr;

InputInjector::InputInjector() {
    init_synthetic_pen();
    filter.set_params(config.filter_min_cutoff, config.filter_beta);
    refresh_display_info();
}

InputInjector::~InputInjector() {
    destroy_synthetic_pen();
}

void InputInjector::init_synthetic_pen() {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) hUser32 = LoadLibraryA("user32.dll");

    if (hUser32) {
        g_CreateSyntheticPointerDevice = (pfnCreateSyntheticPointerDevice)GetProcAddress(hUser32, "CreateSyntheticPointerDevice");
        g_InjectSyntheticPointerInput = (pfnInjectSyntheticPointerInput)GetProcAddress(hUser32, "InjectSyntheticPointerInput");
        g_DestroySyntheticPointerDevice = (pfnDestroySyntheticPointerDevice)GetProcAddress(hUser32, "DestroySyntheticPointerDevice");

        if (g_CreateSyntheticPointerDevice && g_InjectSyntheticPointerInput && g_DestroySyntheticPointerDevice) {
            hSyntheticPen = g_CreateSyntheticPointerDevice(PT_PEN, 1, 1 /* POINTER_FEEDBACK_DEFAULT */);
            if (hSyntheticPen) {
                synthetic_pen_available = true;
            }
        }
    }
}

void InputInjector::destroy_synthetic_pen() {
    if (hSyntheticPen && g_DestroySyntheticPointerDevice) {
        g_DestroySyntheticPointerDevice(hSyntheticPen);
        hSyntheticPen = nullptr;
        synthetic_pen_available = false;
    }
}

void InputInjector::refresh_display_info() {
    cached_monitors = DisplayManager::get_monitors();
    cached_virtual_rect = DisplayManager::get_virtual_desktop_rect();
    cached_virt_w = static_cast<double>(cached_virtual_rect.right - cached_virtual_rect.left);
    cached_virt_h = static_cast<double>(cached_virtual_rect.bottom - cached_virtual_rect.top);
    if (cached_virt_w <= 0) cached_virt_w = 1920.0;
    if (cached_virt_h <= 0) cached_virt_h = 1080.0;
}

void InputInjector::set_config(const Config& new_config) {
    std::lock_guard<std::mutex> lock(config_mutex);
    bool filter_changed = (new_config.filter_min_cutoff != config.filter_min_cutoff ||
                           new_config.filter_beta != config.filter_beta);
    bool monitor_changed = (new_config.target_monitor != config.target_monitor);
    config = new_config;

    // PERF-03: Only update filter params when they actually change
    if (filter_changed) {
        filter.set_params(config.filter_min_cutoff, config.filter_beta);
    }
    // Refresh display cache when monitor selection changes
    if (monitor_changed) {
        refresh_display_info();
    }
}

InputInjector::Config InputInjector::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex);
    return config;
}

void InputInjector::reset_state() {
    if (is_touching.load()) {
        TouchPacket dummy{};
        dummy.packet_type = static_cast<uint8_t>(PacketType::TOUCH_UP);
        handle_touch_up(dummy);
    }
    filter.reset();
    has_prev_relative = false;
    active_pointer_id.store(-1);
}

// BUG-03 FIX: Send keyboard shortcuts via SendInput
void InputInjector::send_key_combo(WORD vk1, WORD vk2) {
    INPUT inputs[4] = {};
    int count = 0;

    // Key down for modifier (if any)
    if (vk1 != 0) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = vk1;
        inputs[count].ki.dwFlags = 0;
        count++;
    }
    // Key down for main key
    if (vk2 != 0) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = vk2;
        inputs[count].ki.dwFlags = 0;
        count++;
    }
    // Key up for main key
    if (vk2 != 0) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = vk2;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    // Key up for modifier
    if (vk1 != 0) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = vk1;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    if (count > 0) {
        SendInput(count, inputs, sizeof(INPUT));
    }
}

void InputInjector::process_packet(const TouchPacket& packet) {
    // Take a snapshot of config under lock, then release immediately
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        cfg = config;
    }

    auto type = static_cast<PacketType>(packet.packet_type);
    switch (type) {
        case PacketType::TOUCH_DOWN:
        {
            // FIX (broken lines): only the first pointer down drives the stroke.
            // Previously every finger DOWN/UP overwrote the shared stroke state,
            // so a second finger lifting ended the pen mid-draw.
            int expected = -1;
            if (active_pointer_id.compare_exchange_strong(expected, packet.finger_id)) {
                handle_touch_down_impl(packet, cfg);
            }
            break;
        }
        case PacketType::TOUCH_MOVE:
            if (packet.finger_id == active_pointer_id.load()) {
                handle_touch_move_impl(packet, cfg, true);
            }
            break;
        case PacketType::HOVER:
            // FIX: pen hovering (in range, not touching) - move without contact.
            if (!is_touching.load() || packet.finger_id == active_pointer_id.load()) {
                handle_touch_move_impl(packet, cfg, false);
            }
            break;
        case PacketType::TOUCH_UP:
            if (packet.finger_id == active_pointer_id.load()) {
                active_pointer_id.store(-1);
                handle_touch_up_impl(packet, cfg);
            }
            break;
        case PacketType::SCROLL:
            handle_scroll_impl(static_cast<int16_t>(packet.y), cfg);
            break;
        case PacketType::BUTTON_DOWN:
            handle_button_impl(packet.flags, true);
            break;
        case PacketType::BUTTON_UP:
            handle_button_impl(packet.flags, false);
            break;
        default:
            break;
    }
}

// Public wrappers (for external callers that don't have a config snapshot)
void InputInjector::handle_touch_down(const TouchPacket& packet) {
    Config cfg;
    { std::lock_guard<std::mutex> lock(config_mutex); cfg = config; }
    handle_touch_down_impl(packet, cfg);
}

void InputInjector::handle_touch_move(const TouchPacket& packet) {
    Config cfg;
    { std::lock_guard<std::mutex> lock(config_mutex); cfg = config; }
    handle_touch_move_impl(packet, cfg, true);
}

void InputInjector::handle_touch_up(const TouchPacket& packet) {
    Config cfg;
    { std::lock_guard<std::mutex> lock(config_mutex); cfg = config; }
    handle_touch_up_impl(packet, cfg);
}

void InputInjector::handle_scroll(int delta_y) {
    Config cfg;
    { std::lock_guard<std::mutex> lock(config_mutex); cfg = config; }
    handle_scroll_impl(delta_y, cfg);
}

void InputInjector::handle_button(uint8_t button_id, bool is_down) {
    handle_button_impl(button_id, is_down);
}

void InputInjector::handle_touch_down_impl(const TouchPacket& packet, const Config& cfg) {
    is_touching.store(true);
    has_prev_relative = false;

    double norm_x = static_cast<double>(packet.x) / 65535.0;
    double norm_y = static_cast<double>(packet.y) / 65535.0;

    auto now = std::chrono::steady_clock::now();
    double timestamp_sec = std::chrono::duration<double>(now.time_since_epoch()).count();

    if (cfg.enable_filter) {
        filter.reset();
        filter.filter_point(norm_x, norm_y, timestamp_sec, norm_x, norm_y);
    }

    if (cfg.mode == TabletMode::ABSOLUTE_TABLET) {
        int win_x = 0, win_y = 0;
        DisplayManager::map_coordinate_to_target(norm_x, norm_y, cfg.target_monitor, cfg.keep_aspect_ratio,
            cfg.tablet_aspect, win_x, win_y, cached_monitors, cached_virtual_rect);
        last_x = win_x;
        last_y = win_y;

        INPUT input[2] = {};
        // 1. Move to position
        input[0].type = INPUT_MOUSE;
        input[0].mi.dx = win_x;
        input[0].mi.dy = win_y;
        input[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;

        // 2. Press Left Down (right button when the ERASER tool is active)
        bool down_eraser = (packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02);
        input[1].type = INPUT_MOUSE;
        input[1].mi.dx = win_x;
        input[1].mi.dy = win_y;
        input[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | (down_eraser ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN) | MOUSEEVENTF_VIRTUALDESK;

        SendInput(2, input, sizeof(INPUT));
    } else if (cfg.mode == TabletMode::PEN_PRESSURE && synthetic_pen_available && g_InjectSyntheticPointerInput) {
        int win_x = 0, win_y = 0;
        DisplayManager::map_coordinate_to_target(norm_x, norm_y, cfg.target_monitor, cfg.keep_aspect_ratio,
            cfg.tablet_aspect, win_x, win_y, cached_monitors, cached_virtual_rect);
        
        // Map 0..65535 to screen pixels using cached values
        int px = cached_virtual_rect.left + static_cast<int>((win_x * (cached_virtual_rect.right - cached_virtual_rect.left)) / 65535.0);
        int py = cached_virtual_rect.top + static_cast<int>((win_y * (cached_virtual_rect.bottom - cached_virtual_rect.top)) / 65535.0);

        POINTER_TYPE_INFO pti = {};
        pti.type = PT_PEN;
        pti.penInfo.pointerInfo.pointerType = PT_PEN;
        pti.penInfo.pointerInfo.pointerId = 1;
        pti.penInfo.pointerInfo.ptPixelLocation.x = px;
        pti.penInfo.pointerInfo.ptPixelLocation.y = py;
        // FIX: POINTER_FLAG_NEW is required on first injection per Windows docs.
        // POINTER_FLAG_CONFIDENCE tells apps this is an intentional stroke (not palm).
        // Missing these causes some drawing apps to silently reject the injection.
        pti.penInfo.pointerInfo.pointerFlags = POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN | POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE;
        // FIX: honor the ERASER tool / stylus eraser end so drawing apps really
        // switch to the eraser (PEN_FLAG_ERASER was never sent before).
        pti.penInfo.penFlags = ((packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02)) ? PEN_FLAG_ERASER : PEN_FLAG_NONE;
        pti.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
        
        // Pressure 0..1024
        uint32_t press = static_cast<uint32_t>((static_cast<double>(packet.pressure) / 65535.0) * 1024.0);
        if (press == 0) press = 512; // default if pressure not sent
        pti.penInfo.pressure = press;
        pti.penInfo.tiltX = packet.tilt_x;
        pti.penInfo.tiltY = packet.tilt_y;

        g_InjectSyntheticPointerInput(hSyntheticPen, &pti, 1);
    } else {
        // Relative mouse down (right button when the ERASER tool is active)
        prev_rel_x = norm_x;
        prev_rel_y = norm_y;
        has_prev_relative = true;

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = ((packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02)) ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void InputInjector::handle_touch_move_impl(const TouchPacket& packet, const Config& cfg, bool in_contact) {
    double norm_x = static_cast<double>(packet.x) / 65535.0;
    double norm_y = static_cast<double>(packet.y) / 65535.0;

    auto now = std::chrono::steady_clock::now();
    double timestamp_sec = std::chrono::duration<double>(now.time_since_epoch()).count();

    if (cfg.enable_filter) {
        filter.filter_point(norm_x, norm_y, timestamp_sec, norm_x, norm_y);
    }

    if (cfg.mode == TabletMode::ABSOLUTE_TABLET) {
        int win_x = 0, win_y = 0;
        DisplayManager::map_coordinate_to_target(norm_x, norm_y, cfg.target_monitor, cfg.keep_aspect_ratio,
            cfg.tablet_aspect, win_x, win_y, cached_monitors, cached_virtual_rect);
        last_x = win_x;
        last_y = win_y;

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = win_x;
        input.mi.dy = win_y;
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &input, sizeof(INPUT));
    } else if (cfg.mode == TabletMode::PEN_PRESSURE && synthetic_pen_available && g_InjectSyntheticPointerInput) {
        int win_x = 0, win_y = 0;
        DisplayManager::map_coordinate_to_target(norm_x, norm_y, cfg.target_monitor, cfg.keep_aspect_ratio,
            cfg.tablet_aspect, win_x, win_y, cached_monitors, cached_virtual_rect);

        int px = cached_virtual_rect.left + static_cast<int>((win_x * (cached_virtual_rect.right - cached_virtual_rect.left)) / 65535.0);
        int py = cached_virtual_rect.top + static_cast<int>((win_y * (cached_virtual_rect.bottom - cached_virtual_rect.top)) / 65535.0);

        POINTER_TYPE_INFO pti = {};
        pti.type = PT_PEN;
        pti.penInfo.pointerInfo.pointerType = PT_PEN;
        pti.penInfo.pointerInfo.pointerId = 1;
        pti.penInfo.pointerInfo.ptPixelLocation.x = px;
        pti.penInfo.pointerInfo.ptPixelLocation.y = py;
        // FIX: stale MOVE after a reconnect reset used to inject a ghost
        // in-contact pen (phantom strokes / broken lines). Degrade to hover when
        // the stroke is no longer active, and honor the ERASER tool here too.
        bool contact = in_contact && is_touching.load();
        bool eraser = (packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02);
        pti.penInfo.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE | POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE
                                             | (contact ? POINTER_FLAG_INCONTACT : 0);
        pti.penInfo.penFlags = eraser ? PEN_FLAG_ERASER : PEN_FLAG_NONE;
        pti.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;

        uint32_t press = static_cast<uint32_t>((static_cast<double>(packet.pressure) / 65535.0) * 1024.0);
        if (press == 0) press = contact ? 512 : 0;
        pti.penInfo.pressure = press;
        pti.penInfo.tiltX = packet.tilt_x;
        pti.penInfo.tiltY = packet.tilt_y;

        g_InjectSyntheticPointerInput(hSyntheticPen, &pti, 1);
    } else {
        if (!in_contact) return; // FIX: hover must not move the relative cursor
        // Relative mouse move (BUG-08: cached desktop dimensions, not hardcoded 1920x1080)
        if (!has_prev_relative) {
            prev_rel_x = norm_x;
            prev_rel_y = norm_y;
            has_prev_relative = true;
            return;
        }

        double dx = (norm_x - prev_rel_x) * cached_virt_w * cfg.relative_speed;
        double dy = (norm_y - prev_rel_y) * cached_virt_h * cfg.relative_speed;
        prev_rel_x = norm_x;
        prev_rel_y = norm_y;

        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>(dx);
        input.mi.dy = static_cast<LONG>(dy);
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void InputInjector::handle_touch_up_impl(const TouchPacket& packet, const Config& cfg) {
    is_touching.store(false);
    has_prev_relative = false;

    if (cfg.mode == TabletMode::ABSOLUTE_TABLET) {
        bool up_eraser = (packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02);
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = last_x;
        input.mi.dy = last_y;
        input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | (up_eraser ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP) | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &input, sizeof(INPUT));
    } else if (cfg.mode == TabletMode::PEN_PRESSURE && synthetic_pen_available && g_InjectSyntheticPointerInput) {
        int px = cached_virtual_rect.left + static_cast<int>((last_x * (cached_virtual_rect.right - cached_virtual_rect.left)) / 65535.0);
        int py = cached_virtual_rect.top + static_cast<int>((last_y * (cached_virtual_rect.bottom - cached_virtual_rect.top)) / 65535.0);

        POINTER_TYPE_INFO pti = {};
        pti.type = PT_PEN;
        pti.penInfo.pointerInfo.pointerType = PT_PEN;
        pti.penInfo.pointerInfo.pointerId = 1;
        pti.penInfo.pointerInfo.ptPixelLocation.x = px;
        pti.penInfo.pointerInfo.ptPixelLocation.y = py;
        pti.penInfo.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CONFIDENCE;
        pti.penInfo.penFlags = ((packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02)) ? PEN_FLAG_ERASER : PEN_FLAG_NONE;

        g_InjectSyntheticPointerInput(hSyntheticPen, &pti, 1);
    } else {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = ((packet.tool_type == static_cast<uint8_t>(ToolType::ERASER)) || (packet.flags & 0x02)) ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void InputInjector::handle_scroll_impl(int delta_y, const Config& cfg) {
    int scroll_amount = cfg.invert_scroll ? -delta_y : delta_y;
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(scroll_amount);
    SendInput(1, &input, sizeof(INPUT));
}

// BUG-03 FIX: Handle Undo (10), Redo (11), Eraser (12) button flags
void InputInjector::handle_button_impl(uint8_t button_id, bool is_down) {
    if (!is_down) {
        // For keyboard shortcuts, we send both press+release in the down handler
        // For mouse buttons, handle release normally
        if (button_id == 1) {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            is_right_down.store(false);
            SendInput(1, &input, sizeof(INPUT));
        } else if (button_id == 2) {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        return;
    }

    // Button press handlers
    switch (button_id) {
        case 1: { // Right click
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            is_right_down.store(true);
            SendInput(1, &input, sizeof(INPUT));
            break;
        }
        case 2: { // Middle click
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            SendInput(1, &input, sizeof(INPUT));
            break;
        }
        case 10: // Undo (Ctrl+Z)
            send_key_combo(VK_CONTROL, 'Z');
            break;
        case 11: // Redo (Ctrl+Y)
            send_key_combo(VK_CONTROL, 'Y');
            break;
        case 12: // Eraser (E key)
            send_key_combo(0, 'E');
            break;
        default:
            break;
    }
}

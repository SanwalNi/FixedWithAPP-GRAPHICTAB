#include "input_injector.hpp"
#include <cmath>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Windows Synthetic Pointer API (Win10 1809+). MinGW headers may not declare
// these, so we define the mirrored layouts and resolve via GetProcAddress.
// ---------------------------------------------------------------------------
namespace synth {
    constexpr DWORD PT_PEN = 2;
    // POINTER_FLAG_*
    constexpr DWORD FLAG_INRANGE     = 0x00000002;
    constexpr DWORD FLAG_INCONTACT   = 0x00000004;
    constexpr DWORD FLAG_FIRSTBUTTON = 0x00000010;
    constexpr DWORD FLAG_SECONDBUTTON= 0x00000020;
    constexpr DWORD FLAG_DOWN        = 0x00010000;
    constexpr DWORD FLAG_UPDATE      = 0x00020000;
    constexpr DWORD FLAG_UP          = 0x00040000;
    // PEN_MASK_* (renamed - MinGW winuser.h defines the original macros)
    constexpr DWORD MASK_PRESSURE = 0x00000001;
    constexpr DWORD MASK_TILT_X   = 0x00000004;
    constexpr DWORD MASK_TILT_Y   = 0x00000008;
    constexpr DWORD MASK_ERASER   = 0x00000004;  // PEN_FLAG_ERASER

    struct POINTER_PEN_INFO {
        DWORD pointerFlags;
        DWORD penMask;
        DWORD penFlags;
        DWORD penStatus;
        RECT  rcContact;
        DWORD orientation;
        DWORD pressure;      // 0..1024
        DWORD rotation;
        LONG  tiltX;
        LONG  tiltY;
    };

    struct POINTER_TYPE_INFO {
        DWORD type;              // PT_PEN
        POINTER_PEN_INFO penInfo;
    };

    using CreateFn  = void* (__stdcall*)(DWORD type, ULONG maxCount, DWORD feedbackMode);
    using InjectFn  = int   (__stdcall*)(void* device, const POINTER_TYPE_INFO* info, UINT32 count);
    using DestroyFn = void  (__stdcall*)(void* device);

    static CreateFn  create  = nullptr;
    static InjectFn  inject  = nullptr;
    static DestroyFn destroy = nullptr;
    static bool resolved = false;

    void resolve() {
        if (resolved) return;
        resolved = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (!u32) return;
        create  = (CreateFn)GetProcAddress(u32, "CreateSyntheticPointerDevice");
        inject  = (InjectFn)GetProcAddress(u32, "InjectSyntheticPointerInput");
        destroy = (DestroyFn)GetProcAddress(u32, "DestroySyntheticPointerDevice");
    }
}

// ---------------------------------------------------------------------------

InputInjector::InputInjector() {
    synth::resolve();
    pen_available_ = (synth::create && synth::inject && synth::destroy);
    if (!pen_available_) {
        std::cout << "[i] Synthetic Pointer API unavailable - pen mode will use mouse fallback.\n";
    }
    refresh_display_info();
}

InputInjector::~InputInjector() {
    if (pen_device_) synth::destroy(pen_device_);
    pen_device_ = nullptr;
}

void InputInjector::refresh_display_info() {
    monitors_ = DisplayManager::get_monitors();
    virtual_rect_ = DisplayManager::get_virtual_rect();
}

double InputInjector::steady_now_sec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void InputInjector::set_config(const Config& cfg) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    bool display_changed = (cfg.target_monitor != config_.target_monitor);
    config_ = cfg;
    if (display_changed) refresh_display_info();
}

InputInjector::Config InputInjector::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

TouchPacket InputInjector::build_config_sync() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    TouchPacket p{};
    p.packet_type = (uint8_t)PacketType::CONFIG_SYNC;
    p.tool_type = (uint8_t)ToolType::FINGER;
    p.finger_id = (uint8_t)config_.target_monitor;
    p.flags = (uint8_t)config_.mode;
    return p;
}

void InputInjector::reset_state() {
    touching_.store(false);
    active_pointer_.store(-1);
    have_last_ = false;
    filter_.reset();
}

// ---------------------------------------------------------------------------

void InputInjector::process_packet(const TouchPacket& packet) {
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        cfg = config_;
    }

    auto type = (PacketType)packet.packet_type;
    switch (type) {
        case PacketType::TOUCH_DOWN: {
            int expected = -1;
            if (active_pointer_.compare_exchange_strong(expected, packet.finger_id)) {
                handle_down(packet, cfg);
            }
            break;
        }
        case PacketType::TOUCH_MOVE:
        case PacketType::HOVER:
            if (packet.finger_id == active_pointer_.load()) {
                handle_move(packet, cfg, type == PacketType::TOUCH_MOVE);
            }
            break;
        case PacketType::TOUCH_UP:
            if (packet.finger_id == active_pointer_.load()) {
                active_pointer_.store(-1);
                handle_up(packet, cfg);
            }
            break;
        case PacketType::SCROLL:
            handle_scroll(packet, cfg);
            break;
        case PacketType::BUTTON_DOWN:
            handle_button(packet, true);
            break;
        case PacketType::BUTTON_UP:
            handle_button(packet, false);
            break;
        case PacketType::CONFIG_SYNC: {
            // App -> PC sync: flags = mode, finger_id = monitor, x/y = view size.
            std::lock_guard<std::mutex> lock(config_mutex_);
            if (packet.flags <= 2) config_.mode = (TabletMode)packet.flags;
            if (packet.finger_id <= 9 && packet.finger_id > 0) config_.target_monitor = packet.finger_id;
            if (packet.x > 0 && packet.y > 0) {
                config_.tablet_aspect = (double)packet.x / (double)packet.y;
            }
            break;
        }
        default:
            break;
    }
}

// DOWN / MOVE / UP ----------------------------------------------------------

void InputInjector::handle_down(const TouchPacket& p, const Config& cfg) {
    touching_.store(true);
    have_last_ = false;

    double nx = (double)p.x / 65535.0;
    double ny = (double)p.y / 65535.0;

    if (cfg.enable_filter) {
        filter_.reset();
        double fx, fy;
        filter_.filter_point(nx, ny, steady_now_sec(), fx, fy);
        nx = fx; ny = fy;
    }

    if (cfg.mode == TabletMode::RELATIVE_MOUSE) {
        have_last_ = false; // first event establishes the origin
        last_pressure_.store(p.pressure);
        return;
    }

    int win_x = 0, win_y = 0;
    DisplayManager::map_coordinate_to_target(nx, ny, cfg.target_monitor, cfg.keep_aspect_ratio,
        cfg.tablet_aspect, win_x, win_y, monitors_, virtual_rect_);

    last_x_ = win_x;
    last_y_ = win_y;
    have_last_ = true;

    if (cfg.mode == TabletMode::PEN_PRESSURE && pen_available_) {
        inject_pen(win_x, win_y, p.pressure, (ToolType)p.tool_type, true, false, true);
    } else {
        inject_mouse_abs(win_x, win_y);
        send_mouse_button(false, true); // left down
    }
    last_pressure_.store(p.pressure);
}

void InputInjector::handle_move(const TouchPacket& p, const Config& cfg, bool in_contact) {
    double nx = (double)p.x / 65535.0;
    double ny = (double)p.y / 65535.0;

    if (cfg.mode == TabletMode::RELATIVE_MOUSE) {
        if (!have_last_) {
            have_last_ = true; // first event establishes the origin
            return;
        }
        int dx = (int16_t)p.x;  // app sends signed deltas in relative mode
        int dy = (int16_t)p.y;
        inject_mouse_rel((int)lround(dx * cfg.relative_speed),
                         (int)lround(dy * cfg.relative_speed));
        last_pressure_.store(p.pressure);
        return;
    }

    if (cfg.enable_filter) {
        double fx, fy;
        filter_.filter_point(nx, ny, steady_now_sec(), fx, fy);
        nx = fx; ny = fy;
    }

    int win_x = 0, win_y = 0;
    DisplayManager::map_coordinate_to_target(nx, ny, cfg.target_monitor, cfg.keep_aspect_ratio,
        cfg.tablet_aspect, win_x, win_y, monitors_, virtual_rect_);

    if (!in_contact && cfg.mode != TabletMode::PEN_PRESSURE) {
        inject_mouse_abs(win_x, win_y); // hover in mouse mode
        return;
    }

    inject_to(win_x, win_y, p.pressure, (ToolType)p.tool_type, in_contact, cfg);
    last_pressure_.store(p.pressure);
}

void InputInjector::handle_up(const TouchPacket& p, const Config& cfg) {
    touching_.store(false);

    if (cfg.mode == TabletMode::RELATIVE_MOUSE) {
        have_last_ = false;
        return;
    }

    double nx = (double)p.x / 65535.0;
    double ny = (double)p.y / 65535.0;
    int win_x = 0, win_y = 0;
    DisplayManager::map_coordinate_to_target(nx, ny, cfg.target_monitor, cfg.keep_aspect_ratio,
        cfg.tablet_aspect, win_x, win_y, monitors_, virtual_rect_);

    // Continuity fix: glide to the exact up position, then lift there.
    inject_to(win_x, win_y, 0, (ToolType)p.tool_type, true, cfg);
    if (cfg.mode == TabletMode::PEN_PRESSURE && pen_available_) {
        inject_pen(win_x, win_y, 0, (ToolType)p.tool_type, false, true, false);
    } else {
        inject_mouse_abs(win_x, win_y);
        send_mouse_button(false, false); // left up
    }
    have_last_ = false;
    filter_.reset();
}

// SUB-STEPPED INJECTION (the line-break fix) --------------------------------

void InputInjector::inject_to(int x, int y, uint16_t pressure, ToolType tool,
                              bool in_contact, const Config& cfg) {
    constexpr int kStep = 24; // px between injected points
    if (!have_last_) {
        last_x_ = x;
        last_y_ = y;
        have_last_ = true;
        if (cfg.mode == TabletMode::PEN_PRESSURE && pen_available_) {
            inject_pen(x, y, pressure, tool, in_contact, false, true);
        } else {
            inject_mouse_abs(x, y);
        }
        return;
    }

    double dx = (double)x - last_x_;
    double dy = (double)y - last_y_;
    double dist = std::sqrt(dx * dx + dy * dy);
    int steps = (int)std::ceil(dist / kStep);
    if (steps < 1) steps = 1;
    if (steps > 64) steps = 64; // safety clamp

    bool pen_mode = (cfg.mode == TabletMode::PEN_PRESSURE && pen_available_);
    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        int ix = (int)lround(last_x_ + dx * t);
        int iy = (int)lround(last_y_ + dy * t);
        if (pen_mode) {
            inject_pen(ix, iy, pressure, tool, in_contact, false, false);
        } else {
            inject_mouse_abs(ix, iy);
        }
    }
    last_x_ = x;
    last_y_ = y;
}

// PEN (Synthetic Pointer API) ------------------------------------------------

void InputInjector::inject_pen(int x, int y, uint16_t pressure, ToolType tool,
                               bool in_contact, bool is_up, bool is_down) {
    if (!pen_device_) {
        pen_device_ = synth::create(synth::PT_PEN, 1, 2 /* POINTER_FEEDBACK_NONE */);
        if (!pen_device_) return;
    }

    synth::POINTER_TYPE_INFO info{};
    info.type = synth::PT_PEN;
    auto& pen = info.penInfo;

    pen.rcContact.left   = x - 2;
    pen.rcContact.top    = y - 2;
    pen.rcContact.right  = x + 2;
    pen.rcContact.bottom = y + 2;

    pen.penMask = synth::MASK_PRESSURE | synth::MASK_TILT_X | synth::MASK_TILT_Y;
    pen.pressure = (DWORD)(pressure >> 6); // 0..65535 -> 0..1024
    if (tool == ToolType::ERASER) pen.penFlags |= synth::MASK_ERASER;

    if (is_up) {
        pen.pointerFlags = synth::FLAG_UP | synth::FLAG_INRANGE;
    } else if (is_down) {
        pen.pointerFlags = synth::FLAG_DOWN | synth::FLAG_INRANGE
                         | synth::FLAG_INCONTACT | synth::FLAG_FIRSTBUTTON;
    } else if (in_contact) {
        pen.pointerFlags = synth::FLAG_UPDATE | synth::FLAG_INRANGE
                         | synth::FLAG_INCONTACT | synth::FLAG_FIRSTBUTTON;
    } else {
        pen.pointerFlags = synth::FLAG_UPDATE | synth::FLAG_INRANGE;
    }

    synth::inject(pen_device_, &info, 1);
}

// MOUSE ----------------------------------------------------------------------

void InputInjector::inject_mouse_abs(int x, int y) {
    LONG vw = virtual_rect_.right - virtual_rect_.left;
    LONG vh = virtual_rect_.bottom - virtual_rect_.top;
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    LONG ax = (LONG)((x - virtual_rect_.left) * 65535.0 / (vw - 1));
    LONG ay = (LONG)((y - virtual_rect_.top) * 65535.0 / (vh - 1));

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in.mi.dx = ax;
    in.mi.dy = ay;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::inject_mouse_rel(int dx, int dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::send_mouse_button(bool right, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    if (right) in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    else       in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN  : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::send_key_combo(WORD vk1, WORD vk2) {
    INPUT in[4]{};
    int n = 0;
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk1; n++;
    if (vk2) { in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk2; n++; }
    if (vk2) { in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk2; in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = vk1; in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    SendInput((UINT)n, in, sizeof(INPUT));
}

// SCROLL / BUTTONS -----------------------------------------------------------

void InputInjector::handle_scroll(const TouchPacket& p, const Config& cfg) {
    int16_t delta = (int16_t)p.y;
    if (delta == 0) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)(delta * 1); // already scaled by the app
    (void)cfg;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::handle_button(const TouchPacket& p, bool down) {
    switch (p.flags) {
        case BTN_RIGHT_CLICK:
            send_mouse_button(true, down);
            break;
        case BTN_UNDO:
            if (down) send_key_combo('Z', VK_CONTROL);
            break;
        case BTN_REDO:
            if (down) send_key_combo('Y', VK_CONTROL);
            break;
        case BTN_ERASER_TOGGLE:
            if (down) send_key_combo('E');
            break;
        default:
            break;
    }
}

#pragma once
// ============================================================================
//  Alamy Engine v2 - wire protocol
//  Must stay in sync with android-app/app/.../Protocol.kt
//  16-byte packed little-endian packets over raw TCP (adb reverse tunnel).
// ============================================================================
#include <cstdint>

#pragma pack(push, 1)

enum class PacketType : uint8_t {
    TOUCH_DOWN   = 0,
    TOUCH_MOVE   = 1,
    TOUCH_UP     = 2,
    HOVER        = 3,
    SCROLL       = 4,
    BUTTON_DOWN  = 5,
    BUTTON_UP    = 6,
    PING         = 7,
    PONG         = 8,
    CONFIG_SYNC  = 9
};

enum class ToolType : uint8_t {
    FINGER = 0,
    STYLUS = 1,
    ERASER = 2,
    MOUSE  = 3
};

enum class TabletMode : uint8_t {
    ABSOLUTE_TABLET = 0,  // 1:1 screen mapping (graphic tablet)
    RELATIVE_MOUSE  = 1,  // trackpad - x/y carry signed deltas
    PEN_PRESSURE    = 2   // Windows synthetic pen with pressure
};

// Quick-action button codes (flags field of BUTTON_DOWN)
enum ButtonCode : uint8_t {
    BTN_RIGHT_CLICK    = 1,
    BTN_UNDO           = 10,
    BTN_REDO           = 11,
    BTN_ERASER_TOGGLE  = 12
};

struct TouchPacket {
    uint8_t  packet_type;   // PacketType
    uint8_t  tool_type;     // ToolType
    uint8_t  finger_id;     // 0..9
    uint8_t  flags;         // button/barrel flags, or mode in CONFIG_SYNC
    uint16_t x;             // normalized 0..65535, or int16 delta in relative mode
    uint16_t y;
    uint16_t pressure;      // normalized 0..65535
    int8_t   tilt_x;        // -90..+90
    int8_t   tilt_y;
    uint32_t client_time;   // microsecond timestamp (for latency probes)
};
static_assert(sizeof(TouchPacket) == 16, "TouchPacket must be exactly 16 bytes");

#pragma pack(pop)

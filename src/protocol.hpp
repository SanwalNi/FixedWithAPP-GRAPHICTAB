#pragma once
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
    ABSOLUTE_TABLET = 0,  // 1:1 Screen Mapping (Graphic Tablet)
    RELATIVE_MOUSE  = 1,  // Trackpad / Touchpad mode
    PEN_PRESSURE    = 2   // Windows Synthetic Pen with Pressure
};

// 16-byte packed binary packet for minimum latency over USB
struct TouchPacket {
    uint8_t  packet_type; // PacketType
    uint8_t  tool_type;   // ToolType
    uint8_t  finger_id;   // 0, 1, 2 for multi-touch
    uint8_t  flags;       // Bit 0: Primary/Barrel, Bit 1: Secondary/Eraser, Bit 2: Inverted
    uint16_t x;           // Normalized 0 to 65535 (or delta X in relative mode)
    uint16_t y;           // Normalized 0 to 65535 (or delta Y in relative mode)
    uint16_t pressure;    // Normalized 0 to 65535 (0 = none, 65535 = max)
    int8_t   tilt_x;      // -90 to +90 degrees
    int8_t   tilt_y;      // -90 to +90 degrees
    uint32_t client_time; // Microsecond client timestamp
};

#pragma pack(pop)

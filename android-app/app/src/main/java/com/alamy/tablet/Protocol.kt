package com.alamy.tablet

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Wire protocol v2 - must stay in sync with engine-v2/src/protocol.hpp.
 * 16-byte packed little-endian packet for minimum latency over USB loopback.
 */
object Protocol {
    const val PACKET_SIZE = 16

    // PacketType
    const val TOUCH_DOWN = 0
    const val TOUCH_MOVE = 1
    const val TOUCH_UP = 2
    const val HOVER = 3
    const val SCROLL = 4
    const val BUTTON_DOWN = 5
    const val BUTTON_UP = 6
    const val PING = 7
    const val PONG = 8
    const val CONFIG_SYNC = 9

    // ToolType
    const val TOOL_FINGER = 0
    const val TOOL_STYLUS = 1
    const val TOOL_ERASER = 2
    const val TOOL_MOUSE = 3

    // TabletMode (mirrors PC engine modes)
    const val MODE_ABSOLUTE = 0
    const val MODE_RELATIVE = 1
    const val MODE_PEN = 2

    // Quick-action button codes (flags field of BUTTON_DOWN)
    const val BTN_RIGHT_CLICK = 1
    const val BTN_UNDO = 10
    const val BTN_REDO = 11
    const val BTN_ERASER_TOGGLE = 12

    fun build(
        packetType: Int,
        toolType: Int,
        fingerId: Int,
        flags: Int,
        x: Int,      // normalized 0..65535 (absolute) or int16 delta (relative)
        y: Int,
        pressure: Int, // normalized 0..65535
        tiltX: Int = 0,
        tiltY: Int = 0,
        timestampMicros: Long = 0L
    ): ByteArray {
        val b = ByteBuffer.allocate(PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        b.put(packetType.toByte())
        b.put(toolType.toByte())
        b.put(fingerId.toByte())
        b.put(flags.toByte())
        b.putShort(x.coerceIn(0, 65535).toShort())
        b.putShort(y.coerceIn(0, 65535).toShort())
        b.putShort(pressure.coerceIn(0, 65535).toShort())
        b.put(tiltX.coerceIn(-90, 90).toByte())
        b.put(tiltY.coerceIn(-90, 90).toByte())
        b.putInt((timestampMicros and 0xFFFFFFFFL).toInt())
        return b.array()
    }

    fun nowMicros(): Long = System.nanoTime() / 1000L

    fun touchDown(tool: Int, fingerId: Int, nx: Float, ny: Float, pressure: Float): ByteArray =
        build(TOUCH_DOWN, tool, fingerId, 0, toFixed(nx), toFixed(ny), toFixed(pressure),
            timestampMicros = nowMicros())

    fun touchMove(tool: Int, fingerId: Int, nx: Float, ny: Float, pressure: Float): ByteArray =
        build(TOUCH_MOVE, tool, fingerId, 0, toFixed(nx), toFixed(ny), toFixed(pressure),
            timestampMicros = nowMicros())

    fun touchUp(tool: Int, fingerId: Int, nx: Float, ny: Float): ByteArray =
        build(TOUCH_UP, tool, fingerId, 0, toFixed(nx), toFixed(ny), 0,
            timestampMicros = nowMicros())

    /** Relative-mode movement: x/y carry signed 16-bit deltas. */
    fun relativeMove(tool: Int, fingerId: Int, dx: Int, dy: Int): ByteArray =
        build(TOUCH_MOVE, tool, fingerId, 0, clampI16(dx), clampI16(dy), toFixed(0.5f),
            timestampMicros = nowMicros())

    fun scroll(deltaY: Int): ByteArray =
        build(SCROLL, TOOL_FINGER, 0, 0, 0, clampI16(deltaY), 0, timestampMicros = nowMicros())

    fun button(flags: Int): ByteArray =
        build(BUTTON_DOWN, TOOL_FINGER, 0, flags, 0, 0, 0, timestampMicros = nowMicros())

    fun buttonUp(flags: Int): ByteArray =
        build(BUTTON_UP, TOOL_FINGER, 0, flags, 0, 0, 0, timestampMicros = nowMicros())

    fun configSync(mode: Int, monitor: Int): ByteArray =
        build(CONFIG_SYNC, TOOL_FINGER, monitor, mode, 0, 0, 0, timestampMicros = nowMicros())

    /** CONFIG_SYNC that also reports the app view size (px) for aspect-accurate mapping. */
    fun configSyncFull(mode: Int, monitor: Int, viewW: Int, viewH: Int): ByteArray =
        build(CONFIG_SYNC, TOOL_FINGER, monitor, mode,
            viewW.coerceIn(0, 65535), viewH.coerceIn(0, 65535), 0, timestampMicros = nowMicros())

    fun ping(timestampMicros: Long): ByteArray =
        build(PING, TOOL_FINGER, 0, 0, 0, 0, 0, timestampMicros = timestampMicros)

    fun pong(timestampMicros: Long): ByteArray =
        build(PONG, TOOL_FINGER, 0, 0, 0, 0, 0, timestampMicros = timestampMicros)

    fun toFixed(v: Float): Int = (v.coerceIn(0f, 1f) * 65535f).toInt()

    fun clampI16(v: Int): Int = v.coerceIn(-32768, 32767)
}

package com.alamy.tablet

import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import java.util.concurrent.atomic.AtomicLong

/**
 * The input engine: consumes raw MotionEvents straight from the Android input
 * pipeline (no browser in between) and converts every digitizer sample -
 * including batched historical samples (120-240 Hz touch controllers) - into
 * wire packets.
 *
 * KEY FIX for capacitive styluses: "gap bridging". Cheap capacitive tips
 * routinely lose contact for 10-50 ms during fast strokes; Android then fires
 * UP followed by a new DOWN, which used to break the line on the PC. Here the
 * UP is deferred for a short window; if contact resumes inside that window the
 * stroke continues silently and the PC never sees a break.
 */
class TouchEngine(
    private val socket: PacketSocket,
    private val ink: InkListener
) {
    interface InkListener {
        /** Local low-latency ink feedback point. */
        fun onInkPoint(nx: Float, ny: Float, pressure: Float, newStroke: Boolean)
        fun onStrokeEnd()
    }

    /** Pressure emulation - capacitive styli report no real pressure. */
    enum class PressureMode { CONSTANT, VELOCITY }

    var pressureMode = PressureMode.CONSTANT
    var constantPressure = 0.5f

    /** True when the PC engine is in RELATIVE (trackpad) mode. */
    var relativeMode = false
    var relativeSpeed = 1.5f

    /** Bridge window: contact-loss period we are willing to ride over. */
    var bridgeWindowMs = 55L
    var bridgeRadiusNorm = 0.06f

    // Packet counters for the HUD (thread-safe, UI reads periodically)
    val packetsSent = AtomicLong(0)

    private var strokeOwner = -1
    private var tool = Protocol.TOOL_FINGER

    private var lastRawX = 0f
    private var lastRawY = 0f
    private var haveLast = false
    private var prevEventTime = 0L

    // deferred UP state (gap bridging)
    private var pendingUp: ByteArray? = null
    private val main = Handler(Looper.getMainLooper())
    private val pendingUpRunnable = Runnable { flushPendingUp() }

    // two-finger scroll (trackpad mode)
    private var scrolling = false
    private var scrollOwnerId = -1
    private var scrollLastY = 0f

    // INPUT ------------------------------------------------------------------

    /**
     * Feed a MotionEvent. @param w/@param h are the active surface dimensions
     * in pixels, used to normalize coordinates to 0..1.
     */
    fun onTouch(e: MotionEvent, w: Float, h: Float): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> handleDown(e, w, h)
            MotionEvent.ACTION_POINTER_DOWN -> handleExtraPointerDown(e)
            MotionEvent.ACTION_MOVE -> handleMove(e, w, h)
            MotionEvent.ACTION_POINTER_UP -> handlePointerUp(e, w, h)
            MotionEvent.ACTION_UP -> handleUp(e, w, h, cancel = false)
            MotionEvent.ACTION_CANCEL -> handleUp(e, w, h, cancel = true)
        }
        return true
    }

    /** Force-end any active/deferred stroke (on disconnect, app pause...). */
    fun reset() {
        main.removeCallbacks(pendingUpRunnable)
        pendingUp = null
        strokeOwner = -1
        haveLast = false
        scrolling = false
        scrollOwnerId = -1
    }

    // HANDLERS ---------------------------------------------------------------

    private fun handleDown(e: MotionEvent, w: Float, h: Float) {
        // Contact resumed while a bridged UP was still pending -> continue
        // the same stroke; the PC never saw a break.
        if (pendingUp != null) {
            main.removeCallbacks(pendingUpRunnable)
            pendingUp = null
            haveLast = false
            emitSamples(e, w, h, newStroke = false)
            return
        }
        // Palm rejection: the first pointer owns the stroke.
        if (strokeOwner != -1) return
        strokeOwner = e.getPointerId(0)
        tool = toolTypeOf(e, 0)
        haveLast = false
        prevEventTime = e.eventTime

        // Open the stroke with an explicit TOUCH_DOWN - the PC engine refuses
        // MOVE samples for pointers it has not seen a DOWN for.
        val x0 = e.getX(0)
        val y0 = e.getY(0)
        val nx0 = (x0 / w).coerceIn(0f, 1f)
        val ny0 = (y0 / h).coerceIn(0f, 1f)
        val p0 = pressureFor(x0, y0, e.eventTime)
        socket.send(Protocol.touchDown(tool, strokeOwner % 10, nx0, ny0, p0))
        packetsSent.incrementAndGet()
        ink.onInkPoint(nx0, ny0, p0, true)
        lastRawX = x0
        lastRawY = y0
        haveLast = true
        prevEventTime = e.eventTime
    }

    private fun handleExtraPointerDown(e: MotionEvent) {
        // Two-finger scroll in trackpad mode; otherwise ignored (palm rejection).
        if (relativeMode && strokeOwner != -1 && !scrolling) {
            scrolling = true
            scrollOwnerId = e.getPointerId(e.actionIndex)
            val idx = e.findPointerIndex(scrollOwnerId)
            if (idx >= 0) scrollLastY = e.getY(idx)
        }
    }

    private fun handleMove(e: MotionEvent, w: Float, h: Float) {
        if (scrolling) {
            val idx = e.findPointerIndex(scrollOwnerId)
            if (idx >= 0) {
                val y = e.getY(idx)
                val dy = (y - scrollLastY).toInt()
                if (dy != 0) {
                    scrollLastY = y
                    socket.send(Protocol.scroll(-dy))
                    packetsSent.incrementAndGet()
                }
            }
            return
        }
        if (strokeOwner != -1 && e.findPointerIndex(strokeOwner) >= 0) {
            emitSamples(e, w, h, newStroke = false)
        }
    }

    private fun handlePointerUp(e: MotionEvent, w: Float, h: Float) {
        val pid = e.getPointerId(e.actionIndex)
        if (scrolling && pid == scrollOwnerId) {
            scrolling = false
            scrollOwnerId = -1
            return
        }
        if (pid == strokeOwner) {
            val idx = e.findPointerIndex(pid)
            deferUp(e.getX(idx) / w, e.getY(idx) / h, cancel = false)
        }
    }

    private fun handleUp(e: MotionEvent, w: Float, h: Float, cancel: Boolean) {
        if (scrolling) {
            scrolling = false
            scrollOwnerId = -1
            return
        }
        if (strokeOwner == -1) return
        val idx = e.findPointerIndex(strokeOwner)
        val nx = if (idx >= 0) e.getX(idx) / w else lastRawX / w
        val ny = if (idx >= 0) e.getY(idx) / h else lastRawY / h
        deferUp(nx.coerceIn(0f, 1f), ny.coerceIn(0f, 1f), cancel = cancel)
    }

    // GAP BRIDGING -----------------------------------------------------------

    private fun deferUp(nx: Float, ny: Float, cancel: Boolean) {
        main.removeCallbacks(pendingUpRunnable)
        pendingUp = Protocol.touchUp(tool, strokeOwner % 10, nx, ny)
        haveLast = false
        ink.onStrokeEnd()
        if (cancel) {
            // A real cancel (system gesture) is honored immediately.
            flushPendingUp()
        } else {
            main.postDelayed(pendingUpRunnable, bridgeWindowMs)
        }
    }

    private fun flushPendingUp() {
        val p = pendingUp ?: return
        pendingUp = null
        socket.send(p)
        packetsSent.incrementAndGet()
        strokeOwner = -1
    }

    // SAMPLE EMITTING --------------------------------------------------------

    /** Emit every sample of the stroke owner: all historical points + current. */
    private fun emitSamples(e: MotionEvent, w: Float, h: Float, newStroke: Boolean) {
        val idx = e.findPointerIndex(strokeOwner)
        if (idx < 0) return

        val count = e.historySize + 1
        for (i in 0 until count) {
            val x = if (i < e.historySize) e.getHistoricalX(idx, i) else e.getX(idx)
            val y = if (i < e.historySize) e.getHistoricalY(idx, i) else e.getY(idx)
            val t = if (i < e.historySize) e.getHistoricalEventTime(i) else e.eventTime

            val nx = (x / w).coerceIn(0f, 1f)
            val ny = (y / h).coerceIn(0f, 1f)
            val pressure = pressureFor(x, y, t)

            if (relativeMode) {
                if (haveLast) {
                    val dx = ((x - lastRawX) * relativeSpeed).toInt()
                    val dy = ((y - lastRawY) * relativeSpeed).toInt()
                    if (dx != 0 || dy != 0) {
                        socket.send(Protocol.relativeMove(tool, strokeOwner % 10, dx, dy))
                        packetsSent.incrementAndGet()
                    }
                }
            } else {
                socket.send(Protocol.touchMove(tool, strokeOwner % 10, nx, ny, pressure))
                packetsSent.incrementAndGet()
            }
            ink.onInkPoint(nx, ny, pressure, newStroke && i == 0)
            lastRawX = x
            lastRawY = y
            prevEventTime = t
            haveLast = true
        }
        // A MOVE inside the bridge window means contact never really ended.
        if (pendingUp != null) {
            main.removeCallbacks(pendingUpRunnable)
            pendingUp = null
        }
    }

    private fun pressureFor(x: Float, y: Float, t: Long): Float = when (pressureMode) {
        PressureMode.CONSTANT -> constantPressure
        PressureMode.VELOCITY -> {
            if (!haveLast || t <= prevEventTime) {
                constantPressure
            } else {
                val dt = (t - prevEventTime).coerceAtLeast(1).toFloat()
                val dist = Math.hypot((x - lastRawX).toDouble(), (y - lastRawY).toDouble())
                val speed = (dist / dt).toFloat() // px per ms
                (0.25f + speed / 10f).coerceIn(0.15f, 1f)
            }
        }
    }

    private fun toolTypeOf(e: MotionEvent, idx: Int): Int = when (e.getToolType(idx)) {
        MotionEvent.TOOL_TYPE_ERASER -> Protocol.TOOL_ERASER
        MotionEvent.TOOL_TYPE_STYLUS -> Protocol.TOOL_STYLUS
        MotionEvent.TOOL_TYPE_MOUSE -> Protocol.TOOL_MOUSE
        else -> Protocol.TOOL_FINGER
    }
}

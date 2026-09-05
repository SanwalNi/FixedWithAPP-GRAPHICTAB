package com.alamy.tablet

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Alamy Tablet - native Android digitizer surface.
 *
 * Replaces the old Chrome/WebSocket pipeline entirely:
 *  - raw MotionEvents (full digitizer rate, incl. historical samples)
 *  - forced high refresh mode (90 Hz)
 *  - immersive fullscreen: no system gestures can interrupt a stroke
 *  - raw TCP over `adb reverse` (no browser, no WebSocket framing)
 */
class MainActivity : Activity(), PacketSocket.Listener, TouchEngine.InkListener {

    private lateinit var socket: PacketSocket
    private lateinit var engine: TouchEngine
    private lateinit var ink: InkSurface

    private lateinit var statusDot: View
    private lateinit var statusText: TextView
    private lateinit var hudText: TextView
    private lateinit var modeButton: Button
    private lateinit var monitorButton: Button
    private lateinit var settingsButton: Button
    private lateinit var hint: TextView

    private var mode = Protocol.MODE_ABSOLUTE
    private var monitor = 1
    private var lastPackets = 0L
    private var lastRateMs = 0L

    private val ui = Handler(Looper.getMainLooper())

    // Timers: latency probe every 500 ms, HUD refresh every 1 s.
    private val pingRunnable = object : Runnable {
        override fun run() { socket.sendPing(); ui.postDelayed(this, 500) }
    }
    private val hudRunnable = object : Runnable {
        override fun run() { updateHud(); ui.postDelayed(this, 1000) }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        forceHighRefreshMode()
        applyImmersive()

        // --- layers -------------------------------------------------------
        val root = FrameLayout(this).apply { setBackgroundColor(-0xF8ECE6) } // #07131A
        ink = InkSurface(this)
        root.addView(ink, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))

        buildTopBar(root)
        buildQuickActions(root)

        hint = TextView(this).apply {
            text = "Draw here"
            setTextColor(0x4DFFFFFF.toInt())
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
        }
        root.addView(hint, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.CENTER))

        setContentView(root)

        // --- engine + transport --------------------------------------------
        socket = PacketSocket(listener = this)
        engine = TouchEngine(socket, this)

        // All touches on the ink surface go straight into the engine.
        ink.setOnTouchListener { _, e ->
            engine.onTouch(e, ink.width.toFloat(), ink.height.toFloat())
        }

        socket.start()
        ui.post(pingRunnable)
        ui.post(hudRunnable)
    }

    // DISPLAY ----------------------------------------------------------------

    /** Request the panel's highest refresh mode (90 Hz on this device). */
    private fun forceHighRefreshMode() {
        val display = windowManager.defaultDisplay ?: return
        val best = display.supportedModes.maxByOrNull { it.refreshRate } ?: return
        if (best.refreshRate > display.mode.refreshRate + 0.1f) {
            window.attributes = window.attributes.apply { preferredDisplayModeId = best.modeId }
        }
    }

    @Suppress("DEPRECATION")
    private fun applyImmersive() {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
    }

    @Suppress("DEPRECATION")
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersive()
    }

    override fun onPause() {
        super.onPause()
        engine.reset()
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacks(pingRunnable)
        ui.removeCallbacks(hudRunnable)
        engine.reset()
        socket.stop()
    }

    // UI HELPERS -------------------------------------------------------------

    private fun dp(v: Int): Int = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    private fun toolButton(label: String): Button = Button(this).apply {
        text = label
        textSize = 13f
        setTextColor(Color.WHITE)
        isAllCaps = false
        setPadding(dp(10), dp(4), dp(10), dp(4))
        background = GradientDrawable().apply {
            setColor(0x33FFFFFF.toInt())
            cornerRadius = dp(14).toFloat()
        }
    }

    // TOP BAR + ACTIONS --------------------------------------------------------

    private fun buildTopBar(root: FrameLayout) {
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12), dp(6), dp(12), dp(6))
            background = GradientDrawable().apply { setColor(0x6607131A.toInt()) }
        }

        statusDot = View(this).apply {
            background = GradientDrawable().apply {
                setColor(Color.RED); shape = GradientDrawable.OVAL
            }
            layoutParams = LinearLayout.LayoutParams(dp(12), dp(12)).apply { marginEnd = dp(8) }
        }
        bar.addView(statusDot)

        statusText = TextView(this).apply {
            text = "Connecting..."
            setTextColor(Color.WHITE)
            textSize = 13f
        }
        bar.addView(statusText)

        hudText = TextView(this).apply {
            text = "0 Hz  -  -- ms"
            setTextColor(0xB3FFFFFF.toInt())
            textSize = 13f
            typeface = Typeface.MONOSPACE
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginStart = dp(12) }
        }
        bar.addView(hudText)

        bar.addView(View(this), LinearLayout.LayoutParams(0, 1, 1f))

        modeButton = toolButton("Tablet")
        modeButton.setOnClickListener { cycleMode() }
        bar.addView(modeButton)

        monitorButton = toolButton("Mon 1")
        monitorButton.setOnClickListener { cycleMonitor() }
        bar.addView(monitorButton)

        settingsButton = toolButton("Settings")
        settingsButton.setOnClickListener { showSettings() }
        bar.addView(settingsButton)

        root.addView(bar, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP))
    }

    private fun buildQuickActions(root: FrameLayout) {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        fun action(label: String, flags: Int) {
            val b = toolButton(label)
            b.setOnClickListener {
                socket.send(Protocol.button(flags))
                if (flags == Protocol.BTN_RIGHT_CLICK) {
                    // Right-click is a true press/release pair; everything
                    // else is an instantaneous action (handled on down).
                    ui.postDelayed({ socket.send(Protocol.buttonUp(flags)) }, 60)
                }
            }
            row.addView(b, LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(8) })
        }
        action("Undo", Protocol.BTN_UNDO)
        action("Redo", Protocol.BTN_REDO)
        action("Eraser", Protocol.BTN_ERASER_TOGGLE)
        action("R-Click", Protocol.BTN_RIGHT_CLICK)

        root.addView(row, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.START).apply {
            marginStart = dp(12); bottomMargin = dp(14)
        })
    }

    // MODE / MONITOR / SETTINGS ------------------------------------------------

    private fun cycleMode() {
        mode = when (mode) {
            Protocol.MODE_ABSOLUTE -> Protocol.MODE_PEN
            Protocol.MODE_PEN -> Protocol.MODE_RELATIVE
            else -> Protocol.MODE_ABSOLUTE
        }
        socket.send(Protocol.configSync(mode, monitor))
        applyModeToUi()
    }

    private fun cycleMonitor() {
        monitor = if (monitor >= 3) 1 else monitor + 1
        socket.send(Protocol.configSync(mode, monitor))
        applyModeToUi()
    }

    private fun applyModeToUi() {
        modeButton.text = when (mode) {
            Protocol.MODE_PEN -> "Pen"
            Protocol.MODE_RELATIVE -> "Trackpad"
            else -> "Tablet"
        }
        monitorButton.text = "Mon $monitor"
        engine.relativeMode = (mode == Protocol.MODE_RELATIVE)
    }

    /** Tell the PC our mode/monitor + exact view aspect for 1:1 mapping. */
    private fun sendFullConfigSync() {
        socket.send(Protocol.configSyncFull(mode, monitor, ink.width, ink.height))
    }

    private fun showSettings() {
        val options = arrayOf(
            "Pressure: " + if (engine.pressureMode == TouchEngine.PressureMode.CONSTANT) "Constant" else "Velocity",
            "Ink feedback: " + if (ink.inkEnabled) "ON" else "OFF",
            "Stylus gap bridge: ${engine.bridgeWindowMs} ms",
            "Clear trail"
        )
        AlertDialog.Builder(this)
            .setTitle("Alamy Settings")
            .setItems(options) { _, which ->
                when (which) {
                    0 -> engine.pressureMode =
                        if (engine.pressureMode == TouchEngine.PressureMode.CONSTANT)
                            TouchEngine.PressureMode.VELOCITY else TouchEngine.PressureMode.CONSTANT
                    1 -> { ink.inkEnabled = !ink.inkEnabled; if (!ink.inkEnabled) ink.clearTrail() }
                    2 -> engine.bridgeWindowMs = when (engine.bridgeWindowMs) {
                        30L -> 55L
                        55L -> 80L
                        else -> 30L
                    }
                    3 -> ink.clearTrail()
                }
            }
            .show()
    }

    // LISTENERS ----------------------------------------------------------------

    override fun onConnectionStateChanged(connected: Boolean) {
        (statusDot.background as? GradientDrawable)?.setColor(
            if (connected) 0xFF22C55E.toInt() else Color.RED)
        statusText.text = if (connected) "USB Active" else "Waiting for PC engine..."
        hudText.text = if (connected) hudText.text else "0 Hz  -  -- ms"
        hint.visibility = if (connected) View.INVISIBLE else View.VISIBLE
        if (!connected) engine.reset() else sendFullConfigSync()
    }

    override fun onConfigSync(modeFromPc: Int, monitorFromPc: Int) {
        mode = modeFromPc
        monitor = monitorFromPc
        applyModeToUi()
    }

    override fun onPingResult(rttMs: Float) {
        hudRtt = rttMs
    }

    private var hudRtt = -1f

    // TouchEngine.InkListener --------------------------------------------------

    override fun onInkPoint(nx: Float, ny: Float, pressure: Float, newStroke: Boolean) {
        ink.addPoint(nx, ny, pressure, newStroke)
    }

    override fun onStrokeEnd() {
        ink.onStrokeEnded()
    }

    // HUD ----------------------------------------------------------------------

    private fun updateHud() {
        val now = System.nanoTime()
        val sent = engine.packetsSent.get()
        if (lastRateMs != 0L) {
            val seconds = (now - lastRateMs) / 1_000_000_000.0
            val hz = ((sent - lastPackets) / seconds).toInt()
            val rtt = if (hudRtt >= 0) "${"%.1f".format(hudRtt)} ms" else "-- ms"
            hudText.text = "$hz Hz  -  $rtt"
        }
        lastPackets = sent
        lastRateMs = now
    }
}

package com.alamy.tablet

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.view.Choreographer
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * Local low-latency ink trail. Runs on the Choreographer (display vsync, so
 * 90 Hz when the app forces the high-refresh mode) for instant visual
 * feedback while drawing. Fully optional and disabled via settings.
 *
 * Trail is kept in an offscreen bitmap because SurfaceView back buffers are
 * not preserved between frames.
 */
class InkSurface(context: Context) : SurfaceView(context), SurfaceHolder.Callback, Choreographer.FrameCallback {

    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = -0xF9492C  // #FF06B6D4 cyan
    }
    private val fadePaint = Paint().apply { color = 0x1407131A }
    private val trailBitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    private var trailBitmap: Bitmap? = null
    private var trailCanvas: Canvas? = null
    private var lastX = -1f
    private var lastY = -1f
    private var hasContent = false
    private var idleFrames = 0
    private var surfaceReady = false
    private var frameScheduled = false

    var inkEnabled = true

    init {
        holder.addCallback(this)
        setZOrderOnTop(false)
    }

    /** Called by TouchEngine for every emitted sample. */
    fun addPoint(nx: Float, ny: Float, pressure: Float, newStroke: Boolean) {
        if (!inkEnabled || !surfaceReady) return
        val tc = trailCanvas ?: return
        val px = nx * tc.width
        val py = ny * tc.height
        strokePaint.strokeWidth = 3f + pressure * 7f
        strokePaint.alpha = (100 + pressure * 155).toInt().coerceIn(0, 255)
        if (newStroke || lastX < 0f) {
            tc.drawCircle(px, py, strokePaint.strokeWidth / 2f, strokePaint)
        } else {
            tc.drawLine(lastX, lastY, px, py, strokePaint)
        }
        lastX = px
        lastY = py
        hasContent = true
        idleFrames = 0
        if (!frameScheduled) {
            frameScheduled = true
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    fun onStrokeEnded() {
        lastX = -1f
    }

    fun clearTrail() {
        trailBitmap?.eraseColor(0)
        hasContent = false
    }

    // Choreographer ----------------------------------------------------------

    override fun doFrame(frameTimeNanos: Long) {
        frameScheduled = false
        if (!surfaceReady) return
        if (hasContent) {
            val tc = trailCanvas ?: return
            tc.drawPaint(fadePaint)  // fade the offscreen trail a little
            idleFrames++
            if (idleFrames > 200) hasContent = false
            val canvas = holder.lockCanvas()
            if (canvas != null) {
                try {
                    canvas.drawColor(0)
                    trailBitmap?.let { canvas.drawBitmap(it, 0f, 0f, trailBitmapPaint) }
                } finally {
                    holder.unlockCanvasAndPost(canvas)
                }
            }
            if (hasContent) {
                frameScheduled = true
                Choreographer.getInstance().postFrameCallback(this)
            }
        }
    }

    // SurfaceHolder ----------------------------------------------------------

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        val old = trailBitmap
        trailBitmap = Bitmap.createBitmap(width.coerceAtLeast(1), height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
        trailCanvas = Canvas(trailBitmap!!)
        if (old != null) {
            trailCanvas!!.drawBitmap(old, 0f, 0f, null)
            old.recycle()
        }
        hasContent = false
        lastX = -1f
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    /** Touches are forwarded to the TouchEngine by the activity. */
    override fun onTouchEvent(event: MotionEvent): Boolean = false
}

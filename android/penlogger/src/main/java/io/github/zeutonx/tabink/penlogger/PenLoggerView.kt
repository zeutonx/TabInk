package io.github.zeutonx.tabink.penlogger

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View

/**
 * Full-screen capture surface. Every MotionEvent is expanded into raw [PenSample]s
 * (all historical samples first, then the current one, for every pointer) and handed to
 * [onSamples]. Ink is drawn into a backing bitmap so memory stays flat in long sessions.
 *
 * Drawing is a plain line between consecutive raw samples, deliberately unsmoothed, so
 * dots, hooks and gaps show exactly what Android delivered.
 */
class PenLoggerView(context: Context) : View(context) {

    var onSamples: ((List<PenSample>) -> Unit)? = null

    /** When true, call requestUnbufferedDispatch() on each down/hover-enter (P0 comparison). */
    var unbufferedDispatch = false

    private var eventSeq = 0L
    private var bitmap: Bitmap? = null
    private var bitmapCanvas: Canvas? = null

    private val lastPoint = HashMap<Int, FloatArray>()
    private var hoverX = Float.NaN
    private var hoverY = Float.NaN
    private var hoverIsEraser = false

    private val penPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK; strokeCap = Paint.Cap.ROUND; style = Paint.Style.STROKE
    }
    private val eraserPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 40, 40); strokeCap = Paint.Cap.ROUND; style = Paint.Style.STROKE
    }
    private val fingerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(90, 30, 110, 230); style = Paint.Style.FILL
    }
    private val samplePaint = Paint().apply { color = Color.argb(160, 0, 140, 60) }
    private val hoverPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.GRAY; style = Paint.Style.STROKE; strokeWidth = 2f
    }

    /** Show a dot at every raw sample position, to see sample density. */
    var showSampleDots = true

    init {
        setBackgroundColor(Color.WHITE)
        isFocusable = true
    }

    fun clearInk() {
        bitmap?.eraseColor(Color.TRANSPARENT)
        lastPoint.clear()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        if (w <= 0 || h <= 0) return
        val newBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val newCanvas = Canvas(newBitmap)
        bitmap?.let { newCanvas.drawBitmap(it, 0f, 0f, null) }
        bitmap = newBitmap
        bitmapCanvas = newCanvas
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (unbufferedDispatch && event.actionMasked == MotionEvent.ACTION_DOWN) {
            requestUnbufferedDispatch(event)
        }
        handle(event)
        return true
    }

    override fun onHoverEvent(event: MotionEvent): Boolean {
        if (unbufferedDispatch && event.actionMasked == MotionEvent.ACTION_HOVER_ENTER) {
            requestUnbufferedDispatch(event)
        }
        handle(event)
        return true
    }

    /** Catches BUTTON_PRESS / BUTTON_RELEASE and anything else not routed to touch/hover. */
    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        val a = event.actionMasked
        if (a == MotionEvent.ACTION_BUTTON_PRESS || a == MotionEvent.ACTION_BUTTON_RELEASE) {
            handle(event)
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    private fun handle(event: MotionEvent) {
        // MotionEvent times and System.nanoTime() are both CLOCK_MONOTONIC on Android.
        val receiveNs = System.nanoTime()
        val samples = expand(event, receiveNs, ++eventSeq)
        draw(samples)
        onSamples?.invoke(samples)
    }

    private fun expand(e: MotionEvent, receiveNs: Long, seq: Long): List<PenSample> {
        val h = e.historySize
        val n = e.pointerCount
        val out = ArrayList<PenSample>((h + 1) * n)
        val action = e.actionMasked
        val actionIndex = e.actionIndex
        for (hi in 0 until h) {
            val t = e.getHistoricalEventTimeNanos(hi)
            for (p in 0 until n) {
                out += PenSample(
                    sampleTimeNs = t, receiveTimeNs = receiveNs, eventSeq = seq, historical = true,
                    action = action, actionIndex = actionIndex, pointerIndex = p,
                    pointerId = e.getPointerId(p), toolType = e.getToolType(p),
                    x = e.getHistoricalX(p, hi), y = e.getHistoricalY(p, hi),
                    pressure = e.getHistoricalPressure(p, hi),
                    distance = e.getHistoricalAxisValue(MotionEvent.AXIS_DISTANCE, p, hi),
                    tilt = e.getHistoricalAxisValue(MotionEvent.AXIS_TILT, p, hi),
                    orientation = e.getHistoricalAxisValue(MotionEvent.AXIS_ORIENTATION, p, hi),
                    buttonState = e.buttonState, historySize = h, unbuffered = unbufferedDispatch,
                )
            }
        }
        val t = e.eventTimeNanos
        for (p in 0 until n) {
            out += PenSample(
                sampleTimeNs = t, receiveTimeNs = receiveNs, eventSeq = seq, historical = false,
                action = action, actionIndex = actionIndex, pointerIndex = p,
                pointerId = e.getPointerId(p), toolType = e.getToolType(p),
                x = e.getX(p), y = e.getY(p), pressure = e.getPressure(p),
                distance = e.getAxisValue(MotionEvent.AXIS_DISTANCE, p),
                tilt = e.getAxisValue(MotionEvent.AXIS_TILT, p),
                orientation = e.getAxisValue(MotionEvent.AXIS_ORIENTATION, p),
                buttonState = e.buttonState, historySize = h, unbuffered = unbufferedDispatch,
            )
        }
        return out
    }

    private fun draw(samples: List<PenSample>) {
        val c = bitmapCanvas ?: return
        for (s in samples) {
            val pen = PenSample.isPen(s.toolType)
            when {
                PenSample.isHoverAction(s.action) -> {
                    if (pen) {
                        hoverX = s.x; hoverY = s.y
                        hoverIsEraser = s.toolType == PenSample.TOOL_ERASER
                        if (s.action == PenSample.ACTION_HOVER_EXIT) { hoverX = Float.NaN; hoverY = Float.NaN }
                    }
                }
                pen && (PenSample.isContactAction(s.action) || s.action == PenSample.ACTION_UP || s.action == PenSample.ACTION_POINTER_UP) -> {
                    hoverX = Float.NaN
                    val paint = if (s.toolType == PenSample.TOOL_ERASER) eraserPaint else penPaint
                    paint.strokeWidth = 1f + 5f * s.pressure.coerceIn(0f, 1f)
                    val isStart = !s.historical && s.actionIndex == s.pointerIndex &&
                        (s.action == PenSample.ACTION_DOWN || s.action == PenSample.ACTION_POINTER_DOWN)
                    val prev = lastPoint[s.pointerId]
                    if (prev == null || isStart) c.drawPoint(s.x, s.y, paint)
                    else c.drawLine(prev[0], prev[1], s.x, s.y, paint)
                    if (showSampleDots) c.drawRect(s.x - 1f, s.y - 1f, s.x + 1f, s.y + 1f, samplePaint)
                    lastPoint[s.pointerId] = floatArrayOf(s.x, s.y)
                }
                !pen && PenSample.isContactAction(s.action) -> {
                    // Fingers/palm: faint blue blobs so accidental contacts are visible.
                    c.drawCircle(s.x, s.y, 10f, fingerPaint)
                }
            }
            val ended = !s.historical && s.actionIndex == s.pointerIndex && (
                s.action == PenSample.ACTION_UP || s.action == PenSample.ACTION_POINTER_UP ||
                    s.action == PenSample.ACTION_CANCEL)
            if (ended || s.action == PenSample.ACTION_CANCEL) lastPoint.remove(s.pointerId)
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        bitmap?.let { canvas.drawBitmap(it, 0f, 0f, null) }
        if (!hoverX.isNaN()) {
            hoverPaint.color = if (hoverIsEraser) Color.rgb(220, 40, 40) else Color.GRAY
            canvas.drawCircle(hoverX, hoverY, 14f, hoverPaint)
        }
    }
}

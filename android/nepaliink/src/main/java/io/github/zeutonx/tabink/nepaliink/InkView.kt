package io.github.zeutonx.tabink.nepaliink

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.View
import com.google.mlkit.vision.digitalink.recognition.Ink

/** One-line writing area. Pen only (palms ignored); historical samples first, none dropped. */
class InkView(context: Context) : View(context) {
    private class Stroke { val points = ArrayList<Ink.Point>(); val path = Path() }

    private val strokes = ArrayList<Stroke>()
    private var current: Stroke? = null
    private var pointerId = -1
    var onInkChanged: (() -> Unit)? = null

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK; style = Paint.Style.STROKE; strokeWidth = 5f
        strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND
    }

    init { setBackgroundColor(Color.WHITE) }

    val isEmpty get() = strokes.isEmpty()
    val isWriting get() = current != null

    fun clear() {
        strokes.clear(); current = null; pointerId = -1
        invalidate(); onInkChanged?.invoke()
    }

    fun buildInk(): Ink {
        val ink = Ink.builder()
        for (s in strokes) {
            val b = Ink.Stroke.builder()
            s.points.forEach { b.addPoint(it) }
            ink.addStroke(b.build())
        }
        return ink.build()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                if (current == null && e.getToolType(i) == MotionEvent.TOOL_TYPE_STYLUS) {
                    pointerId = e.getPointerId(i)
                    current = Stroke()
                    add(e.getX(i), e.getY(i), e.eventTime)
                    onInkChanged?.invoke()
                }
            }
            MotionEvent.ACTION_MOVE -> addAll(e)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP ->
                if (e.getPointerId(e.actionIndex) == pointerId) {
                    // FLAG_CANCELED: the system withdrew this pointer.
                    if (e.flags and MotionEvent.FLAG_CANCELED != 0) {
                        finish(keep = false)
                    } else {
                        addAll(e)
                        finish(keep = true)
                    }
                }
            MotionEvent.ACTION_CANCEL -> finish(keep = false)
        }
        invalidate()
        return true
    }

    private fun addAll(e: MotionEvent) {
        val i = e.findPointerIndex(pointerId)
        if (current == null || i < 0) return
        for (h in 0 until e.historySize) {
            add(e.getHistoricalX(i, h), e.getHistoricalY(i, h), e.getHistoricalEventTime(h))
        }
        add(e.getX(i), e.getY(i), e.eventTime)
    }

    private fun add(x: Float, y: Float, t: Long) {
        val s = current ?: return
        if (s.points.isEmpty()) {
            s.path.moveTo(x, y)
            s.path.lineTo(x + 0.1f, y) // makes a single-sample dot visible
        } else {
            s.path.lineTo(x, y)
        }
        s.points.add(Ink.Point.create(x, y, t))
    }

    private fun finish(keep: Boolean) {
        current?.let { if (keep) strokes.add(it) }
        current = null; pointerId = -1
        onInkChanged?.invoke()
    }

    override fun onDraw(canvas: Canvas) {
        strokes.forEach { canvas.drawPath(it.path, paint) }
        current?.let { canvas.drawPath(it.path, paint) }
    }
}

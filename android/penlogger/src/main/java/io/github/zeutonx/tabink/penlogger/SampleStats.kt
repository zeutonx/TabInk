package io.github.zeutonx.tabink.penlogger

import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_BUTTON_PRESS
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_CANCEL
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_DOWN
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_HOVER_ENTER
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_HOVER_EXIT
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_POINTER_DOWN
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_POINTER_UP
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_UP
import kotlin.math.max
import kotlin.math.min

/**
 * Running statistics answering the P0.2 questions: real pen sample rate, how Android
 * batches samples, delivery lag, pressure distribution, hover distance and button delivery.
 * Pure JVM so it is unit-testable; not thread-safe (call from the UI thread only).
 */
class SampleStats(private val window: Int = 4096) {

    class Ring(capacity: Int) {
        private val data = LongArray(capacity)
        private var next = 0
        var size = 0
            private set

        fun add(v: Long) {
            data[next] = v
            next = (next + 1) % data.size
            if (size < data.size) size++
        }

        /** Nearest-rank percentile of the retained values, or null if empty. */
        fun percentile(p: Double): Long? {
            if (size == 0) return null
            val sorted = data.copyOf(size).also { it.sort() }
            val rank = Math.ceil(p / 100.0 * size).toInt().coerceIn(1, size)
            return sorted[rank - 1]
        }

        fun clear() {
            next = 0; size = 0
        }
    }

    val contactIntervalsNs = Ring(window)
    val hoverIntervalsNs = Ring(window)
    val deliveryLagNs = Ring(window)
    val batchSizes = Ring(window)

    var penSamples = 0L; private set
    var penContactSamples = 0L; private set
    var penHoverSamples = 0L; private set
    var historicalPenSamples = 0L; private set
    var strokes = 0L; private set
    var canceledStrokes = 0L; private set
    var nonMonotonic = 0L; private set
    var otherToolSamplesDuringPenContact = 0L; private set
    var buttonPressEvents = 0L; private set
    var buttonStatesSeen = 0; private set
    val toolCounts = sortedMapOf<String, Long>()

    var minPressure = Float.MAX_VALUE; private set
    var maxPressure = 0f; private set
    /** 10 bins over [0, 1]; the last bin also counts pressure > 1. Contact samples only. */
    val pressureHistogram = LongArray(10)

    var minHoverDistance = Float.MAX_VALUE; private set
    var maxHoverDistance = 0f; private set

    private var penInContact = false
    private var lastContactTimeNs = -1L
    private var lastHoverTimeNs = -1L
    private var lastPenSampleTimeNs = -1L

    /** Feed every sample of one MotionEvent, historical first, in delivery order. */
    fun onEvent(samples: List<PenSample>) {
        var penSamplesInEvent = 0
        for (s in samples) {
            val tool = PenSample.toolName(s.toolType)
            toolCounts[tool] = (toolCounts[tool] ?: 0L) + 1
            if (PenSample.isPen(s.toolType)) {
                penSamplesInEvent++
                onPenSample(s)
            } else if (penInContact) {
                otherToolSamplesDuringPenContact++
            }
        }
        if (penSamplesInEvent > 0) {
            batchSizes.add(penSamplesInEvent.toLong())
            val current = samples.lastOrNull { !it.historical && PenSample.isPen(it.toolType) }
            if (current != null) deliveryLagNs.add(current.receiveTimeNs - current.sampleTimeNs)
        }
    }

    private fun onPenSample(s: PenSample) {
        penSamples++
        if (s.historical) historicalPenSamples++
        buttonStatesSeen = buttonStatesSeen or s.buttonState
        if (s.action == ACTION_BUTTON_PRESS && !s.historical) buttonPressEvents++

        if (lastPenSampleTimeNs >= 0 && s.sampleTimeNs < lastPenSampleTimeNs) nonMonotonic++
        lastPenSampleTimeNs = max(lastPenSampleTimeNs, s.sampleTimeNs)

        val isPointerTransition = !s.historical && s.actionIndex == s.pointerIndex
        when {
            isPointerTransition && (s.action == ACTION_DOWN || s.action == ACTION_POINTER_DOWN) -> {
                strokes++
                penInContact = true
                lastContactTimeNs = -1
                lastHoverTimeNs = -1
            }
            isPointerTransition && s.action == ACTION_CANCEL -> canceledStrokes++
            isPointerTransition && s.action == ACTION_HOVER_ENTER -> lastHoverTimeNs = -1
        }

        if (PenSample.isContactAction(s.action) || s.action == ACTION_UP || s.action == ACTION_POINTER_UP) {
            penContactSamples++
            if (lastContactTimeNs >= 0) contactIntervalsNs.add(s.sampleTimeNs - lastContactTimeNs)
            lastContactTimeNs = s.sampleTimeNs
            if (s.pressure > 0f) {
                minPressure = min(minPressure, s.pressure)
                maxPressure = max(maxPressure, s.pressure)
                pressureHistogram[(s.pressure * 10).toInt().coerceIn(0, 9)]++
            }
        } else if (PenSample.isHoverAction(s.action)) {
            penHoverSamples++
            if (lastHoverTimeNs >= 0) hoverIntervalsNs.add(s.sampleTimeNs - lastHoverTimeNs)
            lastHoverTimeNs = s.sampleTimeNs
            minHoverDistance = min(minHoverDistance, s.distance)
            maxHoverDistance = max(maxHoverDistance, s.distance)
        }

        if (isPointerTransition && (s.action == ACTION_UP || s.action == ACTION_POINTER_UP || s.action == ACTION_CANCEL)) {
            penInContact = false
            lastContactTimeNs = -1
        }
        if (isPointerTransition && s.action == ACTION_HOVER_EXIT) lastHoverTimeNs = -1
    }

    fun reset() {
        listOf(contactIntervalsNs, hoverIntervalsNs, deliveryLagNs, batchSizes).forEach { it.clear() }
        penSamples = 0; penContactSamples = 0; penHoverSamples = 0; historicalPenSamples = 0
        strokes = 0; canceledStrokes = 0; nonMonotonic = 0; otherToolSamplesDuringPenContact = 0
        buttonPressEvents = 0; buttonStatesSeen = 0
        toolCounts.clear()
        minPressure = Float.MAX_VALUE; maxPressure = 0f; pressureHistogram.fill(0)
        minHoverDistance = Float.MAX_VALUE; maxHoverDistance = 0f
        penInContact = false; lastContactTimeNs = -1; lastHoverTimeNs = -1; lastPenSampleTimeNs = -1
    }

    fun summary(): String = buildString {
        fun hz(r: Ring) = r.percentile(50.0)?.takeIf { it > 0 }?.let { "%.0f Hz".format(1e9 / it) } ?: "—"
        fun ms(v: Long?) = v?.let { "%.2f ms".format(it / 1e6) } ?: "—"

        appendLine("CONTACT  rate ${hz(contactIntervalsNs)}  interval p50 ${ms(contactIntervalsNs.percentile(50.0))}  p95 ${ms(contactIntervalsNs.percentile(95.0))}  max ${ms(contactIntervalsNs.percentile(100.0))}")
        appendLine("HOVER    rate ${hz(hoverIntervalsNs)}  interval p50 ${ms(hoverIntervalsNs.percentile(50.0))}  dist ${range(minHoverDistance, maxHoverDistance)}")
        appendLine("DELIVERY lag p50 ${ms(deliveryLagNs.percentile(50.0))}  p95 ${ms(deliveryLagNs.percentile(95.0))}  batch p50 ${batchSizes.percentile(50.0) ?: "—"}  max ${batchSizes.percentile(100.0) ?: "—"}  historical ${pct(historicalPenSamples, penSamples)}")
        appendLine("PRESSURE ${range(minPressure, maxPressure)}  hist ${pressureHistogram.joinToString(" ")}")
        appendLine("PEN      samples $penSamples (contact $penContactSamples, hover $penHoverSamples)  strokes $strokes  canceled $canceledStrokes  non-monotonic $nonMonotonic")
        appendLine("BUTTONS  states-seen 0x${Integer.toHexString(buttonStatesSeen)} ${buttonNames(buttonStatesSeen)}  press-events $buttonPressEvents")
        append("TOOLS    $toolCounts  other-touch-during-pen $otherToolSamplesDuringPenContact")
    }

    private fun range(lo: Float, hi: Float) = if (lo == Float.MAX_VALUE) "—" else "%.3f..%.3f".format(lo, hi)
    private fun pct(n: Long, d: Long) = if (d == 0L) "—" else "%.0f%%".format(100.0 * n / d)

    companion object {
        // MotionEvent.BUTTON_* values.
        const val BUTTON_PRIMARY = 1
        const val BUTTON_SECONDARY = 2
        const val BUTTON_STYLUS_PRIMARY = 32
        const val BUTTON_STYLUS_SECONDARY = 64

        fun buttonNames(state: Int): String = buildList {
            if (state and BUTTON_STYLUS_PRIMARY != 0) add("STYLUS_PRIMARY")
            if (state and BUTTON_STYLUS_SECONDARY != 0) add("STYLUS_SECONDARY")
            if (state and BUTTON_PRIMARY != 0) add("PRIMARY")
            if (state and BUTTON_SECONDARY != 0) add("SECONDARY")
        }.joinToString("|").ifEmpty { "(none)" }
    }
}

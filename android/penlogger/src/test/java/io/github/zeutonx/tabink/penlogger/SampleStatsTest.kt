package io.github.zeutonx.tabink.penlogger

import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_BUTTON_PRESS
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_CANCEL
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_DOWN
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_HOVER_ENTER
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_HOVER_MOVE
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_MOVE
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.ACTION_UP
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.TOOL_FINGER
import io.github.zeutonx.tabink.penlogger.PenSample.Companion.TOOL_STYLUS
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SampleStatsTest {

    private fun s(
        t: Long, action: Int, historical: Boolean = false, tool: Int = TOOL_STYLUS,
        pressure: Float = 0.5f, distance: Float = 0f, buttons: Int = 0,
        receive: Long = t, pointerIndex: Int = 0, pointerId: Int = 0, actionIndex: Int = 0,
    ) = PenSample(
        sampleTimeNs = t, receiveTimeNs = receive, eventSeq = 0, historical = historical,
        action = action, actionIndex = actionIndex, pointerIndex = pointerIndex, pointerId = pointerId,
        toolType = tool, x = 0f, y = 0f, pressure = pressure, distance = distance,
        tilt = 0f, orientation = 0f, buttonState = buttons, historySize = 0, unbuffered = false,
    )

    private val ms = 1_000_000L

    /** A 240 Hz stroke delivered in batches of 2 historical + 1 current sample. */
    private fun stroke240(stats: SampleStats, moves: Int, startNs: Long = 0) {
        val dt = 1_000_000_000L / 240
        var t = startNs
        stats.onEvent(listOf(s(t, ACTION_DOWN)))
        repeat(moves) {
            stats.onEvent(listOf(
                s(t + dt, ACTION_MOVE, historical = true),
                s(t + 2 * dt, ACTION_MOVE, historical = true),
                s(t + 3 * dt, ACTION_MOVE, receive = t + 3 * dt + 4 * ms),
            ))
            t += 3 * dt
        }
        stats.onEvent(listOf(s(t + dt, ACTION_UP)))
    }

    @Test
    fun measuresSampleRateFromHistoricalAndCurrentSamples() {
        val stats = SampleStats()
        stroke240(stats, moves = 50)
        val p50 = stats.contactIntervalsNs.percentile(50.0)!!
        assertEquals(240.0, 1e9 / p50, 1.0)
        assertEquals(1L, stats.strokes)
        assertEquals(100L, stats.historicalPenSamples)
    }

    @Test
    fun measuresBatchSizeAndDeliveryLag() {
        val stats = SampleStats()
        stroke240(stats, moves = 20)
        assertEquals(3L, stats.batchSizes.percentile(100.0))
        assertEquals(4 * ms, stats.deliveryLagNs.percentile(100.0))
    }

    @Test
    fun intervalsDoNotSpanStrokes() {
        val stats = SampleStats()
        stroke240(stats, moves = 5, startNs = 0)
        stroke240(stats, moves = 5, startNs = 10_000 * ms) // 10 s gap between strokes
        assertTrue(stats.contactIntervalsNs.percentile(100.0)!! < 10 * ms)
        assertEquals(2L, stats.strokes)
    }

    @Test
    fun pressureHistogramIgnoresZeroAndClampsAboveOne() {
        val stats = SampleStats()
        stats.onEvent(listOf(s(0, ACTION_DOWN, pressure = 0f)))
        stats.onEvent(listOf(s(1 * ms, ACTION_MOVE, pressure = 0.05f)))
        stats.onEvent(listOf(s(2 * ms, ACTION_MOVE, pressure = 1.3f)))
        assertEquals(1L, stats.pressureHistogram[0])
        assertEquals(1L, stats.pressureHistogram[9])
        assertEquals(0.05f, stats.minPressure, 1e-6f)
        assertEquals(1.3f, stats.maxPressure, 1e-6f)
    }

    @Test
    fun tracksHoverDistanceAndButtons() {
        val stats = SampleStats()
        stats.onEvent(listOf(s(0, ACTION_HOVER_ENTER, pressure = 0f, distance = 3f)))
        stats.onEvent(listOf(s(5 * ms, ACTION_HOVER_MOVE, pressure = 0f, distance = 1f,
            buttons = SampleStats.BUTTON_STYLUS_PRIMARY)))
        stats.onEvent(listOf(s(6 * ms, ACTION_BUTTON_PRESS, pressure = 0f,
            buttons = SampleStats.BUTTON_STYLUS_PRIMARY)))
        assertEquals(2L, stats.penHoverSamples)
        assertEquals(1f, stats.minHoverDistance, 0f)
        assertEquals(3f, stats.maxHoverDistance, 0f)
        assertEquals(1L, stats.buttonPressEvents)
        assertEquals("STYLUS_PRIMARY", SampleStats.buttonNames(stats.buttonStatesSeen))
        assertEquals(5 * ms, stats.hoverIntervalsNs.percentile(50.0))
    }

    @Test
    fun countsNonMonotonicTimestampsAndCancels() {
        val stats = SampleStats()
        stats.onEvent(listOf(s(10 * ms, ACTION_DOWN)))
        stats.onEvent(listOf(s(9 * ms, ACTION_MOVE)))
        stats.onEvent(listOf(s(11 * ms, ACTION_CANCEL)))
        assertEquals(1L, stats.nonMonotonic)
        assertEquals(1L, stats.canceledStrokes)
    }

    @Test
    fun countsFingerContactsDuringPenStroke() {
        val stats = SampleStats()
        stats.onEvent(listOf(s(0, ACTION_DOWN)))
        stats.onEvent(listOf(
            s(1 * ms, ACTION_MOVE, pointerIndex = 0),
            s(1 * ms, ACTION_MOVE, tool = TOOL_FINGER, pointerIndex = 1, pointerId = 1),
        ))
        stats.onEvent(listOf(s(2 * ms, ACTION_UP)))
        stats.onEvent(listOf(s(3 * ms, ACTION_DOWN, tool = TOOL_FINGER)))
        assertEquals(1L, stats.otherToolSamplesDuringPenContact)
        assertEquals(2L, stats.toolCounts["finger"])
    }

    @Test
    fun resetClearsEverything() {
        val stats = SampleStats()
        stroke240(stats, moves = 3)
        stats.reset()
        assertEquals(0L, stats.penSamples)
        assertNull(stats.contactIntervalsNs.percentile(50.0))
        assertTrue(stats.toolCounts.isEmpty())
    }

    @Test
    fun csvRowMatchesHeaderColumnCount() {
        val row = s(1, ACTION_MOVE).toCsvRow()
        assertEquals(PenSample.CSV_HEADER.split(',').size, row.split(',').size)
    }

    @Test
    fun ringPercentileUsesNearestRank() {
        val r = SampleStats.Ring(8)
        (1L..10L).forEach(r::add) // keeps the last 8: 3..10
        assertEquals(8, r.size)
        assertEquals(6L, r.percentile(50.0))
        assertEquals(10L, r.percentile(100.0))
        assertEquals(3L, r.percentile(0.0))
    }
}

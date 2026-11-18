package io.github.zeutonx.tabink.penlogger

/**
 * One pointer sample as delivered by Android, either historical (batched) or current.
 * Raw values only: nothing here is smoothed, rescaled or predicted.
 */
data class PenSample(
    /** Sample time from MotionEvent (event or historical time), CLOCK_MONOTONIC ns. */
    val sampleTimeNs: Long,
    /** When our callback received the containing MotionEvent, CLOCK_MONOTONIC ns. */
    val receiveTimeNs: Long,
    /** Increments once per delivered MotionEvent; groups samples batched together. */
    val eventSeq: Long,
    val historical: Boolean,
    /** MotionEvent.getActionMasked() of the containing event. */
    val action: Int,
    val actionIndex: Int,
    val pointerIndex: Int,
    val pointerId: Int,
    /** MotionEvent.TOOL_TYPE_* */
    val toolType: Int,
    val x: Float,
    val y: Float,
    val pressure: Float,
    /** AXIS_DISTANCE; device-defined units, often 0 when unsupported. */
    val distance: Float,
    val tilt: Float,
    val orientation: Float,
    val buttonState: Int,
    val historySize: Int,
    val unbuffered: Boolean,
) {
    fun toCsvRow(): String = buildString(160) {
        append(sampleTimeNs).append(',')
        append(receiveTimeNs).append(',')
        append(eventSeq).append(',')
        append(if (historical) 'H' else 'C').append(',')
        append(actionName(action)).append(',')
        append(actionIndex).append(',')
        append(pointerIndex).append(',')
        append(pointerId).append(',')
        append(toolName(toolType)).append(',')
        append(x).append(',')
        append(y).append(',')
        append(pressure).append(',')
        append(distance).append(',')
        append(tilt).append(',')
        append(orientation).append(',')
        append(buttonState).append(',')
        append(historySize).append(',')
        append(if (unbuffered) 1 else 0)
    }

    companion object {
        const val CSV_HEADER =
            "sample_time_ns,receive_time_ns,event_seq,source,action,action_index,pointer_index," +
                "pointer_id,tool,x,y,pressure,distance,tilt,orientation,button_state,history_size,unbuffered"

        // MotionEvent constants, duplicated so this file (and its tests) stay pure JVM.
        const val TOOL_UNKNOWN = 0
        const val TOOL_FINGER = 1
        const val TOOL_STYLUS = 2
        const val TOOL_MOUSE = 3
        const val TOOL_ERASER = 4
        const val TOOL_PALM = 5

        const val ACTION_DOWN = 0
        const val ACTION_UP = 1
        const val ACTION_MOVE = 2
        const val ACTION_CANCEL = 3
        const val ACTION_OUTSIDE = 4
        const val ACTION_POINTER_DOWN = 5
        const val ACTION_POINTER_UP = 6
        const val ACTION_HOVER_MOVE = 7
        const val ACTION_SCROLL = 8
        const val ACTION_HOVER_ENTER = 9
        const val ACTION_HOVER_EXIT = 10
        const val ACTION_BUTTON_PRESS = 11
        const val ACTION_BUTTON_RELEASE = 12

        fun toolName(t: Int): String = when (t) {
            TOOL_FINGER -> "finger"
            TOOL_STYLUS -> "stylus"
            TOOL_MOUSE -> "mouse"
            TOOL_ERASER -> "eraser"
            TOOL_PALM -> "palm"
            else -> "unknown($t)"
        }

        fun actionName(a: Int): String = when (a) {
            ACTION_DOWN -> "DOWN"
            ACTION_UP -> "UP"
            ACTION_MOVE -> "MOVE"
            ACTION_CANCEL -> "CANCEL"
            ACTION_OUTSIDE -> "OUTSIDE"
            ACTION_POINTER_DOWN -> "POINTER_DOWN"
            ACTION_POINTER_UP -> "POINTER_UP"
            ACTION_HOVER_MOVE -> "HOVER_MOVE"
            ACTION_SCROLL -> "SCROLL"
            ACTION_HOVER_ENTER -> "HOVER_ENTER"
            ACTION_HOVER_EXIT -> "HOVER_EXIT"
            ACTION_BUTTON_PRESS -> "BUTTON_PRESS"
            ACTION_BUTTON_RELEASE -> "BUTTON_RELEASE"
            else -> "A$a"
        }

        fun isPen(toolType: Int) = toolType == TOOL_STYLUS || toolType == TOOL_ERASER

        fun isContactAction(a: Int) =
            a == ACTION_DOWN || a == ACTION_MOVE || a == ACTION_POINTER_DOWN

        fun isHoverAction(a: Int) =
            a == ACTION_HOVER_ENTER || a == ACTION_HOVER_MOVE || a == ACTION_HOVER_EXIT
    }
}

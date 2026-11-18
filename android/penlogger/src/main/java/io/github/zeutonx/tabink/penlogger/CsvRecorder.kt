package io.github.zeutonx.tabink.penlogger

import android.os.Handler
import android.os.HandlerThread
import java.io.BufferedWriter
import java.io.File

/**
 * Writes samples to CSV on a background thread so file I/O never runs on the input thread.
 * Files land in the app's external files dir; pull them with `adb pull` (see README).
 */
class CsvRecorder(private val file: File, headerComments: List<String>) {

    private val thread = HandlerThread("csv-writer").apply { start() }
    private val handler = Handler(thread.looper)
    private var writer: BufferedWriter? = null

    @Volatile
    var rowsWritten = 0L
        private set

    init {
        handler.post {
            writer = file.bufferedWriter(bufferSize = 64 * 1024).also { w ->
                headerComments.forEach { w.append("# ").append(it).append('\n') }
                w.append(PenSample.CSV_HEADER).append('\n')
            }
        }
    }

    /** Called on the UI thread with one MotionEvent's samples; formatting happens off-thread. */
    fun write(samples: List<PenSample>) {
        handler.post {
            val w = writer ?: return@post
            for (s in samples) w.append(s.toCsvRow()).append('\n')
            rowsWritten += samples.size
        }
    }

    fun close(onClosed: (File) -> Unit) {
        handler.post {
            writer?.flush()
            writer?.close()
            writer = null
            onClosed(file)
            thread.quitSafely()
        }
    }
}

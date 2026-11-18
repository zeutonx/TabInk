package io.github.zeutonx.tabink.penlogger

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * P0.2 S Pen sample logger (docs/roadmap.md). Write on the white area; the stats panel
 * shows live sample rate, batching, delivery lag, pressure, hover and buttons.
 * Record writes every raw sample to CSV for offline analysis.
 */
class MainActivity : Activity() {

    private val stats = SampleStats()
    private var recorder: CsvRecorder? = null
    private lateinit var penView: PenLoggerView
    private lateinit var statsText: TextView
    private lateinit var statusText: TextView
    private lateinit var recordButton: Button
    private val ui = Handler(Looper.getMainLooper())

    private val refreshStats = object : Runnable {
        override fun run() {
            statsText.text = stats.summary()
            recorder?.let { statusText.text = "● REC  ${it.rowsWritten} rows" }
            ui.postDelayed(this, 250)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        penView = PenLoggerView(this).apply {
            onSamples = { samples ->
                stats.onEvent(samples)
                recorder?.write(samples)
            }
        }

        recordButton = button("Record") { toggleRecording() }
        val unbufferedButton = button("Unbuffered: off") {}
        unbufferedButton.setOnClickListener {
            penView.unbufferedDispatch = !penView.unbufferedDispatch
            unbufferedButton.text = "Unbuffered: " + if (penView.unbufferedDispatch) "on" else "off"
            stats.reset()
        }
        val dotsButton = button("Dots: on") {}
        dotsButton.setOnClickListener {
            penView.showSampleDots = !penView.showSampleDots
            dotsButton.text = "Dots: " + if (penView.showSampleDots) "on" else "off"
        }

        statusText = TextView(this).apply {
            setTextColor(Color.DKGRAY); textSize = 13f; setPadding(16, 0, 16, 0)
            text = "Not recording"
        }
        statsText = TextView(this).apply {
            typeface = Typeface.MONOSPACE; textSize = 12f
            setTextColor(Color.BLACK); setBackgroundColor(Color.rgb(245, 245, 240))
            setPadding(16, 8, 16, 8)
        }

        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.rgb(230, 230, 225))
            addView(recordButton)
            addView(button("Clear ink") { penView.clearInk() })
            addView(button("Reset stats") { stats.reset() })
            addView(unbufferedButton)
            addView(dotsButton)
            addView(statusText)
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(bar)
            addView(statsText)
            addView(penView, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
            setOnApplyWindowInsetsListener { v, insets ->
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                WindowInsets.CONSUMED
            }
        }
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        ui.post(refreshStats)
    }

    override fun onPause() {
        ui.removeCallbacks(refreshStats)
        stopRecording()
        super.onPause()
    }

    private fun toggleRecording() {
        if (recorder == null) startRecording() else stopRecording()
    }

    private fun startRecording() {
        val dir = getExternalFilesDir(null) ?: filesDir
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        val file = File(dir, "pen-$stamp.csv")
        recorder = CsvRecorder(file, deviceInfo())
        stats.reset()
        recordButton.text = "Stop"
    }

    private fun stopRecording() {
        val r = recorder ?: return
        recorder = null
        recordButton.text = "Record"
        r.close { f -> ui.post { statusText.text = "Saved ${f.name} (${r.rowsWritten} rows)" } }
    }

    private fun deviceInfo(): List<String> {
        val d = display
        val m = resources.displayMetrics
        return listOf(
            "TabInk pen logger ${packageManager.getPackageInfo(packageName, 0).versionName}",
            "device ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})",
            "android ${Build.VERSION.RELEASE} sdk ${Build.VERSION.SDK_INT} build ${Build.DISPLAY}",
            "display ${m.widthPixels}x${m.heightPixels} density ${m.densityDpi} refresh ${d?.refreshRate} Hz",
            "view ${penView.width}x${penView.height} unbuffered ${penView.unbufferedDispatch}",
            "started ${Date()}",
            "times are CLOCK_MONOTONIC ns; x/y are view pixels; pressure/distance are raw MotionEvent values",
        )
    }

    private fun button(label: String, onClick: (View) -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        setOnClickListener(onClick)
    }
}

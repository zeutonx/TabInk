package io.github.zeutonx.tabink.nepaliink

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.graphics.Color
import android.os.Bundle
import android.view.WindowInsets
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import com.google.mlkit.common.model.DownloadConditions
import com.google.mlkit.common.model.RemoteModelManager
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognition
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognitionModel
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognitionModelIdentifier
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognizer
import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognizerOptions
import com.google.mlkit.vision.digitalink.recognition.RecognitionContext
import com.google.mlkit.vision.digitalink.recognition.WritingArea

/** P0.4 spike: write the prompt, recognise on device, measure error. Ink and text are never logged or saved. */
class MainActivity : Activity() {
    private lateinit var ink: InkView
    private lateinit var prompt: TextView
    private lateinit var result: TextView
    private lateinit var stats: TextView
    private lateinit var recognize: Button
    private lateinit var copy: Button
    private lateinit var next: Button
    private lateinit var download: Button

    private lateinit var model: DigitalInkRecognitionModel
    private var recognizer: DigitalInkRecognizer? = null
    private var run = TestRun()
    private var busy = false
    private var revision = 0
    private var text = ""
    private var lastMs = -1L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        run = TestRun.decode(prefs().getString(KEY_RUN, null)) ?: TestRun()
        model = DigitalInkRecognitionModel.builder(
            checkNotNull(DigitalInkRecognitionModelIdentifier.fromLanguageTag("ne-NP")),
        ).build()

        prompt = label(34f)
        result = label(30f)
        stats = label(14f)
        recognize = button("Recognize") { recognizeInk() }
        copy = button("Copy text") { copy("Nepali text", text) }
        next = button("Next") { nextPrompt() }
        download = button("Download model (~20 MB)") { downloadModel() }
        ink = InkView(this).apply { onInkChanged = { revision++; refresh() } }
        val bar = LinearLayout(this).apply {
            listOf(recognize, button("Clear") { ink.clear() }, next, copy).forEach { addView(it) }
        }
        val bar2 = LinearLayout(this).apply {
            listOf(download, button("Copy report") { copy("Nepali ink report", run.report()) },
                button("Restart test") { confirmRestart() }).forEach { addView(it) }
        }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(230, 230, 225))
            listOf(bar, bar2, prompt, stats).forEach { addView(it) }
            addView(ink, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(260)))
            addView(result)
            setOnApplyWindowInsetsListener { v, insets ->
                val b = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                v.setPadding(b.left, b.top, b.right, b.bottom)
                WindowInsets.CONSUMED
            }
        }
        setContentView(root)
        refresh()
        RemoteModelManager.getInstance().isModelDownloaded(model)
            .addOnSuccessListener(this) { if (it) ready() }
            .addOnFailureListener(this) { result.text = "Model check failed: ${it.message}" }
    }

    override fun onDestroy() {
        recognizer?.close()
        super.onDestroy()
    }

    private fun downloadModel() {
        download.isEnabled = false
        result.text = "Downloading…"
        RemoteModelManager.getInstance().download(model, DownloadConditions.Builder().build())
            .addOnSuccessListener(this) { result.text = "Model ready"; ready() }
            .addOnFailureListener(this) { result.text = "Download failed: ${it.message}"; download.isEnabled = true }
    }

    private fun ready() {
        if (recognizer == null) {
            recognizer = DigitalInkRecognition.getClient(DigitalInkRecognizerOptions.builder(model).build())
        }
        download.isEnabled = false
        refresh()
    }

    private fun recognizeInk() {
        val r = recognizer ?: return
        val at = revision
        val start = System.nanoTime()
        // Empty pre-context: the prompt must not help the recognizer.
        val ctx = RecognitionContext.builder().setPreContext("")
            .setWritingArea(WritingArea(ink.width.toFloat(), ink.height.toFloat())).build()
        busy = true
        refresh()
        r.recognize(ink.buildInk(), ctx)
            .addOnSuccessListener(this) { res ->
                busy = false
                if (at == revision) onResult(res.candidates.map { it.text }, start) // else: ink changed meanwhile
                refresh()
            }
            .addOnFailureListener(this) {
                busy = false
                result.text = "Recognition failed: ${it.message}"
                refresh()
            }
    }

    private fun onResult(candidates: List<String>, startNs: Long) {
        lastMs = (System.nanoTime() - startNs) / 1_000_000
        text = candidates.firstOrNull().orEmpty()
        result.text = text.ifEmpty { "(nothing recognised)" }
        run.score(candidates) // first attempt only, so rewriting until it works does not improve the score
        save()
    }

    private fun nextPrompt() {
        run.next()
        save()
        text = ""
        result.text = ""
        ink.clear()
    }

    private fun confirmRestart() {
        AlertDialog.Builder(this).setMessage("Delete all scores and start from prompt 1?")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Restart") { _, _ ->
                run = TestRun()
                save()
                text = ""
                result.text = ""
                ink.clear()
            }.show()
    }

    private fun copy(label: String, value: String) {
        getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText(label, value))
    }

    private fun prefs() = getSharedPreferences("run", MODE_PRIVATE)

    /** Counts only (see TestRun.encode). */
    private fun save() = prefs().edit().putString(KEY_RUN, run.encode()).apply()

    private fun refresh() {
        val total = Prompts.all.size
        prompt.text = if (run.done) "Done. Write anything." else "${run.index + 1}/$total   ${Prompts.all[run.index]}"
        stats.text = run.report() + if (lastMs >= 0) "\nlast recognition $lastMs ms" else ""
        recognize.isEnabled = recognizer != null && !busy && !ink.isEmpty && !ink.isWriting
        next.isEnabled = !busy && !run.done
        copy.isEnabled = text.isNotEmpty()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun label(size: Float) = TextView(this).apply {
        textSize = size
        setTextColor(Color.BLACK)
        setPadding(dp(12), dp(6), dp(12), dp(6))
    }

    private fun button(t: String, onClick: () -> Unit) = Button(this).apply {
        text = t
        setOnClickListener { onClick() }
    }

    private companion object {
        const val KEY_RUN = "run"
    }
}

package io.github.zeutonx.tabink.nepaliink

import java.text.Normalizer
import java.util.Locale

/** Error-rate arithmetic for the recognition test. Pure JVM; keeps counts only, never text. */
class Accuracy {
    var prompts = 0; private set
    var exact = 0; private set
    var inCandidates = 0; private set
    var charEdits = 0; private set
    var chars = 0; private set
    var wordEdits = 0; private set
    var words = 0; private set

    val cer get() = if (chars == 0) null else charEdits.toDouble() / chars
    val wer get() = if (words == 0) null else wordEdits.toDouble() / words

    /** [candidates] best first; an empty list counts as recognising nothing. */
    fun add(expected: String, candidates: List<String>) {
        val want = normalize(expected)
        val got = candidates.map(::normalize)
        val top = got.firstOrNull().orEmpty()
        val wantChars = codePoints(want)
        val wantWords = wordsOf(want)
        prompts++
        if (top == want) exact++
        if (want in got) inCandidates++
        charEdits += editDistance(wantChars, codePoints(top))
        chars += wantChars.size
        wordEdits += editDistance(wantWords, wordsOf(top))
        words += wantWords.size
    }

    fun merge(o: Accuracy) = restore(counts().zip(o.counts()) { a, b -> a + b })

    fun counts() = listOf(prompts, exact, inCandidates, charEdits, chars, wordEdits, words)

    fun restore(c: List<Int>) {
        require(c.size == COUNTS)
        prompts = c[0]; exact = c[1]; inCandidates = c[2]
        charEdits = c[3]; chars = c[4]; wordEdits = c[5]; words = c[6]
    }

    fun summary(): String {
        fun pct(v: Double?) = if (v == null) "-" else "%.1f%%".format(Locale.ROOT, v * 100)
        return "scored $prompts  CER ${pct(cer)} ($charEdits/$chars)  WER ${pct(wer)} ($wordEdits/$words)  " +
            "exact $exact  in-candidates $inCandidates"
    }

    companion object {
        const val COUNTS = 7

        /** NFC, no ZWJ/ZWNJ (invisible, not handwritten), collapsed spaces. */
        fun normalize(s: String): String =
            Normalizer.normalize(s, Normalizer.Form.NFC)
                .replace("\u200c", "").replace("\u200d", "")
                .replace(Regex("\\s+"), " ").trim()

        fun codePoints(s: String): List<Int> = s.codePoints().toArray().toList()
        private fun wordsOf(s: String) = s.split(' ').filter { it.isNotEmpty() }

        fun <E> editDistance(a: List<E>, b: List<E>): Int {
            var prev = IntArray(b.size + 1) { it }
            for (i in a.indices) {
                val cur = IntArray(b.size + 1)
                cur[0] = i + 1
                for (j in b.indices) {
                    cur[j + 1] = minOf(prev[j + 1] + 1, cur[j] + 1, prev[j] + if (a[i] == b[j]) 0 else 1)
                }
                prev = cur
            }
            return prev[b.size]
        }
    }
}

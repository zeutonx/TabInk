package io.github.zeutonx.tabink.nepaliink

import java.util.Locale

/** What kind of character an error hit. Anything else (punctuation, Latin…) is [OTHER]. */
enum class CharClass {
    CONSONANT, VOWEL, MATRA, HALANT, SIGN, DIGIT, SPACE, OTHER;

    companion object {
        fun of(cp: Int): CharClass = when (cp) {
            0x20 -> SPACE
            0x094D -> HALANT
            0x0900, 0x0901, 0x0902, 0x0903, 0x093C -> SIGN // chandrabindu, anusvara, visarga, nukta
            in 0x0904..0x0914, 0x0960, 0x0961 -> VOWEL
            in 0x0915..0x0939, in 0x0958..0x095F -> CONSONANT
            in 0x093E..0x094C, 0x0962, 0x0963 -> MATRA
            in 0x0966..0x096F -> DIGIT
            else -> OTHER
        }
    }
}

/** Alternatives scored on the same ink as the gate setting. Exploratory: they never affect the gate. */
enum class Variant { FITTED_AREA, NO_CONTEXT }

/**
 * Progress through the prompt set. Holds and stores counts only, never ink or recognised text.
 * Pure JVM.
 */
class TestRun {
    var index = 0; private set
    var skipped = 0; private set
    var scored = false; private set

    /** Edits per class: the expected character's class, or the inserted one's for insertions. */
    private val errors = IntArray(CharClass.entries.size)
    private val expected = IntArray(CharClass.entries.size)
    private val byCategory = Prompts.Category.entries.associateWith { Accuracy() }

    private val byVariant = Variant.entries.associateWith { Accuracy() }

    val done get() = index >= Prompts.all.size
    val total get() = Accuracy().also { t -> byCategory.values.forEach(t::merge) }

    /** Counts the first attempt at the current prompt; later attempts are ignored. */
    fun score(candidates: List<String>, variants: Map<Variant, List<String>> = emptyMap()) {
        if (done || scored) return
        scored = true
        val want = Prompts.all[index]
        byCategory.getValue(Prompts.categoryOf(index)).add(want, candidates)
        variants.forEach { (v, c) -> byVariant.getValue(v).add(want, c) }
        val w = Accuracy.codePoints(Accuracy.normalize(want))
        val g = Accuracy.codePoints(Accuracy.normalize(candidates.firstOrNull().orEmpty()))
        w.forEach { expected[CharClass.of(it).ordinal]++ }
        editedChars(w, g).forEach { errors[CharClass.of(it).ordinal]++ }
    }

    fun next() {
        if (done) return
        if (!scored) skipped++
        index++
        scored = false
    }

    /** Null until every prompt is done. Skipping anything fails the gate. */
    fun gatePassed(): Boolean? {
        if (!done) return null
        val t = total
        return skipped == 0 && t.prompts == Prompts.all.size && (t.cer ?: 1.0) < GATE_CER
    }

    fun report(): String = buildString {
        val verdict = when (gatePassed()) {
            true -> "PASS"
            false -> "FAIL"
            null -> "incomplete"
        }
        appendLine("set v${Prompts.VERSION}  gate (CER < ${(GATE_CER * 100).toInt()}%, none skipped): $verdict  skipped $skipped")
        appendLine("ALL      ${total.summary()}")
        byCategory.forEach { (c, a) -> appendLine("${c.name.padEnd(8)} ${a.summary()}") }
        byVariant.forEach { (v, a) -> appendLine("alt ${v.name}  ${a.summary()}") }
        append("ERRORS   ")
        append(CharClass.entries.filter { errors[it.ordinal] + expected[it.ordinal] > 0 }
            .joinToString("  ") { "${it.name.lowercase(Locale.ROOT)} ${errors[it.ordinal]}/${expected[it.ordinal]}" }
            .ifEmpty { "-" })
    }

    fun encode(): String = (listOf(Prompts.VERSION, index, skipped, if (scored) 1 else 0) +
        errors.toList() + expected.toList() + byCategory.values.flatMap { it.counts() } +
        byVariant.values.flatMap { it.counts() }).joinToString(",")

    companion object {
        const val GATE_CER = 0.10

        /** Null if absent, corrupt, or from another prompt set: the run then starts again. */
        fun decode(s: String?): TestRun? {
            val v = s?.split(',')?.map { it.toIntOrNull() ?: return null } ?: return null
            val n = CharClass.entries.size
            val base = 4 + 2 * n + Prompts.Category.entries.size * Accuracy.COUNTS
            // Runs saved before variants existed stop at [base]; their variants start empty.
            val hasVariants = v.size == base + Variant.entries.size * Accuracy.COUNTS
            if (v.size != base && !hasVariants) return null
            if (v.any { it < 0 } || v[0] != Prompts.VERSION || v[1] > Prompts.all.size || v[3] > 1) return null
            val r = TestRun()
            r.index = v[1]
            r.skipped = v[2]
            r.scored = v[3] == 1
            for (i in 0 until n) {
                r.errors[i] = v[4 + i]
                r.expected[i] = v[4 + n + i]
            }
            r.byCategory.values.forEachIndexed { i, a ->
                val at = 4 + 2 * n + i * Accuracy.COUNTS
                a.restore(v.subList(at, at + Accuracy.COUNTS))
            }
            if (hasVariants) r.byVariant.values.forEachIndexed { i, a ->
                val at = base + i * Accuracy.COUNTS
                a.restore(v.subList(at, at + Accuracy.COUNTS))
            }
            // Every finished prompt was either scored or skipped.
            if (r.total.prompts + r.skipped != r.index + v[3]) return null
            return r
        }

        /** One character per edit on a minimal alignment, so its size equals the edit distance. */
        fun editedChars(want: List<Int>, got: List<Int>): List<Int> {
            val d = Array(want.size + 1) { IntArray(got.size + 1) }
            for (i in 0..want.size) d[i][0] = i
            for (j in 0..got.size) d[0][j] = j
            for (i in 1..want.size) for (j in 1..got.size) {
                d[i][j] = minOf(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + if (want[i - 1] == got[j - 1]) 0 else 1)
            }
            val out = ArrayList<Int>()
            var i = want.size
            var j = got.size
            while (i > 0 || j > 0) {
                val same = i > 0 && j > 0 && want[i - 1] == got[j - 1]
                when {
                    i > 0 && j > 0 && d[i][j] == d[i - 1][j - 1] + (if (same) 0 else 1) -> {
                        if (!same) out += want[i - 1]
                        i--; j--
                    }
                    i > 0 && d[i][j] == d[i - 1][j] + 1 -> { out += want[i - 1]; i-- }
                    else -> { out += got[j - 1]; j-- }
                }
            }
            return out
        }
    }
}

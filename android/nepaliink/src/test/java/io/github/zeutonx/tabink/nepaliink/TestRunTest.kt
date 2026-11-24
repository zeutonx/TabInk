package io.github.zeutonx.tabink.nepaliink

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TestRunTest {
    private fun runAll(answer: (Int) -> List<String>?) = TestRun().apply {
        while (!done) { answer(index)?.let { score(it) }; next() }
    }

    @Test fun perfectRunPasses() {
        val r = runAll { listOf(Prompts.all[it]) }
        assertEquals(true, r.gatePassed())
        assertEquals(100, r.total.exact)
    }

    @Test fun oneSkipFailsTheGate() {
        val r = runAll { if (it == 5) null else listOf(Prompts.all[it]) }
        assertEquals(1, r.skipped)
        assertEquals(false, r.gatePassed())
    }

    @Test fun emptyResultsFailAndUnfinishedIsUndecided() {
        assertEquals(false, runAll { emptyList() }.gatePassed())
        assertNull(TestRun().gatePassed())
    }

    @Test fun onlyFirstAttemptCounts() {
        val r = TestRun()
        r.score(emptyList())
        r.score(listOf(Prompts.all[0]))
        assertEquals(1, r.total.prompts)
        assertEquals(0, r.total.exact)
    }

    @Test fun scoringAfterTheEndIsIgnored() {
        val r = runAll { listOf(Prompts.all[it]) }
        r.score(listOf("x"))
        r.next()
        assertEquals(100, r.total.prompts)
        assertEquals(100, r.index)
    }

    @Test fun roundTripsAtEveryStage() {
        val r = TestRun()
        while (!r.done) {
            assertEquals(r.report(), TestRun.decode(r.encode())!!.report())
            if (r.index % 3 != 0) r.score(listOf("नदि"))
            assertEquals(r.encode(), TestRun.decode(r.encode())!!.encode())
            r.next()
        }
        assertEquals(r.encode(), TestRun.decode(r.encode())!!.encode())
    }

    @Test fun variantsArePairedButNeverAffectTheGate() {
        val r = TestRun()
        while (!r.done) {
            val right = listOf(Prompts.all[r.index])
            r.score(right, mapOf(Variant.FITTED_AREA to emptyList(), Variant.NO_CONTEXT to right))
            r.next()
        }
        assertEquals(true, r.gatePassed())
        assertTrue(r.report().contains("alt FITTED_AREA  scored 100  CER 100.0%"))
        assertTrue(r.report().contains("alt NO_CONTEXT  scored 100  CER 0.0%"))
        assertEquals(r.report(), TestRun.decode(r.encode())!!.report())
    }

    @Test fun runSavedBeforeVariantsStillLoads() {
        val r = TestRun().apply { score(listOf(Prompts.all[0])); next() }
        val old = r.encode().split(',').dropLast(Variant.entries.size * Accuracy.COUNTS).joinToString(",")
        assertEquals(r.report(), TestRun.decode(old)!!.report())
    }

    @Test fun badStateIsRejected() {
        val good = TestRun().apply { score(listOf("x")); next() }.encode()
        assertNull(TestRun.decode(null))
        assertNull(TestRun.decode(""))
        assertNull(TestRun.decode("$good,1"))
        assertNull(TestRun.decode(good.replaceFirst("1,", "99,"))) // other prompt set
        assertNull(TestRun.decode(good.replaceFirst("1,1,", "1,7,"))) // index without scores
    }

    @Test fun storedStateHoldsNoText() {
        val s = TestRun().apply { score(listOf("नदि")) }.encode()
        assertTrue(s.all { it.isDigit() && it < '\u0080' || it == ',' })
    }

    @Test fun errorsAreAttributedByClass() {
        fun classes(a: String, b: String) =
            TestRun.editedChars(Accuracy.codePoints(a), Accuracy.codePoints(b)).map(CharClass::of)
        assertEquals(listOf(CharClass.MATRA), classes("नदी", "नदि"))
        assertEquals(listOf(CharClass.HALANT), classes("क्ष", "कष"))
        assertEquals(listOf(CharClass.SIGN), classes("हाँस", "हास"))
        assertEquals(listOf(CharClass.DIGIT), classes("१२", "१३"))
        assertEquals(listOf(CharClass.SPACE), classes("कख", "क ख")) // insertion
        assertEquals(emptyList<CharClass>(), classes("नेपाल", "नेपाल"))
    }

    @Test fun attributedErrorsEqualEditDistance() {
        for (a in Prompts.all.take(40)) for (b in Prompts.all.takeLast(40)) {
            val x = Accuracy.codePoints(a)
            val y = Accuracy.codePoints(b)
            assertEquals(Accuracy.editDistance(x, y), TestRun.editedChars(x, y).size)
        }
    }

    @Test fun everyPromptCharacterHasAClassAndCategory() {
        Prompts.all.forEach { p -> Accuracy.codePoints(p).forEach { assertFalse(p, CharClass.of(it) == CharClass.OTHER) } }
        val sizes = Prompts.all.indices.groupingBy(Prompts::categoryOf).eachCount()
        assertEquals(mapOf(Prompts.Category.WORDS to 70, Prompts.Category.LINES to 20, Prompts.Category.NUMBERS to 10), sizes)
    }
}

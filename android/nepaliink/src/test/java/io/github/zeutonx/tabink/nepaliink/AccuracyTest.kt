package io.github.zeutonx.tabink.nepaliink

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AccuracyTest {
    @Test fun perfectMatch() {
        val a = Accuracy().apply { add("म नेपाली हुँ", listOf("म नेपाली हुँ")) }
        assertEquals(0.0, a.cer!!, 0.0)
        assertEquals(0.0, a.wer!!, 0.0)
        assertEquals(1, a.exact)
    }

    @Test fun oneWrongMatra() {
        val a = Accuracy().apply { add("नदी", listOf("नदि", "नदी")) }
        assertEquals(1, a.charEdits)
        assertEquals(3, a.chars)
        assertEquals(1, a.wordEdits)
        assertEquals(0, a.exact)
        assertEquals(1, a.inCandidates)
    }

    @Test fun nothingRecognisedIsFullError() {
        val a = Accuracy().apply { add("घर", emptyList()) }
        assertEquals(1.0, a.cer!!, 0.0)
    }

    @Test fun ratesArePooledAcrossPrompts() {
        val a = Accuracy().apply { add("घर", listOf("घर")); add("नदी", listOf("नदि")) }
        assertEquals(1.0 / 5, a.cer!!, 1e-9)
    }

    @Test fun normalizesJoinersAndSpaces() {
        assertEquals("क्ष घर", Accuracy.normalize(" क्\u200dष   घर\n"))
        assertEquals(Accuracy.normalize("क़"), Accuracy.normalize("क़"))
    }

    @Test fun emptyHasNoRate() {
        assertNull(Accuracy().cer)
        assertTrue(Accuracy().summary().contains("CER -"))
    }

    @Test fun promptSetIsFixedAndCoversTheHardCases() {
        val p = Prompts.all
        assertEquals(100, p.size)
        assertEquals(100, p.toSet().size)
        p.forEach { assertEquals(it, Accuracy.normalize(it)) }
        val joined = p.joinToString(" ")
        val digits = ('०'..'९').map { it.toString() }
        val signs = "ािीुूृेैोौंँः".map { it.toString() }
        (listOf("क्ष", "त्र", "ज्ञ", "श्र", "द्ध") + digits + signs).forEach { assertTrue("missing $it", it in joined) }
    }
}

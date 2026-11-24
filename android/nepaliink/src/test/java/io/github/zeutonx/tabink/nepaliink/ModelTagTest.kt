package io.github.zeutonx.tabink.nepaliink

import com.google.mlkit.vision.digitalink.recognition.DigitalInkRecognitionModelIdentifier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

/** The pinned ML Kit library itself must know the Nepali (Nepal) model. */
class ModelTagTest {
    @Test fun nepaliNepalIsAKnownModel() {
        val id = DigitalInkRecognitionModelIdentifier.fromLanguageTag("ne-NP")
        assertNotNull(id)
        assertEquals("ne-NP", id!!.languageTag)
    }
}

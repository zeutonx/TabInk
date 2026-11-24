package io.github.zeutonx.tabink.nepaliink

/** Fixed 100-item test set. Changing it makes results incomparable, so bump [VERSION]. */
object Prompts {
    const val VERSION = 1

    private const val WORDS = """
        नेपाल काठमाडौं पोखरा हिमाल पहाड तराई नदी ताल गाउँ शहर
        घर ढोका झ्याल किताब कलम कापी विद्यालय विद्यार्थी शिक्षक गणित
        आमा बुबा दाजु भाइ दिदी बहिनी साथी परिवार मानिस बालक
        क्षमा क्षेत्र शिक्षा परीक्षा नक्षत्र त्रिभुज चित्र मित्र पुत्र यात्रा
        ज्ञान विज्ञान आज्ञा श्रम श्री विश्राम प्रश्न उत्तर राष्ट्र कृष्ण
        वृक्ष ऋतु दृष्टि हृदय संस्कृति द्वार विद्वान बुद्ध शुद्ध पद्म
        हाँस आँखा अंग ऊँट दुःख ठूलो छाता ढुङ्गा फूल औषधि
    """

    private val LINES = listOf(
        "मेरो नाम राम हो", "म नेपाली हुँ", "आज मौसम राम्रो छ", "तिमी कहाँ जान्छौ",
        "हामी विद्यालय जान्छौं", "उनले चिठी लेखे", "पानी पर्न थाल्यो", "यो किताब मेरो हो",
        "सगरमाथा संसारको अग्लो हिमाल हो", "ज्ञान नै शक्ति हो", "भोलि परीक्षा छ",
        "कृपया ढोका बन्द गर्नुहोस्", "धन्यवाद फेरि भेटौंला", "गणितको प्रश्न सजिलो छ",
        "नदीमा पुल बनाइयो", "बजारमा भीड थियो", "मलाई चिया मन पर्छ",
        "कक्षामा विद्यार्थी पढ्दैछन्", "खाना खानुभयो", "राष्ट्रिय भाषा नेपाली हो",
    )

    private val NUMBERS = listOf(
        "०१२३४", "५६७८९", "२०८३", "१०० रुपैयाँ", "१५ मिनेट",
        "९८४१", "३६५ दिन", "कक्षा १२", "७० किलो", "वि सं २०८०",
    )

    enum class Category { WORDS, LINES, NUMBERS }

    private val words = WORDS.trim().split(Regex("\\s+"))

    val all: List<String> = words + LINES + NUMBERS

    fun categoryOf(index: Int) = when {
        index < words.size -> Category.WORDS
        index < words.size + LINES.size -> Category.LINES
        else -> Category.NUMBERS
    }
}

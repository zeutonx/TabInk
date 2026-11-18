plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.zeutonx.tabink.penlogger"
    compileSdk = 36
    // Matches the SDK baked into docker/android-build (BUILD_TOOLS arg).
    buildToolsVersion = "36.1.0"

    defaultConfig {
        applicationId = "io.github.zeutonx.tabink.penlogger"
        // API 34 gives getHistoricalEventTimeNanos(); the Tab S10 FE ships Android 15.
        minSdk = 34
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0-p0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

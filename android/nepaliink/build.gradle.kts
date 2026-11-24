plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.zeutonx.tabink.nepaliink"
    compileSdk = 36
    // Matches the SDK baked into docker/android-build (BUILD_TOOLS arg).
    buildToolsVersion = "36.1.0"

    defaultConfig {
        applicationId = "io.github.zeutonx.tabink.nepaliink"
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
    // Proprietary (ML Kit Terms of Service). Feasibility spike only until decision D8.
    implementation("com.google.mlkit:digital-ink-recognition:19.0.0")
    testImplementation("junit:junit:4.13.2")
}

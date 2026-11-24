pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "TabInk"

// P0 spike: standalone S Pen sample logger (docs/roadmap.md P0.2).
include(":penlogger")

// P0.4 spike: Nepali handwriting recognition accuracy (docs/roadmap.md P0.4).
include(":nepaliink")

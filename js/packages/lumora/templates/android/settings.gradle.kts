pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\.android.*")
                includeGroupByRegex("com\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "LumoraApp"

includeBuild("{{lumoraSource}}/Android/build-logic")

include(":lumora-android")
project(":lumora-android").projectDir = file("{{lumoraSource}}/Android/lumora-android")

apply(from = "settings-modules.gradle.kts")

include(":app")
project(":app").projectDir = file("app")
project(":app").buildFileName = "android/build.gradle.kts"

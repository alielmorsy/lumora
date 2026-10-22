pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
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

rootProject.name = "Lumora"

includeBuild("build-logic")

include(":lumora-android")
project(":lumora-android").projectDir = file("lumora-android")

val searchRoots = listOf(
    file("../Samples"),
    file("../Apps")
)

for (searchRoot in searchRoots) {
    if (!searchRoot.isDirectory) continue
    searchRoot.listFiles()?.filter { it.isDirectory && File(it, "lumora.json").exists() }?.forEach { appDir ->
        val projectName = ":${appDir.name}"
        include(projectName)
        val p = project(projectName)
        p.projectDir = appDir
        val androidBuildFile = File(appDir, "android/build.gradle.kts")
        if (androidBuildFile.exists()) {
            p.buildFileName = "android/build.gradle.kts"
        }
    }
}

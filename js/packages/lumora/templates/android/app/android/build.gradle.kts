plugins {
    id("lumora.android.app")
}

android {
    sourceSets.getByName("main").java.directories.add("gen/kotlin")
}

dependencies {
    rootProject.subprojects
        .filter { it.name.startsWith("lumora-module-") }
        .forEach { add("implementation", project(it.path)) }
}

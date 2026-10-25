plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "{{androidPackage}}"
    compileSdk = 34
    defaultConfig { minSdk = 26 }
}

dependencies {
    implementation(project(":lumora-android"))
}

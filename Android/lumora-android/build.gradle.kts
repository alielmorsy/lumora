plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.lumora.android"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        minSdk = 30
        manifestPlaceholders["orientation"] = "unspecified"
        manifestPlaceholders["libName"] = "lumora"
        manifestPlaceholders["appLabel"] = "Lumora"
        manifestPlaceholders["appIcon"] = "@mipmap/ic_launcher"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    buildFeatures {
        prefab = true
    }
}

dependencies {
    api(libs.androidx.games.activity)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.core.ktx)
}

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.alamy.tablet"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.alamy.tablet"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "2.0"
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        resources.excludes += "META-INF/*"
    }
}

// Zero external dependencies - pure Android framework APIs for minimum
// APK size, minimum GC pressure on the input path, and maximum reliability.
dependencies {
}

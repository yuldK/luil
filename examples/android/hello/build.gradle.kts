import groovy.json.JsonSlurper

plugins {
    id("com.android.application")
}

// GameActivity의 판번은 네이티브 절반과 같은 핀이 정한다 (third_party/game-activity-prep.json).
// 두 절반의 판번이 어긋나면 JNI 서명이 맞지 않아 실행 중에 깨진다.
val gameActivityPin = JsonSlurper().parse(rootDir.resolve("../../third_party/game-activity-prep.json")) as Map<*, *>
val gameActivityVersion = gameActivityPin["version"] as String

// luil의 CMake가 세운 네이티브 라이브러리 자리다 (<build>/jniLibs/<구성>/<abi>).
// 다른 빌드 디렉터리를 쓰면 -Pluil.buildDirectory=<path>로 준다.
val luilBuildDirectory = providers.gradleProperty("luil.buildDirectory")
    .getOrElse(rootDir.resolve("../../build/android-arm64").path)

android {
    namespace = "io.github.yuldk.luil.hello"
    compileSdk = 37
    // 패키징이 네이티브 라이브러리의 디버그 정보를 걷어 낼 때 쓴다. luil이 빌드에 쓰는 것과 같은 NDK다.
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "io.github.yuldk.luil.hello"
        // Skia 패키지가 정한 하한이다 (ndk_api = 26).
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    sourceSets {
        getByName("debug") {
            jniLibs.directories.add("$luilBuildDirectory/jniLibs/Debug")
        }
        getByName("release") {
            jniLibs.directories.add("$luilBuildDirectory/jniLibs/Release")
        }
    }
}

dependencies {
    implementation("androidx.games:games-activity:$gameActivityVersion")
    // GameActivity가 AppCompatActivity라 AppCompat 테마가 필요하다.
    implementation("androidx.appcompat:appcompat:1.8.0")
}

import groovy.json.JsonSlurper

plugins {
    id("com.android.application")
}

// GameActivity의 판번은 네이티브 절반과 같은 핀이 정한다 (third_party/game-activity-prep.json).
// 두 절반의 판번이 어긋나면 JNI 서명이 맞지 않아 실행 중에 깨진다.
val gameActivityPin = JsonSlurper().parse(rootDir.resolve("../../third_party/game-activity-prep.json")) as Map<*, *>
val gameActivityVersion = gameActivityPin["version"] as String

// luil의 CMake가 세운 네이티브 라이브러리 자리다 (<build>/jniLibs/<예제>/<구성>/<abi>).
// 다른 빌드 디렉터리를 쓰면 -Pluil.buildDirectory=<path>로 준다.
val luilBuildDirectory = providers.gradleProperty("luil.buildDirectory")
    .getOrElse(rootDir.resolve("../../build/android-arm64").path)

// Vulkan 검증 레이어를 debug APK에 함께 싼다. 레이어가 든 디렉터리(<dir>/arm64-v8a/
// libVkLayer_khronos_validation.so)를 -Pluil.vulkanLayerDirectory=<dir>로 준다. 켜는 법은
// docs/concepts/rendering.md에 있다. 주지 않으면 싸지 않는다.
val vulkanLayerDirectory = providers.gradleProperty("luil.vulkanLayerDirectory").orNull

// 예제 하나가 APK 하나다. 앱 쪽 Java 코드가 없으므로 예제끼리 다른 것은 이름과 네이티브
// 라이브러리뿐이라, 모듈 하나에 flavor로 둔다. 네이티브 라이브러리 이름은 예제 CMake의
// target 이름이다 (examples/android/CMakeLists.txt).
val examples = mapOf(
    "hello" to "luil hello",
)

android {
    // R 클래스의 자리다. Java 코드가 없어 앱 id와 달라도 된다.
    namespace = "io.github.yuldk.luil.examples"
    compileSdk = 37
    // 패키징이 네이티브 라이브러리의 디버그 정보를 걷어 낼 때 쓴다. luil이 빌드에 쓰는 것과 같은 NDK다.
    ndkVersion = "27.3.13750724"

    defaultConfig {
        // Skia 패키지가 정한 하한이다 (ndk_api = 26).
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    flavorDimensions += "example"
    productFlavors {
        examples.forEach { (example, label) ->
            create(example) {
                dimension = "example"
                applicationId = "io.github.yuldk.luil.$example"
                manifestPlaceholders["libraryName"] = "luil_$example"
                manifestPlaceholders["label"] = label
            }
        }
    }

    // 예제마다 자기 라이브러리만 싼다 (helloDebug는 jniLibs/hello/Debug).
    sourceSets {
        examples.keys.forEach { example ->
            listOf("Debug", "Release").forEach { configuration ->
                maybeCreate("$example$configuration").jniLibs.directories.add("$luilBuildDirectory/jniLibs/$example/$configuration")
            }
        }
        if (vulkanLayerDirectory != null) {
            getByName("debug").jniLibs.directories.add(vulkanLayerDirectory)
        }
    }
}

dependencies {
    implementation("androidx.games:games-activity:$gameActivityVersion")
    // GameActivity가 AppCompatActivity라 AppCompat 테마가 필요하다.
    implementation("androidx.appcompat:appcompat:1.8.0")
}

// luil Android 예제의 APK 포장이다.
// 네이티브 라이브러리는 luil의 CMake가 세운다 (cmake --preset android-arm64). Gradle은 그것을
// GameActivity의 Java 절반과 함께 APK로 싸고 서명만 한다 (docs/concepts/build-system.md).
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

rootProject.name = "luil-examples"
include(":app")

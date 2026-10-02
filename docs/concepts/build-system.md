# 빌드 체계

luil의 [CMake 구성](../../CMakeLists.txt)은 준비된 의존성을 검사하며 configure 과정에서 다운로드하지 않는다. Skia와 WebView2 SDK는 각각 [`fetch_skia.ps1`](../../scripts/fetch_skia.ps1), [`fetch_webview2.ps1`](../../scripts/fetch_webview2.ps1)로 준비한다. 직접 빌드한 Skia도 필요한 헤더·라이브러리·GN 설정을 제공하면 사용할 수 있다.

## 요구 환경과 의존성

| 항목 | Windows | Android (core만) |
| --- | --- | --- |
| CMake | 4.2.0 이상 | 4.2.0 이상 |
| 플랫폼 | Windows x64 | arm64-v8a, API 26 이상 |
| 생성기 | Visual Studio 17 2022 또는 Visual Studio 18 2026 | Ninja Multi-Config |
| 컴파일러 | MSVC 19.40 이상 | NDK r27d의 clang |
| SDK·런타임 | Windows SDK 10.0.22621.0 이상 | NDK libc++ 정적판(`c++_static`) |
| 언어 | C++20 | C++20 |

[`dependencies.cmake`](../../cmake/dependencies.cmake)는 Skia와 nlohmann/json을 모든 라이브러리 구성에서 찾는다. HTTP를 호출하지 않는 앱에도 이 빌드 의존성은 필요하다. WebView2 SDK는 `LUIL_ENABLE_WEBVIEW`가 켜진 구성에서 찾는다. 끄면 SDK 준비·정적 로더 링크·고지 항목이 빠지고, 공개 API는 그대로이되 frame에 실은 웹뷰는 placeholder로만 남는다. 이 옵션의 기본값은 `LUIL_ENABLE_DIRECT3D`를 따른다. Catch2는 테스트를 켰을 때 필요하다.

Skia 위치는 `LUIL_SKIA_ROOT`, Debug·Release 산출물 위치는 `LUIL_SKIA_BUILD_DEBUG`와 `LUIL_SKIA_BUILD_RELEASE`로 지정한다. 검사는 필요한 정적 라이브러리와 Direct3D·JPEG·WebP·Wuffs·Rust PNG 디코더의 GN 설정을 확인하고, 그 산출물이 luil과 같은 계약으로 컴파일된 것인지도 확인한다. 컴파일러(clang-cl)·ABI(`is_trivial_abi = false`)·CRT(Debug `/MTd`, 그 밖 `/MT`)이며, 근거는 패키지가 적어 둔 `args.gn`과 `toolchain.json`이다. configure는 컴파일러를 찾지도 부르지도 않으므로 소비자에게 clang 설치가 필요하지 않다 ([Skia 빌드 준비](../skia-build.md)). WebView2 SDK 위치는 `LUIL_WEBVIEW2_ROOT`로 지정한다.

## 타깃과 옵션 전파

공개 라이브러리는 `luil`이며 `luil::luil` alias를 제공한다. 소비자는 이것 하나만 링크한다.

라이브러리는 두 static library로 나뉜다. `luil_core`(`luil::luil_core`)는 플랫폼을 모르는 층(messaging·text·theme·ui)이고, `luil`은 그 위의 플랫폼 조립(win32·net)이며 `luil_core`를 PUBLIC으로 링크한다. 경계를 target으로 긋는 이유는 다른 플랫폼에서 core만 세워 검증하기 위해서다 ([Android 이식 계획](../android-port-plan.md)). core의 test는 `luil_core_tests`라는 별도 실행 파일이고 core만 링크한다. core 소스가 플랫폼 헤더나 상위 계층(win32·net)을 include하지 않았는지는 `luil_core_portability` test가 include 줄을 읽어 확인한다. Windows에서는 그런 include가 있어도 빌드되기 때문이다.

`luil_usage`는 소비자가 공개 헤더를 컴파일하는 데 필요한 C++20과 MSVC `/utf-8`을 전파한다. 경고 수준·`/WX`·분석·Windows 매크로 설정을 담은 `luil_options`는 라이브러리와 저장소의 테스트·예제가 PRIVATE로 사용한다. 소비자가 `luil::luil`을 링크한다고 이 내부 경고 정책을 받지는 않는다.

최상위 빌드는 Skia의 `/MT`·`/MTd`와 맞는 정적 CRT를 설정한다. `add_subdirectory`로 포함되면 소비자의 CRT 설정을 유지하므로 소비자가 Skia와 일치시켜야 한다.

## 구성 선택

기본 빌드는 라이브러리를 만든다. `LUIL_BUILD_TESTS`, `LUIL_BUILD_EXAMPLES`, `LUIL_BUILD_TOOLING`, `LUIL_ENABLE_MSVC_ANALYZE`는 기본적으로 꺼져 있다. 테스트를 켜면 smoke 실행 파일에 필요한 예제도 함께 만든다. 설치 규칙을 켜는 `LUIL_INSTALL`은 최상위 프로젝트에서만 기본값이 켜진다. `LUIL_ENABLE_DIRECT3D`와 `LUIL_WARNINGS_AS_ERRORS`는 기본적으로 켜져 있다. 후자는 luil 자신의 소스에 거는 `/WX`이며, 핀으로 고정한 luil을 새 toolset이 경고할 때 소비자가 끌 수 있다.

`LUIL_ENABLE_DIRECT3D`를 끄면 Direct3D 렌더러 대신 stub을 컴파일해 CPU 렌더러만 남는다. luil에서 Ganesh를 부르는 파일은 그 렌더러 하나이므로, 링커가 Skia의 GPU 코드를 실행 파일에서 걷어 낸다. Skia 패키지와 GN 설정 검사는 그대로다. 동작은 [렌더링](rendering.md)에 있다.

`LUIL_ENABLE_WEBVIEW`의 기본값은 `LUIL_ENABLE_DIRECT3D`와 같다. 웹뷰는 Direct3D 렌더러의 합성 아래에만 서므로 CPU 전용 구성에서는 켜도 placeholder뿐이다. 캐시에 이미 값이 있는 빌드 디렉터리에서 Direct3D만 끄면 웹뷰는 켜진 채 남는다. configure가 이 조합을 경고하므로 그때 웹뷰도 명시적으로 끈다.

[프리셋](../../CMakePresets.json)은 일반·테스트·분석 구성을 제공한다.

```powershell
cmake --preset vs2026
cmake --preset vs2026-tests
cmake --preset vs2026-analysis
```

tooling을 요청해도 clang-format 또는 PowerShell이 없으면 관련 형식 검사 타깃을 경고와 함께 생략한다. 라이브러리·테스트·예제의 구성은 계속된다. 실행 파일의 리소스 통합은 [소비자 계약](consumer-contract.md)을 따른다.

## Android

Android에서는 플랫폼을 모르는 층(`luil_core`)과 그 test(`luil_core_tests`), GameActivity 위의 앱 host(`luil`), 예제를 세운다 ([Android 이식 계획](../android-port-plan.md)). 같은 CMake가 대상 플랫폼을 보고 갈린다. Windows에서는 RC 언어·Windows 검사·MSVC 옵션을, Android에서는 [`platform/android.cmake`](../../cmake/platform/android.cmake)의 검사와 [`compiler/clang.cmake`](../../cmake/compiler/clang.cmake)의 `-Wall -Wextra -Werror`를 쓴다. 설치·Direct3D·웹뷰는 Android 구성에서 꺼진다.

| 준비 | 값 |
| --- | --- |
| Android Studio | SDK와 JDK(JBR)를 준다. SDK Manager에서 platform `android-37`, Build-Tools 37, Command-line Tools, Platform-Tools를 받는다 |
| NDK | r27d (`27.3.13750724`). SDK 아래 `ndk/27.3.13750724`에 둔다. CMake preset은 `ANDROID_NDK_HOME`의 `build/cmake/android.toolchain.cmake`를 쓰고, Gradle은 `ndkVersion`으로 같은 것을 찾는다 |
| Ninja | PATH에 없으면 `CMAKE_MAKE_PROGRAM`으로 준다. Visual Studio에 딸린 것을 써도 된다 |
| Skia | `scripts\fetch_skia.ps1 -Target android-arm64`가 `third_party/skia-prep-android-arm64`에 푼다 |
| GameActivity | `scripts\fetch_game_activity.ps1`이 AAR의 헤더와 arm64-v8a 정적 라이브러리를 `third_party/game-activity-prep`에 푼다. Java 절반은 Gradle이 같은 판번을 받는다 |
| 기기 | adb로 연결한다. Android SDK(`ANDROID_HOME`, 없으면 `%LOCALAPPDATA%\Android\Sdk`)의 `platform-tools`를 PATH보다 먼저 찾는다. 여럿이면 `ANDROID_SERIAL`로 정한다 |

preset은 둘이다. `android-arm64-core`는 core와 그 test만, `android-arm64`는 앱 host와 예제까지 세운다.

```powershell
$env:ANDROID_NDK_HOME = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.3.13750724"
cmake --preset android-arm64 -DCMAKE_MAKE_PROGRAM="<ninja.exe>"
cmake --build --preset android-arm64-debug
ctest --preset android-arm64-debug
```

NDK 경로와 Ninja 위치는 사람마다 다르므로 저장소의 preset에 넣지 않는다. 자주 쓰면 Git에서 빠지는 `CMakeUserPresets.json`에 `environment`와 `cacheVariables`로 적어 둔다.

### 예제 APK

예제의 네이티브 절반은 luil의 CMake가 공유 라이브러리(`luil_<예제>`)로 세워 `<빌드>/jniLibs/<예제>/<구성>/arm64-v8a`에 둔다. [`examples/android`](../../examples/android/)의 Gradle 프로젝트는 그것을 GameActivity의 Java 절반과 함께 APK로 싸고 서명만 한다. 앱 쪽 Java 코드는 없다. 예제끼리는 이름과 네이티브 라이브러리만 다르므로 `app` 모듈 하나에 예제마다 flavor(`hello`, `widgets`)를 둔다. Gradle은 Android Studio의 JBR로 돌리며, 시스템의 `JAVA_HOME`을 바꿀 필요는 없다. `examples/android/local.properties`(Git에서 빠진다)에 `sdk.dir`을 적는다.

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
examples\android\gradlew.bat -p examples\android :app:assembleHelloDebug :app:assembleWidgetsDebug
adb install -r examples\android\app\build\outputs\apk\hello\debug\app-hello-debug.apk
adb shell am start -n io.github.yuldk.luil.hello/com.google.androidgamesdk.GameActivity
```

다른 빌드 디렉터리를 쓰면 `-Pluil.buildDirectory=<path>`로 준다. Vulkan 검증 레이어를 debug APK에 함께 싸려면 `-Pluil.vulkanLayerDirectory=<dir>`을 준다 ([렌더링](rendering.md#android의-vulkan)). 네이티브 라이브러리는 16KB 페이지로 정렬한다 (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`).

### 기기 test

test는 기기에서 돈다. `luil_core_tests`의 `CROSSCOMPILING_EMULATOR`가 [`adb_run.cmake`](../../cmake/android/adb_run.cmake)이고, CTest와 Catch2의 test 발견이 실행 파일을 부를 때마다 이 script가 실행 파일을 `/data/local/tmp/luil/<구성>`에 올려(`adb push --sync`, 바뀌었을 때만) 기기 셸에서 실행한다. Catch2가 목록을 쓰라고 준 호스트 경로(`--out`)는 기기 쪽 파일로 바꿔 실행하고 끝나면 당겨 온다. test 발견은 test 시점으로 미루므로(`DISCOVERY_MODE PRE_TEST`) 빌드할 때 기기가 꽂혀 있지 않아도 된다.

core test 실행 파일은 플랫폼 계층을 링크하지 않는다. core가 링크로 묶어 부르는 두 hook(`platform_font_source()`, `platform_fail_fast()`)은 [`core_platform_stub.cpp`](../../tests/core_platform_stub.cpp)가 test용으로 정의한다 (빈 글꼴 관리자, `abort`). 그래서 Windows와 Android가 같은 test를 같은 수만큼 돌린다.

# 빌드 체계

> 선택 이식 범위: 이 문서의 차트·마크업·업데이트·실행 중 스타일·핫 리로드 교차 참조는 원래 전체 통합 브랜치의 설계 기록이다. 현재 main의 채택 범위와 보존 위치는 [선택 기능 이식 기록](../selected-adoption.md)에 적었다.

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

[`dependencies.cmake`](../../cmake/dependencies.cmake)는 Skia와 nlohmann/json을 모든 라이브러리 구성에서 찾는다. HTTP를 호출하지 않는 앱에도 이 빌드 의존성은 필요하다. `luil_core`도 실행 중 스타일 파서 안쪽에서 nlohmann을 쓴다(`$<BUILD_INTERFACE:...>` PRIVATE, 공개 헤더에는 nlohmann 형이 없다). WebView2 SDK는 `LUIL_ENABLE_WEBVIEW`가 켜진 구성에서 찾는다. 끄면 SDK 준비·정적 로더 링크·고지 항목이 빠지고, 공개 API는 그대로이되 frame에 실은 웹뷰는 placeholder로만 남는다. 이 옵션의 기본값은 `LUIL_ENABLE_DIRECT3D`를 따른다. Catch2는 테스트를 켰을 때 필요하다.

libsmb2는 `LUIL_ENABLE_SMB`를 켠 Android 구성에서만 찾는다. **기본은 끔이다.** submodule(`third_party/libsmb2`, `libsmb2-6.2`에 고정)이고, upstream의 CMakeLists 대신 [`dependencies/libsmb2.cmake`](../../cmake/dependencies/libsmb2.cmake)가 같은 소스 목록과 `config.h`로 공유 라이브러리 `libsmb2.so`(target `luil_smb2`)를 세운다. upstream의 CMakeLists는 디렉터리 전체에 정의를 걸고 `BUILD_SHARED_LIBS`를 캐시에 세워 luil의 target까지 번지기 때문이다. libsmb2는 C라 켠 구성에서만 C 언어를 켠다. Windows에서 켜면 configure가 멈춘다. LGPL-2.1이라 앱 라이브러리에 섞지 않고 따로 싸는 것은 [소비자 계약](consumer-contract.md#smb)이 정한다.

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

`LUIL_BUILD_CHART`는 선택 라이브러리 `luil_chart`([차트 라이브러리 · 보존](../selected-adoption.md#구현-완료-후-보존한-항목))를 짓는다. core 밖에 있어 끄면 `chart/`를 읽지도 않는다. 기본은 끄고, 예제나 테스트를 켠 구성에서는 켬을 따른다. 데모와 그 테스트가 이 라이브러리를 쓰므로 예제나 테스트를 켜고 끄면 configure가 원인과 함께 멈춘다. 캐시에 끈 값이 남은 빌드 디렉터리에서 예제만 켜도 같다.

[프리셋](../../CMakePresets.json)은 일반·테스트·분석 구성을 제공한다.

```powershell
cmake --preset vs2026
cmake --preset vs2026-tests
cmake --preset vs2026-analysis
```

tooling을 켜면 소스 스타일 검사 타깃 `luil_style_check`가 생긴다. 검사는 돌 때마다 PATH에서 PowerShell을 찾으므로 구성 단계는 PowerShell이 없어도 계속된다. clang-format은 검사하지 않는다. 손으로 고른 줄바꿈을 formatter가 다시 채우기 때문이다. `.clang-format`은 줄 길이 제한 없이 편집기에서 고른 범위에 쓰는 선택 도구다. 실행 파일의 리소스 통합은 [소비자 계약](consumer-contract.md)을 따른다.

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

예제의 네이티브 절반은 luil의 CMake가 공유 라이브러리(`luil_<예제>`)로 세워 `<빌드>/jniLibs/<예제>/<구성>/arm64-v8a`에 둔다. [`examples/android`](../../examples/android/)의 Gradle 프로젝트는 그것을 GameActivity의 Java 절반과 함께 APK로 싸고 서명만 한다. 앱 쪽 Java 코드는 없다. 예제끼리는 이름과 네이티브 라이브러리만 다르므로 `app` 모듈 하나에 예제마다 flavor(`hello`, `widgets`, `mobile_demo`)를 둔다. Gradle은 Android Studio의 JBR로 돌리며, 시스템의 `JAVA_HOME`을 바꿀 필요는 없다. `examples/android/local.properties`(Git에서 빠진다)에 `sdk.dir`을 적는다.

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
examples\android\gradlew.bat -p examples\android :app:assembleHelloDebug :app:assembleWidgetsDebug
adb install -r examples\android\app\build\outputs\apk\hello\debug\app-hello-debug.apk
adb shell am start -n io.github.yuldk.luil.hello/com.google.androidgamesdk.GameActivity
```

다른 빌드 디렉터리를 쓰면 `-Pluil.buildDirectory=<path>`로 준다. Vulkan 검증 레이어를 debug APK에 함께 싸려면 `-Pluil.vulkanLayerDirectory=<dir>`을 준다 ([렌더링](rendering.md#android의-vulkan)). 네이티브 라이브러리는 16KB 페이지로 정렬한다 (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`).

### 기기 test

test는 기기에서 돈다. `luil_core_tests`의 `CROSSCOMPILING_EMULATOR`가 [`adb_run.cmake`](../../cmake/android/adb_run.cmake)이고, CTest와 Catch2의 test 발견이 실행 파일을 부를 때마다 이 script가 실행 파일을 `/data/local/tmp/luil/<구성>`에 올려(`adb push --sync`, 바뀌었을 때만) 기기 셸에서 실행한다. Catch2가 목록을 쓰라고 준 호스트 경로(`--out`)는 기기 쪽 파일로 바꿔 실행하고 끝나면 당겨 온다. test 발견은 test 시점으로 미루므로(`DISCOVERY_MODE PRE_TEST`) 빌드할 때 기기가 꽂혀 있지 않아도 된다.

HTTP test(`luil_net_tests`)는 JNI로 `java.net`을 부르므로 JVM 안에서 돈다. 공유 라이브러리로 세우고, 같은 script가 `JVM_DEX`를 받으면 라이브러리와 dex를 함께 올려 `app_process`로 띄운다. dex는 [`JvmTestMain.java`](../../tests/android/JvmTestMain.java)를 Android Studio의 JBR `javac`(`JAVA_HOME`, 없으면 Android Studio 자리)로 옮기고 SDK build-tools의 `d8`로 짓는다. 그래서 Android test 구성에는 JBR과 build-tools가 있어야 한다.

core test 실행 파일은 플랫폼 계층을 링크하지 않는다. core가 링크로 묶어 부르는 두 hook(`platform_font_source()`, `platform_fail_fast()`)은 [`core_platform_stub.cpp`](../../tests/core_platform_stub.cpp)가 test용으로 정의한다 (빈 글꼴 관리자, `abort`). 그래서 Windows와 Android가 같은 test를 같은 수만큼 돌린다.

## macOS

플랫폼을 모르는 층(`luil_core`)과 그 test, AppKit 위의 앱 host(`luil`), 예제 둘(`luil_hello`, `luil_demo`)을 세운다. 설치는 아직 없다.

| 항목 | 값 |
| --- | --- |
| 플랫폼 | `LUIL_MACOS_ARCHITECTURE`는 arm64 하나다 (패키지가 그것만 담는다). 최소 판 `LUIL_MINIMUM_MACOS_VERSION`은 패키지의 배포 하한과 같은 11.0이다 |
| 생성기 | 가리지 않는다. preset은 Ninja Multi-Config |
| 컴파일러 | Xcode의 clang. 앱 host는 Objective-C++(`OBJCXX`, ARC)다 |
| Skia | 대상 이름 `mac-arm64`, 자리 `third_party/skia-prep-mac-arm64` ([Skia 빌드 준비](../skia-build.md#macos-패키지와-링크-계약)) |

[`platform/macos.cmake`](../../cmake/platform/macos.cmake)가 아키텍처와 `CMAKE_OSX_DEPLOYMENT_TARGET`을 검사한다. [`compiler/clang.cmake`](../../cmake/compiler/clang.cmake)의 `-Wall -Wextra -Werror`는 컴파일러 ID `Clang`과 `AppleClang`, 언어 `CXX`와 `OBJCXX`에 모두 건다. Xcode의 clang은 ID가 `AppleClang`이라 하나만 적으면 경고 규율이 오류 없이 빠진다. Apple의 `AssertMacros.h`가 `check`·`verify`·`require`를 매크로로 덮지 않게 `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0`도 건다. 공개 헤더에 `check`라는 이름이 있어서다.

최소 판은 11이라 libc++의 부동소수점 `to_chars`(13.3부터)를 쓸 수 없다. chrono 값의 `operator<<`와 실수 `std::format`이 그것을 부르므로, test가 날짜·요일을 Catch2로 적을 때는 [`chrono_string_maker.h`](../../tests/chrono_string_maker.h)를 거친다.

preset은 둘이다. `macos-arm64-core`는 core·앱 host 라이브러리·core test를, `macos-arm64`는 예제와 그 smoke test까지 세운다.

```sh
pwsh -Command "& ./scripts/fetch_skia.ps1 -Target mac-arm64 -Configuration Debug,Release"
cmake --preset macos-arm64
cmake --build --preset macos-arm64-debug
ctest --preset macos-arm64-debug
build/macos-arm64/Debug/luil_demo
```

test는 빌드한 기계에서 그대로 돈다. Windows 프리셋은 Windows 호스트에서만, 이 프리셋은 macOS 호스트에서만 보인다 (`condition`). 키 변환(`src/macos/macos_input.cpp`)은 AppKit을 모르는 순수 층이라 core test에 들어가고, 예제의 `--smoke-test`가 창을 보이지 않은 채 첫 frame을 그려 host를 확인한다.

### 앱 host

진입점은 [`luil::macos::run_application`](../../include/luil/macos/macos_app.h)이다. 메인 thread가 luil의 UI thread이고, 앱 delegate는 Android처럼 플랫폼 중립 부분(`app_delegate`)만 불린다 — Windows의 `window_delegate`를 구현한 앱 delegate를 그대로 넘긴다.

| 경계 | macOS |
| --- | --- |
| 실행 루프와 깨우기 | `NSApplication`의 run loop. 다른 thread의 깨우기는 `CFRunLoopPerformBlock`(공통 모드)이라 창 크기를 끄는 동안에도 돈다 |
| 창과 표면 | 창 하나. 제목 막대는 시스템의 것이라 `ui_platform::window_caption`이 거짓이고 `caption_element`는 자리를 차지하지 않는다 |
| 렌더러 | Metal(Ganesh)이 먼저다. view layer의 자식 `CAMetalLayer`에 그리고, 만들거나 그리다 실패하면 CPU(Skia raster → CGImage layer 내용)로 물러선다. 한 번 물러서면 다시 쓰지 않는다. `gpu`·`direct3d`는 Metal을 요구한다. 보이지 않는 창(최소화·완전히 가림)에는 그리지 않는다 |
| 배율 | 창이 놓인 화면의 `backingScaleFactor`(레티나 2) |
| 글꼴 | `SkFontMgr_New_CoreText`, 기본 UI 서체는 시스템 서체(`.AppleSystemUIFont`). codicon은 라이브러리에 싣는다 |
| 입력 | 마우스·트랙패드·키보드. Command가 주 단축키, Option이 낱말 이동이고 Command+화살표는 줄·글의 처음과 끝이다. Control+클릭은 오른쪽 클릭이다 |
| 텍스트 입력 | `NSTextInputClient`. 조합 글은 `post_composition`으로 보이고 확정은 insert 편집이다 (한글 입력기) |
| popup | 창 위에 겹쳐 그린다 (Android와 같은 overlay) |
| 웹뷰 | WKWebView. Skia view의 형제 NSView로 위층·아래층에 선다 ([웹뷰 합성](../webview-composition-design.md#macos-wkwebview)) |
| 클립보드 | 일반 붙여넣기 판의 글과 HTML |
| 시스템 외양 | 밝은·어두운 모양, 강조색, 대비·움직임·투명도 줄이기를 따른다 |

아직 없는 것은 "지원 안 함"으로 답한다: 보조 창·도킹 표면(무시, 그 창의 웹뷰도 서지 않는다), 파일 대화 상자, 비밀 저장소(Keychain), OS 알림, 파일 감시, HTTP·WebSocket 전송, 단일 인스턴스 인자 전달, OS 파일 끌기. 데스크톱 demo는 이 자리를 페이지마다 실패 글로 보인다.

개발용 환경 변수 둘이 있다. 화면 기록·손쉬운 사용 권한 없이 host를 확인하는 길이다.

| 변수 | 뜻 |
| --- | --- |
| `LUIL_MACOS_FRAME_CAPTURE=<png>` | 그린 frame마다 그 파일을 덮어쓴다 |
| `LUIL_MACOS_INPUT_SCRIPT=<file>` | 창이 뜬 뒤 대본의 클릭·키·입력기 조합을 앱 자신의 이벤트 큐에 넣는다. `snapshot`은 웹뷰까지 합성한 창 내용을 PNG로 적는다 (명령은 [`input_script.h`](../../src/macos/input_script.h)) |

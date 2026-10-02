# Android 이식 계획

luil을 Android(arm64-v8a)에서 돌게 만드는 작업의 단계 계획이다. 나중에 iOS가 같은 경계에 들어올
수 있는 구조를 함께 정한다. 구현은 단계마다 승인을 받은 뒤에 시작한다.

**모든 단계의 공통 조건은 "Windows 동작이 그대로"다.** 단계의 모든 커밋에서 다음이 통과해야 한다.

- `vs2026-tests` 구성의 Release·Debug CTest 전부 (smoke 5개 포함)
- `LUIL_ENABLE_DIRECT3D=OFF`로 구성한 CPU 전용 빌드의 CTest
- 예제(`luil_hello`, `luil_widgets`, `luil_demo`)의 소스를 고치지 않은 빌드
- `luil_installed_consumer`(설치본 소비자)와 `luil_source_style`

## 기준선

2026-10-02에 `c676bca`에서 측정했다. Visual Studio 2026, `vs2026-tests` preset이다.

| 구성 | 결과 |
| --- | --- |
| Release | 799개 중 799개 통과 (smoke 5, asan 19, package 1 포함), 58초 |
| Debug | 799개 중 780개를 실행해 모두 통과했다. 나머지 19개는 asan으로, Debug에서 꺼 두도록 설계되어 있다. 70초 |

이 숫자가 각 단계 완료 판정의 비교 기준이다. 테스트 수는 단계에서 늘 수 있지만 줄어서는
안 된다. 줄어든다면 테스트를 다른 실행 파일로 옮긴 것이어야 하고, 합계가 같아야 한다.

## 사실 대조

skia-prep 세션에서 정리한 사실과 이 저장소의 조사 결과를 코드와 맞대 보았다. 어긋나는 것은
코드와 skia-prep 문서 쪽을 따랐다.

### skia-prep

| 정리된 사실 | 확인 결과 |
| --- | --- |
| `android-arm64` r1, Skia 152 `0873ec164a06`, rust png, NDK r27d, `ndk_api = 26` | 맞다. NDK는 `27.3.13750724`다. |
| 릴리스 태그 `skia-152-0873ec164a06-android-arm64-r1` | 맞다. 이미 공개되었고, 자산 두 개의 sha256·크기가 핀 초안과 같다. |
| 핀 초안은 `target`과 `toolchain`만 다르다 | 형식(schema 2, 키 순서)은 같다. 값은 `tag`, `asset_base_url`, `package_revision`(1), 자산의 `file`·`size`·`sha256`도 다르다. skia-prep의 스크립트에서 이 초안 파일을 만드는 곳은 찾지 못했다. |
| 패키지 배치 | 맞다. `LICENSE`와 `licenses/rust/`가 더 있다. |
| `.a` 15개 | 맞다. 이름도 같다. |
| Android args | 맞다. Ganesh는 명시하지 않았고 Skia 기본값으로 켜진다. |
| `toolchain.json`의 `compiler`는 `"clang"` | 맞다. 같은 파일의 `toolchain` 필드는 `"ndk"`다. luil의 검사는 `compiler`만 읽으므로 그대로 둔다. |
| 소비자 링크 계약 1–7이 skia-prep `docs/skia-build.md` 8.5에 있다 | 8.5에는 다섯 항목(중복 정의 허용, `-landroid -llog`, 할당기, NDK Vulkan 헤더, FreeType 표기)이 있다. minSdk 26은 8.4 표에 있다. **`c++_static`은 문서에 없다.** 근거는 `tools/android_probe.cpp`의 빌드 주석에 적힌 `-static-libstdc++`뿐이다. |
| 할당기 `skgpu::VulkanMemoryAllocators::Make(const VulkanBackendContext&, ThreadSafe)` | 8.5는 내부 헤더(`VulkanMemoryAllocatorPriv.h`)에만 선언된다고 적고, 서명은 probe(65–72행)에 있다. |
| `android_probe.cpp` 네 가지 확인 | 맞다. Vulkan은 `dlopen("libvulkan.so")`로 열고, 확장 없이 Vulkan 1.1 장치를 만든다. |

skia-prep 쪽에서 고칠 것이 하나 있다. `tools/android_probe.cpp` 25–26행의 줄 끝이 `\` 줄 잇기가
아니라 글자 그대로 `\r`이어서, 주석의 빌드 명령을 그대로 붙이면 깨진다. 이 저장소의 작업이
아니므로 여기서는 고치지 않는다.

### luil

| 조사 결과 | 확인 결과 |
| --- | --- |
| `src/win32/` 약 60개 파일, .cpp 약 12,000줄 | 맞다. 60개(.cpp 32, .h 27, `.rc.in` 1)이고, .cpp는 11,960줄, .h는 2,900줄이다. |
| `src/ui`, `src/text`, `src/theme`, messaging 약 11,000줄, Win32 헤더 없음 | 맞다. 합쳐서 약 11,670줄이다. `src/net`은 WinHTTP·`windows.h`에 묶여 있다. |
| `platform_key_code`가 Win32 VK 값을 들고 있다 | 일부만 맞다. `78e1ed0`부터 영문자·숫자는 이름 있는 `key_code`(`key_a`…`key_9`)로 간다. 나머지 키만 `first_platform_key(0x1000) + VK`이고, 이식 가능한 층에서 VK 값을 비교하는 곳은 없다. |
| host API 헤더에 Win32 타입이 없다 | 타입은 없다. 다만 네임스페이스가 `luil::win32`이고, `app_host.h`가 `luil/win32/webview.h`를 include한다. |
| 더블클릭 시간과 시스템 색은 플랫폼이 넣는다 | 더블클릭 시간은 `interaction_config`로 들어간다. 시스템 색·고대비·밝은 모드·accent로 **팔레트를 고르는 일 자체가 Win32 쪽**(`frame_state.cpp`, `win32_window.cpp`)에 있다. |
| `fonts.h`의 구현이 win32 쪽에 있다 | 맞다. `SkFontMgr_New_DirectWrite` 호출이 네 곳(캐시 하나, 매번 새로 만드는 곳 셋)에 있다. |
| `fetch_skia.ps1`이 `target`을 핀 하나와 맞댄다 | 맞다. 핀과 설치된 `VERSION.json`을 다섯 필드로 맞대고, 산출 디렉터리 이름은 `out\skia-ui-<구성>`으로 고정이다. **CMake는 핀을 읽지 않는다.** 각 구성 폴더의 `args.gn`과 `toolchain.json`만 검사한다. |
| Skia 설정이 Windows 전용으로 고정 | 맞다. 산출물 목록에 `spirv_cross.lib`, `d3d12allocator.lib`가 있고, 시스템 라이브러리 `d3dcompiler`, `FontSub`, `Usp10`도 더 링크된다. |
| 테스트 73개 중 43개가 Win32를 참조하지 않는다 | 73개 중 44개다(보조 파일 `raster_probe.cpp` 포함). 다만 모든 테스트가 Win32 라이브러리 전체와 `ws2_32`를 링크하는 실행 파일 하나(`luil_tests`)에 들어 있다. Win32를 참조하는 29개 중 다수(`dpi_scale`, `fence_wait`, `popup_reconcile`, `webview_layout`, `frame_state`)는 순수 계산이다. |

계획에 영향을 주는 새 사실들이다.

- **`app_host.cpp`가 Win32에 두 군데 기대고 있다.** `win32/win32_fonts.h`의 `configured_ui_typeface()`로
  `measure_text` 기본값을 잡고, MSVC 전용 `__fastfail`(`<intrin.h>`)로 멈춘다.
- **한 frame을 그리는 함수 `draw_frame(SkCanvas&, ...)`는 이미 이식 가능하다.** Win32 헤더가 없고,
  자리만 `src/win32/frame_state.cpp`다.
- **popup은 언제나 별도 HWND다.** 같은 표면 안의 overlay 모드는 없다. 같은 표면 안에 그리는
  선례로 `modal_host_element`, tooltip, 끌기 표시가 있다.
- **길게 누르기는 뗄 때 판정한다** ([터치 제스처와 펜 입력](touch-pen-input-design.md)). Android
  사용자는 누르고 있는 동안 메뉴가 열리기를 기대한다.
- `character_typed_event`에 `surface` 필드가 없고, Backspace는 U+0008 문자로 들어온다. 휠은
  `WHEEL_DELTA`(120) 단위다. Android 입력도 이 값으로 옮겨야 한다.
- 입력 thread도 텍스트를 재느라 글꼴을 쓴다. 글꼴 저장소는 mutex로 지키며, thread 사이에서
  공유된다.
- 이 PC의 도구 상황이다. Java, Gradle, Android SDK, Windows용 NDK가 없다. Ninja는 Visual Studio에
  딸린 것만 있다. Ubuntu WSL에는 NDK r27d만 있고 cmake가 없다. adb로 기기가 보인다.

## 목표 구조

### 층

```text
include/luil/{ui,text,theme,messaging}  이식 가능한 공개 API (지금 그대로)
include/luil/app/                       플랫폼 중립 host API (app_host, ui_frame, logic_driver, ...)
include/luil/win32/                     Win32 진입점 (run_application_window, window_config)
include/luil/android/                   Android 진입점 (3단계)
include/luil/ios/                       iOS 진입점 (나중)

src/{ui,text,theme}                     이식 가능한 층 (지금 그대로)
src/host/                               플랫폼이 함께 쓰는 host 구현
src/win32/                              Win32 플랫폼
src/android/                            Android 플랫폼
```

`src/host/`에 들어갈 것은 플랫폼마다 다시 쓰지 않을 코드다. `app_host`, `frame_state`와
`draw_frame`, 테마 해석, 렌더러 실패 전환 정책(`renderer_host`), popup 조정, 다시 그리기 예약,
글꼴 캐시 논리, 포인터 시퀀스 추적, fence 대기 예산이 여기에 든다.

**창과 표면은 하나의 가상 클래스로 추상하지 않는다.** Win32는 창 여러 개, 비클라이언트 caption,
popup마다 HWND를 쓴다. 모바일은 표면 하나에 수명 주기 이벤트가 있다. 둘을 같은 인터페이스에
넣으면 어느 쪽에도 맞지 않는다. 대신 플랫폼은 진입점 하나와 UI thread를 갖고, 공유 코드를
가져다 쓰며, 이식 가능한 층이 요구하는 서비스만 인터페이스로 구현한다.

### CMake 대상

| 대상 | 내용 | 링크 |
| --- | --- | --- |
| `luil_core` (STATIC, 새로) | ui, text, theme, messaging, host | Skia, nlohmann_json |
| `luil` (STATIC, 지금 이름) | 플랫폼 소스와 net | `luil_core` PUBLIC, 플랫폼 시스템 라이브러리 |

소비자가 쓰는 `luil::luil`의 이름과 역할은 그대로다. 설치본 export에 `luil_core`가 더해진다.

### 플랫폼 경계

| 경계 | 지금 | 정할 모양 | 구현 단계 | iOS |
| --- | --- | --- | --- | --- |
| 실행 루프와 깨우기 | `app_host::wake_signals`(std::function), Win32는 `PostMessageW` | 그대로 | 3 | `CFRunLoopSource` |
| 창과 표면 | `window_surface`(HWND) | 플랫폼 내부. 공유 코드는 `frame_state` 조립 | 3 | `UIView` |
| 렌더러 | `skia_renderer`(HWND, `IDCompositionVisual*`가 서명에 있음) | 인터페이스는 `backend/resize/render`만 남긴다. 네이티브 핸들은 플랫폼별 생성 함수만 받는다. `underlay`는 Win32 확장으로 옮긴다. | 1, 3, 4 | `CAMetalLayer` + Ganesh Metal |
| 글꼴 공급 | `win32_fonts.cpp`의 free function, `registry_font_resolver` | `font_source` 인터페이스(관리자 생성, UI 서체 해석, 문자별 fallback, 사용자 언어). 캐시와 mutex는 core에 둔다. | 1, 3 | `SkFontMgr_New_CoreText` |
| 자산 읽기 | `FindResourceW` (codicon, 고지) | `asset_reader`. 바이트 배열을 돌려준다. | 1, 3 | `NSBundle` |
| 시스템 외양 | `win32_window.cpp`가 읽고 팔레트까지 고른다 | `system_appearance` 값(밝은 모드 선호, 고대비와 그 색, accent, 텍스트 품질). 해석은 core로 옮긴다. | 1 | `UITraitCollection` |
| 더블클릭 시간 | `interaction_config::double_click_time` | 그대로 | 3 | 상수 |
| 배율 | `dpi / 96` | 플랫폼이 정한다. Android는 `density / 160`이라 논리 픽셀 1이 1dp다. | 3 | `contentScaleFactor` |
| 타이머 | `next_update`·`next_tick` 계약, Win32는 `SetTimer` | 계약 그대로. Android는 `ALooper_pollOnce`의 시간 제한으로 건다. | 3 | `CADisplayLink`, 타이머 |
| 커서 | `window_config::resolve_cursor` | 그대로. Android는 처음에 무시한다. | 7 | iPadOS 포인터 |
| 클립보드 | 요청 variant와 깨우기는 이식 가능하다. 실행은 Win32 직접 호출이다. | 플랫폼이 UI thread에서 실행한다. 인터페이스는 필요 없다. | 6 | `UIPasteboard` |
| 텍스트 입력 | `tsf_host`(RECT를 씀, src/win32) | 이식 가능한 부분을 `text_input_host`로 올린다. 초점 대상, 확정 문서, 표면 좌표의 범위 사각형, 조합·편집 게시가 들어간다. | 6 | `UITextInput` |
| 키 코드 | 이름 있는 키 + `first_platform_key + VK` | 아래 "키 코드" | 1, 5 | `UIKeyboardHIDUsage` |
| 플랫폼 능력 | 없다 | `platform_info`(여러 창, caption, popup 방식, 웹뷰, 호버, 안전 영역 inset)를 metrics 메시지와 함께 앱에 알린다 | 3 | 같은 구조 |

**키 코드.** 영문자·숫자는 이미 이름 있는 키라 플랫폼 사이에서 같다. 그 밖의 키는
`first_platform_key + 네이티브 코드`로 두고, 이 값은 **플랫폼마다 뜻이 다른 불투명 값**이라고
문서에 적는다. 바이너리 하나는 플랫폼 하나에서만 돌기 때문에 값이 겹쳐도 문제가 없다.

- Android는 `AKEYCODE_*`를 이름 있는 키로 먼저 옮기고, 나머지를 `first_platform_key + AKEYCODE`로
  보낸다.
- 공개 헤더의 `alphanumeric_key_code(vk)`와 `platform_key_code(vk)`는 VK가 ASCII라는 가정을 담고
  있다. 이 둘은 Win32 변환 쪽으로 옮기고, 공개 헤더에는 플랫폼 중립 생성 함수만 남긴다.
- Backspace는 Win32와 같게 `character_typed_event{0x08}`로 보낸다. `key_code::backspace`는 더하지
  않는다. 더하면 Windows 동작이 바뀐다.

**iOS가 같은 자리에 들어오는지.** 위 표의 iOS 열이 모두 채워진다. 다른 점이 하나 있다. UIKit은
메인 thread에서만 불러야 하므로, iOS의 luil UI thread는 메인 thread다. Android에서는
`android_main` thread다. luil의 UI thread는 이미 "호출자 thread, 이벤트 구동"이라 둘 다 받아들인다.

## 단계

사용자 초안의 순서를 따르되 다음을 바꿨다. 이유는 "순서를 바꾼 이유"에 있다.

| 단계 | 내용 | 기기 필요 |
| --- | --- | --- |
| 1 | 이식 층 분리 (Windows만) | 아니다 |
| 2 | Android 빌드와 기기 테스트 (이식 층만) | adb |
| 3 | 최소 APK, 앱 호스트 뼈대, 수명 주기, CPU 그리기, 글꼴·자산 | 화면 |
| 4 | Vulkan 렌더러 | 화면 |
| 5 | 터치·펜·키보드·마우스 입력 | 화면 |
| 6 | 텍스트 입력(IME)과 클립보드 | 화면 |
| 7 | popup overlay, 빼는 것과 대체할 것 | 화면 |
| 8 | 예제, 패키징 마무리, 서명, 소비자 계약 | 화면 |
| 9 | iOS를 위한 자리 (문서만) | 아니다 |

### 1단계 — 이식 층 분리

**목표.** Windows 빌드만으로 이식 층과 Win32를 대상(target) 단위로 가른다. 이 단계가 끝나면
`luil_core`에 Win32 헤더도 Win32 시스템 라이브러리도 없고, 그 사실을 CTest가 지킨다.

1. `luil_core` 대상을 세우고, 테스트를 `luil_core_tests`(이식 가능한 44개)와 `luil_tests`
   (나머지)로 나눈다. core 소스가 Win32 헤더를 include하지 않는지 보는 검사 테스트를 더한다.
2. 순수 계산 파일을 `src/host/`로 옮긴다. 후보는 `frame_state`, `popup_reconcile`,
   `surface_invalidate`, `dpi_scale.h`, `fence_wait.h`, `caption_layout`, `webview_layout`,
   `window_mode`다. 옮기기 전에 파일마다 Win32 의존을 다시 본다. `caption_layout_tests`는
   `caption_surface.h`(HWND)도 include하므로 테스트를 나눈다.
3. host API를 플랫폼 중립 경로·네임스페이스로 옮긴다 (결정 1). `ui_webview`와 `renderer_mode`도
   함께 옮겨, 중립 헤더가 `luil/win32/`를 include하지 않게 한다.
4. `app_host.cpp`의 두 의존을 끊는다. `__fastfail`은 이식 가능한 fail-fast 함수로 바꾼다.
   `measure_text` 기본값은 `font_source`를 거친다.
5. `font_source`, `asset_reader`, `system_appearance`를 정의한다. Win32 구현은 지금 코드를
   감싼다. 테마·팔레트 해석을 core로 옮긴다.
6. `skia_renderer`와 `renderer_host`의 서명에서 HWND와 `IDCompositionVisual*`를 빼고, 실패 전환
   정책을 `src/host/`로 옮긴다.
7. `pointer_sample::message`(`WM_POINTER*` 값)를 플랫폼 중립 단계 값으로 바꾸고,
   `pointer_sequence_tracker`를 `src/host/`로 옮긴다. Android가 같은 추적기를 쓴다.
8. 개념 문서를 고친다: [스레드 모델](concepts/threading-model.md), [렌더링](concepts/rendering.md),
   [빌드 체계](concepts/build-system.md), [소비자 계약](concepts/consumer-contract.md).

**손댈 파일.** `CMakeLists.txt`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `cmake/install.cmake`,
`cmake/luil-config.cmake.in`, `include/luil/luil.h`, `include/luil/win32/*`, 새 `include/luil/app/*`,
`include/luil/ui/ui_events.h`, `src/win32/{app_host,frame_state,skia_renderer,cpu_skia_renderer,direct3d_skia_renderer,window_surface,win32_window,win32_fonts,typeface_loading,embedded_assets,surface_input}.*`,
새 `src/host/*`, 테스트 중 include 경로가 바뀌는 것, 개념 문서들.

**완료 판정.**

- 공통 조건을 모두 통과한다. 테스트 합계가 799 이상이다.
- `luil_core`의 링크 목록에 Win32 시스템 라이브러리가 없고, core 소스 검사 테스트가 통과한다.
- 예제와 `tests/package_consumer`가 소스를 고치지 않고 빌드된다 (결정 1이 (a)나 (b)일 때).
- `luil_smoke_*` 5개가 지금과 같은 결과를 낸다.

**위험.**

- 옮긴 파일에 숨은 Win32 의존이 있을 수 있다. 검사 테스트가 잡게 하고, 옮기기를 작은 커밋으로
  나눈다.
- 네임스페이스 별칭(`using`)은 소비자의 전방 선언(`namespace luil::win32 { class logic_driver; }`)을
  깬다. 흔하지 않지만 깨는 변경이다.
- 테마 해석을 옮기다 고대비·accent 색이 미세하게 달라질 수 있다. `ui_style`·`raster_draw` 테스트가
  지키지만, 고대비는 앱을 띄워 눈으로 확인한다.
- 커밋 수가 많아진다. 하위 단계(위 1–8)마다 공통 조건을 돌린다.

### 2단계 — Android 빌드와 기기 테스트

**목표.** NDK toolchain으로 configure해 `luil_core`와 `luil_core_tests`를 세우고, CTest로 기기에서
돌린다. 여기서는 APK가 필요 없다. 테스트 실행 파일을 `/data/local/tmp`에 올려 `adb shell`로 돌린다.
skia-prep의 `android_probe`와 같은 방식이다.

1. `project(... LANGUAGES CXX)`로 바꾸고 RC는 Windows에서만 `enable_language(RC)` 한다.
2. 플랫폼 검사를 가른다. `cmake/platform/android.cmake`는 `CMAKE_SYSTEM_NAME`이 Android, ABI가
   `arm64-v8a`, `ANDROID_PLATFORM`이 26 이상, STL이 `c++_static`인지 본다. 컴파일러 옵션은
   `cmake/compiler/clang.cmake`로 둔다 (`-Wall -Wextra -Werror` 수준. MSVC `/W4 /WX`와 맞춘다).
3. Skia 핀을 대상별로 둔다.
   - `third_party/skia-prep.json`은 win-x64 핀으로 남긴다. CI와 기존 소비자가 이 경로를 쓴다.
   - `third_party/skia-prep-android-arm64.json`을 새로 둔다.
   - `fetch_skia.ps1 -Target <win-x64|android-arm64>`를 만든다. 기본값은 win-x64다. Android는
     `third_party/skia-prep-android-arm64/`에 푼다.
4. `skia.cmake`를 대상별 표로 가른다.

   | 항목 | win-x64 | android-arm64 |
   | --- | --- | --- |
   | 산출물 | 지금의 .lib 12개 | .a 15개 |
   | 필수 인자 | `skia_use_direct3d`, 코덱 | `skia_use_vulkan`, `skia_use_freetype`, `skia_use_expat`, 코덱 |
   | 컴파일러 | `clang-cl` | `clang` |
   | ABI | `is_trivial_abi = false` | 같다 |
   | CRT | `/MT`·`/MTd` 검사 | 검사하지 않는다. `args.gn`의 `ndk_api`가 소비자의 `ANDROID_PLATFORM` 이하인지 본다. |
   | 시스템 라이브러리 | d3dcompiler, FontSub, Usp10, ws2_32, userenv, ntdll | android, log (libm·libdl·libc는 기본) |
   | 링크 옵션 | 없다 | `-Wl,--allow-multiple-definition`, libskia.a를 맨 앞에 |

5. 링크 순서를 보장한다. `target_link_libraries`의 순서만으로는 CMake가 imported 대상의 순서를
   지키지 않을 수 있다. `$<LINK_GROUP:RESCAN,...>`로 묶고 `luil_skia_skia`를 맨 앞에 둔다. 실제
   링크 명령을 확인해 고정한다.
6. 테스트를 기기에서 돌린다 (결정 5). `CMAKE_CROSSCOMPILING_EMULATOR`에 adb 래퍼 스크립트를
   준다. `catch_discover_tests`는 `DISCOVERY_MODE PRE_TEST`로 두어 빌드할 때 기기가 없어도 된다.
7. Windows 전용 테스트 설정을 Android에서 끈다: `ws2_32`, `/MANIFEST:NO`, asan 실행 파일,
   `luil_installed_consumer`의 `-G`/`-A`. `find_program(pwsh REQUIRED)`는 그대로 둘 수 있다.
   configure가 Windows에서 돌기 때문이다.
8. preset을 더한다: `android-arm64-core`(Ninja, NDK toolchain 파일, `ANDROID_ABI=arm64-v8a`,
   `ANDROID_PLATFORM=android-26`, `ANDROID_STL=c++_static`). Ninja는 PATH에 없으므로 Visual Studio에
   딸린 것을 가리키거나 따로 설치한다 (결정 4).

**손댈 파일.** `CMakeLists.txt`, `CMakePresets.json`, `cmake/platform/{windows,android}.cmake`,
`cmake/compiler/{msvc,clang}.cmake`, `cmake/dependencies/{skia,catch2}.cmake`, `cmake/dependencies.cmake`,
`tests/CMakeLists.txt`, 새 `cmake/android/adb_run.*`, `scripts/fetch_skia.ps1`, 새
`third_party/skia-prep-android-arm64.json`, `.gitignore`, [Skia 빌드 준비](skia-build.md),
[빌드 체계](concepts/build-system.md).

**완료 판정.**

- `ctest --preset android-arm64-core-release`와 `-debug`가 검증 기기에서 모두 통과한다.
- `luil_core_tests`의 테스트 수가 Windows의 `luil_core_tests`와 같다. 기기에서 뺀 테스트가 있으면
  이유와 함께 목록을 남긴다.
- `fetch_skia.ps1`을 인자 없이 부르면 지금과 같은 일을 한다. CI 설정을 고치지 않는다.
- 공통 조건을 통과한다.

**위험.**

- **이식 가능한 층을 clang과 libc++로 처음 컴파일한다.** 경고를 오류로 다루므로, MSVC가 넘기던
  것(암시적 변환, 사용하지 않는 변수, 초기화 순서)이 한꺼번에 쏟아질 수 있다. 고칠 때 Windows
  동작이 바뀌지 않아야 한다. core에서 C++20 라이브러리 기능을 조사해 보니 `std::format`,
  `jthread`, `stop_token`은 쓰지 않는다. `std::to_chars`(정수)만 있다.
- 링크 계약의 순서. 위 5.
- Skia 패키지가 RTTI 없이 컴파일되었을 수 있다. luil의 `app_message`는 `type_info`를 쓴다. Skia
  클래스에 `dynamic_cast`를 쓰지 않으면 섞여도 괜찮지만, probe와 같은 링크로 먼저 확인한다.
- 기기의 시간 정밀도와 부하 때문에 시간에 민감한 테스트가 흔들릴 수 있다.
- Windows용 NDK가 없다 (결정 4).

### 3단계 — 최소 APK와 앱 호스트 뼈대

**목표.** `luil_hello`가 기기에 뜬다. CPU로 그리고, Android 글꼴로 한글까지 나오며, 기본 수명
주기를 견딘다. Vulkan과 입력은 아직이다. "호스트가 살아 있는가"를 "스왑체인이 맞는가"와
떼어서 확인하려고 CPU부터 한다.

- **앱 호스트** (결정 2). `include/luil/android/android_app.h`에
  `run_android_app(android_app*, const app_config&, const app_environment&)`를 둔다.
  `android_main` thread가 luil의 UI thread다. 깨우기 신호는 `eventfd`를 `ALooper_addFd`로 등록해
  나른다. Win32의 `PostMessageW`에 해당한다.
- **수명 주기.**
  - 창이 생기면 표면과 렌더러를 만들고, 창이 사라지면 표면만 버린다.
  - 일시정지하면 그리기를 멈춘다. logic은 계속 돈다.
  - 멈춤(`onStop`)에서 logic에 저장 기회를 준다. Android는 `onDestroy` 없이 프로세스를 죽일 수
    있으므로 종료 저장만 믿을 수 없다. 이 메시지를 공개 API로 더해야 한다
    (`app_delegate::make_lifecycle_message` 같은 것).
  - 회전·크기·밀도·다크 모드 변경으로 Activity가 다시 만들어지지 않도록
    `android:configChanges`로 받는다. 네이티브 상태를 그대로 유지하기 위해서다.
  - 파괴(`onDestroy`)에서 `app_host::shutdown()`을 부른다.
- **CPU 그리기.** `ANativeWindow_setBuffersGeometry(RGBA_8888)` 다음 `ANativeWindow_lock`으로 받은
  버퍼를 `SkSurfaces::WrapPixels`로 감싸 바로 그린다. 복사가 없다. stride를 지킨다.
- **글꼴.** `SkFontMgr_New_Android(nullptr, SkFontScanner_Make_FreeType())`를 쓴다.
  - fallback은 `matchFamilyStyleCharacter`에 사용자 언어(`AConfiguration_getLanguage`·`getCountry`로
    만든 BCP-47)를 넘긴다.
  - 기본 UI 서체는 `sans-serif`다. Windows의 Segoe UI 자리다.
  - 글꼴 캐시는 1단계에서 core로 옮긴 것을 쓴다.
- **자산.** codicon과 고지문을 어떻게 실을지는 둘 중 하나다.
  - APK `assets/`에 넣고 `AAssetManager`로 읽는다. 소비자의 Gradle에 복사 단계가 필요하다.
    Windows `.rc` 조각과 같은 성격의 소비자 단계다.
  - 빌드에서 바이트 배열 `.cpp`로 생성해 라이브러리에 넣는다. 소비자 단계가 없고 iOS에서도
    그대로 쓴다.

  **바이트 배열을 추천한다.** codicon.ttf는 약 146KB다. Windows는 이번에 `.rc`를 그대로 둔다.
- **FreeType 표기.** `luil_generate_third_party_notices`가 Android 패키지의 `NOTICE.md`를 읽어
  FreeType(FTL) 문구가 고지에 들어가는지 확인하고, 소비자 계약 문서에 "앱 문서에 FreeType을
  밝힌다"를 적는다.
- **배율과 inset.** 배율은 `AConfiguration_getDensity() / 160`이다. 시스템 막대·컷아웃·IME inset은
  metrics 메시지와 `platform_info`로 앱에 알린다.

**손댈 파일.** 새 `include/luil/android/android_app.h`, 새
`src/android/{android_app,android_surface,cpu_skia_renderer_android,android_fonts,android_assets}.cpp`,
`src/CMakeLists.txt`, `cmake/platform/android.cmake`, `cmake/generate_notices.cmake`, 새
`cmake/embed_asset.cmake`, 새 `examples/android/`(Gradle 프로젝트 또는 매니페스트, 결정 2),
`examples/hello/` (Android 진입점만 더한다), 공개 API 중 수명 주기 메시지.

**완료 판정.**

- `adb install`한 hello가 뜨고 영문·한글·codicon이 올바르게 그려진다. 화면을 찍어 확인한다.
- 회전 10회, 홈으로 나갔다 돌아오기 10회, 화면 끄고 켜기를 해도 죽지 않는다. logcat에 오류가
  없다.
- 뒤로 가기로 끝내면 `shutdown()`이 3초 예산 안에 끝난다 (logcat 표식으로 확인).
- 공통 조건을 통과한다.

**위험.**

- `android_main` thread와 Java 메인 thread가 다르다. 6단계의 JNI 호출(IME, 클립보드)이 어느
  thread에서 도는지가 여기서 정해진다.
- `configChanges`로 받지 못하는 변경(언어 전환 일부, 멀티 윈도 진입)에서는 Activity가 다시 만들어질
  수 있다. 그때 네이티브 상태를 어떻게 다룰지 정해야 한다.
- 밀도 배율로 Windows와 같은 논리 픽셀 값을 쓰면 크기 감각이 다르다(dp는 96dpi가 아니라 160dpi
  기준이다). 앱의 치수는 바꾸지 않고 배율만 맞춘다.
- CJK fallback이 제조사 글꼴 구성에 따라 다르다.

### 4단계 — Vulkan 렌더러

**목표.** GPU로 그린다. `automatic`에서 Vulkan이 실패하면 CPU로 물러선다. Windows의 Direct3D
경로와 같은 정책이다.

- `ANativeWindow`에서 `VK_KHR_android_surface`로 표면을 만든다. 인스턴스 확장은 `VK_KHR_surface`와
  `VK_KHR_android_surface`, 장치 확장은 `VK_KHR_swapchain`이다. probe처럼 `libvulkan.so`를 dlopen한다.
- Ganesh는 `GrDirectContexts::MakeVulkan`으로 붙인다. 할당기는 `VulkanMemoryAllocators::Make`로
  만들어 `VulkanBackendContext::fMemoryAllocator`에 넘긴다. 선언은 luil의 비공개 헤더
  (`src/android/vulkan_memory_allocator.h`)에 probe와 똑같이 둔다.
- 스왑체인은 FIFO 표시 방식이고 이미지가 둘이나 셋이다. 획득·표시 semaphore는 Ganesh의
  `GrBackendSemaphore`로 대기와 신호를 건다. `VK_ERROR_OUT_OF_DATE_KHR`와 `VK_SUBOPTIMAL_KHR`이면
  스왑체인을 다시 만든다.
- **표면 소멸은 장치 소멸이 아니다.** 창이 사라지면 스왑체인, 감싼 표면, `VkSurfaceKHR`만 버린다.
  `VkDevice`와 `GrDirectContext`는 유지했다가 창이 다시 생기면 스왑체인만 새로 만든다. 백그라운드로
  가면 `purgeUnlockedResources`로 GPU 메모리를 돌려준다. 장치를 잃으면 context를 abandon하고
  CPU로 물러선다 (Windows와 같다).
- 회전에서는 `preTransform`을 표면의 `currentTransform`에 맞출지 정한다. identity로 두면
  컴포지터가 돌려 주지만 비용이 든다. 처음에는 identity로 맞게 만들고 측정한 뒤 정한다.
- `fence_wait.h`의 대기 예산 계산을 `vkWaitForFences`의 조각 대기에 쓴다.
- `renderer_mode`에 GPU 일반 값을 더할지 정한다 (결정 6).

**손댈 파일.** 새 `src/android/{vulkan_skia_renderer.cpp,vulkan_memory_allocator.h}`,
`src/android/android_surface.cpp`, `src/host/` 렌더러 정책, `include/luil/app/renderer_policy.h`(결정 6),
`tests/renderer_policy_tests.cpp`.

**완료 판정.**

- hello와 widgets가 Vulkan으로 그려진다 (backend 정보로 확인).
- 일시정지·재개와 회전을 20회 반복한 뒤에도 장치가 하나이고, 메모리가 계속 늘지 않는다
  (`dumpsys meminfo`).
- Debug에서 Vulkan validation layer를 켜면 오류가 없다. layer가 기기에 없으면 APK에 넣어 돌린다.
- 실패 주입(생성 실패, N frame 뒤 손실)으로 CPU 전환이 동작한다. Windows의 smoke와 같은 방식이다.

**위험.**

- 할당기 선언은 Skia 내부 API다. Skia milestone을 올릴 때마다 서명을 다시 확인해야 한다. 핀의
  `skia_milestone`과 함께 문서에 적는다.
- Adreno 드라이버마다 동작이 다를 수 있다. 검증 기기는 Adreno 730 하나다.
- 스왑체인 다시 만들기와 수명 주기 경합으로 표면이 이미 사라진 뒤 그리기를 시도할 수 있다.

### 5단계 — 터치·펜·키보드·마우스 입력

**목표.** `AInputEvent`(GameActivity면 `GameActivityMotionEvent`)를 luil의 `raw_input_event`로 옮긴다.
제스처 인식은 이미 이식 가능한 층(`interaction_controller`)에 있으므로 다시 쓰지 않는다.

- **포인터.** 도구 종류(`TOOL_TYPE_FINGER`, `STYLUS`, `ERASER`, `MOUSE`)를 `device`로 옮긴다.
  - 1단계에서 옮긴 `pointer_sequence_tracker`가 처리 규칙을 맡는다: 배럴 버튼은 우클릭, 지우개는
    삼키기, 호버는 접촉 없는 이동, `ACTION_CANCEL`은 취소다.
  - 묶여 온 과거 표본(historical)도 차례로 넣는다.
- **시간.** `AMotionEvent_getEventTime`은 `CLOCK_MONOTONIC` 기준 나노초이고, libc++의
  `steady_clock`도 `CLOCK_MONOTONIC`이다. 값을 바로 옮길 수 있다. 이 사실은 테스트로 고정한다.
- **배율.** 누름 이벤트의 `scale`은 표면 배율이다. 판정 거리(12 논리 픽셀)는 dp 기준이 된다.
- **키보드.** 이름 있는 키 표, 영문자·숫자, `first_platform_key + AKEYCODE`로 옮긴다.
  - `KEYCODE_DEL`은 U+0008, Ctrl+Del은 U+007F다.
  - 수정자 역할은 Ctrl을 `primary_shortcut`으로 둔다. Meta(검색 키)는 `meta`다.
  - 소프트 키보드의 글자는 6단계 IME 경로로 들어온다.
- **휠.** `AXIS_VSCROLL`·`AXIS_HSCROLL`의 실수 값을 120 단위로 바꾼다.
- **길게 누르기.** 지금 계약은 뗄 때 판정한다. Android의 기대(누르는 동안 메뉴)에 맞출지는 이
  단계에서 정한다. 맞추려면 input thread에 시간 만료 이벤트를 더해야 하고, Windows의 터치 동작도
  바뀐다. 그래서 따로 승인받는다.
- 시스템 뒤로 가기 제스처 영역(화면 가장자리)과 끌기 스크롤이 겹치는지 본다.

**손댈 파일.** 새 `src/android/android_input.cpp`, `src/host/pointer_sequence_tracker.*`, 새 Android
변환 테스트(기기에서 돈다), [터치 제스처와 펜 입력](touch-pen-input-design.md),
[입력 pump](concepts/input-pump.md).

**완료 판정.**

- widgets에서 탭, 더블 탭, 쓸어 스크롤, 오래 잡고 끌기, 길게 눌러 메뉴가 문서의 표대로 동작한다.
- S Pen으로 호버 툴팁, 배럴 버튼 우클릭, 텍스트 선택이 된다. 검증 기기 S22 Ultra에는 S Pen이 있다.
- 블루투스 키보드나 `adb shell input keyevent`로 단축키와 Tab 초점 이동이 된다.
- 변환 단위 테스트가 기기에서 통과한다.

**위험.** 제조사별 MotionEvent 차이, 손바닥 거부, 여러 손가락이 접촉할 때 첫 접촉만 제스처로
쓰는 계약과 Android 관례의 차이.

### 6단계 — 텍스트 입력(IME)과 클립보드

**목표.** 한글 조합 입력이 텍스트 칸에서 동작하고, 다른 앱과 복사·붙여넣기가 된다.

- **텍스트 입력 경계.** 1단계 이후 `tsf_host`에서 플랫폼 중립 부분을 `text_input_host`로 올리고,
  TSF와 Android가 같은 것을 구현한다. 이 경계는 이식 가능한 hook에 기댄다. `ui_element::text_input`,
  `text_span_bounds`, `text_composition_event`, `make_text_edit_action`이다.
- **Android 쪽 방식** (결정 2와 묶여 있다).

  | 방식 | 조합 상태 | 후보 창 자리 (`CursorAnchorInfo`) | Java 코드 | 빌드 |
  | --- | --- | --- | --- | --- |
  | GameActivity의 GameTextInput | 텍스트·선택·조합 범위를 상태로 준다 | 지원 범위를 확인해야 한다 | AGDK가 준다 | Gradle (AAR) |
  | 직접 만든 `InputConnection` (View 하나 + JNI) | 전부 직접 다룬다 | 직접 보낸다 | 직접 쓴다 (수백 줄) | Gradle, 또는 javac + d8 |
  | 키 이벤트만 (NativeActivity) | 없다. 한글 조합이 불가능하다 | 없다 | 없다 | Gradle 없이 가능 |

  **추천은 GameTextInput으로 시작하는 것이다.** 후보 창 자리나 조합 범위 다루기가 모자라면 직접
  만든 `InputConnection`으로 옮긴다. 두 방식 모두 위의 `text_input_host` 뒤에 있으므로 core는
  바뀌지 않는다.
- **thread.** `InputConnection`은 Java 메인 thread에서 불린다. luil의 UI thread(`android_main`)와
  다르므로 상태를 넘기는 큐가 필요하다. TSF의 `shadow_document`처럼 확정 문서의 사본을 둔다.
- **IME inset.** 소프트 키보드가 초점 칸을 가리면 [키보드 초점 자동 스크롤](focus-reveal-design.md)이
  inset을 보고 칸을 화면 안으로 들인다.
- **클립보드.** UI thread가 JNI로 `ClipboardManager`를 부른다. Android 10부터는 초점을 가진 앱만
  읽을 수 있고, 12부터는 붙여넣을 때 시스템이 알림을 띄운다. 요청과 실행 경로는 지금 그대로다.

**손댈 파일.** 새 `src/host/text_input_host.h`, `src/win32/win32_tsf_input.*`와 `window_surface.cpp`
(구현을 새 경계에 맞춘다), 새 `src/android/{android_text_input,android_clipboard,jni_env}.cpp`, Java
쪽(방식에 따라), [텍스트 입력](concepts/text-input.md),
[Popup 안의 텍스트 입력과 IME](popup-ime-design.md).

**완료 판정.**

- 삼성 키보드와 Gboard에서 한글 조합, 확정, 조합 중 Backspace, 선택 바꾸기가 된다.
- 초점을 받으면 키보드가 뜨고, 잃으면 내려간다. 떠 있는 키보드가 초점 칸을 가리지 않는다.
- 다른 앱과 양쪽으로 복사·붙여넣기가 된다.
- Windows의 `tsf_input_tests`가 그대로 통과한다.

**위험.** IME마다 다른 조합 동작(특히 삼성 키보드). Java thread와 UI thread 사이의 경합. GameTextInput의
기능 한계.

### 7단계 — popup overlay, 빼는 것과 대체할 것

**popup.** Android는 `ui_popup`을 주 표면 안의 layer로 그린다. 지금의 데이터 모델(앵커 기준 논리
좌표, 분리 tree, `surface_tree_list`)을 그대로 쓴다. 해야 할 일은 다음과 같다.

- 그리기 순서: 주 tree, popup들, tooltip·끌기 표시 순서다.
- hit test를 layer별로 보내 이벤트에 popup의 표면 id를 싣는다.
- 닫힘 계기를 옮긴다. 바깥 누르기, 휠은 스크롤, Esc는 뒤로 가기, `surface_resized`는 회전과
  IME로 생긴다.
- 화면 경계 다듬기.

Windows는 지금의 HWND 방식 그대로다. 이 일은 core의 hit test 경로를 건드리므로 widgets의
드롭다운·메뉴가 동작하는지로 확인한다. 5단계 이후 바로 할 수도 있다.

| 기능 | Android 처음 판 | 나중 |
| --- | --- | --- |
| WebView2 | 빌드하지 않는다 (`LUIL_ENABLE_WEBVIEW=OFF`). `webview_element`가 placeholder를 그리고, `ui_frame::webviews`는 무시한다. | Android WebView를 별도 View로 겹치는 방식. 따로 계획한다. |
| UI Automation | 빌드하지 않는다. | `AccessibilityNodeProvider`를 이식 가능한 `make_access_snapshot`·`diff_access_snapshots` 위에 올린다. 따로 계획한다. |
| DirectComposition | 해당 없다. | — |
| caption과 창 모드 | `platform_info.caption = false`로 알려 앱이 `caption_element`를 빼게 한다. `window_minimize`, `toggle_maximize`는 무시한다. `toggle_fullscreen`은 immersive 모드로 옮길 수 있다. | — |
| 보조 창 `ui_window` | 지원하지 않는다. frame에 있으면 무시하고 한 번 경고를 남긴다. | 전체 화면 layer로 보여 줄지 검토한다. |
| 파일 끌어 놓기 | 빌드하지 않는다. | — |
| 커서 | 무시한다. | 마우스·스타일러스 포인터 아이콘 (Java `PointerIcon`) |
| 네트워크 | 아래 "네트워크층" | iOS는 `NSURLSession`으로 같은 자리에 들어온다. |

**네트워크층** (결정 3). Android 구현은 JNI로 `HttpURLConnection`을 부른다. 공개 헤더
(`include/luil/net/*`)와 계약(받은 표마다 답 하나, client thread에서 차례로 전달, 동시 요청 상한,
취소, `stop()` 예산)은 그대로다.

- `src/net`을 이식 가능한 부분과 OS 백엔드로 나눈다. 이식 가능한 부분은 `http_body`,
  `http_media_type`, URL·헤더 다루기다. 백엔드는 WinHTTP와 Android다.
- `http_url`과 `http_request_context`의 `std::wstring`은 WinHTTP 백엔드 안으로 들인다.
- Java 클래스를 두지 않는다. C++가 JNI로 `java.net.URL`과 `HttpURLConnection`을 바로 부르고, 몸
  해석과 전달은 지금처럼 C++ client thread가 맡는다. 요청을 보내는 thread는 JVM에 붙인다
  (`AttachCurrentThread`).
- 코드 페이지 변환은 Windows 전용으로 남긴다. Android는 UTF-8만 받는다 (bionic의 `iconv`는 API 28부터다).
- 테스트는 지금의 loopback 서버(winsock)를 POSIX 소켓으로도 세워 기기에서 돌린다.

이 일은 2단계의 core 분리와 별도로 진행할 수 있다. 다만 JNI가 쓸 `JavaVM`은 Activity가 있어야
얻으므로, 기기 검증은 3단계 이후에 한다.

### 8단계 — 예제, 패키징 마무리, 서명

**목표.** hello, widgets, demo가 기기에서 뜨고, 소비자가 따라 할 수 있는 Android 계약 문서가 있다.

- **패키징** (결정 2). 추천은 Gradle + AGP의 `externalNativeBuild`(CMake)다. GameActivity가 prefab
  AAR로 오므로 Gradle이 사실상 필요하다. luil 라이브러리 자체는 Gradle 없이 CMake만으로
  configure할 수 있게 유지한다. 2단계의 기기 테스트가 그 경로다.
- **서명.** 개발은 Gradle의 debug keystore로 한다. 배포 서명은 소비자의 일이다. 문서에는 apksigner
  절차만 적고, keystore를 저장소에 넣지 않는다.
- **설치와 실행.** `adb install -r`, 그다음 `adb shell am start -n <패키지>/<Activity>`로 띄운다.
- **smoke.** demo에 `--smoke-test`에 해당하는 intent extra를 받게 한다. 한 frame을 그리고 logcat에
  표식을 남긴 뒤 끝난다. 이것을 CTest의 `android` 라벨 테스트로 등록한다.
- **소비자 계약.** [소비자 계약](concepts/consumer-contract.md)에 Android 절을 더한다. minSdk 26,
  `c++_static`, 링크 계약, FreeType 표기, 자산, Vulkan 헤더는 NDK 것을 쓴다는 것.

**완료 판정.** 예제 셋이 기기에서 뜨고 기본 조작이 된다. Android smoke가 CTest로 통과한다. 소비자
계약 문서대로 새 빈 프로젝트에서 hello를 세울 수 있다.

**위험.** Gradle·AGP·JDK 판 사이의 호환. CI에 Android를 넣을지는 따로 정한다 (기기가 필요한
부분은 CI에서 뺀다).

### 9단계 — iOS를 위한 자리 (지금은 하지 않는다)

| 경계 | iOS 구현 |
| --- | --- |
| 진입점과 UI thread | `run_ios_app`. UIKit 메인 thread가 UI thread다. 깨우기는 `CFRunLoopSource`로 한다. |
| 수명 주기 | `UIScene` 활성·비활성·백그라운드를 3단계의 수명 주기 메시지로 옮긴다. |
| 렌더러 | `CAMetalLayer`, `GrDirectContexts::MakeMetal`(Ganesh). CPU는 `CALayer.contents`에 비트맵을 준다. |
| 글꼴 | `SkFontMgr_New_CoreText`. fallback도 CoreText다. |
| 자산 | 3단계의 바이트 배열을 그대로 쓴다. |
| 입력 | `UITouch`. Apple Pencil은 `.pencil` 종류이고 iPadOS 16부터 호버가 있다. 하드웨어 키보드는 `UIPress`로 받는다. |
| IME | `UITextInput` 프로토콜을 `text_input_host` 위에 구현한다. |
| 클립보드 | `UIPasteboard` |
| 배율·inset | `contentScaleFactor`, `safeAreaInsets` |
| 네트워크 | `NSURLSession` (결정 3에서 Android를 OS 스택으로 가면 같은 모양이 된다) |

**skia-prep 쪽에 필요한 것.**

- `ios-arm64` 대상을 만든다. Skia의 iOS Bazel 도구사슬이 macOS에서만 돌아서 Mac이 필요하다
  (skia-prep README).
- 지금의 `skia-152-bazel-rust-android-platform.patch`는 Skia `BUILD.gn`의 `bazel_args`에 Android
  arm64 플랫폼만 더한다. 이것을 iOS로 넓힌다.
- rust 대상 `aarch64-apple-ios`는 upstream `MODULE.bazel`에 이미 있다.
- args는 Metal을 켜고 Vulkan과 FreeType을 끈다. 글꼴은 CoreText로 한다.
- luil 쪽에서는 `skia.cmake`의 대상 표에 한 줄을 더하고 `cmake/platform/ios.cmake`를 만든다.

## 순서를 바꾼 이유

1. **패키징과 앱 호스트를 렌더링보다 앞에 뒀다.** 화면에 그리려면 `ANativeWindow`가 필요하고,
   그것은 Activity에서, Activity는 설치된 APK에서만 나온다. 그래서 최소 APK와 호스트 뼈대(초안의
   8과 5의 일부)가 렌더링(초안의 3)보다 먼저다. APK 없이 돌 수 있는 것은 2단계의 테스트 실행
   파일뿐이다.
2. **글꼴을 첫 화면과 같은 단계에 넣었다.** 글꼴 관리자가 없으면 `draw_frame`이 글자를 그리지
   못해, 첫 화면으로 확인할 것이 없다.
3. **CPU 그리기를 Vulkan보다 먼저, 다른 단계로 했다.** 호스트·수명 주기 문제와 스왑체인 문제를
   떼어 놓기 위해서다. CPU 경로는 Vulkan 실패 시 물러설 자리이기도 하다.
4. **경계 정의를 둘로 나눴다.** core가 의존하는 경계(글꼴, 자산, 시스템 외양, 렌더러 모양,
   host API)는 1단계에서 Windows로 세운다. 플랫폼 안쪽에서만 쓰는 경계(텍스트 입력, 클립보드
   실행, 커서)는 이 문서에 모양만 적고, 쓰는 단계(6, 7)에서 만든다. Win32 구현 하나만 보고 세운
   인터페이스는 두 번째 구현에서 대개 다시 고치게 되기 때문이다.
5. **popup overlay를 따로 세웠다.** 초안은 "같은 표면 안의 overlay로?"라고 물었다. 그렇게 하면
   core의 hit test 경로에 손이 가는 기능이 되므로, 별도 완료 판정이 필요하다.

## 결정할 것

2026-10-02에 다음과 같이 정했다. 아래 표들은 고를 때 본 선택지를 기록으로 남긴 것이다.

| 결정 | 고른 것 |
| --- | --- |
| 1. 공개 host API | (a) `luil/app/`, 네임스페이스 `luil`로 옮기고, 옛 경로는 전달 헤더와 `using` 별칭으로 한 판 유지한다 |
| 2. 앱 호스트·IME·패키징 | (a) GameActivity, GameTextInput, Gradle |
| 3. 네트워크층 | (b) OS 스택을 JNI로 (`HttpURLConnection`) |
| 4. 도구 설치 | (a) Windows에 설치. 받기 전에 크기를 알리고 다시 묻는다 |
| 5. 기기 테스트 | (a) CTest + adb 래퍼 |
| 6. `renderer_mode` | (a) `gpu`를 더한다 |

**Java 소스 방침.** luil이 들고 있는 Java 소스는 0줄로 한다. 클립보드, HTTP, 소프트 키보드 띄우기,
inset 같은 프레임워크 기능은 C++에서 JNI로 프레임워크 클래스를 바로 부른다. Java 소스가 꼭 필요한
곳은 IME 하나다. 조합 입력을 받으려면 `View.onCreateInputConnection`을 재정의해야 하는데, 클래스를
재정의하는 일은 JNI로 할 수 없다. 그 부분은 GameActivity와 GameTextInput이 이미 컴파일된 AAR로
준다. 그래서 APK에는 AGDK의 Java 코드가 들어가고, Gradle도 그 AAR 때문에 필요하다. Java가 전혀
없는 길은 NativeActivity뿐이고, 그러면 한글 조합을 받을 수 없다.

**결정 1 — 공개 host API의 경로와 네임스페이스** (1단계)

| 선택지 | 내용 | 소비자 영향 |
| --- | --- | --- |
| (a) 옮기고 별칭을 남긴다 (추천) | `luil/app/app_host.h`, 네임스페이스 `luil`. 옛 `luil/win32/app_host.h`는 새 헤더를 include하고 `namespace luil::win32 { using luil::app_host; ... }`를 둔다. 한 판 뒤에 옛 경로를 없앤다. | 소스 그대로 빌드된다. 전방 선언만 깨진다. |
| (b) 이름을 그대로 둔다 | Android도 `luil::win32::app_host`를 쓴다. | 영향이 없다. 이름이 사실과 맞지 않는 상태가 남는다. |
| (c) 옮기고 옛 경로를 지운다 | 깨는 변경이다. | 모든 소비자가 include와 이름을 고쳐야 한다. |

**결정 2 — 앱 호스트, IME, 패키징** (3·6·8단계. 서로 묶여 있어 한 번에 정한다)

| 선택지 | 호스트 | IME | 패키징 |
| --- | --- | --- | --- |
| (a) GameActivity (추천) | AGDK GameActivity. 입력 이벤트가 풍부하고, inset을 주며, 유지보수된다. | GameTextInput | Gradle 필수 (AAR) |
| (b) NativeActivity + 직접 만든 InputConnection | NDK에 들어 있는 NativeActivity. 외부 의존이 없다. | Java로 직접 (JNI) | Gradle, 또는 javac + d8 + aapt2 스크립트 |
| (c) 직접 만든 Activity | `SurfaceView` + JNI. 전부 직접 다룬다. | Java로 직접 | Gradle |
| (d) NativeActivity만 | Java 코드가 없다. | 키 이벤트만 (한글 조합 불가) | Gradle 없이 aapt2만 |

**결정 3 — 네트워크층** (7단계)

| 선택지 | 내용 |
| --- | --- |
| (a) 처음에는 끈다 (추천) | Android에서 `http_client::create`가 "지원하지 않음" 오류를 돌려준다. 공개 헤더는 그대로다. 나중에 (b)로 간다. |
| (b) OS 스택을 JNI로 | `HttpURLConnection`. 프록시·인증서 저장소·TLS를 OS가 맡는다. WinHTTP와 같은 철학이고, iOS의 `NSURLSession`과 모양이 같다. |
| (c) libcurl + TLS 라이브러리 | 플랫폼 공통 구현이다. 의존이 무겁고, 인증서 저장소를 따로 다뤄야 한다. |
| (d) Cronet | Google의 네트워크 스택이다. APK가 커진다. |

참고로 `http_body`와 `http_media_type`은 이미 이식 가능하다. 코드 페이지 변환
(`MultiByteToWideChar`)은 Windows 전용이다. bionic의 `iconv`는 API 28부터라 minSdk 26에서는 쓸 수
없으므로, Android는 처음에 UTF-8만 받는다.

**결정 4 — 도구 설치** (2·3단계. 받기 전에 묻는다)

| 선택지 | 받을 것 |
| --- | --- |
| (a) Windows에 설치 (추천) | 2단계: Windows용 NDK r27d (풀면 수 GB. 정확한 크기는 받기 전에 확인해 알린다). 3단계부터: Android SDK command-line tools, platform `android-35`, build-tools, JDK 17. Gradle은 wrapper가 받는다. Ninja는 Visual Studio에 딸린 것을 쓰거나 따로 받는다. |
| (b) WSL에서 빌드 | NDK는 이미 있다. WSL에 cmake 4.2와 ninja를 설치한다. pwsh 스크립트(`fetch_skia.ps1`, 스타일 검사)를 WSL에서 부르려면 pwsh도 필요하다. adb는 Windows 것을 부른다. |

**결정 5 — 기기에서 테스트 돌리는 방식** (2단계)

| 선택지 | 내용 |
| --- | --- |
| (a) CTest + adb 래퍼 (추천) | `CMAKE_CROSSCOMPILING_EMULATOR`로 `ctest`가 테스트마다 기기에서 실행한다. 실행 파일은 바뀌었을 때만 올린다. |
| (b) 별도 스크립트 | `scripts/run_android_tests.ps1`이 올리고 한 번에 돌린다. CTest 목록과 따로 논다. |
| (c) APK 계측 테스트 | 실제 Activity 안에서 돈다. 무겁고 Gradle이 필요하다. |

**결정 6 — `renderer_mode`의 이름** (1·4단계)

| 선택지 | 내용 |
| --- | --- |
| (a) `gpu`를 더한다 (추천) | "플랫폼의 GPU 경로"라는 뜻이다. `direct3d`는 Windows에서 `gpu`와 같은 뜻으로 남는다. 파서는 `gpu`를 받는다. |
| (b) 플랫폼별 값을 더한다 | `vulkan`, `metal`. 다른 플랫폼에서 고르면 오류다. |

## 착수 준비 — 1단계

### 기준선

위 "기준선" 표의 값이다. Release는 799개, Debug는 780개(asan 19개 제외)가 모두 통과했다.
smoke 5개는 두 구성 모두에서 통과했다. CPU 전용 빌드(`LUIL_ENABLE_DIRECT3D=OFF`)의 기준선은
1단계의 첫 커밋 전에 따로 잰다. 지금 그 구성을 담은 preset이 없어서다.

### 작업 순서

1단계를 아래 커밋들로 나눈다. 커밋마다 공통 조건을 돌린다. 3번은 결정 1의 (a)대로 옛 경로를 전달
헤더로 남기고, 6번은 결정 6의 (a)대로 `renderer_mode::gpu`를 더한다. `gpu`는 Windows에서
`direct3d`와 같은 동작이고, `renderer_policy_tests`의 "vulkan 거부" 확인은 그대로 둔다.

1. `build: 이식 가능한 층을 별도 대상으로 세운다` — `luil_core`, `luil_core_tests`, core 소스 검사
   테스트를 더한다.
2. `refactor: 순수 계산 파일을 host 묶음으로 옮긴다` — 파일마다 Win32 의존을 다시 확인한 뒤 옮긴다.
3. `refactor: host API를 플랫폼 중립 경로로 옮긴다` — 결정 1에 따른다.
4. `refactor: app_host가 Win32 글꼴과 MSVC 내장 함수에 기대지 않는다`
5. `refactor: 글꼴·자산·시스템 외양을 플랫폼이 공급한다` — 테마 해석을 core로 옮긴다.
6. `refactor: 렌더러 인터페이스에서 창 핸들을 뺀다` — 결정 6에 따른다.
7. `refactor: 포인터 시퀀스 추적을 Win32 메시지 값에서 뗀다`
8. `docs: 플랫폼 경계를 개념 문서에 적는다`

### 확인한 환경

- CMake 4.2.0, Visual Studio 2026(18)과 2022가 있다. Ninja는 두 VS에 딸려 있다.
- `third_party/skia-prep`에 win-x64 r2가 설치되어 있다.
- adb에 기기가 연결되어 있다 (2단계부터 쓴다).

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
| 렌더러 | `skia_renderer`(HWND, `IDCompositionVisual*`가 서명에 있음) | **1단계에서 세움.** core의 `skia_renderer`(`backend/resize/render`)와 `renderer_host`. 플랫폼은 `renderer_factories`로 생성 함수만 넘긴다. `underlay`는 Win32 확장 `composition_renderer`다. | 1, 3, 4 | `CAMetalLayer` + Ganesh Metal |
| 글꼴 공급 | `win32_fonts.cpp`의 free function, `registry_font_resolver` | **1단계에서 세움.** core의 글꼴 registry와 `font_source`(시스템 관리자, 새로 만드는 관리자, 기본 UI 가족, 사용자 언어). 플랫폼이 `platform_font_source()`를 하나 정의한다. | 1, 3 | `SkFontMgr_New_CoreText` |
| 자산 읽기 | `FindResourceW` (codicon, 고지) | **interface를 만들지 않았다.** 공개 `load_codicon_typeface()`가 경계이고 플랫폼마다 구현한다. Android는 3단계에서 바이트 배열로 구현한다. | 3 | 같은 방식 |
| 시스템 외양 | `win32_window.cpp`가 읽고 팔레트까지 고른다 | **interface를 만들지 않았다.** 해석(`resolve_color_theme`, `frame_palette`)은 이미 core에 있고, OS 값은 기존 setter(`set_system_accent`, `set_text_render_quality`)와 `frame_state` 필드로 넣는다. 플랫폼은 값을 읽기만 한다. | 3 | `UITraitCollection` |
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
  있다. **옮기지 않고 Win32 호환 함수로 남긴다** (1단계에서 정함). 앱이 `platform_key_code('S')`처럼
  부르는 기존 경로이고 [상호작용](concepts/interaction.md)도 그렇게 적고 있어, 옮기면 깨는 변경이
  된다. Android 입력은 자기 키 코드를 이름 키로 직접 옮긴다.
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
| 4. 도구 설치 | (a) Windows에 설치. 받기 전에 크기를 알리고 다시 묻는다. 3단계에서 고쳤다: JDK를 따로 설치하지 않고 Android Studio를 설치해 그 SDK와 JBR을 쓴다 |
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
| (a) Windows에 설치 (추천) | 2단계: Windows용 NDK r27d (풀면 수 GB. 정확한 크기는 받기 전에 확인해 알린다). 3단계부터: Android SDK command-line tools, platform `android-37`, build-tools, JDK 17 (3단계에서 JDK 대신 Android Studio의 JBR로 바꿨다). Gradle은 wrapper가 받는다. Ninja는 Visual Studio에 딸린 것을 쓰거나 따로 받는다. |
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

## 1단계 결과

2026-10-02에 끝냈다. 브랜치는 `android-port`이고 커밋은 로컬에만 있다.

| 커밋 | 내용 |
| --- | --- |
| `7bcfb24` | `luil_core` 대상, `luil_core_tests`, core 이식성 검사 test |
| `9feee30` | popup·웹뷰 자리 대조, 다시 그리기 계획, fence 대기 예산을 core로 |
| `3ec69ae` | host API를 `luil/app/`과 `luil`로. 옛 경로는 별칭 헤더. frame 그리기와 렌더러 정책을 core로 |
| `669b142` | 줄 끝 정리 스크립트가 worktree를 건너뛴다 (작업 중 다른 세션의 자산을 건드린 것을 고침) |
| `d39eb00` | 글꼴 registry를 core로. OS 글꼴 자원은 `platform_font_source()` |
| `6f9ec19` | `app_host`를 core로. 즉시 종료는 `platform_fail_fast()` |
| `5e42000` | 렌더러 interface와 실패 물러섬을 core로. `renderer_mode::gpu` |
| `7795050` | 터치·펜 접촉 추적을 core로. 접촉 단계 `pointer_phase` |

**검증.** 커밋마다 Release, Debug, CPU 전용 Release의 CTest 전부를 돌렸다 (smoke 포함).

| 구성 | 기준선 | 1단계 끝 |
| --- | --- | --- |
| Release | 799 | 811 |
| Debug (asan 19개 제외) | 780 | 792 |
| CPU 전용 Release | 797 (direct3d smoke는 설계대로 건너뜀) | 809 |

늘어난 12개는 core 이식성 검사 1, `gpu` 모드 선택 1, 렌더러 물러섬 9, 메시지→접촉 단계 1이다.
옮긴 test는 실행 파일만 바뀌었고 수는 그대로다. 예제, 설치본 소비자 test, `app_host_tests`는 옛
이름(`luil::win32::app_host` 등)을 그대로 두어 소스 호환을 확인했다.

**계획과 달라진 것.**

- `system_appearance`와 `asset_reader`는 만들지 않았다. 위 "플랫폼 경계" 표에 이유를 적었다.
- 플랫폼 hook 둘(`platform_font_source()`, `platform_fail_fast()`)은 등록 함수가 아니라 플랫폼
  계층이 하나씩 정의하는 링크 결합으로 했다. 창 없는 test와 input thread가 시작 순서와 무관하게
  같은 자원을 보게 하려는 것이다.
- `alphanumeric_key_code`와 `platform_key_code`는 공개 Win32 호환 함수로 남겼다 (위 "키 코드").
- `dpi_scale.h`(96 DPI 기준), `caption_layout`·`window_mode`(데스크톱 창), `webview_layout`·
  `webview_message_gate`(WebView2)는 Win32에 남겼다. 다른 플랫폼이 쓸 일이 없다.

**2단계로 넘기는 것.**

- `app_host_tests`는 `luil_tests`에 남아 있다. `app_host`가 두 플랫폼 hook을 부르므로, core
  test 실행 파일에서 돌리려면 test용 hook 구현(빈 글꼴 관리자, `abort`)을 붙여야 한다. 2단계에서
  Android 기기 test에 넣을지와 함께 정한다.
- 작업 중 HTTP client test 두 개가 한 번씩 실패했다 ("survives stop with requests in flight"는
  `0xC0020043`, "cancelling a heartbeat mid round yields exactly one final response"는 단언 실패).
  각각 20회·30회 다시 돌려 모두 통과했고 `src/net`은 이 단계에서 바뀌지 않았다. 원인 조사는 별도
  작업으로 돌렸다.

### 확인한 환경

- CMake 4.2.0, Visual Studio 2026(18)과 2022가 있다. Ninja는 두 VS에 딸려 있다.
- `third_party/skia-prep`에 win-x64 r2가 설치되어 있다.
- adb에 기기가 연결되어 있다 (2단계부터 쓴다).

## 2단계 결과

2026-10-02에 끝냈다. 브랜치는 `android-port`이고 커밋은 로컬에만 있다.

| 커밋 | 내용 |
| --- | --- |
| `095c121` | core test용 플랫폼 hook 정의. `app_host_tests`를 core test로 옮김 |
| `838b29a` | `fetch_skia.ps1 -Target`과 android-arm64 r1 핀 |
| `992f345` | clang이 막은 쓰지 않는 람다 캡처 하나 (core 전체에서 clang 오류는 이것뿐이었다) |
| `ae36130` | CMake 플랫폼 분리, Android 검사·clang 옵션·Skia 링크 계약, adb test 실행, preset |

**기기 검증.** Galaxy S22 Ultra(Android 14)에서 `ctest --preset android-arm64-core-release`와
`-debug`가 각각 509개 모두 통과했다 (약 2분 20초·2분 40초). Windows의 `luil_core_tests`도 509개라 기기에서
뺀 test는 없다. 링크 명령에 `libskia.a`가 묶음 맨 앞에 오고 `--allow-multiple-definition`, `-landroid -llog`,
`-static-libstdc++`가 들어간 것을 확인했다.

**Windows 검증.** Release 811, Debug 792 (asan 19개 제외), CPU 전용 Release 809개가 모두 통과했다
(smoke·설치본 소비자 포함). 1단계 끝과 같은 수다 — `app_host_tests` 9개는 실행 파일만 바뀌었다.

**정한 것.**

- 생성기는 Ninja Multi-Config로 했다. Skia 검사와 기기 test가 Debug·Release를 한 빌드 디렉터리에서 다룬다.
- NDK 경로와 Ninja 위치는 저장소 preset에 넣지 않고 `ANDROID_NDK_HOME`과 `CMAKE_MAKE_PROGRAM`으로 받는다.
- 기기 실행 래퍼는 pwsh가 아니라 CMake script다. Android 구성에서 Windows 도구를 요구하지 않는다.
- Android 패키지는 skia-prep 빌드 폴더의 같은 sha256 zip으로 `-Offline` 설치했다. 내려받지 않았다.
- NDK r27d(Windows)는 승인을 받아 `%LOCALAPPDATA%\Android\ndk\27.3.13750724`에 설치했다.
  3단계에서 Android Studio의 SDK 아래(`Sdk\ndk\27.3.13750724`)로 옮겼다. Gradle이 `ndkVersion`으로 찾는 자리다.
  받은 zip의 SHA-1은 Google 저장소 목록의 값(`56607cbc…f426`)과 같았다.

**3단계로 넘기는 것.**

- Android SDK command-line tools, platform, build-tools, JDK가 필요하다 (결정 4). 받기 전에 묻는다.
  (3단계에서 Android Studio로 정했고 platform은 `android-37`이다. 아래 "3단계 결과".)
- Android 구성은 아직 `luil` 플랫폼 대상을 세우지 않는다. 3단계에서 `src/android/`가 그 자리에 들어온다.

## 3단계 결과

2026-10-02에 끝냈다. 브랜치는 `android-port`이고 커밋은 로컬에만 있다.

| 커밋 | 내용 |
| --- | --- |
| `44b2c29` | GameActivity 4.4.2 핀과 `fetch_game_activity.ps1`, CMake의 `luil::game_activity` |
| `a251b80` | 앱 delegate의 플랫폼 중립 부분을 `app_delegate`로. `win32::window_delegate`가 상속하고 수명 주기 메시지를 더함 |
| `9e95341` | Android 앱 host(`luil::android::run_application`), CPU 렌더러, Android 글꼴, 내장 codicon, `frame_state`의 원점 |
| `eb6f935` | hello를 Android APK로 (CMake 공유 라이브러리 + Gradle 포장) |
| `2f71a33` | 모바일 앱 바와 플랫폼 성질 (`app_bar_element`, `ui_platform`). 데스크톱 caption은 모바일에서 자리를 차지하지 않음 |

**기기 검증.** Galaxy S22 Ultra(Android 14)에서 확인했다.

- hello가 한글, codicon, 어두운 테마로 그려지고, 내용은 시스템 막대·컷아웃을 뺀 안전 영역에서
  시작한다. 막대 밑까지 앱 배경색이다 (스크린샷 픽셀로 확인).
- 회전 10회, 홈으로 나갔다 돌아오기 10회, 화면 끄고 켜기를 같은 프로세스로 견딘다. 가로에서는
  컷아웃 쪽 inset이 왼쪽으로 옮겨 온다.
- 뒤로 가기로 끝내면 `shutdown()`이 1ms에 끝나고, 같은 프로세스에서 다시 띄워도 바로 선다.
- 기기 core test가 Release·Debug 각 510개(앱 바 커밋 뒤 514개) 통과한다.
- logcat의 오류는 홈으로 나갈 때마다 시스템이 남기는 `BufferQueueProducer ... disconnect: not
  connected (req=2)` 한 줄뿐이다. CPU로 잠갔던 표면을 시스템이 거둘 때의 순서에서 나오는 것으로
  보이며 동작에는 영향이 없다. 4단계에서 Vulkan 경로와 함께 다시 본다. (4단계: Vulkan 경로에서는 나오지 않는다.)

**정한 것.**

- 도구: JDK를 따로 설치하지 않고 Android Studio의 SDK와 JBR을 쓴다. Gradle은 그 JBR로 돌리고
  시스템 `JAVA_HOME`은 바꾸지 않는다. `compileSdk`·`targetSdk`는 37, `minSdk`는 26이다.
- GameActivity의 네이티브 절반은 AAR의 미리 빌드된 정적 라이브러리를 luil의 CMake가 직접 링크한다.
  `abi.json`은 c++_shared·NDK 23으로 적혀 있지만, c++_static·r27d로 링크해 미해결 기호와
  `libc++_shared.so` 의존이 없는 것을 먼저 확인했다. Java 절반은 같은 핀에서 판번을 읽는다.
- APK는 Gradle(AGP 9.4.1, Gradle 9.8.0)이 싸지만 네이티브 라이브러리는 luil의 CMake가 세운다.
  luil 라이브러리는 Gradle 없이 CMake만으로 선다.
- codicon 글꼴은 빌드가 바이트 배열로 만들어 라이브러리에 싣는다. 소비자가 APK assets에 넣는
  단계가 없다.
- 안전 영역은 host가 비킨다. 앱에는 안전 영역의 크기를 알리고, `frame_state`의 원점으로 tree를
  옮겨 그리며, 배경은 표면 전체에 칠한다. 테마가 시스템 막대 배경을 투명으로 둔다.
- 모바일에는 데스크톱 caption이 맞지 않는다는 의견을 받아, 플랫폼 성질(`ui_platform`)과 모바일
  앱 바(`app_bar_element`)를 더했다 ([모바일 앱 바](app-bar-design.md)).

**다음 단계로 넘기는 것.**

- 밝은 테마에서 상태 표시줄 아이콘을 어둡게 바꾸는 일(`WindowInsetsController`, JNI)은 아직 없다.
  지금은 어두운 테마에서만 막대 아이콘이 맞는 색이다.
- 고대비와 시스템 accent(Android 12의 동적 색)는 읽지 않는다.
- 언어를 바꾸면 Activity가 다시 만들어진다(`locale`을 `configChanges`에 넣지 않았다). 글꼴
  registry의 언어는 프로세스에서 한 번만 읽으므로, 같은 프로세스에서 언어를 바꾸면 대체 글꼴이
  옛 언어로 골린다.
- 입력은 비우기만 한다 (5단계). 뒤로 가기만 Activity에 남겨 앱을 끝낸다.
- 예제의 서명은 Gradle의 debug keystore(`%USERPROFILE%\.android\debug.keystore`, 첫 빌드가 만든다)다.
  배포 서명은 8단계다.

## origin/main 위로 옮김

2026-10-02에 3단계까지의 커밋을 `origin/main`(`4860bcd`) 위로 rebase했다. 위 표의 해시는 옮긴 뒤의
값이다. upstream의 터치·펜 정비와 확대·축소 보기(5개 커밋)와 겹친 자리는 다음과 같이 맞췄다.

- upstream이 `pointer_sample`에 더한 `shift`는 core로 옮긴 `src/host/pointer_sequence`가 받고, 그 test
  ("Pen presses retain Shift through a barrel switch")는 접촉 단계로 바꿔 `pointer_sequence_tests`로 옮겼다.
- `zoom_view_tests`는 core 헤더만 써서 `luil_core_tests`에 넣었다. 기기에서도 돈다.
- element 종류는 upstream의 zoom 계열 뒤에 앱 바 둘을 두어 upstream의 값을 그대로 지켰다.
- 대화형 포인터 통합 test(`LUIL_BUILD_POINTER_INTEGRATION_TESTS`)는 공개 헤더만 써서 별칭 헤더로
  그대로 컴파일된다 (컴파일만 확인했다. 실행은 대화형 환경이 필요하다).

옮긴 뒤 Windows Release 841개, Debug 822개(asan 19개 제외), CPU 전용 Release 839개 CTest와 기기 core
test Release·Debug 각 537개가 통과했고, hello APK가 기기에서 앱 바와 함께 뜨는 것을 확인했다.
중간 커밋 하나하나는 다시 빌드하지 않았고 마지막 트리를 검증했다.

## 4단계 결과

2026-10-02에 끝냈다. 브랜치는 `android-port`이고 커밋은 로컬에만 있다.

| 커밋 | 내용 |
| --- | --- |
| `a532507` | `renderer_backend::vulkan`과 그 이름 |
| `3924901` | Android CPU 렌더러가 `resize` 없이 먼저 불려도 창 버퍼 형식을 정하고, RGBA가 아닌 버퍼에는 쓰지 않음 |
| `20e661e` | Android codicon typeface를 프로세스에 하나만 만듦 |
| `3cd3e27` | Gradle 모듈을 `app` 하나와 예제별 flavor로. debug APK에 검증 레이어를 싸는 속성 |
| `11f2072` | Vulkan 장치·렌더러, 앱 host의 기본 모드 `automatic`과 Activity 단위 물러섬, 시스템 속성으로 고르는 렌더러와 실패 주입 |
| `410b18e` | widgets를 Android APK로 |

**기기 검증.** Galaxy S22 Ultra(Android 14, Adreno 730)에서 확인했다.

- hello와 widgets가 Vulkan으로 그려진다 (logcat `renderer vulkan on Adreno (TM) 730`). CPU로 그린
  화면과 비교하면 상태 표시줄 아래 화소의 1%가 채널값 4 이내로만 다르다 (안티에일리어싱).
- 홈으로 나갔다 돌아오기와 회전을 한 번씩 묶어 hello 20회, widgets 20회에 이어 60회를 되풀이했다.
  장치는 Activity를 띄울 때 한 번만 만들어지고(`vulkan device created`) 렌더러만 창마다 섰다.
  오류는 없었다.
- 메모리(`dumpsys meminfo`): 그리는 동안 Graphics 약 118MB, 백그라운드에서 약 24MB. 되풀이하는
  동안 Native Heap이 한 번에 약 160KB씩 늘던 것을 찾아 고쳤다. 렌더러마다 codicon typeface를 글꼴
  바이트(149KB)를 복사해 새로 만들고 있었다(3단계의 CPU 경로도 같았다). 고친 뒤 widgets로 40회 되풀이하는
  동안 Native Heap이 21.6MB에서 21.9MB로 거의 그대로다. Graphics는 되풀이 한 번에 수 KB씩 는다
  (60회에 약 0.2MB, 드라이버 몫으로 보인다).
- 실패 주입: 생성 실패와 세 frame 뒤 손실에서 CPU로 물러서 그리고, 창을 새로 받아도 CPU로 남는다.
  세 경우(처음부터 CPU, 생성 실패, 손실 뒤 전환)의 화면은 픽셀까지 같다. `gpu` 모드의 생성 실패는
  Activity를 끝낸다. 손실 주입을 처음 돌렸을 때 CPU로 넘어간 첫 frame에서 SIGSEGV로 죽었다. CPU
  렌더러가 창 버퍼 형식을 정하지 않은 채 4바이트 픽셀로 써서 버퍼 끝을 넘은 것이라 고쳤다 (`3924901`).
- 검증 레이어: Khronos 1.4.363.0을 debug APK에 싸고 동기화 검증까지 켰다. 정보 수준 출력으로
  레이어가 logcat에 쓰는 것을 먼저 확인했다. 처음에는 새 스왑체인 이미지마다 한 번
  `SYNC-HAZARD-WRITE-AFTER-READ`가 나왔다. Skia의 UNDEFINED 전환 장벽이 이미지 받기 semaphore와
  이어지지 않아서였다. 처음 쓰는 이미지는 fence로 받아 CPU에서 기다리게 고친 뒤 hello·widgets의
  회전·홈·물러섬 경로에서 오류가 없다. identity 전변환을 알리는 성능 경고만 남는다.
- 회전: 가로에서 스왑체인을 한 번만 다시 세운다. identity 전변환의 레이어를 HWC가 `ROT_90`으로
  직접 합성한다(`DEVICE`). GPU 합성 비용이 없어 전변환 맞추기는 하지 않았다.
- 기기 core test Release·Debug 각 538개, Windows Release 843개, Debug 824개(asan 19개 제외),
  CPU 전용 Release 841개 CTest가 통과한다. Windows는 fence 고침과 codicon 고침 전 트리에서 돌렸고,
  그 뒤 바뀐 파일은 Android 전용이다. 중간 커밋 하나하나는 다시 빌드하지 않았고 마지막 트리를
  다시 세워 hello·widgets를 기기에서 띄웠다.

**정한 것.**

- 장치(`vulkan_device`)와 창 하나의 스왑체인(`vulkan_skia_renderer`)을 나눴다. 계획의
  `android_surface.cpp`는 따로 두지 않았다. 표면 생성은 렌더러 안의 몇 줄이다.
- 한 번 물러서면 그 **Activity**가 끝날 때까지 Vulkan을 다시 쓰지 않는다. Windows는 창 하나가
  물러서지만, Android는 창이 수시로 다시 생기므로 창 단위로 두면 손실 뒤에도 매번 다시 시도한다.
- GPU 대기는 빈 제출에 fence를 걸어 `fence_wait.h`의 예산만큼 쪼개 기다린다. 이미지 받기도 같다.
- 렌더러 경로는 명령줄 대신 `debug.luil.*` 시스템 속성으로 고른다 (`apply_debug_properties`,
  앱이 부를 때만). `debug.` 속성은 셸만 쓸 수 있다.
- 할당기 선언(`vulkan_memory_allocator.h`)은 M152의 두 `libskia.a`에서 기호를 확인했다
  ([Skia 빌드 준비](skia-build.md#android-패키지와-링크-계약)).
- 결정 6의 `parse_renderer_mode`는 그대로 `vulkan`을 받지 않는다. backend 이름(`renderer_backend_name`)만
  `vulkan`이다.

**다음 단계로 넘기는 것.**

- HWC가 회전을 못 하는 기기에서는 identity 전변환이 GPU 합성으로 내려간다. 그런 기기를 만나면
  전변환을 맞추고 캔버스를 돌려 그린다.
- 장치 손실의 실제 경로(드라이버가 `VK_ERROR_DEVICE_LOST`를 내는 경우)는 주입으로만 확인했다.
- 입력은 아직 비우기만 한다 (5단계). widgets는 보기만 하고, 레이아웃이 화면 폭보다 넓어 오른쪽이
  잘린다 (Windows용 최소 폭을 전제로 한 예제다. 8단계에서 본다).
- 기기에 남긴 것: `debug.luil.*`·`debug.vulkan.*` 속성은 빈 값으로 지웠다(재부팅하면 사라진다).
  검증 레이어(`build/vulkan-validation-layers`, Git에서 빠진다)는 다시 쓰려고 남겨 두었다.

## 5단계 결과

2026-10-02에 끝냈다. 사용자가 Windows 입력 UX도 함께 맞춰도 된다고 했고, 6단계까지 답을 기다리지 말고 진행하라고 했다. 그래서 아래 "정한 것"은 승인 없이 정한 것이다.

| 커밋 | 내용 |
| --- | --- |
| `aaa565f` | 터치 길게 누르기를 누르고 있는 동안 판정한다 (Windows·Android 공통). controller의 `next_deadline`·`advance`와 pump의 대기 |
| `0820f9b` | widgets: 섹션을 scroll_view에 담음, 토스트 버튼의 우클릭, 모바일 앱 바와 좁은 여백 |
| `6bbb4d9` | Android 입력 변환층(`input_translator`)과 host 연결. 키 필터 수정 |

**기기 검증.** Galaxy S22 Ultra에서 adb 주입으로 확인했다.

- 탭(체크박스·라디오), 길게 누르기(0.45초에는 열리지 않고 0.6초가 지나면 손을 떼기 전에 우클릭 토스트가 뜸,
  뗀 뒤 클릭이 따로 나가지 않음), 가로 화면 쓸기 스크롤, 슬라이더 끌기(30% → 85%).
- 키: 글자, Backspace, Space, Ctrl+Backspace(낱말 지우기), Tab 초점 이동.
- 마우스 탭(`input mouse tap`), 펜 탭(`input stylus tap`).
- 변환 test 9개와 터치 test(길게 누르기 4개 포함)가 기기 core test에서 통과한다.

**고친 것.**

- 움직임 이벤트의 현재 시각이 밀리초인 것을 처음에 나노초로 읽어, 누름 시각이 엉뚱하게 작아 쓸기 스크롤이
  시작하지 않았다. 원시 값을 `CLOCK_MONOTONIC`과 맞대 확인하고 고쳤다 (과거 표본과 키는 나노초다).
- 3단계의 키 필터가 볼륨·카메라·미디어 키까지 먹고 있었다. 시스템 키를 Activity에 넘긴다.
- 주입된 마우스는 버튼 동작 없이 DOWN·UP만 보내 누름이 나가지 않았다. 먼저 온 쪽이 누름이다.

**정한 것.**

- 길게 누르기는 누르고 있는 동안 연다. Windows 터치도 같은 동작이 됐다. 시간 기준은 그대로
  `long_press_time` 600ms다 (Android 시스템 기본은 400ms이고 사용자가 접근성에서 바꿀 수 있다. 시스템 값을
  읽는 것은 JNI가 필요해 미뤘다).
- 길게 누르기에 진동(haptic) 피드백은 없다.
- 마우스는 왼쪽·오른쪽 버튼과 휠만 다룬다. 가운데 버튼과 터치패드 몸짓은 다루지 않는다.

**확인하지 못한 것 (사용자가 기기를 들고 확인해야 한다).**

- S Pen 호버 툴팁, 배럴 버튼 우클릭, 펜으로 텍스트 선택. adb로 호버와 배럴 버튼을 넣을 수 없다.
- 실제 블루투스 키보드의 Ctrl 단축키. `input keycombination`은 Ctrl 상태 없이 글자를 보내 IME가 그것을
  글자로 받는다. 그래서 복사·붙여넣기는 전용 키(`KEYCODE_COPY`·`PASTE`)로 확인했다.
- 두 번 탭은 단위 test로만 확인했다 (widgets에 두 번 탭 대상이 없다).

## 6단계 결과

2026-10-02에 끝냈다.

| 커밋 | 내용 |
| --- | --- |
| `46fff30` | `text_input_host`: TSF와 Android가 같은 텍스트 입력 창구를 쓴다 |
| `fb4651e` | `focus_reveal_event`: 보이는 자리가 줄면 초점 칸을 다시 드러낸다 |
| `007814f` | Android IME(GameTextInput)와 클립보드(JNI) |

**기기 검증.**

- Gboard와 삼성 키보드에서 한글 조합, 확정(Space), 조합 중 Backspace(자모 단위로 풀림).
- Gboard에서 낱말을 두 번 탭해 고른 뒤 쳐서 바꾸기("안녕" → "가").
- 텍스트 칸에 초점이 서면 키보드가 뜨고, 다른 곳을 누르면 내려간다. 가로 화면에서 키보드가 올라오면
  가려질 칸이 키보드 바로 위로 스크롤된다.
- 하드웨어 키(`input text`)는 IME가 먼저 받아 조합한다 (Gboard 한국어에서 `gksrmf` → "한글"). 키 경로와
  겹쳐 두 번 들어가지 않는다.
- 복사·붙여넣기: 앱 안 왕복, 앱 → 설정 앱 검색 칸, 설정 앱 → 앱.
- 기기의 기본 키보드는 확인 동안 삼성 키보드로 바꿨다가 Gboard로 되돌렸다.

**정한 것.**

- IME는 계획대로 GameTextInput으로 시작했다. 조합 범위와 선택을 상태로 주어 TSF의 그림자 문서와 같은
  원리로 이었고, 직접 만든 `InputConnection`이 필요하지 않았다.
- Java thread와 UI thread 사이의 큐는 두지 않았다. glue가 상태를 UI thread에서 꺼내게 해 주므로, IME 알림만
  가로채 UI thread를 깨운다.
- 한 줄 칸으로 다룬다. 완료 단추는 Enter이고 가로 화면의 전체 화면 편집기는 끈다. 처음에는 편집기 정보를 앱 시작
  때 한 번 정해 적용되는지 알 수 없었다(Gboard 한국어 자판은 늘 "↵"로 그린다). 키보드를 띄울 때마다 정하고 IME
  연결을 다시 세우게 바꾼 뒤, Enter가 편집기 동작 6(`IME_ACTION_DONE`)으로 오는 것을 로그로 확인했다. IME가
  줄바꿈을 글로 넣는 경우를 대비해 한 줄 칸은 그것을 걷어 내고 Enter로 보낸다.
- 키보드를 BACK으로 내린 뒤 같은 칸을 다시 누르면 키보드가 다시 뜨는 것을 확인했다.
- 후보 창 자리(`CursorAnchorInfo`)는 보내지 않는다. GameTextInput이 받지 않는다. 플로팅 키보드나 손글씨
  입력에서 후보 창이 칸과 떨어질 수 있다.

**다음 단계로 넘기는 것.**

- popup 안 텍스트 칸의 IME (7단계, popup이 없다).
- 텍스트 칸을 길게 눌러 여는 복사·붙여넣기 메뉴는 popup이 필요하다 (7단계).
- 여러 줄 칸, 비밀번호·숫자 칸의 키보드 종류(`setImeEditorInfo`)는 칸의 종류를 알리는 API가 없다.

**검증 (5·6단계 마지막 트리).** Windows Release 848개, Debug 829개(asan 19개 제외), CPU 전용 Release 846개 CTest와
기기 core test Release 559개, Debug 559개가 통과한다. Windows의 `tsf_input_tests`는 고치지 않고 통과한다.
중간 커밋 하나하나는 다시 빌드하지 않았다.

## 7단계 결과 — popup

2026-10-02에 popup 절반을 끝냈다. 네트워크층은 아래에 따로 적는다.

| 커밋 | 내용 |
| --- | --- |
| `2cf63d3` | `frame_state::overlays`: 주 tree 뒤에 popup layer를 그림자·배경·tree·테두리 순으로 겹쳐 그린다 |
| `3227383` | `overlay_input_router`: 포인터를 layer로 보내고 바깥 누름·휠을 알린다. 닫힘 한 번 내기 규칙을 core로 옮김 |
| `1ae778a` | widgets: 정렬 드롭다운과 카드의 컨텍스트 메뉴, 섹션 휠 스크롤 수정 |
| `2f37f71` | Android host: popup layer, 닫힘 계기, popup이 떠 있는 동안 뒤로 가기, popup 안 IME 자리 |

**기기 검증.** Galaxy S22 Ultra의 widgets에서 adb 주입으로 확인했다.

- 드롭다운을 누르면 목록이 칸 아래에 화면 안으로 들어와 서고, "날짜순"을 고르면 칸이 바뀌고 닫힌다.
- 카드를 길게 누르면 손을 떼기 전에 메뉴가 서고, "경로 복사"를 누르면 토스트 "메뉴: 복사"가 뜬다.
- 뒤로 가기, 바깥 누름, Esc, 회전이 popup을 닫는다. 뒤로 가기로 닫을 때 앱은 그대로 있다. popup이 없을 때 뒤로
  가기는 앱을 떠난다.
- Windows에서는 UI Automation으로 드롭다운을 펼쳐 popup 창(세 항목)이 서고 접으면 사라짐을 확인했다.

**계획과 달라진 것.**

- 그리기 순서. 계획은 주 tree, popup들, tooltip·끌기 순이었다. 주 tree의 tooltip·끌기 표시는 주 tree가 그리므로
  popup이 그 위를 덮는다. 터치 화면에는 hover tooltip이 서지 않아 겹칠 일이 드물어 그대로 두었다.
- `0820f9b`가 섹션을 휠로 흘린다고 적었지만, 정책에 `on_wheel`이 없어 휠이 아무 일도 하지 않았다. `1ae778a`에서
  고쳤다.
- "빼는 것" 표의 행(WebView2·UI Automation·DirectComposition·파일 끌어 놓기 빌드 제외, caption 없음, 커서 무시)은
  2·3단계에서 이미 그렇게 됐다. 보조 창은 이번에 한 번 경고하고 무시하게 했다.

**확인하지 못한 것.**

- popup 안 텍스트 칸의 IME. 글자 자리를 popup 원점으로 옮기는 코드는 넣었지만 widgets에 그런 popup이 없다.
- 텍스트 칸을 길게 눌러 여는 복사·붙여넣기 메뉴는 아직 없다. popup은 이제 서므로 앱이나 텍스트 칸이 열 수 있다.

**검증.** Windows Release 855개, Debug 836개, CPU 전용 Release 853개 CTest와 기기 core test Release 571개가
통과한다 (adb 연결이 끊겨 실패한 1개는 다시 돌려 통과).

## 7단계 결과 — 네트워크층

2026-10-03에 끝냈다.

| 커밋 | 내용 |
| --- | --- |
| `f223c00` | WinHTTP 백엔드와 그것에만 기대는 파일을 `src/net/winhttp`로 옮김 (동작 그대로) |
| `78b1b4e` | 요청 헤더 규칙(헤더 검사, Content-Type 기본값)을 두 백엔드가 같이 쓰게 함 |
| `712c79e` | test의 되돌이 HTTP 서버가 POSIX 소켓으로도 선다 |
| `94254c8` | Android 백엔드: JNI의 `HttpURLConnection`, 앱 host가 JavaVM을 적음 |
| `d46a30b` | 기기에서 HTTP test를 JVM 안에서 돌리는 진입점(`app_process`) |
| `0cf698c` | 앱이 막은 평문 http를 보안 정책의 거절로 답함, 몸 없는 답의 연결을 끊음 |

**계획과 달라진 것.**

- **scheduler를 하나로 묶지 않았다.** 표·배달·되풀이·마감 규칙은 Android 백엔드가 Windows 백엔드의 것을 그대로
  옮겨 따로 든다. WinHTTP의 콜백·손잡이 수명과 막히는 Java 호출은 취소·정지의 모양이 달라, 묶으면 두 쪽의 불변식이
  한 파일에서 엉킨다. 같은 test 102개가 두 플랫폼에서 같은 계약을 잠근다.
- **요청마다 thread 하나다.** 막히는 `HttpURLConnection`을 쓰므로 받아들인 요청마다 worker가 끝까지 몬다. 동시 수는
  `max_in_flight`가 받기 전에 막아 pool을 두지 않았다 (숨은 큐는 `in_flight()`의 뜻을 바꾼다).
- **test는 APK가 아니라 `app_process`에서 돈다.** 계획은 loopback 서버를 POSIX로 세워 기기에서 돌린다고만 했다. JNI가
  부를 JVM이 셸 실행 파일에는 없어, test를 공유 라이브러리로 세우고 adb 셸의 `app_process`가 띄운 JVM이 읽게 했다.
  지금의 CTest 모양(test마다 한 항목, Catch2 발견, `--repeat`)이 그대로다. 그 대가로 Android test 구성에 JBR(`javac`)과
  build-tools(`d8`)가 필요하다. 셸 권한이라 INTERNET 권한과 평문 정책은 이 test가 확인하지 않는다.
- **연결이 서기 직전의 취소.** `disconnect()`는 연결이 서기 전에는 아무 일도 하지 않는다. 그 틈에 걸린 취소는 pump가
  50 ms마다, `stop()`이 기다리는 동안 다시 끊는다.
- charset은 계획대로 utf-8과 utf-16만 옮긴다. JNI가 있으므로 Java의 `Charset`으로 euc-kr 등을 옮기는 길이 열려 있다.
- TLS 1.2·1.3만으로 묶지 않았다 (플랫폼 기본값). `send_timeout`에 해당하는 것이 없다. 그 밖의 대응은
  [http-client-design.md](http-client-design.md)의 "Android 백엔드"에 있다.

**기기 검증.**

- HTTP client·몸·미디어 타입·loopback test 102개가 기기의 JVM 안에서 통과한다 (Windows와 같은 test다).
- 정지·취소 test 8개(보낸 직후 정지 1000번 되풀이 포함)를 `--repeat until-fail:30`으로 돌려 실패가 없었다.
- widgets 앱에 임시로 요청을 넣어 앱 프로세스(GameActivity) 안에서 확인하고 되돌렸다.

  | 요청 | 답 |
  | --- | --- |
  | `https://example.com/` | 200, 713 바이트 |
  | `http://example.com/` (평문, 앱 정책이 막음) | `secure_failure` |
  | `https://127.0.0.1:1/` | `cannot_connect` |
  | 만료된 인증서 (`expired.badssl.com`) | `secure_failure` (`SSLHandshakeException`) |
  | https에서 http로 가는 302 | 따라가지 않고 302가 답 |
  | https에서 https로 가는 302 | 따라가 `final_url`이 바뀜 |

  처음에는 평문 거절이 `connection_lost`로 왔다. 플랫폼이 뜻이 드러나지 않는 `IOException`을 던져서다.
  `NetworkSecurityPolicy`에 미리 물어 `secure_failure`로 답하게 고쳤다 (`0cf698c`).

**앱이 할 일.** 매니페스트에 `android.permission.INTERNET`을 넣는다. targetSdk 28 이상에서 평문 http를 쓰려면 network
security config가 필요하고, 막히면 답이 `secure_failure`다. 예제 앱은 네트워크를 쓰지 않아 권한을 넣지 않았다.

**확인하지 못한 것.**

- 연결이 서기 직전의 취소 경로는 반복 test로만 확인했다 (그 틈을 일부러 만드는 test는 없다).
- system proxy를 쓰는 환경과 Android 9 이하 기기.

**검증 (7단계 마지막 트리).** Windows Release 855개, Debug 836개, CPU 전용 Release 853개 CTest와 기기 test Release 673개,
Debug 673개(core 571, HTTP 102)가 통과한다. 중간 커밋 하나하나는 다시 빌드하지 않았고, WinHTTP 백엔드를 옮긴 두 커밋은
각각 Windows HTTP test로 확인했다.

## 8단계 진행 — 테마와 mobile demo

2026-10-03. 사용자가 8단계의 방향을 정했다.

- 예제의 패키징은 지금처럼 luil의 CMake가 네이티브를 세우고 Gradle은 싸기만 한다 (AGP `externalNativeBuild`가 아니다).
  나중에 다시 묻는다고 했다.
- 데스크톱 demo는 여러 창을 띄우는 데스크톱 앱의 시연이라 휴대폰에 옮기지 않고, 모바일 전용 예제(mobile demo)를
  새로 만든다. 되돌이 HTTP, 별도 창, 웹뷰, 그림 끌어 놓기처럼 모바일 앱의 UI와 맞지 않는 것은 뺀다.
- CI에 Android를 넣을 계획은 없다.
- 테마 일을 하고, 글꼴·IME·텍스트 처리는 미룬다.

| 커밋 | 내용 |
| --- | --- |
| `b7eff91` | 고대비에서 고른 토글과 옅은 강조 단추의 글자가 사라지던 팔레트 버그 (Windows도 같다) |
| `f0756f3` | 상태 표시줄·내비게이션 막대 아이콘, 동적 색, 대비 설정, 실행 중 어두운 모드 전환 |
| `7b84ef4` | 앱이 뒤로 가기를 받는 `ui_frame::back` |
| `847fd4c` | mobile demo 예제 (Windows에서도 선다) |

**정한 것.**

- **메인 thread 다리.** 창 장식은 Java 메인 thread에서만 바꿀 수 있다. GameActivity 4.4.2의 Java 쪽은 매니페스트의
  `android.app.func_name`을 읽지 않아(헤더 주석은 NativeActivity에서 남은 것이다), 링크의 `--wrap=GameActivity_onCreate`로
  생성 함수를 luil이 먼저 받고 메인 looper에 깨우기 fd를 건다. 소비자는 할 일이 없다 (`luil::luil`을 링크하면 따라온다).
- **어두운 모드.** glue의 `AConfiguration`은 앱이 도는 중에 어두운 모드를 바꾸면 옛 값을 준다. GameActivity가 Java
  구성에서 받아 둔 `uiMode`를 읽는다. 3단계 이래 실행 중 전환은 확인하지 않았고 이번에 고쳤다.
- **동적 색.** Android 12의 `system_accent1_500`을 시스템 accent로 세운다. 밝기는 `make_system_accent`가 정하므로 색상과
  채도만 쓰인다.
- **고대비.** Android 14의 대비 설정이 높음이면 고대비 팔레트다. 시스템 고대비 색이 없어 밝은 모드면 흰 바탕, 어두우면
  검정 바탕의 기본 색이다. 삼성 기기는 이 설정 화면을 숨겨 `settings put secure contrast_level 1.0`으로 확인했다.
- **뒤로 가기.** popup 닫기가 먼저, 그다음 frame의 `back`, 둘 다 없으면 Activity를 끝낸다. Android 16의 targetSdk 36
  이상에서도 키가 오도록 매니페스트에 `enableOnBackInvokedCallback="false"`를 둔다.
- **mobile demo의 꼴.** 600dp부터 두 판이다. 라이브러리의 label이 한 줄이라 설명을 짧은 줄로 나눴다 (텍스트 처리는
  미뤘다).

**기기 검증.** Galaxy S22 Ultra(Android 14)에서 확인했다.

- widgets: 밝은·어두운 모드를 실행 중에 바꾸면 내용과 막대 아이콘이 함께 바뀐다. 대비 높음에서 흰 바탕 고대비와
  어두운 아이콘이 나온다.
- mobile demo: 여덟 페이지를 열어 본다. dialog가 열린 페이지의 뒤로 가기가 dialog를 닫고, 다음이 목록으로, 그다음이
  앱을 떠난다. 드롭다운이 열린 동안의 뒤로 가기는 popup만 닫는다. 길게 누르면 카드 메뉴가 뜬다. 테마의 "어둡게"를 고르면
  시스템이 밝아도 막대 아이콘이 희어지고, 동적 색이 "시스템" 키 컬러로 선다. 화면 크기를 태블릿(1440×2304, 280dpi)으로
  바꾸면 두 판이 서고 고른 칸이 보인다.

**발견했지만 고치지 않은 것.**

- 밝은 테마에서 채운 강조 단추(dialog의 기본 단추, "새것" 배지)의 글자가 바탕과 대비가 낮다. 내장 accent 표의 밝은 쪽
  `accentEmphasisFg`가 채움보다 어두운 같은 색이라서다. Windows도 같고 색 설계의 문제라 사용자에게 묻는다.
- modal의 scrim이 상태 표시줄 자리는 덮지 않는다 (tree가 안전 영역에만 그려진다).

**남은 8단계 일.** Android smoke test(CTest `android` 라벨), 소비자 계약의 Android 절, 배포 서명(apksigner) 문서.
미룬 것: 글꼴(언어 바꿈 뒤 대체 글꼴), popup 안 텍스트 칸 IME 확인, 텍스트 칸의 복사·붙여넣기 메뉴, 여러 줄 label.

**검증 (이 트리).** Windows Release 856개, Debug 837개, CPU 전용 Release 854개 CTest와 기기 test Release 674개가 통과한다.

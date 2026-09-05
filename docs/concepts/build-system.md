# 빌드 체계

luil의 [CMake 구성](../../CMakeLists.txt)은 준비된 의존성을 검사하며 configure 과정에서 다운로드하지 않는다. Skia와 WebView2 SDK는 각각 [`fetch_skia.ps1`](../../scripts/fetch_skia.ps1), `fetch_webview2.ps1`로 준비한다. 직접 빌드한 Skia도 필요한 헤더·라이브러리·GN 설정을 제공하면 사용할 수 있다.

## 요구 환경과 의존성

| 항목 | 요구 사항 |
| --- | --- |
| CMake | 4.2.0 이상 |
| 플랫폼 | Windows x64 |
| 생성기 | Visual Studio 17 2022 또는 Visual Studio 18 2026 |
| 컴파일러 | MSVC 19.40 이상 |
| Windows SDK | 10.0.22621.0 이상 |
| 언어 | C++20 |

[`dependencies.cmake`](../../cmake/dependencies.cmake)는 Skia, nlohmann/json, WebView2 SDK를 모든 라이브러리 구성에서 찾는다. HTTP·웹뷰를 호출하지 않는 앱에도 이 빌드 의존성은 필요하다. Catch2는 테스트를 켰을 때 필요하다.

Skia 위치는 `LUIL_SKIA_ROOT`, Debug·Release 산출물 위치는 `LUIL_SKIA_BUILD_DEBUG`와 `LUIL_SKIA_BUILD_RELEASE`로 지정한다. 검사는 필요한 정적 라이브러리와 Direct3D·JPEG·WebP·Wuffs·Rust PNG 디코더의 GN 설정을 확인한다. WebView2 SDK 위치는 `LUIL_WEBVIEW2_ROOT`로 지정한다.

## 타깃과 옵션 전파

공개 라이브러리는 `luil`이며 `luil::luil` alias를 제공한다. `luil_usage`는 소비자가 공개 헤더를 컴파일하는 데 필요한 C++20과 MSVC `/utf-8`을 전파한다. 경고 수준·`/WX`·분석·Windows 매크로 설정을 담은 `luil_options`는 라이브러리와 저장소의 테스트·예제가 PRIVATE로 사용한다. 소비자가 `luil::luil`을 링크한다고 이 내부 경고 정책을 받지는 않는다.

최상위 빌드는 Skia의 `/MT`·`/MTd`와 맞는 정적 CRT를 설정한다. `add_subdirectory`로 포함되면 소비자의 CRT 설정을 유지하므로 소비자가 Skia와 일치시켜야 한다.

## 구성 선택

기본 빌드는 라이브러리를 만든다. `LUIL_BUILD_TESTS`, `LUIL_BUILD_EXAMPLES`, `LUIL_BUILD_TOOLING`, `LUIL_ENABLE_MSVC_ANALYZE`는 기본적으로 꺼져 있다. 테스트를 켜면 smoke 실행 파일에 필요한 예제도 함께 만든다. 설치 규칙을 켜는 `LUIL_INSTALL`은 최상위 프로젝트에서만 기본값이 켜진다.

[프리셋](../../CMakePresets.json)은 일반·테스트·분석 구성을 제공한다.

```powershell
cmake --preset vs2026
cmake --preset vs2026-tests
cmake --preset vs2026-analysis
```

tooling을 요청해도 clang-format 또는 PowerShell이 없으면 관련 형식 검사 타깃을 경고와 함께 생략한다. 라이브러리·테스트·예제의 구성은 계속된다. 실행 파일의 리소스 통합은 소비자 계약을 따른다.

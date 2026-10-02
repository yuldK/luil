# luil

luil은 Windows용 C++20 애플리케이션 프레임워크다. Skia로 UI를 그리고,
불변 element tree와 메시지 전달로 앱 상태·입력·렌더링을 연결한다.
CPU·Direct3D 렌더링, 여러 창과 팝업, 텍스트 입력, 접근성, 이미지,
비동기 HTTP와 WebView2 웹 콘텐츠를 제공한다.

## 빌드 환경

- Windows 11 x64
- Visual Studio 2022 또는 2026, MSVC 19.40 이상
- Windows SDK 10.0.22621.0 이상
- CMake 4.2 이상과 PowerShell

[CMakePresets.json](CMakePresets.json)에 라이브러리·예제·테스트 구성이 있다.
아래 명령은 저장소 루트에서 실행한다. Visual Studio 2022를 사용하면
`vs2026` 대신 대응하는 `vs2022` preset을 선택한다.

```powershell
git submodule update --init third_party/nlohmann_json
scripts\fetch_skia.ps1 -Configuration Debug,Release
scripts\fetch_webview2.ps1
cmake --preset vs2026
cmake --build --preset vs2026-release
```

의존성의 버전과 체크섬은 저장소에 고정되어 있다. CMake configure는
의존성을 내려받지 않는다. 자세한 준비 방법은 [Skia 빌드 준비](docs/skia-build.md)를 본다.
WebView2 Evergreen Runtime은 웹 콘텐츠 실행에 필요한 별도 구성 요소다.
웹뷰를 쓰지 않는 앱은 `-DLUIL_ENABLE_WEBVIEW=OFF`로 SDK 준비를 생략할 수 있다.
GPU가 필요 없는 작은 앱은 `-DLUIL_ENABLE_DIRECT3D=OFF`로 CPU 렌더러만 넣는다.
이 구성은 D3D12 장치를 열지 않고 Skia의 GPU 코드를 링크하지 않으며, 웹뷰도
기본으로 꺼진다. 자세한 내용은 [렌더링](docs/concepts/rendering.md)을 본다.

## 예제 실행

```powershell
cmake --preset vs2026-examples
cmake --build --preset vs2026-examples-debug
build\vs2026-examples\examples\Debug\luil_hello.exe
```

| 예제 | 실행 파일 | 내용 |
| --- | --- | --- |
| [hello](examples/hello) | `luil_hello` | 창과 버튼으로 구성한 최소 앱 |
| [widgets](examples/widgets) | `luil_widgets` | 컨트롤과 입력 요소 사용법 |
| [demo](examples/demo) | `luil_demo` | UI·창·이미지·HTTP·웹뷰 통합 예제 |

첫 앱의 구성은 [시작 가이드](docs/how-to-start.md)에 설명되어 있다.

## 프로젝트에 연결

소스 트리를 포함할 때와 설치 패키지를 사용할 때 모두 `luil::luil`을 링크한다.

```cmake
add_subdirectory(third_party/luil)
target_link_libraries(my_app PRIVATE luil::luil)
```

설치 패키지에서는 다음과 같이 연결한다.

```cmake
find_package(luil 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE luil::luil)
```

공개 헤더는 `luil/` 아래에 있다. 필요한 헤더를 선택하거나
`#include <luil/luil.h>`로 공개 API 전체를 포함한다.
C++20과 UTF-8 컴파일 요건은 타깃을 통해 전파된다.
앱과 Skia의 CRT 설정은 일치해야 하며, 실행 파일에는 폰트와 고지를 담은
리소스 스크립트가 필요하다. [소비자 통합 계약](docs/concepts/consumer-contract.md)에
설치·리소스·플랫폼 의존성 설정을 설명한다.

## 테스트

```powershell
git submodule update --init third_party/catch2
cmake --preset vs2026-tests
cmake --build --preset vs2026-tests-debug
ctest --preset vs2026-tests-debug
cmake --build --preset vs2026-tests-release
ctest --preset vs2026-tests-release
```

Release 테스트에는 messaging의 AddressSanitizer 검증이 포함된다.
`smoke` 레이블은 실제 창에서 CPU·Direct3D·fallback 경로를 확인하며,
`package` 레이블은 설치 위치를 옮긴 뒤 외부 소비자 프로젝트를 빌드한다.
소스 형식은 `scripts\check_source_style.ps1`로 검사한다.

터치·펜의 실제 Win32 입력 경로는 선택적으로 빌드하는 `luil_pointer_integration`으로
검증한다. 일반 `ctest`에는 포함하지 않으며, 전용 대화형 Windows 환경에서 명시적으로
실행한다. 빌드·실행 방법과 검증 범위는 [터치·펜 입력 설계](docs/touch-pen-input-design.md#uia와-네이티브-입력-자동화의-역할)를 참조한다.

플랫폼을 모르는 층(`luil_core`)은 Android arm64에서도 세우고, 그 test를 adb로
연결한 기기에서 돌린다. NDK r27d와 Ninja가 필요하다
([빌드 체계](docs/concepts/build-system.md#android)).

```powershell
scripts\fetch_skia.ps1 -Target android-arm64 -Configuration Debug,Release
$env:ANDROID_NDK_HOME = "<NDK r27d 경로>"
cmake --preset android-arm64-core -DCMAKE_MAKE_PROGRAM="<ninja.exe 경로>"
cmake --build --preset android-arm64-core-release
ctest --preset android-arm64-core-release
```

## 문서

- [문서 안내](docs/README.md): 기능별 사용법과 설계 문서
- [컴포넌트 안내](docs/components.md): 필요한 UI 요소와 관련 예제
- [스레드 모델](docs/concepts/threading-model.md): 상태 소유권과 메시지 흐름
- [불변 tree](docs/concepts/immutable-tree.md): 게시·배치·조회 규칙

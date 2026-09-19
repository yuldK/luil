# 시작 가이드

luil 앱은 상태를 소유하는 logic driver, 입력 정책, 창 delegate로 구성한다.
이 문서는 빌드 준비부터 예제 읽기, 소비자 프로젝트 연결까지 안내한다.

## 의존성 준비

저장소 루트에서 실행한다.

```powershell
git submodule update --init third_party/nlohmann_json
scripts\fetch_skia.ps1 -Configuration Debug,Release
scripts\fetch_webview2.ps1
```

Skia와 WebView2 SDK의 버전·체크섬은 `third_party`의 패키지 설정 파일에
고정되어 있다. 의존성을 준비한 뒤에는 CMake configure가 네트워크를 사용하지 않는다.
Skia를 직접 빌드해 사용하는 방법은 [Skia 준비](skia-build.md)에 있다.
웹 콘텐츠를 실행하는 환경에는 WebView2 Evergreen Runtime도 필요하다.
웹뷰를 쓰지 않는 앱은 `LUIL_ENABLE_WEBVIEW=OFF`로 구성해 `fetch_webview2.ps1` 단계를 생략할 수 있다.

## 최소 예제 실행

```powershell
cmake --preset vs2026-examples
cmake --build --preset vs2026-examples-debug
build\vs2026-examples\examples\Debug\luil_hello.exe
```

Visual Studio 2022에서는 `vs2022-examples`와 `vs2022-examples-debug`를 사용한다.

| 예제 | 읽는 순서와 목적 |
| --- | --- |
| [hello](../examples/hello) | `main.cpp`에서 창 구성을 보고 `hello_app.h`·`.cpp`에서 메시지와 화면 구성을 읽는다 |
| [widgets](../examples/widgets) | `app.h`의 앱 상태와 각 `*_section.cpp`의 컨트롤 설정을 읽는다 |
| [demo](../examples/demo) | 페이지별 UI 구성과 팝업·보조 창·HTTP·웹뷰 연결을 확인한다 |

## 앱과 라이브러리의 경계

| 구성 요소 | 주 실행 스레드 | 책임 |
| --- | --- | --- |
| `luil::win32::logic_driver` | logic | 앱 상태, 메시지 처리, 불변 tree를 담은 frame 구성, 시간 갱신과 종료 처리 |
| `luil::interaction_policy` | input | 앱 element 종류와 텍스트 대상 매핑, 휠·키·편집 요청의 해석 |
| `luil::win32::window_delegate` | UI | 창 배치 보고, 앱 UI 명령과 파일 드롭 등 플랫폼 연동 |

정책 훅은 호출 위치에 따라 UI 스레드에서도 사용된다. 공유할 데이터와
스레드별 책임은 [스레드 모델](concepts/threading-model.md)과
[소비자 계약](concepts/consumer-contract.md)을 따른다.

```text
입력 → element 액션 → 앱 메시지 → logic driver의 상태 변경
     → 새 frame과 불변 tree 게시 → UI 스레드의 그리기
```

element의 액션은 앱 상태를 직접 바꾸는 대신 `app_message`를 반환한다.
`make_message_action`은 메시지 하나를 반환하는 액션을 만드는 도우미다.
앱은 메시지를 처리한 상태로 다음 화면을 구성한다.

## 배치와 표시 규칙

- 앱 설정의 길이는 논리 픽셀을 사용한다. 배치 문맥의 배율로 실제 그리기 좌표를 구한다.
- element는 자신의 상태와 액션을 설정으로 받는다. 게시한 tree의 구조는 변경하지 않는다.
- 색은 테마 팔레트의 역할을 통해 선택한다. 글꼴·테마·고대비 설정은 표시 계층에서 적용한다. 앱 고유의 중립 색·치수는 `ui_style`로 정한다 ([테마와 글꼴](concepts/theming.md)).
- 앱 element 종류는 `application_element_kind`로, 앱 커서는 `application_cursor`로 정의한다.
- 텍스트 편집은 `apply_text_edit`와 `make_text_input_view`로 편집 상태와 표시 상태를 연결한다.

컨테이너·컨트롤·목록·오버레이 선택은 [컴포넌트 안내](components.md)를 참고한다.

## 소스 트리로 연결

```cmake
add_subdirectory(third_party/luil)
target_link_libraries(my_app PRIVATE luil::luil)
```

공개 헤더의 include 경로는 항상 `luil/`로 시작한다.
전체 API는 `<luil/luil.h>`, 개별 API는 `<luil/ui/ui_element.h>`처럼 포함한다.

앱 실행 파일에는 Codicons 폰트와 제3자 고지를 담은 리소스가 필요하다.
`luil_generate_third_party_notices`와 `luil_configure_resource_script`를 사용해
생성한 `.rc` 파일을 실행 파일 소스에 포함한다.
[예제의 CMake 구성](../examples/CMakeLists.txt)에 호출 방법이 있다.

## 설치 패키지로 연결

라이브러리를 빌드한 구성과 같은 구성으로 설치한다.

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-release
cmake --install build/vs2026 --config Release --prefix build/package
```

소비자는 설치 디렉터리를 `CMAKE_PREFIX_PATH`로 지정하고 아래 타깃을 사용한다.

```cmake
find_package(luil 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE luil::luil)
```

Skia 헤더·라이브러리와 WebView2 SDK 위치는 소비자 환경에서 제공한다.
`LUIL_SKIA_ROOT`, `LUIL_SKIA_BUILD_DEBUG`, `LUIL_SKIA_BUILD_RELEASE`,
`LUIL_WEBVIEW2_ROOT`로 지정할 수 있다. 앱과 정적 라이브러리의 CRT 구성을 맞춘다.
설치본에서 빌드하는 구성의 luil 산출물이 없으면 configure가 오류를 보고한다.

## 확인할 항목

1. hello 예제가 실행되는지 확인한다.
2. 앱이 element 액션을 메시지로 처리하고 새 frame을 게시하는지 확인한다.
3. 키보드 초점·IME·클립보드가 필요한 요소에 입력 정책을 연결한다.
4. 종료 요청에서 앱 상태 정리와 스레드 종료가 완료되는지 확인한다.
5. 배포할 실행 파일에 리소스·고지가 포함되고 필요한 런타임이 준비되어 있는지 확인한다.

빌드와 테스트 명령은 [문서 안내](README.md)와
[빌드 시스템](concepts/build-system.md)에서 확인할 수 있다.

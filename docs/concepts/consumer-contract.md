# 소비자 계약

앱은 자신의 상태와 메시지를 정의하고 세 경계로 luil에 연결한다. 최소 구성은 [`hello`](../../examples/hello/)를, 기능별 구성은 [`demo`](../../examples/demo/)를 참고한다.

| 구현 | 실행 위치 | 책임 |
| --- | --- | --- |
| `luil::logic_driver` | 주로 logic | 메시지 처리, 불변 `ui_frame` 생성, 종료와 시간에 따른 상태 변경 |
| `luil::interaction_policy` | 주로 input | 텍스트 대상 식별, 편집·조합 메시지 변환, 휠·키·초점 이동 라우팅 |
| `luil::win32::window_delegate` | UI | 앱 UI 명령, 창 배치·크기 통지, 파일 드롭 |

`app_host`가 input·logic 스레드와 채널을 조립하고, `run_application_window()`의 호출자가 UI 스레드를 맡는다. driver와 policy는 사용 중에 살아 있어야 한다. driver의 `cancel()`, `make_close_message()`, `shutdown_completed()`는 UI에서도 호출되므로 스레드 안전해야 한다. policy의 TSF·클립보드 변환 경로와 게시된 tree의 액션도 UI에서 실행될 수 있다. 메시지 변환은 앱의 mutable 상태를 직접 수정하지 않도록 작성한다.

host API(`app_host`, `logic_driver`, `ui_frame`, `ui_popup`, `ui_window`, `window_placement`, 웹뷰 값)는 플랫폼을 가리지 않으므로 [`luil/app/`](../../include/luil/app/)에 있고 네임스페이스는 `luil`이다. 옛 경로 `luil/win32/app_host.h`·`webview.h`·`renderer_policy.h`와 옛 이름 `luil::win32::logic_driver` 등은 한 판 동안 별칭으로 남는다. 별칭은 새 타입 자체를 가리키므로 기존 코드는 고치지 않고 빌드된다. 깨지는 것은 `namespace luil::win32 { class logic_driver; }` 같은 전방 선언뿐이다. 새 코드는 새 경로와 이름을 쓴다.

앱별 element kind는 `application_element_kind(index)`로 정의한다. 액션은 `make_message_action(intent)` 또는 `make_app_action(intent)`로 앱 메시지를 반환한다. input controller가 클릭을 확정하면 `logic_driver::handle(app_message)`가 상태를 바꾸고 `make_frame()`이 새 frame을 만든다. logic이 게시한 frame을 UI가 받아 다시 그린다.

## 빌드와 실행 파일 리소스

소스 트리를 포함하는 소비자는 `luil::luil`을 링크한다. 실행 파일에는 Codicons와 라이선스·제3자 고지를 담은 `.rc` 조각도 필요하다. [`examples/CMakeLists.txt`](../../examples/CMakeLists.txt)와 같은 순서로 생성한다.

```cmake
add_subdirectory(third_party/luil)
set(LUIL_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/luil/src")
set(LUIL_ASSET_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/luil/assets/codicons")
set(LUIL_NLOHMANN_JSON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/luil/third_party/nlohmann_json")
set(app_notices "${CMAKE_CURRENT_BINARY_DIR}/third_party_notices.txt")
set(app_resources "${CMAKE_CURRENT_BINARY_DIR}/luil_resources.rc")
luil_generate_third_party_notices("${app_notices}")
luil_configure_resource_script("${app_resources}" "${app_notices}")
target_sources(my_app PRIVATE "${app_resources}")
target_link_libraries(my_app PRIVATE luil::luil)
```

이 코드는 `my_app` 실행 파일이 이미 정의된 위치에서 사용한다. 소스 트리용 리소스 생성 함수가 읽는 세 디렉터리는 부모 호출 scope에도 명시한다. `LUIL_SKIA_ROOT`와 `LUIL_WEBVIEW2_ROOT`는 라이브러리 구성에서 선택한 캐시 값을 사용한다. 앱의 아이콘·manifest·버전 리소스는 별도로 더한다. 부모 프로젝트는 RC 언어를 사용할 수 있어야 하며 Skia와 같은 정적 CRT(`/MT`, Debug는 `/MTd`)를 설정해야 한다. 라이브러리를 하위 프로젝트로 포함할 때 luil은 소비자의 CRT 설정을 바꾸지 않는다.

모든 라이브러리 구성에 Skia와 nlohmann/json이 필요하다. Skia는 clang-cl로 컴파일된 것이어야 하며 configure가 패키지의 `toolchain.json`으로 확인한다. 소비자 자신의 컴파일러는 MSVC 그대로이고 clang 설치는 필요하지 않다 ([Skia 빌드 준비](../skia-build.md)). WebView2 SDK는 `LUIL_ENABLE_WEBVIEW`를 켠 구성에 필요하며, 끈 구성은 SDK 없이 빌드되고 웹뷰는 placeholder로 남는다. `LUIL_ENABLE_DIRECT3D=OFF`(CPU 전용) 구성은 웹뷰도 기본으로 끈다. 설치본의 `luil-config.cmake`는 두 값을 `LUIL_ENABLE_DIRECT3D`, `LUIL_ENABLE_WEBVIEW`로 알린다. 고지 생성기는 Skia 패키지의 `NOTICE.md`를 우선 사용한다. 직접 빌드한 Skia 소스 트리에는 Bazel 캐시의 Rust PNG crate 고지가 없으므로 생성기가 불완전한 고지를 경고한다. 배포용 고지는 이를 포함하는 패키지에서 생성한다.

## HTTP와 웹뷰

`luil::net::http_client`의 사용 여부는 앱이 정하지만 별도 선택 빌드 기능은 아니다. 응답을 앱 메시지로 옮겨 `app_host::post_app_message()`에 전달할 수 있다. 이 게시 함수는 어느 스레드에서든 호출 가능하며 포화 시 거절된 메시지는 `app_inbox_statistics()`로 관찰한다. 앱이 소유한 클라이언트는 host가 사라지기 전에 `stop()`으로 중지한다.

웹뷰는 `ui_frame::webviews`에 `ui_webview`를 넣고 tree에 같은 id를 owner로 갖는 `webview_element`를 배치한다. 비어 있지 않은 `user_data_folder`가 필수이며 신뢰 경계가 다른 페이지에는 프로필을 분리한다. 주소는 `navigate_revision`, 페이지로 보낼 JSON은 `post_revision`을 변경해 요청한다. `anchor`를 바꾸면 웹뷰가 재생성되어 페이지 상태가 사라진다.

WebView2 SDK와 별개로 소비자는 Evergreen Runtime의 설치·배포를 준비한다. DirectComposition을 제공하지 않는 CPU backend, 필요한 런타임 인터페이스의 부재, 생성 실패에서는 placeholder가 남는다. 키보드와 IME는 WebView2가 처리하며 포인터와 휠은 luil이 중계한다.

[`webview_policy`](../../include/luil/app/webview.h)의 기본 허용 스킴은 HTTPS다. 검사 대상은 앱의 주소 요청·주 문서의 탐색·새 창 요청이며 iframe 내부 탐색까지 제한하는 정책은 아니다. `open_new_windows_here`를 켜면 주 문서의 사용자 동작으로 발생한 허용 주소를 같은 웹뷰에서 연다. 권한 요청과 다운로드는 항상 거절하고 호스트 객체를 제공하지 않는다. 이를 여는 policy 스위치는 없다.

페이지 이벤트는 사후 통지다. `web_message_received`를 처리할 때 앱은 보낸 문서의 `url`을 확인해야 한다. 메시지 크기와 이벤트 빈도에는 상한이 있으며 버림은 `events_dropped`로 통지한다. 세부 설정과 프로세스 실패 동작은 [`webview.h`](../../include/luil/app/webview.h)를 따른다.

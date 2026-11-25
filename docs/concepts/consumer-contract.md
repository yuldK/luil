# 소비자 계약

> 선택 이식 범위: 이 문서의 차트·마크업·업데이트·실행 중 스타일·핫 리로드 교차 참조는 원래 전체 통합 브랜치의 설계 기록이다. 현재 main의 채택 범위와 보존 위치는 [선택 기능 이식 기록](../selected-adoption.md)에 적었다.

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

소스 트리를 포함하는 소비자는 `luil::luil`을 링크한다. 실행 파일에는 Codicons와 luil의 MIT 라이선스·제3자 고지를 담은 `.rc` 조각도 필요하다. [`examples/CMakeLists.txt`](../../examples/CMakeLists.txt)와 같은 순서로 생성한다.

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

모든 라이브러리 구성에 Skia와 nlohmann/json이 필요하다. Skia는 clang-cl로 컴파일된 것이어야 하며 configure가 패키지의 `toolchain.json`으로 확인한다. 소비자 자신의 컴파일러는 MSVC 그대로이고 clang 설치는 필요하지 않다 ([Skia 빌드 준비](../skia-build.md)). WebView2 SDK는 `LUIL_ENABLE_WEBVIEW`를 켠 구성에 필요하며, 끈 구성은 SDK 없이 빌드되고 웹뷰는 placeholder로 남는다. `LUIL_ENABLE_DIRECT3D=OFF`(CPU 전용) 구성은 웹뷰도 기본으로 끈다. 설치본의 `luil-config.cmake`는 두 값을 `LUIL_ENABLE_DIRECT3D`, `LUIL_ENABLE_WEBVIEW`로 알린다.

Skia 패키지의 기능별 아카이브를 쓰는 기능은 넷이고 모두 기본은 끈다: `LUIL_ENABLE_SVG`, `LUIL_ENABLE_TEXT_SHAPING`, `LUIL_ENABLE_AVIF`, `LUIL_ENABLE_LOTTIE` ([Skia 빌드 준비](../skia-build.md#기능별-아카이브)). 끈 기능은 아카이브를 링크하지 않고 고지에도 싣지 않으며, 공개 API는 그대로 남아 `unsupported_format`으로 답한다. 켠 기능은 기능별 아카이브가 든 패키지(win-x64 r3, android-arm64 r2 이후)를 요구한다. 설치본의 `luil-config.cmake`는 네 값을 같은 이름으로, 켠 Skia 기능의 목록을 `LUIL_SKIA_FEATURES`로 알린다. 텍스트 모양 잡기는 실행 시점에 플랫폼 ICU(Windows 10 1903, Android 12)를 열고, 없으면 모양 잡기 없이 그린다 ([SVG와 Lottie](../vector-image-design.md)).

선택 라이브러리 차트는 `LUIL_BUILD_CHART`로 켠 구성에만 있다 ([차트 라이브러리 · 보존](../selected-adoption.md#구현-완료-후-보존한-항목)). `add_subdirectory` 소비자는 옵션을 켜고 `luil::luil_chart`를 링크한다. 설치본은 `find_package(luil CONFIG REQUIRED COMPONENTS chart)`로 찾고, 차트 없이 설치한 패키지면 여기서 원인과 함께 실패한다. 설치본의 `luil-config.cmake`는 그 값을 `LUIL_BUILD_CHART`로 알린다. 헤더는 `luil/chart.h`이고 `luil/luil.h`에는 들지 않는다.

luil 자체 소스와 문서는 [MIT 라이선스](../../LICENSE)를 따른다. 고지 생성기는 소스 트리의 루트 LICENSE 또는 설치 패키지의 share/luil/LICENSE.luil에서 그 원문을 읽어 실행 파일 고지에 포함한다. Codicons와 의존성에는 각 원본의 라이선스가 적용된다. 고지 생성기는 Skia 패키지의 `NOTICE.md`를 우선 사용하고, 패키지에 `NOTICE.json`이 있으면 core와 켠 기능의 고지만 골라 싣는다. 직접 빌드한 Skia 소스 트리에는 Bazel 캐시의 Rust PNG crate 고지가 없으므로 생성기가 불완전한 고지를 경고한다. 배포용 고지는 이를 포함하는 패키지에서 생성한다.

## SMB

`LUIL_ENABLE_SMB`(Android·iOS 전용, 기본 끔)를 켠 앱은 libsmb2(LGPL-2.1)를 함께 배포한다. 그 조건은 앱이 지킨다.

- `libsmb2.so`를 앱의 네이티브 라이브러리와 **따로** APK의 `jniLibs`에 싼다. luil의 Android 예제는 CMake가 `<빌드>/jniLibs/<예제>/<구성>/<abi>`에 함께 놓는다. 앱 라이브러리에 정적으로 섞지 않는 것이 LGPL-2.1의 "사용자가 라이브러리를 바꿔 끼울 수 있다"를 지키는 길이다.
- iOS에서는 luil이 libsmb2를 앱 실행 파일에 섞지 않고 동적 프레임워크 `libsmb2.framework`(target `luil_smb2`, 설치 이름 `@rpath/libsmb2.framework/libsmb2`, Info.plist에 판 `6.2`·`MinimumOSVersion`·`CFBundleSupportedPlatforms`)로 링크한다. 켠 구성에서는 luil을 링크한 모든 앱 대상이 이 프레임워크를 번들에 실어야 한다 — 없으면 앱이 뜨자마자 dyld가 멈춘다.
  - **CMake 함수.** `add_subdirectory`로 들인 앱은 앱 대상마다 `luil_ios_embed_smb(<대상> [CODE_SIGN_IDENTITY <신원>])`를 부른다 ([`cmake/ios/embed_smb.cmake`](../../cmake/ios/embed_smb.cmake)). 실행 파일의 rpath를 `@executable_path/Frameworks`로 두고(빌드 폴더의 절대 경로를 싣지 않는다), 빌드 뒤에 `<앱>.app/Frameworks/libsmb2.framework`로 복사해 그 자리에서 서명한다. 신원을 주지 않으면 ad-hoc(시뮬레이터용)이다. SMB를 끈 구성에서는 아무것도 하지 않는다. **앱을 서명하는 빌드 뒤 단계보다 먼저 부른다** — 같은 대상의 빌드 뒤 단계는 건 순서대로 돈다. luil의 iOS 예제가 이렇게 쓴다 ([`examples/ios.cmake`](../../examples/ios.cmake)).
  - **손으로 할 때.** 자리는 `<앱>.app/Frameworks/libsmb2.framework`, rpath는 `@executable_path/Frameworks`, 서명은 안쪽부터다 — 프레임워크를 앱과 같은 신원으로 먼저 서명(`codesign --force --sign <신원> <앱>.app/Frameworks/libsmb2.framework`, 권한 파일 없이)하고 그다음 앱을 서명한다. Xcode generator를 쓰면 `XCODE_EMBED_FRAMEWORKS`(`CodeSignOnCopy`)로 같은 일을 맡겨도 된다. 보관·배포 때 Xcode가 프레임워크를 다시 서명하는 것은 상관없다.
  - **바꿔 끼우기.** 사용자는 같은 설치 이름으로 지은 `libsmb2.framework`로 `Frameworks/`의 것을 갈고 번들을 다시 서명하면 된다. App Store로 배포할 때 이 조건을 어떻게 지킬지(예: 앱의 목적 파일을 요청자에게 내주기)는 앱이 따로 판단한다.
  - **Info.plist.** `NSBonjourServices`에 `_smb._tcp`, `NSLocalNetworkUsageDescription`에 로컬 네트워크 권한 창의 글을 싣는다. 찾기와 `.local` 풀기는 시스템 DNS-SD라 멀티캐스트 권한(`com.apple.developer.networking.multicast`)은 필요 없다 ([SMB client의 iOS](../smb-client-design.md#ios)).
- 앱의 고지(설정의 오픈 소스 라이선스 화면 등)에 LGPL-2.1 전문(`third_party/libsmb2/LICENCE-LGPL-2.1.txt`)과 쓴 판(`libsmb2-6.2`)·소스 위치(https://github.com/sahlberg/libsmb2)를 싣는다. luil의 고지 생성기는 Windows 실행 파일 리소스용이라 Android·iOS에서는 앱이 직접 싣는다. iOS 프레임워크 안에도 전문(`LICENCE-LGPL-2.1.txt`)을 함께 넣어 바꿔 끼울 파일과 그 조건이 한 묶음으로 다니게 했지만, 사용자가 보는 고지 화면은 앱의 몫이다.
- luil은 libsmb2를 고치지 않고 쓴다. 고쳐 쓰려면 고친 소스도 LGPL로 공개해야 한다.
- 같은 저장소의 libdcerpc 부분은 BSD-2 라이선스다.

## iOS 앱 번들

`add_subdirectory`로 luil을 들인 iOS 앱은 앱 대상(`MACOSX_BUNDLE` 실행 파일)마다 [`cmake/ios/`](../../cmake/ios/)의 두 함수를 부른다. luil의 iOS 예제가 같은 함수를 같은 순서로 쓴다 ([`examples/ios.cmake`](../../examples/ios.cmake)).

```cmake
add_executable(my_app MACOSX_BUNDLE ...)
set_target_properties(my_app PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist")
target_link_libraries(my_app PRIVATE luil::luil)
# 1. 번들 안을 채우는 빌드 뒤 단계 (앱의 자원 복사, 다른 프레임워크 싣기)를 먼저 건다.
luil_ios_embed_smb(my_app CODE_SIGN_IDENTITY "${MY_SIGNING_IDENTITY}")   # SMB를 끈 구성에서는 아무 일도 없다
# 2. 서명은 마지막이다.
luil_ios_sign_bundle(my_app
    BUNDLE_IDENTIFIER "com.example.my-app"
    CODE_SIGN_IDENTITY "${MY_SIGNING_IDENTITY}"
    PROVISIONING_PROFILE "${MY_PROVISIONING_PROFILE}")
```

- **`luil_ios_sign_bundle(<대상> [BUNDLE_IDENTIFIER <id>] [CODE_SIGN_IDENTITY <신원>] [PROVISIONING_PROFILE <경로>])`** ([`cmake/ios/sign.cmake`](../../cmake/ios/sign.cmake)). SDK에 따라 하는 일이 다르므로 두 구성에 같은 호출을 둔다.
  - 시뮬레이터(iphonesimulator): 실행 파일의 `__TEXT,__entitlements` 구역에 앱 id와 Keychain 묶음(`LUILSIMULATOR.<번들 id>`)을 싣고 빌드 뒤에 ad-hoc 서명한다. 이 권한이 없으면 비밀 저장소가 `errSecMissingEntitlement`(-34018)로 거절된다. 서명에 실으면 시뮬레이터가 앱을 띄우지 않으므로 구역에 싣는다. 번들 id는 `BUNDLE_IDENTIFIER`, 없으면 대상의 `MACOSX_BUNDLE_GUI_IDENTIFIER`이고 Info.plist의 `CFBundleIdentifier`와 같아야 한다. 신원·profile은 쓰지 않는다.
  - 기기(iphoneos): 신원과 provisioning profile을 주면 빌드 뒤에 profile을 `embedded.mobileprovision`으로 넣고 그 권한으로 서명한다. 번들 id가 profile의 앱 id(와일드카드 포함)에 들지 않으면 그 빌드 단계가 원인과 함께 멈춘다. 신원이 없으면 서명하지 않고 configure에서 알리기만 한다 (기기에 설치되지 않는다). 없는 profile 경로는 configure에서 멈춘다.
  - 한 대상에 한 번만 부른다 (두 번째는 configure에서 멈춘다).
- **순서.** 같은 대상의 빌드 뒤 단계는 건 순서대로 돌고, 서명은 그때까지 번들에 들어온 파일을 봉인한다. 그래서 `luil_ios_embed_smb`와 앱 자신의 번들 복사 단계를 **먼저** 걸고 `luil_ios_sign_bundle`을 **마지막에** 부른다. 서명 뒤에 바뀐 파일이 있으면 기기가 설치를 거절한다. 시뮬레이터의 권한 구역은 링크 옵션이라 순서와 상관없다. 보관·배포 때 Xcode 등 다른 도구가 번들을 다시 서명하는 것은 상관없다.
- 앱이 자기 권한(App Groups, iCloud 등)을 더 실어야 하면 이 함수 대신 자기 서명 단계를 둔다. 그때도 시뮬레이터에는 앱 id·Keychain 묶음 구역이 있어야 비밀 저장소가 돈다.

## 뒤로 갈 때의 저장

모바일 OS는 앱이 뒤로 간 뒤 종료 알림 없이 프로세스를 멈추거나 거둔다. 앱은 `app_lifecycle::background` 메시지(`app_delegate::make_lifecycle_message`)를 받을 때 저장한다.

- **iOS는 host가 배경 작업 시간을 쥔다.** 장면이 뒤로 갈 때(`sceneDidEnterBackground:`) host가 `beginBackgroundTaskWithName:expirationHandler:`로 시간을 쥐고 lifecycle 메시지를 게시한 뒤, logic이 그 메시지를 처리(`logic_driver::handle`이 돌아옴)하면 놓는다. 그 사이 logic thread가 몇 초 걸려 저장해도 iOS가 앱을 멈추지 않는다 (시뮬레이터와 기기(iPad mini, iOS 27)에서 3.5초 남짓 걸리는 저장이 뒤에서 끝까지 되는 것을 보았다. 시간을 쥐지 않으면 0.3초 만에 멈췄다가 앞으로 돌아온 뒤에야 이어졌다). 앱이 빈 메시지를 돌려주면 곧바로 놓는다.
- **처리 뒤에도 이어지는 저장**(worker에 맡긴 쓰기, 업로드)은 앱이 `app_host::hold_background_work()`로 시간을 따로 쥐었다가 끝날 때 놓는다 (`background_work`, 소멸해도 놓는다). 아무 thread에서 쥐고 놓는다. 같은 원리로 앱이 쓰는 `app_host::post_app_message_holding(message, work)`도 있다.
- **만료.** 시간에는 iOS가 정한 끝이 있다 (흔히 30초 안팎, 시뮬레이터와 기기 모두 뒤로 간 지 약 26초). 만료 처리기가 불리면 host가 그 시간을 곧바로 돌려주고(돌려주지 않으면 iOS가 앱을 죽인다), 그 시간의 `background_work::expired()`가 참이 되며, `app_delegate::make_background_expired_message()`가 뒤에 머문 한 번에 한 번 불려 그 메시지를 앱 inbox에 넣는다. 그 직후 앱은 멈춘다 — 저장이 끝나지 않은 채 멈출 수 있으므로 저장은 임시 파일에 다 쓴 뒤 이름을 바꿔 다는 식으로 해서 반쯤 쓴 파일이 남지 않게 한다. logic thread에서 오래 저장하며 만료를 지켜보려면 처리 첫머리에 시간을 하나 쥐고 `expired()`를 보며 남은 일을 멈춘다 (만료 메시지는 그 처리가 끝난 뒤에야 처리된다). 만료 뒤 앱은 죽지 않고 몇 초 안에 멈추며, 앞으로 돌아오면 이어서 돈다.
- **다른 플랫폼은 아무 일도 없다.** Android·macOS·Windows는 시간을 주지 않는다 — `hold_background_work()`는 빈 값(`held()` 거짓), `post_app_message_holding`은 그냥 게시, 만료 메시지는 불리지 않는다. 앱은 플랫폼을 가리지 않고 같은 코드를 둔다.

## OS 알림

[OS 알림](../os-notification-design.md)(`show_notification`)을 쓰는 앱이 지는 것이다.

- Windows는 `window_config::identity`에 앱 식별(AUMID)을 준다. 라이브러리가 시작할 때 `HKCU\Software\Classes\AppUserModelId\<id>`와 활성화기의 `HKCU\Software\Classes\CLSID\{clsid}`를 쓰고 스스로 지우지 않는다. 앱의 제거 길에서 `close_all_notifications` 뒤 `win32::unregister_app_identity`를 부른다. 아이콘은 실행 파일 리소스가 아니라 PNG·ICO 파일의 절대 경로로 준다. 꺼진 앱의 알림을 누르면 COM이 `"<실행 파일>" -luil-notification`으로 앱을 띄우므로 앱은 `notification_launch`로 그 실행을 알아보고 인자를 파일로 읽지 않는다.
- Android는 매니페스트에 `<uses-permission android:name="android.permission.POST_NOTIFICATIONS" />`를 싣고, targetSdk 33 이상이면 `app_host::request_permission(app_permission::notifications)`으로 묻는다.

## HTTP와 웹뷰

`luil::net::http_client`의 사용 여부는 앱이 정하지만 별도 선택 빌드 기능은 아니다. 응답을 앱 메시지로 옮겨 `app_host::post_app_message()`에 전달할 수 있다. 이 게시 함수는 어느 스레드에서든 호출 가능하며 포화 시 거절된 메시지는 `app_inbox_statistics()`로 관찰한다. 앱이 소유한 클라이언트는 host가 사라지기 전에 `stop()`으로 중지한다.

웹뷰는 `ui_frame::webviews`에 `ui_webview`를 넣고 tree에 같은 id를 owner로 갖는 `webview_element`를 배치한다. 비어 있지 않은 `user_data_folder`가 필수이며 신뢰 경계가 다른 페이지에는 프로필을 분리한다. 주소는 `navigate_revision`, 페이지로 보낼 JSON은 `post_revision`을 변경해 요청한다. `anchor`를 바꾸면 웹뷰가 재생성되어 페이지 상태가 사라진다.

WebView2 SDK와 별개로 소비자는 Evergreen Runtime의 설치·배포를 준비한다. DirectComposition을 제공하지 않는 CPU backend, 필요한 런타임 인터페이스의 부재, 생성 실패에서는 placeholder가 남는다. 키보드와 IME는 WebView2가 처리하며 포인터와 휠은 luil이 중계한다.

[`webview_policy`](../../include/luil/app/webview.h)의 기본 허용 스킴은 HTTPS다. 검사 대상은 앱의 주소 요청·주 문서의 탐색·새 창 요청이며 iframe 내부 탐색까지 제한하는 정책은 아니다. `open_new_windows_here`를 켜면 주 문서의 사용자 동작으로 발생한 허용 주소를 같은 웹뷰에서 연다. 권한 요청과 다운로드는 항상 거절하고 호스트 객체를 제공하지 않는다. 이를 여는 policy 스위치는 없다.

페이지 이벤트는 사후 통지다. `web_message_received`를 처리할 때 앱은 보낸 문서의 `url`을 확인해야 한다. 메시지 크기와 이벤트 빈도에는 상한이 있으며 버림은 `events_dropped`로 통지한다. 세부 설정과 프로세스 실패 동작은 [`webview.h`](../../include/luil/app/webview.h)를 따른다.

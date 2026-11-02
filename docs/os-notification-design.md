# OS 알림

앱 창 밖, 운영 체제의 알림 자리(Windows 토스트와 알림 센터, Android 알림 창)에 알림을 올리고 바꾸고 거둔다. 사용자가 누르면 앱이 앞으로 서고 누른 알림의 id가 앱 메시지로 온다. 창 안에 그리는 [toast_element](../include/luil/ui/toast_element.h)와는 다른 기능이다. 공개 형식은 새 헤더 `include/luil/app/notification.h`의 `notification`·`notification_event`·`notification_availability`와 자유 함수 `show_notification`·`close_notification`·`close_all_notifications`·`notification_status`·`notification_launch`, `app_delegate::make_notification_message`, `app_permission::notifications`, `ui_platform_capabilities::notifications`, Win32의 `window_config::identity`(`win32::app_identity`)와 `win32::unregister_app_identity`다.

Windows 쪽은 앱 식별(AUMID)을 이 노드가 처음 세운다. [작업 표시줄 문서](taskbar-design.md) 215줄이 미룬 일이다.

## 문제

- 알림을 올리는 코드가 없다. `src/`·`include/`에 WinRT(`Windows.UI.Notifications`), `RoGetActivationFactory`, AUMID 설정이 없고, Android 쪽에도 `NotificationManager` 호출이 없다.
- 앱이 직접 짜기 어렵다. Windows는 패키지 없는 앱이 토스트를 올리려면 앱 식별을 OS에 등록해야 하고, 클릭을 받으려면 이벤트가 오는 thread 풀에서 UI thread로 넘겨야 한다. 주 창 프로시저는 공개 헤더에 없다([win32_window.h](../include/luil/win32/win32_window.h)). Android는 `PendingIntent`가 Activity를 둘 세우지 않게 짓는 법과, Java 없이 누른 알림을 알아내는 법을 앱마다 푼다.
- 권한 틀은 있으나 값이 없다. `app_permission`은 `local_network` 하나뿐이다([app_host.h](../include/luil/app/app_host.h) 296줄). Android 13부터 알림에는 `POST_NOTIFICATIONS`가 든다.
- 창 앞세우기만으로는 안 된다. `focus_request::activate`는 앞 창이 아니면 OS가 막는다(app_host.h 328줄). 알림 클릭은 OS가 주는 별도 활성화 길이다.

## 공개 형식

`include/luil/app/notification.h`, namespace `luil`. `include/luil/app`은 core 검사 범위라 OS 헤더가 없다. [platform_services.h](../include/luil/app/platform_services.h)처럼 선언은 여기, 정의는 플랫폼 층에 있다. `luil.h`가 include한다.

```cpp
namespace luil {
    struct notification
    {
        // 앱이 정한 키다. ASCII 글자·숫자와 `.` `_` `-`로 1~64자다. 같은 id로 다시 올리면 그 알림을 바꾼다.
        //  - OS의 활성화 인자로 나가고 눌림 사건이 이것만 돌려준다. 사용자 글을 담지 않는다.
        std::u8string id {};
        std::u8string title {};
        std::u8string body {}; // 비어도 된다
        // 알림 옆 그림이다. 비면 없다. 긴 변이 256 px를 넘으면 줄여 보낸다.
        ui_image image {};
        // 소리 없이 올린다.
        bool silent { false };
    };

    enum class notification_event_kind
    {
        activated, // 사용자가 눌렀다
        dismissed, // 사용자가 닫았다 (Windows만)
        failed,    // 올린 뒤 OS가 거절했다 (Windows만)
    };

    struct notification_event
    {
        notification_event_kind kind { notification_event_kind::activated };
        std::u8string id {};
        bool launched { false };       // activated: 이 클릭이 꺼져 있던 앱을 띄웠다
        std::u8string error {};        // failed: 진단 영문
        std::uint32_t lost_before { 0 }; // 이 사건 앞에서 버린 사건 수 (결정 8)
    };

    enum class notification_availability
    {
        available,
        blocked,           // 사용자·정책이 이 앱의 알림을 껐다
        permission_needed, // `app_permission::notifications`를 받아야 한다
        unsupported,       // 이 플랫폼·구성에는 길이 없다 (Windows 앱 식별이 없거나 서지 않았다)
    };

    // 모두 어느 thread에서나 부른다.
    [[nodiscard]] notification_availability notification_status();
    [[nodiscard]] platform_result show_notification(const notification& value);
    [[nodiscard]] platform_result close_notification(std::u8string_view id);
    [[nodiscard]] platform_result close_all_notifications();
    // 이 실행이 알림 클릭으로 뜬 것인가 (Windows의 활성화 표식). 앱은 이때 인자를 파일로 읽지 않는다. Android는 늘 거짓이다.
    [[nodiscard]] bool notification_launch(std::span<const std::u8string> arguments) noexcept;
}
```

- `app_delegate`(app_delegate.h 132줄 `make_permission_message` 뒤)에 `[[nodiscard]] virtual app_message make_notification_message(const notification_event& event)`를 더한다. 기본은 빈 메시지다. UI thread에서 불린다.
- `app_permission`은 끝에 `notifications`, `ui_platform_capabilities`는 끝에 `bool notifications { false }`를 더한다.
- Win32는 `window_config` 끝에 `app_identity identity {}`를 더한다.

```cpp
namespace luil::win32 {
    struct app_identity
    {
        // AUMID다. 비면 앱 식별을 세우지 않는다 (지금과 같다). 128자 이하, 공백 없음, `회사.제품` 꼴을 권한다.
        std::u8string id {};
        std::u8string display_name {}; // 알림의 앱 이름. 비면 caption.title
        std::u8string icon_path {};    // 앱 아이콘 파일(.png·.ico)의 절대 경로. 비면 적지 않는다
        bool activator { true };       // 꺼진 앱도 알림 클릭으로 띄운다 (결정 4)
    };
    // 앱 식별의 레지스트리 항목을 지운다. 앱의 제거 길에서 부른다. 없으면 `not_found`다.
    [[nodiscard]] platform_result unregister_app_identity(std::u8string_view id);
}
```

비공개 파일은 이렇다.

| 파일 | 몫 |
| --- | --- |
| `src/host/notification_queue.h`·`.cpp` (core) | 사건 대기열, 되보내기와 유실 수(결정 8). OS 헤더가 없다 |
| `src/win32/toast_payload.h`·`.cpp` | 토스트 XML을 짓는 순수 함수. 이스케이프·자르기·활성화 인자 |
| `src/win32/app_identity.h`·`.cpp` | AUMID 검증, 레지스트리 쓰기·지우기, 활성화기 CLSID, 프로세스 AUMID |
| `src/win32/notification_win32.cpp` | WinRT 호출, 이벤트 연결, 임시 그림, COM 활성화기 |
| `src/android/notification_android.cpp` | 채널·`Notification.Builder`·`PendingIntent`를 JNI로 부른다 |

## 규칙

**올리기.** 아래 순서로 본다. id가 규칙 밖이거나 글이 잘못된 UTF-8이면 `failed`이고 OS를 부르지 않는다. 제목은 256, 본문은 1024 코드 포인트에서 자르고 끝에 `…`을 단다. 이 상한은 라이브러리가 정한 것이다. `notification_status()`가 `available`이 아니면 `blocked`·`permission_needed`는 `failed`, `unsupported`는 `unsupported`다(결정 9). 같은 id는 바꾸기다. Windows는 tag(id)와 group(`luil`), Android는 tag(id)와 번호 0이다. `close_notification`은 그 하나를, `close_all_notifications`는 이 앱의 것을 모두 거둔다. 앱이 거둔 알림은 사건이 없다.

**토스트 XML.** `toast_payload`가 글로 짓고 `XmlDocument::LoadXml`로 읽는다. 템플릿은 `ToastGeneric`이고 `<text>` 둘, 그림이 있으면 `<image placement="appLogoOverride">`, `silent`이면 `<audio silent="true"/>`다. 글의 `& < > " '`는 개체 참조로 바꾸고, XML 1.0이 받지 않는 제어 문자(탭·줄바꿈 밖의 U+0000~U+001F)는 뺀다. 짝 없는 surrogate는 UTF-8 검사에서 이미 걸린다. `launch` 속성에는 `luil:<id>`만 싣는다. 그림 주소는 `file:///` 절대 경로이고 공백·`#`·`%`를 백분율 부호화한다.

**thread.** 공개 함수는 어느 thread에서나 부른다. 부른 thread에 COM apartment가 없으면 프로세스에 MTA 사용 표를 한 번 올린다(`CoIncrementMTAUsage`). UI thread의 STA(`com_sta_scope`)에서는 그대로 부른다. 이 STA에서 WinRT 활성화 팩토리가 서는지는 알려진 동작이나 실측 전이다. Windows의 `Activated`·`Dismissed`·`Failed`는 thread 풀에서 온다. 그 처리기는 사건을 `notification_queue`에 넣고 주 창에 `notification_event_message`(`WM_APP + 6`, [window_messages.h](../src/win32/window_messages.h) 표 끝)를 게시하기만 한다. UI thread가 그 메시지에서 큐를 비우며 `make_notification_message`를 부르고 `post_app_message_ticketed`로 게시한다. 앱 상태를 thread 풀에서 만지는 길은 없다. 라이브러리는 올린 `ToastNotification`을 거둘 때까지 쥐어 처리기가 살아 있게 한다.

**사건.** `Dismissed`의 까닭이 `UserCanceled`면 `dismissed`이고, `ApplicationHidden`(앱이 거둠)과 `TimedOut`(알림 센터로 옮겨 감)은 사건이 없다. `Failed`는 HRESULT 글을 실은 `failed`다. 눌림은 결정 4의 길로 온다. 눌림 사건 뒤 라이브러리는 보이는 주 창을 앞으로 세운다. 최소화는 되살린다. [인자 넘기기](single-instance-args-design.md) 결정 5와 같은 helper다. 숨은 주 창(`ui_frame::main_hidden`)은 보이게 하지 않는다. 알림 클릭이 앞으로 세울 권한을 주는지는 실측 전이다.

**앱 식별(Windows).** `identity.id`가 있으면 `run_application_window`가 `application_window`를 짓기 전에 한다(창보다 먼저여야 작업 표시줄 묶음이 맞는다). id가 규칙 밖이면 시작 실패다(잘못된 터치 설정과 같다).

1. `SetCurrentProcessExplicitAppUserModelID(id)`. 이 프로세스의 창은 모두 이 id로 묶인다.
2. `HKCU\Software\Classes\AppUserModelId\<id>`에 `DisplayName`, `IconUri`(있으면), `CustomActivator`(`activator`면)를 쓴다. 값이 같으면 쓰지 않는다.
3. `activator`면 `HKCU\Software\Classes\CLSID\{clsid}\LocalServer32`에 `"<실행 파일>" -luil-notification`을 쓴다. CLSID는 AUMID에서 이름 기반 UUID(v5)로 짓는다. 앱이 GUID를 고르지 않아 데모의 값을 베낀 두 앱이 부딪히지 않는다.

쓰기에 실패하면 시작은 이어지고 `notification_status()`가 `unsupported`, `show_notification`이 그 오류 글로 `failed`다. 라이브러리는 항목을 스스로 지우지 않는다. 앱이 꺼진 뒤에도 알림 센터의 알림이 앱을 가리켜야 해서다. 지우는 것은 앱의 제거 길(`unregister_app_identity`)이다. 실행 파일을 옮기면 다음 실행이 경로를 고쳐 쓴다. `run_application_window` 전에 `show_notification`을 부르면 식별이 아직 없어 `unsupported`다.

**COM 활성화기.** `run()`이 `on_started` 뒤에 `CoRegisterClassObject(clsid, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE)`로 `INotificationActivationCallback`을 건다. `window_environment::instance`가 있고 첫째가 아니면 걸지 않는다. 둘째가 걸면 COM이 어느 쪽으로 보낼지 모른다. host를 거두기 전에 `CoRevokeClassObject`로 푼다. UI thread STA에서 건 개체라 `Activate`가 UI thread의 메시지 펌프로 온다고 본다(실측 전). `Activate`는 `invokedArgs`의 `luil:<id>`를 읽어 큐에 넣고 곧 돌아온다. 활성화기를 걸면 `Activated`는 잇지 않는다. 같은 눌림이 두 길로 오지 않게 하려는 것이고, 두 길이 함께 오는지는 실측한다. 명령 줄에 `-luil-notification`이나 COM이 덧붙이는 `-Embedding`이 있으면 `notification_launch`가 참이고, 그 실행의 첫 눌림은 `launched`다.

**그림.** Windows는 `encode_png`([image_encode.h](../include/luil/ui/image_encode.h) 30줄)로 `%TEMP%\luil-notifications\<AUMID>\<id>-<번호>.png`에 쓴다. 같은 id를 바꾸거나 거두면 옛 파일을 지운다. 시작할 때 `ToastNotificationHistory::GetHistory`에 없는 id의 파일을 지운다. 알림 센터에 남은 알림이 그림을 잃지 않게 종료 때는 지우지 않는다. Android는 `Bitmap.createBitmap(int[], w, h, ARGB_8888)`로 `setLargeIcon`에 싣는다. 256 px 상한은 바인더 거래 크기를 넘지 않게 하려는 것이다.

**Android.** `application_context()`([java_vm.h](../src/android/java_vm.h) 27줄)로 부르므로 Activity 없이 어느 thread에서나 된다.

- 채널은 하나(`luil.default`, `IMPORTANCE_DEFAULT`)이고 첫 `show`에서 만든다. 이름은 앱 이름(`PackageManager.getApplicationLabel`)이다. `silent`는 API 31부터 `setSilent(true)`이고 그 아래는 채널 소리를 따른다.
- 작은 아이콘은 `ApplicationInfo.icon`이다. 단색 실루엣으로 그려져 모양이 뭉개질 수 있다. 앱 자원을 모르는 라이브러리가 고를 수 있는 유일한 값이다.
- 본문은 `BigTextStyle`, `setAutoCancel(true)`다.
- 누르면 `PendingIntent.getActivity(context, 요청 번호, intent, FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT)`가 뜬다. intent는 `getLaunchIntentForPackage`가 지은 런처 intent(MAIN/LAUNCHER, `FLAG_ACTIVITY_NEW_TASK`)에 extra `luil.notification.id`만 더한 것이다. extra는 intent 비교(`filterEquals`)에 들지 않아, task가 살아 있으면 Activity를 새로 세우지 않고 task를 앞으로 가져온다고 알려져 있다(실측 전). 요청 번호는 id의 해시라 알림마다 PendingIntent가 따로 선다.
- 콜드 스타트는 첫 `APP_CMD_GAINED_FOCUS`에서 한 번(`start()` 밖, 한 번 플래그) `Activity.getIntent()`의 extra를 JNI로 읽어 `launched` 눌림으로 낸다. 초점이 오기 전에는 사건이 앱에 가지 않는다. `FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY`가 있으면(최근 앱에서 다시 연 것) 읽지 않는다. 읽은 뒤 `removeExtra`로 지워 Activity가 다시 서도 같은 프로세스에서 두 번 내지 않는다.
- `notification_status()`는 `areNotificationsEnabled()`와 채널 importance가 `NONE`인지를 본다. 꺼져 있고 `POST_NOTIFICATIONS`가 필요한데 없으면 `permission_needed`, 아니면 `blocked`다. 방해 금지 중에도 알림 창에는 들어가므로 `available`이다.

**권한.** `android_permission_name`([device_request.cpp](../src/android/device_request.cpp) 46줄)에 `notifications` case를 더한다. 기기와 targetSdk가 둘 다 33 이상이면 `android.permission.POST_NOTIFICATIONS`, 아니면 `nullptr`(`not_required`)이다. 묻기와 답은 `local_network`와 같은 길이다([기기 요청](device-request-design.md)의 권한 절). targetSdk 33 미만 앱이 33 이상 기기에서 돌면 시스템이 첫 채널을 만들 때 스스로 묻는다. 이때 `permission`은 `not_required`이고 답 전까지 `notification_status()`가 `blocked`다. Windows는 늘 `not_required`이고 끈 설정은 `notification_status()`가 말한다.

**접근성·배율·테마·움직임 줄이기.** 알림은 OS가 그리고 읽는다. luil은 글과 그림만 준다. Windows의 고대비·배율은 셸이 따른다. 움직임 줄이기도 셸 몫이다.

## 결정

**1. Windows 구현 길.** 질문: 토스트를 무엇으로 올리는가.
- 길 a: Windows SDK의 WinRT `Windows.UI.Notifications`를 ABI 헤더와 WRL로 직접 부른다. 되는 것: 최소 SDK 10.0.22621.0(CMakeLists.txt 33줄)에 `winrt/windows.ui.notifications.h`·`um/NotificationActivationCallback.h`가 있다. 런타임 재배포가 없고 정적 런타임(`/MT`, CMakeLists.txt 8줄)과 맞는다. COM은 이미 `ComPtr`를 쓴다. 대가: 코드가 장황하다. 링크 목록에 `runtimeobject`가 는다.
- 길 b: Windows App SDK의 `AppNotificationManager`. 대가: 런타임 설치와 NuGet 부트스트랩이 들어 luil의 받는 방침과 맞지 않는다.
- 길 c: `Shell_NotifyIcon` 풍선. 되는 것: AUMID가 없어도 된다. 대가: 트레이 아이콘(R3 tray)이 먼저 서야 하고 그림·갱신·사건이 빈약하다.
- 권장: a. C++/WinRT 헤더는 예외로 오류를 알리고 컴파일이 무거워 ABI로 간다. 대가: 장황한 코드와 링크 하나. **권장대로 진행.**

**2. 앱 식별 등록.** 질문: AUMID·표시 이름·아이콘을 OS에 누가 어떻게 알리는가.
- 길 a: 라이브러리가 시작마다 `HKCU\Software\Classes\AppUserModelId\<id>`를 쓴다(규칙). 되는 것: 바로가기와 설치기가 없어도 빌드 폴더의 실행 파일이 토스트를 올린다. 사용자 권한만 든다. 대가: 레지스트리 항목이 남고 앱이 제거 길에서 지워야 한다. 이 길로 패키지 없는 앱의 토스트가 서고 이름·아이콘이 맞는지는 실측 전이다. `IconUri`는 이미지 파일 경로만 받는다고 알려져 실행 파일 리소스를 못 가리킨다.
- 길 b: 시작 메뉴에 `System.AppUserModel.ID`·`ToastActivatorCLSID`를 단 바로가기를 만든다. 되는 것: 오래된 문서의 길이라 Windows 10 초기 판에서도 선다고 알려져 있다. 대가: 휴대용 앱이 사용자 시작 메뉴를 어지럽히고 `propsys`·`IShellLink` 코드가 는다.
- 길 c: 설치기가 하고 라이브러리는 id만 받는다. 대가: 개발 중과 휴대용 실행에는 알림이 없다.
- 권장: a. 공통 대가로, 식별을 단 프로세스는 `SetCurrentProcessExplicitAppUserModelID`를 걸어 이전에 고정한 바로가기(속성 없음)와 단추가 갈린다. 사용자는 다시 고정한다. 대가: 남는 레지스트리 항목, 실측 전 가정 하나. **승인 필요.**

**3. 활성화기 CLSID.** 질문: COM 활성화기의 CLSID를 누가 정하는가.
- 길 a: AUMID에서 이름 기반 UUID(v5)로 짓는다. 되는 것: 같은 id면 늘 같고 앱이 GUID를 다루지 않는다. 대가: SHA-1 계산(advapi32의 CryptoAPI, 이미 링크됨)이 든다. AUMID를 바꾸면 CLSID도 바뀐다.
- 길 b: `app_identity`에 CLSID 글을 받는다. 대가: 예제의 GUID를 베끼면 두 앱이 서로의 실행 파일을 띄운다.
- 권장: a. 대가: 해시 코드 몇 줄. **권장대로 진행.**

**4. 클릭 활성화(Windows).** 질문: 앱이 꺼진 뒤의 클릭도 받는가.

| | 앱이 떠 있다 | 앱이 꺼져 있다 |
| --- | --- | --- |
| 길 a (프로세스 안 이벤트) | `ToastNotification::Activated`가 thread 풀로 온다 | 받을 것이 없다. 종료 때 이 앱의 알림을 지운다(`History::Clear`) |
| 길 b (COM 활성화기) | COM이 떠 있는 첫째의 등록 개체로 바로 보낸다. 새 프로세스가 없다 | COM이 `LocalServer32`로 새 프로세스를 띄운다. 그 프로세스가 첫째이고 `run()`의 등록 뒤 `Activate`를 받는다 |
| 길 c (프로토콜 + 인자 넘기기) | 셸이 `<scheme>:<id>`로 새 프로세스를 띄우고 그것이 `send_to_primary`로 첫째에 넘긴다 | 새 프로세스가 첫째다 |

- 길 a: 되는 것: 레지스트리 CLSID와 COM 서버가 없다. 대가: 알림 센터가 앱보다 오래 살지 못한다. 앱이 꺼져 있으면 알림이 사라진다.
- 길 b: 되는 것: 앱이 꺼져도 알림이 남고 누르면 앱이 뜬다. 떠 있으면 G7 길을 거치지 않는다. 대가: CLSID 레지스트리, 시작 표식 처리(`notification_launch`), `CoRegisterClassObject`·`CoRevokeClassObject` 수명. `Activate`가 UI thread로 오는지와 `-Embedding`이 붙는지는 실측 전이다. 첫째가 등록에 실패했거나 끝나는 중이면 COM이 새 프로세스를 띄운다. 그 프로세스가 둘째면 앱의 G7 길(`send_to_primary`)로 표식 인자가 첫째에 가고 눌림은 잃는다. 앱은 `notification_launch`로 그 인자를 걸러낸다.
- 길 c: 되는 것: G7을 그대로 쓴다. 대가: 사용자 단위 URL scheme을 등록해 웹 페이지의 링크도 앱을 띄울 수 있고, 클릭마다 프로세스가 하나 섰다 진다.
- 권장: b. G7 확정 때 "앱이 꺼진 뒤의 알림 클릭은 os_notify가 따로 진다"고 했다. 표시·바꾸기·거두기와 프로세스 안 사건(길 a의 코드)을 먼저 한 커밋으로 내고, 활성화기를 다음 커밋으로 얹는다. 활성화기 실측이 막히면 첫 커밋에서 멈추고 a로 돌아간다. 대가: 크기가 XL에 가깝다. **승인 필요.**

**5. Android 클릭 payload.** 질문: 누른 알림을 Java 없이 어떻게 아는가.
- 길 a: 앞으로 세우기 + 콜드 스타트 extra(규칙). 되는 것: 꺼진 앱은 정확한 id를 받는다. 대가: 살아 있는 앱을 누르면 앞으로 오지만 사건이 없다. 새 intent를 받으려면 `onNewIntent`가 든다. `GameActivityCallbacks`([GameActivity.h](../third_party/game-activity-prep/include/game-activity/GameActivity.h) 162~298줄)에 그 콜백이 없고 하위 클래스는 Java다. 그래서 unsupported다.
- 길 b: `getActiveNotifications`로 사라진 알림을 보고 누른 것을 짐작한다. 대가: 사용자가 밀어 지운 것과 가를 수 없다.
- 길 c: 앞으로 세우기만. 대가: 콜드 스타트의 id도 잃는다.
- 권장: a. 대가: 살아 있는 앱의 눌림 사건이 없다. **권장대로 진행.**

**6. 기능 범위.** 질문: 이번에 무엇까지 하는가.
- 길 a: 올리기·같은 id 바꾸기·거두기·무음·그림. 대가: 단추·답장·진행률·예약이 없다.
- 길 b: 단추·인라인 답장·진행률·예약까지. 대가: Android 단추·답장·삭제 감지·예약은 `BroadcastReceiver` 하위 클래스(Java)가 들어 unsupported다. 진행률은 두 플랫폼 모두 Java 없이 되지만(`setProgress`, 토스트 데이터 바인딩) 갱신 규약을 따로 세워야 한다.
- 권장: a. 진행률은 바뀌는 상태라 [작업 표시줄](taskbar-design.md)처럼 frame 선언으로 갈지 따로 정하는 편이 낫다. 대가: 위 넷이 없다. **권장대로 진행.**

**7. 그림.** 질문: 그림을 무엇으로 받는가.
- 길 a: 파일 경로만. 대가: Android는 경로를 못 쓰고(앱 파일을 읽어 다시 Bitmap으로 지어야 한다), 실행 중에 지은 그림은 앱이 파일로 써야 한다.
- 길 b: `ui_image`. Windows는 임시 PNG, Android는 Bitmap(규칙). 대가: 임시 파일 정리와 크기 상한이 든다.
- 권장: b. [작업 표시줄 오버레이](taskbar-design.md)와 같은 원천이다. 대가: 임시 파일 정리. **권장대로 진행.**

**8. 유실.** 질문: UI thread가 사건을 inbox에 넣지 못하면(reject_newest, `post_app_message_ticketed`가 0) 어떻게 하는가.
- 길 a: 큐에 되돌려 다음 `snapshot_wake_message`에서 다시 낸다. 큐가 64개를 넘으면 가장 오래된 것을 버리고 다음 사건의 `lost_before`에 센다. 되는 것: [파일 감시](file-watch-design.md) 결정 1처럼 조용한 유실이 없다. inbox가 차면 logic이 frame을 내므로 다시 낼 때가 온다. 대가: 큐와 수 하나.
- 길 b: 버린다. 대가: 눌림이 소리 없이 사라진다.
- 권장: a. Android는 콜드 스타트 사건 하나뿐이라 같은 큐를 `APP_CMD_GAINED_FOCUS`에서 다시 비운다. 대가: 큐 상한만큼 메모리. **권장대로 진행.**

**9. 막힘.** 질문: 사용자가 알림을 끈 앱의 `show`가 무엇을 답하는가.
- 길 a: `show_notification`이 `failed`이고 까닭은 `notification_status()`가 말한다. 되는 것: 앱이 "알림이 꺼져 있다"를 보일 수 있다. 대가: Windows 집중 지원은 공개 API로 읽을 수 없어 `ok`인데 배너 없이 알림 센터로만 간다.
- 길 b: 늘 `ok`이고 조회만 둔다. 대가: 앱이 조회를 잊으면 올린 줄 안다.
- 길 c: `platform_status`에 값을 더한다. 대가: 모든 플랫폼 도구의 switch가 바뀐다.
- 권장: a. Windows는 `ToastNotifier::get_Setting`이 `Enabled`가 아니면 `blocked`다. 대가: 집중 지원은 가리지 못한다. **권장대로 진행.**

**10. 공개 API 모양.** 질문: 함수인가 객체인가, 사건은 어디로 오는가.
- 길 a: 자유 함수 + `app_delegate` 훅. 되는 것: `open_url`·`make_permission_message`와 같은 모양이다. 콜드 스타트 사건처럼 앱 객체가 서기 전의 사건도 host가 들고 있다가 낸다. 대가: 프로세스 전역 상태(식별, 쥔 토스트)가 라이브러리 안에 하나 있다.
- 길 b: `notification_center` 객체 + 훅. 대가: 앱이 객체 수명을 쥐어야 하는데 식별과 활성화기는 창 수명이라 둘이 어긋난다.
- 길 c: 객체 + `deliver` 콜백([file_watcher](file-watch-design.md)와 같다). 대가: 사건이 thread 풀에서 앱 코드로 가고, 콜드 스타트 사건을 객체가 설 때까지 따로 쥐어야 한다.
- 권장: a. 대가: 전역 상태 하나. **권장대로 진행.**

## 플랫폼

### Win32

- `notification_win32.cpp`가 공개 함수를 정의한다. `IToastNotificationManagerStatics`로 `CreateToastNotifierWithId(aumid)`, `IToastNotificationFactory`로 토스트를 짓고 `put_Tag`·`put_Group` 뒤 `Show`다. 거두기는 `IToastNotificationManagerStatics2::get_History`의 `Remove(tag, group, aumid)`·`Clear(aumid)`다. 링크에 `runtimeobject`를 더한다([src/CMakeLists.txt](../src/CMakeLists.txt) 646~667줄 목록).
- [win32_window.cpp](../src/win32/win32_window.cpp)는 이 줄이 고친다. 자리는 `run_application_window`의 식별 적용, `run()`의 `on_started` 뒤 사건 받는 곳(주 창 `HWND`) 등록과 활성화기 등록, `handle_window_message`의 `notification_event_message` case와 `snapshot_wake_message`의 다시 내기, `host_.reset()` 세 자리 앞의 해제, `publish_platform()`의 `notifications` 한 줄이다. 능력은 식별이 서고 팩토리를 얻었을 때 참이다.
- 레이어드 표시와 Direct3D 합성 창은 같다. 창을 그리지 않는다.

### Android

- 모두 기존 framework 클래스 호출이라 Java 소스는 0줄이다([Android 이식 계획](android-port-plan.md)의 Java 소스 방침). `NotificationChannel`은 API 26이라 minSdk 26(examples/android/app/build.gradle.kts 40줄)과 맞는다.
- [android_app.cpp](../src/android/android_app.cpp)에서 고치는 함수: `refresh_permissions`의 고정 목록(1024줄), `permission_book`의 칸과 `state` switch(1800~1815줄), `APP_CMD_GAINED_FOCUS` 분기(760줄 이후)의 콜드 스타트 읽기와 큐 비우기, 새 함수 `take_launch_notification`이다. `start()` 안은 `set_ui_platform`의 `.notifications = true`(538줄) 한 줄뿐이다. 읽기를 `on_started` 호출(657줄) 다음에 두지 않는다. 그 자리는 줄 3 ink의 축 켜기(653~654줄) 바로 아래라 hunk가 붙는다. 538줄은 축 켜기와 115줄 떨어져 있다.
- 매니페스트는 고치지 않는다. 지금 Activity는 `launchMode`가 없다([AndroidManifest.xml](../examples/android/app/src/main/AndroidManifest.xml) 21줄). 바꾸면 모든 예제의 수명이 바뀐다. 소비자가 바꾸는 것은 소비자 몫이다. mobile demo flavor 매니페스트에만 `POST_NOTIFICATIONS`를 더한다.
- 살아 있는 앱의 눌림 사건, `dismissed`, `failed`, 단추·답장·예약은 unsupported다(결정 5·6).

## 다른 노드와의 약속

- **인자 넘기기(줄 4, 앞).** `window_config`의 식별 칸은 이 노드가 낸다(그 문서 결정 2의 전제). 알림 눌림은 `send_to_primary`를 쓰지 않는다. 결정 4 b에서 떠 있는 첫째는 COM으로 받는다. 첫째가 끝나는 중의 경합에서만 COM이 띄운 둘째가 앱의 G7 길로 `-luil-notification` 인자를 넘기고, 앱은 `notification_launch`로 걸러낸다. 그 문서의 약속 절은 이미 이 뜻으로 고쳤다. `win32_window.cpp`는 그 노드 뒤에 직렬로 고친다. `on_started` 뒤와 `host_.reset()` 세 자리 앞은 같은 hunk가 될 수 있어 그 노드의 줄 뒤에 잇는다. 앞으로 세우는 helper는 그 노드가 `advance_focus_request`에서 뽑은 것을 쓴다. 병합 순서는 인자 넘기기, os_notify, image_clip이다.
- **점프 목록.** 인자 넘기기 결정 8이 이 노드의 AUMID 실측을 보고 정한다. 실측 결과(아래 확인)는 그 결정의 근거로 쓴다.
- **작업 표시줄.** 215줄의 "os_notify의 앱 식별이 먼저 서야 한다"를 구현 커밋이 이 문서로 잇고 244줄 범위 밖의 AUMID를 지운다. 식별을 단 앱에서 진행률이 그대로 서는지 사람 확인에 더한다. `WM_APP + 6`은 이 노드가 표의 첫 새 번호다.
- **image_clip(줄 4, 다음).** `win32_window.cpp`와 `app_delegate.h`를 이 노드 뒤에 고친다. 훅은 이 노드의 것 뒤에 더한다. `notification_android.cpp`의 `ui_image`→Bitmap 변환이 필요하면 그 노드가 공용 헤더로 꺼낸다.
- **줄 3 ink·inspector.** `android_app.cpp`의 함수가 다르다. ink는 `copy_motion`과 `start()`의 축 켜기(653~654줄)를, 이 노드는 `start()`의 538줄 한 줄과 다른 함수를 고친다. 콜드 스타트 읽기가 `start()` 밖이라 ink의 줄과 hunk가 붙지 않는다. 읽기를 `start()` 안에 두어야 하면 ink가 먼저 병합되고 이 노드가 그 위에 얹는다. 줄은 `return true;` 바로 앞에 두고 `if`에 중괄호를 더하지 않는다. inspector의 `win32_window.cpp` frame 칸 한 줄은 줄 4가 통합 단계에서 별도 커밋으로 내고([인자 넘기기](single-instance-args-design.md)가 맡는다) 이 노드 커밋에 섞지 않는다.
- **G6.** 이 노드에는 라이브러리 기본 문구가 없다. 제목·본문은 앱이, 표시 이름은 앱이나 caption 제목이, 채널 이름은 앱 이름이 준다. [default-text](default-text-design.md) 규칙의 "앱이 아니면 만들 수 없는 글"이라 `default_text.h`를 include하지 않는다.
- **문서.** 구현 커밋이 [기기 요청](device-request-design.md) 권한 절에 알림 권한 한 문단, `docs/concepts/consumer-contract.md`에 매니페스트 `POST_NOTIFICATIONS` 한 줄을 더한다. `platform-services-design.md`는 고치지 않는다.
- **데모 공용 파일.** `examples/demo/toasts_page.*`의 정보 막대 카드는 R1에서 이미 들어갔고(`make_info_bar`), 이번 라운드에 이 페이지를 고치는 줄은 이 노드뿐이다. `examples/demo_main.cpp`·`examples/mobile_demo/device_page.cpp`는 여러 줄이 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고 이 두 파일의 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인도 통합 때 붙인다.

## 확인

- `tests/toast_payload_tests.cpp`(`luil_tests`): `& < > " '` 이스케이프, 제어 문자 빼기, 탭·줄바꿈 유지, 제목·본문 자르기가 코드 포인트 경계와 `…`, `launch`에 `luil:<id>`만 실리는지, id 규칙(65자, 공백, 한글, 빈 값은 거절), `silent`의 audio 요소, 그림 경로의 `file:///`와 공백·`#`·`%` 부호화.
- `tests/app_identity_tests.cpp`(`luil_tests`): AUMID 검증(128·129자, 공백), 레지스트리 경로와 값 구성, CLSID가 같은 id에서 같고 다른 id에서 다른지와 UUID v5 판·변형 비트. 등록·해제 왕복은 `luil.test.<pid>` id로 HKCU에 쓰고 test가 지운다. 시작할 때 남은 `luil.test.*`를 먼저 지운다. 값이 같으면 다시 쓰지 않는지, 없는 id의 해제가 `not_found`인지.
- `tests/notification_queue_tests.cpp`(core): 순서 유지, 거절된 사건을 되돌려 다음에 먼저 내는지, 64개 상한에서 오래된 것을 버리고 `lost_before`에 세는지, 되보낸 뒤 수가 0으로 돌아오는지.
- `tests/device_request_tests.cpp`(core, 추가): `notifications`가 API·targetSdk 32·33의 네 조합에서 33·33만 `POST_NOTIFICATIONS`인지. 기본 능력 단언에 `notifications == false`.
- `tests/win32_window_tests.cpp`(추가, `run_placement_probe` 틀): 비공개 진입점으로 다른 thread에서 사건을 넣으면 delegate 훅이 창을 만든 thread에서 불리는지, 숨은 주 창이 보이지 않는지, `identity`가 규칙 밖이면 시작 실패인지. 토스트를 실제로 올리지 않는다.
- 데모 `examples/demo/toasts_page.cpp`의 "OS 알림" 구역: 단추 `OS 알림 보내기`·`OS 알림 바꾸기`·`OS 알림 거두기`, label `OS 알림 상태: 사용 가능`, `OS 알림 결과: ok`, `OS 알림 사건: 없음`(받으면 `OS 알림 사건: 눌림 demo-1 · 꺼진 앱에서` 꼴). `demo_main.cpp`가 `identity`(id `luil.Demo`, 아이콘은 데모 assets의 PNG)를 싣고 훅을 페이지 intent로 넘긴다. `--unregister-app-identity` 플래그는 `unregister_app_identity`를 부르고 끝낸다. `tests/` 쪽 페이지 test가 intent를 직접 주어 label을 본다.
- 모바일 데모 `device_page.cpp`의 "알림" 구역: 권한 상태 label(`로컬 네트워크 권한`과 같은 꼴), `알림 권한 묻기`·`알림 보내기` 단추, `알림으로 시작: 아니오`(콜드 스타트면 id) label. `tests/mobile_demo_page_tests.cpp`가 상태별 이름을 읽는다.
- 실측 전 가정과 재는 법(구현 단계, 사용자 허락 뒤):
  - 레지스트리 AUMID만으로 빌드 폴더의 데모가 토스트를 올리는가. `ToastNotificationHistory::GetHistory(aumid)`의 수를 되읽고, 이름·아이콘은 사람이 본다.
  - `Activated`가 활성화기 유무에 따라 어느 길로 오는가, `Activate`가 UI thread로 오는가, `-Embedding`이 붙는가. 데모가 thread id와 명령 줄을 로그로 남긴다.
  - 클릭이 앞으로 세울 권한을 주는가. 다른 앱을 앞에 두고 누른다.
  - Android 런처 intent PendingIntent가 Activity를 하나로 두는가. 실기기에서 `dumpsys activity activities`의 GameActivity 수를 센다. 채널·제목·contentIntent는 `dumpsys notification --noredact`, 권한은 `pm grant`·`pm revoke`, 콜드 스타트 extra는 `am force-stop` 뒤 알림을 사람이 누른다.
- 격리 데스크톱에는 셸이 없어 토스트가 그려지지 않는다고 본다(추정). 알림 서비스는 세션 단위라 격리 데스크톱에서 올린 토스트가 사용자 화면에 뜰 수도 있다(추정). 그래서 자동 test는 `Show`를 부르지 않고, UIA 되읽기는 데모 label의 결과까지다. 배너·알림 센터·클릭은 사용자 데스크톱에서 허락을 받고 본다.
- 사람 확인: 배너와 알림 센터의 모양, 같은 id 바꾸기, 거두기, 끈 설정과 집중 지원의 결과, 꺼진 뒤 알림 센터에서 누르기, 관리자 권한 실행, 식별을 단 앱의 작업 표시줄 진행률과 고정 단추. Android 실기기의 알림 창·잠금 화면, 권한 대화상자(허용·거절·다시 묻지 않음), 살아 있을 때 누르면 같은 Activity가 앞으로 오는지.

## 범위 밖

- 단추·인라인 답장·진행률·예약·배지 숫자(결정 6). 여러 채널과 채널 설정 화면.
- Android의 살아 있는 앱 눌림 사건과 닫힘 감지(Java 필요).
- 앱 식별의 자동 정리, 시작 메뉴 바로가기, 설치기 연동.
- 실행 파일 아이콘을 `IconUri`용 파일로 뽑는 일. 앱이 파일을 준다.
- 집중 지원 상태 읽기. 공개 API가 없다.
- 점프 목록(인자 넘기기 결정 8).

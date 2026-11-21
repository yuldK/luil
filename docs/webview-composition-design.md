# WebView2 composition hosting

Luil은 WebView2 composition controller를 Skia가 그리는 tree의 한 slot에 배치한다. Application은 `ui_frame::webviews`로 controller의 수명과 policy를 publish하고, 같은 id의 `webview_element`를 anchor surface tree에 배치한다.

공개 계약은 [`include/luil/app/webview.h`](../include/luil/app/webview.h)와 [`include/luil/ui/webview_element.h`](../include/luil/ui/webview_element.h)에 있다. Hosting은 [`src/win32/webview_host.h`](../src/win32/webview_host.h)와 [`src/win32/webview_host.cpp`](../src/win32/webview_host.cpp), layout 판단은 [`src/host/webview_layout.h`](../src/host/webview_layout.h)에 구현되어 있다.

WebView 기능을 사용하려면 WebView2 SDK로 build해야 하며 runtime에는 Evergreen WebView2 Runtime build 2210 이상이 필요하다. Runtime이나 필요한 interface가 없으면 slot은 placeholder를 유지하고 application에 `creation_failed` event를 보낸다.

## Frame 계약과 reconciliation

`ui_webview`는 안정적인 id, anchor surface id, user-data folder, navigation 및 message revision, policy, event callback을 가진다. Main surface는 빈 anchor id를 쓰며 secondary window는 해당 `ui_window::id`를 쓴다.

Platform은 frame마다 다음 규칙으로 live controller를 맞춘다.

- 새 id는 environment와 composition controller 생성을 시작한다.
- 기존 id는 최신 policy와 callback을 적용하고 새 revision의 command를 실행한다.
- 사라진 id는 controller를 닫고 visual을 제거한다.
- 같은 id의 anchor 또는 user-data folder가 바뀌면 controller를 다시 만든다.

Environment는 정규화된 user-data folder별로 공유한다. 서로 다른 folder는 별도 profile과 environment를 사용한다. 빈 folder는 허용하지 않는다. Profile data를 둘 위치와 수명은 application이 명시적으로 정해야 한다.

`webview_element`가 tree에 없거나 id와 anchor가 맞지 않으면 controller는 숨겨진다. Element는 독립적인 native child window가 아니며, immutable tree가 controller를 직접 소유하지 않는다.

## DirectComposition tree

각 Direct3D surface의 composition target은 다음 순서를 가진다.

```text
root
├── underlay
│   └── 아래층 WebView visuals
├── Skia swap-chain visual
└── overlay
    └── 위층 WebView visuals
```

`ui_webview::layer`로 웹뷰마다 `webview_layer::underlay`와 `webview_layer::overlay`를 고른다. 아래층을 기본값으로 두어 기존 앱의 가림·모서리 합성을 보존한다. 위층은 처음 표시할 때 창 뒤가 잠깐 비쳐서는 안 되는 사각 슬롯에 적합하다. 같은 id의 층을 바꾸면 컨트롤러와 페이지를 유지하며 부모 visual을 바꾼다. Composition swap chain은 `DXGI_ALPHA_MODE_PREMULTIPLIED`를 사용하므로 alpha가 0인 Skia pixel에서 아래 WebView가 보이고 opaque pixel은 WebView를 덮는다.

Direct3D renderer는 flip-model swap chain을 `HWND`에 직접 붙이지 않고 `CreateSwapChainForComposition`을 사용한다. 따라서 presentation이 DirectComposition target 안에 머물고 CPU renderer로 돌아가기 전에 target을 안전하게 detach할 수 있다.

DirectComposition device는 application 전체에서 공유한다. 각 `HWND`는 자체 target, root, 두 컨테이너, Skia visual, swap chain을 가진다. 각 WebView는 anchor surface에서 고른 컨테이너의 자식이다. 별도 HWND인 popup과 소유 창은 이 tree 전체 위에 설 수 있다.

Direct3D가 실패해 CPU로 fallback하면 GDI presentation 전에 기존 composition target을 detach한다. CPU·레이어드 표면에는 합성 층이 없으므로 WebView를 숨기고 slot에는 placeholder를 그린다.

## Slot, clipping, transparency

Element는 arranged physical bounds와 arrange에 사용한 scale을 보관한다. Host는 `ui_tree::visible_bounds`를 사용해 ancestor clipping과 scroll viewport가 반영된 rectangle을 얻는다.

`plan_webview_layout`은 visible rectangle을 정수 physical pixel로 반올림하고 surface client rectangle과 교차시킨다. 비었거나 완전히 clip되면 controller를 숨긴다. DirectComposition visual offset이 위치를 정하고 visual-local clip이 page pixel을 slot 밖으로 나가지 못하게 한다. Controller bounds는 `(0, 0)`에서 시작하는 width와 height만 제공한다.

자리표는 두 방식 모두 매번 바탕과 placeholder를 그린다. **위층**에서는 그 픽셀을 그대로 두고 WebView2가 실제로 제출한 페이지 픽셀을 위에 합성한다. 빈 visual은 투명하지만 컨트롤러의 초기 빈 문서가 먼저 단색 면을 제출할 수 있다. 실측에서 위층을 즉시 보인 구현은 placeholder 다음에 단색 슬롯이 3장 잡혔다. 위층은 첫 항해가 끝날 때까지 기본 바탕을 투명하게 두고 컨트롤러를 숨긴다. 숨긴 동안에도 자리의 크기와 배율은 먼저 준다. 첫 문서는 보이기 전에 열리므로, 주지 않으면 0×0 뷰포트에서 load되어 그때 크기를 재는 페이지가 0으로 초기화한다. 첫 `NavigationCompleted`에서 페이지용 불투명 바탕을 복구하고 표시를 허용한다. 이 사건 뒤에 합성 픽셀이 늦어져도 아래에는 자리표가 그대로 있다. 이후 항해는 그때마다 숨기지 않는다. 다시 보일 때 브라우저가 보존한 직전 페이지 픽셀이 새 항해보다 먼저 보일 수 있다.

**아래층**은 기존처럼 controller가 보이고 첫 `NavigationCompleted`를 받은 뒤 Skia 슬롯을 알파 0으로 비운다. 이 사건은 첫 합성 그림보다 먼저 올 수 있어 그 사이 슬롯에 창 뒤가 비치는 결함이 남는다. GDI 면을 바탕색으로 칠하는 완화책은 backdrop을 쓰는 창에서 효과까지 가린다. 컨트롤러의 기본 바탕색도 브라우저가 첫 그림을 낸 뒤에 보인다.

가림 판정은 슬롯 중앙과 안쪽 네 모서리의 hit test다. 한 점이라도 다른 요소에 닿으면 아래층은 구멍을 막고, 위층은 `put_IsVisible(FALSE)`로 웹뷰 전체를 숨긴다. 부모 visual은 붙인 채 유지해 페이지 수명과 스크롤을 보존한다. 표본이 다시 모두 자리표에 닿으면 같은 컨트롤러를 보인다. 두 층 모두 가림·숨김 전에 포인터 capture를 취소한다.

표본 사이의 가는 요소와 hit test에 잡히지 않는 초점 테두리·그림자는 이 판정에서 빠질 수 있다. 위층에서는 이런 Skia 픽셀이 페이지 뒤로 간다. visual의 clip은 사각형이므로 Skia가 그린 둥근 모서리도 위층 페이지를 자를 수 없다. 둥근 마스크나 부분 겹침을 Skia 픽셀로 표현하는 슬롯은 아래층을 선택한다.

한 frame에 아래층의 실제 구멍이 하나라도 있으면 LCD subpixel 글꼴을 끄고 grayscale antialiasing을 쓴다. 위층만 있는 frame은 구멍 목록이 비므로 원래의 RGB pixel geometry를 유지한다.

### 떠 있는 판에 붙인 웹뷰

자리표는 tree의 어느 자리에나 설 수 있다. 다른 내용 위에 겹쳐 놓은 판(`layer_element`에 나중에 담은 `panel_element`) 안에 자리표를 두면 웹뷰가 그 판과 함께 내용 위에 떠 있다. 판은 창이 아니라 같은 tree의 element라 창 client 밖으로 나가지 못하고, 웹뷰는 자리표가 배치된 자리를 frame마다 따라간다. 두 층 모두에서 선다.

- 자리표는 판의 테두리 안쪽에 둔다. 자리표가 테두리까지 덮으면 아래층의 구멍이 그 자리의 테두리 픽셀을 지우고, 위층은 페이지가 덮는다.
- 판의 바탕·제목·테두리·그림자는 자리표 밖이라 그대로 보인다. 자리표 **안**에 겹쳐 그린 요소는 어느 층에서도 페이지 위에 보이지 않는다.
- 판 위에 다른 요소가 와서 가림 표본에 닿으면 위의 가림 규칙대로 구멍이 닫히거나 웹뷰가 숨는다.

데모의 ‘웹뷰’ 페이지에서 ‘웹뷰를 본문 위에 떠 있는 판에 붙인다’를 켜면 자리표가 본문에서 판으로 옮겨 가고, 어느 탭에서든 판이 본문 위에 떠 있다. ‘판을 왼쪽에 둔다’·‘판을 위에 둔다’로 네 모서리를 오간다.

## DPI 계약

Tree bounds와 pointer coordinate는 physical pixel이다. Composition controller는 `COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS`를 사용하므로 bounds와 `SendMouseInput`의 단위가 같다.

Luil은 WebView2의 monitor scale 자동 감지를 끄고 `RasterizationScale`을 직접 설정한다. CSS pixel은 physical pixel을 arranged scale로 나눈 값이다. Scale은 rectangle을 제공한 같은 `webview_element`에서 가져오며 별도 surface DPI sampling을 하지 않는다.

DPI transition 중 이미 published된 tree는 이전 scale을 가질 수 있다. Luil은 tree scale이 target surface와 맞지 않으면 그 rectangle을 적용하지 않는다. Application이 새 DPI로 arrange한 tree를 publish할 때까지 이전 rectangle을 유지한 뒤 scale과 size를 함께 적용한다. Size와 scale이 어긋난 두 번의 page reflow를 방지한다.

## Pointer 입력

Composition controller 위에는 일반 child window가 없으므로 mouse message는 Luil surface로 온다. `window_surface`는 element tree에 event를 post하기 전에 지원하는 message를 `webview_host`에 먼저 제공한다.

Host는 move, left/right/middle press와 release, double-click, vertical 및 horizontal wheel을 `ICoreWebView2CompositionController::SendMouseInput`으로 보낸다. Surface-client coordinate를 WebView-local physical coordinate로 바꾸고 button, modifier flag와 wheel delta를 보존한다.

Visible WebView는 half-open rectangle 안의 pointer input을 소비해 뒤의 list나 button이 같은 event를 받지 않게 한다. Hidden 또는 occluded WebView는 소비하지 않는다. Press가 WebView 안에서 시작되면 기록된 button capture가 rectangle 밖의 movement와 release도 같은 WebView로 보낸다. Capture loss는 page 밖 release를 합성해 WebView state를 정리한다.

누른 button이 없으면 pointer leave를 전달한다. Win32 pointer capture는 계속 Luil surface가 소유하므로 native tree interaction과 composition content에 capture mechanism 하나를 쓴다.

## Keyboard focus와 text input

`webview_element`는 tab stop이며 hit-opaque지만 Luil focus trap은 아니다. Logical focus가 처음 slot에 오면 host가 `MoveFocus`를 호출해 page focus sequence에 진입한다.

Page 안에서는 WebView2가 keyboard, character, accessibility, IME를 소유한다. Luil은 일반 key를 재전송하지 않는다. `GotFocus`와 `LostFocus`는 pointer로 진입한 경우까지 browser controller와 slot의 logical focus를 동기화한다.

Page의 Tab traversal이 끝에 닿아 `MoveFocusRequested`가 오면 같은 방향으로 다시 이동해 WebView 안에서 focus를 순환한다. `Ctrl+Tab`과 `Ctrl+Shift+Tab`은 다음 또는 이전 Luil focus target으로 나가는 명시적 경로다. Escape는 surface의 일반 dismiss 경로로 전달한다. Alt+F4는 처리하지 않아 top-level window가 닫을 수 있게 한다.

WebView child가 Win32 focus를 가지는 동안 anchor surface는 input controller에 focus loss를 보고하지 않고 TSF text document를 일시적으로 양보한다. Luil과 WebView2가 같은 STA에서 IME를 두고 경쟁하지 않게 한다. Focus가 surface로 돌아오면 별도 logical focus transition을 만들지 않고 TSF session을 다시 활성화한다.

## Command와 event

Frame은 반복 publish되므로 navigation과 host-to-page message는 revision counter를 쓴다. Revision이 바뀔 때만 명령을 실행한다.

- 0이 아닌 `navigate_revision`은 `navigate_url`을 연다.
- 0이 아닌 `post_revision`은 `post_message`를 `PostWebMessageAsJson`에 전달한다.

비동기 controller 생성 전에 온 command는 준비될 때까지 보관한다. 같은 revision은 반복 실행하지 않는다. `post_message`의 JSON 문법은 application 책임이다.

Page-to-host message는 `window.chrome.webview.postMessage`에서 JSON으로 온다. `web_message_received`는 serialize된 JSON과 source document URL을 포함한다. Trusted state를 바꾸기 전에 application이 message shape와 origin을 모두 검사해야 한다.

`on_event`는 `webview_event`를 `app_message`로 바꿔 logic thread에 post한다. Event는 platform decision 뒤의 알림이다. Navigation, permission, new-window 판단은 WebView2 callback 안에서 동기적으로 해야 하므로 application policy를 미리 publish한다.

Event는 navigation 시작·완료·실패·차단, 거부된 permission, 차단된 download, 수신 message, dropped event, renderer 또는 browser process failure, 생성 실패를 포함한다.

## 생성 실패

WebView를 세우지 못하면 `creation_failed`를 WebView마다 한 번 보낸다. Application이 runtime 설치 여부와 version을 따로 조회하지 않고도 사용자에게 설치나 갱신을 안내할 수 있게 하려는 것이다. `failure`가 갈래를, `hresult`가 원인 HRESULT를, `error`가 사람이 읽는 설명을 담는다.

| `failure` | 판정 | `hresult` |
|---|---|---|
| `runtime_missing` | Environment 생성이 `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)`를 돌려준다. Static loader가 runtime을 찾지 못한 경우다. | 그 값 |
| `runtime_too_old` | Browser version의 세 번째 수가 2210보다 작거나 composition controller interface가 없다. `error`에 찾은 version과 필요한 build가 있다. | 0 |
| `environment_failed` | 그 밖의 environment 생성 실패다. User data folder에 쓸 수 없거나, 같은 folder를 다른 option의 environment가 쓰고 있는 경우가 흔하다. | 그 값, 없으면 0 |
| `controller_failed` | Environment는 섰지만 composition controller나 core view를 얻지 못했다. | 그 값, 없으면 0 |
| `not_built` | `LUIL_ENABLE_WEBVIEW=OFF` build의 stub host다. | 0 |

`tests/webview_failure_tests.cpp`는 두 id를 같은 실패 프로필로 여러 번 게시해 실제 host와 stub에서 id마다 한 번씩 `creation_failed`를 받는지 확인한다. 두 id를 뺀 뒤 다시 실으면 각 수명의 첫 실패를 다시 받는다.

Environment 생성이 동기적으로 실패하면 실패 원인을 environment에 남겨, 같은 frame에서 그 folder를 쓰는 WebView도 같은 원인을 받는다. 알림은 생성 중이던 항목이 실패로 바뀔 때만 나간다. 이미 선 WebView의 browser process가 죽은 경우는 `browser_process_failed`가 알리므로 `creation_failed`를 다시 보내지 않는다. 이 event는 page가 일으킨 것이 아니므로 rate limit을 지나지 않는다. Application이 id를 목록에서 뺐다가 다시 publish하면 다시 시도하고, 또 실패하면 또 알린다.

## Policy와 trust boundary

`webview_policy` 기본값은 script 활성화, developer tool 비활성화, 기본 context menu 비활성화, script dialog 활성화, browser accelerator key 비활성화, pinch zoom 비활성화, swipe navigation 비활성화다. Policy는 frame마다 갱신한다. Event filter 값은 즉시 적용되며 WebView2 setting은 runtime의 next-navigation 의미에 따라 적용된다.

`allowed_schemes` 기본값은 HTTPS뿐이다. 비교는 case-insensitive이며 host-requested navigation, top-level link와 redirect, 허용된 new-window request에 적용한다. `javascript:`는 항상 거부한다. Main-document scheme filter는 현재 iframe navigation을 다루지 않는다.

새 browser window는 만들지 않는다. `open_new_windows_here == false`이면 모두 차단한다. True이면 main document에서 user gesture로 시작한 요청만 기존 WebView를 navigate할 수 있고 destination도 scheme filter를 통과해야 한다. Iframe 또는 user gesture 없는 요청은 차단한다.

모든 permission은 거부하고 profile에 저장하지 않는다. 모든 download는 save dialog 전에 취소한다. Host object는 비활성화한다. Certificate failure 무시, host-object proxy 활성화, download나 permission 허용, SmartScreen 비활성화, input 제한 제거를 위한 policy switch는 없다.

## 표면 표시 가능 여부

`ui_webview::report_presentation`을 켜면 준비된 컨트롤러마다 `presentation_changed`가 첫 값과 변경을 알린다. `webview_event::presentation_available`은 표면에서 선택한 층과 웹뷰의 합성 visual로 판정한다. CPU·레이어드 표시와 GPU에서 CPU로 물러난 표면에서는 거짓이다. 자리표가 tree에서 빠지거나 가려져도 이 값은 같다. 페이지 사건의 초당 상한은 적용하지 않는다.

## Event backpressure

Page event는 application의 bounded message 경로를 공유한다. `maximum_message_bytes` 기본값은 256 KiB이며 web message 하나의 UTF-8 JSON 크기를 제한한다. `maximum_events_per_second` 기본값은 120이며 web message, navigation event, permission request, download attempt를 포함한다.

Rate window는 첫 event부터 1초간이다. Limit 0은 모든 gated event를 버린다. Window에서 처음 버린 event만 size 또는 rate 원인을 담은 `events_dropped` 알림 하나를 만들고, 이후 drop은 inbox flood를 막기 위해 조용히 처리한다. Process-failure event와 drop notice는 일반 gate를 우회한다. Host-to-page message는 trusted application이 만들므로 제한하지 않는다.

## Process failure

Main render process 종료는 `render_process_failed`를 보내고 page를 한 번 reload한다. 10초 안에 다시 종료되면 알리기만 하고 reload하지 않아 crash-reload loop를 막는다. Iframe render process 종료는 main document를 reload하지 않는다.

Browser process 종료는 `browser_process_failed`를 보내고 WebView를 failed로 표시해 reconciliation에서 정리한다. 새 controller가 필요하면 application이 id를 제거한 뒤 다시 publish한다. GPU와 utility process failure는 WebView2 recovery에 맡긴다.

## 수명 규칙

Host와 controller는 모두 UI-thread object다. 비동기 생성 callback은 shared alive flag와 creation serial을 capture한다. Shutdown, 제거, 교체 뒤 도착한 callback은 orphan controller를 닫고 죽은 host에 접근하지 않는다.

Host는 WebView가 붙어 있는 동안 선택한 부모 visual의 COM reference를 보유한다. 그렇지 않으면 renderer 파괴가 WebView teardown보다 먼저 visual을 해제할 수 있다. 숨길 때 root visual을 detach하지 않고 `IsVisible`을 사용해 다음 frame까지 page content를 보존한다.

Shutdown은 future callback을 먼저 무효화하고 event delivery function을 지운 뒤 child visual 제거, controller close, environment release 순서로 진행한다. WebView event가 파괴된 `app_host`를 delivery closure로 붙잡지 못하게 한다.

## 접근성

Slot은 Luil UI Automation tree에서 이름 있는 group으로 나타나고 일반 focus traversal에 참여한다. Luil은 DOM node나 page text를 `access_info`로 복제하지 않는다. Page의 접근성 subtree는 slot의 자식으로 선다.

### Page가 slot 아래에 서는 방식

Composition hosting의 WebView2는 앵커 창 아래에 크기 0의 자식 창(`Chrome_WidgetWin_0`, WebView2 문서의 "input HWND")을 두고, page의 UIA tree를 그 창의 HWND 가지로 내준다. 아무것도 하지 않으면 UIA3 client(Narrator 등)에게 그 가지가 **창의 첫 자식**으로 선다. Page 내용은 읽히지만 slot과 따로 놀고, 화면 차례와 다른 자리에 선다. UIA2 관리 client(`System.Windows.Automation`)는 크기 0의 자식 창을 훑지 않아 page가 아예 보이지 않는다.

Luil은 slot 요소를 그 창의 **대리**로 세운다. WinUI의 WebView2가 컨트롤 자신의 provider로 하는 것과 같은 모양이다.

- 앵커 창의 UIA root가 `IRawElementProviderHwndOverride`를 구현한다. 그 창을 물으면 slot 요소의 provider를 돌려준다.
- Slot provider는 `get_HostRawElementProvider`로 그 창을 host로 답한다. UIA가 slot과 그 창을 한 요소로 합친다. 그래서 slot의 부모·형제는 Luil tree대로이고, 자식은 그 창의 HWND 가지(page)다.
- Slot provider는 `ProviderOptions_OverrideProvider`를 함께 내세운다. 이것을 빼면 UIA3 client가 page 가지에서 바깥으로 나오는 길(다음 형제 찾기)에서 override → host → `Parent` → `FirstChild`를 끝없이 돈다. demo에서 그렇게 멈추는 것을 확인했다.
- 따로 감싼 자식 provider를 slot 아래에 두는 길은 쓰지 않는다. 그 자식이 Luil fragment tree에 속하는지 그 창에 속하는지가 갈려 같은 순환이 난다.

그 창은 WebView2가 정하므로 host가 찾는다. 앵커 창의 `Chrome_WidgetWin_0` 자식 가운데 `ICoreWebView2Environment4::GetAutomationProviderForWindow`가 이 웹뷰의 `ICoreWebView2CompositionController2::get_AutomationProvider`와 같은 COM 객체를 돌려주는 창이다. 이 판정은 **UIA 질의 밖**, 자리를 앉히는 `apply_layout`에서 하고 handle을 기억해 둔다. UIA가 page 가지를 훑는 도중에 WebView2를 부르면, 브라우저 프로세스가 우리 UI thread에 되묻는 동안 서로를 기다릴 수 있기 때문이다. 그려지지 않는 웹뷰는 대리를 세우지 않는다. 그동안 보이는 것은 placeholder이고 보조 기술도 그것을 읽는다.

Demo에서 잰 결과는 다음과 같다(runtime 155.0.4283.62).

- UIA3 client: slot이 `Chrome_WidgetWin_0`과 합쳐진 group으로 서고, 그 아래에 page의 Document와 글·링크·단추가 선다. 창의 첫 자식 자리의 중복은 없다. 부모는 창 root이고 앞뒤 형제는 화면 차례 그대로다. 끝까지 훑어도 멈추지 않는다. Slot 안의 점을 `ElementFromPoint`로 물으면 page의 Document가 답한다(page 위쪽 여백의 점은 slot이 답한다).
- `ElementFromHandle`로 그 창을 잡은 요소와 tree를 따라 내려가 잡은 slot은 같은 요소다(`CompareElements`, runtime id `42.<hwnd>`).
- UIA2 관리 client: slot이 group으로 서고 멈추지 않는다. Page 내용은 이 client가 원래 그 창을 훑지 않아 보이지 않는다.
- Chromium은 접근성을 늦게 켠다. 처음 묻는 순간에는 Document가 비어 있을 수 있고, 다음 질의부터 내용이 찬다.

자리표가 계속 선 채 접근성 HWND가 붙거나 떨어지면 그 자리표에서 `ChildrenInvalidated`를 한 번 낸다. 같은 HWND를 가진 frame을 다시 그려도 알리지 않는다. 실험에서 WebView2의 page provider는 `IRawElementProviderFragmentRoot`를 지원했다. 이를 `GetEmbeddedFragmentRoots`에서 돌려준 빌드에서도 관리 UIA2의 RawView·ControlView 첫 자식과 자손 수는 모두 비어 있었다. 이 실험 경로는 공개하지 않는다.

## 검증

데모의 ‘웹뷰’ 페이지는 ‘페이지를 자리표 위에 얹는다 (위층)’ 확인란으로 같은 웹뷰의 층을 오간다. 기본은 위층이고, 끄면 앱의 기본값인 아래층(구멍 뚫기·가림 시 구멍 닫기)을 같은 화면에서 확인한다. 아래 실측은 위층에서 잰 것이다.

Release 데모에서 새 프로세스의 주 창이 처음 보인 순간부터 `PrintWindow(PW_RENDERFULLCONTENT)`로 64장씩 세 번 찍었다. placeholder가 각각 10장·9장·8장 이어진 뒤 페이지가 보였고 단색 슬롯은 없었다. 같은 데모에서 글 탭으로 자리표를 빼고 메모 탭으로 돌아오면 직전 문서가 1장, 새 문서가 63장 보였다. 이는 표본 간격보다 짧은 전환까지 보증하지 않는다.

‘웹뷰 위에 대화 열기’로 Skia 대화가 페이지를 가리고 닫으면 페이지가 다시 보였다. 명령 팔레트 popup과 도구 창은 페이지와 실제로 겹치는 지점에서 해당 HWND가 맨 위였다. CSS 100px 자의 물리 폭은 144 DPI 두 모니터에서 각각 150px, 120 DPI 모니터에서 125px이었다. GPU 손실 주입 후에는 ‘표면 표시 불가’ 알림과 자리표가 보였고 페이지의 자가 사라졌다.

[`tests/webview_element_tests.cpp`](../tests/webview_element_tests.cpp)는 slot layout, scale 보존, hit opacity, tab 동작, 접근성을 검증한다. [`tests/webview_layout_tests.cpp`](../tests/webview_layout_tests.cpp)는 clipping, occlusion, rounding, half-open pointer bounds, capture, scale을 검증한다. [`tests/webview_message_gate_tests.cpp`](../tests/webview_message_gate_tests.cpp)는 message size와 rate limit, notice 억제, zero limit, clock window를 검증한다. [`tests/webview_failure_tests.cpp`](../tests/webview_failure_tests.cpp)는 생성 실패의 갈래 판정과 runtime build 번호 읽기를 검증한다. [`tests/uia_provider_tests.cpp`](../tests/uia_provider_tests.cpp)는 자식 창을 대신하는 요소가 그 창을 host로 답하고 override 옵션을 내세우며, 창 root가 그 창 자리에 그 요소를 돌려주는 것을 검증한다. Runtime integration에서는 creation, navigation policy, input, IME, focus exit, monitor 간 DPI 이동, process failure, Evergreen runtime 부재도 확인해야 한다.

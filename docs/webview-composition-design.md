# WebView2 composition hosting

Luil은 WebView2 composition controller를 Skia가 그리는 tree의 한 slot에 배치한다. Application은 `ui_frame::webviews`로 controller의 수명과 policy를 publish하고, 같은 id의 `webview_element`를 anchor surface tree에 배치한다.

공개 계약은 [`include/luil/app/app_host.h`](../include/luil/app/app_host.h)와 [`include/luil/ui/webview_element.h`](../include/luil/ui/webview_element.h)에 있다. Hosting은 [`src/win32/webview_host.h`](../src/win32/webview_host.h)와 [`src/win32/webview_host.cpp`](../src/win32/webview_host.cpp), layout 판단은 [`src/win32/webview_layout.h`](../src/win32/webview_layout.h)에 구현되어 있다.

WebView 기능을 사용하려면 WebView2 SDK로 build해야 하며 runtime에는 Evergreen WebView2 Runtime build 2210 이상이 필요하다. Runtime이나 필요한 interface가 없으면 slot은 placeholder를 유지하고 application에 failure event를 보낸다.

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
│   └── WebView visuals
└── Skia swap-chain visual
```

Skia visual은 WebView underlay 위에 있다. Composition swap chain은 `DXGI_ALPHA_MODE_PREMULTIPLIED`를 사용하므로 alpha가 0인 Skia pixel에서 아래 WebView가 보이고 opaque pixel은 WebView를 덮는다.

Direct3D renderer는 flip-model swap chain을 `HWND`에 직접 붙이지 않고 `CreateSwapChainForComposition`을 사용한다. 따라서 presentation이 DirectComposition target 안에 머물고 CPU renderer로 돌아가기 전에 target을 안전하게 detach할 수 있다.

DirectComposition device는 application 전체에서 공유한다. 각 `HWND`는 자체 target, root, underlay, Skia visual, swap chain을 가지며 각 WebView는 anchor surface의 underlay에 붙은 child visual을 가진다.

Direct3D가 실패해 CPU로 fallback하면 GDI presentation 전에 기존 composition target을 detach한다. CPU path에는 composition underlay가 없으므로 WebView를 숨기고 slot에는 placeholder를 그린다.

## Slot, clipping, transparency

Element는 arranged physical bounds와 arrange에 사용한 scale을 보관한다. Host는 `ui_tree::visible_bounds`를 사용해 ancestor clipping과 scroll viewport가 반영된 rectangle을 얻는다.

`plan_webview_layout`은 visible rectangle을 정수 physical pixel로 반올림하고 surface client rectangle과 교차시킨다. 비었거나 완전히 clip되면 controller를 숨긴다. DirectComposition visual offset이 위치를 정하고 visual-local clip이 page pixel을 slot 밖으로 나가지 못하게 한다. Controller bounds는 `(0, 0)`에서 시작하는 width와 height만 제공한다.

Element는 항상 일반 background와 placeholder text를 그린다. WebView2가 navigation을 한 번 완료하고 controller가 visible이 되면 그 영역에 alpha-zero hole을 낸다. 그 전에는 비어 있는 composition visual을 통해 desktop이 보이지 않도록 placeholder를 유지한다. Navigation 실패도 WebView2가 error page를 그리므로 painted 상태다.

Slot 위에 다른 element가 있다고 판단되면 hole을 내지 않는다. 현재 판정은 center와 네 inset corner를 sample하고 다섯 지점 모두에서 interaction hit testing 결과가 `webview_element`일 것을 요구한다. 하나라도 덮이면 전체 hole을 보류해 Luil pixel이 page를 가린다. Rectangle subtraction을 하지 않는 all-or-nothing 근사이며 sample 사이의 좁은 overlay를 반드시 찾지는 못한다.

가려진 동안 WebView rectangle은 유지해 불필요한 page reflow를 피한다. Hole이 보류되거나 view가 숨겨지면 pointer capture는 취소된다.

투명 WebView hole이 있는 frame은 Skia surface의 LCD subpixel text를 끄고 grayscale antialiasing을 쓴다. Channel별 subpixel coverage는 투명 pixel과 올바르게 합성할 수 없기 때문이다. Hole이 없는 frame은 일반 RGB pixel geometry를 유지한다.

Controller 기본 background는 opaque white다. Page가 직접 background를 그릴 수 있지만 background가 지정되지 않았다고 desktop이 composition hole을 통해 보이지는 않는다.

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

Event는 navigation 시작·완료·실패·차단, 거부된 permission, 차단된 download, 수신 message, dropped event, renderer 또는 browser process failure를 포함한다.

## Policy와 trust boundary

`webview_policy` 기본값은 script 활성화, developer tool 비활성화, 기본 context menu 비활성화, script dialog 활성화, browser accelerator key 비활성화, pinch zoom 비활성화, swipe navigation 비활성화다. Policy는 frame마다 갱신한다. Event filter 값은 즉시 적용되며 WebView2 setting은 runtime의 next-navigation 의미에 따라 적용된다.

`allowed_schemes` 기본값은 HTTPS뿐이다. 비교는 case-insensitive이며 host-requested navigation, top-level link와 redirect, 허용된 new-window request에 적용한다. `javascript:`는 항상 거부한다. Main-document scheme filter는 현재 iframe navigation을 다루지 않는다.

새 browser window는 만들지 않는다. `open_new_windows_here == false`이면 모두 차단한다. True이면 main document에서 user gesture로 시작한 요청만 기존 WebView를 navigate할 수 있고 destination도 scheme filter를 통과해야 한다. Iframe 또는 user gesture 없는 요청은 차단한다.

모든 permission은 거부하고 profile에 저장하지 않는다. 모든 download는 save dialog 전에 취소한다. Host object는 비활성화한다. Certificate failure 무시, host-object proxy 활성화, download나 permission 허용, SmartScreen 비활성화, input 제한 제거를 위한 policy switch는 없다.

## Event backpressure

Page event는 application의 bounded message 경로를 공유한다. `maximum_message_bytes` 기본값은 256 KiB이며 web message 하나의 UTF-8 JSON 크기를 제한한다. `maximum_events_per_second` 기본값은 120이며 web message, navigation event, permission request, download attempt를 포함한다.

Rate window는 첫 event부터 1초간이다. Limit 0은 모든 gated event를 버린다. Window에서 처음 버린 event만 size 또는 rate 원인을 담은 `events_dropped` 알림 하나를 만들고, 이후 drop은 inbox flood를 막기 위해 조용히 처리한다. Process-failure event와 drop notice는 일반 gate를 우회한다. Host-to-page message는 trusted application이 만들므로 제한하지 않는다.

## Process failure

Main render process 종료는 `render_process_failed`를 보내고 page를 한 번 reload한다. 10초 안에 다시 종료되면 알리기만 하고 reload하지 않아 crash-reload loop를 막는다. Iframe render process 종료는 main document를 reload하지 않는다.

Browser process 종료는 `browser_process_failed`를 보내고 WebView를 failed로 표시해 reconciliation에서 정리한다. 새 controller가 필요하면 application이 id를 제거한 뒤 다시 publish한다. GPU와 utility process failure는 WebView2 recovery에 맡긴다.

## 수명 규칙

Host와 controller는 모두 UI-thread object다. 비동기 생성 callback은 shared alive flag와 creation serial을 capture한다. Shutdown, 제거, 교체 뒤 도착한 callback은 orphan controller를 닫고 죽은 host에 접근하지 않는다.

Host는 WebView가 붙어 있는 동안 underlay visual의 COM reference를 보유한다. 그렇지 않으면 renderer 파괴가 WebView teardown보다 먼저 visual을 해제할 수 있다. 숨길 때 root visual을 detach하지 않고 `IsVisible`을 사용해 다음 frame까지 page content를 보존한다.

Shutdown은 future callback을 먼저 무효화하고 event delivery function을 지운 뒤 child visual 제거, controller close, environment release 순서로 진행한다. WebView event가 파괴된 `app_host`를 delivery closure로 붙잡지 못하게 한다.

## 접근성

Slot은 Luil UI Automation tree에서 이름 있는 group으로 나타나고 일반 focus traversal에 참여한다. Page의 접근성 subtree는 WebView2가 hosted content로 공개하며 Luil은 DOM node나 page text를 `access_info`로 복제하지 않는다.

## 검증

[`tests/webview_element_tests.cpp`](../tests/webview_element_tests.cpp)는 slot layout, scale 보존, hit opacity, tab 동작, 접근성을 검증한다. [`tests/webview_layout_tests.cpp`](../tests/webview_layout_tests.cpp)는 clipping, occlusion, rounding, half-open pointer bounds, capture, scale을 검증한다. [`tests/webview_message_gate_tests.cpp`](../tests/webview_message_gate_tests.cpp)는 message size와 rate limit, notice 억제, zero limit, clock window를 검증한다. Runtime integration에서는 creation, navigation policy, input, IME, focus exit, monitor 간 DPI 이동, process failure, Evergreen runtime 부재도 확인해야 한다.

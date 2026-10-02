# 여러 top-level window

Application은 `ui_frame::windows`에 secondary top-level window를 publish한다. 각 `ui_window`는 자체 tree, custom caption, renderer, DPI, input state, text-input session을 가진 owned tool 또는 document window를 설명한다.

공개 frame 계약은 [`include/luil/app/app_host.h`](../include/luil/app/app_host.h)에 있다. Platform 동작은 [`src/win32/secondary_surface.h`](../src/win32/secondary_surface.h)와 [`src/win32/win32_window.cpp`](../src/win32/win32_window.cpp)의 reconciliation code에 구현되어 있다.

## Window publish

```cpp
luil::ui_window tools {};
tools.id = u8"tools";
tools.caption.title = u8"Tools";
tools.x = 40.0f;
tools.y = 40.0f;
tools.width = 480.0f;
tools.height = 640.0f;
tools.minimum_width = 280.0f;
tools.minimum_height = 240.0f;
tools.tree = tools_tree;
tools.metrics = [](float width, float height, float scale) {
    return luil::app_message { tools_resized { width, height, scale } };
};
tools.close = [] {
    return luil::make_app_action(close_tools {});
};
frame.windows.push_back(std::move(tools));
```

`id`는 안정적인 application key이며 popup과 WebView anchor가 사용하는 surface namespace를 공유한다. 한 frame 안에서 고유해야 한다. Width 또는 height가 0 이하면 window가 없는 것으로 취급한다.

Platform은 frame마다 published id 목록과 live window를 맞춘다.

- 새 id는 window를 만든다.
- 기존 id는 최신 tree, caption, callback, minimum size를 적용한다.
- frame에서 사라진 id는 window를 파괴한다.

Id가 계속 publish되는 동안 creation failure를 기억해 frame마다 다시 시도하지 않는다. Id를 제거한 뒤 다시 publish하면 새 생성을 시도할 수 있다.

## Ownership과 초기 배치

Secondary window는 main window를 owner로 둔 Win32 owned window다. Owner 위에 유지되고 owner와 함께 파괴되지만 독립적으로 이동하고 resize할 수 있다.

`x`, `y`는 main window client origin 기준 logical offset이며, `width`, `height`는 요청한 logical client size다. 이 값들은 생성할 때만 사용한다. 이후 배치는 사용자 이동과 resize가 소유하며 같은 id를 다시 publish해도 원래 위치로 돌아가지 않는다.

초기 rectangle은 main window의 현재 scale로 변환하고 client bounds에서 window bounds로 조정한 뒤 가장 가까운 monitor work area에 제한한다. 생성 뒤에는 해당 window가 위치한 monitor의 DPI를 따른다. `minimum_width`, `minimum_height`는 live policy이며 이후 Win32 minimum-size query에 최신 값이 쓰인다.

## Metrics와 tree layout

Platform은 생성 뒤와 client size 또는 DPI 변경 뒤에 `metrics(width, height, scale)`을 호출한다. 크기는 logical client pixel이고 `scale`은 logical pixel당 physical pixel 수다. 반환한 application message는 정상 logic-thread inbox로 간다.

Application은 이 event로 surface용 새 tree를 arrange하고 이후 frame에 publish한다. 그때까지 이전 tree와 원래 scale은 일관된 쌍으로 유지된다. Platform은 published tree를 변경하거나 다시 layout하지 않는다.

Secondary tree는 해당 window의 client origin에서 시작한다. 일반 custom-caption interaction을 사용하면 위쪽에 `caption_element`를 포함해야 한다. `ui_window::caption`은 이에 맞는 native title, icon, tooltip, button policy, non-client metrics를 제공한다. 두 값은 같은 caption을 설명해야 한다.

## 닫기

Caption close button과 Alt+F4는 `ui_window::close`를 호출한다. UI thread는 반환된 action을 dispatch하지만 window를 직접 파괴하지 않는다. Application logic이 다음 frame에서 id를 빼면 닫힌다.

`close`가 비어 있으면 닫기 요청을 무시한다. Close callback은 published frame당 최대 한 번 발생하며 window를 다시 publish하면 재활성화된다. 반복 system message가 application을 flood하지 않으면서 열린 상태를 application이 결정하게 한다.

## 주 창만의 것

창 배치 보고·복원과 전체 화면은 **주 창의 것이다.** Secondary window는 자기 caption과 크기 조절을 갖지만 `window_placement`에 실리지 않고 `ui_command::window_toggle_fullscreen`의 대상도 아니다. 보조 창의 자리는 만들 때 한 번 정해지고 그 뒤로는 사용자의 것이므로 저장하고 되돌릴 주체가 없으며, 화면을 덮는 창이 여럿이면 어느 것이 덮는지가 앱 상태 밖에서 갈린다.

Secondary window의 style은 언제나 통상 모드의 계산이다([window_mode.h](../src/win32/window_mode.h)의 `window_style_for(buttons, normal)`). 전체 화면 계약은 [Win32 창과 표면](concepts/window.md)에 있다.

## Rendering과 DPI

각 top-level surface는 자체 swap chain 또는 CPU bitmap을 소유하고 `window_config`의 renderer policy를 사용한다. `automatic`은 해당 surface에서 Direct3D를 시도한 뒤 CPU로 fallback한다. Runtime renderer loss도 같은 fallback 경로를 쓴다.

Direct3D surface는 application의 DirectComposition device를 공유하지만 각 window는 자체 composition target과 visual을 가진다. Surface별 DPI는 input, caption metrics, tree arrangement, text geometry, WebView scale에 쓰인다.

## Input, focus, IME

Pointer, wheel, key, focus, file drag, accessibility event에는 surface id가 붙는다. Main-window event와 같은 input pump로 들어가며 해당 surface의 tree에서 hit testing한다.

Hover, pressed state, logical focus, menu highlight, drag origin, popup association은 surface별로 해석한다. Surface가 사라지면 그 surface의 gesture state는 다른 window로 옮기지 않고 취소한다.

Focusable top-level window마다 TSF session 하나를 소유한다. Active surface만 TSF에 text target을 알려 thread manager focus를 두고 session끼리 경쟁하지 않게 한다. Popup은 activate하지 않고 anchor surface의 text session을 빌린다.

시간 기반 paint도 surface identity를 지킨다. Platform은 surface별로 filter한 interaction state를 사용해 모든 tree의 `next_update`를 구하고 가장 이른 시점에 timer 하나를 건다.

## Popup, WebView, file drop

Popup은 `ui_popup::anchor`에 secondary window id를 지정한다. Coordinate, owner, DPI, dismissal, text-input association이 해당 surface를 따른다.

WebView는 `ui_webview::anchor`에 같은 id를 지정하고 secondary tree에 대응하는 `webview_element`를 둔다. 기존 WebView의 anchor를 바꾸면 controller와 DirectComposition visual이 원래 window에 결합되어 있으므로 다시 만든다.

`window_config::accept_file_drop`이 true이면 secondary window도 자체 OLE drop target을 등록하고 자신의 tree에서 drop을 처리한다.

## 검증

[`tests/secondary_surface_tests.cpp`](../tests/secondary_surface_tests.cpp)는 live secondary surface가 최신 caption data, minimum size, callback, tree를 적용하는지 검증한다. Window reconciliation, focus routing, popup anchoring, surface input translation, renderer fallback, text-input 동작은 대응하는 Win32 component test에서 반복 검증한다.

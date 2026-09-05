# Popup 앵커 표면

`ui_popup::anchor`는 popup의 위치, 소유 관계, DPI, 활성 수명을 결정하는 최상위 표면 id다. 빈 문자열은 주 창이며 값이 있으면 `ui_window::id`가 가리키는 보조 창이다.

공개 형식은 [app_host.h](../include/luil/win32/app_host.h)의 `ui_popup`, `ui_window`, `ui_frame`에 있다.

## Popup 선언

```cpp
luil::win32::ui_popup popup {
    .id = u8"font-menu",
    .anchor = u8"inspector",
    .x = menu_x,
    .y = menu_y,
    .width = 280.0f,
    .height = 360.0f,
    .tree = std::make_shared<luil::ui_tree>(std::move(tree)),
    .dismiss = make_popup_dismiss(),
};
```

`x`, `y`, `width`, `height`는 앵커 표면 client 좌표의 논리 픽셀이다. Popup tree는 popup client의 `(0, 0)`에서 시작하는 별도 tree다.

Popup id는 비어 있지 않아야 하며 frame의 popup 사이에서 유일해야 한다. 빈 표면 id는 주 창을 뜻하므로 popup id로 사용할 수 없다. Popup과 보조 창 표면 id는 같은 이름 공간에 들어가므로 서로도 충돌하지 않게 정한다.

## 앵커가 결정하는 것

UI thread는 앵커 표면을 찾아 다음 값을 사용한다.

- 앵커 HWND를 popup owner로 설정한다.
- 앵커 client 원점을 화면 좌표로 바꿔 popup 위치를 계산한다.
- 앵커의 DPI scale로 논리 크기를 물리 크기로 변환한다.
- popup이 어느 최상위 창의 keyboard focus와 TSF session을 공유하는지 결정한다.
- 표면 이동 때 따라 움직일 popup을 고른다.

따라서 보조 창 안에서 연 메뉴는 보조 창 좌표에 놓이고 그 창 위에 유지된다.

## 생성 조건

다음 조건을 만족할 때만 popup surface를 만든다.

- 비어 있지 않고 고유한 `id`의 popup 선언이 frame에 있다.
- `width`와 `height`가 모두 양수다.
- `anchor`가 가리키는 최상위 표면이 존재한다.

앵커 보조 창과 popup이 같은 frame에 처음 나타나도 UI thread는 표면을 먼저 reconcile한 뒤 popup을 만들 수 있다. 앵커를 찾지 못한 frame에서는 popup을 건너뛰고 다음 frame에서 다시 평가한다.

## Reconcile 규칙

UI thread는 현재 popup surface와 frame의 `ui_popup` 목록을 id로 비교한다.

- 새 id는 생성한다.
- 사라진 id나 크기가 0 이하인 popup은 파괴한다.
- 같은 id와 같은 anchor는 위치, 크기, tree, dismiss 설정을 갱신한다.
- 같은 id의 anchor가 바뀌면 기존 창을 파괴하고 새 owner로 다시 만든다.

Win32 owner는 창 생성 후 안전하게 교체하는 상태로 취급하지 않으므로 anchor 변경을 재생성 경계로 삼는다.

각 popup은 Win32 owner 관계로 앵커 창 위에 유지된다. Frame의 배열 순서는 기존 popup HWND 사이의 Z-order를 다시 정하지 않으므로, 서로 겹치는 popup의 앞뒤 관계를 배열 재정렬에 의존해서는 안 된다.

## 위치와 작업 영역

논리 좌표는 앵커의 DPI로 변환하고 `ClientToScreen`으로 화면 좌표에 옮긴다. 최종 창 사각형은 가장 가까운 모니터 작업 영역에 맞게 조정한다.

Popup이 모니터 경계를 넘어도 앱 모델의 `x`와 `y`는 바꾸지 않는다. 화면 배치와 모니터 정보는 UI thread의 책임이다.

## 앵커 이동과 수명

최상위 표면이 움직이면 UI thread는 모든 popup에 `surface_moved` dismiss reason을 알린다. Factory가 빈 action을 반환해 남은 popup 가운데, 움직인 표면에 앵커된 것만 새 화면 위치로 옮긴다. 다른 창에 붙은 popup의 위치는 바꾸지 않는다.

앵커 표면이 파괴되면 해당 popup surface도 제거된다. 앱은 보조 창을 frame에서 제거할 때 그 창의 popup도 함께 제거하는 것이 좋다. 남은 선언은 앵커가 없으므로 다시 생성되지 않는다.

Popup은 앵커의 활성화 상태를 공유하지만 닫힘 여부는 `dismiss` factory가 결정한다. 표면 이동, 크기 변경, 활성 전환 같은 reason에서 빈 action을 반환하면 popup을 유지할 수 있다.

## 반드시 유지할 불변식

- Popup 위치는 앵커 client 기준 논리 픽셀이다.
- Popup id는 비어 있지 않고 surface 이름 공간에서 고유하다.
- Owner, 좌표 변환, DPI, TSF는 같은 anchor 값에서 나온다.
- 존재하지 않는 anchor에는 popup을 만들지 않는다.
- Anchor 변경은 같은 HWND 이동이 아니라 popup 재생성이다.
- 표면 이동 뒤 위치 갱신은 그 표면에 붙은 popup에만 적용한다.
- Popup tree 좌표계는 popup client 원점에서 시작한다.

## 검증 지침

[popup_reconcile_tests.cpp](../tests/popup_reconcile_tests.cpp)는 생성, 이동, 파괴, anchor 변경, 0 크기를 검증한다. [window_position_tests.cpp](../tests/window_position_tests.cpp)와 Win32 surface test는 DPI 변환과 작업 영역 조정을 확인한다.

Reconcile 구현은 [popup_reconcile.cpp](../src/win32/popup_reconcile.cpp), 실제 표면은 [popup_surface.cpp](../src/win32/popup_surface.cpp), frame 조립은 [win32_window.cpp](../src/win32/win32_window.cpp)에 있다.

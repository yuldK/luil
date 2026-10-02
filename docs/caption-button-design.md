# Caption 버튼 구성

`caption_config::buttons`는 custom caption에 최소화, 최대화, 닫기 버튼을 둘지 정한다. 같은 값이 element 생성, 비클라이언트 hit test, Win32 창 스타일에 사용된다. 화면에 보이는 버튼과 운영체제가 제공하는 창 기능이 항상 일치하는 것이 이 계약의 핵심이다.

공개 형식은 [caption_metrics.h](../include/luil/ui/caption_metrics.h)와 [caption_element.h](../include/luil/ui/caption_element.h)에 있다.

## 설정

```cpp
luil::caption_config caption {
    .title = u8"Inspector",
    .buttons = {
        .minimize = false,
        .maximize = false,
        .close = true,
    },
};
```

`caption_buttons`의 세 값은 기본으로 모두 `true`다. Tooltip 문자열은 버튼의 존재 여부를 결정하지 않고 존재하는 버튼의 설명으로만 사용한다.

## 배치 규칙

버튼은 caption 오른쪽 끝부터 닫기, 최대화, 최소화 순서로 배치된다. 꺼진 버튼은 element를 만들지 않고 공간도 차지하지 않는다. 남은 버튼은 오른쪽으로 붙는다.

`make_caption_layout()`은 같은 규칙으로 각 버튼의 왼쪽 경계를 계산한다. 없는 버튼의 경계는 그 시점의 오른쪽 끝과 같아서 `hit_test_caption()`의 구간 판정에 들어가지 않는다.

제목과 앱 아이콘은 가장 왼쪽에 있는 실제 버튼의 경계까지만 그린다. 버튼이 하나도 없으면 caption 오른쪽 끝까지 사용할 수 있다.

논리 픽셀로 지정한 `caption_ui_metrics`는 DPI에 맞춰 물리 픽셀로 변환된다. 그리기와 hit test는 동일한 높이와 버튼 폭을 사용한다.

## Hit test와 시스템 동작

Custom caption의 비클라이언트 hit test는 다음 우선순위를 사용한다.

1. 크기 조절 테두리와 모서리
2. 시스템 메뉴 아이콘 영역
3. 존재하는 caption 버튼
4. caption 끌기 영역
5. 일반 client 영역

버튼 hit는 각각 Win32의 최소화, 최대화, 닫기 hit code로 변환된다. 존재하지 않는 버튼의 영역은 caption 끌기 영역으로 취급된다.

`window_style_for()`는 `minimize`가 꺼지면 `WS_MINIMIZEBOX`를, `maximize`가 꺼지면 `WS_MAXIMIZEBOX`를 제거한다. 크기 조절과 시스템 메뉴에 필요한 스타일은 유지한다.

## 전체 화면 중의 caption

**창이 테두리 없는 전체 화면이면 caption은 hit되지 않는다.** 위 우선순위 전체가 건너뛰어지고 모든 자리가 `HTCLIENT`로 답한다 — 크기 조절 테두리도, 시스템 메뉴 영역도, 버튼도, 끌기 영역도 없다. 화면을 덮은 창에 끌기 띠가 남아 있으면 끌기 한 번에 전체 화면이 통째로 딸려 나오고, 가장자리가 남아 있으면 화면 끝을 노려 누르다 창 크기가 바뀐다.

버튼 집합과 모드는 **같은 스타일 계산의 두 인자**다. `window_style_for(buttons, mode)`는 버튼을 보고 `WS_MINIMIZEBOX`·`WS_MAXIMIZEBOX`를 정하고, 전체 화면이면 거기서 `WS_THICKFRAME`과 `WS_MAXIMIZEBOX`를 더 뺀다. caption 갱신이 `GWL_STYLE`을 다시 쓰는 자리와 전체 화면 진입이 스타일을 세우는 자리가 같은 함수를 부르므로, 둘 중 하나가 다른 하나를 지우지 않는다.

전체 화면 중에도 앱이 caption tree를 계속 실을 수는 있지만 그 자리는 이제 일반 client다. 앱은 `window_placement::fullscreen`이나 `draw_context::fullscreen`을 보고 caption 줄을 접는 편이 낫다. 자세한 계약은 [Win32 창과 표면](concepts/window.md)에 있다.

닫기 버튼은 별도의 `WS_CLOSEBOX` 스타일이 없으므로 시각적 구성과 hit test에서 제어한다. 앱은 `ui_window::close` 또는 주 창 종료 흐름으로 실제 닫힘 요청을 처리한다.

## Frame 간 갱신

보조 창의 `caption_config`가 바뀌면 표면은 다음 항목을 함께 갱신한다.

- caption tree가 그리는 버튼과 제목
- 비클라이언트 hit test에 쓰는 metrics와 버튼 집합
- 최소화·최대화 기능을 나타내는 창 스타일
- Alt+Tab과 작업 표시줄에 보이는 OS 창 제목

창 스타일이 바뀌면 비클라이언트 frame을 다시 계산한다. 새 버튼 집합이 window style과 client 크기 계산에 모두 사용되어 테두리 두께와 client 영역이 어긋나지 않는다.

## 구성 예

```cpp
luil::ui_window inspector {
    .id = u8"inspector",
    .caption = {
        .title = u8"Inspector",
        .buttons = { false, false, true },
    },
    .width = 420.0f,
    .height = 640.0f,
};
```

버튼이 전혀 필요 없는 정보 창은 세 값을 모두 `false`로 둔다. Caption 전체는 끌기 영역이 되며 시스템 메뉴 아이콘 영역은 별도 규칙에 따라 남는다.

## 반드시 유지할 불변식

- 버튼의 존재는 `caption_buttons` 한 값에서 나온다.
- 없는 버튼은 그리지 않고 hit되지 않으며 자리도 차지하지 않는다.
- 최소화와 최대화 opt-out은 대응하는 Win32 style도 제거한다.
- 배치와 hit test는 같은 metrics와 DPI를 사용한다.
- caption 설정 갱신은 OS 제목과 창 스타일에도 반영된다.
- 전체 화면 창의 caption은 그려지더라도 hit되지 않는다.
- 버튼 집합과 창 모드는 같은 스타일 계산의 인자이며 서로를 덮어쓰지 않는다.

## 검증 지침

[caption_layout_tests.cpp](../tests/caption_layout_tests.cpp)는 전체 버튼, 일부 버튼, 버튼 없음의 경계와 hit 결과를 검증한다. 또한 `caption_element` bounds와 `make_caption_layout()`의 일치, 버튼 opt-out과 창 style의 일치, caption 자리 계산이 비클라이언트 판정과 하나임을 확인한다. 모드별 스타일과 hit 결과는 [window_mode_tests.cpp](../tests/window_mode_tests.cpp)가 창 없이 검증한다.

Win32 구현 경계는 [caption_layout.cpp](../src/win32/caption_layout.cpp), [caption_surface.cpp](../src/win32/caption_surface.cpp), [caption_surface.h](../src/win32/caption_surface.h), [window_mode.h](../src/win32/window_mode.h)에 있다.

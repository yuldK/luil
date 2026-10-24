# UI element

[`ui_element`](../../include/luil/ui/ui_element.h)는 화면의 모든 항목이 구현하는 추상이다. tree를 새 frame에서 다시 만들 수 있으므로 주소가 아니라 값인 `ui_element_id`로 식별한다. id의 유일성 범위는 tree 하나이며, 서로 다른 표면은 같은 id를 쓸 수 있다.

```cpp
struct ui_element_id
{
    ui_element_kind kind;
    std::u8string owner;
};
```

`ui_element_kind`에는 프레임워크 kind와 앱 kind가 함께 있다. `application_element_kind(index)`는 앱 영역의 열린 값을 만든다.

```cpp
constexpr auto kind_button = luil::application_element_kind(3);
```

kind의 수는 두 대역으로 나뉜다.

| 대역 | 수 | 주인 |
| --- | --- | --- |
| 라이브러리 | 0 ~ 1023 | 프레임워크 kind (`end_of_library_kinds` 앞까지) |
| 앱 | 1024 ~ | `application_element_kind(index)` = 1024 + index |

예약 대역은 처음에 64였다. 프레임워크 kind가 그것을 넘어 표·목록의 펼침 단추와 탭 막대의 새 kind가 앱의 첫 kind들(`application_element_kind(0)`~`(5)`)과 같은 수가 되었고, kind로 찾는 코드(`ids_of_kind`, 정책의 kind 판정)가 서로를 집을 수 있는 상태였다. 예약 대역을 지금 쓰는 것의 열 배가 넘는 1024로 넓혔다. 새 프레임워크 kind는 `end_of_library_kinds` 바로 위에 더하고, 그 값이 앱 대역에 닿으면 `static_assert`가 막는다. `is_application_element_kind`는 앱 대역인지 답한다.

이 변경으로 앱 kind의 수가 한 번 바뀌었다(64 + index → 1024 + index). `application_element_kind(index)`로 상수를 만든 앱은 다시 빌드하면 그대로 맞는다. kind 수를 직접 적었거나 저장해 둔 곳(설정 파일, 자동화 script의 AutomationId `kind:owner`)은 앱마다 고쳐야 한다. 자동화 script는 kind 수를 코드에 박지 않는다.

액션은 `std::function<std::vector<input_action>(const ui_action_context&)>`이며 상태를 직접 바꾸지 않는다. 액션은 메시지만 반환하고 logic 또는 UI가 해당 메시지를 실행한다.

## Drag와 draw

`drag_source`/`drop_target`은 항목 이동용 ghost와 수락 여부를 제공하고, `pointer_drag_target`은 scroll bar처럼 포인터가 element 밖으로 나가도 누른 동안 연속 이동을 받는다. `on_move`에는 이전·현재 좌표가 모두 있다. OS 파일 drag는 `drag_payload::files`를 사용하며 파일을 받는 target은 `accepts`로 판정한다. 여러 항목을 함께 끄는 payload는 `dragged_owners`에 키들을 담고(`dragged_owner`는 대표 항목), 기본 ghost가 대표 이름과 나머지 개수(`+N`)를 보인다. `rejected_cursor`를 주면 받는 target이 없는 자리 위의 포인터 모양이 그것이다.

```cpp
struct draw_context
{
    SkCanvas& canvas;
    SkTypeface* codicon_typeface;
    SkTypeface* ui_typeface;
    SkTypeface* code_typeface;
    const font_resolver* fonts;
    const ui_color_palette& palette;
    float scale;
    std::chrono::steady_clock::time_point now;
    bool maximized;
    bool fullscreen;
    ui_metrics metrics;
};
```

element는 테마·DPI·폰트 선택을 소유하지 않고 draw context로 받는다. `now`는 tooltip delay와 caret blink의 순수 계산에 사용한다. `metrics`는 앱 스타일의 치수(본문·작은 글자 크기, 컨트롤·행 모서리)로, 기본값이 내장 스타일과 같아 채우지 않은 context도 같은 그림이다 ([테마와 글꼴](theming.md)).

`interaction_snapshot`에는 hover, press, focus, menu highlight, drag 표시가 값으로 들어 있다. element는 자신의 id와 비교하기만 하며 표면 선택은 window 계층이 한다. `ui_tree::cursor_at()`은 현재 잡고 있는 element 또는 hit 대상의 cursor를 조회하고, 결과가 `inherit`이면 창 기본값을 사용한다. drag 중에는 시작 element가 cursor를 계속 결정한다.

게시된 element는 수정하지 않는다. setter는 tree 게시 전에만 사용하고, 변경은 다음 frame에서 새 element를 만든다.

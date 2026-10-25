# 머무름 트리거와 hit를 받지 않는 hover 영역

미리보기 카드는 포인터가 잠시 머무르면 열리고 떠나거나 누르면 닫힌다. 자동으로 숨는 도구 막대·옆 판은 보이지 않는 띠 위에 포인터가 오면 드러나고, 그 띠는 아래 내용의 누름을 막지 않는다. 앱이 타이머로 hover 시각을 훑거나 `GetCursorPos`를 돌리지 않아도 되도록 element 역할 두 가지를 둔다.

공개 형식은 [ui_element.h](../include/luil/ui/ui_element.h)의 `hover_dwell_target`·`hover_region_target`이다.

## 머무름 (`set_hover_dwell`)

```cpp
card->set_hover_dwell(luil::hover_dwell_target {
    .delay = std::chrono::milliseconds { 350 },
    .on_dwell = luil::make_message_action(preview_intent { true }),
    .on_end = luil::make_message_action(preview_intent { false }),
});
```

| 일 | 결과 |
| --- | --- |
| 포인터가 임자 위에 `delay` 동안 머문다 | `on_dwell` 한 번 |
| 찬 뒤 임자를 떠난다 (가둠·popup이 덮어 hover가 옮는 것 포함) | `on_end` 한 번 |
| 찬 뒤 아무 버튼이나 누르거나 끌기가 시작된다 | `on_end` 한 번 |
| 차기 전에 떠나거나 누른다 | 아무것도 내지 않는다 |
| 누른 뒤 같은 임자 위에 머문다 | 다시 재지 않는다 (떠났다 돌아오면 잰다) |
| 터치 | 머무름이 없다 (hover가 없다) |

- **임자**는 포인터 아래 element에서 root 쪽으로 처음 만나는 머무름 역할이다. 카드 안의 제목·그림 사이를 옮겨 다녀도 임자가 같아 시계가 이어진다. 비활성 element는 임자가 아니다.
- 머무름 역할만 단 element(액션 없는 카드)도 hit 대상이다(`interactive()`). 그러지 않으면 포인터 아래 hover가 서지 않아 머무름이 영영 차지 않는다 — 실제 데모에서 잡힌 결함이다.
- 시간은 서브메뉴를 hover로 여는 것과 같은 길이다. controller는 시계를 보지 않고 `next_deadline`으로 찰 시각을 알리며, 입력 pump가 그때 `advance`로 지금 시각을 준다. test는 시각을 손으로 정한다.
- 판정은 이벤트를 처리한 **뒤의** hover·누름 상태로 한 곳(`update_dwell`)에서 센다. 누름·끌기·떠남의 갈래마다 심으면 언젠가 하나를 빠뜨린다.
- 새 tree를 받을 때도 다시 본다(`refresh_hover_tracking`). 포인터가 머문 채 대화 상자의 scrim이 서면 hover가 scrim으로 옮고 머무름이 끝난다.
- 액션은 받은 때의 사본으로 알린다. 끝을 알릴 때 임자가 tree에서 이미 사라졌을 수 있다.

## hover 영역 (`set_hover_region`)

```cpp
strip->set_hover_region(luil::hover_region_target {
    .on_enter = [] { return luil::make_app_action(toolbar_intent { true }); },
    .on_leave = [] { return luil::make_app_action(toolbar_intent { false }); },
    .hold_while_captured = true,
});
```

- 판정은 hit test가 아니라 **보이는 자리**(`ui_tree::hover_regions_at`)다. 영역 element가 액션을 갖지 않으면 hit를 받지 않으므로 그 자리의 누름·hover는 아래 element가 그대로 받는다. 위를 다른 element가 덮어도 영역 안이다.
- 영역을 여럿 겹쳐도 된다. 각각 따로 들어옴·나감을 알린다.
- `hold_while_captured`(기본 참)면 누른 채 끄는 동안은 영역을 벗어나도 나감을 뗄 때까지 미룬다. 드러난 도구 막대의 단추를 끄는 도중에 숨기지 않는다.
- 포인터가 창을 떠나면 나감이다. 새 tree에서 영역이 포인터 아래에 새로 서면 들어옴, 사라지면 나감이다.
- 마우스·펜만이다. 터치에는 hover가 없어 영역도 없다 — 터치에서 숨은 막대를 드러내는 길(가장자리 쓸기·단추)은 앱이 둔다.

## 검증

- `tests/hover_dwell_tests.cpp`: 지연 뒤 한 번, 자식 사이의 이동에도 이어지는 시계, 지연 전 떠남, 누름의 취소·끝과 다시 재지 않음, 터치, scrim이 덮을 때의 끝, 영역의 들어옴·나감과 hit를 받지 않음, 창 떠남, 잡은 동안의 미룸, 머문 포인터 아래 새로 선 영역.
- 데모 "버튼과 컨트롤" 페이지의 머무름 카드와 숨은 도구 막대 띠.

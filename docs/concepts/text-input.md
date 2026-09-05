# 텍스트 입력

한 줄 입력은 [`text_input_element`](../../include/luil/ui/dialog_elements.h)가 사용하는 확정된 초안, 입력 초점, 배치 계산을 분리한다.

| 상태 | 소유자 |
| --- | --- |
| `text`, caret, 선택 | 앱 logic의 `text_edit_state` |
| `interaction_snapshot::focused_input` | input 스레드 |
| bounds, 가로 스크롤, 픽셀↔offset 변환 | `text_input_element` |

앱 logic은 편집 가능한 초안을 유지하고 `text_input_element`는 그 초안에서 만든 `text_input_view` 사본을 보관해 그린다. element를 다시 만들어도 logic의 초안이 유지되므로 입력을 이어 갈 수 있다. 초점은 input controller가 별도로 게시한다.

포인터 x를 UTF-8 byte offset으로 바꾸는 `offset_at()`은 배치에 사용한 글꼴과 측정기, 가로 스크롤을 동일하게 사용해야 한다. `text_measurer`만 주입받으므로 입력 controller도 픽셀 계산을 공유할 수 있다. `scroll_for()`는 caret이 보이는 최소 이동량을 순수 함수로 계산한다.

```cpp
struct text_input_view
{
    std::u8string text;
    std::size_t caret, anchor;
    bool composing;
    std::u8string composition_text;
    std::size_t composition_caret, composing_begin, composing_end;
};
```

IME 조합 중에는 확정 `text`를 바꾸지 않고 화면에 표시할 전체 문자열을 `composition_text`에 둔다. element는 그 값을 그리고 `composing_begin`부터 `composing_end`까지 표시한다. 확정 시 앱은 한 번의 `replace_all`로 초안을 갱신하므로 조합 단계가 undo 기록에 쌓이지 않는다.

앱은 `make_text_input_view(state, composition, target)`로 확정 초안과 해당 target의 조합 표시를 합칠 수 있다. `text_input_view::displayed_text()`는 조합 중 화면에 보이는 전체 문자열을 반환한다. 점진 검색은 `search_query(view, any_match)`를 사용하면 표시 문자열이 아무 결과도 맞히지 못하는 조합 단계에서 비어 있지 않은 확정 문자열로 물러설 수 있다. 확정 문자열이 비어 있으면 표시 문자열을 그대로 쓴다.

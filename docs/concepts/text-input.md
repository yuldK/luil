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
    std::uint64_t applied_sequence;
};
```

IME 조합 중에는 확정 `text`를 바꾸지 않고 화면에 표시할 전체 문자열을 `composition_text`에 둔다. element는 그 값을 그리고 `composing_begin`부터 `composing_end`까지 표시한다. 확정 시 앱은 한 번의 `replace_all`로 초안을 갱신하므로 조합 단계가 undo 기록에 쌓이지 않는다.

앱은 `make_text_input_view(state, composition, target)`로 확정 초안과 해당 target의 조합 표시를 합칠 수 있다. `text_input_view::displayed_text()`는 조합 중 화면에 보이는 전체 문자열을 반환한다. 점진 검색은 `search_query(view, any_match)`를 사용하면 표시 문자열이 아무 결과도 맞히지 못하는 조합 단계에서 비어 있지 않은 확정 문자열로 물러설 수 있다. 확정 문자열이 비어 있으면 표시 문자열을 그대로 쓴다.

## Android IME

IME의 `replace_all`에는 caret과 anchor 및 `text_edit_request::sequence`가 함께 온다. 앱은
`apply_text_edit`로 처리하고 `make_text_input_view`로 snapshot을 만들면 처리 번호가 자동으로
왕복한다. 편집이나 view를 직접 구현하는 앱은 거른 입력을 포함해 처리 완료 번호를
`text_edit_state::applied_sequence`에서 `text_input_view::applied_sequence`로 보존해야 한다.
IME는 최신 번호가 돌아오기 전의 중간 snapshot을 무시하므로 빠른 타이핑이 되돌아가지 않는다.

Android에서는 GameActivity가 주는 GameTextInput이 IME(`InputConnection`)를 맡는다. 앱 쪽 Java 코드는 없다. [`ime_session`](../../src/android/android_text_input.h)이 TSF의 shadow document와 같은 원리로 IME와 초점 칸을 잇는다.

- 초점이 텍스트 칸에 서면 그 칸의 확정 글과 선택을 IME에 넘기고 소프트 키보드를 띄운다. 떠나면 내린다. 사용자가 내린 키보드는 같은 칸을 다시 누르면 다시 뜬다.
- IME가 고친 상태가 오면 조합 범위가 있을 때는 `text_composition_event`로 화면에만 보이고, 조합이 없으면 `replace_all` 하나로 확정한다. 초점이 옮겨 가면 조합 중이던 글을 옛 칸에 확정한다.
- **앱 쪽 글이 바뀌었을 때만** IME에 넘긴다. IME가 고친 글이 logic을 거쳐 돌아오는 동안 앱의 글은 아직 옛것이라, 그 사이에 넘기면 IME가 되돌아간다. 돌아온 글이 IME의 것과 다르면(하드웨어 키, 붙여넣기, 앱의 입력 거르기) 그때 넘긴다.
- GameTextInput의 선택·조합 범위는 **UTF-16 코드 단위**이고 글은 Java의 modified UTF-8이다 (기기에서 `'사'` 3바이트에 선택 1로 확인했다). 둘 다 UTF-8 byte offset으로 옮긴다.
- 하드웨어 키보드의 글자도 IME가 먼저 받는다. Gboard 한국어 모드는 `a`·`b`를 두벌식 `ㅁ`·`ㅠ`로 조합해 IME 상태로 보내므로 키 경로와 겹치지 않는다. IME가 받지 않은 키만 키 이벤트로 온다.
- 키보드의 완료 단추(`IME_ACTION_DONE`)는 Enter 키로 보낸다. 가로 화면에서 전체 화면 편집기로 바뀌지 않게 한다 (`IME_FLAG_NO_EXTRACT_UI`). 이 값은 키보드를 띄울 때마다 정하고 IME 연결을 다시 세운다 — 연결은 창이 초점을 얻을 때 이미 만들어져 있다. Gboard 한국어 자판은 이때도 단추를 "↵"로 그리지만 누르면 완료 동작이 온다. 그래도 IME가 줄바꿈을 글로 넣으면 한 줄 칸은 그것을 걷어 내고 Enter로 보낸다.
- 키보드가 올라오면 그 높이를 안전 영역의 아래 가장자리에 더해 앱이 줄어든 크기로 다시 배치한다. 줄어든 frame이 오면 host가 `focus_reveal_event`를 보내고, controller가 `interaction_policy::on_focus_moved`로 초점 칸을 다시 드러낸다. 그래서 키보드가 칸을 가리지 않는다 (policy가 `route_reveal`을 답해야 한다).
- glue는 IME 상태가 바뀌어도 looper를 깨우지 않는다. host가 GameActivity의 IME 알림을 가로채 UI thread를 깨운다.

클립보드는 UI thread가 JNI로 `ClipboardManager`를 부른다 ([`android_clipboard.h`](../../src/android/android_clipboard.h)). 붙여넣기는 Windows처럼 그 칸의 `insert` 편집으로 앱에 보낸다. 키보드의 복사·붙여넣기·잘라내기 전용 키(`KEYCODE_COPY` 등)는 Ctrl+C·V·X로 옮긴다.

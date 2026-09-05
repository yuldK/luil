# 텍스트 편집

[`text_edit`](../../include/luil/text/text_edit.h)의 편집 모델은 UI·글꼴·플랫폼을 모른다. 문자열은 UTF-8이고 `caret`과 `anchor`는 항상 문자 경계의 byte offset이다.

## UTF-8 경계

Skia에 전달하기 전에 외부 입력을 `utf8_is_valid()`로 확인한다. 잘못된 UTF-8은 `utf8_replace_invalid()`로 각 잘못된 바이트를 U+FFFD로 대체한다. 검사는 overlong 인코딩, surrogate, U+10FFFF 초과, 잘린 이어짐 바이트를 모두 거부한다.

```cpp
if (!utf8_is_valid(text))
    text = utf8_replace_invalid(text);
```

`utf8_encode()`는 유효 범위를 벗어난 code point에 빈 값을 반환한다. `utf8_decode()`는 잘못된 위치에서 U+FFFD와 길이 1을 반환해 순회가 멈추지 않게 한다.

## 상태와 undo

```cpp
struct text_edit_state
{
    std::u8string text;
    std::size_t caret, anchor;
    std::vector<text_edit_revision> undo, redo;
    text_edit_group group;
    std::size_t group_caret;
};
```

편집 함수는 `text_edit_state&`를 받고 픽셀 좌표를 다루지 않는다. 같은 종류의 삽입 또는 삭제가 caret을 이어 가는 동안 하나의 undo 그룹이 된다. caret을 옮기거나 선택하면 `none`으로 시작한다. 선택 대체와 붙여넣기는 `replace` 그룹이며 묶지 않는다. IME는 조합 중간 상태를 기록하지 않고 확정 결과를 하나의 `replace_all` 명령으로 적용한다. undo 기록은 100개로 제한한다.

`text_edit_command`는 insert, backspace, 단어 이동·선택, undo, redo, caret 배치 등의 동작을 표현한다. input 스레드는 `text_edit_request`를 만들고 policy가 이를 앱 메시지로 변환한다. 앱 logic은 `ui_interaction.h`의 `luil::apply_text_edit(state, request, filter)`로 초안을 갱신한다. 선택적인 `text_insert_filter`는 insert·replace 전에 숫자 제한이나 길이 제한처럼 앱별 입력 규칙을 적용한다.

이 편집 모델의 문자 이동은 UTF-8 code point 경계를 사용하며 grapheme cluster 편집을 제공하는 계약은 아니다. 단어 이동에서 ASCII 영숫자와 비ASCII 바이트는 단어로, 나머지는 구분자로 취급한다. 경로의 `/`와 식별자의 `-` 등에서 멈춘다. 외부 값으로 글을 교체할 때는 `text_edit_set_text()`가 caret·anchor를 유효 범위로 맞추고 undo·redo를 비운다.

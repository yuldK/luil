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
    bool caret_upstream;
    std::vector<text_edit_revision> undo, redo;
    text_edit_group group;
    std::size_t group_caret;
};
```

편집 함수는 `text_edit_state&`를 받고 픽셀 좌표를 다루지 않는다. `caret_upstream`은 caret이 앞 글자 쪽에 붙는다는 표시(affinity)다. 여러 줄 칸에서 넘겨 접은 자리의 caret을 윗줄 끝에 그릴 때만 뜻이 있다. `apply_text_edit`가 `place_caret`에서만 세우고 다른 모든 명령에서 지운다 ([자동 줄 넘김](../text-area-wrap-design.md#접은-자리의-caret-affinity)). 같은 종류의 삽입 또는 삭제가 caret을 이어 가는 동안 하나의 undo 그룹이 된다. caret을 옮기거나 선택하면 `none`으로 시작한다. 선택 대체와 붙여넣기는 `replace` 그룹이며 묶지 않는다. IME는 조합 중간 상태를 기록하지 않고 확정 결과를 하나의 `replace_all` 명령으로 적용한다. undo 기록은 100개(`text_edit_history_limit`)로 제한하고, 시점마다 글 전체를 복사하므로 undo·redo가 함께 담는 글도 32 MiB(`text_edit_history_byte_limit`)로 제한한다. 넘치면 가장 먼 과거부터, 그다음 가장 먼 미래부터 버리되 바로 앞뒤 한 걸음씩은 남겨 상한보다 큰 문서도 한 번은 되돌리고 다시 실행할 수 있다.

`text_edit_command`는 insert, backspace, 단어 이동·선택, undo, redo, caret 배치 등의 동작을 표현한다. input 스레드는 `text_edit_request`를 만들고 policy가 이를 앱 메시지로 변환한다. 앱 logic은 [`ui_interaction.h`](../../include/luil/ui/ui_interaction.h)의 `luil::apply_text_edit(state, request, filter)`로 초안을 갱신한다. 선택적인 `text_insert_filter`는 insert·replace 전에 숫자 제한이나 길이 제한처럼 앱별 입력 규칙을 적용한다.

이 편집 모델의 문자 이동은 아래의 글자소 경계를 따른다. 단어 이동에서 ASCII 영숫자와 비ASCII 바이트는 단어로, 나머지는 구분자로 취급한다. 경로의 `/`와 식별자의 `-` 등에서 멈춘다. 외부 값으로 글을 교체할 때는 `text_edit_set_text()`가 caret·anchor를 유효 범위로 맞추고 undo·redo를 비운다.

## 글자소 편집 경계

좌우 이동·Delete·caret 놓기·낱말 선택은 글자소 경계를 따른다. 내장 표는 Unicode 16.0.0의
UAX #29 부분 집합이며 결합 부호·이체 선택자·이모지 수식자·ZWJ 이모지·국기 RI 쌍·한글 자모·CR LF와
인도계 결합 자음(GB9c, `क्ष`처럼 virama로 이은 자음)을 묶는다.
`LUIL_ENABLE_TEXT_SHAPING` 구성은 기존 SkUnicode 경로의 플랫폼 ICU 또는 libgrapheme을 먼저 쓰고,
그 서비스를 열 수 없으면 내장 표로 물러선다. 글자소 표의 생성기는 `scripts/generate_grapheme_data.py`다.
UCD 16.0.0의 `auxiliary/GraphemeBreakProperty.txt`, `emoji/emoji-data.txt`, `UnicodeData.txt`,
`DerivedCoreProperties.txt`(InCB)를 읽고, 각 파일의 판을 확인한 뒤 `src/text/grapheme_data.h`를 쓴다.

Backspace도 한 글자소를 지우되, 마지막 코드포인트가 Mn·Mc·Me 결합 부호인 일반 글은 그 부호만 벗긴다.
`e`와 결합 악센트에서는 `e`가 남는다. ZWJ·이체 선택자·피부색·keycap·국기·한글 자모 시퀀스는
통째로 지운다. IME 조합 중 caret과 삭제는 계속 IME가 정한다. 조합 표시 상태는 이 규칙으로 다시 맞추지 않는다.

## 배치된 글자와 편집 경계

텍스트 모양 잡기를 켠 UI는 [text_layout](../text-layout-design.md)의 같은 glyph·cluster 값에서
caret·hit test·선택·줄 넘김을 계산한다. 합자 하나가 글자소 여러 개를 덮으면 그 advance를
글자소 수로 나눈다. 편집 상태의 offset과 IME 조합 필드는 계속 원문 UTF-8 byte다.
일괄 경계가 필요한 배치는 `grapheme_boundaries()`를 쓴다. 양방향 시각 편집은 후속 범위다.

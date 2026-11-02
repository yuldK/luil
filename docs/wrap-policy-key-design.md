# 줄 나눔 정책 스타일 키

앱이 여러 줄 글의 줄 나눔 정책(`hangul_words`·`unicode`)을 스타일 파일 한 곳에서 고른다. 키는 `text.wrapPolicy`이고 값은 `hangulWords`·`unicode`다. label·markdown·여러 줄 칸의 config와 `text_block_style`의 `line_break`는 `std::optional`이 되어, 비어 있으면 앱 스타일의 `text.wrap_policy`를 따른다. 값이 배치와 그리기에 닿도록 `measure_context`는 앱 스타일 포인터를, `draw_context`는 정책 값을 든다. [실행 중 스타일 읽기](style-sheet-design.md) 결정 5에서 사용자가 길 b를 골랐고(2026-11-02, 권장 a와 다름), 이 문서는 그 길의 모양을 정한다. 키 이름·값, 네 칸의 optional, 두 문맥이 스타일을 나르는 것은 길 b가 정했으므로 다시 묻지 않는다. 이 문서가 [줄 나눔](line-break-design.md) 137·164행의 미룸을 닫는다.

## 문제

- 정책은 컨트롤마다 적는다. [줄 나눔](line-break-design.md) 결정 3(길 a)이 정책을 `text_layout_request::line_break`와 네 config의 같은 칸에 두었다. 앱 전체를 `unicode`로 바꾸려는 앱은 label·markdown·여러 줄 칸마다 적고, 새로 짓는 컨트롤에 적는 것을 잊으면 그 칸만 어절에서 바뀐다. 137행이 "G4가 정해지면 스타일 키가 config 기본값을 채우는 길을 그때 더한다", 164행이 "`text` 절에 키를 더하면 `single_line_break` 옆이 자리"라고 미뤘다.
- "적지 않음"을 나타낼 값이 없다. `label_config::line_break`(`label_element.h` 47행), `markdown_config::line_break`(`markdown_element.h` 43행), `text_area_config::line_break`(`text_area_element.h` 110행), `text_block_style::line_break`(`text_block.h` 79행)가 모두 구체 값 `hangul_words`로 시작한다. 앱이 일부러 고른 `hangul_words`와 적지 않은 기본이 같은 값이라 스타일이 끼어들 자리가 없다.
- 배치 문맥은 스타일을 들지만 측정 문맥이 버린다. `arrange_context::style`은 있다(`ui_element.h` 699행). 그러나 `preferred_size`가 받는 `measure_context`(649~653행)는 `scale`·`measurer`·`metrics`뿐이고, `arrange_context::measure()`(708~711행)가 `{ scale, measurer, metrics }`만 넘겨 `style`을 버린다. label의 줄 접기(`label_element::measure_wrap`)와 `measure_text_block`은 이 측정 문맥만 본다. [줄 나눔](line-break-design.md) 136행의 "배치 문맥은 `ui_metrics`만 나른다"는 이 뜻에서 반만 맞다.
- 그리기 문맥은 치수만 든다(`draw_context::metrics`, 773행 근처). 그리기에서 줄을 새로 접는 길이 하나 있다. 배치한 줄 없이 부르는 `draw_text_block(context, text, box, style, paint)`(`text_block.cpp` 260~276행)가 `wrap`이면 그리기에서 `wrap_lines`를 돈다.
- `text_block_style`을 쓰는 요소는 config에 칸이 없다. info_bar의 본문(`info_bar_element.cpp` 77~85행의 `message_style`)처럼 칸을 열지 않은 요소는 늘 `hangul_words`다([줄 나눔](line-break-design.md) 166행).
- 키 표의 enum 형 이름은 식별자로 쓰인다. 생성기는 행의 셋째 글을 `${enum_type}_names[]`와 `${enum_type}::${value}`로 적는다(`generate_style.cmake` 165·169·414행). `text::line_break_policy`를 그대로 적으면 `text::line_break_policy_names`가 되어 식별자가 아니다.

## 공개 형식

```cpp
// include/luil/theme/ui_style.h
#include "luil/text/line_break.h"

namespace luil {
    // 키 표가 enum 형 이름을 식별자로 쓴다(`line_break_policy_names`). text 이름공간의 정책을 이 이름으로도 본다.
    using line_break_policy = text::line_break_policy;

    struct text_style
    {
        // 한 줄 도우미의 줄바꿈 처리다. host가 frame의 스타일에서 읽어 `set_single_line_break`에 건다 (지금과 같다).
        single_line_break line_break { single_line_break::space };
        // 여러 줄 글이 config에 정책을 적지 않았을 때의 정책이다 (JSON `text.wrapPolicy`).
        //  - label·markdown·여러 줄 칸·`text_block_style`의 `line_break`가 비면 이 값이다. 배치·측정은
        //    `measure_context::style`로, 그리기에서 접는 길은 `draw_context::wrap_policy`로 받는다.
        line_break_policy wrap_policy { line_break_policy::hangul_words };

        [[nodiscard]] bool operator==(const text_style&) const noexcept = default;
    };
}
```

```cmake
# cmake/style_keys.cmake: 한 행을 더한다. 생성기와 파서는 고치지 않는다.
set(text_fields
    "lineBreak|line_break|single_line_break|space,symbol,paragraphs"
    "wrapPolicy|wrap_policy|line_break_policy|hangulWords,unicode")
```

```json
// assets/style.json의 text 절
"text": {
    "lineBreak": "space",
    "wrapPolicy": "hangulWords"
}
```

```cpp
// 네 칸. 비면 앱 스타일의 `text.wrap_policy`다. 지정 초기화와 대입(`.line_break = text::line_break_policy::unicode`)은 그대로다.
struct label_config     { /* ... */ std::optional<text::line_break_policy> line_break {}; };
struct markdown_config  { /* ... */ std::optional<text::line_break_policy> line_break {}; };
struct text_area_config { /* ... */ std::optional<text::line_break_policy> line_break {}; };
struct text_block_style { /* ... */ std::optional<text::line_break_policy> line_break {}; };

// `text_layout_request::line_break`는 구체 값 그대로다. 요소가 정책을 풀어 싣는다.
```

```cpp
// include/luil/ui/ui_element.h (문맥 구조체 구역만 고친다)
struct measure_context
{
    float scale { 1.0f };
    const text_measurer* measurer { nullptr };
    const ui_metrics* metrics { nullptr };
    // 앱 스타일이다 (`arrange_context::style`에서 온다). 비어 있으면 내장 스타일(`default_ui_style`, LUIL_STYLE로 바꾼 값)이다.
    //  - 끝에 더해 세 칸만 적은 초기화(`{ scale, &measurer, metrics }`, `{ .scale = 2.0f }`)가 그대로다.
    //  - 포인터만 들고 잰 뒤 잊는다 (`metrics`와 같다).
    const ui_style* style { nullptr };

    // 이 문맥이 쓸 앱 스타일이다 (비었으면 내장 스타일).
    [[nodiscard]] const ui_style& app_style() const noexcept;
    // config가 고른 정책이 있으면 그것, 없으면 앱 스타일의 `text.wrap_policy`다.
    [[nodiscard]] text::line_break_policy line_break_for(std::optional<text::line_break_policy> chosen) const noexcept;
    // ... 나머지는 지금과 같다
};

// arrange_context
[[nodiscard]] measure_context measure() const noexcept
{
    return { scale, measurer, metrics, style };
}

struct draw_context
{
    // ... `metrics`까지 지금과 같다
    ui_metrics metrics {};
    // 스타일의 여러 줄 정책이다 (`ui_style::text::wrap_policy`). 그리기에서 줄을 접는 길(배치한 줄 없이 부르는
    // `draw_text_block`)이 config 칸이 빈 글에 쓴다.
    //  - 기본값은 struct 기본값이라 채우지 않은 문맥은 지금의 그림이다 (`metrics`와 같은 규칙이다).
    //  - 배치에서 접은 줄을 그리는 label·markdown·여러 줄 칸·info_bar 본문은 이 값을 읽지 않는다.
    text::line_break_policy wrap_policy { text::line_break_policy::hangul_words };
    bool reduce_motion { false };
    // ... 나머지는 지금과 같다
};
```

```cpp
// 앱: 스타일 파일 한 줄로 앱 전체를 음절 접기로 바꾼다.
//   "text": { "lineBreak": "space", "wrapPolicy": "unicode" }
auto parsed { luil::parse_ui_style(text) };
if (parsed.ok())
    style_ = std::make_shared<const luil::ui_style>(std::move(*parsed.style));
frame->style = style_;  // 그리기의 `draw_context::wrap_policy`
frame->tree = make_tree(luil::make_arranged_tree(std::move(root), slot, scale, {}, nullptr, style_.get()));  // 배치·측정
// 한 칸만 어절로 두려면 그 config에 적는다. 적은 값이 스타일보다 앞선다.
luil::label_config title { .text = u8"설정을 저장했습니다", .wrap = true, .line_break = luil::text::line_break_policy::hangul_words };
```

## 규칙

**우선순위는 셋이다.** config에 적은 값, 문맥이 든 앱 스타일의 `text.wrap_policy`, 문맥이 비었을 때의 내장값 순이다(결정 2).

- 측정 문맥이 비면(`style == nullptr`) 내장 스타일이다. `default_ui_style()`은 LUIL_STYLE로 구운 값이라 파일로 `unicode`를 구운 앱은 손으로 지은 빈 측정 문맥에서도 `unicode`다.
- 그리기 문맥을 채우지 않으면 struct 기본값 `hangul_words`다. `draw_context::metrics`가 이미 이 규칙이다(결정 1의 대가).
- 스타일 파일에서 `text.wrapPolicy`가 빠지면 struct 기본값 `hangul_words`다. 컴포넌트 칸과 같은 규칙이다([실행 중 스타일 읽기](style-sheet-design.md) 115행). 그래서 지금 있는 스타일 파일은 모두 받히고 뜻도 같다.
- 아무것도 적지 않은 앱은 지금과 같다. config가 비고 스타일이 `hangul_words`라 접는 자리가 바뀌지 않는다.

**정책을 푸는 자리는 넷이다.** 요소가 optional을 구체 값으로 풀어 `text_layout_request::line_break`에 싣는다. 이 밖에서는 풀지 않는다(결정 3).

| 자리 | 푸는 때 | 푸는 식 |
| --- | --- | --- |
| `text_block.cpp`의 `measure_text_block` | 측정 | `context.line_break_for(style.line_break)`를 `wrap_lines`에 넘긴다 |
| `text_block.cpp`의 `draw_text_block`(배치한 줄 없는 다섯 인자) | 그리기 | `style.line_break.value_or(context.wrap_policy)` |
| `markdown_element::layout(measure_context, width)` | 측정·배치 | `context.line_break_for(config_.line_break)` |
| `text_area_element::arrange` | 배치 | `context.measure().line_break_for(config_.line_break)`를 멤버(`resolved_line_break_`)에 쥔다 |

- `wrap_lines`(`text_block.cpp` 70행)는 스타일의 optional이 아니라 푼 정책을 인자로 받는다. 지금 `style.line_break`를 request에 옮기는 81행이 그 인자가 된다.
- label은 풀지 않는다. `block_style`(`label_element.cpp` 33~43행)이 config의 optional을 `text_block_style`로 그대로 옮기고, `measure_text_block`이 푼다. label의 그리기는 배치에서 접은 줄(`wrapped_`)을 그리는 여섯 인자 길이라 그리기 문맥을 보지 않는다.
- 여러 줄 칸은 배치 밖에서도 정책을 쓴다(`lines_for` 447행, `layout_request` 490행, `wrap_displayed` 529행). 셋 다 배치에서 쥔 `resolved_line_break_`를 읽는다. 배치 전에는 `config_.line_break.value_or(default_ui_style().text.wrap_policy)`로 시작한다.
- 여섯 인자 `draw_text_block`(배치한 줄을 받는 길)은 정책을 쓰지 않는다. info_bar 본문이 이 길이라 info_bar는 측정 문맥만으로 앱 스타일을 따른다.

**cache는 푼 값을 본다.**

- `text_area_wrap_cache::line_break_`는 푼 정책을 기억한다. 무효화 조건(548행)은 `config_.line_break`가 아니라 `resolved_line_break_`와 견준다. 그래서 config가 비어 있고 앱이 실행 중에 스타일을 바꿔 정책이 뒤집히면 다음 배치에서 `entries_`·`rows_`가 함께 비워진다. config가 같아도 그렇다.
- `text_layout_request`의 cache key(`operator==`, `layout_key_hash`)는 구체 값이라 바뀌지 않는다. 풀린 값이 같으면 같은 항목이다.

**두 자리에 같은 값을 준다.** 앱이 스타일을 `make_arranged_tree(..., style)`(배치·측정)와 `ui_frame::style`(그리기)에 같은 포인터로 주는 규칙([실행 중 스타일 읽기](style-sheet-design.md) 130행)이 그대로 정책에도 닿는다. 배치 쪽은 `arrange_context::style`이 이미 tree 끝까지 잇고(`for_child`) `measure()`가 이번에 넘긴다. 그리기 쪽은 host가 `draw_frame`에서 `frame_style(state).text.wrap_policy`를 채운다(통합 몫, 아래 약속). 그리기에서 줄을 접는 길은 다섯 인자 `draw_text_block` 하나뿐이라, 통합 전에는 그 길만 스타일이 `unicode`여도 `hangul_words`로 그린다.

**키 표는 한 행이다.** 데이터 파일 `text_fields`의 `lineBreak` 뒤에 한 행을 더하고 생성기와 파서는 고치지 않는다(결정 4).

- 형 이름은 `line_break_policy`이고 `ui_style.h`의 별칭으로 `luil` 이름공간에서 보인다. 생성 헤더는 `luil::generated::style_keys`와 `luil::generated` 안이라 바깥 이름공간의 별칭을 찾는다(`tab_bar_variant`를 찾는 것과 같다).
- 값 이름은 생성기의 `luil_style_snake_case`가 `hangulWords`를 `hangul_words`로 바꾼다(`generate_style.cmake` 43~47행). `unicode`는 그대로다.
- `text_style`의 선언 순서가 `line_break`, `wrap_policy`라 행도 그 순서다. 순서가 어긋나면 기본값 헤더의 지정 초기화가 컴파일에서 막히고, 행이 빠지면 키 표 자리 시험이 잡는다.
- 허용 값 밖은 `Invalid value at text.wrapPolicy: keepAll (expected one of hangulWords, unicode)`로 두 쪽이 막는다. 낯선 키·형 오류도 기존 칸과 같은 글이다.
- `assets/style.json`에 `"wrapPolicy": "hangulWords"`를 적는다. 내장 파일을 읽은 값이 `default_ui_style()`과 같다는 시험은 빠져도 통과하지만, 앱이 복사해 고치는 견본 파일에 키가 보여야 한다.

**닿지 않는 것.** 한 줄 도우미(`set_single_line_break`)와 `text.lineBreak`는 그대로다. 초점·입력·접근성 이름·UIA 글 범위는 원문이고, 화면 줄 단위 범위만 새 접은 줄을 따른다(지금과 같다). 배율·테마·고대비와 무관하다. 고대비에서 스타일의 색은 쓰이지 않지만 `text`·`metrics`처럼 색이 아닌 칸은 고대비에서도 앱의 값이다. 움직임이 없다.

## 결정

길 b가 정한 것(키 이름과 값, 네 칸의 optional, 두 문맥이 스타일을 나름)은 묻지 않는다. 아래는 그 안의 모양이다. 모두 공개 API를 길 b가 적은 범위 밖으로 넓히지 않아 권장대로 진행한다.

**1. 문맥이 드는 것.** 질문: 두 문맥이 `ui_style` 포인터를 드는가, 정책 값만 드는가.

- 길 a: `measure_context`는 `const ui_style* style`(배치 문맥의 선례), `draw_context`는 정책 값 `wrap_policy`(그리기 문맥 `metrics`의 선례). 되는 것: `measure()`가 이미 가진 `style`을 넘기는 한 줄이다. 비면 내장 스타일(LUIL_STYLE)이다. 뒤의 노드가 측정에서 다른 칸(R4 markup의 서식 글 기본 같은 것)을 읽을 때 문맥 칸을 새로 늘리지 않는다. 그리기는 값이라 수명을 따질 것이 없고 host가 `frame_style(state)`에서 한 칸 채운다. 대가: 두 문맥의 빈 값이 다르다. LUIL_STYLE로 `unicode`를 구운 앱이 손으로 지은 빈 측정 문맥은 `unicode`, 채우지 않은 그리기 문맥은 `hangul_words`다. `metrics`가 이미 그렇게 어긋나 있고(측정의 빈 `metrics`는 `default_ui_style().metrics`, 그리기는 struct 기본값), 그리기에서 접는 길이 하나뿐이라 닿는 곳이 좁다.
- 길 b: 둘 다 `const ui_style*`. 되는 것: 대칭이고 빈 값이 둘 다 LUIL_STYLE이다. 대가: `draw_context`에 치수의 원천이 둘(`metrics` 값과 `style->metrics`)이 되어 어느 쪽이 이기는지 규칙이 따로 든다. `metrics` 칸을 지우면 길 b가 말하지 않은 공개 칸 삭제다.
- 길 c: 둘 다 정책 값. 되는 것: 가장 작다. 대가: `measure()`가 머리글 안에서 `style`이 빌 때의 내장값을 불러야 하고, 측정에서 스타일의 다른 칸이 필요해질 때마다 문맥 칸이 하나씩 는다. 배치 문맥과 모양이 갈린다.
- 권장: a. 대가: 위의 빈 값 비대칭이다. 시험은 두 문맥의 빈 값을 따로 잠근다. 권장대로 진행.

**2. 칸이 비었을 때의 우선순위.** 질문: config 칸과 앱 스타일이 함께 있으면 무엇이 이기는가.

- 길 a: config에 적은 값, 문맥의 앱 스타일, 내장값 순. 되는 것: 탭 막대의 선례(`tab_bar_element.cpp` 414·1106행의 `config_.style.has_value() ? *config_.style : context.app_style().tab_bar`)와 같다. 한 칸만 어절로 두는 앱이 그 칸에 적으면 된다. 대가: 앱이 스타일로 모든 칸을 강제할 길이 없다. 적은 칸은 스타일을 바꿔도 그대로다.
- 길 b: 스타일이 config를 덮는다. 되는 것: 파일 하나로 전부 바뀐다. 대가: 데모의 정책 스위치처럼 일부러 고른 칸이 스타일에 지워지고, config 칸이 있는 까닭이 사라진다.
- 길 c: 요소를 지을 때 config 기본값을 `default_ui_style()`로 채운다. 되는 것: 문맥을 고치지 않는다. 대가: 실행 중 스타일(`parse_ui_style`)이 닿지 않고 LUIL_STYLE만 따른다. 길 b가 정한 "문맥이 스타일을 나른다"와 어긋난다.
- 권장: a. 대가: 위의 강제 불가다. 권장대로 진행.

**3. 푸는 때.** 질문: optional을 언제 구체 값으로 푸는가.

- 길 a: 측정·배치에서 풀고 요소가 푼 값을 쥔다. 그리기에서 접는 다섯 인자 `draw_text_block`만 그리기 문맥에서 푼다(위의 표). 되는 것: 잰 줄과 그린 줄이 한 정책이다. 여러 줄 칸의 cache가 푼 값을 견주어 실행 중 스타일 변경도 무효화한다. `text_layout_request`는 구체 값 그대로라 rich_edit와의 약속(정책은 `text_layout_request::line_break`로만)이 그대로다. 대가: 여러 줄 칸에 멤버 하나(`resolved_line_break_`)가 는다.
- 길 b: 그리기에서도 모두 다시 푼다. 대가: 배치 문맥과 그리기 문맥이 다른 정책을 들면(통합 전처럼) 잰 높이와 그린 줄이 어긋나 글이 칸을 넘친다.
- 길 c: `text_layout_request::line_break`도 optional로 두고 `make_text_layout`이 푼다. 대가: 배치 함수에는 스타일 문맥이 없어 전역을 새로 지나야 하고, cache key가 빈 값과 구체 값을 따로 센다. rich_edit 약속이 바뀐다.
- 권장: a. 대가: 위의 멤버 하나다. 권장대로 진행.

**4. 키 표의 enum 형 이름.** 질문: `text` 이름공간의 enum을 생성기를 고치지 않고 어떻게 표에 적는가.

- 길 a: `ui_style.h`에 `using line_break_policy = text::line_break_policy;`를 두고 행에 `line_break_policy`를 적는다. 되는 것: 생성기와 파서를 고치지 않는다는 [실행 중 스타일 읽기](style-sheet-design.md) 결정 5의 약속을 지킨다. 대가: 공개 이름 `luil::line_break_policy`가 하나 는다. 같은 enum이라 두 이름이 섞여도 형이 같다. `ui_style.h`가 `luil/text/line_break.h`를 include한다(core 헤더끼리라 이식성 검사와 무관하다).
- 길 b: 생성기가 형 글의 `::`를 `_`로 바꿔 표 이름을 짓는다. 되는 것: 새 공개 이름이 없다. 대가: 생성기를 고치고 생성기 시험에 사례를 더한다. 앞 노드와의 약속을 어긴다.
- 길 c: enum을 `luil` 이름공간으로 옮긴다. 대가: `luil::text::line_break_policy`를 쓰는 코드가 모두 깨진다. 길 b가 말하지 않은 공개 이름 삭제다.
- 권장: a. 대가: 위의 별칭 하나다. 권장대로 진행.

**5. 손으로 짓는 문맥 자리.** 질문: 문맥을 손으로 짓는 자리를 어떻게 다루는가.

- 길 a: 새 칸에 기본값을 두어 모든 자리가 그대로 컴파일되게 하고, 뜻이 달라지는 자리만 고친다(아래 표). 되는 것: 길 b가 대가로 적은 "열 곳을 고친다"가 라이브러리 두 곳과 데모 한 곳으로 준다. test의 손으로 지은 문맥은 "내장 스타일로 잰다"는 지금 뜻 그대로다. 다른 줄의 파일을 고치지 않는다. 대가: 손으로 지은 문맥은 앱 스타일을 모른다. 표에 적은 데모 두 자리(toasts)가 견본 스타일을 `unicode`로 바꾼 뒤 잰 높이와 배치가 어긋날 수 있다.
- 길 b: 열 곳 모두에 `.style`을 적는다. 되는 것: 빠뜨림이 없다. 대가: test 아홉 곳은 스타일을 따로 쥐지 않아 `nullptr`을 적는 diff뿐이고, 다른 줄의 파일(`frame_state.cpp`)까지 고쳐야 한다.
- 길 c: 문맥을 집합체가 아니게 하고 만드는 함수로만 짓게 한다. 되는 것: 빠뜨림을 컴파일이 막는다. 대가: 지정 초기화를 쓰는 모든 자리(test 수십 곳과 앱)가 깨진다. 길 b가 말하지 않은 공개 API 변경이다.
- 권장: a. 대가: 위의 데모 한계다. 권장대로 진행.

## 손으로 짓는 문맥 자리

`measure_context`. 새 칸 `style`은 끝에 기본값 `nullptr`로 더해 셋까지 적은 위치 초기화와 지정 초기화가 그대로다.

| 자리 | 꼴 | 처리 |
| --- | --- | --- |
| `include/luil/ui/ui_element.h` 710행 `arrange_context::measure()` | 위치 셋 | 이 노드가 고친다. `style`을 넷째로 넘긴다 |
| `src/ui/markdown_element.cpp` 453행 `make_markdown_tooltip`의 `preferred_size({}, …)` | 빈 문맥 | 그대로. 뒤의 `make_arranged_tree`도 스타일 없이 배치해 잰 값과 배치가 같은 내장 스타일이다 |
| `tests/breadcrumb_tests.cpp` 61행 | 지정 | 그대로 |
| `tests/content_layout_tests.cpp` 31행 | 위치 셋 | 그대로 |
| `tests/font_style_native_tests.cpp` 265행 | 빈 문맥 | 그대로 |
| `tests/font_style_tests.cpp` 74행 | 지정 | 그대로 |
| `tests/info_bar_element_tests.cpp` 34행 | 지정 | 그대로 |
| `tests/label_wrap_tests.cpp` 27행 | 지정 | 그대로. 새 시험은 `.style`을 실은 문맥을 따로 짓는다 |
| `tests/split_button_element_tests.cpp` 217행 | 위치 셋 | 그대로 |
| `tests/text_block_native_tests.cpp` 126행 | 빈 문맥 | 그대로 |
| `tests/text_block_tests.cpp` 29행 | 지정 | 그대로 |
| `tests/choice_group_element_tests.cpp` 87행, `tests/markdown_element_tests.cpp` 114행 | `preferred_size({ … })` | 그대로 |
| `examples/demo/line_break_sample.h` 97행 | 지정 | 이 노드가 고친다(데모 커밋). `const luil::ui_style*` 인자를 받아 `.style`에 싣는다. 테마 페이지의 새 칸이 쓴다 |
| `examples/demo/basics_page.cpp` 643행, `examples/mobile_demo/basics_page.cpp` 323행 | 지정 | 그대로. 줄 나눔 칸은 config에 정책을 적어 스타일보다 앞선다 |
| `examples/demo/toasts_page.cpp` 208행, `examples/mobile_demo/toasts_page.cpp` 209행 | 지정 | 그대로. info_bar 본문은 칸이 없어 스타일을 따르는데 이 자리는 내장 스타일로 잰다. 견본 스타일이 `wrapPolicy`를 바꾼 뒤에는 높이가 어긋날 수 있어 한계로 적는다 |

`draw_context`. 새 칸 `wrap_policy`는 `metrics` 뒤에 기본값 `hangul_words`로 더한다. 지정 초기화는 칸을 건너뛸 수 있어 `.metrics = …, .reduce_motion = …`처럼 적은 자리가 그대로다.

| 자리 | 처리 |
| --- | --- |
| `src/ui/ui_element.cpp` 773~788행 `derive_draw_context` | 이 노드가 고친다. `.wrap_policy = context.wrap_policy`를 옮긴다. `palette_scope_element.cpp` 35행, `ui_tree.cpp` 81행(tooltip), `zoom_view_element.cpp` 348행은 이 함수를 지나 저절로 잇는다 |
| `src/host/frame_state.cpp` 81~96행 `draw_frame` | 기본값으로 그대로 컴파일된다. 다른 줄의 파일이라 `.wrap_policy = frame_style(state).text.wrap_policy`를 잇는 일은 통합 몫이다 |
| `src/win32/drag_ghost_window.cpp` 139~149행 | 그대로. 끌기 ghost는 한 줄 글(`draw_text`·`draw_text_within`)이라 정책을 읽지 않는다 |
| `tests/font_style_native_tests.cpp`, `glyph_element_tests.cpp`, `gpu_image_tiling_tests.cpp`, `raster_draw_tests.cpp`(1270행), `raster_probe.cpp`(39행), `text_block_native_tests.cpp`, `text_block_tests.cpp` | 그대로. 새 시험만 `.wrap_policy`를 적는다 |

config를 값으로 읽는 라이브러리 자리는 이 노드가 고친다. `label_element.cpp` 41행(optional을 그대로 옮긴다), `markdown_element.cpp` 356행, `text_area_element.cpp` 447·490·529·548·555행, `text_block.cpp` 81행이다. test와 데모는 칸에 대입만 해(`label_wrap_tests.cpp` 132행, `text_area_element_tests.cpp` 886·911행, `line_break_sample.h` 51·60행) 그대로 컴파일된다. 칸을 값으로 견주거나 `switch`하는 test는 없다.

## 플랫폼

**Win32.** `src/win32`를 고치지 않는다. 호스트는 이미 `frame->style`을 `frame_state.style`로 옮기고(`win32_window.cpp` 2046행), 그리기 문맥은 `frame_state.cpp`가 짓는다. 그 한 칸은 통합 몫이다. 끈 구성과 켠 구성이 같은 규칙으로 접는다.

**Android.** Java 소스는 0줄이고 JNI도 부르지 않는다. 바뀌는 코드는 모두 core(`theme/`, `ui/`, `text/`)라 그대로 돈다. 호스트 쪽은 Win32와 같은 `frame_state.cpp`를 함께 쓴다(`android_app.cpp` 1690행이 스타일을 옮긴다). 새 시험은 `luil_core_tests`라 기기에서도 돈다. unsupported인 부분은 없다.

## 다른 노드와의 약속

- **style_sheet(R2, 같은 줄 앞 노드)**: 데이터 파일 `text_fields`에 한 행, `assets/style.json`에 한 줄, `tests/style_cases.json`에 사례 셋을 더한다. 생성기(`generate_style.cmake`)와 파서(`ui_style_json.cpp`)는 고치지 않는다(결정 4). 컴포넌트 칸은 tuple을 돌아 새 행을 저절로 읽는다. 사례 수는 45에서 48이 된다.
- **uax14(R1, 같은 줄)**: [줄 나눔](line-break-design.md) 137행(결정 3의 "G4가 정해지면 스타일 키가 config 기본값을 채우는 길을 그때 더한다")과 164행(style_sheet 약속)의 미룸을 이 문서가 닫는다. 두 줄과 136행의 "배치 문맥은 `ui_metrics`만 나른다"는 구현 커밋이 이 문서를 가리키게 고친다. 166행의 "`text_block_style`을 쓰는 새 요소는 기본 정책(`hangul_words`)을 따른다"는 "앱 스타일의 정책을 따른다"로 고친다. 이 설계 커밋은 line-break-design.md를 고치지 않는다.
- **rich_edit(R3, 같은 줄)**: 약속 넷([줄 나눔](line-break-design.md) 162행)이 그대로다. 정책은 여전히 `text_layout_request::line_break`(구체 값)로만 들어간다. `text_area_wrap_cache`의 무효화 조건은 이 노드가 "config의 정책"을 "푼 정책"으로 바꾼 그 자리이고, rich_edit는 그 옆에 문단 폭을 더한다.
- **markup(R4, 같은 줄)**: 서식 글 config도 칸을 optional로 두고 측정 문맥의 `line_break_for`로 풀면 앱 기본을 따른다. 측정에서 스타일의 다른 칸이 필요하면 `measure_context::app_style()`을 읽는다(결정 1).
- **hot_reload(R5, 같은 줄)**: 새 스타일 포인터를 두 자리에 주면 다음 배치에서 정책이 바뀌고 여러 줄 칸의 cache가 비워진다. 다시 읽기에서 따로 할 일이 없다.
- **같은 라운드 다른 줄**: 이 노드는 `modal_host_element.*`, `zoom_view_element.*`, `ink_canvas_element.*`, `src/host/frame_state.*`, `src/win32/win32_window.cpp`, `src/android/android_app.cpp`, `src/ui/ui_interaction.*`, `include/luil/ui/ui_events.h`를 고치지 않는다. 그 파일의 문맥 자리는 새 칸의 기본값으로 그대로 컴파일된다. `ui_element.h`는 줄 3(ink)이 `zoom_source`(508행) 뒤 구역에 덧붙이고, 이 노드는 문맥 구조체 구역(`measure_context` 649행부터 `derive_draw_context` 선언까지)만 고친다. `ui_element.cpp`는 `derive_draw_context`와 새 `measure_context::app_style`·`line_break_for`(`arrange_context::app_style` 옆)만 고친다. 이 노드는 라이브러리 kind를 더하지 않는다.
- **통합 몫**: `frame_state.cpp`의 `draw_frame`에 `.wrap_policy = frame_style(state).text.wrap_policy`를 `.metrics` 뒤에 잇는다. 다른 줄이 R2에 새로 짓는 측정·그리기 문맥(modal_host·ink가 여러 줄 글을 잰다면)에 `style`·`wrap_policy`를 잇는다. 색인(`docs/README.md`)에 이 문서 줄을 더한다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(헤더, 소스, 데이터 파일, `assets/style.json`, 사례 표, test, 문서)을 먼저 내고 데모 커밋을 뒤에 쌓는다.
- 구현 때 함께 고치는 문서: [줄 나눔](line-break-design.md) 136·137·164·166행과 공개 형식 절의 네 칸, [여러 줄 라벨](label-wrap-design.md)·[직접 그리는 글](text-block-design.md)·[자동 줄 넘김](text-area-wrap-design.md)의 칸 서술, [실행 중 스타일 읽기](style-sheet-design.md) 결정 5와 범위 밖의 "더하지 않는다" 서술(이 노드가 더했다로), [테마](concepts/theming.md)의 스타일 절, `ui_style.h`의 `text_style`·`ui_style::text` 주석(지금은 한 줄 도우미만 말한다).

## 확인

자동 test.

- `tests/ui_style_json_tests.cpp`·`tests/style_tests.cmake`(공유 사례 표 `tests/style_cases.json`에 셋을 더한다):
  - `text.wrapPolicy`를 `"unicode"`로 둔 파일을 받고 `text.wrapPolicy`가 `unicode`인지;
  - `"keepAll"`을 막고 첫 오류가 `invalid_value`·`text`·`wrapPolicy`, 글이 `Invalid value at text.wrapPolicy: keepAll (expected one of hangulWords, unicode)`인지;
  - 키를 지운 파일을 받고 struct 기본값(`default: true`)인지.
  - 키 표 자리 시험이 `text_style`의 두 행을 빈틈 없이 덮는지(기존 시험이 새 행을 저절로 본다). 생성기 시험은 키 표 헤더에 `"wrapPolicy"`와 `&text_section::wrap_policy` 꼴의 행이 있는지.
  - 내장 파일을 읽은 값이 `default_ui_style()`과 같고 `text.wrap_policy`가 `hangul_words`인지.
- `tests/wrap_policy_tests.cpp`(`luil_core_tests`, Windows와 Android 기기, 고정 폭 측정기):
  - 우선순위: 칸이 빈 label을 `unicode` 스타일을 실은 측정 문맥으로 재면 음절 사이에서 접고, 칸에 `hangul_words`를 적으면 같은 문맥에서도 어절에서 접는지. 빈 측정 문맥은 `default_ui_style()`의 정책인지;
  - `arrange_context::measure()`가 `style`을 넘기고, `make_arranged_tree(..., style)`로 배치한 label의 접은 줄이 스타일을 따르는지(`for_child`로 몇 계층 아래까지);
  - markdown: 칸이 빈 블록이 스타일의 정책으로 접히는지. 여러 줄 칸: 두 경로(측정기 길과 `make_text_layout(request, measurer)`)가 스타일 `unicode`에서 같은 줄인지, config는 그대로 두고 스타일만 바꿔 다시 배치하면 `text_area_wrap_cache`가 비워지고 다시 찬 자리가 cache 없이 접은 것과 같은지;
  - `text_block`: `measure_text_block`이 측정 문맥의 스타일을, 다섯 인자 `draw_text_block`이 `draw_context::wrap_policy`를 따르는지(raster로 두 줄 수를 견준다). 채우지 않은 그리기 문맥은 `hangul_words`인지;
  - `derive_draw_context`가 `wrap_policy`를 옮기는지;
  - info_bar 본문이 config 칸 없이 측정 문맥의 스타일로 접히는지.
- 기존 시험(`label_wrap_tests`, `text_block_tests`, `text_area_element_tests`, `markdown_element_tests`, `line_break_tests`, `text_layout_tests`)이 고치지 않고 그대로 통과하는지. `luil_umbrella_header`, `luil_core_portability`, `luil_source_style`, `luil_installed_consumer`.

데모. 상태를 label로 그려 UIA Name과 화면으로 되읽는다.

- 데스크톱 테마 페이지의 "스타일 파일" 묶음에 견본 단추 `theme-style-sample-wrap`("견본: 줄 나눔 unicode")을 더한다. 내장 파일 글의 `text.wrapPolicy`를 `unicode`로 바꿔 칸에 싣고 곧바로 읽는다(`style_sheet_sample.h`에 동작 하나). 그 아래에 칸이 빈 여러 줄 label `theme-wrap-label`(견본 글과 폭은 `line_break_sample.h`의 것)과 상태 label `theme-wrap-status`를 둔다. 상태는 "스타일 정책 unicode · 라벨 N줄 · 첫 줄 끝 'X'"이고 build에서 `measure_wrap(label, { .scale, .measurer, .style = 지금 스타일 })`로 잰 값이다. 주 tree는 `make_frame`이 본문 `arrange_context`에 `.style`을 이미 실어([실행 중 스타일 읽기](style-sheet-design.md) 234행) 화면의 label도 같은 정책으로 접힌다. 처음과 `theme-style-reset` 뒤에는 "스타일 정책 hangul_words"다. 거부(낯선 키 견본) 뒤에는 앞서 적용한 정책이 남는다.
- 모바일 데모 테마 페이지에 같은 견본 단추와 두 label을 둔다. 상태 label은 폭에서 줄을 바꾼다.
- 데모 kind는 각 데모의 `common.h`에 더하고 test·UIA 스크립트는 이름으로 찾는다. `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`가 고정 폭 측정기로 두 정책의 상태 글을 잠근다. 화면의 값은 실제 글꼴로 잰 값이라 다를 수 있어 검증 때 UIA와 화면으로 되읽는다.
- UIA(격리 데스크톱): `theme-style-sample-wrap`을 Invoke해 `theme-wrap-status`가 `unicode`와 첫 줄 끝을 말하는지, 낯선 키 견본 뒤에도 `unicode`가 남는지, `theme-style-reset` 뒤 `hangul_words`인지 읽는다. 기본 페이지의 줄 나눔 칸(`line-break-status`)은 스위치가 적은 정책을 그대로 말해 스타일에 닿지 않는지 읽는다. 모바일 데모(Windows 창)도 같은 label을 읽는다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.

사람 확인.

- 데스크톱 데모에서 견본을 적용한 뒤 창 폭을 줄이며 테마 페이지의 label이 한글 음절 사이에서 접히는지, 기본 페이지의 줄 나눔 칸은 스위치대로인지 눈으로 본다.
- 실기기 mobile_demo에서 같은 견본을 적용해 label이 음절 사이에서 접히는지 본다.

## 범위 밖

- `keep-all`([줄 나눔](line-break-design.md) 결정 2의 c). enum에 값을 더하면 데이터 파일의 허용 목록에 한 이름을 더한다.
- 컨트롤 종류별 스타일 정책(label만 `unicode` 같은 것)과 선택자.
- info_bar처럼 `text_block_style`을 쓰는 요소의 config에 칸을 여는 일. 칸이 없어도 이제 앱 스타일을 따른다.
- host가 짓는 메뉴 popup·글 편집 메뉴 tree(`menu_popup.cpp` 39행, `text_edit_menu.cpp` 62행)의 스타일. 한 줄 글이라 정책을 읽지 않는다.
- 데모 toasts 페이지의 손으로 지은 측정 문맥(위의 표). 견본 스타일이 정책을 바꾼 뒤의 높이 차이는 한계로 둔다.

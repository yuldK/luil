# 서식 있는 글 편집

여러 줄 칸(`text_area_element`)에서 굵게·기울임·밑줄·코드·링크를 보이는 대로 편집한다. 글은 지금처럼 `text_edit_state`가 들고, 서식은 그 곁에 text 층의 값 `text_format`이 든다. 편집마다 서식 구간이 글을 따라 옮겨 가고, 실행 취소가 글과 서식을 함께 되돌린다. 1단계는 인라인 서식만이고 목록·제목·인용 같은 블록은 2단계다. 공개 형식은 [text_format.h](../include/luil/text/text_format.h)의 `text_mark`·`text_format`, [text_edit.h](../include/luil/text/text_edit.h)에 더하는 서식 칸, [rich_text.h](../include/luil/ui/rich_text.h)의 `rich_text_command`·`rich_text_presenter`, [rich_text_commands.h](../include/luil/ui/rich_text_commands.h)의 `make_rich_text_commands`·`make_rich_text_key_handler`다. 새 요소 kind는 없다. 클립보드 교환과 저장 형식은 [서식 글 교환](rich-edit-exchange-design.md)에 나눠 적는다.

## 문제

- 칸은 구간을 그리기만 한다. `text_area_config::spans`([text_area_element.h](../include/luil/ui/text_area_element.h) 124행)는 앱이 준 `text_span`을 칠할 뿐이고 편집하면 offset이 어긋난다. 구문 강조는 바뀐 줄을 글에서 다시 칠해 이 문제를 피하지만([syntax_highlighter.h](../include/luil/ui/syntax_highlighter.h)), 사용자가 고른 서식은 글에서 다시 지을 수 없다. 앱이 편집마다 구간을 옮겨야 한다.
- `text_span`에 밑줄·코드 칸이 없다([text_document.h](../include/luil/ui/text_document.h) 31~40행). 배치 쪽 `text_layout_style`은 `code`를 받아 code face로 배치하지만([text_layout.h](../include/luil/ui/text_layout.h) 25~33행, `text_layout_skia.cpp` 128행), 칸의 `metric_styles`는 `bold || italic`인 구간만 넘긴다([text_area_element.cpp](../src/ui/text_area_element.cpp) 463행). 칸의 밑줄은 링크와 조합 글에만 긋는다.
- 실행 취소가 서식을 모른다. `text_edit_revision`은 글·caret·anchor만 담는다(text_edit.h 19~24행). 서식만 바꾼 동작은 기록에 없고, 앱이 따로 기록을 지으면 칸이 4단계에서 먼저 받는 Ctrl+Z([ui_interaction.cpp](../src/ui/ui_interaction.cpp) `process_text_input_key`의 `key_z` 갈래)와 순서가 어긋난다.
- IME는 글 전체를 바꾼다. TSF와 GameTextInput은 확정 글을 `replace_all`로 보낸다([win32_tsf_input.cpp](../src/win32/win32_tsf_input.cpp) 206행, [android_text_input.cpp](../src/android/android_text_input.cpp) 409행). 어디가 바뀌었는지 다시 견주어야 서식을 옮길 수 있다.
- 서식 명령을 받는 자리가 없다. Ctrl+B·I·U·K는 칸이 쓰지 않아 위로 흐르지만(`process_text_input_key`의 `default`) 받아 주는 helper가 없어 앱마다 id·단축키·메뉴 체크를 다시 적는다.
- 보조 기술이 서식을 모른다. Text 패턴 범위의 `GetAttributeValue`는 `IsReadOnly`만 답하고 `FindAttribute`는 늘 빈 답이다([uia_provider.cpp](../src/win32/uia_provider.cpp) 2175·2122행).

## 공개 형식

```cpp
// include/luil/text/text_format.h (새 파일, text_change.h만 include한다. ui와 Skia를 모른다)
enum class text_mark : std::uint8_t   // 비트 합이다. 뒤 비트는 2단계 몫(취소선 등)으로 남긴다
{
    none = 0,
    bold = 1u << 0,
    italic = 1u << 1,
    underline = 1u << 2,
    code = 1u << 3,
};
[[nodiscard]] constexpr text_mark operator|(text_mark left, text_mark right) noexcept;
[[nodiscard]] constexpr text_mark operator&(text_mark left, text_mark right) noexcept;
[[nodiscard]] constexpr bool has_mark(text_mark marks, text_mark mark) noexcept;

struct text_mark_run { std::size_t begin { 0 }; std::size_t end { 0 }; text_mark marks { text_mark::none }; };
struct text_format_link { std::size_t begin { 0 }; std::size_t end { 0 }; std::u8string destination {}; };

// 글 한 벌의 서식이다 (UTF-8 byte offset, 글자소 경계).
struct text_format
{
    std::vector<text_mark_run> runs {};      // begin 순, 겹치지 않음, `none` 없음, 맞닿은 같은 서식은 합친다
    std::vector<text_format_link> links {};  // begin 순, 겹치지 않음. 주소가 다른 이웃은 합치지 않는다
    [[nodiscard]] bool operator==(const text_format&) const = default;
};

[[nodiscard]] text_mark text_marks_at(const text_format& format, std::size_t offset) noexcept;  // offset 앞 글자
[[nodiscard]] bool text_marks_cover(const text_format& format, std::size_t begin, std::size_t end, text_mark mark) noexcept;
[[nodiscard]] text_format text_format_with_mark(const text_format& format, std::size_t begin, std::size_t end, text_mark mark, bool on);
[[nodiscard]] text_format text_format_with_link(const text_format& format, std::size_t begin, std::size_t end, std::u8string_view destination);
[[nodiscard]] text_format text_format_shifted(const text_format& format, const text_change& change, text_mark inserted);
[[nodiscard]] text_format text_format_slice(const text_format& format, std::size_t begin, std::size_t end);  // 0부터 다시 센다
[[nodiscard]] std::size_t text_format_bytes(const text_format& format) noexcept;  // 기록 상한이 세는 크기

// include/luil/text/text_edit.h (구조체 끝에 더한다. 기존 `{ text, caret, anchor }` 초기화는 그대로 읽힌다)
struct text_edit_revision { /* ... */ std::shared_ptr<const text_format> format {}; };
struct text_edit_state
{
    /* ... */
    std::shared_ptr<const text_format> format {};  // 비면 서식 없는 글이다. 편집마다 새 값으로 바뀐다
    std::optional<text_mark> typing_marks {};      // 다음 넣는 글의 서식이다. 기록 대상이 아니다
};
[[nodiscard]] text_mark text_edit_typing_marks(const text_edit_state& state) noexcept;  // 다음에 넣는 글이 받을 서식 (구현 결정 A)
void text_edit_set_format(text_edit_state& state, std::shared_ptr<const text_format> format);  // 서식만 바꾸는 한 걸음
void text_edit_insert_formatted(text_edit_state& state, std::u8string_view text, const text_format& fragment);
void text_edit_set_formatted_text(text_edit_state& state, std::u8string_view text, std::shared_ptr<const text_format> format);

// include/luil/ui/text_document.h, `text_span`의 `italic` 뒤
bool underline { false };
bool code { false };

// include/luil/ui/rich_text.h (새 파일)
enum class rich_text_command { bold, italic, underline, code, clear_format, link, copy, cut, paste, paste_plain };
struct rich_text_state { bool bold { false }; bool italic { false }; bool underline { false }; bool code { false }; bool link { false }; };
[[nodiscard]] rich_text_state rich_text_state_of(const text::text_edit_state& state);
bool apply_rich_text_command(text::text_edit_state& state, rich_text_command command);  // 서식 다섯만 한다
bool set_rich_text_link(text::text_edit_state& state, std::u8string_view destination);  // 빈 주소는 링크를 걷는다
class rich_text_presenter  // 서식을 `text_area_config::spans`·`links`로 옮긴다
{
public:
    void update(const text::text_edit_state& state);  // `format` 포인터가 같으면 지난 vector를 그대로 둔다
    [[nodiscard]] std::shared_ptr<const std::vector<text_span>> spans() const noexcept;
    [[nodiscard]] std::shared_ptr<const std::vector<text_link>> links() const noexcept;
};

// include/luil/ui/rich_text_commands.h (새 파일, commands.h를 include한다)
inline constexpr std::u8string_view rich_text_bold_command_id { u8"format.bold" };  // italic·underline·code·link, clear_format은 "format.clear" (구현 결정 B)
struct rich_text_command_labels { std::u8string bold {}, italic {}, underline {}, code {}, link {}, clear_format {}; };
[[nodiscard]] std::vector<app_command> make_rich_text_commands(const rich_text_state& state, const rich_text_command_labels& labels = {});
[[nodiscard]] std::optional<rich_text_command> rich_text_command_of(std::u8string_view id);
[[nodiscard]] key_handler make_rich_text_key_handler(std::vector<app_command> commands, std::function<input_action(rich_text_command)> make_action);
```

앱은 칸 하나에 `text_edit_state` 하나와 `rich_text_presenter` 하나를 쥔다. `apply_text_edit` 뒤 같은 turn에 `presenter.update(state)`를 부르고 `spans()`·`links()`를 칸 설정에 넘긴다. 서식 명령은 칸에 단 key handler나 도구 막대 단추가 낸 앱 메시지를 `apply_rich_text_command`로 받는다.

## 규칙

**구간과 옮기기.**

- `runs`·`links`의 불변식(위의 주석)을 모든 함수가 지킨다. 그래서 같은 서식은 `==`로 같고, test와 presenter의 비교가 여기에 기댄다.
- 글을 바꾸는 `text_edit.cpp` 함수가 서식도 옮긴다. 바뀐 구간을 이미 아는 길(insert, backspace, delete, 낱말 지우기, cut, 선택 대체)은 그 구간을 그대로 쓴다. `replace_all`만 `text_change_between`으로 구간을 찾는다. 공통 접두·접미가 모호하면(`aa`에서 `a` 하나를 지움) 변경의 끝이 새 caret과 같아지도록 모호한 범위 안에서 옮긴다. caret이 편집이 끝난 자리이기 때문이다.
- 넣은 글의 서식은 이 차례로 정한다. `typing_marks`가 있으면 그것, 선택을 대체하면 고른 첫 글자의 서식, 아니면 앞 글자의 서식이고 글 처음이나 줄 처음이면 뒤 글자의 서식이다. 링크는 넣은 자리가 링크 한가운데(`begin < offset < end`)일 때만 늘어난다. 링크 끝에 이어 쓴 글은 링크가 아니다.
- 지운 범위는 구간에서 잘라 낸다. 빈 구간은 버리고 맞닿은 같은 서식은 합친다. 경계는 글자소 경계로 넓힌다(구문 강조 도우미가 렉서 출력에 하는 것과 같다). 글자소 한가운데의 경계는 그 글자소의 끝으로 민다. 글자소는 첫 바이트의 서식을 따르므로 굵은 `e` 뒤에 서식 없이 친 결합 부호도 굵다. 경계 하나가 그 줄을 훑으므로 편집이 닿은 자리(`[offset, offset + inserted]`)의 경계만 맞춘다. 밖에서 불러온 서식(`text_edit_set_formatted_text`)만 글 전체를 맞추고 글 크기로 자른다.
- `text_edit_insert_formatted`는 넣은 자리를 서식 없이 비우고 조각의 서식을 옮겨 놓는다. 링크 한가운데에 넣었으면 늘어난 링크를 조각 둘레에서 끊는다. 조각은 주변 서식과 섞지 않는다.
- `format`이 비면 옮기지 않는다. 한 줄 칸과 서식을 쓰지 않는 칸은 비용이 없다.
- 비용: offset이 절대값이라 편집 뒤의 구간을 모두 옮기고 새 `text_format`을 하나 짓는다. 편집 하나가 구간 수에 비례한다. 글 크기에는 기대지 않는다.

**실행 취소.**

- 기록의 시점이 서식도 담는다. 덩어리를 열 때(`begin_edit`, text_edit.cpp 163행) 지금의 `format` 포인터를 시점에 넣고, `text_edit_undo`·`redo`가 글과 함께 되돌린다. 같은 서식을 가리키는 시점은 포인터를 나눠 쥔다.
- `text_edit_set_format`은 서식만 바꾸는 한 덩어리다. `replace`처럼 묶이지 않고 redo를 비운다. 같은 포인터나 같은 값이면 걸음이 아니다. 고른 구간이 없어 `typing_marks`만 바꾼 명령도 걸음이 아니다.
- 바이트 상한(`text_edit_history_byte_limit`)은 글 크기에 `text_format_bytes`를 더해 센다(`history_bytes`, text_edit.cpp 126행). 나눠 쥔 서식도 시점마다 센다. 실제보다 크게 세어 상한을 넘지 않는 쪽이다.
- `text_edit_set_text`는 기록과 함께 서식도 비운다. 서식 있는 글을 밖에서 불러오면 `text_edit_set_formatted_text`다.
- 앱 수준 [실행 취소 stack](undo-stack-design.md)과는 지금처럼 따로 간다(그 문서의 결정 3). 칸에 초점이 있으면 Ctrl+Z가 칸의 기록으로 글과 서식을 함께 되돌린다.

**다음 글자 서식과 명령.**

- 고른 구간이 없을 때 서식 명령은 `typing_marks`만 바꾼다(넣을 글이 이어받을 서식, 곧 `text_edit_typing_marks`에서 그 비트를 뒤집는다). 다음에 넣는 글이 그것을 받고 넣은 뒤 비운다. 그 뒤의 글은 앞 글자를 이어받아 같은 서식이다. 글을 바꾸는 편집(넣기·지우기), caret 이동, 놓기, 선택, 실행 취소, `set_text`가 비운다. IME가 같은 글을 다시 알린 `replace_all`은 caret이 그대로면 비우지 않는다. 조합 중에 누른 명령이 확정 글까지 살아 있어야 해서다.
- 고른 구간이 모두 그 서식이면 끄고 아니면 켠다(`text_marks_cover`). `clear_format`은 고른 구간의 `runs`만 지운다. 링크 걷기는 `set_rich_text_link(state, {})`다. `code`는 다른 서식과 함께 걸린다.
- `apply_rich_text_command`는 바뀐 것이 있으면 참이다. 고른 구간의 서식이 그대로면(지울 것 없는 `clear_format`) 거짓이고 걸음도 없다. `link`·`copy`·`cut`·`paste`·`paste_plain`에는 거짓을 돌려준다. 링크는 앱이 주소를 받는 대화 상자를 띄운 뒤 `set_rich_text_link`로 걸고, 나머지는 교환 문서의 helper가 한다.
- `set_rich_text_link`는 고른 구간이 없으면 caret이 선 링크(안이거나 바로 뒤, `text_input_snapshot::link`와 같은 판정)의 주소를 바꾸거나 걷는다. 그런 링크가 없으면 거짓이다. 맞닿은 이웃 링크는 주소가 같을 때 하나로 합친다.
- `rich_text_state_of`는 고른 구간이 있으면 구간 전체가 켜졌는지, 없으면 `text_edit_typing_marks`(다음 글자 서식, 없으면 넣을 글이 이어받을 서식)를 답한다. 링크는 고른 구간이 한 링크 안이거나, 고른 구간 없이 caret이 링크 안이거나 바로 뒤일 때다. 메뉴 체크와 도구 막대 단추의 켜짐이 이것이다.
- 도구 막대 단추는 `set_preserve_pointer_focus(true)`로 칸의 초점을 지킨다. 누를 때 초점이 단추로 가면 IME 조합이 그 자리에서 확정되고, 칸을 다시 누르는 caret 놓기가 `typing_marks`를 비운다.

**키.**

- Ctrl+B·I·U·K는 `process_text_input_key`(ui_interaction.cpp 3288행)가 쓰지 않아 8단계 `key_handler`에 닿는다([key-propagation-design.md](key-propagation-design.md)). 앱이 칸에 `make_rich_text_key_handler`를 달면 명령표를 `match_command`(범위 `focused`)로 맞추고 앱 메시지를 낸다. controller는 고치지 않는다.
- 기본 단축키는 굵게 Ctrl+B, 기울임 Ctrl+I, 밑줄 Ctrl+U, 링크 Ctrl+K다. 코드와 서식 지우기는 기본 단축키가 없다. Ctrl+E는 2단계 가운데 정렬의 자리이고 Ctrl+Space는 IME 전환과 겹치는 환경이 있다. 사용자가 다시 정한 단축키(`user_shortcuts`)를 따른다.
- 판정용 표는 정적이고 모두 켠다. 판정은 input thread가 하고 칸의 기록은 logic thread의 것이라서다(실행 취소 stack의 같은 규칙). 체크와 켜짐은 메뉴·팔레트에 보이는 표만 `rich_text_state_of`로 짓는다.
- Tab은 지금처럼 칸을 떠난다. Enter는 줄 바꿈이고 목록 이어 쓰기는 2단계다.
- 앱이 이름을 주지 않으면 `default_text`의 기본 이름이다(굵게, 기울임꼴, 밑줄, 코드, 링크, 서식 지우기).

**IME.**

- 조합 중의 글은 서식 없이 지금처럼 밑줄로 그린다. 앱의 구간은 확정 글 기준이고 칸이 조합 글만큼 옮겨 그린다(text_area_element.h 121행). 확정하면 `replace_all`이 넣은 글에 위의 차례대로 서식을 준다. 조합 중에 누른 서식 명령은 `typing_marks`만 바꿔 확정 글이 받는다.
- TSF와 GameTextInput은 평문만 본다. `GetText`가 `TS_RT_PLAIN` 한 run인 것(win32_tsf_input.cpp 445행)을 바꾸지 않는다.

**그리기.**

- `text_span::underline`은 글자 아래 1 논리 픽셀 선을 그 구간의 글자색으로 긋는다. 링크 밑줄과 같은 자리이고 배율을 곱한다. 줄 바꿈 칸에는 긋지 않는다. `code`는 code face로 배치하고 바탕을 `raised_background`로 칠한다. 코드 바탕은 `text_background_role`을 늘리지 않고 `draw_content` 앞의 바탕 칠하기에서 `palette.raised_background`로 직접 칠한다. 구간에 바탕 역할이 함께 있으면 그쪽이 앞선다.
- `metric_styles`가 `bold || italic || code`인 구간을 배치에 넘긴다. code face는 advance를 바꾸므로 `text_area_wrap_cache`가 글과 함께 견주는 굵기 구간(`entry::styles`)에 실린다. 밑줄은 폭을 바꾸지 않아 배치에 넘기지 않는다. 배치가 없는 측정기 길은 지금처럼 보통 글꼴로 재고 코드는 바탕만 칠한다.
- 고대비에서 `raised_background`는 `window_background`라(`src/theme/ui_theme.cpp` 282행) 코드 바탕은 사라지고 code face가 유일한 단서다. 색이 모여도 굵기·기울임·밑줄·code face가 남는 단서다. 밑줄은 글자색이라 고대비 글자색을 따른다. 움직임은 없다.
- 줄 높이가 고정(`text_area_line_height`)이라 글자 크기를 바꾸는 서식은 두지 않는다. 서식은 `text_layout_request::line_break`를 건드리지 않고 칸이 푼 정책(`resolved_line_break_`)을 그대로 쓴다. 블록이 없어 문단마다 접는 폭이 같다.
- 구문 강조와는 칸 하나에 하나다. `spans`를 두 도우미가 함께 채우지 않는다. 코드 칸은 `syntax_highlighter`, 서식 칸은 `rich_text_presenter`다.

**접근성 (Win32 UIA).**

- 칸이 `text_input_snapshot` 끝에 더한 `spans`(칸 설정의 구간을 가리키는 포인터)로 그리는 구간을 알린다. Text 패턴 범위의 `GetAttributeValue`가 `FontWeight`(700·400), `IsItalic`, `UnderlineStyle`(Single·None)을 답하고 범위 안에서 값이 갈리면 `UiaGetReservedMixedAttributeValue`다. `FindAttribute`는 그 셋에서 값이 같은 다음 범위(뒤로 찾으면 앞 범위)를 범위 안에서 찾는다. 답하는 값(700·400, 참·거짓, Single·None) 밖의 값은 아무 범위에도 맞지 않는다. 빈 범위는 그 자리 뒤 글자의 값이고 글 끝이면 앞 글자의 값이다. 구간이 없는 칸(한 줄 칸, 구간을 주지 않은 여러 줄 칸)은 400·거짓·None이다. 코드는 알리지 않는다(provider가 글꼴 이름을 모른다). 조합 중에는 Text 패턴이 조합 글 문서를 보이고 구간은 확정 글 기준이라 서식 속성을 답하지 않는다(지원하지 않는다는 예약 값, `FindAttribute`는 찾지 못함).
- 구문 강조 칸도 같은 구간을 싣는다. 굵게 칠한 키워드는 굵게라고 답한다. 그린 대로 답하므로 둘을 가르지 않는다. Value 패턴의 값은 평문 그대로이고 링크의 Hyperlink 자식도 지금 그대로다.

## 결정

**1. 문서 모델(N2).** 질문: 서식 있는 글을 보이는 대로 편집하는 서식 문서로 만드는가, 마크다운 원문을 편집하게 하는가.

- 길 a: 서식 문서(이 문서). 되는 것: 굵게·기울임·밑줄·코드·링크를 보이는 대로 고치고, 편집마다 구간이 따라가며 Ctrl+B 같은 명령을 받는다. R1이 바탕을 놓았다(`text_change_between`, `text_span::italic`, 칸의 굵기 배치). 대가: XL이다. `text_edit.h`·`text_edit.cpp`와 `text_area_element.cpp`를 깊게 고치고, 서식 undo와 HTML 거르기라는 새 유지 몫이 생긴다. 서식은 칸 기록(`text_edit_revision`·`text_edit_state` 끝)에 들어 결정 3이 여기에 딸려 정해진다.
- 길 b: 원문 + 미리보기. 되는 것: 새 편집 모델이 없어 위험이 가장 작다. 대가: 보이는 대로 편집이 아니고 사용자가 마크다운 문법을 알아야 한다.
- 길 c: 원문 + 칸 안 서식. 되는 것: 원문이 저장 형식 그대로이고 편집 모델이 지금 그대로다. 대가: 기호가 화면에 남고, 여러 줄에 걸친 강조는 줄 상태 정수 하나로 다루기 어렵다. `parse_markdown`은 기호를 벗긴 글을 내어([markdown_element.h](../include/luil/ui/markdown_element.h) 32행) 원문 offset과 맞지 않으므로 렉서를 따로 짓는다.
- 권장: a. 대가: 위의 XL과 유지 몫이다. 보이는 대로 편집보다 원문이 저장 형식인 메모 앱이 주된 쓰임이면 c가, 렌더 확인만 필요하면 b가 낫다. **승인: 길 a (2026-11-03).**
- b를 고르면 줄여 쓴다. 공개 형식은 새 헤더 없이 `text_area_element`와 `markdown_element`를 나란히 두는 조립 예다(스크롤 맞추기는 범위 밖). 고칠 파일은 `examples/demo`의 새 페이지와 이 문서뿐이고 `text_edit.h`·`text_document.h`·클립보드·UIA는 그대로다. 확인은 데모에서 칸에 글을 넣고 미리보기의 접근 값이 바뀌는지 UIA로 읽는다. [서식 글 교환](rich-edit-exchange-design.md)(결정 7~10)은 통째로 빠지고 줄 3에 낼 갈래도 없다. `ui_events.h`·`win32_image_clipboard.cpp`·`android_clipboard.cpp`·`uia_provider.cpp`는 고치지 않는다.
- c를 고르면 줄여 쓴다. 공개 형식은 `include/luil/ui/markdown_lexer.h`의 `make_markdown_line_lexer()`(`syntax_line_lexer`를 돌려준다)와 `text_span`의 `underline`·`code` 두 칸이다. 기호는 `text_color_role::secondary`로 흐리게 칠한다. 고칠 파일은 새 `src/ui/markdown_lexer.cpp`, `text_document.h`, `text_area_element.cpp`의 `metric_styles`·`draw_content`다. 확인은 `tests/markdown_lexer_tests.cpp`(luil_core_tests)가 `**굵게**`·`*기울임*`·`` `코드` ``·`[글](주소)`와 닫히지 않은 기호, 줄 상태를 넘는 강조를 단언한다. [서식 글 교환](rich-edit-exchange-design.md)(결정 7~10)은 통째로 빠지고 줄 3에 낼 갈래도 없다. `ui_events.h`·`win32_image_clipboard.cpp`·`android_clipboard.cpp`·`uia_provider.cpp`는 고치지 않는다. 서식 글 쪽으로는 `text_span` 두 칸과 그리기만 남는다.

**2. 구간의 형과 그리는 자리.** 질문: 서식을 어떤 형으로 들고 어디서 그리는가.

- 길 a: text 층의 새 형 `text_format`을 들고, 그리기는 `text_span` 끝에 `underline`·`code`를 더해 `text_area_element`의 모드로 한다(이 문서). 되는 것: `text_edit.h`가 ui 형을 모른 채 서식을 기록에 담는다. 칸은 지금 길(`spans`·`links`)로 그리고 새 kind가 없어 `ui_element_id.h`·`inspect_names.cpp`를 고치지 않는다. 대가: 서식 값과 그리는 구간이 두 벌이라 presenter가 옮긴다.
- 길 b: `text_span` vector를 그대로 모델로 쓴다. 되는 것: 옮길 일이 없다. 대가: text 층이 ui 헤더를 include하게 되어 층이 거꾸로 서고, 색 역할과 바탕 같은 그리기 칸이 모델에 섞인다.
- 길 c: 새 요소 `rich_text_element`(새 kind). 되는 것: 서식 칸만의 키와 그리기를 따로 다듬는다. 대가: 여러 줄 칸의 배치·IME·스크롤·링크를 다시 짓거나 상속할 수 없는(final) 칸을 감싸야 하고, kind를 더하는 세 파일이 함께 바뀐다.
- 권장: a. 대가: 위의 옮김이다. 서식이 같은 포인터면 옮기지 않는다. **권장대로 진행.**

**3. 서식의 실행 취소.** 질문: 서식 변경을 누구의 기록에 넣는가.

- 길 a: 칸 기록을 넓힌다. `text_edit_revision`과 `text_edit_state` 끝에 `format`을 더한다(이 문서). 되는 것: 한 기록이라 글과 서식이 어긋나지 않고, 칸의 Ctrl+Z가 둘을 함께 되돌린다. IME `replace_all`과 묶기 규칙도 그대로다. 대가: 공통 모듈 `text_edit.h`·`.cpp`가 깊게 바뀌고, 바이트 상한 계산과 모든 변경 함수의 옮기기를 test로 잠가야 한다.
- 길 b: 앱 수준 `undo_stack`에 서식 걸음을 쌓는다. 되는 것: `text_edit.h`를 고치지 않는다. 대가: 칸에 초점이 있으면 Ctrl+Z가 칸의 글만 되돌린다. 글 편집과 서식 걸음의 차례가 두 stack에 갈려 되돌리면 구간이 엉뚱한 글에 걸린다.
- 길 c: presenter가 칸 기록 옆에 서식 기록을 병렬로 든다. `undo.size()`를 지켜보며 맞춘다. 되는 것: 칸 기록의 구조(`text_edit.h`)를 건드리지 않는다. 대가: 상한으로 앞 시점이 버려지거나 묶기가 갈리는 순간 짝이 어긋나고, 서식만 바꾼 걸음을 칸 기록에 넣을 길이 없다.
- 권장: a. 대가: 위의 깊은 변경이다. 서식이 없는 칸은 빈 포인터 하나라 비용이 없다. **권장대로 진행.** 결정 1이 a일 때 따라온다. b·c는 위 대가로 고르지 못하는 길이다.

**4. 넣은 글의 서식.** 질문: 새 글은 어떤 서식을 받는가.

- 길 a: 앞 글자를 이어받고, 고른 구간 없이 누른 명령은 `typing_marks`로 다음 글자에 건다(이 문서, 워드 프로세서의 흔한 규칙). 되는 것: 굵게 낱말 끝에 이어 쓰면 굵게이고, Ctrl+B를 누르고 쓰면 그 글부터 굵게다. 대가: `typing_marks`라는 기록 밖 상태가 하나 늘고 caret 이동마다 비운다.
- 길 b: 이어받지 않는다. 새 글은 `typing_marks`만 받는다. 되는 것: 규칙이 한 줄이다. 대가: 굵게 낱말을 고치면 고친 글자만 보통 글이 되어 낯설다.
- 길 c: 이어받되, 고른 구간 없는 명령은 caret이 선 낱말 전체에 건다. 되는 것: 상태가 늘지 않는다. 대가: 빈 자리나 낱말 사이에서 누르면 걸 곳이 없어 이어 쓸 서식을 미리 켤 수 없다.
- 권장: a. 대가: 위의 상태 하나다. **권장대로 진행.**

**5. 서식 명령의 키.** 질문: Ctrl+B 같은 키를 누가 받아 명령으로 바꾸는가.

- 길 a: 새 헤더의 `make_rich_text_key_handler`를 앱이 칸에 단다(이 문서). 되는 것: controller를 고치지 않는다(키가 이미 8단계로 흐른다). 명령표 하나로 판정·메뉴·팔레트·사용자 단축키를 함께 쓴다. hotkey가 고치는 `commands.h`를 이 노드가 건드리지 않는다. 대가: 앱이 칸을 지을 때 한 줄을 더 쓴다.
- 길 b: 칸이 config 칸으로 키를 내장한다. 되는 것: 앱 코드가 가장 적다. 대가: 단축키를 다시 정하는 길이 명령표와 갈리고, 칸 설정에 명령 어휘가 들어간다.
- 길 c: 앱 `on_key`(11단계)에 맡긴다. 되는 것: 칸 설정과 key handler가 필요 없다. 대가: 초점이 칸 밖이어도 맞아 앱이 초점을 따로 확인하고, 칸의 부모 `key_handler`가 먼저 가져갈 수 있다.
- 권장: a. 대가: 위의 한 줄이다. **권장대로 진행.**

**6. 보조 기술의 서식.** 질문: 1단계에 UIA 서식 속성을 넣는가.

- 길 a: 굵기·기울임·밑줄을 `GetAttributeValue`·`FindAttribute`로 답한다(이 문서). 되는 것: 화면 읽기가 서식을 읽고 서식으로 이동한다. 대가: `text_input_snapshot`에 칸 하나가 늘고 `uia_provider.cpp`의 범위 함수 둘이 바뀐다.
- 길 b: 미룬다. 되는 것: 플랫폼 파일을 고치지 않는다. 대가: 보이는 차이를 화면 읽기 사용자가 알 길이 없다. 서식 칸의 핵심 정보가 빠진다.
- 권장: a. 대가: 위의 두 자리다. **권장대로 진행.**

구현하며 정한 것이다. 위 결정의 범위 안이라 따로 승인을 묻지 않았다.

**구현 결정 A. 다음 글자 서식을 묻는 함수.** `text_edit.h`에 `text_edit_typing_marks(state)`를 공개한다. 넣은 글의 서식 차례(`typing_marks`, 고른 첫 글자, 앞 글자, 줄 처음의 뒤 글자)를 한곳에 두어 `text_edit_insert`와 `rich_text_state_of`·`apply_rich_text_command`가 같은 답을 낸다. 다른 길은 `rich_text.cpp`에 같은 차례를 다시 적는 것인데, 둘이 어긋나면 도구 막대가 켜졌다고 보인 서식과 친 글의 서식이 갈린다.

**구현 결정 B. 서식 명령의 id.** `rich_text_commands.h`가 `format.bold`·`format.italic`·`format.underline`·`format.code`·`format.link`·`format.clear`를 `rich_text_*_command_id` 상수로 공개하고 `rich_text_command_of`가 그 id를 되찾는다. 실행 취소 명령표(`edit.undo`)와 같은 꼴이다. 메뉴 막대·팔레트가 같은 id로 명령을 고른다.

**구현 결정 C. 링크 명령의 대상.** `set_rich_text_link`는 고른 구간이 없으면 caret이 선 링크(안이거나 바로 뒤)를 대상으로 한다. 링크 끝에서 Ctrl+K를 눌러 주소를 고치거나 걷는 흔한 동작이 고르기 없이 된다. 판정은 `text_input_snapshot::link`와 같아 편집 메뉴의 "링크 열기"와 어긋나지 않는다.

## 플랫폼

**Win32.** 서식은 C++ 값이고 그리기는 공통 칸이다. 플랫폼 파일은 `uia_provider.cpp`의 `uia_text_range_provider::GetAttributeValue`·`FindAttribute`와 그 위의 익명 `access_text_document`만 고친다. TSF(`win32_tsf_input.cpp`)는 평문 그대로이고 `win32_window.cpp`는 고치지 않는다. 클립보드는 교환 문서다.

**Android.** Java 소스는 0줄이다. 서식 모델·옮기기·그리기는 같은 C++이고 JNI를 부르지 않는다. GameTextInput은 평문을 `replace_all`로 보내 위의 옮기기 규칙을 그대로 탄다. 하드웨어 키보드가 없으면 단축키가 없으므로 도구 막대 단추가 같은 앱 메시지를 낸다(앱 몫). 접근성 계층이 없어 서식 속성은 알리지 않는다. 터치 편집 메뉴(`src/host/text_edit_menu.cpp`)는 고치지 않는다. unsupported로 둘 부분은 없다.

## 다른 노드와의 약속

- **앞 라운드 문서가 이 노드에 남긴 것을 닫는다.** 구문 강조([syntax-highlight-design.md](syntax-highlight-design.md) 222행): `text_change_between`는 부르기만 하고(`replace_all` 길에서만), `text_span` 칸은 끝에 더한다. 구문 강조가 남긴 `metric_styles`·`draw_content`를 같은 줄에서 차례로 고친다. 줄 나눔([line-break-design.md](line-break-design.md) 163행): 문단별 접는 폭은 1단계에 하지 않는다. 블록이 없어 문단마다 폭이 같기 때문이다. 약속 넷은 2단계 블록이 그대로 이어 받는다. 줄 나눔 정책 키([wrap-policy-key-design.md](wrap-policy-key-design.md) 244행): 바뀌는 것이 없다. 실행 취소 stack(결정 3): 칸 기록은 칸의 것이 그대로이고 서식은 칸 기록에 든다. 그림 클립보드: 교환 문서가 닫는다.
- **줄 3(X-3·X-4·menu_bar).** 이 문서의 키는 controller를 고치지 않는다. [잉크](ink-design.md) 253행이 적은 controller 직렬은 이 노드에 해당이 없다. 서식 복사·붙여넣기 키의 한 갈래만 교환 문서의 약속으로 줄 3이 낸다. menu_bar의 서식 메뉴는 `make_rich_text_commands`의 결과를 `menu_item_from`으로 짓는다.
- **줄 4(hotkey).** `make_rich_text_commands`는 지정 초기화(`.key`, `.control`, `.shift`)로 짓는다. hotkey 결정 1이 a면 `key_chord`는 그대로이고, 나중에 끝에 칸이 더해져도 깨지지 않는다. `commands.h`·`app_host.h`·`ui_platform.h`·`win32_window.cpp`는 이 노드가 고치지 않는다.
- **같은 파일을 고치는 자리.** `uia_provider.cpp`는 Text 패턴 범위 함수 둘과 `access_text_document`(그 곁의 익명 helper)만 고친다(줄 1·줄 3의 표·메뉴 막대는 요소 provider 쪽이다). `ui_element.h`의 `text_input_snapshot`은 끝에 `spans` 한 칸을 더하고 파일 처음에 `text_span`을 전방 선언한다. 교환 노드의 `clipboard_keys_to_parent`는 그 뒤에 붙는다. `default_text.h`·`.cpp`는 끝에 여섯 낱말을 더한다. kind를 더하지 않아 `ui_element_id.h`·`inspect_names.cpp`·`ui_element_tests.cpp`의 `last[]`는 그대로다.
- **R4 markup.** R4 markup이 서식 글을 선언하면 `rich_text_from_html`로 HTML 조각을 읽는 길을 쓸 수 있다(core 코드라 바로 부를 수 있다). 꼴은 그 문서가 정한다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(헤더, 구현, test, 문서)을 먼저 내고, 데모 연결 커밋은 통합 때 다른 줄의 데모 커밋 뒤에 직렬로 쌓는다. `examples/demo_main.cpp`는 새 페이지 등록 줄만 더한다(include, 내비게이션 한 줄, 메시지 사슬, `build_page` 갈래, 멤버). 두 데모가 함께 쓰는 서식 칸 상태와 상태 글은 새 헤더 `examples/demo/rich_text_sample.h`에 둔다(`readonly_undo_keys.h`와 같은 꼴). 새 편집 칸 `rich-editor`는 `examples/demo/common.h` 끝에 kind·target 상수(`kind_rich_editor_input`, `target_rich_editor`)를, [text_targets.h](../examples/demo/text_targets.h)의 `text_target_of_kind`에 한 줄을 더한다. 거기에 없으면 키 입력이 닿지 않고 UIA Value도 읽기 전용이다. 모바일은 `examples/mobile_demo/common.h` 끝에 같은 상수를, `common.cpp`의 `text_target_of_kind`에 한 줄을 더한다. 이 파일들과 `examples/mobile_demo/basics_page.cpp`는 줄 1 color_picker도 고치므로 데모 연결 커밋은 통합 때 color_picker 뒤에 직렬로 쌓고, kind 숫자는 그때 끝의 다음 수를 쓴다.

## 확인

자동 test.

- `tests/text_format_tests.cpp`(luil_core_tests, 새 파일): 넣기·지우기·대체가 구간을 옮기는지, 넣은 글의 서식 차례(typing_marks, 고른 첫 글자, 앞 글자, 줄 처음의 뒤 글자), 링크 끝에 이어 쓴 글이 링크가 아닌지, 맞닿은 같은 서식이 합쳐지고 빈 구간이 없는지, 경계가 글자소 경계인지, 켜고 끄기 왕복이 처음 값과 `==`인지, `slice`·`bytes`.
- `tests/text_edit_tests.cpp`(luil_core_tests, 끝에 더함): 실행 취소·다시 실행이 글과 서식을 함께 되돌리는지, `text_edit_set_format`이 한 걸음이고 묶이지 않는지, 연속 입력이 한 덩어리로 묶이는지, 바이트 상한이 서식 크기를 세는지, `set_text`가 서식을 비우는지, `replace_all`의 `aa` 경우가 caret 쪽 글자를 지운 것으로 옮기는지, caret 이동이 `typing_marks`를 비우는지.
- `tests/rich_text_tests.cpp`(luil_core_tests, 새 파일): presenter가 같은 포인터에 같은 vector를 돌려주는지, 명령 helper의 id·단축키·`checked`, `match_command`가 Ctrl+B·I·U·K에 맞고 `find_conflicts`가 `make_undo_commands({})`와 겹치지 않는지, key handler가 맞지 않는 키에 nullopt인지.
- `tests/text_area_element_tests.cpp`(luil_core_tests, 끝에 더함): `text_input()`의 `spans`가 설정의 구간을 가리키는지, 코드 구간이 `raised_background` 바탕을 칠하고 밑줄 구간은 칠하지 않는지(raster test 방식, 글꼴이 없어 폭이 4 논리 픽셀인 줄 바꿈 칸으로 본다).
- `tests/uia_provider_tests.cpp`(luil_tests): 굵게·기울임·밑줄 범위의 값, 섞인 범위의 예약 값, `FindAttribute`의 앞뒤 찾기, 조합 중에 답하지 않는지. `tests/text_layout_native_tests.cpp`(luil_tests): code face 구간의 폭이 `styles { code }` 요청과 같고 보통 face와 다른지(설정한 두 글꼴이 다를 때), 코드 구간만 다른 문단이 wrap cache 항목을 갈라 쥐고 밑줄만 다른 문단은 한 항목인지, 밑줄 구간이 글자색 화소를 더 그리는지. core test에는 글꼴과 native 배치가 없어 cache 항목과 밑줄 화소를 이쪽에서 본다.
- `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`(luil_core_tests, 칸 표에 한 줄씩): `rich-editor`의 kind가 `text_target_of_kind`에 등록돼 편집 대상이 있는지.
- 데모 조립: `examples/demo/rich_text_page.cpp`를 `tests/CMakeLists.txt`의 luil_core_tests 데모 묶음에 올리고 `tests/demo_gallery_tests.cpp`에 `rich-state` label과 토글 intent 케이스를 더한다. 모바일 줄은 `tests/mobile_demo_page_tests.cpp`에 이름을 읽는 케이스를 더한다.
- `luil_umbrella_header`, `luil_core_portability`, `luil_source_style`, `luil_default_text`.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다.

- 데스크톱 새 페이지 `examples/demo/rich_text_page.*`: 서식 칸 `rich-editor`, 도구 막대 토글 `rich-bold`·`rich-italic`·`rich-underline`·`rich-code`·`rich-link`, label `rich-state`("굵게 켜짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐 · 구간 3 · 링크 1 · 되돌리기 2"). 앞의 다섯은 `rich_text_state_of`이고 뒤의 셋은 서식 구간 수, 링크 수, 칸 기록의 되돌리기 수다. `rich-link`는 대화 상자 없이 고정 주소(`https://example.com/`)를 걸고, 켜져 있으면 걷는다. UIA(격리 데스크톱)로 칸에 글을 넣고 범위를 골라 토글을 Toggle한 뒤 label과 Text 패턴의 `FontWeight`를 읽는다. PostMessage 키로 Ctrl+B·Ctrl+Z를 보내 서식이 켜졌다 되돌아오는지 읽는다.
- 모바일 데모 `examples/mobile_demo/basics_page.*`: 같은 칸과 손가락 높이 토글, 같은 label. 별점·색 고르기 줄 뒤에 둔다. 데모 연결은 줄 1 데모 커밋 뒤에 쌓는다. Windows 창은 UIA로 읽고 Android는 사람이 화면으로 읽는다.

사람 확인.

- 한글 IME 조합 중 Ctrl+B와 확정 뒤의 서식, 굵게 낱말 끝에서 이어 쓴 조합 글의 서식(Windows).
- Android 실기기에서 화면 키보드 조합 중 굵게 토글을 누르고 확정했을 때의 서식.
- 내레이터가 굵게·기울임·밑줄 범위를 서식으로 읽는지.

## 범위 밖

- 블록 서식(목록, 제목, 인용, 정렬, 들여쓰기)과 문단별 접는 폭. 2단계이고 줄 나눔 약속 넷 위에 선다.
- 글자 크기·색·글꼴 고르기, 취소선. 줄 높이 고정과 첫 소비자가 없음이 까닭이다. `text_mark`의 뒤 비트를 남긴다.
- 인라인 그림과 개체. 서식 도구 막대 요소, 서식 글을 TSF에 알리기(`GetFormattedText`), 한 칸에서 구문 강조와 서식 섞기.
- 수십만 구간 문서의 편집 비용(상대 offset, 조각 나무).

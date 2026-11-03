# 서식 글 교환

[서식 있는 글 편집](rich-edit-design.md)의 글과 서식(`text_format`)을 다른 앱과 주고받고 파일로 남긴다. 복사하면 글과 HTML 조각을 한 클립에 넣고, 붙여넣을 때는 HTML을 걸러 허용한 서식만 받으며, HTML이 없으면 글로 물러선다. 저장도 같은 HTML 쓰기·읽기를 쓴다. 공개 형식은 [ui_events.h](../include/luil/ui/ui_events.h)의 `clipboard_data_copy_request::html`·`clipboard_data::html`과 [rich_text.h](../include/luil/ui/rich_text.h)의 `rich_text_to_html`·`rich_text_from_html`·`rich_text_copy_request`·`rich_text_paste`다. [그림 클립보드](image-clipboard-design.md)가 세운 한 클립 여러 형식 길 위에 선다.

## 문제

- 칸의 복사는 고른 글만 싣는다. Ctrl+C는 4단계 텍스트 칸이 먼저 받아 `copy_focused_selection`이 `clipboard_copy_request { 고른 글 }`을 낸다([ui_interaction.cpp](../src/ui/ui_interaction.cpp) 3258행). controller는 서식을 모르고 칸의 기록은 logic thread에 있다.
- 붙여넣기도 글만이다. Ctrl+V가 낸 `clipboard_paste_request`를 UI thread가 `CF_UNICODETEXT`로 읽어 insert 편집으로 바꾼다([win32_window.cpp](../src/win32/win32_window.cpp) 1912~1926행, [android_app.cpp](../src/android/android_app.cpp) 1748~1758행). 서식을 실을 자리가 없다.
- 여러 형식 클립은 있지만 HTML 칸이 없다. `clipboard_data_copy_request`는 글·그림·PNG(ui_events.h 703행), `clipboard_data`는 글·그림(729행)이다. Win32는 `clipboard_data_blocks`·`read_clipboard_data`([win32_image_clipboard.cpp](../src/win32/win32_image_clipboard.cpp) 91·139행)가 이 둘을 다루고, Android는 글 칸만 넣는다(android_app.cpp 1742행).
- 거르기가 없다. 외부 HTML에는 스크립트, 바깥 자원, 문서 머리, 조건부 주석이 섞여 온다. 저장소에는 HTML 읽기가 없다(마크다운은 core의 md4c, JSON은 nlohmann이다).
- 저장 형식이 없다. 앱이 `text_format`을 파일로 남기려면 형식을 직접 정한다.

## 공개 형식

```cpp
// include/luil/ui/ui_events.h, 두 구조체 끝에 더한다 (기존 칸의 순서와 뜻은 그대로)
// `clipboard_data_copy_request::operator==`는 `= default`가 아니라 손으로 쓴 인라인 함수(746행, text·image·png만 견준다)다. 거기에 `html == other.html`을 더한다.
// 701행 주석의 넣는 차례도 "PNG, CF_DIBV5, HTML Format, CF_UNICODETEXT"로 고친다. `clipboard_data`의 `==`는 `= default`라 새 칸을 그대로 견준다.
struct clipboard_data_copy_request { /* text, image, png */ std::u8string html {}; };  // 조각 HTML(UTF-8). 비면 HTML 형식을 넣지 않는다
struct clipboard_data { /* text, image_format, image, image_too_large, tag, sequence */ std::u8string html {}; };  // 비면 없었거나 읽지 못했다

// include/luil/ui/rich_text.h ([서식 있는 글 편집](rich-edit-design.md)의 헤더에 더한다)
struct rich_text_fragment
{
    std::u8string text {};
    text::text_format format {};
};
struct rich_text_html_limits
{
    std::size_t max_bytes { std::size_t { 16 } * 1024 * 1024 };  // 넘으면 HTML을 버리고 글로 물러선다
    std::size_t max_depth { 256 };                               // 더 깊은 요소는 서식 없이 글만 읽는다
};
// 글을 조각 HTML로 쓴다. 문서 머리 없이 바깥 `<span>` 하나와 `<b>`·`<i>`·`<u>`·`<code>`·`<a href>`·`<br>`만 쓴다.
[[nodiscard]] std::u8string rich_text_to_html(std::u8string_view text, const text::text_format& format);
// HTML을 걸러 읽는다. 허용 밖의 요소는 버리거나 글만 남긴다. 바깥 자원을 읽지 않는다.
[[nodiscard]] rich_text_fragment rich_text_from_html(std::u8string_view html, const rich_text_html_limits& limits = {});
// 고른 구간의 글과 HTML이다. 고른 구간이 없으면 빈 요청이다.
[[nodiscard]] clipboard_data_copy_request rich_text_copy_request(const text::text_edit_state& state);
// 클립보드 답을 caret에 넣는다. `plain`이거나 HTML이 없으면 글을 넣고, 아니면 거른 조각을 넣는다. 그림은 넣지 않는다.
bool rich_text_paste(text::text_edit_state& state, const clipboard_data& data, bool plain = false);

// include/luil/ui/text_area_element.h, `text_area_config` 끝에
// 복사·잘라내기·붙여넣기 키를 칸이 쓰지 않고 `key_handler`로 넘긴다 (서식 칸). `text_input_snapshot`에 같은 이름으로 실린다.
bool clipboard_keys_to_parent { false };

// include/luil/ui/ui_element.h, `text_input_snapshot` 끝에 (칸이 설정에서 옮겨 싣는다)
// 앞 노드가 먼저 `const std::vector<text_span>* spans`(보조 기술의 서식 속성이 읽는 칸 설정의 구간, text_span은 전방 선언)를 더했다. 이 칸은 그 뒤다.
bool clipboard_keys_to_parent { false };
```

앱의 길은 이렇다. 칸에 단 `make_rich_text_key_handler`가 Ctrl+C를 `rich_text_command::copy` 앱 메시지로 낸다. logic thread가 `app_host::post_clipboard_request(rich_text_copy_request(state))`를 부른다. 잘라내기는 그 뒤에 `text_edit_erase_selection`을 부른다. 붙여넣기는 `clipboard_data_paste_request { tag }`를 넣고, `app_delegate::make_clipboard_data_message`의 답을 받은 `handle`이 `rich_text_paste`를 부른다.

## 규칙

**키 경로.**

- `clipboard_keys_to_parent`인 칸에서는 Ctrl+C·Ctrl+X·Ctrl+V(Shift 포함), Ctrl+Insert, Shift+Insert, Shift+Delete가 4단계에서 소비되지 않고 8단계 `key_handler`로 간다. 이 갈래는 controller의 것이라 줄 3이 낸다(약속 절). 읽기 전용 칸이 Ctrl+X·Ctrl+V를 삼키는 갈래는 그 앞이라 그대로다.
- `make_rich_text_key_handler`는 명령표와 별도로 위의 고정 조합을 `copy`·`cut`·`paste`로, Ctrl+Shift+V를 `paste_plain`으로 옮긴다. 이 조합은 사용자가 다시 정하지 않는다(편집의 표준 키다).
- 줄 3의 갈래가 들어오기 전이나 플래그를 끈 칸은 지금 길 그대로다. 복사는 평문이고, 붙여넣은 글은 넣은 글의 서식 규칙(앞 글자를 이어받음)을 따른다. 깨지는 것은 없고 서식만 빠진다.
- 암호 칸은 서식 칸이 아니다. 플래그를 세워도 칸이 `secret`이면 controller가 지금처럼 복사를 막는다.

**쓰기(복사·저장).**

- 서식이 같은 조각마다 끊어 태그를 열고 닫는다. 바깥부터 `<a href>`, `<code>`, `<b>`, `<i>`, `<u>` 차례다. 겹침을 다시 짜지 않으므로 늘 바르게 닫힌다.
- 글의 `&`·`<`·`>`, 속성의 `"`를 이스케이프하고 줄 바꿈은 `<br>`이다. CR은 `&#13;`로 써서 읽기가 글자 그대로 되돌린다(Windows 붙여넣기가 남긴 CR LF도 왕복한다). 공백 여럿과 탭은 `white-space: pre-wrap`을 건 바깥 `<span>` 하나로 지킨다. 빈 글은 빈 HTML이다.
- 왕복: `rich_text_from_html(rich_text_to_html(t, f))`는 1단계 서식에서 `{ t, f }`와 같다.

**읽기(붙여넣기·불러오기).**

- 서식: `b`·`strong`은 굵게, `i`·`em`은 기울임, `u`·`ins`는 밑줄, `code`·`kbd`·`samp`·`tt`는 코드다. `style` 속성은 `font-weight`(bold, bolder, 600 이상은 켬, normal·400은 끔), `font-style`(italic, oblique), `text-decoration`·`text-decoration-line`(underline, none), `white-space`(pre, pre-wrap은 공백을 지킨다)만 읽고, 태그보다 앞선다. 문서 편집기는 굵게를 `style`로만 싣거나 감싸는 `<b>`를 `font-weight: normal`로 끄기 때문이다.
- 링크: `a`의 `href`가 `http:`·`https:`·`mailto:`(대소문자 무시, 앞뒤 공백 무시)일 때만 링크다. 다른 scheme(`javascript:`, `data:`, `file:`)과 scheme 없는 상대 주소는 링크 없이 글만 남긴다.
- 구조: `br`은 줄 바꿈이고 그 자리의 서식과 링크를 받는다. `p`·`div`·`li`·`h1`~`h6`·`blockquote`·`pre`·`tr`과 `ul`·`ol`·`table`·`dl`·`dt`·`dd`·`section`·`article`·`header`·`footer`는 블록이라 앞뒤에 줄 바꿈 하나를 둔다(겹치지 않고, 서식이 없고, 글이 이어질 때만 나온다). `hr`은 블록 경계다. `td`·`th` 사이는 탭이다. 블록의 모양(제목 크기, 목록 기호)은 1단계에 버린다.
- 버림: `script`·`style`·`head`·`title`·`template`·`noscript`·`svg`·`math`·`iframe`·`object`는 내용째 버린다. 내용이 글인 `script`·`style`·`title`·`iframe`·`noscript`(와 `xmp`·`noembed`·`noframes`)는 닫는 태그까지 글자 그대로 건너뛰고, 내용이 markup인 `head`·`template`·`svg`·`math`·`object`는 숨긴 채 짝을 맞춘다. `body`가 열리면 닫지 않은 `head`를 닫는다. `img`는 버린다(대체 글도 넣지 않는다). 주석, `<!doctype>`, CDATA, 처리 지시는 버린다. 그 밖의 모르는 요소는 태그만 버리고 글을 남긴다(`style`은 읽는다). 스스로 닫은 태그(`<b/>`)는 열지 않는다.
- 글: 문자 참조는 숫자 꼴과 `amp`·`lt`·`gt`·`quot`·`apos`·`nbsp`(U+00A0)를 푼다. 모르는 이름과 `;` 없는 이름은 글 그대로이고, 0·surrogate·`U+10FFFF` 밖의 숫자는 U+FFFD다. 참조가 낸 글자는 공백 접기를 받지 않는다. `pre`와 공백을 지키는 `white-space` 밖의 공백 여럿은 하나로 접고(처음 만난 공백 자리의 서식과 링크를 받는다) 줄 처음과 블록 가장자리의 공백은 버린다. 지키는 공백의 CR LF와 CR은 줄 바꿈 하나다.
- 한도: `max_bytes`를 넘거나 깨진 UTF-8이면 HTML을 버리고 `clipboard_data::text`로 물러선다. `max_depth`보다 깊은 요소는 서식 없이 글만 읽는다. 그런 요소는 스택에 올리지 않고 수만 세며(닫는 태그 하나가 하나를 지운다), 숨길 요소는 닫는 태그까지 건너뛴다. 끝나지 않은 태그·따옴표를 만나면 거기서 멈춘다. 읽기는 바깥 주소를 풀거나 가져오지 않는다.
- 붙여넣은 조각은 자기 서식을 그대로 가진다(`text_edit_insert_formatted`, 실행 취소 한 덩어리). 주변 서식과 섞지 않는다. 서식 없이 붙이는 길은 `paste_plain`이다.

**그림.** `clipboard_data`에 그림만 있으면 `rich_text_paste`는 거짓을 돌려주고 아무것도 넣지 않는다. 글과 그림이 함께 오면 글만 넣는다. 그림이 필요한 앱은 같은 답에서 `decode_clipboard_image`로 따로 읽는다. 인라인 그림은 1단계에 없다.

## 결정

**7. 서식 복사·붙여넣기의 길.** 질문: Ctrl+C·Ctrl+V로 서식을 주고받을 때 요청을 어디서 짓는가.

- 길 a: 칸의 플래그로 키를 `key_handler`에 넘기고, logic thread가 `clipboard_data_*` 요청을 짓는다(이 문서). 되는 것: 서식과 기록이 있는 logic thread에서 HTML을 짓는다. `win32_window.cpp`의 여러 형식 분기(1888~1910행)와 Android 분기가 이미 있어 줄 4 파일을 고치지 않는다. 줄 3은 세 줄 남짓의 갈래 하나다. 대가: 줄 3과 병합 차례가 묶이고, 갈래가 들어오기 전까지 키로는 평문이다.
- 길 b: `input_action` 끝에 `clipboard_data_copy_request`를 더하고([그림 클립보드](image-clipboard-design.md) 결정 6의 b) controller가 snapshot의 서식으로 HTML을 짓는다. 붙여넣기는 `text_edit_request`에 서식 조각을 싣는다. 되는 것: 앱 메시지 한 단계가 없다. 대가: controller가 HTML 쓰기를 알게 되고, 붙여넣기가 `apply_text_edit`(줄 3)와 `win32_window.cpp`(줄 4)의 붙여넣기 분기를 함께 고친다.
- 길 c: 1단계는 평문만 키로 하고 서식 복사는 앱의 단추·메뉴 명령으로만 한다. 되는 것: 다른 줄 파일을 고치지 않는다. 대가: Ctrl+C로 복사한 서식이 사라지는 것이 기본 동작이 되어 서식 칸의 뜻이 약해진다.
- 권장: a. 대가: 위의 병합 차례다. 갈래가 늦어도 평문으로 물러서므로 소리 없이 버려지는 요청이 없다. **승인: 길 a (2026-11-03).**

**8. 교환 형식.** 질문: 어떤 클립보드 형식을 넣고 읽는가.

- 길 a: HTML 하나(Win32 등록 형식 "HTML Format", Android `ClipData.newHtmlText`·`getHtmlText`)와 글(이 문서). 되는 것: 브라우저·문서 편집기·워드 프로세서가 모두 HTML을 넣고 읽는다. luil 사이 왕복이 정확하다. 대가: RTF만 읽는 앱에는 평문이 붙는다.
- 길 b: HTML과 RTF. 되는 것: RTF만 읽는 오래된 편집기와도 서식이 간다. 대가: RTF 쓰기·읽기와 그 거르기를 따로 짓는다. Android에는 RTF 관례가 없다.
- 길 c: luil 전용 등록 형식(서식 값 그대로)과 HTML. 되는 것: luil 사이에서 거르기를 거치지 않는다. 대가: 1단계 서식은 HTML 왕복이 이미 정확해 얻는 것이 없고 형식이 하나 는다.
- 권장: a. 그림 클립보드가 열어 둔 `rtf` 칸은 더하지 않는다. 대가: 위의 RTF 앱이다. **권장대로 진행.**

**9. 거르기의 폭.** 질문: 붙여넣은 HTML에서 무엇을 서식으로 받는가.

- 길 a: 허용 태그와 `style`의 세 속성(이 문서). 되는 것: 태그로 싣는 브라우저와 `style`로 싣는 문서 편집기 모두에서 굵게·기울임·밑줄이 온다. 대가: CSS 값 일부를 읽는 작은 파서가 든다.
- 길 b: 허용 태그만. 되는 것: 파서가 가장 작다. 대가: `style`로만 굵게를 싣는 편집기의 서식이 사라지고, `font-weight: normal`로 끈 감싸는 `<b>` 때문에 글 전체가 굵게 붙는다.
- 길 c: 서식을 버리고 HTML에서 글만 꺼낸다. 되는 것: 가장 안전하다. 대가: 서식 붙여넣기가 없다.
- 권장: a. 대가: 위의 파서다. 서식 값 셋 밖의 CSS는 읽지 않는다. **권장대로 진행.**

**10. 저장 형식.** 질문: 앱이 서식 글을 저장할 때 라이브러리가 어떤 형식을 주는가.

- 길 a: 클립보드와 같은 HTML 쓰기·읽기(이 문서). 되는 것: 코드가 한 벌이고 1단계 서식이 정확히 왕복하며 다른 프로그램이 연다. 대가: 사람이 원문으로 고치기에는 마크다운보다 어렵다.
- 길 b: 마크다운 쓰기와 `parse_markdown`으로 읽기. 되는 것: 사람이 읽는 원문이다. 대가: 밑줄에 마크다운 문법이 없고 `parse_markdown`은 HTML을 글로 표시해([markdown_element.h](../include/luil/ui/markdown_element.h) 31행) 밑줄이 왕복하지 않는다. 쓰기를 새로 짓는다.
- 길 c: 자체 JSON(글, `runs`, `links`). 되는 것: 정확하고 2단계 블록을 더하기 쉽다. 대가: 다른 프로그램이 열지 못하고, 서식 값은 공개 구조체라 앱이 스스로 지을 수 있는 것을 라이브러리가 하나 더 든다.
- 권장: a. 대가: 위의 원문 편집이다. **권장대로 진행.**

**구현 결정 C. 고정 키 판정은 공개하지 않는다.** 복사·붙여넣기 키를 명령으로 옮기는 판정은 `rich_text_commands.cpp` 안의 함수이고 `make_rich_text_key_handler`가 명령표보다 먼저 부른다. 앱이 볼 것은 처리기가 내는 `copy`·`cut`·`paste`·`paste_plain` 메시지뿐이다. 판정을 공개하면 사용자가 다시 정하지 않는 키를 앱이 따로 다룰 자리가 생겨 두 칸의 키가 갈린다.

**구현 결정 D. Win32 HTML 읽기 상한.** `win32_image_clipboard.h`의 `max_clipboard_html_bytes`(16 MiB, `rich_text_html_limits::max_bytes`와 같은 값)를 넘는 조각은 UI thread에서 복사하지 않고 `html`을 비운다. 다른 프로세스가 정한 블록 크기라 상한이 없으면 붙여넣기 한 번이 UI thread에서 그만큼을 잡는다. 붙여넣기는 글로 물러선다. Android는 framework가 이미 Java 글로 들고 있어 따로 자르지 않는다.

**구현 결정 E. 데모의 클립보드 요청.** 견본(`examples/demo/rich_text_sample.h`)이 복사·붙여넣기 명령을 받으면 요청(`clipboard_data_copy_request`나 표식 단 `clipboard_data_paste_request`)을 지어 쥐고, host를 쥔 셸이 같은 logic turn에 `take_clipboard_request`로 가져가 넣는다. 붙여넣기 답은 셸의 `make_clipboard_data_message`가 표식(`rich_text_clipboard_tag`)으로 갈라 `rich_text_clipboard_intent`로 짓는다. 모바일 driver는 host를 몰라 delegate가 `on_started`에서 `clipboard_host_intent { &host }`를 한 번 보낸다(데스크톱 셸이 `host_`를 쥐는 것과 같은 수명). 다른 길은 앱 UI 명령의 인자에 HTML을 싣는 것인데, 잘라내기가 두 메시지로 갈리고 글을 HTML에서 다시 꺼내야 한다.

## 플랫폼

**Win32.** `win32_image_clipboard.cpp`의 `clipboard_data_blocks`가 `html`이 있으면 "HTML Format" 블록을 `CF_UNICODETEXT` 앞에 더한다. 넣는 차례는 "PNG", `CF_DIBV5`, "HTML Format", `CF_UNICODETEXT`이고 하나라도 실패하면 거두는 `put_clipboard_blocks` 규칙이 그대로다. 블록은 `Version:0.9`와 `StartHTML`·`EndHTML`·`StartFragment`·`EndFragment` 머리(열 자리 0 채움이라 머리 길이가 값과 상관없다, UTF-8 바이트 offset) 뒤에 `<html><body>`와 `<!--StartFragment-->`·`<!--EndFragment-->` 주석으로 감싼 조각이고, 끝에 NUL 한 바이트를 붙인다(`EndHTML`은 그 앞). `read_clipboard_data`는 같은 한 번 열기 안에서 그 형식을 읽어 머리의 조각 offset을 `GlobalSize` 범위(첫 NUL까지) 안에서 확인하고, 조각 offset이 없거나 맞지 않으면 `StartHTML`~`EndHTML`을 쓴다(`EndHTML:-1`은 블록 끝). 머리는 CRLF·LF 줄을 모두 읽고 `SourceURL` 같은 모르는 키는 넘긴다. 둘 다 맞지 않거나 깨진 UTF-8이면 `html`을 비운다. 머리 짓기와 풀기는 순수 함수(`cf_html_block`, `cf_html_fragment`)로 두어 test가 직접 본다. `win32_window.cpp`와 `win32_clipboard.cpp`는 고치지 않는다.

**Android.** Java 소스는 0줄이다. `android_clipboard.cpp`가 `ClipData.newHtmlText(label, text, html)`(API 16) 정적 메서드를 JNI로 불러 글과 HTML을 한 항목에 넣는 `copy_rich_text_to_clipboard`를 두고(HTML이 비면 `copy_text_to_clipboard`), 읽기(`read_clipboard_data`)는 글을 집은 그 항목의 `getHtmlText()`를 함께 부른다. 둘 다 기존 framework 클래스를 부르는 것이라 Java 클래스를 쓰지 않는다. `android_app.cpp`의 `clipboard_data_copy_request` 분기(1742행)는 `copy_text_to_clipboard` 대신 그 새 함수를 부르는 한 줄만 바뀐다. 이 노드의 브랜치는 `android_app.cpp`를 고치지 않아 그 한 줄은 R3 통합 커밋이 냈다. 그 뒤로 Android의 여러 형식 복사도 글과 HTML을 한 항목에 넣는다(글이 비면 넣지 않는 기존 규칙은 그대로다). 터치 편집 메뉴의 복사·붙여넣기는 평문 그대로다. 메뉴의 실행(`android_app.cpp` 1290행 `execute_edit_menu`)이 controller를 거치지 않고 고른 글을 `copy_text_to_clipboard`로 넣고 붙여넣기를 `clipboard_paste_request`로 바로 실행하므로 칸의 플래그가 닿지 않는다. unsupported로 둘 부분은 없다.

## 다른 노드와의 약속

- **줄 3(controller).** `process_text_input_key`에 갈래 하나: snapshot의 `clipboard_keys_to_parent`가 참이고 `secret`이 아니면 위의 복사·잘라내기·붙여넣기 조합에 nullopt를 돌려준다. 읽기 전용 갈래 뒤, `multiline` 갈래 앞이다. 필드는 이 노드가 `ui_element.h`의 `text_input_snapshot` 끝과 `text_area_config` 끝에 더하므로, 통합 때 줄 2를 먼저 합치고 줄 3의 갈래 커밋과 그 test(`tests/key_propagation_tests.cpp` 끝)를 그 뒤에 쌓는다. `navigation_keys_to_parent`(ui_element.h 160행)와 같은 꼴이다.
- **그림 클립보드(R2).** 그 문서가 이 노드에 남긴 것을 닫는다([image-clipboard-design.md](image-clipboard-design.md) 205행). `html`은 두 구조체 끝에 더하고(앞 구조체의 손으로 쓴 `operator==`에도 한 칸) 기존 칸은 그대로다. `rtf`는 결정 8로 더하지 않는다. 입력 thread에서 짓는 여러 형식 복사(그 문서 결정 6의 b)는 결정 7로 하지 않는다. 그림 붙여넣기는 1단계에 넣지 않는다.
- **줄 4(tray·hotkey).** `win32_window.cpp`·`app_delegate.h`·`app_host.h`·`ui_platform.h`를 고치지 않는다. HTML은 두 플랫폼 모두 다뤄 능력 칸이 늘지 않는다. `android_app.cpp`는 위의 분기 한 줄만이고(R3 통합 커밋이 냈다) 줄 4가 같은 파일을 고치면 다른 함수다.
- **R4 markup.** R4 markup이 서식 글을 선언하면 `rich_text_from_html`로 HTML 조각을 읽는 길을 쓸 수 있다(core 코드라 바로 부를 수 있다). 꼴은 그 문서가 정한다.
- **데모는 따로 커밋한다.** [서식 있는 글 편집](rich-edit-design.md)의 데모 페이지가 복사·붙여넣기 단추와 클립보드 답 연결(`make_clipboard_data_message`의 tag)을 함께 가진다. 이 노드의 데모 커밋이 `examples/demo_main.cpp`의 delegate 갈래 한 줄과 페이지 처리 앞의 요청 넣기, `examples/mobile_demo/app.*`·`main.cpp`·`android_main.cpp`의 host 전달(구현 결정 E)을 함께 내고, 통합 때 다른 줄의 같은 파일 변경 뒤에 직렬로 쌓는다.

## 확인

자동 test.

- `tests/rich_text_html_tests.cpp`(luil_core_tests, 새 파일): 쓰기·읽기 왕복이 같은 값인지, 이스케이프와 `<br>`, 서식 경계마다 바르게 닫히는지, `style`이 태그보다 앞서는지(`<b style="font-weight:normal">`), 허용 scheme만 링크인지, 버리는 요소의 내용이 사라지는지, 블록·표의 줄 바꿈과 탭, 문자 참조, 공백 접기와 `pre`, `max_bytes`·`max_depth`·깨진 UTF-8에서 물러서는지.
- `tests/rich_text_tests.cpp`(luil_core_tests, 끝에 더함): `rich_text_copy_request`가 고른 구간의 글·HTML을 싣는지, `rich_text_paste`가 HTML·글·`plain`·그림만 있는 답에서 각각 무엇을 넣고 한 걸음으로 되돌아가는지, key handler가 고정 조합을 `copy`·`cut`·`paste`·`paste_plain`으로 옮기는지.
- `tests/clipboard_image_tests.cpp`(luil_core_tests, 끝에 더함): `html`만 다른 두 `clipboard_data_copy_request`가 같지 않고 `html`이 같으면 같은지. 손으로 쓴 `operator==`가 새 칸을 보는지를 잠근다.
- `tests/win32_image_clipboard_tests.cpp`(luil_tests): `cf_html_block` 머리 offset이 맞는지, `cf_html_fragment`가 잘리거나 범위 밖인 offset을 거절하는지, `EndHTML:-1`·LF 머리·첫 NUL 뒤 바이트, 블록 차례("PNG", `CF_DIBV5`, "HTML Format", `CF_UNICODETEXT`). `[system]` 태그 test가 실제 클립보드에 글·HTML을 넣고 되읽어 같은지. 기존 `[system]` test처럼 일반 ctest 안에서 돌고, 시작 전 클립보드의 글과 HTML을 쥐었다 끝에 되돌린다.
- `tests/text_area_element_tests.cpp`(luil_core_tests, 끝에 더함): `clipboard_keys_to_parent`가 `text_input()`에 실리는지.
- Android: JNI 길은 구현 때 `.cpp`를 묶은 확인 프로그램을 `run-as`로 실기기에서 돌려 `newHtmlText`로 넣고 `getHtmlText`로 되읽는다.
- `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`(luil_core_tests, 끝에 더함): 단추 이름, 복사 요청의 글·HTML, 붙여넣기 요청의 표식, 답을 넣은 뒤의 `rich-clipboard`·`rich-state`, 실행 취소, 칸 처리기의 Ctrl+C·Ctrl+Shift+V.

데모. 데스크톱 서식 페이지에 단추 `rich-copy`("서식 복사")·`rich-paste`("서식 붙여넣기")와 label `rich-clipboard`("복사 · HTML 412바이트 · 굵게 2 · 링크 1", 붙여넣기 뒤에는 "붙여넣기 · …"나 "붙여넣기 · 글만")를 둔다. 수는 실은 HTML을 다시 거른 값이라 왕복을 함께 보인다. UIA(격리 데스크톱)로 범위를 골라 `rich-copy`를 Invoke하고 `rich-paste` 뒤 `rich-state`의 구간 수가 늘어나는지 읽는다. 키는 PostMessage로 Ctrl+C·Ctrl+V를 보낸다(줄 3 갈래가 들어온 뒤의 기대다). 모바일 기본 페이지의 서식 칸 아래에도 같은 단추 둘과 `rich-clipboard`를 둔다.

사람 확인.

- 브라우저와 워드 프로세서에서 서식 글을 복사해 붙이고, 반대로 칸에서 복사해 그쪽에 붙일 때 굵게·기울임·밑줄·링크가 가는지(Windows).
- Android 실기기에서 다른 앱의 서식 글을 붙일 때와 이 앱에서 복사한 글을 다른 앱에 붙일 때.

## 범위 밖

- RTF 쓰기·읽기, luil 전용 클립보드 형식, 인라인 그림 붙여넣기.
- 마크다운 쓰기와 마크다운 불러오기, 자체 JSON 형식.
- 블록 서식의 교환(제목 크기, 목록 기호, 표 모양). 2단계 블록이 서면 같은 쓰기·읽기에 더한다.
- Android 터치 편집 메뉴의 서식 복사.

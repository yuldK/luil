# 화면 마크업

앱이 실행 중에 마크업 글을 읽어 화면의 한 가지를 element tree로 펼친다. 글은 한 번 읽어 불변인 틀(`ui_markup_document`)로 두고, 앱이 `make_frame`마다 그 틀을 자기 상태로 펼쳐 `std::unique_ptr<ui_element>`를 얻는다. 펼친 결과는 손으로 지은 요소와 같은 형·같은 config라 앱이 지은 tree의 한 자리에 그대로 끼운다. 문법은 XML 1.0의 엄한 부분집합이고(결정 1) 1단계 어휘는 컨테이너와 config가 값뿐인 요소다(결정 3). 공개 형식은 새 헤더 `include/luil/ui/ui_markup.h`의 `parse_ui_markup`·`ui_markup_document`·`ui_markup_parse_result`·`ui_markup_limits`·`build_ui_markup`이고, 라이브러리 kind `markup_node`가 하나 는다. 값·사건·id 바인딩과 오류의 꼴은 [마크업 바인딩과 오류](markup-binding-design.md)가 다룬다. 이름은 글 본문을 그리는 [markdown_element.h](../include/luil/ui/markdown_element.h)의 `markdown_*`과 섞이지 않도록 늘 `ui_markup`으로 시작한다(결정 2).

## 문제

- 화면은 C++로만 짓는다. [hello_app.cpp](../examples/hello/hello_app.cpp) 84~107행은 요소마다 `ui_element_id`·config·길이·액션을 손으로 적는다. 글 하나, 간격 하나를 바꿔도 다시 빌드한다. R5 hot_reload가 "마크업 파일을 저장하면 다음 frame에 반영"하려면 구조를 실행 중에 읽는 길이 먼저 있어야 한다(N3 a).
- 엄한 마크업 읽기가 없다. core에는 nlohmann(JSON, 스타일 파서가 안쪽에서만 쓴다, `src/CMakeLists.txt` 700~703행), md4c(마크다운, 889~891행), `ui/rich_text_html.cpp`(372행)가 있다. 마지막 것은 남의 HTML을 너그럽게 거르는 읽기라 모르는 요소의 글을 남기고 끝나지 않은 태그에서 조용히 멈춘다([서식 글 교환](rich-edit-exchange-design.md) 73~75행). 틀린 자리를 알려야 하는 화면 정의에는 쓰지 못한다. Skia 꾸러미에는 SVG 모듈 안에서만 쓰이는 비공개 XML 읽기가 있고(`svg_decode.cpp`가 쓰는 `SkSVGDOM`) 범용 XML 읽기 API는 공개되지 않는다.
- 이름으로 요소를 짓는 표가 없다. 요소는 대부분 `xxx_element(ui_element_id, xxx_config)`이지만 `check_element`는 config의 `owner`만 받고 kind를 스스로 정한다([check_element.h](../include/luil/ui/check_element.h) 33·76행). config에는 값 칸과 함수 칸이 섞여 있다. `panel_config::background`는 팔레트 선택자다([panel_element.h](../include/luil/ui/panel_element.h) 31행).
- 길이는 담을 때 정한다([stack_element.h](../include/luil/ui/stack_element.h) 98행). 원하는 크기를 답하는 요소(label 75행, 글자 단추 `dialog_elements.h` 209행, check 90행, stack 139행)만 맞춤(`add_fit`)으로 담는다. panel·progress·wrap은 답하지 않아 맞춤이면 0이 된다.
- 앞 노드가 R4 markup에 남긴 약속이 넷이다. [스타일 읽기](style-sheet-design.md) 228행(소스만 더하고 오류 꼴을 맞춘다), [줄 나눔](line-break-design.md) 166행과 [줄 나눔 정책 스타일 키](wrap-policy-key-design.md) 245행(정책 칸), [서식 있는 글 편집](rich-edit-design.md) 210행과 [서식 글 교환](rich-edit-exchange-design.md) 127행(서식 글의 꼴), [파일 감시](file-watch-design.md) 234행(감시는 그 API만)이다. 이 문서와 바인딩 문서가 닫는다(약속 절).

## 공개 형식

```cpp
// include/luil/ui/ui_markup.h (ui_element.h·ui_inspect.h를 include한다. 읽기의 형을 내지 않는다)
namespace luil {
    // 읽기와 펼치기의 한도다. 마크업 글을 신뢰하지 않는 파일로 보고 정한다 (markup-binding-design.md).
    struct ui_markup_limits
    {
        std::size_t max_bytes { std::size_t { 1 } * 1024 * 1024 };
        std::size_t max_depth { 64 };     // 요소 중첩이다. for·if·unless도 한 단이다
        std::size_t max_nodes { 10000 };  // 틀의 요소 수다
        std::size_t max_built { 50000 };  // 한 번 펼칠 때 짓는 element 수다. 틀이 쥐고 펼칠 때 쓴다
    };

    // 읽은 틀이다. 이름·속성·수·자리를 모두 확인하고 enum과 수를 C++ 값으로 바꿔 둔 불변 값이다. 속은 비공개다.
    class ui_markup_document;

    struct ui_markup_parse_result
    {
        std::shared_ptr<const ui_markup_document> document {};  // 오류가 하나라도 있으면 비어 있다
        std::vector<ui_markup_error> errors {};                 // document가 있으면 비어 있다
        [[nodiscard]] bool ok() const noexcept { return document != nullptr; }
    };

    // 마크업 글(UTF-8)을 읽는다. 파일을 읽지 않는다. 전역 상태가 없어 어느 thread에서나 부른다.
    [[nodiscard]] ui_markup_parse_result parse_ui_markup(std::u8string_view text, const ui_markup_limits& limits = {});

    // 틀을 앱 값으로 펼친다. 앱 함수를 부르므로 logic thread(`make_frame`)에서 부른다.
    // `ui_markup_context`·`ui_markup_build_result`·`ui_markup_error`는 markup-binding-design.md에 있다.
    [[nodiscard]] ui_markup_build_result build_ui_markup(const ui_markup_document& document, const ui_markup_context& context);
}

// include/luil/ui/ui_element_id.h, `end_of_library_kinds` 바로 위 (src/ui/inspect_names.cpp의 case를 같은 커밋에)
// 마크업이 지은 요소다 (`build_ui_markup`). owner는 마크업의 id다. kind 속성을 적은 요소는 그 앱 kind다.
markup_node,
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- 누른 횟수를 세는 칸 -->
<stack id="counter" direction="column" spacing="12" padding="24">
  <label id="count" text="{countText}" fontSize="14" color="primary"/>
  <stack direction="row" spacing="8" length="30">
    <textButton id="more" text="하나 더" action="increment"/>
    <check id="list" label="목록 보기" checked="{showList}" action="toggleList"/>
  </stack>
  <if test="{showList}">
    <for each="{items}" as="item" key="{item.key}">
      <textButton id="item" text="{item.title}" visual="link" action="select" argument="{item.key}"/>
    </for>
  </if>
  <slot name="search" length="28"/>
</stack>
```

```cpp
// 앱의 logic thread. 읽기는 처음과 다시 읽을 때 한 번이다. 실패하면 쥔 틀을 바꾸지 않는다.
auto parsed { luil::parse_ui_markup(text) };
if (parsed.ok())
    document_ = parsed.document;
// make_frame마다. 값 표·사건 함수·slot 함수는 바인딩 문서의 `ui_markup_context`다.
luil::ui_markup_build_result built { luil::build_ui_markup(*document_, context_for(state_)) };
built.element->arrange({ body_slot, scale });
root->add(std::move(built.element));
```

## 규칙

**문법은 XML 1.0의 엄한 부분집합이다.** 받는 글은 모두 바른 XML이라 XML 편집기와 검사 도구가 같은 글을 받는다. 받지 않는 구문은 조용히 넘기지 않고 오류다(오류 종류는 바인딩 문서).

- UTF-8만 받는다. 맨 앞의 BOM은 건너뛰고 깨진 UTF-8은 `syntax`다. 맨 앞의 XML 선언(`<?xml version="1.0" encoding="UTF-8"?>`)은 받고 그 밖의 처리 지시는 `unsupported`다. 선언은 `version`(1.0만), `encoding`(UTF-8만, 대소문자 무시), `standalone`(`yes`·`no`) 차례이고, 1.0 밖의 version과 UTF-8 밖의 encoding은 `unsupported`, 그 밖에 틀린 선언과 맨 앞이 아닌 선언은 `syntax`다.
- `<!DOCTYPE`, CDATA, 이름 공간(이름 속의 `:`, `xmlns` 속성)은 `unsupported`다. DTD가 없으므로 글이 정하는 엔티티도 바깥 엔티티도 없다.
- 엔티티는 `&amp;`·`&lt;`·`&gt;`·`&quot;`·`&apos;`와 숫자 참조(`&#10;`, `&#x41;`)뿐이다. 문자 참조와 본문 글자는 XML 1.0 Char 범위(U+0009·000A·000D·0020~D7FF·E000~FFFD·10000~10FFFF)만 받고 그 밖(`&#0;`, `&#1;`, surrogate, U+FFFE·FFFF, U+10FFFF 밖)은 `syntax`다. XML이 막는 주석 속 `--`, 글 속 `]]>`, 속성 값의 `<`도 `syntax`다.
- 주석(`<!-- -->`)은 요소 사이 어디든 둔다. 요소 사이의 공백뿐인 글(XML의 S: 공백·탭·CR·LF)은 버린다. U+00A0과 참조로 적은 공백(`&#32;`)은 글이다. 공백이 아닌 글은 글을 받는 요소 안에서만 된다. 1단계에는 그런 요소가 없어 `invalid_child`다.
- 속성 값은 큰따옴표나 작은따옴표로 감싼다. 같은 속성이 둘이면 `duplicate_attribute`다. 속성 값의 줄 바꿈과 탭은 XML 규칙대로 공백 하나가 된다. 글 속의 줄 바꿈은 `&#10;`로 적는다.
- 뿌리 요소는 하나이고 어휘의 요소다. `for`·`if`·`unless`·`gap`·`slot`은 뿌리가 되지 못한다. 뿌리 뒤에는 공백과 주석만 온다.
- 줄은 LF, CR LF, CR을 모두 한 줄 바꿈으로 센다. 열은 줄 처음부터 센 코드 포인트다(1부터). 맨 앞의 BOM은 열로 세지 않고 `offset`에는 든다. 문법 오류(`syntax`·`unsupported`·`too_large`)는 형제를 다 읽기 전이라 `path`가 비고 줄·열이 자리를 말한다. 닫지 않은 요소는 그 여는 태그를, 닫지 않은 값·주석은 그 속성·주석의 처음을 가리킨다.
- 읽기와 어휘 확인은 재귀하지 않는다. 열린 요소를 목록으로 쥐어, 앱이 `max_depth`를 크게 올려도 호출 깊이가 글을 따라 깊어지지 않는다.

**어휘(1단계).** 요소 이름은 C++ 클래스 이름에서 `_element`를 떼고 camelCase로 쓴다. 속성 이름은 config 필드 이름을 camelCase로 쓴다(`font_size` → `fontSize`). enum 값도 열거자를 camelCase로 쓴다(`toggle_switch` → `toggleSwitch`, `hangul_words` → `hangulWords`). [스타일 읽기](style-sheet-design.md) 116행과 같은 규칙이고, 이름과 C++ 값의 짝은 어휘 소스의 constexpr 표가 쥔다. 실행 중 이름 바꾸기는 없다.

| 요소 | C++ | id | 속성 | 자식 | 맞춤 |
| --- | --- | --- | --- | --- | --- |
| `stack` | `stack_element` | kind 속성·`markup_node` | `direction`(`column`·`row`), `spacing`, `padding`, `crossAlignment`, `mainAlignment` | 여럿, 자리 속성 | 답한다 |
| `wrap` | `wrap_element` | kind 속성·`markup_node` | `itemWidth`, `itemHeight`, `spacing`, `lineSpacing` | 여럿 | 답하지 않는다 |
| `panel` | `panel_element` | kind 속성·`markup_node` | `background`(역할), `cornerRadius`, `shadow` | 하나 | 답하지 않는다 |
| `label` | `label_element` | kind 속성·`markup_node` | `text`, `fontSize`, `fontWeight`, `slant`, `color`, `background`, `padding`, `align`, `elide`, `wrap`, `lineBreak`, `maxLines` | 없음 | 답한다 |
| `textButton` | `text_button_element` | kind 속성·`markup_node`, `id` 꼭 | `text`, `visual`, `defaultButton`, `selected`, `action`, `argument` | 없음 | 답한다 |
| `check` | `check_element` | `check` 고정, `id` 꼭 | `style`, `label`, `checked`, `mixed`, `action`, `argument` | 없음 | 답한다 |
| `progress` | `progress_element` | kind 속성·`markup_node` | `value`, `thickness` | 없음 | 답하지 않는다 |
| `gap` | `add_gap`·`add_flexible_gap` | 없다 | `length` 또는 `weight` | 없음 | `stack` 안에서만 |
| `slot` | 앱이 짓는다 | 앱 요소의 것 | `name` | 없음 | 자리를 적어야 한다 |
| `for`·`if`·`unless` | 구조 | 없다 | `each`·`as`·`key`, `test` | 여럿 | 부모 자리로 펼친다 |

- 공통 속성은 `id`, `kind`(`check` 밖), `enabled`, `visible`, `tooltip`, `accessName`이다. 각각 `ui_element`의 `set_enabled`·`set_visible`·`set_tooltip`·`set_access_name`(ui_element.h 1217~1237행)으로 가고 `check`의 `tooltip`은 `check_config::tooltip`이다. `textButton`은 손 모양 커서를 건다(hello와 같다). `gap`·`slot`·구조 요소는 공통 속성을 받지 않는다.
- `id`는 `[A-Za-z0-9_.-]`로 1~64바이트이고, `kind`·`action`·slot의 `name`은 `[A-Za-z_][A-Za-z0-9_.-]*`로 64바이트까지다. 같은 `id`를 글에 두 번 적으면 반복 안팎을 가리지 않고 `duplicate_id`다. 반복은 한 번 적은 `id`를 항목마다 key로 가르므로, 형제 반복 둘이 같은 `id`와 같은 key를 내어 겹치는 일을 읽을 때 막는다.
- `label`의 굵기는 `fontWeight`다. 필드 이름 `weight`가 stack 안의 자리 속성 `weight`(유연 항목)와 겹쳐 이름 규칙의 예외로 둔다(`fontSize`의 짝).
- `stack`의 `padding`은 수 하나(네 변)이고 `paddingLeft`·`paddingTop`·`paddingRight`·`paddingBottom`이 한 변을 고친다. 이름 없는 수를 늘어놓지 않는다(`stack_item` 주석, stack_element.h 57~63행의 까닭). `label`의 `padding`은 좌우 여백 수 하나다(label_element.h 37행).
- 수는 십진 꼴 `[+-]?(숫자+(.숫자*)?|.숫자+)([eE][+-]?숫자+)?`이고 앞뒤 공백을 받지 않는다. 유한하고 float에 들어야 한다. 로캘을 보지 않고 직접 읽는다(Android NDK의 libc++에는 실수 `from_chars`가 없고 `strtod`는 소수점이 로캘을 따른다). 길이·간격·크기는 0 이상(음수는 `-0`까지 막는다)이고 `fontSize`·`thickness`·`weight`는 0보다 크다. `value`·`shadow`는 0~1, `maxLines`는 0 이상의 정수다. 적은 글자의 범위는 읽을 때, 바인딩한 값은 요소의 규칙(progress는 자른다)으로 다룬다. 참거짓은 `true`·`false`다.
- 바인딩의 꼴은 읽을 때 본다(바인딩 문서 규칙). `{a.b}`의 `a`는 열린 반복의 `as`여야 하고, 수·참거짓 속성은 값 전체가 이름 하나일 때만 바인딩이다. 틀리면 `invalid_value`다.
- `lineBreak`·`selected`는 적지 않으면 비운다(`std::optional`). `lineBreak`가 비면 배치 문맥의 앱 스타일 `text.wrap_policy`를 따른다([줄 나눔 정책 스타일 키](wrap-policy-key-design.md) 245행).
- 내지 않는 칸: 함수 칸(선택자, factory, 스크롤 액션), `text_button_config::glyph`, `panel_config::border`(테 색은 `palette_role`에 없는 역할이다)와 `border_sides`. 필요하면 다음 단계에 행으로 더한다.

**자리 속성.** `stack`의 자식은 `stack_item` 필드 이름 그대로 `length`·`weight`·`crossLength`·`minimum`·`maximum`·`fit`을 받는다(stack_element.h 67~94행).

- `weight`가 있으면 유연 항목, `length`면 고정 항목이다. 둘 다 없으면 맞춤이다.
- 맞춤은 원하는 크기를 답하는 요소(표의 "맞춤" 칸)만 된다. 답하지 않는 요소에 `length`도 `weight`도 없으면 읽을 때 `missing_length`다. 0으로 접혀 보이지 않는 자식을 만들지 않는다. `slot`은 앱 요소가 답하는지 읽기가 모르므로 늘 자리를 적는다(`fit="true"`도 된다). 답하지 않는 요소의 `fit`과 `fit="false"`는 자리가 아니다.
- `minimum`·`maximum`은 `weight`와 함께일 때만 받는다. `fit`과 `length`·`weight`를 함께 적거나 `length`와 `weight`를 함께 적으면 `invalid_value`다.
- `stack` 밖(뿌리, `panel`의 자식, `wrap`의 자식)의 자리 속성은 `unknown_attribute`다. 뿌리의 자리는 앱이 `arrange`로 주고, panel·wrap의 자식은 그 컨테이너가 크기를 주므로 자리 없이 선다.
- 구조 요소(`for`·`if`·`unless`)는 자리를 갖지 않는다. 그 자식들이 부모 `stack`의 자식 자리에 차례로 들고 자리 속성은 그 자식들이 적는다. 구조 요소는 자식을 여럿 받는 `stack`·`wrap` 안에만 두고, `gap`은 구조 요소를 건너 가장 가까운 요소가 `stack`일 때만 된다. `panel`의 내용은 요소나 `slot` 하나다(펼친 수가 frame마다 달라지는 구조 요소는 내용이 되지 못한다).

**색은 역할로만 말한다.**

- `panel`의 `background`는 `palette_role` 이름이다([ui_style.h](../include/luil/theme/ui_style.h) 65~78행). 이름은 스타일 JSON과 같은 목록이고([style_keys.cmake](../cmake/style_keys.cmake) 75행), 어휘 소스는 생성기가 내는 비공개 키 표 헤더의 `palette_role_names`(`generate_style.cmake` 165행)를 그대로 읽어 목록이 한 곳이다. 펼칠 때 `palette_color(palette, role)`을 부르는 선택자가 된다.
- `#rrggbb` 같은 구체 색은 어느 속성도 받지 않는다(`invalid_value`). [테마](concepts/theming.md) 68행의 원칙이다. 글 색은 `label_color_role`, 단추는 `text_button_visual`이다.
- 마크업은 스타일 키(`ui_style`)를 속성으로 받지 않는다. 앱 전체 스타일은 지금처럼 `ui_frame::style`과 `make_arranged_tree`의 `style`이 나른다. 가지 하나만 스타일을 바꾸는 것은 선택자·상속(G4 c)이라 범위 밖이다.

**배율·테마·고대비·움직임 줄이기.** 길이는 논리 픽셀이고 배율은 지금처럼 `arrange`가 곱한다. 색이 역할뿐이라 테마와 고대비는 손으로 지은 요소와 똑같이 따른다. 마크업에는 움직임이 없어 움직임 줄이기에 닿지 않는다.

**접근성.** 펼친 요소는 손으로 지은 요소와 같은 형·같은 config라 UIA 역할·이름·패턴이 같다. 이름은 label·단추의 글이고 `accessName`이 앞선다. AutomationId는 지금처럼 `<kind 수>:<owner>`이고([uia_provider.cpp](../src/win32/uia_provider.cpp) 158~166행) 자동화 script는 owner로 집는다. kind 수를 script에 박지 않는다. 검사기에는 `markup_node:<owner>`로 보인다.

**기본 문구.** 마크업은 스스로 글을 넣지 않는다. 요소가 넣는 기본 이름은 그 요소의 것 그대로다([기본 문구](default-text-design.md)). 오류 글은 개발자용 영문 진단이라(`describe`) 마크업 소스에는 `luil_default_text` 검사가 막는 한글이 없다.

**C++ 조립을 돕는다.** 마크업은 화면의 한 가지를 짓는다. `root_element`, 캡션·앱 바, 창, popup 표면과 그것을 여닫는 일은 C++에 남는다. popup의 내용 tree도 같은 함수로 펼칠 수 있다. 펼친 뿌리는 배치되지 않은 채 돌아오고 앱이 `arrange`해 끼운다.

**읽기는 한 번, 펼치기는 매 frame.** 이름·enum·수·자리·id 꼴은 읽을 때 모두 확인해 C++ 값으로 바꿔 둔다. 펼칠 때는 바인딩을 찾고 요소를 짓기만 한다. `make_frame`에서 글을 읽지 않는다. 틀 하나를 여러 번, 여러 tree에 펼칠 수 있다(바인딩 문서의 `owner_prefix`).

**서식 글의 꼴(2단계).** `<richText>`의 내용은 HTML 조각이다. 엄한 읽기가 그 안의 `b`·`i`·`u`·`code`·`a`·`br`과 글을 바른 XML로 받고, 읽을 때 그 조각을 HTML 글로 다시 적어 `rich_text_from_html`([rich_text.h](../include/luil/ui/rich_text.h) 110행)에 한 번 넘긴 `rich_text_fragment`를 틀에 쥔다. 링크 scheme 거르기와 한도는 그 함수의 것이다. 1단계에는 이 요소를 펼치지 않는다(결정 3).

## 결정

확정(2026-11-03): N3 길 a(실행 중 해석). 근거: [스타일 읽기](style-sheet-design.md) 227~228행, [파일 감시](file-watch-design.md) 234행.

**1. 문법.** 질문: 마크업 글을 어떤 꼴로 적는가.

- 길 a: JSON. 요소 하나가 객체(`{"type": "label", "text": "…"}`)이고 자식은 `children` 배열이다. 되는 것: 새 읽기가 없다. core의 nlohmann을 스타일 파서처럼 안쪽에서 쓴다. 대가: 깊은 tree에서 괄호·따옴표·`children`이 화면 구조를 가린다. 주석이 없다(스타일 파서는 RFC 8259 그대로 막는다, [스타일 읽기](style-sheet-design.md) 120행). 의미 오류(모르는 속성)의 줄·열이 어렵다. nlohmann의 SAX 처리기는 자리를 받지 못하고(`sax_parse`에는 lexer가 넘어가지 않는다, `third_party/nlohmann_json/include/nlohmann/detail/input/parser.hpp` 164·180행) 자리는 `JSON_DIAGNOSTIC_POSITIONS`를 켠 DOM에만 있다(`json_sax.hpp` 182·239~246행). 읽기 소스 하나에서만 켜면 ABI 태그 `_dp`로 이름 공간이 갈려(`abi_macros.hpp` 43~46행) 스타일 파서와 겹치지 않지만, 키에는 자리가 없어 값의 자리를 대신 가리킨다. 서식 글은 HTML을 JSON 문자열 안에 이스케이프해 싣는다.
- 길 b: XML 1.0의 엄한 부분집합(위의 규칙). 되는 것: 요소는 요소, 속성은 config 칸, 자식은 자식이라 tree가 글 모양 그대로다. 주석이 있다. 줄·열을 읽는 자리에서 바로 센다. XML 편집기가 강조하고 짝을 맞춘다. 서식 글이 내용(`<b>`·`<a>`)으로 들어가 `rich_text_from_html`과 그대로 맞는다. 대가: core에 엄한 읽기(약 450줄)와 그 시험을 새로 짓는다. 스타일(JSON)과 형식이 둘이 된다.
- 길 c: 자체 중괄호 문법(QML꼴, `stack { label { text: "…" } }`). 되는 것: 가장 짧고 읽기 좋다. 대가: 낱말 읽기, 글·수 표기, 이스케이프를 모두 새로 정해 문서로 남긴다. 편집기 지원이 없다. 가장 크다.
- 권장: b. 화면은 깊은 tree이고 스타일은 얕은 키 표라 모양이 다르다. 대가: 위의 새 읽기와 두 형식이다. 승인: 길 b (2026-11-03).

**2. 이름.** 질문: `markdown_element.h`와 섞이지 않는 이름을 무엇으로 하는가.

- 길 a: `ui_markup`(이 문서). 헤더 `ui_markup.h`, 형 `ui_markup_*`, 함수 `parse_ui_markup`·`build_ui_markup`, kind `markup_node`. 되는 것: 노드·문서 이름과 같고 `ui_` 앞붙이로 `markdown_*`과 한눈에 갈린다. `ui_markup`으로 찾으면 마크다운이 걸리지 않는다. 대가: 낱말 자체는 markdown과 끝 하나가 다르다.
- 길 b: `ui_template`(`parse_ui_template`). 되는 것: "틀을 펼친다"는 뜻이 이름에 있다. 대가: 문서와 검색에서 C++ template과 섞인다.
- 길 c: 앞붙이 없는 `markup_*`. 되는 것: 짧다. 대가: `markup_`·`markdown_`이 자동 완성에서 나란히 선다.
- 권장: a. `markup_element` 클래스는 두지 않는다(마크업은 요소가 아니라 요소를 짓는 틀이다). 대가: 위의 끝 하나 차이다. 권장대로 진행.

**3. 1단계 어휘의 폭.** 질문: 처음에 어떤 요소를 받는가.

- 길 a: 컨테이너(`stack`·`wrap`·`panel`)와 config가 값뿐인 요소(`label`·`textButton`·`check`·`progress`), `gap`, 그리고 나머지를 위한 `slot`(이 문서). 되는 것: 표가 작아 config와 어긋날 자리가 적다. 편집 칸·목록·표·그림은 앱이 C++로 지어 `slot`에 끼운다. 대가: 편집 칸이 많은 화면은 slot이 많다. 편집 칸은 앱의 편집 상태(`text_input_view`, [text_input_state.h](../include/luil/ui/text_input_state.h) 13~32행)와 kind로 대상을 고르는 정책(`interaction_policy::text_target_of`, [ui_interaction.h](../include/luil/ui/ui_interaction.h) 267~273행)이 있어야 서므로 값 표로 나르지 못한다.
- 길 b: 넓게. 편집 칸(값 표에 `text_input_view` 칸), 목록·표(모델과 factory), 탭 막대까지 받는다. 되는 것: 화면 대부분을 마크업으로 쓴다. 대가: 값 형이 커지고 함수 칸을 이름으로 잇는 규약이 요소마다 는다. 속성 표가 config 수십 개를 따라가야 한다.
- 길 c: 컨테이너와 `label`만. 되는 것: 가장 작다. 대가: 단추와 체크마저 slot이라 마크업의 뜻이 약하다.
- 서식 글(`richText`)은 꼴만 정하고(위) 2단계에 펼친다. `text_format`을 `text_span`으로 옮기는 일이 `rich_text_presenter` 안에만 있고(`rich_text.cpp` 140~165행) 읽기 전용 `text_area_element`도 `text_input_view`를 받는다(text_area_element.h 182행). 1단계는 바인딩 모델을 먼저 굳힌다.
- 권장: a. 대가: 위의 slot이다. 권장대로 진행.

**구현 때 정한 것(2026-11-03).** 결정 절의 길을 바꾸지 않는 세부라 규칙 절에 적었다.

- `label`의 굵기 속성은 `fontWeight`다. 까닭: `weight`를 그대로 두면 label을 stack의 유연 항목으로 담을 수 없다.
- 구조 요소는 `stack`·`wrap` 안에만, `panel`의 내용은 요소나 `slot` 하나다. 까닭: panel 안의 `if`를 받으면 내용이 0이나 둘이 될 수 있다.
- 같은 `id`는 글 전체에서 한 번이다(바인딩 문서의 "반복 밖에서"보다 엄하다). 까닭: 반복 안을 풀면 형제 반복 둘의 owner가 key에 따라 겹친다.
- 읽기와 어휘 확인은 재귀하지 않는다. 까닭: 재귀하면 `max_depth`를 크게 올린 앱이 깊은 글에서 호출 스택을 넘친다.

## 플랫폼

**Win32.** `src/win32`를 고치지 않는다. 펼친 요소는 지금의 요소라 UIA·IME·DPI가 그대로다.

**Android.** Java 소스는 0줄이고 JNI도 부르지 않는다. 읽기·펼치기는 core라 그대로 돌고 `luil_core_tests`가 기기에서 돈다. 글을 얻는 것은 앱의 몫이다. 앱 전용 폴더의 파일은 표준 입출력으로 읽고, APK assets는 앱이 NDK의 `AAssetManager`(C API)로 읽는다. luil은 그 관리자를 내지 않는다([스타일 읽기](style-sheet-design.md) 결정 6과 같다). 모바일 데모는 글을 `embed_binary.cmake`로 실행 파일에 싣는다. Android의 luil에는 접근성 계층이 없어 접근성 규칙은 Win32 UIA만이다. unsupported인 부분은 없다.

**빌드.** 길 b면 새 소스 셋(`src/ui/ui_markup_read.cpp` 엄한 읽기와 `describe`, `ui_markup_vocabulary.cpp` 어휘 표, 바인딩 노드의 `ui_markup_build.cpp` 펼치기, 셋이 함께 쓰는 비공개 헤더 `ui_markup_document.h`)을 `luil_core`의 `ui/markup` 목록에 더할 뿐 링크가 바뀌지 않는다. 어휘 소스는 스타일 키 표 헤더를 읽으려고 비공개 생성 include를 쓴다. 이미 `luil_core` PRIVATE에 있다(`src/CMakeLists.txt` 685~688행). 새 공개 헤더는 `include/luil/luil.h`에 더한다. 길 a면 nlohmann 링크는 그대로이고 `JSON_DIAGNOSTIC_POSITIONS`를 읽기 소스 한 곳에서만 켠다.

## 다른 노드와의 약속

- **markup_binding(R4, 같은 줄 다음 노드).** 이 노드는 읽기 쪽을 한다. `ui_markup.h`의 `ui_markup_limits`·`ui_markup_document`·`ui_markup_error_kind`(펼칠 때 종류까지 한 벌)·`ui_markup_error`·`describe`(모든 종류)·`ui_markup_parse_result`·`parse_ui_markup`, kind `markup_node`, 읽을 때의 바인딩 꼴 확인이다. 틀의 속은 비공개 헤더 `src/ui/ui_markup_document.h`에 있다: 노드 목록(자식은 부모 뒤), 속성마다 적은 값이나 바인딩 조각과 그 자리, 표의 행과 그 행이 값을 거는 `apply`, 자리 방식(`placement`). 다음 노드가 `ui_markup_value`·`ui_markup_scope`·`ui_markup_action`·`ui_markup_context`·`ui_markup_build_result`·`build_ui_markup`을 헤더 끝에 더하고, 속성을 행 순서대로(바인딩은 찾은 값으로) 다시 걸어 요소를 짓는다. 같은 화면 견줌·견본 픽셀 시험과 데모 칸도 그 노드다.
- **style_sheet(R2, 같은 줄).** [스타일 읽기](style-sheet-design.md) 228행을 닫는다. 이 노드는 소스만 더하고 길 b면 nlohmann도 쓰지 않는다. 오류는 같은 꼴(종류 enum, 자리, 영문 한 줄 `describe`)에 줄·열을 더하고 형은 따로다(`ui_markup_error`). 자리는 점 대신 요소 이름을 `/`로 잇는다. 같은 줄의 "결정 5의 길 b가 markup에서 필요해지면"과 173행의 "첫 소비자"는 [줄 나눔 정책 스타일 키](wrap-policy-key-design.md)가 이미 길 b를 했으므로 이 노드가 질 것이 없다. `palette_role` 이름은 그 노드의 키 표를 읽는다.
- **uax14·wrap_policy(R1·R2, 같은 줄).** [줄 나눔](line-break-design.md) 166행과 [정책 키](wrap-policy-key-design.md) 245행을 닫는다. `label`의 `lineBreak`는 `std::optional` 칸을 비우거나 채운다. 비면 측정 문맥의 `line_break_for`가 앱 스타일을 따른다. 1단계 마크업은 스타일의 다른 칸을 읽지 않아 `measure_context::app_style()`을 쓸 일이 없다.
- **rich_edit·교환(R3, 같은 줄).** [서식 있는 글 편집](rich-edit-design.md) 210행과 [서식 글 교환](rich-edit-exchange-design.md) 127행의 꼴은 위의 "서식 글의 꼴"로 정한다. 펼치기는 2단계로 미룬다(결정 3의 까닭). 그때 `rich_text.*`에 `text_format`을 구간으로 옮기는 공개 도우미를 하나 더한다(같은 줄의 파일이다).
- **inspector(R2).** `markup_node`의 이름표 case를 같은 커밋에 더한다([inspect_names.cpp](../src/ui/inspect_names.cpp) 4~8행). `kind` 속성의 이름표는 검사기의 `kind_names`와 같은 형·같은 값이다(바인딩 문서 결정 6).
- **file_watch·hot_reload(R1·R5).** [파일 감시](file-watch-design.md) 234행대로 이 노드는 감시하지 않고 파일도 읽지 않는다. 다시 읽기의 약속은 바인딩 문서에 있다.
- **같은 라운드 다른 줄.** `ui_interaction.*`, `win32_window.cpp`, `window_surface.cpp`, `ui_events.h`, `accessibility.h`, `uia_provider.cpp`, `transition.h`·`spring.h`, `app_host.h`·`app_delegate.h`·`ui_platform.h`, `field_popup.h`를 고치지 않는다. 겹치는 곳은 덧붙임뿐이다. `ui_element_id.h`(`end_of_library_kinds` 위 한 줄, 줄 1의 date_picker·teaching_tip과 줄 3의 page_anim도 더할 수 있다), `inspect_names.cpp`(끝의 case), `src/CMakeLists.txt`(`ui` 소스 줄), `tests/CMakeLists.txt`, `include/luil/luil.h`다. 병합은 기계적이고 `tests/ui_element_tests.cpp`의 `last[]`는 통합 때 마지막 kind로 맞춘다. date_picker·teaching_tip은 1단계 어휘에 들지 않는다. 그 노드가 들어온 뒤 어휘 행으로 더한다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(헤더, 읽기·어휘·펼치기 소스, kind와 이름표, test, 문서) 뒤에 데모 연결 커밋을 낸다. 고치는 데모 파일은 `examples/demo/theme_page.*`·`markup_sample.h`(새 파일)·`assets/markup_sample.xml`(새 파일)·`common.h`(kind)·`text_targets.h`(slot의 칸), `examples/demo_main.cpp`(실은 글을 넘긴다), `examples/CMakeLists.txt`(싣기), `examples/mobile_demo/theme_page.*`·`assets.h`·`app.h`·`common.h`·`mobile_demo.cmake`와 `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`·`tests/CMakeLists.txt`(시험 실행 파일에 견본을 싣는다)다. 통합 때 다른 줄의 데모 변경(특히 page_anim의 페이지 바꿈) 뒤에 직렬로 쌓는다.
- 통합 때 함께 고치는 문서: [구성 요소](components.md) 한 행과 색인(`docs/README.md`).

## 확인

자동 test.

- `tests/ui_markup_read_tests.cpp`(`luil_core_tests`, Windows와 Android 기기, 새 파일): 바른 글의 요소·속성·주석·엔티티·숫자 참조, BOM과 XML 선언. 막는 글마다 종류와 줄·열. 닫지 않은 태그, 짝이 틀린 닫기, 따옴표 없는 값, 겹친 속성, 모르는 엔티티, `<!DOCTYPE`·CDATA·처리 지시·이름의 `:`·`xmlns`(unsupported), 깨진 UTF-8, XML Char 밖의 글자와 숫자 참조, 뿌리 둘, 뿌리 뒤의 글이다. CR LF·CR 줄과 한글·이모지가 든 줄의 열(코드 포인트). 한도 셋(`max_bytes`·`max_depth`·`max_nodes`)이 `too_large` 하나로 멈추는지. 신뢰하지 않는 글: 바른 견본의 모든 앞부분(잘린 글), 망가뜨린 글 3000개(정해진 의사 난수), 깊이 10만, 1 MiB에 가까운 글, 한 요소의 속성 6만 개가 멈추거나 넘치지 않고(제곱 시간도 쓰지 않고) 오류가 자리 순인지.
- `tests/ui_markup_tests.cpp`(`luil_core_tests`, 새 파일): 어휘. 모르는 요소·속성, 허용 밖 enum 이름, `#rrggbb`, 범위 밖 수, `panel`의 둘째 자식, `missing_length`(panel·progress·wrap·slot을 자리 없이), `fit`과 `length`를 함께, `stack` 밖의 자리 속성. `palette_role` 이름이 키 표의 행과 같은 수·같은 값인지. 속성 표의 행마다 기본값이 아닌 값을 적어 C++로 같은 값을 준 config와 같은지(비공개 헤더의 `ui_markup_literal_values`). 읽을 때의 바인딩 꼴, 필수 속성, id·이름 꼴, 겹친 id.
- 같은 화면 견줌(같은 파일, 바인딩 노드): 같은 화면을 C++로 짓고 마크업으로 펼쳐 같은 자리에 배치한 두 `ui_tree`를 pre-order로 돌며 bounds·`visible`·`enabled`·접근 역할·이름이 같은지, 마크업 쪽 `unarranged()`·`duplicate_ids()`가 비었는지. 속성 표의 행마다 기본값이 아닌 값을 적어 C++로 같은 값을 준 요소와 같은지(행이 엉뚱한 필드를 가리키는 것을 잡는다). C++20은 config의 필드를 셀 수 없어 config에 새 필드가 늘어난 것은 잡지 못한다. 어휘 표의 머리 주석이 그 짝을 적는다.
- 견본 픽셀(`tests/raster_probe.h`, 바인딩 노드): `panel`의 `background` 역할이 그 팔레트 색이고 고대비에서는 시스템 색인지.
- 기존 kind 이름 전수 test(`tests/ui_inspect_tests.cpp`, `tests/ui_element_tests.cpp`)가 `markup_node`를 받는지.
- `luil_umbrella_header`, `luil_core_portability`, `luil_source_style`, `luil_default_text`.

데모(바인딩 노드). 상태를 label로 그려 `luil-uia`로 되읽는다.

- 견본 묶음 `examples/demo/markup_sample.h`(새 파일, 두 데모와 page test가 함께 쓴다, `style_sheet_sample.h`의 선례)와 견본 글 `examples/demo/assets/markup_sample.xml`(데모 빌드가 `embed_binary.cmake`로 싣는다)이다.
- 데스크톱 테마 페이지의 "스타일 파일" 묶음 아래에 "마크업" 묶음을 둔다. 둘 다 R5 hot_reload가 다시 읽을 파일이라 한 자리에 둔다. 묶음 안의 가지가 견본 글을 펼친 것이다. label `markup-count`("누른 횟수 0"), 단추 `markup-more`, 체크 `markup-list`, 반복 단추 셋(`markup-item`과 key `apple`·`pear`·`plum`, 누르면 고른 항목이 된다), 목록이 꺼졌을 때의 안내 label `markup-list-hint`(`unless`), slot `markup-search`(C++가 짓는 한 줄 칸)다. 단추 줄 stack은 `kind="layout"`이라 검사기와 같은 이름표로 데모의 kind가 된다. 가지 밖(C++)에 단추 `markup-reload`("다시 읽기")·`markup-sample-unknown`("견본: 모르는 속성")·`markup-sample-broken`("견본: 닫지 않은 태그")·`markup-sample-value`("견본: 없는 값")와 label `markup-status`를 둔다.
- `markup-status`는 "펼침 · 요소 7 · 오류 0"(목록을 켜면 요소 10), "거부 · 오류 1 · 4:61 Unknown attribute on label: colr · 이전 틀 유지", "거부 · 오류 1 · 3:1 Syntax error in stack · 이전 틀 유지", "펼침 · 요소 10 · 오류 1 · 4:28 Unknown value: cuont"처럼 말한다. 요소 수는 `ui_markup_build_result::built`이고 줄·열은 `describe`의 것이다.
- 모바일 데모 테마 페이지에 같은 묶음을 둔다. 글 칸이 없어 slot에는 C++가 지은 label(`markup-search`, "slot: C++가 지은 칸")을 담고 견본 단추 넷만 둔다.
- UIA(격리 데스크톱): `markup-more`를 Invoke해 `markup-count`가 바뀌는지, 견본을 Invoke한 뒤 `markup-status`가 거부를 말하고 가지가 이전 틀 그대로인지, `markup-list`를 Toggle해 반복 단추 셋이 나고 사라지는지 읽는다. 모바일 데모(Windows 창)도 같은 label을 읽는다. Android에서는 사람이 화면으로 읽는다.

사람 확인.

- 화면 하나를 마크업으로 직접 써 보고 읽기·쓰기가 편한지, 오류 글이 고칠 자리를 바로 가리키는지.
- 마크업 가지와 손으로 지은 같은 화면을 데스크톱과 Android 실기기에서 밝은·어두운·고대비로 눈으로 견준다.

## 범위 밖

- 파일 읽기, 감시, 다시 읽기의 디바운스는 [핫 리로드](hot-reload-design.md)가 닫았다. APK assets 읽기.
- 함수 칸과 모델을 쥔 요소(편집 칸·목록·표·탭·그림)의 어휘. slot으로 짓는다. `richText` 펼치기(꼴만 정했다).
- 마크업 안의 스타일 키·선택자·상속과 구체 색. 움직임과 전환.
- 다른 파일 넣기(include), 조각 정의와 재사용, 스키마 파일(XSD) 배포, 마크업 쓰기(직렬화).
- 빌드 때 C++ 생성(N3 길 b).

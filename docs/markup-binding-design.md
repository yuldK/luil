# 마크업 바인딩과 오류

[화면 마크업](markup-design.md)의 틀을 앱 상태로 펼치는 규칙과 오류의 꼴이다. 앱은 frame마다 값 표(`ui_markup_scope`), 사건 함수, slot 함수, kind 이름표를 `ui_markup_context` 하나에 담아 `build_ui_markup`에 넘긴다. 마크업은 `{이름}`으로 값을 읽고, `for`로 목록을 돌고, `if`·`unless`로 가지를 고르고, `action`으로 앱 메시지를 잇는다. 식 언어는 없다(결정 4). 오류는 줄·열과 요소 자리를 든 `ui_markup_error`이고 영문 한 줄 `describe`로 읽는다. 공개 형식은 같은 헤더 `include/luil/ui/ui_markup.h`의 `ui_markup_value`·`ui_markup_scope`·`ui_markup_action`·`ui_markup_context`·`ui_markup_build_result`·`ui_markup_error_kind`·`ui_markup_error`·`describe`다. 결정 번호는 그 문서에 이어 4부터다.

## 문제

- 이름으로 앱 메시지를 지을 수 없다. `app_message`는 담는 형의 `typeid`를 쥐고([app_message.h](../include/luil/ui/app_message.h) 22~24행) `get<T>`가 그 형으로만 푼다(38행). 마크업의 `action="increment"`는 앱이 이름을 메시지로 바꾸는 함수가 있어야 선다.
- 액션은 C++ 함수 값이다(`ui_action`, [ui_element.h](../include/luil/ui/ui_element.h) 188행). `make_message_action`은 메시지를 값으로 쥐고(194~197행), 마크다운의 `activate_link`는 누를 때 앱 함수를 부른다(`markdown_element.cpp` 246행). 누를 때 부르면 앱 함수가 액션을 돌리는 thread에서 불린다.
- 정체성은 id다. kind와 owner가 frame 사이의 hover·눌림·초점을 잇고([ui_element_id.h](../include/luil/ui/ui_element_id.h) 7~10행), 같은 id가 둘이면 뒤의 것이 조용히 `duplicate_ids()`에만 남는다(`ui_tree.cpp` 540~544행). AutomationId도 `kind:owner`다. 반복에서 owner를 정하는 규칙이 없으면 목록이 바뀔 때 초점이 엉뚱한 행으로 옮는다.
- 라이브러리의 구조 부품은 `{none, owner}`를 쓴다(예: `modal_host_element.cpp` 73행의 판). 마크업이 같은 kind를 쓰면 앱이 적은 id가 그 owner와 겹칠 수 있다.
- 앱 kind의 이름표를 값으로 넘기는 선례가 있다(`inspector_kind_name`, [ui_inspect.h](../include/luil/ui/ui_inspect.h) 33행, `inspector_config::kind_names` 56행, [검사기](inspector-design.md) 143행).
- 스타일 오류(`style_error`)는 자리와 키를 들지만 줄·열이 없다([스타일 읽기](style-sheet-design.md) 32~40행). 얕은 키 표라 키 경로로 충분했다. 화면 정의는 깊고 같은 이름의 형제가 많아 줄·열이 있어야 고칠 곳을 찾는다.

## 공개 형식

```cpp
// include/luil/ui/ui_markup.h (markup-design.md의 선언에 이어서)
namespace luil {
    class ui_markup_scope;
    using ui_markup_list = std::shared_ptr<const std::vector<ui_markup_scope>>;
    // 앱이 주는 값이다. 참거짓·수·글·목록 넷뿐이다.
    using ui_markup_value = std::variant<bool, double, std::u8string, ui_markup_list>;

    // 이름에서 값으로 가는 표다. 반복의 항목도 이 표 하나다. frame마다 앱이 짓는 값이다.
    class ui_markup_scope
    {
    public:
        ui_markup_scope& set(std::u8string name, ui_markup_value value);  // 같은 이름은 덮어쓴다
        [[nodiscard]] const ui_markup_value* find(std::u8string_view name) const noexcept;
        [[nodiscard]] std::span<const std::pair<std::u8string, ui_markup_value>> entries() const noexcept;  // 넣은 차례다

    private:
        std::vector<std::pair<std::u8string, ui_markup_value>> values_ {};
    };

    // 사건 하나다. 펼칠 때 앱 함수가 받는다.
    struct ui_markup_action
    {
        std::u8string_view name {};      // action 속성의 이름
        std::u8string_view argument {};  // argument 속성을 펼친 글. 없으면 비어 있다
        ui_element_id id {};             // 그 요소의 id (반복 key까지 든 owner)
    };

    struct ui_markup_context
    {
        const ui_markup_scope* values { nullptr };
        // 이름을 앱 메시지로 바꾼다. 펼칠 때 logic thread에서 부른다. 모르는 이름이면 빈 값(monostate)을 돌려준다.
        std::function<input_action(const ui_markup_action& action)> make_action {};
        // slot 요소를 앱이 짓는다. 펼칠 때 부른다. 모르는 이름이면 nullptr다. scope는 바깥 표 위에 둘러싼 항목을 덮은 표다.
        std::function<std::unique_ptr<ui_element>(std::u8string_view name, const ui_markup_scope& scope)> make_slot {};
        // kind 속성의 이름표다. 검사기에 주는 것과 같은 형·같은 값이다 (`inspector_config::kind_names`).
        std::span<const inspector_kind_name> kinds {};
        // 비어 있지 않으면 모든 owner 앞에 이것과 단위 구분자(U+001F)를 둔다. 같은 틀을 한 tree에 두 번 펼칠 때 쓴다.
        std::u8string owner_prefix {};
    };

    struct ui_markup_build_result
    {
        std::unique_ptr<ui_element> element {};  // 틀의 뿌리다. 오류가 있어도 선다
        std::vector<ui_markup_error> errors {};  // 펼칠 때 오류다. 자리 순이고 같은 자리의 같은 오류는 하나다
        std::size_t built { 0 };                 // 지은 element 수다 (뿌리와 slot 포함, 구조 요소와 gap 빼고)
    };

    enum class ui_markup_error_kind
    {
        // 읽을 때 (parse_ui_markup)
        syntax,               // 닫지 않은 태그, 짝이 틀린 닫기, 따옴표, 모르는 엔티티, 깨진 UTF-8, 뿌리 둘
        unsupported,          // DTD, 처리 지시, CDATA, 이름 공간
        too_large,            // max_bytes·max_depth·max_nodes를 넘었다
        unknown_element,
        unknown_attribute,
        duplicate_attribute,
        missing_attribute,    // 단추·체크의 id, slot의 name, for의 each·as·key, if·unless의 test
        invalid_value,        // 허용 이름 밖, 수가 아님, 범위 밖, id·바인딩의 꼴, 구체 색, 함께 못 쓰는 자리 속성
        invalid_child,        // 받지 않는 자식이나 글 (panel의 둘째 자식, label 안의 요소)
        missing_length,       // 원하는 크기를 답하지 않는 요소나 slot에 자리가 없다
        duplicate_id,         // 글 전체에서 같은 id를 두 번 적었다
        // 펼칠 때 (build_ui_markup)
        unknown_value,        // 값 표에 없는 이름
        wrong_value_type,     // 형이 맞지 않는 값 (글 자리에 수, each에 글)
        unknown_action,       // make_action이 빈 값을 돌려주었다
        unknown_slot,         // make_slot이 nullptr를 돌려주었다
        unknown_kind,         // kind 이름이 이름표에 없다
        duplicate_key,        // 한 반복 안에서 key가 겹친다
        too_many_built,       // max_built를 넘었다
    };

    struct ui_markup_error
    {
        ui_markup_error_kind kind { ui_markup_error_kind::syntax };
        std::size_t line { 0 };    // 1부터다. 펼칠 때 오류는 그 속성이 적힌 자리다
        std::size_t column { 0 };  // 1부터, 줄 처음부터 센 코드 포인트다
        std::size_t offset { 0 };  // 0부터 센 바이트 자리다
        std::u8string path {};     // 요소 자리다 ("stack/stack[2]/textButton")
        std::u8string name {};     // 문제의 요소·속성·값·사건 이름이다
        std::u8string value {};    // 받은 글이다. 64바이트에서 자른다
        [[nodiscard]] bool operator==(const ui_markup_error&) const = default;
    };

    // "4:21 Unknown attribute on label: colr" 꼴의 영문 한 줄이다. 개발자용 진단이다.
    [[nodiscard]] std::u8string describe(const ui_markup_error& error);
}
```

```cpp
// 앱의 make_frame. 값 표는 이번 frame의 상태에서 짓는다.
luil::ui_markup_scope values {};
values.set(u8"countText", count_text(clicks_)).set(u8"showList", show_list_).set(u8"items", item_scopes_);
const luil::ui_markup_context context {
    .values = &values,
    .make_action = [](const luil::ui_markup_action& action) -> luil::input_action {
        if (action.name == u8"increment")
            return luil::make_app_action(increment_intent {});
        if (action.name == u8"select")
            return luil::make_app_action(select_intent { std::u8string { action.argument } });
        return {};
    },
    .make_slot = [this](std::u8string_view name, const luil::ui_markup_scope&) { return name == u8"search" ? make_search_input() : nullptr; },
    .kinds = kind_names_,
};
```

## 규칙

**값.**

- 속성 값의 `{이름}`은 값 표에서 찾는다. 이름은 `[A-Za-z_][A-Za-z0-9_]*`이고, 반복 변수는 `{item.title}`처럼 점 하나로 그 항목의 이름을 읽는다. `{{`·`}}`는 글자 `{`·`}`다. `{` 안의 `:`는 앞으로의 접두(마크업 후속)를 위해 비워 두어 지금은 `invalid_value`다.
- 찾는 차례: `{item.title}`은 `as="item"`인 가장 안쪽 반복의 항목에서만 찾는다. 점 없는 `{title}`은 안쪽 항목부터 바깥 항목, 마지막에 context의 `values` 순으로 찾는다(안쪽이 먼저다).
- 속성 값 전체가 `{이름}` 하나면 값의 형을 그대로 쓴다. 참거짓 속성(`checked`·`mixed`·`visible`·`enabled`·`test`)에는 참거짓, 수 속성(`value`·`length`·`spacing` 등)에는 수, `each`에는 목록이다. 글이 섞이면(`누른 횟수: {count}`) 글 속성만 되고 끼우는 값도 글이어야 한다. 수를 글로 바꾸지 않는다. 자리 구분과 소수점이 언어마다 달라 앱이 글로 지어 넣는다.
- 바인딩을 받는 속성은 글·수·참거짓 속성과 `each`·`test`·`key`·`argument`다. enum·역할·`id`·`kind`·`action`·`name`은 적힌 글자뿐이다. 어휘 확인을 모두 읽을 때 끝내기 위해서다.
- 찾지 못한 이름은 `unknown_value`, 형이 틀리면 `wrong_value_type`이다. 그 속성은 빈 값(빈 글, 거짓, 빈 목록)으로 선다(결정 7). 글에 끼운 이름 하나가 틀려도 그 속성 전체가 빈 글이다. 수 속성은 0 대신 그 요소의 기본값으로 선다.
- 바인딩한 수가 그 속성의 범위 밖(무한, float 밖, 음수 길이, 0 이하의 `fontSize`·`thickness`·`weight`, 정수가 아닌 `maxLines`)이어도 `wrong_value_type`이고 요소의 기본값으로 선다. 0~1 속성(`value`·`shadow`)은 progress처럼 자른다. 적은 글자의 범위는 읽을 때 막으므로 펼칠 때만의 규칙이다.
- 값 표는 frame마다 앱이 짓는 값이다. 관찰·갱신 구조가 없다. tree가 frame마다 새로 서므로([불변 tree](concepts/immutable-tree.md) 3행) 펼칠 때의 값이 곧 화면이다.

**반복과 조건.**

- `<for each="{items}" as="item" key="{item.key}">`는 목록의 항목마다 자식들을 펼친다. `as`는 반복 변수 이름이고 `key`는 꼭 있다. 항목은 `ui_markup_scope`이고 바깥 이름도 보인다(안쪽이 먼저다).
- `<if test="{x}">`는 참일 때, `<unless test="{x}">`는 거짓일 때 자식들을 펼친다. `test`는 참거짓 값 하나다. 비교·부정·논리식은 없고 앱이 참거짓을 지어 넣는다.
- 구조 요소는 element를 짓지 않는다. 자식들이 부모의 자식 자리에 차례로 든다. 겹쳐도 된다(`for` 안의 `if`).
- 한 번 펼치기에서 지은 element가 `max_built`를 넘으면 `too_many_built` 하나를 남기고 그 뒤를 짓지 않는다. 뿌리는 늘 서고 수에 든다. 구조 요소와 `gap`은 세지 않고 slot이 돌려준 요소는 센다.
- 펼치기는 재귀하지 않고 쌓은 frame으로 돈다(읽기와 같은 까닭). element의 해제는 자식을 따라 재귀하므로, 앱이 `max_depth`를 수천으로 올린 틀은 펼침이 아니라 tree를 버릴 때 호출 스택에 닿는다(Debug 빌드 2000단에서 넘쳤다). 그래서 펼치기는 깊이 상한 `ui_markup_build_max_depth`(256, 셈은 `max_depth`와 같다)를 넘는 틀을 펼치지 않는다. 뿌리만 서고 `too_large`(이름 `max_depth`, 값 256) 하나를 남기며, 자리는 처음으로 넘은 노드다. 읽기는 앱이 올린 한도까지 그대로 받는다(사용자 결정 2026-11-04, 해제를 반복으로 바꾸는 길과 계약만 두는 길보다 골랐다).

**id와 owner(결정 6).**

- `id`는 `[A-Za-z0-9_.-]`로 1~64바이트다. 단추·체크처럼 누르는 요소는 `id`가 꼭 있다(`missing_attribute`). 글 전체에서 같은 `id`를 두 번 적으면 반복 안팎을 가리지 않고 읽을 때 `duplicate_id`다.
- owner는 `id`다. 반복 안이면 그 뒤에 단위 구분자(U+001F)와 항목의 `key`를 잇고, 겹친 반복은 바깥 key부터 잇는다. 확인 대화 상자의 단추와 별점 칸이 쓰는 구분자다(ui_element_id.h 166·170행). context의 `owner_prefix`가 있으면 맨 앞에 그것과 구분자를 둔다.
- kind는 `kind` 속성의 이름을 `kinds`에서 찾은 앱 kind이고, 없으면 `markup_node`다. `check`는 kind가 `check`로 정해져 `kind` 속성이 없다. 찾지 못한 이름은 `unknown_kind`이고 `markup_node`로 선다.
- `id`가 없는 요소의 owner는 뿌리부터 센 자식 번호를 `/`로 이은 자리다(`/0/2`, 반복 안이면 key가 붙는다). 번호는 틀의 자식 차례이고 구조 요소도 한 단이다(`<stack><if><label/></if></stack>`의 label은 `/0/0`). 그래서 값이 바뀌어도 같은 자리는 같은 owner다. 뿌리는 `/`다. `/`는 `id`에 쓰지 못해 겹치지 않는다. 글을 고치면 바뀌지만 누르는 요소가 아니라 잃을 상태가 없다.
- 초점·hover·눌림은 id가 같으면 frame 사이와 다시 읽기 사이에 이어진다. 반복에 항목이 끼어들거나 빠져도 key가 같으면 같은 행이다. key가 겹치면 `duplicate_key`이고 펼침은 그대로 서며 `ui_tree::duplicate_ids()`에도 남는다.

**사건(결정 5).**

- `action`은 앱이 뜻을 정하는 이름(`[A-Za-z_][A-Za-z0-9_.-]*`)이고 `argument`는 펼친 글이다. `textButton`은 왼쪽 누름, `check`는 `check_config::toggle`이다.
- `build_ui_markup`이 그 요소를 지을 때 logic thread에서 `make_action({ name, argument, id })`을 한 번 부르고, 돌아온 `input_action`을 값으로 쥔 `ui_action`을 건다(`make_message_action`과 같은 꼴). input·UI thread는 앱 함수를 부르지 않는다.
- 빈 값(`std::monostate`)이 오거나 `make_action`이 비어 있으면 `unknown_action`이다. 단추는 비활성으로, 체크는 누를 수 없게 선다(check_element.h 45~47행의 규칙).
- `action`을 적지 않은 단추·체크는 `make_action`을 부르지 않는다. 단추는 활성인 채 액션 없이, 체크는 누를 수 없게 선다. 읽기가 `action`을 꼭 받지 않으므로 오류가 아니다.
- 좌표나 끌기 값이 필요한 액션, 한 요소의 여러 사건(두 번 누름, hover)은 1단계에 없다.

**slot(결정 8).** `<slot name="search"/>`에서 `make_slot(name, scope)`을 부른다. scope는 그 자리에서 보이는 값 표(반복 변수 포함)다. context의 `values`를 복사해 둘러싼 반복의 항목을 바깥부터 덮은 표라, 점 없는 이름을 찾는 차례와 같다(반복 밖이면 `values` 그대로). 돌아온 요소는 앱이 정한 id를 그대로 쓰고 마크업의 자리 속성으로 담긴다(`owner_prefix`도 붙이지 않는다). nullptr면 `unknown_slot`이고 그 자리는 빈다. stack 안의 고정·유연 자리는 같은 길이의 빈 자리로 남겨 둘레의 배치가 들썩이지 않는다.

**오류(결정 7).**

- 읽기: 오류가 하나라도 있으면 `document`가 빈다. 오류는 끝까지 모으되 `syntax`·`unsupported`·`too_large`는 그 하나만 남기고 멈춘다(앞서 모은 어휘 오류도 지운다). 순서는 글의 자리 순이다. 스타일의 결정 3과 같다.
- 펼치기: 요소는 늘 선다. 오류는 자리 순으로 모은다. 같은 자리(`offset`)의 같은 종류·이름·값은 하나만 남긴다. 반복이 같은 실수를 항목마다 되풀이해 목록이 항목 수만큼 늘지 않는다(`duplicate_key`는 key마다 값이 달라 남는다). 같은 틀·같은 값이면 같은 목록이라 앱이 frame마다 같은 줄을 띄워도 흔들리지 않는다.
- 자리: `line`·`column`은 1부터, `offset`은 0부터 센 바이트다. 펼칠 때 오류는 그 속성이 적힌 자리다. `path`는 뿌리부터 요소 이름을 `/`로 잇고 같은 이름의 형제는 `[n]`(1부터)을 붙인다. `name`은 문제의 요소·속성·값·사건 이름이고 `value`는 받은 글을 64바이트에서 자른 것이다.
- 펼칠 때 오류의 칸: `unknown_value`는 `name`이 적은 이름(`cuont`, `item.title`)이다. `wrong_value_type`은 `name`이 속성 이름, `value`가 적은 이름이다. `unknown_action`·`unknown_slot`·`unknown_kind`는 `name`이 그 이름이다. `duplicate_key`는 `name`이 `key`, `value`가 겹친 key이고 `path`는 `for`의 자리다. `too_many_built`는 `name`이 `max_built`, `value`가 한도이고 자리는 멈춘 요소의 여는 태그다.
- `describe`는 `<줄>:<열> <영문 한 줄>`이다(`4:21 Unknown attribute on label: colr`, `3:21 Unknown value: cuont`, `3:17 Wrong value type for text on label: count`, `4:31 Duplicate key in for: k`, `6:5 Built elements exceed max_built (50000)`). 스타일의 `describe`처럼 개발자용 진단이라 앱의 언어를 따르지 않는다.

**한도와 신뢰하지 않는 글.** 마크업 글은 앱 밖에서 온 파일일 수 있다고 본다.

- 읽기는 파일·주소를 열지 않는다. 넣기(include)와 바깥 엔티티가 없고 DTD를 막는다. 글 속의 주소는 글자일 뿐이다. 2단계 서식 글의 링크는 `rich_text_from_html`이 `http:`·`https:`·`mailto:`만 남긴다.
- `max_bytes`(1 MiB)를 넘으면 읽지 않는다. `max_depth`(64)는 읽는 자리에서 세어 재귀가 깊어지기 전에 멈춘다. `max_nodes`(1만)는 틀의 크기를, 펼침의 `max_built`(5만)는 큰 목록과 겹친 반복을 막는다. 모두 `ui_markup_limits`에서 앱이 바꾼다.
- 바인딩은 식이 아니라 이름 찾기라 글이 계산을 부풀리지 못한다.

## 결정

**4. 값 바인딩의 모양.** 질문: 마크업이 앱 상태를 어떻게 읽는가.

- 길 a: 이름 값 표와 `for`·`if`·`unless`(이 문서). 되는 것: tree가 frame마다 새로 서므로 펼칠 때 이름을 찾는 것으로 충분하다. 값 형이 넷이라 API가 작고 글이 계산을 부풀리지 못한다. 대가: 앱이 frame마다 상태를 값 표로 옮긴다(글 복사). 계산된 값(비었는지, 형식 맞춘 수)은 앱이 지어 넣는다.
- 길 b: 이름 붙은 구멍(slot)만 둔다. 구조는 정적이고 바뀌는 것은 모두 C++가 짓는다. 되는 것: 바인딩 규칙이 없다. 대가: 글 하나가 바뀌는 label도 slot이라 마크업에는 배치만 남는다. 다시 읽기로 바꿀 수 있는 것이 적다.
- 길 c: 식 언어(`{count + 1}`, `{a ? b : c}`, 비교). 되는 것: 앱이 넣는 값이 준다. 대가: 식의 형·오류·평가 비용과 한도를 새로 정한다. 신뢰하지 않는 글이 계산을 고른다. 한번 열면 되돌리기 어렵다.
- 권장: a. 대가: 위의 값 표 짓기다. 길 c는 a 위에 나중에 더할 수 있지만 거꾸로는 안 된다. 승인: 길 a (2026-11-03).

**5. 사건을 잇는 때.** 질문: `action` 이름을 앱 메시지로 언제 바꾸는가. 확정 N3 a의 "`activate_link`와 같은 꼴"은 이름에서 앱 메시지를 짓는 함수 하나라는 모양을 가리킨다. 부르는 때는 이 결정이 정하고, 길 a는 앱 함수를 입력 thread에서 부르지 않으려고 펼칠 때로 한다.

- 길 a: 펼칠 때 logic thread에서 `make_action`을 불러 값을 쥔다(이 문서). 되는 것: `make_message_action`과 같은 꼴이라 input·UI thread가 앱 함수를 부르지 않는다([검사기](inspector-design.md) 143행과 같은 까닭). 모르는 이름이 펼칠 때 오류로 드러난다. 대가: 누르는 요소마다 frame마다 함수가 한 번 불린다.
- 길 b: 누를 때 부른다(`activate_link` 꼴). 되는 것: 누르지 않은 요소의 비용이 없다. 대가: 앱 함수가 액션을 돌리는 thread에서 불려 앱 상태를 잡지 않는 순수 함수여야 한다. 모르는 이름이 누를 때에야 드러난다.
- 길 c: 이름에서 `input_action`으로 가는 값 표를 받는다. 되는 것: 함수가 없다. 대가: `argument`마다 다른 메시지를 지으려면 항목마다 표를 늘린다.
- 권장: a. 대가: 위의 호출 수다. 권장대로 진행.

**6. id와 kind.** 질문: 마크업의 `id`가 어떤 `ui_element_id`가 되는가.

- 길 a: owner는 `id`, kind는 `kind` 속성의 앱 kind이거나 새 라이브러리 kind `markup_node`(이 문서). 되는 것: 앱이 kind를 정하지 않아도 id가 서고, 라이브러리 구조 부품의 `{none, owner}`와 겹치지 않는다. 검사기에 `markup_node:<id>`로 보여 어디서 왔는지 읽힌다. 대가: kind가 하나 늘고 같은 커밋에 이름표 case가 붙는다.
- 길 b: `id`가 없으면 `none`이고, 있으면 `kind`를 꼭 적는다. 되는 것: 라이브러리 kind가 늘지 않는다. 대가: 이름 붙은 요소마다 앱 kind 상수가 든다. `kind`를 빼고 `none`을 쓰게 두면 modal 판(`{none, owner}`) 같은 구조 부품과 겹칠 수 있다.
- 길 c: 앱이 context에 기본 kind 하나를 준다. 되는 것: 라이브러리 kind가 늘지 않는다. 대가: 앱마다 같은 상수를 하나씩 정하고, 검사기에 앱 이름으로 보여 마크업 요소가 다른 앱 요소와 섞인다.
- 권장: a. kind 이름표의 형은 검사기의 `inspector_kind_name`이라 앱이 한 벌을 두 곳에 준다. 대가: 위의 kind 하나다. 권장대로 진행.

**7. 오류 정책.** 질문: 틀린 글과 맞지 않는 값에서 무엇을 돌려주는가.

- 길 a: 읽기는 전체를 막고 오류를 모두 모은다. 펼치기는 늘 서고 빈 값으로 채운 자리를 오류로 알린다(이 문서). 되는 것: 다시 읽기가 실패하면 이전 틀이 남는다([스타일 읽기](style-sheet-design.md) 결정 3과 같다). 값 하나가 빠져도 화면이 비지 않는다. 대가: 펼침 오류가 있는 화면이 그대로 보여 앱이 오류 목록을 보지 않으면 놓친다.
- 길 b: 읽기도 부분으로. 모르는 요소·속성은 건너뛰고 경고만 둔다. 되는 것: 오타 하나로 화면 전체가 이전에 머물지 않는다. 대가: 반쯤 맞는 화면이 서서 "바꿨는데 안 바뀐다"가 숨는다. 새 판 어휘를 쓴 파일이 옛 판에서 빈 자리로 선다.
- 길 c: 펼치기도 전체를 막는다(element 없음). 되는 것: 틀린 값이 화면에 서지 않는다. 대가: tree는 frame마다 새로 지어 이전 element를 다시 쓸 수 없으므로 앱이 대신할 화면을 따로 짓는다.
- 권장: a. 대가: 위의 놓침이다. 데모는 오류 수를 상태 줄에 그린다. 권장대로 진행.

**8. 앱 정의 요소.** 질문: 어휘 밖의 요소를 마크업 안에 어떻게 두는가.

- 길 a: `slot`과 펼칠 때의 `make_slot`(이 문서). 되는 것: 편집 칸·목록·그림·앱 고유 요소가 지금의 C++ 그대로 마크업 자리에 든다. 읽기가 앱 어휘를 알 필요가 없다. 대가: slot 안은 마크업으로 고치지 못한다.
- 길 b: 앱이 태그 이름, 속성 꼴, factory를 등록하고 읽기가 그 표로 확인한다. 되는 것: 앱 요소도 속성으로 적는다. 대가: 등록 표를 읽기 전에 받아야 해 `parse_ui_markup`이 앱 값에 기대고, 속성 꼴의 공개 형식이 커진다.
- 길 c: 두지 않는다. 앱은 마크업 가지 바깥에만 C++ 요소를 둔다. 되는 것: 가장 작다. 대가: 칸 하나 때문에 화면 전체를 C++로 돌린다.
- 권장: a. 길 b는 2단계에서 a 위에 더할 수 있다. 대가: 위의 slot 안이다. 권장대로 진행.

**구현 때 정한 것(2026-11-03).** 결정 절의 길을 바꾸지 않는 세부라 규칙 절에 적었다.

- 점 없는 `{이름}`은 안쪽 항목부터 바깥 항목, 마지막에 `values`에서 찾는다. 까닭: "바깥 이름도 보인다(안쪽이 먼저다)"를 글자 그대로 따르면 반복 안의 `{title}`이 그 항목의 것이다. slot이 받는 표도 같은 차례로 덮어 짓는다.
- slot의 표를 짓도록 `ui_markup_scope::entries()`(넣은 차례의 읽기 전용 목록)를 공개한다. 까닭: 표를 덮어 지으려면 항목의 이름을 다 돌아야 한다. 값 형 넷과 `set`·`find`는 그대로다.
- 수 속성의 빈 값은 0이 아니라 그 요소의 기본값이고, 범위 밖의 바인딩 수도 `wrong_value_type`이다. 까닭: 0 이하의 `fontSize`·`thickness`와 음수 `maxLines`(부호 없는 칸)를 짓지 않는다.
- 같은 자리의 같은 오류는 하나만 남긴다. 까닭: 목록 항목 천 개에 같은 오타 하나가 오류 천 줄이 되지 않는다.
- `action`을 적지 않은 단추는 활성인 채 액션 없이 선다(오류 아님). 빈 slot은 stack 안의 고정·유연 자리를 빈 자리로 남긴다.
- `describe`의 `wrong_value_type`은 끝에 적은 이름을 붙인다(`… on label: count`). 까닭: 한 요소의 여러 속성이 같은 이름을 읽을 때 어느 바인딩인지 가린다.

## 플랫폼

[화면 마크업](markup-design.md)의 플랫폼 절과 같다. 바인딩은 core 코드이고 thread 규칙(읽기는 어느 thread, 펼치기는 logic thread)만 있다. Android에서도 Java 소스 0줄, JNI 없음이고 unsupported인 부분은 없다.

## 다른 노드와의 약속

- **hot_reload(R5, 같은 줄).** [스타일 읽기](style-sheet-design.md) 227행의 다섯 약속을 그대로 따른다. (1) `parse_ui_markup`은 순수 함수라 감시 thread에서 불러도 되고 `shared_ptr<const ui_markup_document>`만 `post_app_message`로 넘긴다. 틀은 불변이라 thread 사이에 나눠 쥔다. (2) 파일 하나가 틀 하나다. 다시 읽기는 파일 전체를 다시 읽는다. (3) 실패하면 `document`가 비고 앱은 쥔 틀을 바꾸지 않는다. 화면은 이전 틀로 남는다. (4) 반쯤 쓴 파일은 `syntax`로 막힌다. 쓰는 쪽은 `replace_file_atomically`를 쓴다. (5) 오류는 `describe` 한 줄이다. 감시는 [파일 감시](file-watch-design.md)의 `file_watch_scope::file`이다(236~242행).
- **상태 보존(같은 약속).** 앱 상태는 logic driver에 있고 틀은 구조만 든다. 그래서 다시 읽어도 값이 그대로다. 초점·hover·눌림은 id로 이어져 `id`·`key`가 바뀌지 않은 요소는 그대로 남는다. 다시 읽은 틀에서 사라진 id의 초점은 controller가 거둔다(`interaction_controller::update_focus`, `ui_interaction.cpp` 4506~4507행). 스크롤 자리처럼 앱이 id로 쥔 상태도 같다.
- **문자열 표(마크업 후속).** R5의 [문자열 표](i18n-design.md)는 마크업을 열지 않았다(그 문서의 결정 6). `{` 안의 `:`는 그대로 비워 둔다. 표 조회는 마크업 후속에서 `{t:key}` 같은 접두로 더할 수 있고 지금의 이름 찾기와 겹치지 않는다. 그 전까지 번역 글은 앱이 고른 문장을 값으로 넣는다.
- **줄 3(controller).** 사건과 초점은 지금의 `ui_action`·id 길이라 `ui_interaction.*`을 고치지 않는다. 줄 3에 약속할 것이 없다.
- **검사기(R2).** `kinds`는 `inspector_config::kind_names`와 같은 형이라 앱이 한 벌을 두 곳에 준다.

## 확인

자동 test. 모두 `tests/ui_markup_tests.cpp`(`luil_core_tests`, Windows와 Android 기기)이다.

- 값: 값 표를 바꾼 다음 펼침에 새 값이 나오는지. 형 틀림과 없는 이름이 빈 값과 오류 하나인지. 글 끼우기와 `{{`·`}}`, `{` 안의 `:`가 막히는지. 바인딩한 `padding`이 적은 `paddingLeft`를 덮지 않는지(행 순서), 범위 밖의 바인딩 수가 형 오류이고 0~1 속성은 잘리는지.
- 반복과 조건: owner가 `id`, U+001F, key 순인지, 겹친 반복, 빈 목록, key 겹침이 `duplicate_key`와 `duplicate_ids()`에 함께 남는지. `if`·`unless`가 고른 가지만 짓는지(값이 없으면 `unless`의 가지가 선다). `max_built`에서 `too_many_built` 하나로 멈추는지. 기본 한도보다 깊은 틀(256단)을 재귀 없이 펼치는지, 상한을 넘는 틀(257단)은 뿌리만 서고 `too_large` 하나인지.
- 사건: 펼칠 때 `make_action`이 이름·인자·id로 요소마다 한 번 불리고, 지은 `ui_action`이 그 값을 돌려주며, 액션을 돌릴 때 앱 함수가 다시 불리지 않는지. 빈 값이면 단추가 비활성이고 체크가 누를 수 없는지.
- id·kind: `kind` 이름이 앱 kind로, 없으면 `markup_node`로 서는지. `unknown_kind`, `owner_prefix`, 이름 없는 요소의 자리 owner가 frame 사이에 같은지.
- slot: scope가 반복 변수를 보는지, nullptr이면 `unknown_slot`이고 자리가 비는지.
- 초점 유지: `ui_interaction_tests.cpp`의 틀로 controller에 펼친 tree 둘을 차례로 주고(반복 앞에 항목 하나를 끼운 뒤) 초점이 같은 key의 단추에 남는지.
- 오류: 종류마다 줄·열·`offset`·`path`·`describe` 글. 같은 틀·같은 값이면 오류 목록이 같은지.

데모와 사람 확인은 [화면 마크업](markup-design.md)의 확인 절이다. `markup-status`가 읽기 오류와 펼침 오류를 갈라 말하고, 견본 "없는 값"이 펼침 오류(가지는 선다)를, "모르는 속성"·"닫지 않은 태그"가 읽기 거부(이전 틀 유지)를 보인다.

## 범위 밖

- 식 언어(비교·산술·삼항), 값의 양방향 묶음, 관찰·갱신.
- 앱 정의 태그 등록(결정 8의 길 b). 좌표·끌기 액션과 한 요소의 여러 사건.
- 다시 읽기의 감시와 디바운스는 [핫 리로드](hot-reload-design.md)가 닫았다. 문자열 표 조회(마크업 후속).

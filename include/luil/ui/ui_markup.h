#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_inspect.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 화면 마크업이다 (docs/markup-design.md, docs/markup-binding-design.md).
// 앱이 실행 중에 마크업 글을 읽어 불변인 틀(`ui_markup_document`)로 두고, frame마다 그 틀을 자기 상태로 펼친다.
//  - 문법은 XML 1.0의 엄한 부분집합이다. 받는 글은 모두 바른 XML이고, 받지 않는 구문(DTD·CDATA·이름 공간·처리 지시)은
//    조용히 넘기지 않고 오류다. 바깥 엔티티와 넣기가 없어 읽기는 파일·주소를 열지 않는다.
//  - 이름·enum·수·자리·id 꼴은 읽을 때 모두 확인한다. 펼칠 때(`build_ui_markup`, 바인딩 문서)는 값을 찾고 요소를 짓기만 한다.
//  - 이름은 글 본문을 그리는 `markdown_*`과 섞이지 않도록 늘 `ui_markup`으로 시작한다.
namespace luil {
    // 읽기와 펼치기의 한도다. 마크업 글을 앱 밖에서 온 신뢰하지 않는 파일로 보고 정한다.
    //  - 넘으면 `too_large` 하나를 남기고 멈춘다. 읽기는 깊이를 읽는 자리에서 세어 재귀가 깊어지기 전에 멈춘다.
    struct ui_markup_limits
    {
        // 글의 바이트 수다 (맨 앞의 BOM 포함). 넘으면 읽지 않는다.
        std::size_t max_bytes { std::size_t { 1 } * 1024 * 1024 };
        // 요소 중첩이다. 뿌리가 1단이고 `for`·`if`·`unless`도 한 단이다.
        std::size_t max_depth { 64 };
        // 틀의 요소 수다 (구조 요소 포함, 글과 주석은 세지 않는다).
        std::size_t max_nodes { 10000 };
        // 한 번 펼칠 때 짓는 element 수다. 틀이 쥐고 펼칠 때 쓴다.
        std::size_t max_built { 50000 };
    };

    // 읽은 틀이다. 이름·속성·수·자리를 모두 확인하고 enum과 수를 C++ 값으로 바꿔 둔 불변 값이다. 속은 비공개다.
    //  - 불변이라 thread 사이에 `shared_ptr<const ui_markup_document>`로 나눠 쥔다.
    class ui_markup_document;

    enum class ui_markup_error_kind
    {
        // 읽을 때 (parse_ui_markup)
        // 닫지 않은 태그, 짝이 틀린 닫기, 따옴표 없는 값, 모르는 엔티티, 깨진 UTF-8, XML 글자 밖의 글자, 뿌리 둘, 뿌리 뒤의 글이다.
        syntax,
        // DTD, 처리 지시, CDATA, 이름 공간(이름 속의 `:`와 `xmlns`), UTF-8 밖의 encoding, 1.0 밖의 version이다.
        unsupported,
        // max_bytes·max_depth·max_nodes를 넘었다.
        too_large,
        unknown_element,
        unknown_attribute,
        duplicate_attribute,
        // 단추·체크의 id, slot의 name, for의 each·as·key, if·unless의 test다.
        missing_attribute,
        // 허용 이름 밖, 수가 아님, 범위 밖, id·이름·바인딩의 꼴, 구체 색, 함께 못 쓰는 자리 속성이다.
        invalid_value,
        // 받지 않는 자식이나 글이다 (panel의 둘째 자식, label 안의 요소, 뿌리가 된 구조 요소).
        invalid_child,
        // 원하는 크기를 답하지 않는 요소나 slot에 stack 안의 자리가 없다.
        missing_length,
        // 같은 id를 두 번 적었다.
        duplicate_id,
        // 펼칠 때 (build_ui_markup, docs/markup-binding-design.md)
        // 값 표에 없는 이름이다.
        unknown_value,
        // 형이 맞지 않는 값이다 (글 자리에 수, each에 글).
        wrong_value_type,
        // make_action이 빈 값을 돌려주었다.
        unknown_action,
        // make_slot이 nullptr를 돌려주었다.
        unknown_slot,
        // kind 이름이 이름표에 없다.
        unknown_kind,
        // 한 반복 안에서 key가 겹친다.
        duplicate_key,
        // max_built를 넘었다.
        too_many_built,
    };

    struct ui_markup_error
    {
        ui_markup_error_kind kind { ui_markup_error_kind::syntax };
        // 1부터다. 펼칠 때 오류는 그 속성이 적힌 자리다.
        std::size_t line { 0 };
        // 1부터, 줄 처음부터 센 코드 포인트다. 맨 앞의 BOM은 세지 않는다.
        std::size_t column { 0 };
        // 0부터 센 바이트 자리다 (BOM 포함).
        std::size_t offset { 0 };
        // 요소 자리다 ("stack/stack[2]/textButton"). 뿌리부터 요소 이름을 `/`로 잇고, 같은 이름의 형제가 여럿이면 `[n]`(1부터)을 붙인다.
        //  - 구조 요소(`for`·`if`·`unless`)도 한 단이다.
        //  - `invalid_child`는 그 자식을 받지 않은 부모의 자리다. 뿌리가 될 수 없는 요소면 비어 있다.
        //  - `syntax`·`unsupported`·`too_large`는 비어 있다. 글이 다 읽히기 전이라 형제를 다 모른다. 줄·열이 자리를 말한다.
        std::u8string path {};
        // 문제의 요소·속성·값·사건 이름이다.
        //  - `syntax`는 걸린 이름(닫지 않았거나 짝이 틀린 요소, 모르는 엔티티, 값이 깨진 속성)이고 없으면 비어 있다.
        //  - `too_large`는 넘은 한도의 필드 이름(`max_bytes`·`max_depth`·`max_nodes`·`max_built`)이다.
        //  - `invalid_child`는 받지 않은 자식 요소의 이름이고, 글이면 비어 있다(글은 `value`).
        //  - 펼칠 때: `unknown_value`는 적은 이름(`cuont`, `item.title`), `wrong_value_type`은 속성 이름, `unknown_action`·`unknown_slot`·
        //    `unknown_kind`는 그 이름, `duplicate_key`는 `key`, `too_many_built`는 `max_built`다.
        std::u8string name {};
        // 받은 글이다. 64바이트에서 글자 경계로 자른다.
        //  - `syntax`는 그 자리에서 만난 것이다 (짝이 틀린 닫기의 이름, `&nbsp;`, 깨진 바이트의 `0xC3`, 막는 글자의 `U+0001`).
        //  - `too_large`·`too_many_built`는 한도의 값이다.
        //  - `wrong_value_type`은 적은 이름, `duplicate_key`는 겹친 key다.
        std::u8string value {};

        [[nodiscard]] bool operator==(const ui_markup_error&) const = default;
    };

    // "4:21 Unknown attribute on label: colr" 꼴의 영문 한 줄이다. 개발자용 진단이라 앱의 언어를 따르지 않는다.
    [[nodiscard]] std::u8string describe(const ui_markup_error& error);

    struct ui_markup_parse_result
    {
        // 오류가 하나라도 있으면 비어 있다. 부르는 쪽은 쥔 틀을 그대로 둔다.
        std::shared_ptr<const ui_markup_document> document {};
        // document가 있으면 비어 있다. 순서는 글의 자리(offset) 순이다.
        //  - `syntax`·`unsupported`·`too_large`는 그 하나뿐이다. 앞서 모은 어휘 오류도 남기지 않는다.
        //  - 나머지는 끝까지 모은다.
        std::vector<ui_markup_error> errors {};

        [[nodiscard]] bool ok() const noexcept
        {
            return document != nullptr;
        }
    };

    // 마크업 글(UTF-8)을 읽는다. 파일을 읽지 않는다. 전역 상태가 없어 어느 thread에서나 부른다.
    [[nodiscard]] ui_markup_parse_result parse_ui_markup(std::u8string_view text, const ui_markup_limits& limits = {});

    // --- 펼치기 (docs/markup-binding-design.md) ---

    class ui_markup_scope;
    // 반복이 도는 목록이다. 항목마다 이름 값 표 하나다. 나눠 쥐어 frame마다 표를 다시 지어도 목록을 복사하지 않는다.
    using ui_markup_list = std::shared_ptr<const std::vector<ui_markup_scope>>;
    // 앱이 주는 값이다. 참거짓·수·글·목록 넷뿐이다 (결정 4).
    //  - 수를 글로 바꾸지 않는다. 자리 구분과 소수점이 언어마다 달라 앱이 글로 지어 넣는다.
    using ui_markup_value = std::variant<bool, double, std::u8string, ui_markup_list>;

    // 이름에서 값으로 가는 표다. 반복의 항목도 이 표 하나다. frame마다 앱이 짓는 값이다.
    //  - 관찰·갱신 구조가 없다. tree가 frame마다 새로 서므로 펼칠 때의 값이 곧 화면이다.
    //  - 이름 수가 적은 표라 차례로 찾는다.
    class ui_markup_scope
    {
    public:
        // 같은 이름은 덮어쓴다. 이어 부르도록 자기를 돌려준다.
        ui_markup_scope& set(std::u8string name, ui_markup_value value);
        [[nodiscard]] const ui_markup_value* find(std::u8string_view name) const noexcept;
        // 넣은 차례의 이름과 값이다. slot이 받는 표(바깥 표 위에 항목을 덮은 것)를 짓는 데 쓴다.
        [[nodiscard]] std::span<const std::pair<std::u8string, ui_markup_value>> entries() const noexcept
        {
            return values_;
        }

    private:
        std::vector<std::pair<std::u8string, ui_markup_value>> values_ {};
    };

    // 사건 하나다. 펼칠 때 앱 함수(`ui_markup_context::make_action`)가 받는다. 글은 그 호출 동안만 산다.
    struct ui_markup_action
    {
        // `action` 속성의 이름이다.
        std::u8string_view name {};
        // `argument` 속성을 펼친 글이다. 없으면 비어 있다.
        std::u8string_view argument {};
        // 그 요소의 id다 (반복 key까지 든 owner).
        ui_element_id id {};
    };

    struct ui_markup_context
    {
        // 이름을 찾는 표다. 비어 있으면 모든 이름이 `unknown_value`다.
        const ui_markup_scope* values { nullptr };
        // 이름을 앱 메시지로 바꾼다. 펼칠 때 logic thread에서 누르는 요소마다 한 번 부르고, 돌려준 값을 액션이 쥔다 (결정 5).
        //  - 누를 때 다시 부르지 않는다. input·UI thread는 앱 함수를 부르지 않는다 (`make_message_action`과 같은 꼴).
        //  - 모르는 이름이면 빈 값(`std::monostate`)을 돌려준다. 단추는 비활성으로, 체크는 누를 수 없게 선다.
        std::function<input_action(const ui_markup_action& action)> make_action {};
        // `slot` 요소를 앱이 짓는다 (결정 8). 펼칠 때 부른다. 모르는 이름이면 nullptr다.
        //  - `scope`는 그 자리에서 보이는 이름이다. 바깥 표 위에 둘러싼 반복의 항목을 바깥부터 덮은 표다 (안쪽이 이긴다).
        //  - 돌려준 요소는 앱이 정한 id를 그대로 쓰고 마크업의 자리 속성으로 담긴다.
        std::function<std::unique_ptr<ui_element>(std::u8string_view name, const ui_markup_scope& scope)> make_slot {};
        // `kind` 속성의 이름표다. 검사기에 주는 것과 같은 형·같은 값이다 (`inspector_config::kind_names`).
        std::span<const inspector_kind_name> kinds {};
        // 비어 있지 않으면 모든 owner 앞에 이것과 단위 구분자(U+001F)를 둔다. 같은 틀을 한 tree에 두 번 펼칠 때 쓴다.
        //  - slot이 돌려준 요소의 id는 앱의 것이라 붙이지 않는다.
        std::u8string owner_prefix {};
    };

    struct ui_markup_build_result
    {
        // 틀의 뿌리다. 오류가 있어도 선다 (결정 7). 배치하지 않은 채라 앱이 `arrange`해 끼운다.
        std::unique_ptr<ui_element> element {};
        // 펼칠 때 오류다. 글의 자리(offset) 순이고, 같은 자리의 같은 오류는 하나다 (반복이 같은 실수를 항목마다 되풀이하지 않는다).
        //  - 같은 틀·같은 값이면 같은 목록이라 앱이 frame마다 같은 줄을 띄워도 흔들리지 않는다.
        std::vector<ui_markup_error> errors {};
        // 지은 element 수다 (뿌리와 slot이 돌려준 요소 포함, 구조 요소와 gap은 세지 않는다).
        std::size_t built { 0 };
    };

    // 틀을 앱 값으로 펼친다. 앱 함수를 부르므로 logic thread(`make_frame`)에서 부른다.
    //  - 재귀하지 않는다. 앱이 `max_depth`를 크게 올린 틀도 호출 깊이가 글을 따라 깊어지지 않는다.
    //  - 뿌리는 늘 선다. 그 밖의 element가 `max_built`에 닿으면 `too_many_built` 하나를 남기고 그 뒤를 짓지 않는다.
    [[nodiscard]] ui_markup_build_result build_ui_markup(const ui_markup_document& document, const ui_markup_context& context);
}

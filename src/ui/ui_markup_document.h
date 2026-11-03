#pragma once

// 화면 마크업 틀의 속이다 (docs/markup-design.md). 읽기(ui_markup_read.cpp)·어휘(ui_markup_vocabulary.cpp)·펼치기가 함께 쓰고
// 시험이 들여다본다. 공개 헤더에는 `ui_markup_document`의 이름만 있다.

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/label_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_markup.h"
#include "luil/ui/wrap_element.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace luil {
    // 글 속의 자리다. 오류가 이 셋을 그대로 싣는다 (`ui_markup_error`).
    struct ui_markup_position
    {
        std::size_t line { 1 };
        std::size_t column { 1 };
        std::size_t offset { 0 };

        [[nodiscard]] bool operator==(const ui_markup_position&) const = default;
    };

    inline constexpr std::size_t ui_markup_no_parent { static_cast<std::size_t>(-1) };

    // 엄한 XML 읽기가 지은 나무다. 어휘를 모른다.
    struct ui_markup_xml_attribute
    {
        std::u8string name {};
        // 줄 끝·공백을 XML 규칙대로 공백 하나로 바꾸고 엔티티를 푼 값이다.
        std::u8string value {};
        // 이름의 첫 글자다.
        ui_markup_position at {};
    };

    struct ui_markup_xml_node
    {
        // 요소의 이름이다. 글 조각이면 비어 있다.
        std::u8string name {};
        // 글 조각이다. 엔티티를 풀고 줄 끝을 LF로 맞춘 글이다. 요소면 비어 있다.
        //  - 공백(XML의 S: 공백·탭·CR·LF)뿐인 글은 버린다. 참조(`&#32;`)는 적은 글이라 남는다.
        std::u8string text {};
        bool is_text { false };
        // 요소는 `<`, 글은 공백이 아닌 첫 글자다.
        ui_markup_position at {};
        std::size_t parent { ui_markup_no_parent };
        std::vector<ui_markup_xml_attribute> attributes {};
        std::vector<std::size_t> children {};
    };

    struct ui_markup_xml_result
    {
        // nodes[0]이 뿌리 요소다. 오류가 있으면 비어 있다.
        std::vector<ui_markup_xml_node> nodes {};
        std::optional<ui_markup_error> error {};
    };

    // 글을 엄한 XML 부분집합으로 읽는다. 오류는 하나뿐이다 (syntax·unsupported·too_large).
    //  - 재귀하지 않는다. 깊이는 한도가 읽는 자리에서 센다. 어휘 확인도 재귀하지 않는다.
    [[nodiscard]] ui_markup_xml_result read_ui_markup_xml(std::u8string_view text, const ui_markup_limits& limits);

    // 어휘의 요소다. 이름은 C++ 클래스 이름에서 `_element`를 뗀 것이다 (`text_button` ← `textButton`).
    enum class ui_markup_tag
    {
        stack,
        wrap,
        panel,
        label,
        text_button,
        check,
        progress,
        gap,
        slot,
        // `for`·`if`·`unless`다. element를 짓지 않고 자식들이 부모의 자식 자리에 차례로 든다.
        for_each,
        if_true,
        unless_true,
    };

    // 이름 하나다. `{name}`이거나 반복 변수의 `{item.name}`이다.
    struct ui_markup_reference
    {
        // 반복 변수(`for`의 `as`)다. 비면 값 표의 이름이다.
        std::u8string variable {};
        std::u8string name {};

        [[nodiscard]] bool operator==(const ui_markup_reference&) const = default;
    };

    // 바인딩한 값의 한 조각이다. 적은 글이거나 이름 하나다.
    //  - `{{`·`}}`는 적은 글의 `{`·`}`로 풀어 둔다.
    struct ui_markup_piece
    {
        std::u8string text {};
        std::optional<ui_markup_reference> reference {};

        [[nodiscard]] bool operator==(const ui_markup_piece&) const = default;
    };

    // 요소의 config다. 구조 요소·gap·slot은 비어 있다.
    using ui_markup_config = std::variant<std::monostate, stack_config, wrap_config, panel_config, label_config, text_button_config, check_config, progress_config>;

    // 요소마다 같은 속성이다 (`ui_element`의 set_enabled·set_visible·set_tooltip·set_access_name으로 간다).
    //  - check의 tooltip은 `check_config::tooltip`이라 여기를 쓰지 않는다.
    struct ui_markup_common
    {
        bool enabled { true };
        bool visible { true };
        std::u8string tooltip {};
        std::u8string access_name {};
    };

    // 속성이 거는 자리다. 행의 `apply`가 config·공통 속성·stack 안의 자리 중 하나를 고친다.
    struct ui_markup_values
    {
        ui_markup_config config {};
        ui_markup_common common {};
        stack_item item {};
    };

    // 적은 값을 바꾼 C++ 값이다. 글은 글, 수는 double, 참거짓은 bool, 이름(enum·역할)은 표의 값(int)이다.
    using ui_markup_literal = std::variant<bool, double, std::u8string, int>;

    enum class ui_markup_value_type
    {
        text,
        number,
        boolean,
        // 표의 이름 중 하나다. 바인딩을 받지 않는다.
        choice,
    };

    // 수가 받는 범위다. 모두 유한하고 float에 든다.
    enum class ui_markup_number_rule
    {
        // 0 이상이다 (길이·간격·크기).
        non_negative,
        // 0보다 크다 (`fontSize`·`thickness`·`weight`).
        positive,
        // 0 이상 1 이하다 (`value`·`shadow`).
        unit,
        // 0 이상의 정수다 (`maxLines`).
        count,
    };

    struct ui_markup_choice
    {
        std::string_view name {};
        int value { 0 };
    };

    // 어휘 표의 한 행이다. 속성 이름과 그것이 고치는 C++ 칸의 짝이다 (ui_markup_vocabulary.cpp의 표).
    struct ui_markup_row
    {
        std::string_view name {};
        ui_markup_value_type type { ui_markup_value_type::text };
        ui_markup_number_rule rule { ui_markup_number_rule::non_negative };
        std::span<const ui_markup_choice> choices {};
        // 값을 건다. 값의 형은 `type`이 정한 것이다 (text → u8string, number → double, boolean → bool, choice → int).
        void (*apply)(ui_markup_values& values, const ui_markup_literal& value) { nullptr };
    };

    // 속성 하나다. 적은 값이면 `literal`이고, 이름을 적었으면 `pieces`다.
    struct ui_markup_attribute
    {
        // config·공통·자리에 거는 속성의 행이다. `each`·`test`·`key`·`argument`처럼 요소가 따로 쥐는 속성은 비어 있다.
        const ui_markup_row* row { nullptr };
        ui_markup_literal literal {};
        // 바인딩이다. 수·참거짓·`each`·`test`는 이름 하나뿐이고, 글은 적은 글과 이름이 섞인다.
        std::vector<ui_markup_piece> pieces {};
        // 속성 이름의 첫 글자다. 펼칠 때 오류가 이 자리를 싣는다.
        ui_markup_position at {};

        [[nodiscard]] bool bound() const noexcept
        {
            return pieces.empty() == false;
        }
    };

    // stack 안의 자리다. 읽을 때 적은 속성에서 정한다.
    enum class ui_markup_placement
    {
        // stack 밖이거나 구조 요소다.
        none,
        // 원하는 크기다 (`fit`을 적었거나, 아무것도 적지 않은 맞춤 요소).
        fit,
        // `length`다.
        fixed,
        // `weight`다.
        flexible,
    };

    struct ui_markup_node
    {
        ui_markup_tag tag { ui_markup_tag::stack };
        // 여는 태그의 `<`다.
        ui_markup_position at {};
        std::size_t parent { ui_markup_no_parent };
        // 자리의 한 단이다 ("stack[2]"). 오류의 `path`는 뿌리부터 이것을 `/`로 잇는다.
        std::u8string step {};
        std::vector<std::size_t> children {};
        // config·공통·자리에 거는 속성이다. 표의 행 순서라 `padding` 뒤에 `paddingLeft`가 온다 (한 변이 이긴다).
        std::vector<ui_markup_attribute> attributes {};
        ui_markup_placement placement { ui_markup_placement::none };

        // 요소의 이름이다. 적은 글자뿐이다.
        std::u8string id {};
        ui_markup_position id_at {};
        // 앱 kind의 이름이다. 비면 `markup_node`다 (펼칠 때 이름표에서 찾는다).
        std::u8string kind {};
        ui_markup_position kind_at {};
        // 사건 이름과 그 인자다 (textButton·check).
        std::u8string action {};
        ui_markup_position action_at {};
        std::optional<ui_markup_attribute> argument {};
        // slot의 이름이다.
        std::u8string slot_name {};
        ui_markup_position slot_name_at {};
        // for의 목록(이름 하나)·반복 변수·key다.
        std::optional<ui_markup_attribute> each {};
        std::u8string as {};
        std::optional<ui_markup_attribute> key {};
        // if·unless의 참거짓이다.
        std::optional<ui_markup_attribute> test {};
    };

    // 공개 헤더의 틀이다. 읽기가 짓고 나면 바꾸지 않는다.
    class ui_markup_document
    {
    public:
        // nodes[0]이 뿌리다. 자식은 늘 부모보다 뒤에 있다.
        std::vector<ui_markup_node> nodes {};
        ui_markup_limits limits {};
    };

    // 읽은 나무를 어휘로 확인해 틀을 짓는다. 오류가 하나라도 있으면 틀이 비고, 오류는 자리 순이다.
    struct ui_markup_check_result
    {
        std::shared_ptr<const ui_markup_document> document {};
        std::vector<ui_markup_error> errors {};
    };

    [[nodiscard]] ui_markup_check_result check_ui_markup_vocabulary(std::span<const ui_markup_xml_node> nodes, const ui_markup_limits& limits);

    // 그 마크업 이름의 어휘 요소다. 모르는 이름이면 비어 있다.
    [[nodiscard]] std::optional<ui_markup_tag> ui_markup_tag_of(std::u8string_view name) noexcept;
    [[nodiscard]] std::u8string_view ui_markup_tag_name(ui_markup_tag tag) noexcept;

    // 그 요소가 받는 속성의 행이다 (config와 공통 속성). 자리 속성은 `ui_markup_item_rows`다.
    [[nodiscard]] std::span<const ui_markup_row> ui_markup_rows(ui_markup_tag tag) noexcept;
    [[nodiscard]] std::span<const ui_markup_row> ui_markup_item_rows() noexcept;
    // `panel`의 `background`가 받는 역할 이름이다. 스타일 키 표의 `palette_role_names`를 그대로 옮긴 것이다.
    [[nodiscard]] std::span<const ui_markup_choice> ui_markup_palette_roles() noexcept;

    // 그 요소의 기본값이다 (config는 그 요소의 것, 공통과 자리는 기본).
    [[nodiscard]] ui_markup_values ui_markup_default_values(ui_markup_tag tag);
    // 적은 값만 표 순서대로 건 값이다. 바인딩한 속성은 건너뛴다.
    //  - 펼치기는 이것을 바탕으로 쓰지 않는다. 바인딩한 `padding`이 적은 `paddingLeft`를 덮지 않도록 모든 속성을 표 순서대로
    //    다시 건다 (바인딩은 찾은 값으로).
    [[nodiscard]] ui_markup_values ui_markup_literal_values(const ui_markup_node& node);

    // 뿌리부터 그 노드까지의 자리다 ("stack/stack[2]/textButton").
    [[nodiscard]] std::u8string ui_markup_path(std::span<const ui_markup_node> nodes, std::size_t index);

    // 오류 하나를 짓는다. `value`는 64바이트에서 글자 경계로 자른다.
    [[nodiscard]] ui_markup_error ui_markup_error_at(ui_markup_error_kind kind, const ui_markup_position& at, std::u8string path, std::u8string_view name, std::u8string_view value);
    // 64바이트에서 글자 경계로 자른 글이다. 바른 UTF-8을 받는다.
    [[nodiscard]] std::u8string ui_markup_clip(std::u8string_view text);
    // 십진 글이다 (한도의 값).
    [[nodiscard]] std::u8string ui_markup_decimal(std::size_t value);
}

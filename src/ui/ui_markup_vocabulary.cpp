#include "ui/ui_markup_document.h"

#include "luil/generated/ui_style_keys.h"
#include "luil/theme/ui_style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

// 화면 마크업의 어휘다 (docs/markup-design.md 어휘 절).
//  - 표의 행 하나가 속성 이름 하나와 그것이 고치는 C++ 칸 하나의 짝이다. 이름은 config 필드 이름의 camelCase이고
//    enum 값도 열거자의 camelCase다. 실행 중 이름 바꾸기는 없다.
//  - C++20은 config의 필드를 셀 수 없어, config에 새 필드가 늘어도 이 표는 모른다. 새 필드를 마크업에 내려면 행을 더한다.
//    짝이 맞는지는 시험(ui_markup_tests.cpp)이 행마다 기본값이 아닌 값을 적어 C++로 같은 값을 준 config와 견준다.
//  - 짝:
//    stack     stack_config      direction·spacing·padding(네 변)·paddingLeft/Top/Right/Bottom·crossAlignment·mainAlignment
//    wrap      wrap_config       itemWidth·itemHeight·spacing·lineSpacing
//    panel     panel_config      background(palette_role 선택자)·cornerRadius·shadow
//    label     label_config      text·fontSize·fontWeight(weight)·slant·color·background·padding·align·elide·wrap·lineBreak·maxLines
//    textButton text_button_config text·visual·defaultButton·selected
//    check     check_config      style·label·checked·mixed·tooltip
//    progress  progress_config   value·thickness
//    공통      ui_element        enabled·visible·tooltip·accessName (check의 tooltip은 config 칸이다)
//    자리      stack_item        length·weight·crossLength·minimum·maximum·fit (gap은 length·weight)
//  - 내지 않는 칸: 함수 칸(선택자·factory·스크롤 액션), `text_button_config::glyph`·`fill`·`label_color`, `panel_config::border`·
//    `border_sides`. 이름·액션·인자·반복·조건은 행이 아니라 노드가 따로 쥔다 (`ui_markup_node`).
namespace luil {
    namespace {
        namespace keys = generated::style_keys;

        [[nodiscard]] std::string_view narrow(const std::u8string_view text) noexcept
        {
            return { reinterpret_cast<const char*>(text.data()), text.size() };
        }

        // 스타일 키 표의 이름 목록을 그대로 옮긴다. 역할·굵기·줄 나눔 정책 이름의 목록이 한 곳이다 (cmake/style_keys.cmake).
        template <class enum_type, std::size_t count>
        [[nodiscard]] constexpr std::array<ui_markup_choice, count> choices_of(const keys::enum_name<enum_type> (&names)[count]) noexcept
        {
            std::array<ui_markup_choice, count> choices {};
            for (std::size_t index { 0 }; index < count; ++index)
                choices[index] = { names[index].name, static_cast<int>(names[index].value) };
            return choices;
        }

        constexpr auto palette_role_choices { choices_of(keys::palette_role_names) };
        constexpr auto font_weight_choices { choices_of(keys::font_weight_names) };
        constexpr auto line_break_policy_choices { choices_of(keys::line_break_policy_names) };

        constexpr ui_markup_choice stack_direction_choices[] {
            { "column", static_cast<int>(stack_direction::column) },
            { "row", static_cast<int>(stack_direction::row) },
        };
        constexpr ui_markup_choice stack_alignment_choices[] {
            { "start", static_cast<int>(stack_alignment::start) },
            { "center", static_cast<int>(stack_alignment::center) },
            { "end", static_cast<int>(stack_alignment::end) },
            { "stretch", static_cast<int>(stack_alignment::stretch) },
        };
        constexpr ui_markup_choice stack_main_alignment_choices[] {
            { "start", static_cast<int>(stack_main_alignment::start) },
            { "center", static_cast<int>(stack_main_alignment::center) },
            { "end", static_cast<int>(stack_main_alignment::end) },
        };
        constexpr ui_markup_choice font_slant_choices[] {
            { "upright", static_cast<int>(font_slant::upright) },
            { "italic", static_cast<int>(font_slant::italic) },
        };
        constexpr ui_markup_choice label_color_choices[] {
            { "primary", static_cast<int>(label_color_role::primary) },
            { "dim", static_cast<int>(label_color_role::dim) },
            { "error", static_cast<int>(label_color_role::error) },
        };
        constexpr ui_markup_choice label_background_choices[] {
            { "none", static_cast<int>(label_background_role::none) },
            { "notice", static_cast<int>(label_background_role::notice) },
            { "raised", static_cast<int>(label_background_role::raised) },
        };
        constexpr ui_markup_choice text_align_choices[] {
            { "start", static_cast<int>(text_align::start) },
            { "center", static_cast<int>(text_align::center) },
            { "end", static_cast<int>(text_align::end) },
        };
        constexpr ui_markup_choice text_elide_choices[] {
            { "end", static_cast<int>(text_elide::end) },
            { "middle", static_cast<int>(text_elide::middle) },
            { "start", static_cast<int>(text_elide::start) },
        };
        constexpr ui_markup_choice text_button_visual_choices[] {
            { "normal", static_cast<int>(text_button_visual::normal) },
            { "accent", static_cast<int>(text_button_visual::accent) },
            { "link", static_cast<int>(text_button_visual::link) },
            { "danger", static_cast<int>(text_button_visual::danger) },
        };
        constexpr ui_markup_choice check_style_choices[] {
            { "checkbox", static_cast<int>(check_style::checkbox) },
            { "radio", static_cast<int>(check_style::radio) },
            { "toggleSwitch", static_cast<int>(check_style::toggle_switch) },
            { "toggleButton", static_cast<int>(check_style::toggle_button) },
            { "toggleChip", static_cast<int>(check_style::toggle_chip) },
        };

        template <class>
        struct member_traits;

        template <class owner_type, class field_type>
        struct member_traits<field_type owner_type::*>
        {
            using owner = owner_type;
            using field = field_type;
        };

        template <class field_type>
        inline constexpr bool is_optional { false };

        template <class field_type>
        inline constexpr bool is_optional<std::optional<field_type>> { true };

        template <class field_type>
        [[nodiscard]] constexpr ui_markup_value_type value_type_of() noexcept
        {
            if constexpr (is_optional<field_type>)
                return value_type_of<typename field_type::value_type>();
            else if constexpr (std::is_same_v<field_type, bool>)
                return ui_markup_value_type::boolean;
            else if constexpr (std::is_same_v<field_type, std::u8string>)
                return ui_markup_value_type::text;
            else if constexpr (std::is_enum_v<field_type>)
                return ui_markup_value_type::choice;
            else
                return ui_markup_value_type::number;
        }

        template <class field_type>
        [[nodiscard]] field_type converted(const ui_markup_literal& literal)
        {
            if constexpr (is_optional<field_type>)
                return field_type { converted<typename field_type::value_type>(literal) };
            else if constexpr (std::is_same_v<field_type, bool>)
                return std::get<bool>(literal);
            else if constexpr (std::is_same_v<field_type, std::u8string>)
                return std::get<std::u8string>(literal);
            else if constexpr (std::is_enum_v<field_type>)
                return static_cast<field_type>(std::get<int>(literal));
            else
                return static_cast<field_type>(std::get<double>(literal));
        }

        // 행이 고치는 자리다. 자리 속성은 stack_item, 공통 속성은 공통 칸, 나머지는 그 요소의 config다.
        template <class owner_type>
        [[nodiscard]] owner_type& target_of(ui_markup_values& values)
        {
            if constexpr (std::is_same_v<owner_type, stack_item>)
                return values.item;
            else if constexpr (std::is_same_v<owner_type, ui_markup_common>)
                return values.common;
            else
                return std::get<owner_type>(values.config);
        }

        template <auto field>
        void apply_field(ui_markup_values& values, const ui_markup_literal& literal)
        {
            using traits = member_traits<decltype(field)>;
            target_of<typename traits::owner>(values).*field = converted<typename traits::field>(literal);
        }

        template <auto field>
        [[nodiscard]] constexpr ui_markup_row field_row(const std::string_view name, const ui_markup_number_rule rule = ui_markup_number_rule::non_negative) noexcept
        {
            using traits = member_traits<decltype(field)>;
            return { .name = name, .type = value_type_of<typename traits::field>(), .rule = rule, .apply = &apply_field<field> };
        }

        template <auto field>
        [[nodiscard]] constexpr ui_markup_row choice_row(const std::string_view name, const std::span<const ui_markup_choice> choices) noexcept
        {
            return { .name = name, .type = ui_markup_value_type::choice, .choices = choices, .apply = &apply_field<field> };
        }

        void apply_stack_padding(ui_markup_values& values, const ui_markup_literal& literal)
        {
            std::get<stack_config>(values.config).padding = edge_insets::all(static_cast<float>(std::get<double>(literal)));
        }

        template <auto side>
        void apply_stack_padding_side(ui_markup_values& values, const ui_markup_literal& literal)
        {
            std::get<stack_config>(values.config).padding.*side = static_cast<float>(std::get<double>(literal));
        }

        // 역할을 고르는 선택자를 건다. 구체 색을 캡처하지 않아 테마와 고대비가 그대로 따라온다.
        void apply_panel_background(ui_markup_values& values, const ui_markup_literal& literal)
        {
            const palette_role role { static_cast<palette_role>(std::get<int>(literal)) };
            std::get<panel_config>(values.config).background = [role](const ui_color_palette& palette) {
                return palette_color(palette, role);
            };
        }

        [[nodiscard]] constexpr ui_markup_row number_row(const std::string_view name, void (*apply)(ui_markup_values&, const ui_markup_literal&)) noexcept
        {
            return { .name = name, .type = ui_markup_value_type::number, .apply = apply };
        }

        template <std::size_t first_count, std::size_t second_count>
        [[nodiscard]] constexpr std::array<ui_markup_row, first_count + second_count> joined(const ui_markup_row (&first)[first_count], const ui_markup_row (&second)[second_count]) noexcept
        {
            std::array<ui_markup_row, first_count + second_count> rows {};
            for (std::size_t index { 0 }; index < first_count; ++index)
                rows[index] = first[index];
            for (std::size_t index { 0 }; index < second_count; ++index)
                rows[first_count + index] = second[index];
            return rows;
        }

        constexpr ui_markup_row common_rows[] {
            field_row<&ui_markup_common::enabled>("enabled"),
            field_row<&ui_markup_common::visible>("visible"),
            field_row<&ui_markup_common::tooltip>("tooltip"),
            field_row<&ui_markup_common::access_name>("accessName"),
        };
        // check의 tooltip은 config 칸이라 공통 칸에서 뺀다.
        constexpr ui_markup_row check_common_rows[] {
            field_row<&ui_markup_common::enabled>("enabled"),
            field_row<&ui_markup_common::visible>("visible"),
            field_row<&ui_markup_common::access_name>("accessName"),
        };

        constexpr ui_markup_row stack_own_rows[] {
            choice_row<&stack_config::direction>("direction", stack_direction_choices),
            field_row<&stack_config::spacing>("spacing"),
            // 한 변이 네 변을 이긴다. 행 순서대로 걸기 때문이다 (`ui_markup_node::attributes`).
            number_row("padding", &apply_stack_padding),
            number_row("paddingLeft", &apply_stack_padding_side<&edge_insets::left>),
            number_row("paddingTop", &apply_stack_padding_side<&edge_insets::top>),
            number_row("paddingRight", &apply_stack_padding_side<&edge_insets::right>),
            number_row("paddingBottom", &apply_stack_padding_side<&edge_insets::bottom>),
            choice_row<&stack_config::cross_alignment>("crossAlignment", stack_alignment_choices),
            choice_row<&stack_config::main_alignment>("mainAlignment", stack_main_alignment_choices),
        };
        constexpr ui_markup_row wrap_own_rows[] {
            field_row<&wrap_config::item_width>("itemWidth"),
            field_row<&wrap_config::item_height>("itemHeight"),
            field_row<&wrap_config::spacing>("spacing"),
            field_row<&wrap_config::line_spacing>("lineSpacing"),
        };
        constexpr ui_markup_row panel_own_rows[] {
            { .name = "background", .type = ui_markup_value_type::choice, .choices = palette_role_choices, .apply = &apply_panel_background },
            field_row<&panel_config::corner_radius>("cornerRadius"),
            field_row<&panel_config::shadow>("shadow", ui_markup_number_rule::unit),
        };
        constexpr ui_markup_row label_own_rows[] {
            field_row<&label_config::text>("text"),
            field_row<&label_config::font_size>("fontSize", ui_markup_number_rule::positive),
            // 필드 이름은 `weight`지만 stack 안의 자리 속성 `weight`(유연 항목)와 겹쳐 `fontWeight`로 쓴다 (`fontSize`의 짝).
            choice_row<&label_config::weight>("fontWeight", font_weight_choices),
            choice_row<&label_config::slant>("slant", font_slant_choices),
            choice_row<&label_config::color>("color", label_color_choices),
            choice_row<&label_config::background>("background", label_background_choices),
            field_row<&label_config::padding>("padding"),
            choice_row<&label_config::align>("align", text_align_choices),
            choice_row<&label_config::elide>("elide", text_elide_choices),
            field_row<&label_config::wrap>("wrap"),
            choice_row<&label_config::line_break>("lineBreak", line_break_policy_choices),
            field_row<&label_config::max_lines>("maxLines", ui_markup_number_rule::count),
        };
        constexpr ui_markup_row text_button_own_rows[] {
            field_row<&text_button_config::text>("text"),
            choice_row<&text_button_config::visual>("visual", text_button_visual_choices),
            field_row<&text_button_config::default_button>("defaultButton"),
            field_row<&text_button_config::selected>("selected"),
        };
        constexpr ui_markup_row check_own_rows[] {
            choice_row<&check_config::style>("style", check_style_choices),
            field_row<&check_config::label>("label"),
            field_row<&check_config::checked>("checked"),
            field_row<&check_config::mixed>("mixed"),
            field_row<&check_config::tooltip>("tooltip"),
        };
        constexpr ui_markup_row progress_own_rows[] {
            field_row<&progress_config::value>("value", ui_markup_number_rule::unit),
            field_row<&progress_config::thickness>("thickness", ui_markup_number_rule::positive),
        };

        constexpr auto stack_rows { joined(stack_own_rows, common_rows) };
        constexpr auto wrap_rows { joined(wrap_own_rows, common_rows) };
        constexpr auto panel_rows { joined(panel_own_rows, common_rows) };
        constexpr auto label_rows { joined(label_own_rows, common_rows) };
        constexpr auto text_button_rows { joined(text_button_own_rows, common_rows) };
        constexpr auto check_rows { joined(check_own_rows, check_common_rows) };
        constexpr auto progress_rows { joined(progress_own_rows, common_rows) };

        // stack 안의 자리다 (`stack_item` 필드 이름 그대로).
        constexpr ui_markup_row item_rows[] {
            field_row<&stack_item::length>("length"),
            field_row<&stack_item::weight>("weight", ui_markup_number_rule::positive),
            field_row<&stack_item::cross_length>("crossLength"),
            field_row<&stack_item::minimum>("minimum"),
            field_row<&stack_item::maximum>("maximum"),
            field_row<&stack_item::fit>("fit"),
        };
        // gap은 `add_gap`·`add_flexible_gap`이라 길이 하나뿐이다.
        constexpr ui_markup_row gap_rows[] {
            field_row<&stack_item::length>("length"),
            field_row<&stack_item::weight>("weight", ui_markup_number_rule::positive),
        };

        struct tag_entry
        {
            std::u8string_view name {};
            ui_markup_tag tag { ui_markup_tag::stack };
        };

        constexpr tag_entry tag_entries[] {
            { u8"stack", ui_markup_tag::stack },
            { u8"wrap", ui_markup_tag::wrap },
            { u8"panel", ui_markup_tag::panel },
            { u8"label", ui_markup_tag::label },
            { u8"textButton", ui_markup_tag::text_button },
            { u8"check", ui_markup_tag::check },
            { u8"progress", ui_markup_tag::progress },
            { u8"gap", ui_markup_tag::gap },
            { u8"slot", ui_markup_tag::slot },
            { u8"for", ui_markup_tag::for_each },
            { u8"if", ui_markup_tag::if_true },
            { u8"unless", ui_markup_tag::unless_true },
        };

        [[nodiscard]] bool is_structural(const ui_markup_tag tag) noexcept
        {
            return tag == ui_markup_tag::for_each || tag == ui_markup_tag::if_true || tag == ui_markup_tag::unless_true;
        }

        // config를 가진 어휘 요소다 (gap·slot·구조 요소가 아니다). 뿌리가 되고 id·공통 속성을 받는다.
        [[nodiscard]] bool is_element(const ui_markup_tag tag) noexcept
        {
            return is_structural(tag) == false && tag != ui_markup_tag::gap && tag != ui_markup_tag::slot;
        }

        // 원하는 크기를 답하는 요소다 (`ui_element::preferred_size`). 이 요소만 자리 없이 맞춤으로 담는다.
        [[nodiscard]] bool answers_size(const ui_markup_tag tag) noexcept
        {
            return tag == ui_markup_tag::stack || tag == ui_markup_tag::label || tag == ui_markup_tag::text_button || tag == ui_markup_tag::check;
        }

        // `[A-Za-z_][A-Za-z0-9_]*`다 (값 이름과 반복 변수).
        [[nodiscard]] bool is_identifier(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return false;
            for (std::size_t index { 0 }; index < text.size(); ++index)
            {
                const char8_t c { text[index] };
                const bool letter { (c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z') || c == u8'_' };
                if (letter == false && (index == 0 || c < u8'0' || c > u8'9'))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool is_id_char(const char8_t c) noexcept
        {
            return (c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z') || (c >= u8'0' && c <= u8'9') || c == u8'_' || c == u8'.' || c == u8'-';
        }

        constexpr std::size_t name_limit { 64 };

        // `id`다. `[A-Za-z0-9_.-]`로 1~64바이트다. `/`와 U+001F를 쓰지 못해 펼칠 때 잇는 owner와 겹치지 않는다.
        [[nodiscard]] bool is_id(const std::u8string_view text) noexcept
        {
            return text.empty() == false && text.size() <= name_limit && std::all_of(text.begin(), text.end(), is_id_char);
        }

        // 사건·kind·slot의 이름이다. `[A-Za-z_][A-Za-z0-9_.-]*`로 64바이트까지다.
        [[nodiscard]] bool is_name(const std::u8string_view text) noexcept
        {
            if (text.empty() || text.size() > name_limit || (text[0] >= u8'0' && text[0] <= u8'9') || text[0] == u8'.' || text[0] == u8'-')
                return false;
            return std::all_of(text.begin(), text.end(), is_id_char);
        }

        [[nodiscard]] double power_of_ten(const long long exponent) noexcept
        {
            // 22까지는 double이 정확히 담는다. 그보다 크면 어림이고, 넘치면 무한이라 범위에서 막힌다.
            constexpr double exact[] {
                1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
            };
            if (exponent < static_cast<long long>(std::size(exact)))
                return exact[exponent];
            return std::pow(10.0, static_cast<double>(exponent));
        }

        // `[+-]?(숫자+(.숫자*)?|.숫자+)([eE][+-]?숫자+)?`를 읽는다. 로캘을 보지 않는다.
        //  - Android NDK의 libc++에는 실수 `from_chars`가 없고 `strtod`는 소수점이 로캘을 따른다. 유효 19자리까지 정수로 모으고
        //    10의 거듭제곱으로 맞춘다. 화면 치수에는 충분한 정밀도다.
        [[nodiscard]] std::optional<double> parse_number(const std::u8string_view text) noexcept
        {
            std::size_t at { 0 };
            bool negative { false };
            if (at < text.size() && (text[at] == u8'+' || text[at] == u8'-'))
            {
                negative = text[at] == u8'-';
                ++at;
            }
            std::uint64_t mantissa { 0 };
            int kept { 0 };
            long long exponent { 0 };
            std::size_t digits { 0 };
            const auto take = [&](const char8_t c, const bool fraction) {
                ++digits;
                if (mantissa == 0 && c == u8'0')
                {
                    if (fraction)
                        --exponent;
                    return;
                }
                if (kept < 19)
                {
                    mantissa = mantissa * 10u + static_cast<std::uint64_t>(c - u8'0');
                    ++kept;
                    if (fraction)
                        --exponent;
                }
                else if (fraction == false)
                {
                    ++exponent;
                }
            };
            while (at < text.size() && text[at] >= u8'0' && text[at] <= u8'9')
                take(text[at++], false);
            if (at < text.size() && text[at] == u8'.')
            {
                ++at;
                while (at < text.size() && text[at] >= u8'0' && text[at] <= u8'9')
                    take(text[at++], true);
            }
            if (digits == 0)
                return std::nullopt;
            if (at < text.size() && (text[at] == u8'e' || text[at] == u8'E'))
            {
                ++at;
                bool negative_exponent { false };
                if (at < text.size() && (text[at] == u8'+' || text[at] == u8'-'))
                {
                    negative_exponent = text[at] == u8'-';
                    ++at;
                }
                long long written { 0 };
                std::size_t exponent_digits { 0 };
                while (at < text.size() && text[at] >= u8'0' && text[at] <= u8'9')
                {
                    // 넘치는 지수는 어차피 무한이나 0이다. 값을 키우지 않고 자리만 읽는다.
                    if (written < 100000)
                        written = written * 10 + static_cast<long long>(text[at] - u8'0');
                    ++exponent_digits;
                    ++at;
                }
                if (exponent_digits == 0)
                    return std::nullopt;
                exponent += negative_exponent ? -written : written;
            }
            if (at != text.size())
                return std::nullopt;
            double value { static_cast<double>(mantissa) };
            if (mantissa != 0 && exponent > 0)
                value *= power_of_ten(exponent);
            else if (mantissa != 0 && exponent < 0)
                value /= power_of_ten(-exponent);
            return negative ? -value : value;
        }

        // 수가 행의 범위에 드는가다. 음수는 `-0`까지 막는다 (스타일 읽기와 같다).
        [[nodiscard]] bool fits(const double value, const ui_markup_number_rule rule) noexcept
        {
            if (std::isfinite(value) == false || std::fabs(value) > static_cast<double>(std::numeric_limits<float>::max()))
                return false;
            switch (rule)
            {
            case ui_markup_number_rule::non_negative:
                return std::signbit(value) == false;
            case ui_markup_number_rule::positive:
                return value > 0.0;
            case ui_markup_number_rule::unit:
                return std::signbit(value) == false && value <= 1.0;
            case ui_markup_number_rule::count:
                return std::signbit(value) == false && value == std::floor(value) && value <= 4294967295.0;
            }
            return false;
        }

        // `{…}` 안의 이름이다. 점이 있으면 앞이 열린 반복 변수여야 한다. `:`는 앞으로의 접두(마크업 후속)를 위해 비워 둔다.
        [[nodiscard]] std::optional<ui_markup_reference> parse_reference(const std::u8string_view text, const std::span<const std::u8string> variables)
        {
            const std::size_t dot { text.find(u8'.') };
            if (dot == std::u8string_view::npos)
            {
                if (is_identifier(text) == false)
                    return std::nullopt;
                return ui_markup_reference { .name = std::u8string { text } };
            }
            const std::u8string_view variable { text.substr(0, dot) };
            const std::u8string_view name { text.substr(dot + 1) };
            if (is_identifier(variable) == false || is_identifier(name) == false)
                return std::nullopt;
            if (std::find(variables.begin(), variables.end(), variable) == variables.end())
                return std::nullopt;
            return ui_markup_reference { .variable = std::u8string { variable }, .name = std::u8string { name } };
        }

        // 속성 값을 조각으로 가른다. `{{`·`}}`는 글자, `{이름}`은 이름이다. 꼴이 틀리면 비어 있다.
        [[nodiscard]] std::optional<std::vector<ui_markup_piece>> parse_pieces(const std::u8string_view text, const std::span<const std::u8string> variables)
        {
            std::vector<ui_markup_piece> pieces {};
            std::u8string literal {};
            const auto flush = [&pieces, &literal] {
                if (literal.empty())
                    return;
                pieces.push_back({ .text = std::move(literal) });
                literal.clear();
            };
            for (std::size_t at { 0 }; at < text.size();)
            {
                const char8_t c { text[at] };
                const bool doubled { at + 1 < text.size() && text[at + 1] == c };
                if ((c == u8'{' || c == u8'}') && doubled)
                {
                    literal.push_back(c);
                    at += 2;
                    continue;
                }
                if (c == u8'}')
                    return std::nullopt;
                if (c != u8'{')
                {
                    literal.push_back(c);
                    ++at;
                    continue;
                }
                const std::size_t close { text.find(u8'}', at + 1) };
                if (close == std::u8string_view::npos)
                    return std::nullopt;
                std::optional<ui_markup_reference> reference { parse_reference(text.substr(at + 1, close - at - 1), variables) };
                if (reference.has_value() == false)
                    return std::nullopt;
                flush();
                pieces.push_back({ .reference = std::move(reference) });
                at = close + 1;
            }
            flush();
            return pieces;
        }

        // 속성 값을 그 형으로 읽는다. 꼴이 틀리면 비어 있다.
        //  - 이름(enum·역할)은 적은 글자뿐이다. 글은 적은 글과 이름을 섞고, 수·참거짓은 값 전체가 이름 하나일 때만 바인딩이다.
        [[nodiscard]] std::optional<ui_markup_attribute> read_value(
            const ui_markup_value_type type, const ui_markup_number_rule rule, const std::span<const ui_markup_choice> choices
            , const ui_markup_xml_attribute& source, const std::span<const std::u8string> variables)
        {
            ui_markup_attribute attribute { .at = source.at };
            if (type == ui_markup_value_type::choice)
            {
                for (const ui_markup_choice& choice : choices)
                {
                    if (choice.name == narrow(source.value))
                    {
                        attribute.literal = choice.value;
                        return attribute;
                    }
                }
                return std::nullopt;
            }
            std::optional<std::vector<ui_markup_piece>> pieces { parse_pieces(source.value, variables) };
            if (pieces.has_value() == false)
                return std::nullopt;
            const bool referenced { std::any_of(pieces->begin(), pieces->end(), [](const ui_markup_piece& piece) { return piece.reference.has_value(); }) };
            if (type == ui_markup_value_type::text)
            {
                if (referenced)
                    attribute.pieces = std::move(*pieces);
                else
                    attribute.literal = pieces->empty() ? std::u8string {} : std::move(pieces->front().text);
                return attribute;
            }
            if (referenced)
            {
                if (pieces->size() != 1)
                    return std::nullopt;
                attribute.pieces = std::move(*pieces);
                return attribute;
            }
            if (type == ui_markup_value_type::boolean)
            {
                if (source.value != u8"true" && source.value != u8"false")
                    return std::nullopt;
                attribute.literal = source.value == u8"true";
                return attribute;
            }
            const std::optional<double> number { parse_number(source.value) };
            if (number.has_value() == false || fits(*number, rule) == false)
                return std::nullopt;
            attribute.literal = *number;
            return attribute;
        }

        // XML의 공백(공백·탭·CR·LF)을 앞뒤에서 뗀 글이다.
        [[nodiscard]] std::u8string_view trimmed(const std::u8string_view text) noexcept
        {
            constexpr std::u8string_view spaces { u8" \t\r\n" };
            const std::size_t first { text.find_first_not_of(spaces) };
            if (first == std::u8string_view::npos)
                return {};
            return text.substr(first, text.find_last_not_of(spaces) - first + 1);
        }

        [[nodiscard]] const ui_markup_row* find_row(const std::span<const ui_markup_row> rows, const std::u8string_view name) noexcept
        {
            for (const ui_markup_row& row : rows)
            {
                if (row.name == narrow(name))
                    return &row;
            }
            return nullptr;
        }

        // 읽은 나무를 어휘로 확인한다. 오류는 끝까지 모으고 마지막에 글의 자리 순으로 늘어놓는다.
        class vocabulary_checker
        {
        public:
            vocabulary_checker(const std::span<const ui_markup_xml_node> xml, const ui_markup_limits& limits)
                : xml_ { xml }
                , limits_ { limits }
            {
            }

            [[nodiscard]] ui_markup_check_result run()
            {
                const ui_markup_xml_node& root { xml_.front() };
                const std::optional<ui_markup_tag> tag { ui_markup_tag_of(root.name) };
                // 뿌리는 config를 가진 요소다. 구조 요소·gap·slot은 부모의 자리에 들 것이라 뿌리가 되지 못한다.
                if (tag.has_value() == false)
                    error(ui_markup_error_kind::unknown_element, root.at, root.name, root.name, {});
                else if (is_element(*tag) == false)
                    error(ui_markup_error_kind::invalid_child, root.at, {}, root.name, {});
                else
                    walk(root);
                if (errors_.empty() == false)
                {
                    std::stable_sort(errors_.begin(), errors_.end(), [](const ui_markup_error& left, const ui_markup_error& right) {
                        return left.offset < right.offset;
                    });
                    return { .errors = std::move(errors_) };
                }
                auto document { std::make_shared<ui_markup_document>() };
                document->nodes = std::move(nodes_);
                document->limits = limits_;
                return { .document = std::move(document) };
            }

        private:
            void error(const ui_markup_error_kind kind, const ui_markup_position& at, std::u8string path, const std::u8string_view name, const std::u8string_view value)
            {
                errors_.push_back(ui_markup_error_at(kind, at, std::move(path), name, value));
            }

            // 그 노드의 자리다. 오류가 날 때만 짓는다. 요소마다 자리 글을 쥐면 깊은 나무에서 깊이의 제곱이 든다.
            //  - 한 노드의 오류는 몰려 나므로 마지막 하나만 쥔다.
            [[nodiscard]] std::u8string path_of(const std::size_t index)
            {
                if (index != cached_node_)
                {
                    cached_path_ = ui_markup_path(nodes_, index);
                    cached_node_ = index;
                }
                return cached_path_;
            }

            // 들어간 요소 하나다. 자식을 차례로 돌며 받을 수 있는지 본다.
            struct frame
            {
                std::size_t source { 0 };
                std::size_t node { 0 };
                // 자식이 놓이는 컨테이너다. 구조 요소는 제 컨테이너를 물려준다.
                std::optional<ui_markup_tag> container {};
                // 같은 이름의 형제 수와 지금까지 센 수다. 형제를 다 센 뒤라 첫째도 `[1]`이다.
                std::map<std::u8string_view, std::size_t> totals {};
                std::map<std::u8string_view, std::size_t> counts {};
                std::size_t next { 0 };
                std::size_t panel_content { 0 };
                bool loop { false };
            };

            // 재귀하지 않고 쌓은 frame으로 돈다. 앱이 `max_depth`를 크게 올려도 호출 깊이가 글을 따라 깊어지지 않는다.
            void walk(const ui_markup_xml_node& root)
            {
                std::vector<frame> frames {};
                enter(frames, 0, std::nullopt, ui_markup_no_parent, root.name);
                while (frames.empty() == false)
                {
                    frame& top { frames.back() };
                    const ui_markup_xml_node& source { xml_[top.source] };
                    if (top.next == source.children.size())
                    {
                        if (top.loop)
                            variables_.pop_back();
                        frames.pop_back();
                        continue;
                    }
                    const ui_markup_xml_node& child { xml_[source.children[top.next]] };
                    const std::size_t child_index { source.children[top.next] };
                    ++top.next;
                    if (child.is_text)
                    {
                        // 1단계에는 글을 받는 요소가 없다 (서식 글은 2단계). 오류의 value는 앞뒤 공백을 뗀 글이다.
                        error(ui_markup_error_kind::invalid_child, child.at, path_of(top.node), {}, trimmed(child.text));
                        continue;
                    }
                    std::u8string step { child.name };
                    const std::size_t number { ++top.counts[child.name] };
                    if (top.totals[child.name] > 1)
                        step += u8"[" + ui_markup_decimal(number) + u8"]";
                    const std::optional<ui_markup_tag> child_tag { ui_markup_tag_of(child.name) };
                    if (child_tag.has_value() == false)
                    {
                        error(ui_markup_error_kind::unknown_element, child.at, path_of(top.node) + u8"/" + step, child.name, {});
                        continue;
                    }
                    if (accepts(nodes_[top.node].tag, top.container, *child_tag, top.panel_content) == false)
                    {
                        error(ui_markup_error_kind::invalid_child, child.at, path_of(top.node), child.name, {});
                        continue;
                    }
                    // `enter`가 frame을 더하면 `top`이 무효가 되므로 값을 먼저 꺼낸다.
                    const std::optional<ui_markup_tag> container { top.container };
                    const std::size_t parent { top.node };
                    enter(frames, child_index, container, parent, std::move(step));
                }
            }

            // 노드를 짓고 속성을 읽은 뒤 자식을 돌 frame을 쌓는다. `container`는 이 노드가 놓인 자리의 컨테이너다 (뿌리면 비어 있다).
            void enter(std::vector<frame>& frames, const std::size_t source_index, const std::optional<ui_markup_tag> container, const std::size_t parent, std::u8string step)
            {
                const ui_markup_xml_node& source { xml_[source_index] };
                const ui_markup_tag tag { *ui_markup_tag_of(source.name) };
                const std::size_t index { nodes_.size() };
                nodes_.push_back({ .tag = tag, .at = source.at, .parent = parent, .step = std::move(step) });
                if (parent != ui_markup_no_parent)
                    nodes_[parent].children.push_back(index);
                read_attributes(index, source, container);

                frame entered { .source = source_index, .node = index, .container = is_structural(tag) ? container : std::optional<ui_markup_tag> { tag } };
                for (const std::size_t child : source.children)
                {
                    if (xml_[child].is_text == false)
                        ++entered.totals[xml_[child].name];
                }
                entered.loop = tag == ui_markup_tag::for_each && nodes_[index].as.empty() == false;
                if (entered.loop)
                    variables_.push_back(nodes_[index].as);
                frames.push_back(std::move(entered));
            }

            // `parent` 안에 `child`를 둘 수 있는가다. `container`는 구조 요소를 건너뛴 가장 가까운 요소다.
            //  - panel은 내용 하나뿐이다. 구조 요소는 펼친 수가 frame마다 달라 panel의 자식이 되지 못한다.
            //  - gap은 stack 안에서만, 구조 요소는 자식을 여럿 받는 stack·wrap 안에서만 된다.
            [[nodiscard]] static bool accepts(const ui_markup_tag parent, const std::optional<ui_markup_tag> container, const ui_markup_tag child, std::size_t& panel_content) noexcept
            {
                if (parent == ui_markup_tag::panel)
                    return (is_element(child) || child == ui_markup_tag::slot) && panel_content++ == 0;
                if (parent != ui_markup_tag::stack && parent != ui_markup_tag::wrap && is_structural(parent) == false)
                    return false;
                if (child == ui_markup_tag::gap)
                    return container == ui_markup_tag::stack;
                if (is_structural(child))
                    return container == ui_markup_tag::stack || container == ui_markup_tag::wrap;
                return true;
            }

            [[nodiscard]] std::span<const std::u8string> scope() const noexcept
            {
                return variables_;
            }

            // 요소가 행 밖에서 따로 쥐는 속성이다. 그 요소의 것이면 읽고 참을 돌려준다.
            [[nodiscard]] bool read_special(const std::size_t index, const ui_markup_xml_attribute& source)
            {
                ui_markup_node& node { nodes_[index] };
                const ui_markup_tag tag { node.tag };
                const std::u8string_view name { source.name };
                const auto invalid = [&] {
                    error(ui_markup_error_kind::invalid_value, source.at, path_of(index), source.name, source.value);
                };
                const auto text_value = [&](std::optional<ui_markup_attribute>& target, const std::span<const std::u8string> variables) {
                    target = read_value(ui_markup_value_type::text, {}, {}, source, variables);
                    if (target.has_value() == false)
                        invalid();
                };
                if (name == u8"id" && is_element(tag))
                {
                    if (is_id(source.value) == false)
                    {
                        invalid();
                        return true;
                    }
                    node.id = source.value;
                    node.id_at = source.at;
                    // 같은 id를 글에 두 번 적으면 반복 안팎을 가리지 않고 막는다. 반복은 한 번 적은 id를 항목마다 key로 가른다.
                    if (ids_.insert(source.value).second == false)
                        error(ui_markup_error_kind::duplicate_id, source.at, path_of(index), source.value, source.value);
                    return true;
                }
                if (name == u8"kind" && is_element(tag) && tag != ui_markup_tag::check)
                {
                    if (is_name(source.value) == false)
                        invalid();
                    node.kind = source.value;
                    node.kind_at = source.at;
                    return true;
                }
                const bool acts { tag == ui_markup_tag::text_button || tag == ui_markup_tag::check };
                if (name == u8"action" && acts)
                {
                    if (is_name(source.value) == false)
                        invalid();
                    node.action = source.value;
                    node.action_at = source.at;
                    return true;
                }
                if (name == u8"argument" && acts)
                {
                    text_value(node.argument, scope());
                    return true;
                }
                if (name == u8"name" && tag == ui_markup_tag::slot)
                {
                    if (is_name(source.value) == false)
                        invalid();
                    node.slot_name = source.value;
                    node.slot_name_at = source.at;
                    return true;
                }
                if (tag == ui_markup_tag::for_each)
                {
                    if (name == u8"as")
                        return true;
                    if (name == u8"each")
                    {
                        // 목록은 이름 하나다. 적은 글은 목록이 아니다.
                        text_value(node.each, scope());
                        if (node.each.has_value() && (node.each->pieces.size() != 1 || node.each->pieces.front().reference.has_value() == false))
                        {
                            node.each.reset();
                            invalid();
                        }
                        return true;
                    }
                    if (name == u8"key")
                    {
                        // key는 그 반복의 변수를 읽는다 (`{item.key}`).
                        std::vector<std::u8string> variables { variables_ };
                        if (node.as.empty() == false)
                            variables.push_back(node.as);
                        text_value(node.key, variables);
                        return true;
                    }
                    return false;
                }
                if (name == u8"test" && (tag == ui_markup_tag::if_true || tag == ui_markup_tag::unless_true))
                {
                    node.test = read_value(ui_markup_value_type::boolean, {}, {}, source, scope());
                    if (node.test.has_value() == false)
                        invalid();
                    return true;
                }
                return false;
            }

            void read_attributes(const std::size_t index, const ui_markup_xml_node& source, const std::optional<ui_markup_tag> container)
            {
                ui_markup_node& node { nodes_[index] };
                const std::span<const ui_markup_row> rows { ui_markup_rows(node.tag) };
                const bool placed { container == ui_markup_tag::stack && is_structural(node.tag) == false && node.tag != ui_markup_tag::gap };
                // key가 그 반복의 변수를 읽으므로 `as`를 먼저 본다.
                if (node.tag == ui_markup_tag::for_each)
                {
                    for (const ui_markup_xml_attribute& attribute : source.attributes)
                    {
                        if (attribute.name != u8"as")
                            continue;
                        if (is_identifier(attribute.value))
                            node.as = attribute.value;
                        else
                            error(ui_markup_error_kind::invalid_value, attribute.at, path_of(index), attribute.name, attribute.value);
                        break;
                    }
                }
                // 행 순서로 걸기 위해 행의 차례를 함께 쥔다 (요소의 행 다음에 자리 행).
                std::vector<std::pair<std::size_t, ui_markup_attribute>> ranked {};
                // 속성 수는 글이 정한다. 한 요소에 속성을 수십만 적은 글이 제곱 시간을 쓰지 않도록 집합으로 찾는다.
                std::set<std::u8string_view> seen {};
                for (const ui_markup_xml_attribute& attribute : source.attributes)
                {
                    if (seen.insert(attribute.name).second == false)
                    {
                        error(ui_markup_error_kind::duplicate_attribute, attribute.at, path_of(index), attribute.name, attribute.value);
                        continue;
                    }
                    if (read_special(index, attribute))
                        continue;
                    const ui_markup_row* row { find_row(rows, attribute.name) };
                    std::size_t rank { row == nullptr ? 0 : static_cast<std::size_t>(row - rows.data()) };
                    if (row == nullptr && placed)
                    {
                        row = find_row(ui_markup_item_rows(), attribute.name);
                        if (row != nullptr)
                            rank = rows.size() + static_cast<std::size_t>(row - ui_markup_item_rows().data());
                    }
                    if (row == nullptr)
                    {
                        error(ui_markup_error_kind::unknown_attribute, attribute.at, path_of(index), attribute.name, attribute.value);
                        continue;
                    }
                    std::optional<ui_markup_attribute> value { read_value(row->type, row->rule, row->choices, attribute, scope()) };
                    if (value.has_value() == false)
                    {
                        error(ui_markup_error_kind::invalid_value, attribute.at, path_of(index), attribute.name, attribute.value);
                        continue;
                    }
                    value->row = row;
                    ranked.emplace_back(rank, std::move(*value));
                }
                std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                    return left.first < right.first;
                });
                for (auto& [rank, attribute] : ranked)
                    node.attributes.push_back(std::move(attribute));

                require(index, source);
                if (placed || node.tag == ui_markup_tag::gap)
                    place(index, source);
            }

            void require(const std::size_t index, const ui_markup_xml_node& source)
            {
                const ui_markup_node& node { nodes_[index] };
                const auto has = [&source](const std::u8string_view name) {
                    return std::any_of(source.attributes.begin(), source.attributes.end(), [name](const ui_markup_xml_attribute& attribute) {
                        return attribute.name == name;
                    });
                };
                std::vector<std::u8string_view> required {};
                switch (node.tag)
                {
                case ui_markup_tag::text_button:
                case ui_markup_tag::check:
                    // 누르는 요소는 id가 있어야 frame 사이에 초점·눌림이 잇는다.
                    required = { u8"id" };
                    break;
                case ui_markup_tag::slot:
                    required = { u8"name" };
                    break;
                case ui_markup_tag::for_each:
                    required = { u8"each", u8"as", u8"key" };
                    break;
                case ui_markup_tag::if_true:
                case ui_markup_tag::unless_true:
                    required = { u8"test" };
                    break;
                default:
                    break;
                }
                for (const std::u8string_view name : required)
                {
                    if (has(name) == false)
                        error(ui_markup_error_kind::missing_attribute, source.at, path_of(index), name, {});
                }
            }

            // stack 안의 자리를 정한다. 0으로 접혀 보이지 않는 자식을 만들지 않도록 길이가 없는 자리는 막는다.
            void place(const std::size_t index, const ui_markup_xml_node& source)
            {
                ui_markup_node& node { nodes_[index] };
                const std::span<const ui_markup_row> items { node.tag == ui_markup_tag::gap ? ui_markup_rows(ui_markup_tag::gap) : ui_markup_item_rows() };
                const auto find = [&node, items](const std::string_view name) -> const ui_markup_attribute* {
                    for (const ui_markup_attribute& attribute : node.attributes)
                    {
                        const bool item { attribute.row >= items.data() && attribute.row < items.data() + items.size() };
                        if (item && attribute.row->name == name)
                            return &attribute;
                    }
                    return nullptr;
                };
                // 함께 못 쓰는 자리 속성이다. 오류의 value는 적은 글이다.
                const auto reject = [&](const ui_markup_attribute& attribute) {
                    const std::u8string_view name { reinterpret_cast<const char8_t*>(attribute.row->name.data()), attribute.row->name.size() };
                    const auto written { std::find_if(source.attributes.begin(), source.attributes.end(), [name](const ui_markup_xml_attribute& candidate) {
                        return candidate.name == name;
                    })
                    };
                    error(ui_markup_error_kind::invalid_value, attribute.at, path_of(index), name, written == source.attributes.end() ? std::u8string_view {} : std::u8string_view { written->value });
                };
                // 적었지만 값이 틀린 자리 속성은 이미 invalid_value다. 같은 실수에 missing_length를 겹쳐 말하지 않는다.
                const bool written_placement { std::any_of(source.attributes.begin(), source.attributes.end(), [](const ui_markup_xml_attribute& attribute) {
                    return attribute.name == u8"length" || attribute.name == u8"weight" || attribute.name == u8"fit";
                })
                };
                const auto missing = [&] {
                    if (written_placement == false)
                        error(ui_markup_error_kind::missing_length, source.at, path_of(index), source.name, {});
                };
                const ui_markup_attribute* const length { find("length") };
                const ui_markup_attribute* const weight { find("weight") };
                if (length != nullptr && weight != nullptr)
                    reject(*weight);
                if (node.tag == ui_markup_tag::gap)
                {
                    if (weight != nullptr)
                        node.placement = ui_markup_placement::flexible;
                    else if (length != nullptr)
                        node.placement = ui_markup_placement::fixed;
                    else
                        missing();
                    return;
                }
                const ui_markup_attribute* const fit { find("fit") };
                if (fit != nullptr && (length != nullptr || weight != nullptr))
                    reject(*fit);
                // 하한·상한은 유연 항목의 것이다 (stack_item 주석).
                for (const ui_markup_attribute* const bound : { find("minimum"), find("maximum") })
                {
                    if (bound != nullptr && weight == nullptr)
                        reject(*bound);
                }
                if (weight != nullptr)
                {
                    node.placement = ui_markup_placement::flexible;
                    return;
                }
                if (length != nullptr)
                {
                    node.placement = ui_markup_placement::fixed;
                    return;
                }
                const bool fit_written_true { fit != nullptr && (fit->bound() || std::get<bool>(fit->literal)) };
                const bool fit_written_false { fit != nullptr && fit_written_true == false };
                // slot은 앱 요소가 답하는지 읽기가 모르므로 늘 자리를 적는다. 답하지 않는 요소에는 `fit`도 자리가 아니다.
                const bool fits_itself { node.tag == ui_markup_tag::slot ? fit_written_true : answers_size(node.tag) && fit_written_false == false };
                if (fits_itself)
                {
                    node.placement = ui_markup_placement::fit;
                    return;
                }
                // 맞춤이 아닌 `fit`은 적었어도 자리가 아니다.
                if (fit != nullptr || written_placement == false)
                    error(ui_markup_error_kind::missing_length, source.at, path_of(index), source.name, {});
            }

            std::span<const ui_markup_xml_node> xml_ {};
            const ui_markup_limits& limits_;
            std::vector<ui_markup_node> nodes_ {};
            std::vector<ui_markup_error> errors_ {};
            std::vector<std::u8string> variables_ {};
            std::set<std::u8string> ids_ {};
            std::size_t cached_node_ { ui_markup_no_parent };
            std::u8string cached_path_ {};
        };
    }

    std::optional<ui_markup_tag> ui_markup_tag_of(const std::u8string_view name) noexcept
    {
        for (const tag_entry& entry : tag_entries)
        {
            if (entry.name == name)
                return entry.tag;
        }
        return std::nullopt;
    }

    std::u8string_view ui_markup_tag_name(const ui_markup_tag tag) noexcept
    {
        for (const tag_entry& entry : tag_entries)
        {
            if (entry.tag == tag)
                return entry.name;
        }
        return {};
    }

    std::span<const ui_markup_row> ui_markup_rows(const ui_markup_tag tag) noexcept
    {
        switch (tag)
        {
        case ui_markup_tag::stack:
            return stack_rows;
        case ui_markup_tag::wrap:
            return wrap_rows;
        case ui_markup_tag::panel:
            return panel_rows;
        case ui_markup_tag::label:
            return label_rows;
        case ui_markup_tag::text_button:
            return text_button_rows;
        case ui_markup_tag::check:
            return check_rows;
        case ui_markup_tag::progress:
            return progress_rows;
        case ui_markup_tag::gap:
            return gap_rows;
        case ui_markup_tag::slot:
        case ui_markup_tag::for_each:
        case ui_markup_tag::if_true:
        case ui_markup_tag::unless_true:
            return {};
        }
        return {};
    }

    std::span<const ui_markup_row> ui_markup_item_rows() noexcept
    {
        return item_rows;
    }

    std::span<const ui_markup_choice> ui_markup_palette_roles() noexcept
    {
        return palette_role_choices;
    }

    ui_markup_values ui_markup_default_values(const ui_markup_tag tag)
    {
        ui_markup_values values {};
        switch (tag)
        {
        case ui_markup_tag::stack:
            values.config = stack_config {};
            break;
        case ui_markup_tag::wrap:
            values.config = wrap_config {};
            break;
        case ui_markup_tag::panel:
            values.config = panel_config {};
            break;
        case ui_markup_tag::label:
            values.config = label_config {};
            break;
        case ui_markup_tag::text_button:
            values.config = text_button_config {};
            break;
        case ui_markup_tag::check:
            values.config = check_config {};
            break;
        case ui_markup_tag::progress:
            values.config = progress_config {};
            break;
        case ui_markup_tag::gap:
        case ui_markup_tag::slot:
        case ui_markup_tag::for_each:
        case ui_markup_tag::if_true:
        case ui_markup_tag::unless_true:
            break;
        }
        return values;
    }

    ui_markup_values ui_markup_literal_values(const ui_markup_node& node)
    {
        ui_markup_values values { ui_markup_default_values(node.tag) };
        for (const ui_markup_attribute& attribute : node.attributes)
        {
            if (attribute.row != nullptr && attribute.bound() == false)
                attribute.row->apply(values, attribute.literal);
        }
        return values;
    }

    std::u8string ui_markup_path(const std::span<const ui_markup_node> nodes, const std::size_t index)
    {
        std::vector<std::u8string_view> steps {};
        for (std::size_t at { index }; at != ui_markup_no_parent && at < nodes.size(); at = nodes[at].parent)
            steps.push_back(nodes[at].step);
        std::u8string path {};
        for (auto step { steps.rbegin() }; step != steps.rend(); ++step)
        {
            if (path.empty() == false)
                path.push_back(u8'/');
            path += *step;
        }
        return path;
    }

    ui_markup_check_result check_ui_markup_vocabulary(const std::span<const ui_markup_xml_node> nodes, const ui_markup_limits& limits)
    {
        if (nodes.empty())
            return { .errors = { ui_markup_error_at(ui_markup_error_kind::syntax, {}, {}, {}, {}) } };
        return vocabulary_checker { nodes, limits }.run();
    }
}

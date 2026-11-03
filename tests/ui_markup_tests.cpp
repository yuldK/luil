#include "luil/ui/ui_markup.h"

#include "luil/generated/ui_style_keys.h"
#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/label_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/wrap_element.h"
#include "raster_probe.h"
#include "ui/ui_markup_document.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 화면 마크업의 어휘다 (docs/markup-design.md 어휘 절). 읽을 때 이름·enum·수·자리·id 꼴을 모두 확인하고, 적은 값은 C++ 값으로
// 바꿔 둔다. 뒤쪽 절은 펼치기(값 바인딩, 반복과 조건, 사건, id, slot, 오류, 같은 화면 견줌, 견본 픽셀)다
// (docs/markup-binding-design.md).
namespace {
    using luil::ui_markup_error_kind;
    namespace keys = luil::generated::style_keys;

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::string described(const luil::ui_markup_parse_result& result)
    {
        std::string text {};
        for (const luil::ui_markup_error& error : result.errors)
            text += narrow(luil::describe(error)) + "\n";
        return text;
    }

    [[nodiscard]] const luil::ui_markup_document& document_of(const luil::ui_markup_parse_result& result)
    {
        INFO(described(result));
        REQUIRE(result.ok());
        return *result.document;
    }

    [[nodiscard]] luil::ui_markup_error only_error(const std::u8string_view text)
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text) };
        INFO(narrow(text));
        INFO(described(result));
        REQUIRE(result.ok() == false);
        REQUIRE(result.errors.size() == 1);
        return result.errors.front();
    }

    // 막히는 글이고 그 오류 하나의 종류와 이름이다.
    void require_error(const std::u8string_view text, const ui_markup_error_kind kind, const std::string_view name)
    {
        const luil::ui_markup_error error { only_error(text) };
        INFO(narrow(text));
        INFO(narrow(luil::describe(error)));
        REQUIRE(error.kind == kind);
        REQUIRE(narrow(error.name) == name);
    }

    void require_ok(const std::u8string_view text)
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text) };
        INFO(narrow(text));
        INFO(described(result));
        REQUIRE(result.ok());
    }

    // 뿌리 하나의 적은 값이다.
    [[nodiscard]] luil::ui_markup_values root_values(const std::u8string_view text)
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text) };
        INFO(narrow(text));
        return luil::ui_markup_literal_values(document_of(result).nodes.front());
    }

    // 뿌리 하나의 config 사본이다. 임시 값의 안을 가리키는 참조가 남지 않도록 값으로 돌려준다.
    template <class config_type>
    [[nodiscard]] config_type root_config(const std::u8string_view text)
    {
        return std::get<config_type>(root_values(text).config);
    }

    // 뿌리 stack의 첫 자식이다.
    [[nodiscard]] luil::ui_markup_node first_child(const std::u8string_view text)
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text) };
        INFO(narrow(text));
        const luil::ui_markup_document& document { document_of(result) };
        REQUIRE(document.nodes.size() >= 2);
        return document.nodes[1];
    }

    [[nodiscard]] const luil::ui_markup_attribute& attribute_of(const luil::ui_markup_node& node, const std::string_view name)
    {
        const luil::ui_markup_attribute* found { nullptr };
        for (const luil::ui_markup_attribute& attribute : node.attributes)
        {
            if (attribute.row != nullptr && attribute.row->name == name)
                found = &attribute;
        }
        INFO("attribute " << name);
        REQUIRE(found != nullptr);
        return *found;
    }

    [[nodiscard]] luil::ui_markup_piece text_piece(const std::u8string_view text)
    {
        return { .text = std::u8string { text } };
    }

    [[nodiscard]] luil::ui_markup_piece name_piece(const std::u8string_view name, const std::u8string_view variable = {})
    {
        return { .reference = luil::ui_markup_reference { .variable = std::u8string { variable }, .name = std::u8string { name } } };
    }
}

TEST_CASE("Every markup row writes the config field it names", "[ui][markup][vocabulary]")
{
    // 행마다 기본값이 아닌 값을 적고, 같은 값을 C++로 준 config와 견준다. 행이 엉뚱한 필드를 가리키면 여기서 걸린다.
    SECTION("label")
    {
        const luil::ui_markup_values values { root_values(
            u8"<label text=\"t\" fontSize=\"20.5\" fontWeight=\"semibold\" slant=\"italic\" color=\"error\" background=\"raised\" padding=\"3\""
            u8" align=\"center\" elide=\"middle\" wrap=\"true\" lineBreak=\"unicode\" maxLines=\"2\""
            u8" enabled=\"false\" visible=\"false\" tooltip=\"tip\" accessName=\"name\"/>")
        };
        const auto& config { std::get<luil::label_config>(values.config) };
        REQUIRE(narrow(config.text) == "t");
        REQUIRE(config.font_size == 20.5f);
        REQUIRE(config.weight == luil::font_weight::semibold);
        REQUIRE(config.slant == luil::font_slant::italic);
        REQUIRE(config.color == luil::label_color_role::error);
        REQUIRE(config.background == luil::label_background_role::raised);
        REQUIRE(config.padding == 3.0f);
        REQUIRE(config.align == luil::text_align::center);
        REQUIRE(config.elide == luil::text_elide::middle);
        REQUIRE(config.wrap);
        REQUIRE(config.line_break == luil::text::line_break_policy::unicode);
        REQUIRE(config.max_lines == 2);
        REQUIRE(values.common.enabled == false);
        REQUIRE(values.common.visible == false);
        REQUIRE(narrow(values.common.tooltip) == "tip");
        REQUIRE(narrow(values.common.access_name) == "name");
        // 적지 않은 칸은 C++ 기본값이다. lineBreak가 비면 앱 스타일의 정책을 따른다.
        const auto plain { root_config<luil::label_config>(u8"<label/>") };
        REQUIRE(plain.font_size == luil::label_config {}.font_size);
        REQUIRE(plain.line_break.has_value() == false);
        REQUIRE(plain.color == luil::label_color_role::dim);
    }
    SECTION("textButton")
    {
        const luil::ui_markup_values values { root_values(u8"<textButton id=\"b\" text=\"go\" visual=\"danger\" defaultButton=\"true\" selected=\"false\"/>") };
        const auto& config { std::get<luil::text_button_config>(values.config) };
        REQUIRE(narrow(config.text) == "go");
        REQUIRE(config.visual == luil::text_button_visual::danger);
        REQUIRE(config.default_button);
        REQUIRE(config.selected == std::optional<bool> { false });
        REQUIRE(std::get<luil::text_button_config>(root_values(u8"<textButton id=\"b\"/>").config).selected.has_value() == false);
    }
    SECTION("check")
    {
        const luil::ui_markup_values values { root_values(u8"<check id=\"c\" style=\"toggleSwitch\" label=\"on\" checked=\"true\" mixed=\"true\" tooltip=\"tip\" accessName=\"n\"/>") };
        const auto& config { std::get<luil::check_config>(values.config) };
        REQUIRE(config.style == luil::check_style::toggle_switch);
        REQUIRE(narrow(config.label) == "on");
        REQUIRE(config.checked);
        REQUIRE(config.mixed);
        // check의 tooltip은 config 칸이다.
        REQUIRE(narrow(config.tooltip) == "tip");
        REQUIRE(values.common.tooltip.empty());
        REQUIRE(narrow(values.common.access_name) == "n");
    }
    SECTION("progress")
    {
        const auto config { root_config<luil::progress_config>(u8"<progress value=\"0.25\" thickness=\"6\"/>") };
        REQUIRE(config.value == 0.25f);
        REQUIRE(config.thickness == 6.0f);
    }
    SECTION("stack")
    {
        const auto config { root_config<luil::stack_config>(u8"<stack direction=\"row\" spacing=\"4\" crossAlignment=\"center\" mainAlignment=\"end\" padding=\"8\"/>") };
        REQUIRE(config.direction == luil::stack_direction::row);
        REQUIRE(config.spacing == 4.0f);
        REQUIRE(config.cross_alignment == luil::stack_alignment::center);
        REQUIRE(config.main_alignment == luil::stack_main_alignment::end);
        REQUIRE(config.padding == luil::edge_insets::all(8.0f));
        // 한 변은 네 변을 이긴다. 적은 차례와 상관없다.
        const auto sides { root_config<luil::stack_config>(u8"<stack paddingLeft=\"1\" paddingTop=\"2\" padding=\"8\" paddingBottom=\"4\"/>") };
        REQUIRE(sides.padding == luil::edge_insets { 1.0f, 2.0f, 8.0f, 4.0f });
        const auto right { root_config<luil::stack_config>(u8"<stack paddingRight=\"3\"/>") };
        REQUIRE(right.padding == luil::edge_insets { 0.0f, 0.0f, 3.0f, 0.0f });
    }
    SECTION("wrap")
    {
        const auto config { root_config<luil::wrap_config>(u8"<wrap itemWidth=\"40\" itemHeight=\"30\" spacing=\"2\" lineSpacing=\"5\"/>") };
        REQUIRE(config.item_width == 40.0f);
        REQUIRE(config.item_height == 30.0f);
        REQUIRE(config.spacing == 2.0f);
        REQUIRE(config.line_spacing == 5.0f);
    }
    SECTION("panel")
    {
        const auto config { root_config<luil::panel_config>(u8"<panel background=\"noticeBackground\" cornerRadius=\"6\" shadow=\"0.5\"/>") };
        REQUIRE(config.corner_radius == 6.0f);
        REQUIRE(config.shadow == 0.5f);
        // 역할은 선택자로 건다. 그리는 때의 팔레트를 따라 테마와 고대비가 그대로 온다.
        REQUIRE(config.background);
        for (const luil::color_theme theme : { luil::color_theme::light, luil::color_theme::dark })
        {
            const luil::ui_color_palette palette { luil::color_palette_for(theme) };
            REQUIRE(config.background(palette) == palette.notice_background);
        }
        REQUIRE(std::get<luil::panel_config>(root_values(u8"<panel/>").config).background == nullptr);
    }
    SECTION("stack item")
    {
        const luil::ui_markup_node flexible { first_child(u8"<stack><label weight=\"2\" crossLength=\"30\" minimum=\"10\" maximum=\"90\"/></stack>") };
        const luil::ui_markup_values values { luil::ui_markup_literal_values(flexible) };
        REQUIRE(flexible.placement == luil::ui_markup_placement::flexible);
        REQUIRE(values.item.weight == 2.0f);
        REQUIRE(values.item.cross_length == 30.0f);
        REQUIRE(values.item.minimum == 10.0f);
        REQUIRE(values.item.maximum == 90.0f);
        const luil::ui_markup_node fixed { first_child(u8"<stack><panel length=\"24\"/></stack>") };
        REQUIRE(fixed.placement == luil::ui_markup_placement::fixed);
        REQUIRE(luil::ui_markup_literal_values(fixed).item.length == 24.0f);
        const luil::ui_markup_node fit { first_child(u8"<stack><slot name=\"s\" fit=\"true\"/></stack>") };
        REQUIRE(fit.placement == luil::ui_markup_placement::fit);
        REQUIRE(luil::ui_markup_literal_values(fit).item.fit);
        // 원하는 크기를 답하는 요소는 자리를 적지 않으면 맞춤이다.
        REQUIRE(first_child(u8"<stack><label/></stack>").placement == luil::ui_markup_placement::fit);
        // gap은 길이 하나다.
        const luil::ui_markup_node gap { first_child(u8"<stack><gap weight=\"3\"/></stack>") };
        REQUIRE(gap.placement == luil::ui_markup_placement::flexible);
        REQUIRE(luil::ui_markup_literal_values(gap).item.weight == 3.0f);
        REQUIRE(first_child(u8"<stack><gap length=\"5\"/></stack>").placement == luil::ui_markup_placement::fixed);
    }
}

TEST_CASE("Markup panel background names are the style palette roles", "[ui][markup][vocabulary]")
{
    // 역할 이름은 스타일 JSON과 같은 목록이다 (cmake/style_keys.cmake). 키 표의 행과 같은 수·같은 값이다.
    const std::span<const luil::ui_markup_choice> roles { luil::ui_markup_palette_roles() };
    REQUIRE(roles.size() == std::size(keys::palette_role_names));
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    for (std::size_t index { 0 }; index < roles.size(); ++index)
    {
        REQUIRE(roles[index].name == keys::palette_role_names[index].name);
        REQUIRE(roles[index].value == static_cast<int>(keys::palette_role_names[index].value));
        const std::u8string text { u8"<panel background=\"" + std::u8string { roles[index].name.begin(), roles[index].name.end() } + u8"\"/>" };
        const auto config { root_config<luil::panel_config>(text) };
        REQUIRE(config.background(palette) == luil::palette_color(palette, keys::palette_role_names[index].value));
    }
}

TEST_CASE("Markup rejects names and values outside the vocabulary", "[ui][markup][vocabulary]")
{
    require_error(u8"<stak/>", ui_markup_error_kind::unknown_element, "stak");
    require_error(u8"<stack><textInput/></stack>", ui_markup_error_kind::unknown_element, "textInput");
    REQUIRE(narrow(only_error(u8"<stack><textInput/></stack>").path) == "stack/textInput");
    require_error(u8"<label colr=\"primary\"/>", ui_markup_error_kind::unknown_attribute, "colr");
    // C++ 필드 이름 그대로는 받지 않는다 (camelCase).
    require_error(u8"<label font_size=\"12\"/>", ui_markup_error_kind::unknown_attribute, "font_size");
    // 허용 밖 enum 이름과 대소문자다.
    require_error(u8"<stack direction=\"diagonal\"/>", ui_markup_error_kind::invalid_value, "direction");
    require_error(u8"<check id=\"c\" style=\"toggle_switch\"/>", ui_markup_error_kind::invalid_value, "style");
    require_error(u8"<label fontWeight=\"Bold\"/>", ui_markup_error_kind::invalid_value, "fontWeight");
    require_error(u8"<panel background=\"accent\"/>", ui_markup_error_kind::invalid_value, "background");
    // 구체 색은 어느 속성도 받지 않는다. 색은 역할로만 말한다.
    require_error(u8"<label color=\"#ff0000\"/>", ui_markup_error_kind::invalid_value, "color");
    require_error(u8"<panel background=\"#202020\"/>", ui_markup_error_kind::invalid_value, "background");
    // 수가 아니거나 범위 밖이다. 음수는 -0까지 막는다.
    const std::u8string_view numbers[] {
        u8"-1", u8"-0", u8"12px", u8" 12", u8"", u8".", u8"1e", u8"0x10", u8"inf", u8"nan", u8"1,5", u8"1e39", u8"1e999999",
    };
    for (const std::u8string_view number : numbers)
        require_error(u8"<stack spacing=\"" + std::u8string { number } + u8"\"/>", ui_markup_error_kind::invalid_value, "spacing");
    require_error(u8"<label fontSize=\"0\"/>", ui_markup_error_kind::invalid_value, "fontSize");
    require_error(u8"<progress thickness=\"0\"/>", ui_markup_error_kind::invalid_value, "thickness");
    require_error(u8"<progress value=\"1.5\"/>", ui_markup_error_kind::invalid_value, "value");
    require_error(u8"<panel shadow=\"2\"/>", ui_markup_error_kind::invalid_value, "shadow");
    require_error(u8"<label maxLines=\"1.5\"/>", ui_markup_error_kind::invalid_value, "maxLines");
    require_error(u8"<label maxLines=\"1e10\"/>", ui_markup_error_kind::invalid_value, "maxLines");
    require_error(u8"<stack><gap weight=\"0\"/></stack>", ui_markup_error_kind::invalid_value, "weight");
    require_error(u8"<label wrap=\"yes\"/>", ui_markup_error_kind::invalid_value, "wrap");
    require_error(u8"<label wrap=\"True\"/>", ui_markup_error_kind::invalid_value, "wrap");
    // 받는 수의 꼴이다: 부호, 소수점, 지수, 앞의 0.
    require_ok(u8"<stack spacing=\"+1.5e1\" padding=\".5\"><gap length=\"007\"/><gap length=\"2.\"/><gap length=\"1E-2\"/><gap length=\"0\"/></stack>");
    REQUIRE(std::get<luil::stack_config>(root_values(u8"<stack spacing=\"+1.5e1\"/>").config).spacing == 15.0f);
    REQUIRE(std::get<luil::stack_config>(root_values(u8"<stack spacing=\"0.000125e4\"/>").config).spacing == 1.25f);
    REQUIRE(std::get<luil::stack_config>(root_values(u8"<stack spacing=\"12345678901234567890123\"/>").config).spacing == 1.2345678901234568e22f);
}

TEST_CASE("Markup checks where each element may stand", "[ui][markup][vocabulary]")
{
    // panel은 내용 하나뿐이다. 둘째부터 받지 않고, 구조 요소와 gap도 받지 않는다.
    require_ok(u8"<panel><stack/></panel>");
    require_ok(u8"<panel><slot name=\"s\"/></panel>");
    require_error(u8"<panel><label/><label/></panel>", ui_markup_error_kind::invalid_child, "label");
    REQUIRE(narrow(only_error(u8"<panel><label/><label/></panel>").path) == "panel");
    require_error(u8"<panel><if test=\"true\"><label/></if></panel>", ui_markup_error_kind::invalid_child, "if");
    require_error(u8"<panel><gap length=\"1\"/></panel>", ui_markup_error_kind::invalid_child, "gap");
    // 글자를 받는 요소가 없다. 요소 안의 요소도 받지 않는 요소가 있다.
    const luil::ui_markup_error text { only_error(u8"<stack>\n  hello\n</stack>") };
    REQUIRE(text.kind == ui_markup_error_kind::invalid_child);
    REQUIRE(text.name.empty());
    REQUIRE(narrow(text.value) == "hello");
    REQUIRE(text.line == 2);
    REQUIRE(text.column == 3);
    REQUIRE(narrow(luil::describe(text)) == "2:3 Text is not allowed in stack: hello");
    // 공백이 아닌 글자(U+00A0)와 참조로 적은 공백은 글이다.
    REQUIRE(only_error(u8"<stack> </stack>").kind == ui_markup_error_kind::invalid_child);
    REQUIRE(only_error(u8"<stack>&#32;</stack>").kind == ui_markup_error_kind::invalid_child);
    require_error(u8"<label><label/></label>", ui_markup_error_kind::invalid_child, "label");
    require_error(u8"<stack><textButton id=\"b\"><label/></textButton></stack>", ui_markup_error_kind::invalid_child, "label");
    // gap은 stack 안에서만, 구조 요소는 stack·wrap 안에서만 선다. 구조 요소를 건너 가장 가까운 요소를 본다.
    require_error(u8"<wrap><gap length=\"1\"/></wrap>", ui_markup_error_kind::invalid_child, "gap");
    require_ok(u8"<stack><if test=\"true\"><gap length=\"1\"/></if></stack>");
    require_error(u8"<wrap><if test=\"true\"><gap length=\"1\"/></if></wrap>", ui_markup_error_kind::invalid_child, "gap");
    require_ok(u8"<wrap itemWidth=\"10\" itemHeight=\"10\"><for each=\"{xs}\" as=\"x\" key=\"{x.k}\"><panel/></for></wrap>");
    // 뿌리는 config를 가진 요소다.
    for (const std::u8string_view root : { u8"<for each=\"{a}\" as=\"x\" key=\"{x.k}\"/>", u8"<if test=\"true\"/>", u8"<unless test=\"true\"/>", u8"<gap length=\"1\"/>", u8"<slot name=\"s\"/>" })
    {
        const luil::ui_markup_error error { only_error(root) };
        REQUIRE(error.kind == ui_markup_error_kind::invalid_child);
        REQUIRE(error.path.empty());
    }
    REQUIRE(narrow(luil::describe(only_error(u8"<gap length=\"1\"/>"))) == "1:1 Invalid root element: gap");
}

TEST_CASE("Markup stack children need a length unless they report one", "[ui][markup][vocabulary]")
{
    // 원하는 크기를 답하지 않는 요소(panel·progress·wrap)와 slot은 자리가 없으면 막는다. 0으로 접혀 보이지 않는 자식을 만들지 않는다.
    for (const std::u8string_view child : { u8"<panel/>", u8"<progress/>", u8"<wrap/>", u8"<slot name=\"s\"/>", u8"<gap/>" })
    {
        const std::u8string text { u8"<stack>" + std::u8string { child } + u8"</stack>" };
        const luil::ui_markup_error error { only_error(text) };
        REQUIRE(error.kind == ui_markup_error_kind::missing_length);
        REQUIRE(error.column == 8);
    }
    // 답하지 않는 요소에는 fit도 자리가 아니다. fit="false"는 자리를 적은 것이 아니다.
    require_error(u8"<stack><panel fit=\"true\"/></stack>", ui_markup_error_kind::missing_length, "panel");
    require_error(u8"<stack><label fit=\"false\"/></stack>", ui_markup_error_kind::missing_length, "label");
    require_error(u8"<stack><slot name=\"s\" fit=\"false\"/></stack>", ui_markup_error_kind::missing_length, "slot");
    REQUIRE(narrow(luil::describe(only_error(u8"<stack><panel/></stack>"))) == "1:8 Missing length on panel (set length, weight or fit)");
    require_ok(u8"<stack><panel length=\"10\"/><progress weight=\"1\"/><slot name=\"s\" fit=\"{auto}\"/><label fit=\"true\"/></stack>");
    // 함께 못 쓰는 자리 속성이다.
    require_error(u8"<stack><label fit=\"true\" length=\"10\"/></stack>", ui_markup_error_kind::invalid_value, "fit");
    require_error(u8"<stack><label length=\"10\" weight=\"1\"/></stack>", ui_markup_error_kind::invalid_value, "weight");
    require_error(u8"<stack><gap length=\"10\" weight=\"1\"/></stack>", ui_markup_error_kind::invalid_value, "weight");
    require_error(u8"<stack><label length=\"10\" minimum=\"5\"/></stack>", ui_markup_error_kind::invalid_value, "minimum");
    require_error(u8"<stack><label maximum=\"5\"/></stack>", ui_markup_error_kind::invalid_value, "maximum");
    // stack 밖(뿌리, panel·wrap의 자식)의 자리 속성은 모르는 속성이다. 뿌리의 자리는 앱이 arrange로 준다.
    require_error(u8"<stack length=\"10\"/>", ui_markup_error_kind::unknown_attribute, "length");
    require_error(u8"<panel><label weight=\"1\"/></panel>", ui_markup_error_kind::unknown_attribute, "weight");
    require_error(u8"<wrap><label length=\"10\"/></wrap>", ui_markup_error_kind::unknown_attribute, "length");
    // 구조 요소 안의 자식은 부모 stack의 자리를 받는다. 구조 요소는 자리를 갖지 않는다.
    require_ok(u8"<stack><if test=\"true\"><panel length=\"4\"/></if></stack>");
    require_error(u8"<stack><if test=\"true\" length=\"4\"><label/></if></stack>", ui_markup_error_kind::unknown_attribute, "length");
    require_error(u8"<stack><if test=\"true\"><panel/></if></stack>", ui_markup_error_kind::missing_length, "panel");
    // label의 글자 굵기는 fontWeight다. stack 안의 weight는 늘 유연 항목이다.
    const luil::ui_markup_node label { first_child(u8"<stack><label weight=\"1\" fontWeight=\"bold\"/></stack>") };
    REQUIRE(label.placement == luil::ui_markup_placement::flexible);
    REQUIRE(std::get<luil::label_config>(luil::ui_markup_literal_values(label).config).weight == luil::font_weight::bold);
}

TEST_CASE("Markup requires ids and names and checks their form", "[ui][markup][vocabulary]")
{
    // 누르는 요소의 id, slot의 name, for의 each·as·key, if·unless의 test다.
    require_error(u8"<stack><textButton text=\"a\"/></stack>", ui_markup_error_kind::missing_attribute, "id");
    require_error(u8"<stack><check/></stack>", ui_markup_error_kind::missing_attribute, "id");
    require_error(u8"<stack><slot length=\"1\"/></stack>", ui_markup_error_kind::missing_attribute, "name");
    require_error(u8"<stack><if><gap length=\"1\"/></if></stack>", ui_markup_error_kind::missing_attribute, "test");
    require_error(u8"<stack><unless/></stack>", ui_markup_error_kind::missing_attribute, "test");
    const luil::ui_markup_parse_result loop { luil::parse_ui_markup(u8"<stack><for/></stack>") };
    REQUIRE(loop.errors.size() == 3);
    REQUIRE(narrow(loop.errors[0].name) == "each");
    REQUIRE(narrow(loop.errors[1].name) == "as");
    REQUIRE(narrow(loop.errors[2].name) == "key");
    REQUIRE(narrow(luil::describe(loop.errors[0])) == "1:8 Missing attribute on for: each");
    // id는 `[A-Za-z0-9_.-]`로 1~64바이트다.
    require_ok(u8"<stack id=\"a-1_b.c\"/>");
    require_ok(u8"<stack id=\"0\"/>");
    for (const std::u8string_view id : { u8"", u8"a b", u8"a/b", u8"a:b", u8"{x}", u8"한" })
    {
        const std::u8string text { u8"<stack id=\"" + std::u8string { id } + u8"\"/>" };
        require_error(text, ui_markup_error_kind::invalid_value, "id");
    }
    require_ok(u8"<stack id=\"" + std::u8string(64, u8'a') + u8"\"/>");
    require_error(u8"<stack id=\"" + std::u8string(65, u8'a') + u8"\"/>", ui_markup_error_kind::invalid_value, "id");
    // 사건·kind·slot의 이름은 `[A-Za-z_][A-Za-z0-9_.-]*`다. `kind`는 check가 받지 않는다(kind가 정해져 있다).
    require_ok(u8"<stack kind=\"card.list\"><textButton id=\"b\" action=\"open_file\" argument=\"x\"/><slot name=\"search-box\" length=\"1\"/></stack>");
    require_error(u8"<stack><textButton id=\"b\" action=\"1go\"/></stack>", ui_markup_error_kind::invalid_value, "action");
    require_error(u8"<stack kind=\"{k}\"/>", ui_markup_error_kind::invalid_value, "kind");
    require_error(u8"<stack><slot name=\"a b\" length=\"1\"/></stack>", ui_markup_error_kind::invalid_value, "name");
    require_error(u8"<stack><check id=\"c\" kind=\"card\"/></stack>", ui_markup_error_kind::unknown_attribute, "kind");
    // 이름 칸은 그 요소만 받는다.
    require_error(u8"<stack><label action=\"go\"/></stack>", ui_markup_error_kind::unknown_attribute, "action");
    require_error(u8"<stack><gap id=\"g\" length=\"1\"/></stack>", ui_markup_error_kind::unknown_attribute, "id");
    require_error(u8"<stack><slot name=\"s\" id=\"x\" length=\"1\"/></stack>", ui_markup_error_kind::unknown_attribute, "id");
    require_error(u8"<stack><if test=\"true\" visible=\"false\"/></stack>", ui_markup_error_kind::unknown_attribute, "visible");
}

TEST_CASE("Markup refuses the same id written twice", "[ui][markup][vocabulary]")
{
    const luil::ui_markup_error twice { only_error(u8"<stack>\n  <label id=\"a\"/>\n  <panel id=\"a\" length=\"1\"/>\n</stack>") };
    REQUIRE(twice.kind == ui_markup_error_kind::duplicate_id);
    REQUIRE(twice.line == 3);
    REQUIRE(twice.column == 10);
    REQUIRE(narrow(twice.name) == "a");
    REQUIRE(narrow(luil::describe(twice)) == "3:10 Duplicate id: a");
    // 반복 안팎을 가리지 않는다. 반복은 한 번 적은 id를 항목마다 key로 가른다.
    require_error(u8"<stack><label id=\"a\"/><for each=\"{xs}\" as=\"x\" key=\"{x.k}\"><label id=\"a\"/></for></stack>", ui_markup_error_kind::duplicate_id, "a");
    require_error(u8"<stack><for each=\"{xs}\" as=\"x\" key=\"{x.k}\"><label id=\"a\"/></for><for each=\"{ys}\" as=\"y\" key=\"{y.k}\"><label id=\"a\"/></for></stack>", ui_markup_error_kind::duplicate_id, "a");
    require_ok(u8"<stack><for each=\"{xs}\" as=\"x\" key=\"{x.k}\"><label id=\"a\"/></for></stack>");
}

TEST_CASE("Markup reads bindings into names and keeps written values", "[ui][markup][vocabulary][binding]")
{
    // 글 속성은 적은 글과 이름을 섞는다. `{{`·`}}`는 글자다.
    const luil::ui_markup_node mixed { first_child(u8"<stack><label text=\"Clicks: {count} {{x}}\"/></stack>") };
    const luil::ui_markup_attribute& text { attribute_of(mixed, "text") };
    REQUIRE(text.bound());
    REQUIRE(text.pieces == std::vector<luil::ui_markup_piece> { text_piece(u8"Clicks: "), name_piece(u8"count"), text_piece(u8" {x}") });
    // 이름이 없으면 적은 글이고, 겹친 중괄호는 풀어 둔다.
    const luil::ui_markup_node literal { first_child(u8"<stack><label text=\"{{literal}}\"/></stack>") };
    REQUIRE(attribute_of(literal, "text").bound() == false);
    REQUIRE(narrow(std::get<std::u8string>(attribute_of(literal, "text").literal)) == "{literal}");
    // 수·참거짓 속성은 값 전체가 이름 하나일 때만 바인딩이다.
    const luil::ui_markup_node values { first_child(u8"<stack><check id=\"c\" checked=\"{on}\" label=\"{title}\"/><progress value=\"{done}\" length=\"{size}\"/></stack>") };
    REQUIRE(attribute_of(values, "checked").pieces == std::vector<luil::ui_markup_piece> { name_piece(u8"on") });
    require_error(u8"<progress value=\"{a}{b}\"/>", ui_markup_error_kind::invalid_value, "value");
    require_error(u8"<progress value=\"0.{a}\"/>", ui_markup_error_kind::invalid_value, "value");
    require_error(u8"<label wrap=\"{a} \"/>", ui_markup_error_kind::invalid_value, "wrap");
    // 이름(enum·역할)·id·kind·action·name은 적은 글자뿐이다.
    require_error(u8"<stack direction=\"{dir}\"/>", ui_markup_error_kind::invalid_value, "direction");
    require_error(u8"<panel background=\"{role}\"/>", ui_markup_error_kind::invalid_value, "background");
    require_error(u8"<stack id=\"{id}\"/>", ui_markup_error_kind::invalid_value, "id");
    require_error(u8"<stack><textButton id=\"b\" action=\"{act}\"/></stack>", ui_markup_error_kind::invalid_value, "action");
    // 바인딩의 꼴이다. `:`는 앞으로의 접두를 위해 비워 둔다. 닫지 않은 `{`, 홀로 선 `}`, 빈 이름, 점 둘이다.
    for (const std::u8string_view value : { u8"{t:key}", u8"{a", u8"a}", u8"{}", u8"{a.b.c}", u8"{1a}", u8"{a b}", u8"{a-b}" })
    {
        const std::u8string written { u8"<label text=\"" + std::u8string { value } + u8"\"/>" };
        require_error(written, ui_markup_error_kind::invalid_value, "text");
    }
    // 점이 있으면 앞은 열린 반복의 변수다.
    require_error(u8"<label text=\"{item.title}\"/>", ui_markup_error_kind::invalid_value, "text");
    const luil::ui_markup_parse_result loop { luil::parse_ui_markup(
        u8"<stack><for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><for each=\"{row.cells}\" as=\"cell\" key=\"{row.key}-{cell.key}\">"
        u8"<textButton id=\"cell\" text=\"{cell.title}\" argument=\"{row.key}/{cell.key}\" action=\"pick\"/></for></for>"
        u8"<label text=\"{row.title}\"/></stack>")
    };
    REQUIRE(loop.errors.size() == 1);
    REQUIRE(loop.errors.front().kind == ui_markup_error_kind::invalid_value);
    REQUIRE(narrow(loop.errors.front().path) == "stack/label");
    // 반복과 조건의 값이다. each는 이름 하나, key와 argument는 글, test는 참거짓이다.
    const luil::ui_markup_parse_result structure { luil::parse_ui_markup(
        u8"<stack><for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><textButton id=\"r\" action=\"go\" argument=\"#{row.key}\"/></for>"
        u8"<if test=\"{shown}\"><gap length=\"1\"/></if><unless test=\"false\"><gap length=\"1\"/></unless></stack>")
    };
    const luil::ui_markup_document& document { document_of(structure) };
    const luil::ui_markup_node& for_node { document.nodes[1] };
    REQUIRE(for_node.tag == luil::ui_markup_tag::for_each);
    REQUIRE(narrow(for_node.as) == "row");
    REQUIRE(for_node.each->pieces == std::vector<luil::ui_markup_piece> { name_piece(u8"rows") });
    REQUIRE(for_node.key->pieces == std::vector<luil::ui_markup_piece> { name_piece(u8"key", u8"row") });
    const luil::ui_markup_node& button { document.nodes[2] };
    REQUIRE(narrow(button.action) == "go");
    REQUIRE(button.argument->pieces == std::vector<luil::ui_markup_piece> { text_piece(u8"#"), name_piece(u8"key", u8"row") });
    REQUIRE(document.nodes[3].tag == luil::ui_markup_tag::if_true);
    REQUIRE(document.nodes[3].test->pieces == std::vector<luil::ui_markup_piece> { name_piece(u8"shown") });
    REQUIRE(document.nodes[5].tag == luil::ui_markup_tag::unless_true);
    REQUIRE(std::get<bool>(document.nodes[5].test->literal) == false);
    require_error(u8"<stack><for each=\"rows\" as=\"row\" key=\"{row.key}\"/></stack>", ui_markup_error_kind::invalid_value, "each");
    require_error(u8"<stack><for each=\"{rows}\" as=\"row.x\" key=\"k\"/></stack>", ui_markup_error_kind::invalid_value, "as");
    require_error(u8"<stack><if test=\"yes\"/></stack>", ui_markup_error_kind::invalid_value, "test");
}

TEST_CASE("Markup collects every vocabulary error in text order", "[ui][markup][vocabulary]")
{
    const luil::ui_markup_parse_result result { luil::parse_ui_markup(
        u8"<stack spacing=\"-1\">\n"
        u8"  <panel/>\n"
        u8"  <label colr=\"x\" text=\"a\" text=\"b\"/>\n"
        u8"  <wat/>\n"
        u8"  <textButton/>\n"
        u8"</stack>")
    };
    REQUIRE(result.ok() == false);
    REQUIRE(result.document == nullptr);
    INFO(described(result));
    const std::vector<ui_markup_error_kind> kinds {
        ui_markup_error_kind::invalid_value,
        ui_markup_error_kind::missing_length,
        ui_markup_error_kind::unknown_attribute,
        ui_markup_error_kind::duplicate_attribute,
        ui_markup_error_kind::unknown_element,
        ui_markup_error_kind::missing_attribute,
    };
    REQUIRE(result.errors.size() == kinds.size());
    for (std::size_t index { 0 }; index < kinds.size(); ++index)
    {
        REQUIRE(result.errors[index].kind == kinds[index]);
        if (index > 0)
            REQUIRE(result.errors[index - 1].offset < result.errors[index].offset);
    }
    REQUIRE(narrow(result.errors[4].path) == "stack/wat");
    REQUIRE(narrow(luil::describe(result.errors[0])) == "1:8 Invalid value for spacing on stack: -1");
    REQUIRE(narrow(luil::describe(result.errors[4])) == "4:3 Unknown element: wat");
    // 같은 이름의 형제는 `[n]`(1부터)이다.
    const luil::ui_markup_error sibling { only_error(u8"<stack><stack/><stack><label colr=\"x\"/></stack></stack>") };
    REQUIRE(narrow(sibling.path) == "stack/stack[2]/label");
}

// --- 펼치기 (docs/markup-binding-design.md) ---

namespace {
    // test 정의 kind다. 앱이 자기 kind를 정하는 것과 같은 길이다.
    constexpr luil::ui_element_kind kind_app_panel { luil::application_element_kind(5) };
    constexpr luil::ui_element_kind kind_app_label { luil::application_element_kind(6) };

    constexpr char8_t separator { u8'\x1F' };

    // 앱 메시지의 대역이다. 사건 이름과 인자를 그대로 싣는다.
    struct markup_intent
    {
        std::u8string name {};
        std::u8string argument {};
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_markup_document> template_of(const std::u8string_view text, const luil::ui_markup_limits& limits = {})
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text, limits) };
        INFO(narrow(text));
        INFO(described(result));
        REQUIRE(result.ok());
        return result.document;
    }

    [[nodiscard]] std::string described(const std::vector<luil::ui_markup_error>& errors)
    {
        std::string text {};
        for (const luil::ui_markup_error& error : errors)
            text += narrow(luil::describe(error)) + "\n";
        return text;
    }

    // 펼친 뿌리를 그 자리에 배치한 tree다 (배율 1).
    [[nodiscard]] luil::ui_tree arranged(std::unique_ptr<luil::ui_element> root, const float width = 400.0f, const float height = 300.0f)
    {
        REQUIRE(root != nullptr);
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f);
    }

    [[nodiscard]] luil::ui_element_id node_id(std::u8string owner)
    {
        return { luil::ui_element_kind::markup_node, std::move(owner) };
    }

    [[nodiscard]] std::u8string joined(const std::u8string_view first, const std::u8string_view second)
    {
        std::u8string owner { first };
        owner.push_back(separator);
        owner += second;
        return owner;
    }

    [[nodiscard]] std::u8string text_of(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        INFO("owner " << narrow(id.owner));
        REQUIRE(element != nullptr);
        return element->accessibility().name;
    }

    // 항목 표 하나다 (key와 title).
    [[nodiscard]] luil::ui_markup_scope row_scope(const std::u8string_view key, const std::u8string_view title)
    {
        luil::ui_markup_scope row {};
        row.set(u8"key", std::u8string { key }).set(u8"title", std::u8string { title });
        return row;
    }

    [[nodiscard]] luil::ui_markup_list rows_of(std::vector<luil::ui_markup_scope> rows)
    {
        return std::make_shared<const std::vector<luil::ui_markup_scope>>(std::move(rows));
    }

    [[nodiscard]] std::vector<luil::ui_markup_error_kind> kinds_of(const std::vector<luil::ui_markup_error>& errors)
    {
        std::vector<luil::ui_markup_error_kind> kinds {};
        for (const luil::ui_markup_error& error : errors)
            kinds.push_back(error.kind);
        return kinds;
    }

    // 액션을 한 번 돌린 메시지다.
    [[nodiscard]] std::vector<luil::input_action> run(const luil::ui_element& element)
    {
        const luil::ui_action* const action { element.action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)(luil::ui_action_context { .element = element.id() });
    }

    [[nodiscard]] const markup_intent& intent_of(const luil::input_action& action)
    {
        const luil::app_message* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        const markup_intent* const intent { message->get<markup_intent>() };
        REQUIRE(intent != nullptr);
        return *intent;
    }

    // 사건 이름을 그대로 실은 메시지로 바꾸는 앱 함수다. `unknown`으로 시작하는 이름은 모른다.
    [[nodiscard]] luil::input_action echo_action(const luil::ui_markup_action& action)
    {
        if (action.name.starts_with(u8"unknown"))
            return {};
        return luil::make_app_action(markup_intent { std::u8string { action.name }, std::u8string { action.argument } });
    }
}

TEST_CASE("Markup bindings read the values of each build", "[ui][markup][binding]")
{
    const auto document { template_of(
        u8"<stack id=\"root\">"
        u8"<label id=\"count\" text=\"누른 횟수: {count}\" length=\"20\"/>"
        u8"<label id=\"braces\" text=\"{{{count}}}\" length=\"20\"/>"
        u8"<label id=\"shown\" text=\"보임\" visible=\"{shown}\" length=\"20\"/>"
        u8"</stack>")
    };
    luil::ui_markup_scope values {};
    values.set(u8"count", std::u8string { u8"3" }).set(u8"shown", true);
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, { .values = &values }) };
    INFO(described(built.errors));
    REQUIRE(built.errors.empty());
    REQUIRE(built.built == 4);
    luil::ui_tree tree { arranged(std::move(built.element)) };
    REQUIRE(text_of(tree, node_id(u8"count")) == u8"누른 횟수: 3");
    // `{{`·`}}`는 글자다.
    REQUIRE(text_of(tree, node_id(u8"braces")) == u8"{3}");
    REQUIRE(tree.find(node_id(u8"shown"))->visible());

    // 표를 바꾸면 다음 펼침이 새 값이다. 같은 이름은 덮어쓴다.
    values.set(u8"count", std::u8string { u8"4" }).set(u8"shown", false);
    tree = arranged(luil::build_ui_markup(*document, { .values = &values }).element);
    REQUIRE(text_of(tree, node_id(u8"count")) == u8"누른 횟수: 4");
    REQUIRE(tree.find(node_id(u8"shown"))->visible() == false);

    // 형이 틀리면 그 속성은 빈 값이고 오류 하나다. 수를 글로 바꾸지 않는다.
    luil::ui_markup_scope wrong {};
    wrong.set(u8"count", 3.0).set(u8"shown", std::u8string { u8"true" });
    built = luil::build_ui_markup(*document, { .values = &wrong });
    INFO(described(built.errors));
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::wrong_value_type, ui_markup_error_kind::wrong_value_type, ui_markup_error_kind::wrong_value_type });
    REQUIRE(narrow(built.errors.front().name) == "text");
    REQUIRE(narrow(built.errors.front().value) == "count");
    tree = arranged(std::move(built.element));
    REQUIRE(text_of(tree, node_id(u8"count")).empty());
    REQUIRE(tree.find(node_id(u8"shown"))->visible() == false);

    // 없는 이름도 빈 값으로 선다. 요소는 그대로 선다.
    const luil::ui_markup_scope empty {};
    built = luil::build_ui_markup(*document, { .values = &empty });
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::unknown_value, ui_markup_error_kind::unknown_value, ui_markup_error_kind::unknown_value });
    REQUIRE(narrow(built.errors.front().name) == "count");
    REQUIRE(built.built == 4);
    // 값 표가 없어도 같다.
    REQUIRE(luil::build_ui_markup(*document, {}).errors == built.errors);

    // `{` 안의 `:`는 앞으로의 접두를 위해 비워 둔다 (읽을 때 막는다).
    require_error(u8"<stack><label text=\"{t:key}\" length=\"20\"/></stack>", ui_markup_error_kind::invalid_value, "text");
}

TEST_CASE("Markup builds attributes in row order with bound values", "[ui][markup][binding]")
{
    // 바인딩한 `padding`(네 변)이 뒤에 적은 `paddingLeft`를 덮지 않는다. 행 순서대로 다시 걸기 때문이다.
    const auto document { template_of(u8"<stack id=\"root\" paddingLeft=\"3\" padding=\"{pad}\"><label id=\"a\" text=\"a\" length=\"10\"/></stack>") };
    luil::ui_markup_scope values {};
    values.set(u8"pad", 10.0);
    const luil::ui_tree tree { arranged(luil::build_ui_markup(*document, { .values = &values }).element, 200.0f, 100.0f) };
    const luil::rect_f bounds { tree.find(node_id(u8"a"))->bounds() };
    REQUIRE(bounds.x == 3.0f);
    REQUIRE(bounds.y == 10.0f);
    REQUIRE(bounds.width == 200.0f - 13.0f);

    // 수가 그 속성의 범위 밖이면 형 오류이고 요소의 기본값이 남는다. 0~1 속성은 요소처럼 자른다.
    const auto ranged { template_of(
        u8"<stack id=\"root\">"
        u8"<label id=\"l\" text=\"x\" fontSize=\"{size}\" maxLines=\"{lines}\" length=\"10\"/>"
        u8"<progress id=\"p\" value=\"{done}\" length=\"4\"/>"
        u8"</stack>")
    };
    luil::ui_markup_scope wild {};
    wild.set(u8"size", 0.0).set(u8"lines", -1.0).set(u8"done", 2.0);
    const luil::ui_markup_build_result built { luil::build_ui_markup(*ranged, { .values = &wild }) };
    INFO(described(built.errors));
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::wrong_value_type, ui_markup_error_kind::wrong_value_type });
    REQUIRE(narrow(built.errors[0].name) == "fontSize");
    REQUIRE(narrow(built.errors[1].name) == "maxLines");
    const luil::ui_tree wild_tree { arranged(luil::build_ui_markup(*ranged, { .values = &wild }).element) };
    REQUIRE(wild_tree.find(node_id(u8"p"))->accessibility().range->value == 1.0f);
    wild.set(u8"size", std::numeric_limits<double>::infinity()).set(u8"lines", 2.5).set(u8"done", std::nan(""));
    REQUIRE(luil::build_ui_markup(*ranged, { .values = &wild }).errors.size() == 3);
}

TEST_CASE("Markup loops key their owners and conditions pick one branch", "[ui][markup][binding]")
{
    const auto document { template_of(
        u8"<stack id=\"root\">"
        u8"<for each=\"{rows}\" as=\"row\" key=\"{row.key}\">"
        u8"<textButton id=\"b\" text=\"{row.title}\" length=\"20\"/>"
        u8"<for each=\"{row.cells}\" as=\"cell\" key=\"{cell.key}\">"
        u8"<label id=\"c\" text=\"{cell.title}/{row.title}/{title}\" length=\"10\"/>"
        u8"</for>"
        u8"</for>"
        u8"<if test=\"{on}\"><label id=\"yes\" text=\"y\" length=\"10\"/></if>"
        u8"<unless test=\"{on}\"><label id=\"no\" text=\"n\" length=\"10\"/></unless>"
        u8"</stack>")
    };
    luil::ui_markup_scope first { row_scope(u8"r1", u8"첫째") };
    first.set(u8"cells", rows_of({ row_scope(u8"x", u8"가"), row_scope(u8"y", u8"나") }));
    luil::ui_markup_scope second { row_scope(u8"r2", u8"둘째") };
    second.set(u8"cells", rows_of({}));
    luil::ui_markup_scope values {};
    values.set(u8"rows", rows_of({ first, second })).set(u8"on", true).set(u8"title", std::u8string { u8"바깥" });

    luil::ui_markup_build_result built { luil::build_ui_markup(*document, { .values = &values }) };
    INFO(described(built.errors));
    REQUIRE(built.errors.empty());
    REQUIRE(built.built == 6);
    luil::ui_tree tree { arranged(std::move(built.element)) };
    REQUIRE(tree.duplicate_ids().empty());
    // owner는 id, U+001F, key 순이고 겹친 반복은 바깥 key부터 잇는다. 이름은 안쪽 항목부터 바깥 표까지 찾는다.
    REQUIRE(text_of(tree, node_id(joined(u8"b", u8"r1"))) == u8"첫째");
    REQUIRE(text_of(tree, node_id(joined(joined(u8"c", u8"r1"), u8"y"))) == u8"나/첫째/나");
    REQUIRE(text_of(tree, node_id(joined(u8"b", u8"r2"))) == u8"둘째");
    REQUIRE(tree.find(node_id(joined(joined(u8"c", u8"r2"), u8"x"))) == nullptr);
    REQUIRE(tree.find(node_id(u8"yes")) != nullptr);
    REQUIRE(tree.find(node_id(u8"no")) == nullptr);
    // 자식 순서는 항목 순서다.
    const auto& children { tree.root()->children() };
    REQUIRE(children.size() == 5);
    REQUIRE(children[0]->id() == node_id(joined(u8"b", u8"r1")));
    REQUIRE(children[3]->id() == node_id(joined(u8"b", u8"r2")));

    // 빈 목록과 거짓 조건이다.
    values.set(u8"rows", rows_of({})).set(u8"on", false);
    tree = arranged(luil::build_ui_markup(*document, { .values = &values }).element);
    REQUIRE(tree.root()->children().size() == 1);
    REQUIRE(tree.find(node_id(u8"no")) != nullptr);

    // 조건 값이 없으면 거짓으로 서므로 unless의 가지가 선다.
    values = {};
    values.set(u8"rows", rows_of({}));
    built = luil::build_ui_markup(*document, { .values = &values });
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::unknown_value, ui_markup_error_kind::unknown_value });
    tree = arranged(std::move(built.element));
    REQUIRE(tree.find(node_id(u8"no")) != nullptr);

    // 목록 자리에 글이면 형 오류이고 빈 목록이다.
    values.set(u8"rows", std::u8string { u8"rows" }).set(u8"on", true);
    built = luil::build_ui_markup(*document, { .values = &values });
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::wrong_value_type });
    REQUIRE(narrow(built.errors.front().name) == "each");
}

TEST_CASE("Markup reports a repeated key and keeps the duplicate ids visible", "[ui][markup][binding]")
{
    const auto document { template_of(u8"<stack id=\"root\"><for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><textButton id=\"b\" text=\"{row.title}\" length=\"20\"/></for></stack>") };
    luil::ui_markup_scope values {};
    values.set(u8"rows", rows_of({ row_scope(u8"r1", u8"a"), row_scope(u8"r1", u8"b"), row_scope(u8"r2", u8"c") }));
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, { .values = &values }) };
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::duplicate_key });
    REQUIRE(narrow(built.errors.front().value) == "r1");
    REQUIRE(narrow(built.errors.front().path) == "stack/for");
    // 펼침은 그대로 서고 tree의 겹친 id에도 남는다.
    REQUIRE(built.built == 4);
    const luil::ui_tree tree { arranged(std::move(built.element)) };
    REQUIRE(tree.duplicate_ids() == std::vector<luil::ui_element_id> { node_id(joined(u8"b", u8"r1")) });
}

TEST_CASE("Markup stops at max_built with one error", "[ui][markup][binding]")
{
    luil::ui_markup_limits limits {};
    limits.max_built = 3;
    const auto document { template_of(u8"<stack id=\"root\"><for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><label id=\"l\" text=\"{row.title}\" length=\"10\"/></for></stack>", limits) };
    std::vector<luil::ui_markup_scope> rows {};
    for (int index { 0 }; index < 10; ++index)
        rows.push_back(row_scope(std::u8string(1, static_cast<char8_t>(u8'a' + index)), u8"t"));
    luil::ui_markup_scope values {};
    values.set(u8"rows", rows_of(std::move(rows)));
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, { .values = &values }) };
    INFO(described(built.errors));
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::too_many_built });
    REQUIRE(narrow(built.errors.front().name) == "max_built");
    REQUIRE(narrow(built.errors.front().value) == "3");
    REQUIRE(narrow(luil::describe(built.errors.front())).ends_with("Built elements exceed max_built (3)"));
    REQUIRE(built.built == 3);
    // 뿌리와 그 앞까지 지은 것은 선다.
    const luil::ui_tree tree { arranged(std::move(built.element)) };
    REQUIRE(tree.root()->children().size() == 2);
    REQUIRE(tree.unarranged().empty());
}

TEST_CASE("Markup builds walk deep templates without recursion", "[ui][markup][binding]")
{
    // 앱이 깊이 한도를 올린 틀도 펼치기는 쌓은 frame으로 돈다 (읽기와 같은 까닭). element의 해제는 자식을 따라 재귀해
    // Debug 빌드에서 2000단이면 호출 스택을 넘긴다. 이 시험은 기본 한도(64)의 네 배에서 멈춘다.
    constexpr std::size_t depth { 256 };
    std::u8string text {};
    for (std::size_t index { 0 }; index < depth; ++index)
        text += u8"<stack>";
    for (std::size_t index { 0 }; index < depth; ++index)
        text += u8"</stack>";
    luil::ui_markup_limits limits {};
    limits.max_depth = depth + 1;
    limits.max_nodes = depth + 1;
    const auto document { template_of(text, limits) };
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, {}) };
    REQUIRE(built.errors.empty());
    REQUIRE(built.built == depth);
    // 이름 없는 요소의 owner는 자식 차례의 자리다.
    const luil::ui_element* element { built.element.get() };
    REQUIRE(element->id() == node_id(u8"/"));
    element = element->children().front().get();
    REQUIRE(element->id() == node_id(u8"/0"));
    REQUIRE(element->children().front()->id() == node_id(u8"/0/0"));
}

TEST_CASE("Markup actions are made once while building", "[ui][markup][binding][action]")
{
    const auto document { template_of(
        u8"<stack id=\"root\">"
        u8"<for each=\"{rows}\" as=\"row\" key=\"{row.key}\">"
        u8"<textButton id=\"pick\" text=\"{row.title}\" action=\"select\" argument=\"고른 {row.key}\" length=\"20\"/>"
        u8"</for>"
        u8"<check id=\"list\" label=\"목록\" checked=\"{on}\" action=\"toggle\" length=\"24\"/>"
        u8"<textButton id=\"lost\" text=\"모름\" action=\"unknownName\" length=\"20\"/>"
        u8"<check id=\"lost-check\" label=\"모름\" action=\"unknownToggle\" length=\"24\"/>"
        u8"<textButton id=\"plain\" text=\"그냥\" length=\"20\"/>"
        u8"</stack>")
    };
    luil::ui_markup_scope values {};
    values.set(u8"rows", rows_of({ row_scope(u8"a", u8"가"), row_scope(u8"b", u8"나") })).set(u8"on", true);
    std::vector<std::pair<std::u8string, std::u8string>> calls {};
    std::vector<luil::ui_element_id> ids {};
    const luil::ui_markup_context context {
        .values = &values,
        .make_action = [&calls, &ids](const luil::ui_markup_action& action) {
            calls.emplace_back(action.name, action.argument);
            ids.push_back(action.id);
            return echo_action(action);
        },
    };
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, context) };
    INFO(described(built.errors));
    // 누르는 요소마다 한 번이다. 사건이 없는 단추는 부르지 않는다.
    REQUIRE(calls.size() == 5);
    REQUIRE(calls[0] == std::pair<std::u8string, std::u8string> { u8"select", u8"고른 a" });
    REQUIRE(calls[1] == std::pair<std::u8string, std::u8string> { u8"select", u8"고른 b" });
    REQUIRE(calls[2] == std::pair<std::u8string, std::u8string> { u8"toggle", u8"" });
    REQUIRE(ids[0] == node_id(joined(u8"pick", u8"a")));
    REQUIRE(ids[2] == luil::ui_element_id { luil::ui_element_kind::check, u8"list" });
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::unknown_action, ui_markup_error_kind::unknown_action });
    REQUIRE(narrow(built.errors.front().name) == "unknownName");

    const luil::ui_tree tree { arranged(std::move(built.element)) };
    // 지은 액션은 쥔 값을 돌려준다. 돌릴 때 앱 함수를 다시 부르지 않는다.
    const std::vector<luil::input_action> picked { run(*tree.find(node_id(joined(u8"pick", u8"b")))) };
    REQUIRE(picked.size() == 1);
    REQUIRE(intent_of(picked.front()).name == u8"select");
    REQUIRE(intent_of(picked.front()).argument == u8"고른 b");
    const std::vector<luil::input_action> toggled { run(*tree.find({ luil::ui_element_kind::check, u8"list" })) };
    REQUIRE(intent_of(toggled.front()).name == u8"toggle");
    REQUIRE(calls.size() == 5);
    // 모르는 사건이면 단추는 비활성이고 체크는 누를 수 없다. 사건이 없는 단추는 그대로 선다.
    REQUIRE(tree.find(node_id(u8"lost"))->enabled() == false);
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"lost-check" })->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(tree.find(node_id(u8"plain"))->enabled());
    REQUIRE(tree.find(node_id(u8"plain"))->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(tree.find(node_id(joined(u8"pick", u8"a")))->enabled());

    // make_action이 비어 있으면 모든 사건이 모르는 이름이다.
    REQUIRE(luil::build_ui_markup(*document, { .values = &values }).errors.size() == 4);
}

TEST_CASE("Markup ids take app kinds, prefixes and positions", "[ui][markup][binding][id]")
{
    const auto document { template_of(
        u8"<stack id=\"root\" kind=\"appPanel\">"
        u8"<label text=\"a\" length=\"10\"/>"
        u8"<label id=\"named\" kind=\"appLabel\" text=\"b\" length=\"10\"/>"
        u8"<label id=\"odd\" kind=\"nowhere\" text=\"c\" length=\"10\"/>"
        u8"<stack length=\"20\"><if test=\"{on}\"><label text=\"d\" length=\"10\"/></if></stack>"
        u8"</stack>")
    };
    const std::vector<luil::inspector_kind_name> kinds { { kind_app_panel, u8"appPanel" }, { kind_app_label, u8"appLabel" } };
    luil::ui_markup_scope values {};
    values.set(u8"on", true);
    luil::ui_markup_context context { .values = &values, .kinds = kinds };
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, context) };
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::unknown_kind });
    REQUIRE(narrow(luil::describe(built.errors.front())).ends_with("Unknown kind: nowhere"));
    luil::ui_tree tree { arranged(std::move(built.element)) };
    REQUIRE(tree.root()->id() == luil::ui_element_id { kind_app_panel, u8"root" });
    REQUIRE(tree.find({ kind_app_label, u8"named" }) != nullptr);
    REQUIRE(tree.find(node_id(u8"odd")) != nullptr);
    // 이름 없는 요소는 뿌리부터 센 자식 차례다. 구조 요소도 한 단이다.
    REQUIRE(tree.find(node_id(u8"/0")) != nullptr);
    REQUIRE(tree.find(node_id(u8"/3")) != nullptr);
    REQUIRE(tree.find(node_id(u8"/3/0/0")) != nullptr);
    // 같은 틀·같은 값이면 frame 사이에 같은 owner다.
    const luil::ui_tree again { arranged(luil::build_ui_markup(*document, context).element) };
    REQUIRE(again.find(node_id(u8"/3/0/0")) != nullptr);

    // 같은 틀을 한 tree에 두 번 펼칠 때는 앞붙이로 가른다.
    context.owner_prefix = u8"left";
    tree = arranged(luil::build_ui_markup(*document, context).element);
    REQUIRE(tree.root()->id() == luil::ui_element_id { kind_app_panel, joined(u8"left", u8"root") });
    REQUIRE(tree.find(node_id(joined(u8"left", u8"/0"))) != nullptr);
}

TEST_CASE("Markup slots come from the app with the visible names", "[ui][markup][binding][slot]")
{
    const auto document { template_of(
        u8"<stack id=\"root\">"
        u8"<for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><slot name=\"cell\" length=\"10\"/></for>"
        u8"<slot name=\"missing\" length=\"12\"/>"
        u8"<label id=\"after\" text=\"z\" length=\"5\"/>"
        u8"</stack>")
    };
    luil::ui_markup_scope values {};
    values.set(u8"rows", rows_of({ row_scope(u8"a", u8"가"), row_scope(u8"b", u8"나") })).set(u8"title", std::u8string { u8"바깥" }).set(u8"outer", true);
    const luil::ui_markup_context context {
        .values = &values,
        .make_slot = [](const std::u8string_view name, const luil::ui_markup_scope& scope) -> std::unique_ptr<luil::ui_element> {
            if (name != u8"cell")
                return nullptr;
            // 바깥 표 위에 항목을 덮은 표다. 항목의 이름이 이긴다.
            const auto* const key { std::get_if<std::u8string>(scope.find(u8"key")) };
            const auto* const title { std::get_if<std::u8string>(scope.find(u8"title")) };
            const auto* const outer { std::get_if<bool>(scope.find(u8"outer")) };
            REQUIRE(key != nullptr);
            REQUIRE(title != nullptr);
            REQUIRE(outer != nullptr);
            luil::label_config config {};
            config.text = *title;
            return std::make_unique<luil::label_element>(luil::ui_element_id { kind_app_label, u8"cell-" + *key }, std::move(config));
        },
    };
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, context) };
    REQUIRE(kinds_of(built.errors) == std::vector<ui_markup_error_kind> { ui_markup_error_kind::unknown_slot });
    REQUIRE(narrow(luil::describe(built.errors.front())).ends_with("Unknown slot: missing"));
    REQUIRE(built.built == 4);
    const luil::ui_tree tree { arranged(std::move(built.element)) };
    // slot의 요소는 앱의 id를 그대로 쓰고 마크업의 자리로 담긴다.
    REQUIRE(text_of(tree, { kind_app_label, u8"cell-a" }) == u8"가");
    REQUIRE(tree.find({ kind_app_label, u8"cell-b" })->bounds().y == 10.0f);
    REQUIRE(tree.find({ kind_app_label, u8"cell-b" })->bounds().height == 10.0f);
    // 빈 slot의 자리는 남는다.
    REQUIRE(tree.find(node_id(u8"after"))->bounds().y == 32.0f);
}

TEST_CASE("Markup keeps focus on the same key after an item is inserted", "[ui][markup][binding][focus]")
{
    const auto document { template_of(u8"<stack id=\"root\"><for each=\"{rows}\" as=\"row\" key=\"{row.key}\"><textButton id=\"item\" text=\"{row.title}\" action=\"select\" length=\"24\"/></for></stack>") };
    const auto tree_of = [&document](std::vector<luil::ui_markup_scope> rows) {
        luil::ui_markup_scope values {};
        values.set(u8"rows", rows_of(std::move(rows)));
        return std::make_shared<const luil::ui_tree>(arranged(luil::build_ui_markup(*document, { .values = &values, .make_action = echo_action }).element));
    };
    luil::interaction_controller controller {};
    controller.set_tree(tree_of({ row_scope(u8"a", u8"가"), row_scope(u8"b", u8"나"), row_scope(u8"c", u8"다") }));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab }));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab }));
    REQUIRE(controller.snapshot().focused == node_id(joined(u8"item", u8"b")));

    // 앞에 항목이 끼어도 key가 같은 단추에 초점이 남는다.
    controller.set_tree(tree_of({ row_scope(u8"z", u8"앞"), row_scope(u8"a", u8"가"), row_scope(u8"b", u8"나"), row_scope(u8"c", u8"다") }));
    REQUIRE(controller.snapshot().focused == node_id(joined(u8"item", u8"b")));
    // 다시 읽은 틀에서 그 key가 사라지면 controller가 초점을 거둔다.
    controller.set_tree(tree_of({ row_scope(u8"a", u8"가") }));
    REQUIRE(controller.snapshot().focused != node_id(joined(u8"item", u8"b")));
}

TEST_CASE("Markup build errors carry the place of the attribute", "[ui][markup][binding][errors]")
{
    const std::u8string text {
        u8"<stack id=\"root\">\n"
        u8"  <label id=\"a\" text=\"{cuont}\" length=\"10\"/>\n"
        u8"  <label id=\"b\" text=\"{count}\" length=\"10\"/>\n"
        u8"  <for each=\"{rows}\" as=\"row\" key=\"{row.key}\">\n"
        u8"    <textButton id=\"c\" text=\"{row.missing}\" action=\"unknownGo\" length=\"20\"/>\n"
        u8"  </for>\n"
        u8"  <slot name=\"nothing\" length=\"10\"/>\n"
        u8"</stack>"
    };
    const auto document { template_of(text) };
    luil::ui_markup_scope values {};
    values.set(u8"count", 1.0).set(u8"rows", rows_of({ row_scope(u8"k", u8"x"), row_scope(u8"k", u8"y") }));
    const luil::ui_markup_context context { .values = &values, .make_action = echo_action };
    const luil::ui_markup_build_result built { luil::build_ui_markup(*document, context) };
    INFO(described(built.errors));
    const std::vector<std::string> expected {
        "2:17 Unknown value: cuont",
        "3:17 Wrong value type for text on label: count",
        "4:31 Duplicate key in for: k",
        "5:24 Unknown value: row.missing",
        "5:45 Unknown action: unknownGo",
        "7:9 Unknown slot: nothing",
    };
    REQUIRE(built.errors.size() == expected.size());
    for (std::size_t index { 0 }; index < expected.size(); ++index)
        REQUIRE(narrow(luil::describe(built.errors[index])) == expected[index]);
    // 반복이 같은 자리에서 같은 실수를 되풀이해도 하나다.
    REQUIRE(built.errors[0].offset == text.find(u8"text=\"{cuont}"));
    REQUIRE(built.errors[3].offset == text.find(u8"text=\"{row.missing}"));
    REQUIRE(narrow(built.errors[0].path) == "stack/label[1]");
    REQUIRE(narrow(built.errors[3].path) == "stack/for/textButton");
    REQUIRE(narrow(built.errors[5].path) == "stack/slot");
    // 같은 틀·같은 값이면 같은 목록이다.
    REQUIRE(luil::build_ui_markup(*document, context).errors == built.errors);
}

TEST_CASE("A markup screen arranges like the same screen built in C++", "[ui][markup][binding][parity]")
{
    // hello 예제 꼴의 화면을 C++와 마크업으로 같은 값으로 짓고, 같은 자리에 배치한 두 tree를 pre-order로 견준다.
    const auto document { template_of(
        u8"<stack id=\"screen\" direction=\"column\" spacing=\"12\" padding=\"24\">"
        u8"<label id=\"count\" text=\"{countText}\" fontSize=\"14\" color=\"primary\" length=\"18\"/>"
        u8"<stack id=\"row\" direction=\"row\" spacing=\"8\" length=\"30\">"
        u8"<textButton id=\"more\" text=\"하나 더\" action=\"increment\" length=\"120\"/>"
        u8"<check id=\"list\" label=\"목록 보기\" checked=\"{showList}\" action=\"toggle\" weight=\"1\"/>"
        u8"</stack>"
        u8"<gap length=\"6\"/>"
        u8"<panel id=\"card\" background=\"surfaceBackground\" cornerRadius=\"6\" length=\"40\"><progress id=\"bar\" value=\"{progress}\" thickness=\"4\"/></panel>"
        u8"<wrap id=\"chips\" itemWidth=\"40\" itemHeight=\"20\" spacing=\"4\" lineSpacing=\"4\" weight=\"1\"><label id=\"w1\" text=\"a\"/><label id=\"w2\" text=\"b\" enabled=\"false\" tooltip=\"둘째\"/></wrap>"
        u8"</stack>")
    };
    luil::ui_markup_scope values {};
    values.set(u8"countText", std::u8string { u8"누른 횟수 2" }).set(u8"showList", true).set(u8"progress", 0.25);
    luil::ui_markup_build_result built { luil::build_ui_markup(*document, { .values = &values, .make_action = echo_action }) };
    INFO(described(built.errors));
    REQUIRE(built.errors.empty());
    const luil::ui_tree markup { arranged(std::move(built.element), 360.0f, 280.0f) };

    luil::stack_config screen_config {};
    screen_config.spacing = 12.0f;
    screen_config.padding = luil::edge_insets::all(24.0f);
    auto screen { std::make_unique<luil::stack_element>(node_id(u8"screen"), screen_config) };
    luil::label_config count {};
    count.text = u8"누른 횟수 2";
    count.font_size = 14.0f;
    count.color = luil::label_color_role::primary;
    screen->add(std::make_unique<luil::label_element>(node_id(u8"count"), std::move(count)), 18.0f);
    luil::stack_config row_config {};
    row_config.direction = luil::stack_direction::row;
    row_config.spacing = 8.0f;
    auto row { std::make_unique<luil::stack_element>(node_id(u8"row"), row_config) };
    auto more { std::make_unique<luil::text_button_element>(node_id(u8"more"), luil::text_button_config { .text = u8"하나 더" }) };
    more->set_cursor(luil::ui_cursor::hand);
    more->set_action(luil::ui_trigger::left_click, luil::make_message_action(markup_intent { u8"increment" }));
    row->add(std::move(more), 120.0f);
    luil::check_config list {};
    list.owner = u8"list";
    list.label = u8"목록 보기";
    list.checked = true;
    list.toggle = luil::make_message_action(markup_intent { u8"toggle" });
    row->add_flexible(std::make_unique<luil::check_element>(std::move(list)));
    screen->add(std::move(row), 30.0f);
    screen->add_gap(6.0f);
    luil::panel_config card {};
    card.corner_radius = 6.0f;
    card.background = [](const luil::ui_color_palette& palette) {
        return luil::palette_color(palette, luil::palette_role::surface_background);
    };
    auto panel { std::make_unique<luil::panel_element>(node_id(u8"card"), std::move(card)) };
    panel->set_content(std::make_unique<luil::progress_element>(node_id(u8"bar"), luil::progress_config { .value = 0.25f, .thickness = 4.0f }));
    screen->add(std::move(panel), 40.0f);
    luil::wrap_config chips_config {};
    chips_config.item_width = 40.0f;
    chips_config.item_height = 20.0f;
    chips_config.spacing = 4.0f;
    chips_config.line_spacing = 4.0f;
    auto chips { std::make_unique<luil::wrap_element>(node_id(u8"chips"), chips_config) };
    luil::label_config first {};
    first.text = u8"a";
    chips->add(std::make_unique<luil::label_element>(node_id(u8"w1"), std::move(first)));
    luil::label_config second {};
    second.text = u8"b";
    auto second_label { std::make_unique<luil::label_element>(node_id(u8"w2"), std::move(second)) };
    second_label->set_enabled(false);
    second_label->set_tooltip(u8"둘째");
    chips->add(std::move(second_label));
    screen->add_flexible(std::move(chips));
    const luil::ui_tree by_hand { arranged(std::move(screen), 360.0f, 280.0f) };

    REQUIRE(markup.unarranged().empty());
    REQUIRE(markup.duplicate_ids().empty());
    std::vector<std::pair<const luil::ui_element*, const luil::ui_element*>> pending { { markup.root(), by_hand.root() } };
    std::size_t compared { 0 };
    while (pending.empty() == false)
    {
        const auto [left, right] { pending.back() };
        pending.pop_back();
        ++compared;
        INFO("owner " << narrow(right->id().owner));
        REQUIRE(left->id() == right->id());
        REQUIRE(left->bounds().x == right->bounds().x);
        REQUIRE(left->bounds().y == right->bounds().y);
        REQUIRE(left->bounds().width == right->bounds().width);
        REQUIRE(left->bounds().height == right->bounds().height);
        REQUIRE(left->visible() == right->visible());
        REQUIRE(left->enabled() == right->enabled());
        REQUIRE(left->accessibility().role == right->accessibility().role);
        REQUIRE(left->accessibility().name == right->accessibility().name);
        REQUIRE(left->tooltip() == right->tooltip());
        REQUIRE(left->cursor() == right->cursor());
        REQUIRE((left->action(luil::ui_trigger::left_click) == nullptr) == (right->action(luil::ui_trigger::left_click) == nullptr));
        REQUIRE(left->children().size() == right->children().size());
        for (std::size_t index { left->children().size() }; index > 0; --index)
            pending.emplace_back(left->children()[index - 1].get(), right->children()[index - 1].get());
    }
    REQUIRE(compared == 10);
}

TEST_CASE("A markup panel background paints its palette role", "[ui][markup][binding][raster]")
{
    // 역할을 고르는 선택자라 테마와 고대비를 그대로 따른다. 자리는 물리 픽셀이다 (raster_probe.h).
    const auto document { template_of(u8"<stack id=\"root\"><panel id=\"card\" background=\"accentSoft\" length=\"30\"><label text=\"\"/></panel></stack>") };
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const luil::ui_color expected { luil::palette_color(palette, luil::palette_role::accent_soft) };
        INFO("theme " << static_cast<int>(theme));
        REQUIRE(expected != palette.window_background);
        const luil::ui_tree tree { luil::make_arranged_tree(luil::build_ui_markup(*document, {}).element, { 0.0f, 0.0f, 100.0f, 80.0f }, 2.0f) };
        luil::testing::raster_frame frame { 100, 80, palette };
        frame.set_theme(theme);
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(50, 30) == expected);
        REQUIRE(frame.pixel_at(50, 70) == palette.window_background);
    }
}

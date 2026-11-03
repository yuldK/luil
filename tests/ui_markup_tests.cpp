#include "luil/ui/ui_markup.h"

#include "luil/generated/ui_style_keys.h"
#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"
#include "ui/ui_markup_document.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// 화면 마크업의 어휘다 (docs/markup-design.md 어휘 절). 읽을 때 이름·enum·수·자리·id 꼴을 모두 확인하고, 적은 값은 C++ 값으로
// 바꿔 둔다. 펼치기(값 바인딩, 같은 화면 견줌, 견본 픽셀)는 바인딩 노드의 시험이다 (docs/markup-binding-design.md).
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

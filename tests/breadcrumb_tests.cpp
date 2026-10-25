#include "luil/ui/breadcrumb_element.h"

#include "host/font_registry.h"
#include "luil/text/grapheme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkFont.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {
    // 바이트마다 글자 크기의 절반 폭이다 (12px 글이면 6px).
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        return static_cast<float>(text.size()) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    struct went_intent
    {
        std::u8string key {};
    };

    struct collapsed_intent
    {
        std::vector<std::u8string> keys {};
    };

    // 조각 넷: "C:"(2) "Users"(5) "Documents"(9) "report"(6). 조각 하나의 폭은 글 + 12다.
    [[nodiscard]] luil::breadcrumb_config make_config()
    {
        luil::breadcrumb_config config {};
        config.owner = u8"path";
        config.segments = {
            { u8"c", u8"C:", true },
            { u8"users", u8"Users", true },
            { u8"documents", u8"Documents", true },
            { u8"report", u8"report", false },
        };
        config.navigate = [](const std::u8string& key) { return luil::make_app_action(went_intent { key }); };
        config.open_children = [](const std::u8string& key, const luil::ui_action_context&) { return luil::make_app_action(went_intent { u8"children:" + key }); };
        config.open_collapsed = [](const std::vector<std::u8string>& keys, const luil::ui_action_context&) { return luil::make_app_action(collapsed_intent { keys }); };
        config.name = u8"경로";
        config.current_label = u8"현재 위치";
        config.children_label = u8"하위 항목";
        config.collapsed_label = u8"앞 경로";
        return config;
    }

    [[nodiscard]] luil::measure_context measure()
    {
        return luil::measure_context { .scale = 1.0f, .measurer = &half_size_measurer };
    }

    // 왼쪽 누름의 액션을 실행한다.
    [[nodiscard]] std::vector<luil::input_action> click(const luil::ui_element& element)
    {
        const luil::ui_action* const action { element.action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)(luil::ui_action_context {});
    }

    template<typename intent_type>
    [[nodiscard]] const intent_type* intent_of(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr ? message->get<intent_type>() : nullptr;
    }
} // namespace

TEST_CASE("A breadcrumb folds leading segments as the width shrinks", "[ui][breadcrumb]")
{
    const luil::breadcrumb_config config { make_config() };
    // 접지 않은 폭: 조각 (2+5+9+6)*6 + 4*12 = 180, 꺾쇠는 아래 단계가 없는 마지막 조각 뒤를 빼고 셋 48 → 228.
    REQUIRE(luil::breadcrumb_element::collapsed_count(config, measure(), 228.0f) == 0u);
    // "C:"(24 + 꺾쇠 16)를 접으면 접은 단추(24 + 꺾쇠 16)가 같은 폭을 차지해 줄지 않는다. 그다음 수인 둘을 접는다
    // (40 + 66 + 16 + 48 = 170).
    REQUIRE(luil::breadcrumb_element::collapsed_count(config, measure(), 227.0f) == 2u);
    REQUIRE(luil::breadcrumb_element::collapsed_count(config, measure(), 170.0f) == 2u);
    // 셋을 접으면 40 + 48 = 88이다.
    REQUIRE(luil::breadcrumb_element::collapsed_count(config, measure(), 169.0f) == 3u);
    // 지금 자리는 끝까지 남는다.
    REQUIRE(luil::breadcrumb_element::collapsed_count(config, measure(), 10.0f) == 3u);
    // 조각이 하나면 접을 것이 없다.
    luil::breadcrumb_config single { make_config() };
    single.segments.resize(1);
    REQUIRE(luil::breadcrumb_element::collapsed_count(single, measure(), 1.0f) == 0u);
}

TEST_CASE("Breadcrumb parts navigate, open children and list the folded segments", "[ui][breadcrumb]")
{
    luil::breadcrumb_config config { make_config() };
    auto element { std::make_unique<luil::breadcrumb_element>(config) };
    luil::breadcrumb_element* const bar { element.get() };
    luil::arrange_context context {};
    context.slot = { 0.0f, 0.0f, 180.0f, 24.0f };
    context.measurer = &half_size_measurer;
    bar->arrange(context);
    const luil::ui_tree tree { std::move(element) };
    REQUIRE(bar->collapsed() == 2u);

    // 접은 조각은 숨고 접은 단추가 선다. 그 단추는 접힌 키를 앞에서부터 낸다.
    REQUIRE(tree.visibly_contains(luil::breadcrumb_segment_id(u8"path", u8"c")) == false);
    REQUIRE(tree.visibly_contains(luil::breadcrumb_segment_id(u8"path", u8"documents")));
    const luil::ui_element* const overflow { tree.find(luil::breadcrumb_collapsed_id(u8"path")) };
    REQUIRE(overflow != nullptr);
    REQUIRE(overflow->visible());
    // 메시지는 돌려받은 목록 안에 산다 — 목록을 붙잡아 두고 읽는다.
    const std::vector<luil::input_action> folded_actions { click(*overflow) };
    const collapsed_intent* const folded { intent_of<collapsed_intent>(folded_actions) };
    REQUIRE(folded != nullptr);
    REQUIRE(folded->keys == std::vector<std::u8string> { u8"c", u8"users" });

    // 조각은 그 키로 간다. 꺾쇠는 그 조각의 아래 단계를 연다.
    const luil::ui_element* const documents { tree.find(luil::breadcrumb_segment_id(u8"path", u8"documents")) };
    REQUIRE(intent_of<went_intent>(click(*documents))->key == u8"documents");
    const luil::ui_element* const chevron { tree.find(luil::breadcrumb_chevron_id(u8"path", u8"documents")) };
    REQUIRE(intent_of<went_intent>(click(*chevron))->key == u8"children:documents");
    // 지금 자리는 누를 것이 없고 이름에 상태를 함께 말한다.
    const luil::ui_element* const current { tree.find(luil::breadcrumb_segment_id(u8"path", u8"report")) };
    REQUIRE(current->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(current->accessibility().role == luil::access_role::link);
    REQUIRE(current->accessibility().name == u8"report · 현재 위치");
    REQUIRE(chevron->tooltip() == u8"Documents · 하위 항목");
    // 아래 단계가 없는 지금 자리에는 꺾쇠가 없다.
    REQUIRE(tree.find(luil::breadcrumb_chevron_id(u8"path", u8"report")) == nullptr);
    // 막대는 이름 있는 navigation이고 Tab의 한 자리다(←/→가 안을 돈다).
    REQUIRE(tree.root()->accessibility().role == luil::access_role::navigation);
    REQUIRE(tree.root()->accessibility().name == u8"경로");
    REQUIRE(tree.root()->focus_group() == luil::focus_axis::horizontal);
}

TEST_CASE("Start elision keeps the end of a path on a grapheme boundary", "[ui][breadcrumb][elide]")
{
    const SkFont font { luil::configured_ui_typeface(), 12.0f };
    const std::u8string path { u8"C:\\Users\\someone\\Documents\\report.txt" };
    // 넉넉하면 그대로다.
    REQUIRE(luil::elide_text_start(path, 10000.0f, font) == path);
    const float narrow { luil::measure_text(u8"…report.txt", font) + 0.5f };
    const std::u8string elided { luil::elide_text_start(path, narrow, font) };
    REQUIRE(elided.starts_with(u8"…"));
    REQUIRE(elided.ends_with(u8"report.txt"));
    REQUIRE(luil::measure_text(elided, font) <= narrow);
    // 조합형 한글 끝을 남겨도 글자소를 가르지 않는다.
    std::u8string hangul {};
    for (int index { 0 }; index < 10; ++index)
        hangul += u8"\u1112\u1161\u11AB";
    const std::u8string tail { luil::elide_text_start(hangul, luil::measure_text(hangul, font) / 2.0f, font) };
    const std::u8string_view kept { std::u8string_view { tail }.substr(std::u8string_view { u8"…" }.size()) };
    const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(hangul) };
    REQUIRE(std::find(boundaries.begin(), boundaries.end(), hangul.size() - kept.size()) != boundaries.end());
    // `…`조차 들어가지 않으면 빈 값이다.
    REQUIRE(luil::elide_text_start(path, 1.0f, font).empty());
}

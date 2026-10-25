#include "luil/ui/choice_group_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "raster_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
    struct select_intent
    {
        std::u8string value {};
    };

    [[nodiscard]] luil::choice_group_config make_config(const luil::choice_style style)
    {
        luil::choice_group_config config {};
        config.style = style;
        config.items = { { u8"latte", u8"라떼" }, { u8"mocha", u8"모카" }, { u8"drip", u8"드립" } };
        config.selected = u8"mocha";
        config.select = [](const std::u8string& value) { return luil::input_action { luil::app_message { select_intent { value } } }; };
        return config;
    }
} // namespace

TEST_CASE("A radio choice group stacks one row per value", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::radio) };
    REQUIRE(luil::choice_group_element::height_for(config) == 3.0f * luil::choice_radio_row_height);

    luil::choice_group_element group { std::move(config) };
    group.arrange({ { 0.0f, 100.0f, 300.0f, 999.0f }, 2.0f });
    REQUIRE(group.children().size() == 3);
    REQUIRE(group.bounds().height == 3.0f * luil::choice_radio_row_height * 2.0f);

    // 행은 위에서 아래로 쌓이고 폭을 다 쓴다.
    const luil::rect_f first { group.children()[0]->bounds() };
    const luil::rect_f second { group.children()[1]->bounds() };
    REQUIRE(first.y == 100.0f);
    REQUIRE(first.height == luil::choice_radio_row_height * 2.0f);
    REQUIRE(first.width == 300.0f);
    REQUIRE(second.y == first.y + first.height);
    REQUIRE(group.children()[0]->id() == luil::ui_element_id { luil::ui_element_kind::choice, u8"latte" });
}

TEST_CASE("A filled toggle choice group splits the slot width evenly inside its track", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::toggle) };
    config.width = luil::choice_width::fill;
    REQUIRE(luil::choice_group_element::height_for(config) == luil::choice_toggle_height);

    luil::choice_group_element group { std::move(config) };
    group.arrange({ { 0.0f, 0.0f, 308.0f, 999.0f }, 1.0f });
    // 트랙은 받은 폭 전부이고, 안쪽 여백 2와 칸 사이 2를 뺀 자리를 세 칸이 나눈다: (308 - 2*2 - 2*2) / 3 = 100.
    REQUIRE(group.bounds().width == 308.0f);
    REQUIRE(group.bounds().height == luil::choice_toggle_height);
    const luil::rect_f first { group.children()[0]->bounds() };
    const luil::rect_f second { group.children()[1]->bounds() };
    const luil::rect_f third { group.children()[2]->bounds() };
    REQUIRE(first.x == 2.0f);
    REQUIRE(first.y == 2.0f);
    REQUIRE(first.width == 100.0f);
    REQUIRE(first.height == luil::choice_toggle_height - 4.0f);
    REQUIRE(second.x == 104.0f);
    REQUIRE(third.x == 206.0f);
}

TEST_CASE("A fitted toggle choice group sizes every segment to the longest label", "[ui][choice]")
{
    // 글 한 바이트를 글자 크기의 절반 폭으로 재는 가짜다. 「라떼」·「모카」·「드립」은 UTF-8 6바이트라 12px에서 36이다.
    const luil::text_measurer half_width { [](const std::u8string_view text, const float pixel_size) { return static_cast<float>(text.size()) * pixel_size * 0.5f; } };
    luil::choice_group_config config { make_config(luil::choice_style::toggle) };
    config.items[0].label = u8"카페라떼";
    luil::choice_group_element group { config };

    // 가장 긴 글 「카페라떼」(12바이트 → 72)에 좌우 여백 14씩을 더한 100이 모든 칸의 폭이다.
    const float segment { 72.0f + luil::choice_segment_padding * 2.0f };
    const float track { segment * 3.0f + luil::choice_segment_gap * 2.0f + luil::choice_track_padding * 2.0f };
    const auto preferred { group.preferred_size({ .scale = 1.0f, .measurer = &half_width }, 999.0f) };
    REQUIRE(preferred.has_value());
    REQUIRE(preferred->width == track);
    REQUIRE(preferred->height == luil::choice_toggle_height);

    // 넓은 자리에서도 트랙만큼만 차지하고 왼쪽에 붙는다.
    group.arrange({ .slot = { 10.0f, 0.0f, 900.0f, 999.0f }, .scale = 1.0f, .measurer = &half_width });
    REQUIRE(group.bounds().x == 10.0f);
    REQUIRE(group.bounds().width == track);
    for (const auto& child : group.children())
        REQUIRE(child->bounds().width == segment);

    // 자리가 모자라면 트랙이 자리에 맞춰 줄고 칸이 같이 준다.
    group.arrange({ .slot = { 0.0f, 0.0f, 152.0f, 999.0f }, .scale = 1.0f, .measurer = &half_width });
    REQUIRE(group.bounds().width == 152.0f);
    REQUIRE(group.children()[0]->bounds().width == group.children()[2]->bounds().width);
    REQUIRE(group.children()[2]->bounds().x + group.children()[2]->bounds().width <= 152.0f - luil::choice_track_padding + 0.01f);
}

TEST_CASE("A toggle hit lands on the covering segment and gaps hit nothing", "[ui][choice][hit]")
{
    // 폭 308, 칸 100 + 사이 2, 안쪽 여백 2: latte 2..102, mocha 104..204, drip 206..306.
    luil::choice_group_config config { make_config(luil::choice_style::toggle) };
    config.width = luil::choice_width::fill;
    luil::choice_group_element group { std::move(config) };
    group.arrange({ { 0.0f, 0.0f, 308.0f, 999.0f }, 1.0f });
    const float middle { luil::choice_toggle_height / 2.0f };

    // 경계 바로 안쪽은 그 칸의 것이다.
    const luil::ui_element* const first { group.hit_test(101.0f, middle) };
    REQUIRE(first != nullptr);
    REQUIRE(first->id().owner == u8"latte");
    const luil::ui_element* const second { group.hit_test(105.0f, middle) };
    REQUIRE(second != nullptr);
    REQUIRE(second->id().owner == u8"mocha");

    // 칸 사이 틈은 아무의 것도 아니다.
    // 그룹은 위에 뜨는 표면이 아니라 흡수하지 않는다 — 뒤 배치가 받는 것이 맞다.
    REQUIRE(group.hit_test(103.0f, middle) == nullptr);
}

TEST_CASE("Only the selected segment floats a thumb brighter than the track", "[ui][choice][raster]")
{
    // 고른 칸이 트랙보다 어두우면 눌려 들어가 꺼진 것으로 읽힌다. 라이트는 판이 불투명한 흰색이라 픽셀이 그대로 남는다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::light) };
    luil::choice_group_config config { make_config(luil::choice_style::toggle) };
    config.width = luil::choice_width::fill;
    for (luil::choice_item& item : config.items)
        item.label.clear();
    // 자리는 물리 픽셀이다 (raster_probe.h의 규약). 배율 2: 칸은 4..204, 208..408, 412..612, 위아래 4..48.
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::choice_group_element>(std::move(config)), { 0.0f, 0.0f, 616.0f, 52.0f }, 2.0f) };
    luil::testing::raster_frame frame { 616, 52, palette };
    frame.draw(tree, luil::interaction_snapshot {});
    // 판 가운데(테에서 먼 자리)다.
    REQUIRE(frame.pixel_at(308, 26) == palette.thumb_background);
    REQUIRE(frame.pixel_at(104, 26) != palette.thumb_background);
    REQUIRE(frame.pixel_at(512, 26) == frame.pixel_at(104, 26));
}

TEST_CASE("A choice click carries its value in the select message", "[ui][choice]")
{
    const luil::choice_group_element group { make_config(luil::choice_style::radio) };
    const luil::ui_element* const drip { group.children()[2].get() };
    REQUIRE(drip->cursor() == luil::ui_cursor::hand);

    const luil::ui_action* const action { drip->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    auto actions { (*action)({ drip->id(), 0.0f, 0.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<select_intent>()->value == u8"drip");

    // 토글 묶음도 같은 메시지를 낸다.
    const luil::choice_group_element toggles { make_config(luil::choice_style::toggle) };
    const luil::ui_element* const latte { toggles.children()[0].get() };
    auto toggled { (*latte->action(luil::ui_trigger::left_click))({ latte->id(), 0.0f, 0.0f }) };
    auto* const toggle_message { std::get_if<luil::app_message>(&toggled[0]) };
    REQUIRE(toggle_message != nullptr);
    REQUIRE(toggle_message->get<select_intent>()->value == u8"latte");
}

TEST_CASE("A choice group without a select factory builds inert choices", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::radio) };
    config.select = nullptr;
    const luil::choice_group_element group { std::move(config) };
    REQUIRE(group.children().size() == 3);
    REQUIRE(group.children()[0]->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(group.children()[0]->interactive() == false);
}

TEST_CASE("A choice group is one tab stop that enters at the selected value", "[ui][choice][focus]")
{
    // 라디오는 세로로 쌓이고 토글은 가로로 늘어선다 — 방향은 style이 이미 안다.
    luil::choice_group_element radio { make_config(luil::choice_style::radio) };
    REQUIRE(radio.focus_group() == luil::focus_axis::vertical);
    luil::choice_group_element toggle { make_config(luil::choice_style::toggle) };
    REQUIRE(toggle.focus_group() == luil::focus_axis::horizontal);

    auto group { std::make_unique<luil::choice_group_element>(make_config(luil::choice_style::radio)) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(group), { 0.0f, 0.0f, 300.0f, 200.0f }, 1.0f) };
    // 묶음은 자리 하나로 접히고, 그 자리는 선택된 값이다.
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { luil::ui_element_id { luil::ui_element_kind::choice, u8"mocha" } });
}

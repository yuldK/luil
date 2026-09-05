#include "luil/ui/dialog_elements.h"

#include "luil/generated/codicons.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <utility>
#include <vector>

// dialog 캡션은 "없는 것은 두지 않는다"는 규약으로 조립된다.
// 비어 있는 설정 항목이 그 부분을 아예 만들지 않는지, 그리고 잡아 끈 양이
// 논리 픽셀로 나오는지를 못박는다.

TEST_CASE("A dialog caption omits the parts that were not configured", "[ui][dialog]")
{
    luil::dialog_caption_config config {};
    config.title = u8"제목";
    // 정적 사이저는 논리 픽셀이다 (다른 element의 height_for와 같은 단위).
    REQUIRE(luil::dialog_caption_element::height_for(config) == 34.0f);
    luil::dialog_caption_element caption { std::move(config) };
    caption.arrange({ { 0.0f, 0.0f, 200.0f, 0.0f }, 1.0f });

    // 높이는 설정이 정하고 slot의 높이는 무시한다.
    REQUIRE(caption.bounds().height == 34.0f);
    // 닫기 액션이 없으면 버튼도 없고, 이동 factory가 없으면 잡아도 움직이지 않는다.
    REQUIRE(caption.children().empty());
    REQUIRE(caption.pointer_drag() == nullptr);
}

TEST_CASE("A dialog caption puts the close button above its own drag area", "[ui][dialog]")
{
    bool closed { false };
    luil::dialog_caption_config config {};
    config.close = [&closed](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        closed = true;
        return {};
    };
    config.move = [](const float, const float) { return luil::input_action {}; };
    luil::dialog_caption_element caption { std::move(config) };
    caption.arrange({ { 0.0f, 0.0f, 200.0f, 0.0f }, 1.0f });

    REQUIRE(caption.children().size() == 1u);
    const luil::ui_element* const button { caption.hit_test(180.0f, 17.0f) };
    REQUIRE(button != nullptr);
    REQUIRE(button->id().kind == luil::ui_element_kind::dialog_caption_close);

    const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    static_cast<void>((*action)(luil::ui_action_context { button->id(), 180.0f, 17.0f, false }));
    REQUIRE(closed);

    // 버튼 밖의 캡션은 캡션 자신이 받는다.
    //  - 이 자리가 잡아 끄는 손잡이다.
    const luil::ui_element* const bar { caption.hit_test(20.0f, 17.0f) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->id().kind == luil::ui_element_kind::dialog_caption);
}

TEST_CASE("A dialog caption reports drag deltas in logical pixels", "[ui][dialog]")
{
    float moved_x { 0.0f };
    float moved_y { 0.0f };
    luil::dialog_caption_config config {};
    config.move = [&moved_x, &moved_y](const float dx, const float dy) {
        moved_x = dx;
        moved_y = dy;
        return luil::input_action {};
    };
    luil::dialog_caption_element caption { std::move(config) };
    caption.arrange({ { 0.0f, 0.0f, 200.0f, 0.0f }, 2.0f });
    REQUIRE(caption.bounds().height == 68.0f);

    const luil::pointer_drag_target* const target { caption.pointer_drag() };
    REQUIRE(target != nullptr);
    const luil::ui_action_context previous { caption.id(), 10.0f, 10.0f, false };
    const luil::ui_action_context current { caption.id(), 30.0f, 40.0f, false };

    // 배율 2에서 물리 20·30px 이동은 논리 10·15px이다.
    const std::vector<luil::input_action> actions { target->on_move(previous, current) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(moved_x == 10.0f);
    REQUIRE(moved_y == 15.0f);

    // 좌표가 그대로면 메시지를 내지 않는다.
    REQUIRE(target->on_move(current, current).empty());
}

TEST_CASE("A dialog caption asks for the grab cursor only when it can move", "[ui][dialog][cursor]")
{
    const luil::interaction_snapshot idle {};

    luil::dialog_caption_config fixed {};
    fixed.title = u8"제목";
    const luil::dialog_caption_element still { std::move(fixed) };
    REQUIRE(still.cursor_at(10.0f, 10.0f, idle) == luil::ui_cursor::inherit);

    luil::dialog_caption_config movable {};
    movable.move = [](const float, const float) { return luil::input_action {}; };
    luil::dialog_caption_element caption { std::move(movable) };
    caption.arrange({ { 0.0f, 0.0f, 200.0f, 0.0f }, 1.0f });
    REQUIRE(caption.cursor_at(10.0f, 10.0f, idle) == luil::ui_cursor::grab);

    // 눌린 채 끄는 동안에는 잡은 모양이다.
    luil::interaction_snapshot holding {};
    holding.pressed = caption.id();
    REQUIRE(caption.cursor_at(10.0f, 10.0f, holding) == luil::ui_cursor::grabbing);
}

TEST_CASE("A text input measures IME span queries against the queried document", "[ui][dialog][ime]")
{
    constexpr luil::ui_element_kind kind_query { luil::application_element_kind(0) };
    // element는 확정 글이 비어 있다 — 조합이 막 시작된 상태다.
    luil::text_input_element input { { kind_query, u8"query" }, luil::text_input_view {}, luil::text_input_config {} };
    input.arrange({ { 100.0f, 10.0f, 214.0f, 20.0f }, 1.0f });

    // byte 하나가 10px인 가짜 측정기다.
    const luil::text_measurer measure { [](const std::u8string_view text, float) { return static_cast<float>(text.size()) * 10.0f; } };

    // 조합 문서 "abcdef"의 [2, 4) 구간 — 확정 글(0바이트)이 아니라
    // 질의한 문서로 재야 한다. 왼쪽 여백 7px + 2글자 = x 127.
    const auto span { input.text_span_bounds({ u8"abcdef", 6u, 2u, 4u }, measure) };
    REQUIRE(span.has_value());
    REQUIRE(span->x == 127.0f);
    REQUIRE(span->width == 20.0f);
    REQUIRE(span->y == 10.0f);
    REQUIRE(span->height == 20.0f);

    // 문서 밖 offset은 끝으로 잘린다.
    const auto clamped { input.text_span_bounds({ u8"ab", 2u, 10u, 20u }, measure) };
    REQUIRE(clamped.has_value());
    REQUIRE(clamped->x == 127.0f);
    REQUIRE(clamped->width == 0.0f);

    // 글이 칸보다 길면 caret이 보이도록 민 가로 스크롤이 반영된다.
    // 안쪽 폭 200px, 글 30바이트 = 300px, caret 끝 → 스크롤 100px.
    const auto scrolled { input.text_span_bounds({ u8"012345678901234567890123456789", 30u, 30u, 30u }, measure) };
    REQUIRE(scrolled.has_value());
    REQUIRE(scrolled->x == 100.0f + 7.0f - 100.0f + 300.0f);
}

TEST_CASE("A text input announces the caret flip only while focused", "[ui][dialog][update]")
{
    constexpr luil::ui_element_kind kind_query { luil::application_element_kind(0) };
    const luil::text_input_element input { { kind_query, u8"query" }, luil::text_input_view {}, luil::text_input_config {} };

    // 초점이 없으면 caret도 없어 예고할 것이 없다.
    const std::chrono::steady_clock::time_point focused_at { std::chrono::seconds { 100 } };
    const luil::update_context context { focused_at + luil::caret_blink_interval / 4 };
    REQUIRE(input.next_update(context, {}).has_value() == false);

    // 초점을 받은 동안은 다음 반주기 경계를 예고한다 (위상은 초점 시각 기준).
    luil::interaction_snapshot focused {};
    focused.focused_input = input.id();
    focused.focus_started_at = focused_at;
    REQUIRE(input.next_update(context, focused) == focused_at + luil::caret_blink_interval);

    // 다른 박스의 초점은 내 caret이 아니다.
    focused.focused_input = { kind_query, u8"other" };
    REQUIRE(input.next_update(context, focused).has_value() == false);
}

TEST_CASE("A text input reserves room for the glyph and the clear button", "[ui][dialog]")
{
    // 담는 쪽이 칸 폭을 정하므로(측정 단계가 없다) 아이콘이 먹는 폭을 알려 준다.
    REQUIRE(luil::text_input_element::leading_width_for(luil::text_input_config {}) == 0.0f);
    REQUIRE(luil::text_input_element::trailing_width_for(luil::text_input_config {}) == 0.0f);

    luil::text_input_config searchable {};
    searchable.leading_glyph = luil::codicons::icon_search;
    searchable.clear = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    REQUIRE(luil::text_input_element::leading_width_for(searchable) == luil::text_input_glyph_size + luil::text_input_icon_gap);
    REQUIRE(luil::text_input_element::trailing_width_for(searchable) == luil::text_input_clear_size + luil::text_input_icon_gap);
}

TEST_CASE("A text input makes the clear button only when the app gives an action", "[ui][dialog]")
{
    constexpr luil::ui_element_kind kind_query { luil::application_element_kind(0) };

    // 없는 것은 두지 않는다 — 액션이 없으면 버튼도 없다.
    const luil::text_input_element plain { { kind_query, u8"query" }, luil::text_input_view {}, luil::text_input_config {} };
    REQUIRE(plain.children().empty());

    luil::text_input_config config {};
    config.clear = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    const luil::text_input_element clearable { { kind_query, u8"query" }, luil::text_input_view {}, std::move(config) };
    REQUIRE(clearable.children().size() == 1u);
    const luil::ui_element& button { *clearable.children()[0] };
    // 칸의 owner를 그대로 물려받아 칸이 여럿이어도 갈린다.
    REQUIRE(button.id() == luil::ui_element_id { luil::ui_element_kind::text_input_clear, u8"query" });
    // 칸 안의 보조 버튼이다. Tab의 자리는 칸이지 이 버튼이 아니다.
    REQUIRE(button.tab_stop() == false);
}

TEST_CASE("A leading glyph moves where the pointer lands in the text", "[ui][dialog]")
{
    // 그리기·offset_at·IME 질의가 같은 inner_width를 쓰는지를 hit test 쪽에서 본다.
    // byte 하나가 10px인 가짜 측정기다.
    constexpr luil::ui_element_kind kind_query { luil::application_element_kind(0) };
    const luil::text_measurer measure { [](const std::u8string_view text, float) { return static_cast<float>(text.size()) * 10.0f; } };
    luil::text_input_view value {};
    value.text = u8"abcdef";

    luil::text_input_element plain { { kind_query, u8"query" }, value, luil::text_input_config {} };
    plain.arrange({ { 100.0f, 10.0f, 214.0f, 20.0f }, 1.0f });
    // 글 시작은 x + 7이고 거기서 25px 오른쪽은 두 경계 사이의 한가운데다.
    REQUIRE(plain.offset_at(100.0f + 7.0f + 25.0f, measure) == 3u);

    luil::text_input_config searchable {};
    searchable.leading_glyph = luil::codicons::icon_search;
    luil::text_input_element searched { { kind_query, u8"query" }, std::move(value), std::move(searchable) };
    searched.arrange({ { 100.0f, 10.0f, 214.0f, 20.0f }, 1.0f });
    // 글이 글리프 칸(14 + 5)만큼 오른쪽으로 밀렸으므로 같은 좌표가 앞쪽 글자를 가리킨다.
    REQUIRE(searched.offset_at(100.0f + 7.0f + 25.0f, measure) == 1u);
}
TEST_CASE("Long UTF-8 input hit testing measures logarithmically many prefixes", "[ui][dialog]")
{
    luil::text_input_view value {};
    for (int index = 0; index < 4096; ++index)
        value.text += u8"가";
    std::size_t calls {};
    const luil::text_measurer measure { [&](const std::u8string_view text, float) {
        ++calls;
        return static_cast<float>(text.size());
    }
    };
    luil::text_input_element input { { luil::application_element_kind(0), u8"long" }, value, {} };
    input.arrange({ { 0.0f, 0.0f, 20000.0f, 24.0f }, 1.0f });
    REQUIRE(input.offset_at(7.0f + 6001.0f, measure) == 6000u);
    REQUIRE(calls < 32u);
}

TEST_CASE("Text hit testing keeps zero-width marks with the chosen boundary", "[ui][dialog]")
{
    luil::text_input_view value {};
    value.text = u8"ab~c";
    const luil::text_measurer measure { [](const std::u8string_view text, float) { return static_cast<float>(text.size() - (text.find(u8'~') != std::u8string_view::npos ? 1 : 0)); } };
    luil::text_input_element input { { luil::application_element_kind(0), u8"marks" }, value, {} };
    input.arrange({ { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f });
    REQUIRE(input.offset_at(7.0f + 1.6f, measure) == 3u);
}

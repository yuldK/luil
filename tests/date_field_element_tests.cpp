#include "luil/ui/calendar_element.h"

#include "luil/app/date_picker_popup.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/dropdown_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 날짜 칸과 달력 popup이다 (docs/date-picker-design.md). 칸은 필드 popup 열림 규약을 따르고 popup은 크기와 칸 아래 자리를 스스로 정한다.
namespace {
    using std::chrono::year_month_day;

    struct open_intent
    {
        bool open { false };
    };

    class panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] year_month_day date(const int year, const unsigned month, const unsigned day)
    {
        return year_month_day { std::chrono::year { year }, std::chrono::month { month }, std::chrono::day { day } };
    }

    [[nodiscard]] luil::date_field_config make_field(const bool open)
    {
        luil::date_field_config config {};
        config.owner = u8"due";
        config.value = date(2026, 11, 3);
        config.open = open;
        config.set_open = [](const bool target) { return luil::make_app_action(open_intent { target }); };
        return config;
    }

    [[nodiscard]] std::vector<bool> opens_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<bool> values {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const open_intent* const intent { message->get<open_intent>() }; intent != nullptr)
                    values.push_back(intent->open);
        return values;
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool alt = false)
    {
        luil::key_pressed_event event {};
        event.key = code;
        event.alt = alt;
        return event;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> field_tree(const bool open)
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f });
        auto field { std::make_unique<luil::date_field_element>(make_field(open)) };
        field->arrange({ { 0.0f, 0.0f, 160.0f, luil::dropdown_height }, 1.0f });
        root->add(std::move(field));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::calendar_config make_calendar_config()
    {
        luil::calendar_config config {};
        config.owner = u8"due-calendar";
        config.value = date(2026, 11, 3);
        config.cursor = date(2026, 11, 3);
        config.select = [](const year_month_day) { return luil::make_app_action(open_intent { false }); };
        config.move_to = [](const year_month_day) { return luil::input_action {}; };
        return config;
    }

    [[nodiscard]] bool same_rect(const luil::rect_f& first, const luil::rect_f& second)
    {
        return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
    }
}

TEST_CASE("The date field opens with a click, Alt down, F4 and down and closes when open", "[ui][date-picker][date-field]")
{
    const luil::date_field_element closed { make_field(false) };
    REQUIRE(closed.id() == luil::ui_element_id { luil::ui_element_kind::date_field, u8"due" });
    REQUIRE(closed.cursor() == luil::ui_cursor::hand);
    REQUIRE(opens_of((*closed.action(luil::ui_trigger::left_click))({ closed.id() })) == std::vector<bool> { true });
    const auto& handler { *closed.key_handler_of() };
    REQUIRE(opens_of(*handler(key(luil::key_code::arrow_down, true))) == std::vector<bool> { true });
    REQUIRE(opens_of(*handler(key(luil::key_code::f4))) == std::vector<bool> { true });
    REQUIRE(opens_of(*handler(key(luil::key_code::arrow_down))) == std::vector<bool> { true });
    REQUIRE(handler(key(luil::key_code::arrow_up)).has_value() == false);
    // 닫혀 있으면 떠남 훅이 없다.
    REQUIRE(closed.focus_leave_action() == nullptr);

    const luil::date_field_element open { make_field(true) };
    REQUIRE(opens_of((*open.action(luil::ui_trigger::left_click))({ open.id() })) == std::vector<bool> { false });
    // 열린 동안 여는 키는 삼킨다.
    const std::optional<std::vector<luil::input_action>> swallowed { (*open.key_handler_of())(key(luil::key_code::f4)) };
    REQUIRE(swallowed.has_value());
    REQUIRE(swallowed->empty());
    REQUIRE(open.focus_leave_action() != nullptr);
    REQUIRE(opens_of((*open.focus_leave_action())({ open.id() })) == std::vector<bool> { false });

    // 여는 메시지가 없으면 눌리지 않고 보조 기술도 열지 못한다.
    luil::date_field_config inert { make_field(false) };
    inert.set_open = nullptr;
    const luil::date_field_element still { inert };
    REQUIRE(still.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(still.key_handler_of() == nullptr);
    REQUIRE(still.access_actions({ luil::access_command::expand }).has_value() == false);
}

TEST_CASE("The date field reads as a combo box whose value is the shown text", "[ui][date-picker][date-field]")
{
    const luil::date_field_element closed { make_field(false) };
    const luil::access_info info { closed.accessibility() };
    REQUIRE(info.role == luil::access_role::combo_box);
    REQUIRE(info.name == u8"날짜");
    REQUIRE(info.expanded == false);
    REQUIRE(info.value == u8"2026년 11월 3일");
    REQUIRE(opens_of(*closed.access_actions({ luil::access_command::expand })) == std::vector<bool> { true });
    REQUIRE(opens_of(*closed.access_actions({ luil::access_command::collapse })) == std::vector<bool> { false });

    // 앱이 준 글 꼴과 이름이다.
    luil::date_field_config custom { make_field(true) };
    custom.format = [](const year_month_day day) { return luil::format_iso_date(day); };
    custom.name = u8"due date";
    const luil::date_field_element shown { custom };
    REQUIRE(shown.accessibility().value == u8"2026-11-03");
    REQUIRE(shown.accessibility().name == u8"due date");
    REQUIRE(shown.accessibility().expanded == true);

    // 값이 없거나 틀리면 값 글이 비어 있다. placeholder는 값이 아니다.
    luil::date_field_config empty { make_field(false) };
    empty.value.reset();
    REQUIRE(luil::date_field_element { empty }.accessibility().value.empty());
    empty.value = date(2026, 2, 30);
    REQUIRE(luil::date_field_element { empty }.accessibility().value.empty());
}

TEST_CASE("Pressing the open date field and releasing without a new frame closes it", "[ui][date-picker][date-field][reopen]")
{
    luil::interaction_controller controller {};
    controller.set_tree(field_tree(true));
    std::vector<bool> opens { opens_of(controller.process(luil::pointer_pressed_event { 40.0f, 14.0f, luil::pointer_button::left, at(0) })) };
    for (const bool value : opens_of(controller.process(luil::pointer_released_event { 40.0f, 14.0f, luil::pointer_button::left, at(10) })))
        opens.push_back(value);
    REQUIRE(opens == std::vector<bool> { false });

    // 바깥 누름이 popup을 닫고 닫힌 frame이 누름보다 먼저 게시돼도 펼쳐 있던 칸의 누름이라 닫힌다 (X-4).
    controller.set_tree(field_tree(true));
    controller.set_tree(field_tree(false));
    luil::pointer_pressed_event closing { 40.0f, 14.0f, luil::pointer_button::left, at(1000) };
    closing.closed_popup = true;
    opens = opens_of(controller.process(closing));
    for (const bool value : opens_of(controller.process(luil::pointer_released_event { 40.0f, 14.0f, luil::pointer_button::left, at(1010) })))
        opens.push_back(value);
    REQUIRE(opens == std::vector<bool> { false });
}

TEST_CASE("The date picker popup is the calendar size plus padding placed under the field", "[ui][date-picker][popup]")
{
    luil::date_picker_popup_config config {};
    config.id = u8"date-popup";
    config.scale = 2.0f;
    config.below = luil::field_popup_anchor { .field = { 100.0f, 50.0f, 160.0f, 56.0f }, .limit = { 0.0f, 0.0f, 1600.0f, 1600.0f } };
    config.dismiss = [](const luil::popup_dismiss_reason) { return luil::make_app_action(open_intent { false }); };
    const luil::ui_popup popup { luil::make_date_picker_popup(make_calendar_config(), config) };
    REQUIRE(popup.id == u8"date-popup");
    // 달력 224 x 248에 여백 8을 두른다.
    REQUIRE(popup.width == 240.0f);
    REQUIRE(popup.height == 264.0f);
    // 필드 아래 간격 2(논리)다. 좌표는 논리 픽셀이다.
    REQUIRE(popup.x == 50.0f);
    REQUIRE(popup.y == 55.0f);
    REQUIRE(popup.dismiss != nullptr);
    REQUIRE(popup.tree != nullptr);
    // tree는 배율 2로 배치된다. 달력은 여백 8 안쪽이고 3일은 첫 주 셋째 칸이다 (1일이 일요일).
    const luil::ui_element* const calendar { popup.tree->find({ luil::ui_element_kind::calendar, u8"due-calendar" }) };
    REQUIRE(calendar != nullptr);
    REQUIRE(same_rect(calendar->bounds(), { 16.0f, 16.0f, 448.0f, 496.0f }));
    const luil::ui_element* const cursor { popup.tree->find(luil::calendar_element::day_id(u8"due-calendar", date(2026, 11, 3))) };
    REQUIRE(cursor != nullptr);
    REQUIRE(same_rect(cursor->bounds(), { 16.0f + 2.0f * 64.0f, 16.0f + 112.0f, 64.0f, 64.0f }));
    REQUIRE(cursor->focusable());
    // 바탕 판이 여백의 누름을 흡수한다.
    REQUIRE(popup.tree->hit_test(4.0f, 4.0f) != nullptr);

    // 아래가 모자라고 위가 들어가면 위로 뒤집는다.
    config.below.field = { 100.0f, 1400.0f, 160.0f, 56.0f };
    const luil::ui_popup above { luil::make_date_picker_popup(make_calendar_config(), config) };
    REQUIRE(above.y == (1400.0f - 4.0f - 264.0f * 2.0f) / 2.0f);

    // 터치 칸 44면 popup도 커진다. 배율이 틀리면 1로 본다.
    luil::calendar_config touch { make_calendar_config() };
    touch.cell_size = 44.0f;
    config.scale = 0.0f;
    config.below.field = { 100.0f, 50.0f, 160.0f, 28.0f };
    const luil::ui_popup large { luil::make_date_picker_popup(touch, config) };
    REQUIRE(large.width == 308.0f + 16.0f);
    REQUIRE(large.y == 50.0f + 28.0f + 2.0f);
}

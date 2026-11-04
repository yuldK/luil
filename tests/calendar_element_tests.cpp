#include "luil/ui/calendar_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 달력이다 (docs/date-picker-design.md). 자식의 자리와 키 표는 요소를 직접 배치해 보고, 초점·Tab·Enter는 controller에 실제
// 이벤트를 넣어 본다.
namespace {
    using std::chrono::year_month_day;

    struct select_intent
    {
        year_month_day day {};
    };

    struct move_intent
    {
        year_month_day day {};
    };

    struct turn_intent
    {
        year_month_day day {};
    };

    struct default_intent
    {
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

    // 2026년 11월이다. 1일이 일요일이라 일요일 시작 격자의 첫 칸이 1일이다. 커서는 10일(화요일), 고른 날과 오늘은 3일이다.
    [[nodiscard]] luil::calendar_config make_config()
    {
        luil::calendar_config config {};
        config.owner = u8"trip";
        config.value = date(2026, 11, 3);
        config.cursor = date(2026, 11, 10);
        config.today = date(2026, 11, 3);
        config.select = [](const year_month_day day) { return luil::make_app_action(select_intent { day }); };
        config.move_to = [](const year_month_day day) { return luil::make_app_action(move_intent { day }); };
        return config;
    }

    [[nodiscard]] bool weekday_only(const year_month_day day)
    {
        const std::chrono::weekday weekday { std::chrono::sys_days { day } };
        return weekday != std::chrono::Saturday && weekday != std::chrono::Sunday;
    }

    // 슬롯은 (10, 20)에서 시작한다. 배율 1이면 머리 줄은 y 20..52, 요일 줄은 52..76, 격자는 (10, 76)부터 칸 32다.
    constexpr luil::rect_f slot { 10.0f, 20.0f, 400.0f, 600.0f };

    [[nodiscard]] std::unique_ptr<luil::calendar_element> make_calendar(luil::calendar_config config, const luil::rect_f& area = slot, const float scale = 1.0f)
    {
        auto calendar { std::make_unique<luil::calendar_element>(std::move(config)) };
        calendar->arrange({ area, scale });
        return calendar;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(std::unique_ptr<luil::ui_element> content, std::unique_ptr<luil::ui_element> extra = nullptr)
    {
        // 내용은 이미 배치했다. root는 자리만 잡고 자식을 다시 배치하지 않는다.
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 600.0f, 700.0f }, 1.0f });
        root->add(std::move(content));
        if (extra != nullptr)
            root->add(std::move(extra));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] std::u8string part(const std::u8string_view name)
    {
        std::u8string owner { u8"trip" };
        owner += u8'\u001f';
        owner += name;
        return owner;
    }

    const luil::ui_element_id previous_id { luil::ui_element_kind::calendar_nav, part(u8"prev") };
    const luil::ui_element_id next_id { luil::ui_element_kind::calendar_nav, part(u8"next") };
    const luil::ui_element_id grid_id { luil::ui_element_kind::calendar_grid, u8"trip" };

    [[nodiscard]] luil::ui_element_id day_of(const year_month_day day)
    {
        return luil::calendar_element::day_id(u8"trip", day);
    }

    [[nodiscard]] const luil::ui_element* search(const luil::ui_element& root, const luil::ui_element_id& id)
    {
        if (root.id() == id)
            return &root;
        for (const auto& child : root.children())
            if (const luil::ui_element* const found { search(*child, id) }; found != nullptr)
                return found;
        return nullptr;
    }

    [[nodiscard]] const luil::ui_element& find(const luil::ui_element& root, const luil::ui_element_id& id)
    {
        const luil::ui_element* const found { search(root, id) };
        REQUIRE(found != nullptr);
        return *found;
    }

    template<typename Intent>
    [[nodiscard]] std::vector<year_month_day> days_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<year_month_day> days {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const Intent* const intent { message->get<Intent>() }; intent != nullptr)
                    days.push_back(intent->day);
        return days;
    }

    // 메시지의 차례를 글로 본다. "move 2026-11-12"처럼 낱말과 ISO 날짜다.
    [[nodiscard]] std::vector<std::u8string> messages_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> names {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            REQUIRE(message != nullptr);
            if (const select_intent* const intent { message->get<select_intent>() }; intent != nullptr)
                names.push_back(u8"select " + luil::format_iso_date(intent->day));
            else if (const move_intent* const moved { message->get<move_intent>() }; moved != nullptr)
                names.push_back(u8"move " + luil::format_iso_date(moved->day));
            else if (const turn_intent* const turned { message->get<turn_intent>() }; turned != nullptr)
                names.push_back(u8"turn " + luil::format_iso_date(turned->day));
            else
                names.push_back(u8"other");
        }
        return names;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int time)
    {
        return luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int time)
    {
        return luil::pointer_released_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool shift = false, const bool control = false, const bool alt = false)
    {
        luil::key_pressed_event event {};
        event.key = code;
        event.shift = shift;
        event.control = control;
        event.alt = alt;
        return event;
    }

    [[nodiscard]] std::optional<std::vector<luil::input_action>> pressed(const luil::ui_element& grid, const luil::key_pressed_event& event)
    {
        REQUIRE(grid.key_handler_of() != nullptr);
        return (*grid.key_handler_of())(event);
    }

    [[nodiscard]] std::vector<luil::input_action> click(const luil::ui_element& element)
    {
        REQUIRE(element.action(luil::ui_trigger::left_click) != nullptr);
        return (*element.action(luil::ui_trigger::left_click))({ element.id() });
    }

    [[nodiscard]] bool same_rect(const luil::rect_f& first, const luil::rect_f& second)
    {
        return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
    }
}

TEST_CASE("The calendar builds its buttons, its grid and 42 days with ISO owners", "[ui][calendar]")
{
    const auto calendar { make_calendar(make_config()) };
    REQUIRE(calendar->id() == luil::ui_element_id { luil::ui_element_kind::calendar, u8"trip" });
    REQUIRE(calendar->children().size() == 3u);
    REQUIRE(calendar->children()[0]->id() == previous_id);
    REQUIRE(calendar->children()[1]->id() == next_id);
    const luil::ui_element& grid { *calendar->children()[2] };
    REQUIRE(grid.id() == grid_id);
    REQUIRE(grid.children().size() == 42u);
    REQUIRE(grid.children().front()->id() == luil::ui_element_id { luil::ui_element_kind::calendar_day, part(u8"2026-11-01") });
    REQUIRE(grid.children().back()->id() == luil::ui_element_id { luil::ui_element_kind::calendar_day, part(u8"2026-12-12") });
    REQUIRE(day_of(date(2026, 11, 3)) == luil::ui_element_id { luil::ui_element_kind::calendar_day, part(u8"2026-11-03") });
    REQUIRE(calendar->cursor() == date(2026, 11, 10));

    // 크기는 논리 상수의 합이다. 폭 7칸, 높이는 머리 줄 한 칸과 요일 줄과 6주다.
    REQUIRE(luil::calendar_element::size_for(make_config()).width == 224.0f);
    REQUIRE(luil::calendar_element::size_for(make_config()).height == 248.0f);
    luil::calendar_config touch { make_config() };
    touch.cell_size = 44.0f;
    REQUIRE(luil::calendar_element::size_for(touch).width == 308.0f);
    REQUIRE(luil::calendar_element::size_for(touch).height == 44.0f * 7.0f + luil::calendar_weekday_height);

    // 자리가 넓으면 왼쪽 위에 붙인다. 단추는 머리 줄 양 끝이고 10일은 둘째 주 셋째 칸이다.
    REQUIRE(same_rect(calendar->bounds(), { 10.0f, 20.0f, 224.0f, 248.0f }));
    REQUIRE(same_rect(find(*calendar, previous_id).bounds(), { 10.0f, 20.0f, 32.0f, 32.0f }));
    REQUIRE(same_rect(find(*calendar, next_id).bounds(), { 202.0f, 20.0f, 32.0f, 32.0f }));
    REQUIRE(same_rect(grid.bounds(), { 10.0f, 76.0f, 224.0f, 192.0f }));
    REQUIRE(same_rect(find(*calendar, day_of(date(2026, 11, 10))).bounds(), { 74.0f, 108.0f, 32.0f, 32.0f }));

    // 배율 2에서는 물리 픽셀로 두 배다.
    const auto doubled { make_calendar(make_config(), { 0.0f, 0.0f, 1000.0f, 1000.0f }, 2.0f) };
    REQUIRE(same_rect(doubled->bounds(), { 0.0f, 0.0f, 448.0f, 496.0f }));
    REQUIRE(same_rect(find(*doubled, day_of(date(2026, 11, 10))).bounds(), { 128.0f, 176.0f, 64.0f, 64.0f }));

    // 월요일 시작이면 첫 칸이 10월 26일이다.
    luil::calendar_config monday { make_config() };
    monday.first_day = std::chrono::Monday;
    const auto shifted { make_calendar(monday) };
    REQUIRE(shifted->children()[2]->children().front()->id() == day_of(date(2026, 10, 26)));
}

TEST_CASE("Each key in the table sends the cursor it moves to", "[ui][calendar][focus]")
{
    const auto calendar { make_calendar(make_config()) };
    const luil::ui_element& grid { find(*calendar, grid_id) };
    const auto moved = [&grid](const luil::key_pressed_event& event) {
        const std::optional<std::vector<luil::input_action>> actions { pressed(grid, event) };
        REQUIRE(actions.has_value());
        return days_of<move_intent>(*actions);
    };
    using days = std::vector<year_month_day>;
    REQUIRE(moved(key(luil::key_code::arrow_left)) == days { date(2026, 11, 9) });
    REQUIRE(moved(key(luil::key_code::arrow_right)) == days { date(2026, 11, 11) });
    REQUIRE(moved(key(luil::key_code::arrow_up)) == days { date(2026, 11, 3) });
    REQUIRE(moved(key(luil::key_code::arrow_down)) == days { date(2026, 11, 17) });
    REQUIRE(moved(key(luil::key_code::page_up)) == days { date(2026, 10, 10) });
    REQUIRE(moved(key(luil::key_code::page_down)) == days { date(2026, 12, 10) });
    REQUIRE(moved(key(luil::key_code::page_up, true)) == days { date(2025, 11, 10) });
    REQUIRE(moved(key(luil::key_code::page_down, true)) == days { date(2027, 11, 10) });
    REQUIRE(moved(key(luil::key_code::home)) == days { date(2026, 11, 1) });
    REQUIRE(moved(key(luil::key_code::end)) == days { date(2026, 11, 30) });

    // Ctrl·Alt를 든 키는 앱 단축키다. Shift는 Page에서만 뜻이 있다. 표에 없는 키는 흐른다 (Esc는 popup 닫기로 간다).
    REQUIRE(pressed(grid, key(luil::key_code::arrow_right, false, true)).has_value() == false);
    REQUIRE(pressed(grid, key(luil::key_code::page_down, false, false, true)).has_value() == false);
    REQUIRE(pressed(grid, key(luil::key_code::arrow_right, true)).has_value() == false);
    REQUIRE(pressed(grid, key(luil::key_code::home, true)).has_value() == false);
    REQUIRE(pressed(grid, key(luil::key_code::escape)).has_value() == false);
    REQUIRE(pressed(grid, key(luil::key_code::tab)).has_value() == false);
}

TEST_CASE("Month and year keys fold the day and the range ends swallow the key", "[ui][calendar][focus]")
{
    luil::calendar_config config { make_config() };
    config.cursor = date(2026, 1, 31);
    const auto january { make_calendar(config) };
    const luil::ui_element& grid { find(*january, grid_id) };
    REQUIRE(days_of<move_intent>(*pressed(grid, key(luil::key_code::page_down))) == std::vector<year_month_day> { date(2026, 2, 28) });
    config.cursor = date(2024, 2, 29);
    const auto leap { make_calendar(config) };
    REQUIRE(days_of<move_intent>(*pressed(find(*leap, grid_id), key(luil::key_code::page_down, true))) == std::vector<year_month_day> { date(2025, 2, 28) });

    // 범위 끝에서 더 갈 곳이 없으면 빈 목록으로 삼킨다. 범위로 접힌 목표는 그 끝이다.
    config.bounds = { date(2026, 11, 5), date(2026, 11, 20) };
    config.cursor = date(2026, 11, 20);
    const auto end { make_calendar(config) };
    const luil::ui_element& bounded { find(*end, grid_id) };
    for (const luil::key_code code : { luil::key_code::arrow_right, luil::key_code::arrow_down, luil::key_code::page_down, luil::key_code::end })
    {
        const std::optional<std::vector<luil::input_action>> swallowed { pressed(bounded, key(code)) };
        REQUIRE(swallowed.has_value());
        REQUIRE(swallowed->empty());
    }
    REQUIRE(days_of<move_intent>(*pressed(bounded, key(luil::key_code::home))) == std::vector<year_month_day> { date(2026, 11, 5) });
    REQUIRE(days_of<move_intent>(*pressed(bounded, key(luil::key_code::page_up))) == std::vector<year_month_day> { date(2026, 11, 5) });
}

TEST_CASE("A cursor that is not a date or lies outside the range is drawn folded", "[ui][calendar]")
{
    luil::calendar_config config { make_config() };
    config.cursor = {};
    REQUIRE(make_calendar(config)->cursor() == date(2026, 11, 3));
    config.value.reset();
    config.cursor = date(2026, 2, 30);
    REQUIRE(make_calendar(config)->cursor() == date(2026, 11, 3));
    config.bounds = { date(2026, 12, 1), std::nullopt };
    config.cursor = date(2026, 11, 10);
    const auto folded { make_calendar(config) };
    REQUIRE(folded->cursor() == date(2026, 12, 1));
    // 키의 목표는 접은 커서에서 센다.
    REQUIRE(days_of<move_intent>(*pressed(find(*folded, grid_id), key(luil::key_code::arrow_right))) == std::vector<year_month_day> { date(2026, 12, 2) });
}

TEST_CASE("Only the cursor day is a tab stop and the other days hand their pointer focus to it", "[ui][calendar][focus]")
{
    const auto calendar { make_calendar(make_config()) };
    const luil::ui_element_id cursor { day_of(date(2026, 11, 10)) };
    for (const auto& day : find(*calendar, grid_id).children())
    {
        REQUIRE(day->tab_stop() == (day->id() == cursor));
        if (day->id() != cursor)
            REQUIRE(day->pointer_focus_target() == cursor);
    }
    REQUIRE(find(*calendar, previous_id).tab_stop());
    REQUIRE(find(*calendar, next_id).tab_stop());
    REQUIRE(calendar->tab_stop() == false);
    REQUIRE(find(*calendar, grid_id).tab_stop() == false);

    // Tab은 이전 달, 다음 달, 커서 날 순이고 다시 처음으로 돈다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_calendar(make_config())));
    std::vector<luil::ui_element_id> visited {};
    for (int index { 0 }; index < 4; ++index)
    {
        static_cast<void>(controller.process(key(luil::key_code::tab)));
        visited.push_back(controller.snapshot().focused);
    }
    REQUIRE(visited == std::vector<luil::ui_element_id> { previous_id, next_id, cursor, previous_id });
}

TEST_CASE("The controller sends the grid keys from the focused day and Enter chooses it", "[ui][calendar][focus]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_calendar(make_config())));
    for (int index { 0 }; index < 3; ++index)
        static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(controller.snapshot().focused == day_of(date(2026, 11, 10)));
    REQUIRE(messages_of(controller.process(key(luil::key_code::arrow_right))) == std::vector<std::u8string> { u8"move 2026-11-11" });
    REQUIRE(messages_of(controller.process(key(luil::key_code::page_up, true))) == std::vector<std::u8string> { u8"move 2025-11-10" });
    REQUIRE(messages_of(controller.process(key(luil::key_code::enter))) == std::vector<std::u8string> { u8"select 2026-11-10" });
    REQUIRE(messages_of(controller.process(key(luil::key_code::space))) == std::vector<std::u8string> { u8"select 2026-11-10" });
    REQUIRE(controller.process(key(luil::key_code::arrow_right, false, true)).empty());
}

TEST_CASE("Clicking a day chooses it and a day off the cursor moves the cursor first", "[ui][calendar]")
{
    const auto calendar { make_calendar(make_config()) };
    REQUIRE(messages_of(click(find(*calendar, day_of(date(2026, 11, 10))))) == std::vector<std::u8string> { u8"select 2026-11-10" });
    REQUIRE(messages_of(click(find(*calendar, day_of(date(2026, 11, 20))))) == std::vector<std::u8string> { u8"move 2026-11-20", u8"select 2026-11-20" });
    // 다른 달의 날도 칸이고 고를 수 있다.
    REQUIRE(messages_of(click(find(*calendar, day_of(date(2026, 12, 5))))) == std::vector<std::u8string> { u8"move 2026-12-05", u8"select 2026-12-05" });
    REQUIRE(find(*calendar, day_of(date(2026, 11, 20))).cursor() == luil::ui_cursor::hand);

    // 실제 누름에서 초점은 커서 날에 서고, 앱의 초점 요청이 누른 날로 옮긴다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_calendar(make_config())));
    // 20일은 셋째 주 여섯째 칸이다 (10 + 5 × 32 + 16, 76 + 2 × 32 + 16).
    std::vector<std::u8string> sent { messages_of(controller.process(press(186.0f, 156.0f, 0))) };
    for (const std::u8string& name : messages_of(controller.process(release(186.0f, 156.0f, 10))))
        sent.push_back(name);
    REQUIRE(sent == std::vector<std::u8string> { u8"move 2026-11-20", u8"select 2026-11-20" });
    REQUIRE(controller.snapshot().focused == day_of(date(2026, 11, 10)));
}

TEST_CASE("A day that cannot be chosen has no click, keeps the focus and swallows Enter", "[ui][calendar][focus]")
{
    luil::calendar_config config { make_config() };
    config.available = weekday_only;
    config.bounds = { date(2026, 11, 2), date(2026, 11, 28) };
    config.cursor = date(2026, 11, 7);
    const auto calendar { make_calendar(config) };
    // 토요일, 범위 밖의 날은 누를 수 없다. 이름에 꼬리가 붙고 자리는 커서 날만이다.
    for (const year_month_day day : { date(2026, 11, 7), date(2026, 11, 1), date(2026, 11, 30) })
    {
        const luil::ui_element& cell { find(*calendar, day_of(day)) };
        REQUIRE(cell.action(luil::ui_trigger::left_click) == nullptr);
        REQUIRE(cell.cursor() == luil::ui_cursor::arrow);
    }
    REQUIRE(find(*calendar, day_of(date(2026, 11, 7))).tab_stop());
    REQUIRE(find(*calendar, day_of(date(2026, 11, 7))).accessibility().name == u8"2026년 11월 7일 토요일, 고를 수 없음");
    REQUIRE(find(*calendar, day_of(date(2026, 11, 9))).action(luil::ui_trigger::left_click) != nullptr);

    // 기본 단추가 있는 화면이다. 고를 수 없는 커서 날의 Enter·Space는 빈 목록이고 기본 단추로 가지 않는다.
    auto fallback { std::make_unique<panel>(luil::ui_element_id { luil::application_element_kind(5), u8"ok" }) };
    fallback->arrange({ { 400.0f, 600.0f, 60.0f, 30.0f }, 1.0f });
    fallback->set_action(luil::ui_trigger::left_click, luil::make_message_action(default_intent {}));
    fallback->set_default_button(true);
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_calendar(config), std::move(fallback)));
    // 7일은 첫 주 일곱째 칸이다. 누름을 받아 초점이 그 날(커서)에 선다.
    static_cast<void>(controller.process(press(218.0f, 92.0f, 0)));
    REQUIRE(controller.process(release(218.0f, 92.0f, 10)).empty());
    REQUIRE(controller.snapshot().focused == day_of(date(2026, 11, 7)));
    REQUIRE(controller.process(key(luil::key_code::enter)).empty());
    REQUIRE(controller.process(key(luil::key_code::space)).empty());
    // 커서는 그 날에서 움직인다 (결정 6, 건너뛰지 않는다).
    REQUIRE(messages_of(controller.process(key(luil::key_code::arrow_right))) == std::vector<std::u8string> { u8"move 2026-11-08" });

    // 범위 밖 1일을 눌러도 초점은 커서 날에 남는다.
    static_cast<void>(controller.process(press(26.0f, 92.0f, 100)));
    static_cast<void>(controller.process(release(26.0f, 92.0f, 110)));
    REQUIRE(controller.snapshot().focused == day_of(date(2026, 11, 7)));
}

TEST_CASE("The month buttons move a month, prefer the turn message and stop where the month is out of range", "[ui][calendar]")
{
    const auto calendar { make_calendar(make_config()) };
    REQUIRE(messages_of(click(find(*calendar, previous_id))) == std::vector<std::u8string> { u8"move 2026-10-10" });
    REQUIRE(messages_of(click(find(*calendar, next_id))) == std::vector<std::u8string> { u8"move 2026-12-10" });
    REQUIRE(find(*calendar, previous_id).accessibility() == luil::access_info { .role = luil::access_role::button, .name = u8"이전 달" });
    REQUIRE(find(*calendar, next_id).accessibility().name == u8"다음 달");
    REQUIRE(find(*calendar, previous_id).tooltip() == u8"이전 달");
    REQUIRE(find(*calendar, previous_id).cursor() == luil::ui_cursor::hand);

    // 단추만의 메시지가 있으면 그것을 낸다. 앱은 초점 요청 없이 커서만 옮겨 초점이 단추에 남는다 (결정 8).
    luil::calendar_config turning { make_config() };
    turning.turn_month = [](const year_month_day day) { return luil::make_app_action(turn_intent { day }); };
    turning.cursor = date(2026, 1, 31);
    const auto turned { make_calendar(turning) };
    REQUIRE(messages_of(click(find(*turned, next_id))) == std::vector<std::u8string> { u8"turn 2026-02-28" });
    REQUIRE(messages_of(click(find(*turned, previous_id))) == std::vector<std::u8string> { u8"turn 2025-12-31" });
    // 키는 여전히 `move_to`다.
    REQUIRE(messages_of(*pressed(find(*turned, grid_id), key(luil::key_code::page_down))) == std::vector<std::u8string> { u8"move 2026-02-28" });

    // 그 달 전체가 범위 밖이면 비활성이다. 한 날이라도 범위 안이면 그 날로 접어 옮긴다.
    luil::calendar_config bounded { make_config() };
    bounded.bounds = { date(2026, 10, 31), date(2026, 11, 30) };
    const auto edge { make_calendar(bounded) };
    REQUIRE(find(*edge, previous_id).enabled());
    REQUIRE(messages_of(click(find(*edge, previous_id))) == std::vector<std::u8string> { u8"move 2026-10-31" });
    REQUIRE(find(*edge, next_id).enabled() == false);
    bounded.bounds = { date(2026, 11, 1), date(2026, 12, 1) };
    const auto start { make_calendar(bounded) };
    REQUIRE(find(*start, previous_id).enabled() == false);
    REQUIRE(find(*start, next_id).enabled());
    // 달력의 끝(0001년 1월)에서도 이전 달은 비활성이다.
    bounded.bounds = {};
    bounded.cursor = date(1, 1, 15);
    REQUIRE(find(*make_calendar(bounded), previous_id).enabled() == false);
}

TEST_CASE("Without messages the calendar is display only", "[ui][calendar]")
{
    luil::calendar_config config { make_config() };
    config.select = nullptr;
    config.move_to = nullptr;
    const auto calendar { make_calendar(config) };
    // 단추가 없고 격자에 처리기가 없으며, 날은 자리도 클릭도 아니다. 머리 줄 자리는 그대로라 크기가 같다.
    REQUIRE(calendar->children().size() == 1u);
    REQUIRE(search(*calendar, previous_id) == nullptr);
    const luil::ui_element& grid { find(*calendar, grid_id) };
    REQUIRE(grid.key_handler_of() == nullptr);
    for (const auto& day : grid.children())
    {
        REQUIRE(day->tab_stop() == false);
        REQUIRE(day->action(luil::ui_trigger::left_click) == nullptr);
        REQUIRE(day->interactive() == false);
    }
    REQUIRE(same_rect(calendar->bounds(), { 10.0f, 20.0f, 224.0f, 248.0f }));

    // 고르기만 있으면 클릭은 고르기 하나이고 키는 Enter만 삼킨다.
    config.select = make_config().select;
    const auto chooser { make_calendar(config) };
    REQUIRE(messages_of(click(find(*chooser, day_of(date(2026, 11, 20))))) == std::vector<std::u8string> { u8"select 2026-11-20" });
    REQUIRE(pressed(find(*chooser, grid_id), key(luil::key_code::arrow_right)).has_value() == false);
    REQUIRE(search(*chooser, next_id) == nullptr);
}

TEST_CASE("A disabled calendar disables its buttons, its grid and its days", "[ui][calendar]")
{
    auto calendar { std::make_unique<luil::calendar_element>(make_config()) };
    calendar->set_enabled(false);
    calendar->arrange({ slot, 1.0f });
    REQUIRE(find(*calendar, previous_id).enabled() == false);
    REQUIRE(find(*calendar, grid_id).enabled() == false);
    REQUIRE(find(*calendar, day_of(date(2026, 11, 10))).enabled() == false);
    REQUIRE(find(*calendar, day_of(date(2026, 11, 10))).focusable() == false);
}

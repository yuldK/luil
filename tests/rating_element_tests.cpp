#include "luil/ui/rating_element.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 별점이다 (docs/rating-design.md).
// 칸의 자리와 값 규칙은 요소를 직접 배치해 보고, 누름·끌기·터치·키는 controller에 실제 이벤트를 넣어 본다.
namespace {
    // 별점이 낸 절대 메시지의 앱 쪽 정의다.
    struct rate_intent
    {
        float value { 0.0f };
    };

    struct scrolled
    {
        float delta { 0.0f };
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

    [[nodiscard]] luil::rating_config make_config(const float value = 0.0f, const luil::rating_precision precision = luil::rating_precision::whole)
    {
        luil::rating_config config {};
        config.owner = u8"review";
        config.value = value;
        config.precision = precision;
        config.change_to = [](const float target) { return luil::make_app_action(rate_intent { target }); };
        return config;
    }

    // 별 다섯, 칸 28이면 폭 140이다. 슬롯은 그보다 넓어 남는 자리가 오른쪽에 생긴다.
    constexpr luil::rect_f wide_slot { 100.0f, 40.0f, 280.0f, 28.0f };

    [[nodiscard]] std::unique_ptr<luil::rating_element> make_rating(luil::rating_config config, const luil::rect_f& slot = wide_slot, const float scale = 1.0f)
    {
        auto rating { std::make_unique<luil::rating_element>(std::move(config)) };
        rating->arrange({ slot, scale });
        return rating;
    }

    [[nodiscard]] bool same_rect(const luil::rect_f& first, const luil::rect_f& second)
    {
        return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
    }

    [[nodiscard]] luil::ui_element_id cell_id(const std::size_t number)
    {
        std::u8string owner { u8"review" };
        owner += u8'\u001f';
        owner += std::u8string { static_cast<char8_t>(u8'0' + number % 10u) };
        if (number >= 10u)
            owner.insert(owner.size() - 1u, 1u, static_cast<char8_t>(u8'0' + number / 10u));
        return { luil::ui_element_kind::rating_cell, std::move(owner) };
    }

    [[nodiscard]] const luil::ui_element& cell_of(const luil::rating_element& rating, const std::size_t number)
    {
        REQUIRE(number >= 1u);
        REQUIRE(number <= rating.children().size());
        return *rating.children()[number - 1u];
    }

    // 낸 메시지에서 별점 값만 뽑는다.
    [[nodiscard]] std::vector<float> values_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<float> values {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            REQUIRE(message != nullptr);
            if (const rate_intent* const intent { message->get<rate_intent>() }; intent != nullptr)
                values.push_back(intent->value);
        }
        return values;
    }

    [[nodiscard]] std::vector<float> clicked(const luil::rating_element& rating, const std::size_t number)
    {
        const luil::ui_action* const click { cell_of(rating, number).action(luil::ui_trigger::left_click) };
        REQUIRE(click != nullptr);
        return values_of((*click)({ cell_of(rating, number).id() }));
    }

    [[nodiscard]] std::vector<float> stepped(const luil::rating_element& rating, const luil::value_step step)
    {
        REQUIRE(rating.key_step() != nullptr);
        const std::optional<std::vector<luil::input_action>> actions { rating.key_step()->on_step(step) };
        REQUIRE(actions.has_value());
        return values_of(*actions);
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = device;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = device;
        event.in_contact = device != luil::pointer_device::mouse;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_released_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = device;
        return event;
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool control = false)
    {
        return luil::key_pressed_event { code, control, false, false, false, at(0) };
    }

    // 별점을 담은 창이다. 가로로 흐르는 창이면 그 안에 별점이 선다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(std::unique_ptr<luil::rating_element> rating, const bool scrolls = false)
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 600.0f, 200.0f }, 1.0f });
        auto window { std::make_unique<panel>(luil::ui_element_id { luil::application_element_kind(0), u8"window" }) };
        window->arrange({ { 0.0f, 0.0f, 600.0f, 200.0f }, 1.0f });
        if (scrolls)
        {
            luil::scroll_source source {};
            source.scroll = [](const float delta) { return luil::make_app_action(scrolled { delta }); };
            source.axis = luil::scroll_axis::horizontal;
            window->set_scroll_source(std::move(source));
        }
        window->add(std::move(rating));
        root->add(std::move(window));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 칸 28짜리 별점이 (100, 40)에서 시작하므로 칸 n의 가운데 x다. 높이의 가운데는 54다.
    [[nodiscard]] float center_of_cell(const std::size_t number)
    {
        return 100.0f + 28.0f * static_cast<float>(number - 1u) + 14.0f;
    }
}

TEST_CASE("The cells are contiguous and there is one per step", "[ui][rating]")
{
    // 정수 별은 별 수만큼, 칸 28이니 폭 140이고 슬롯의 남는 자리는 비운다.
    const auto whole { make_rating(make_config()) };
    REQUIRE(whole->id() == luil::ui_element_id { luil::ui_element_kind::rating, u8"review" });
    REQUIRE(same_rect(whole->bounds(), luil::rect_f { 100.0f, 40.0f, 140.0f, 28.0f }));
    REQUIRE(whole->children().size() == 5u);
    for (std::size_t number = 1; number <= 5u; ++number)
    {
        const luil::ui_element& cell { cell_of(*whole, number) };
        REQUIRE(cell.id() == cell_id(number));
        // 칸이 빈틈없이 이어져 별 사이도 칸이다.
        REQUIRE(same_rect(cell.bounds(), luil::rect_f { 100.0f + 28.0f * static_cast<float>(number - 1u), 40.0f, 28.0f, 28.0f }));
    }

    // 반 별은 칸이 두 배이고 폭은 별의 절반이다.
    const auto half { make_rating(make_config(0.0f, luil::rating_precision::half)) };
    REQUIRE(half->children().size() == 10u);
    REQUIRE(same_rect(cell_of(*half, 1).bounds(), luil::rect_f { 100.0f, 40.0f, 14.0f, 28.0f }));
    REQUIRE(same_rect(cell_of(*half, 10).bounds(), luil::rect_f { 226.0f, 40.0f, 14.0f, 28.0f }));
}

TEST_CASE("The star count is cut into one through twenty", "[ui][rating]")
{
    luil::rating_config none { make_config() };
    none.count = 0;
    REQUIRE(make_rating(none)->children().size() == 1u);
    luil::rating_config many { make_config() };
    many.count = 99;
    const auto twenty { make_rating(many, { 0.0f, 0.0f, 1000.0f, 28.0f }) };
    REQUIRE(twenty->children().size() == luil::rating_max_count);
    REQUIRE(luil::rating_element::width_for(many) == 560.0f);
    REQUIRE(luil::rating_element::width_for(none) == 28.0f);
}

TEST_CASE("The slot sizes the cells down and the scale multiplies them", "[ui][rating]")
{
    // 폭 100이면 별 다섯이 20씩이다. 설정 값 28이 아니라 자리가 이긴다.
    const auto narrow { make_rating(make_config(), { 10.0f, 0.0f, 100.0f, 28.0f }) };
    REQUIRE(narrow->bounds().width == 100.0f);
    REQUIRE(same_rect(cell_of(*narrow, 2).bounds(), luil::rect_f { 30.0f, 0.0f, 20.0f, 28.0f }));

    // 배율 2에서는 칸이 물리 56이다.
    const auto scaled { make_rating(make_config(), { 100.0f, 40.0f, 400.0f, 56.0f }, 2.0f) };
    REQUIRE(same_rect(scaled->bounds(), luil::rect_f { 100.0f, 40.0f, 280.0f, 56.0f }));
    REQUIRE(same_rect(cell_of(*scaled, 2).bounds(), luil::rect_f { 156.0f, 40.0f, 56.0f, 56.0f }));
}

TEST_CASE("The cell size and the size helpers follow the config", "[ui][rating]")
{
    // 설정이 `std::function`을 담아 상수 평가 안에서 지을 수 없다. 그래서 `constexpr` 함수인데도 `STATIC_REQUIRE`가 아니다.
    luil::rating_config config {};
    REQUIRE(luil::rating_element::height_for(config) == luil::rating_default_cell_size);
    REQUIRE(luil::rating_element::width_for(config) == 5.0f * luil::rating_default_cell_size);
    config.control_size = 44.0f;
    REQUIRE(luil::rating_element::height_for(config) == 44.0f);
    REQUIRE(luil::rating_element::width_for(config) == 220.0f);
    // 쓸 수 없는 값은 기본 칸이다.
    config.control_size = -3.0f;
    REQUIRE(luil::rating_element::height_for(config) == luil::rating_default_cell_size);
    config.control_size = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(luil::rating_element::height_for(config) == luil::rating_default_cell_size);

    // 터치 앱의 44 칸이다. 반 별은 칸 폭이 22다.
    luil::rating_config touch { make_config(0.0f, luil::rating_precision::half) };
    touch.control_size = 44.0f;
    const auto half { make_rating(touch, { 0.0f, 0.0f, 360.0f, 44.0f }) };
    REQUIRE(half->bounds().width == 220.0f);
    REQUIRE(cell_of(*half, 1).bounds().width == 22.0f);
}

TEST_CASE("The value is cut into range and the minimum follows the clear rule", "[ui][rating]")
{
    luil::rating_config config { make_config(3.0f) };
    REQUIRE(luil::rating_element::clamp_value(config) == 3.0f);
    config.value = -2.0f;
    REQUIRE(luil::rating_element::clamp_value(config) == 0.0f);
    config.value = 9.0f;
    REQUIRE(luil::rating_element::clamp_value(config) == 5.0f);
    config.value = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(luil::rating_element::clamp_value(config) == 0.0f);
    config.value = 4.3f;
    REQUIRE(luil::rating_element::clamp_value(config) == 4.3f);

    // 지울 수 있으면 0이다. 지울 수 없으면 한 번 매긴 뒤에는 한 걸음이고, 값이 0이면 아직 0이다.
    config.value = 3.0f;
    REQUIRE(luil::rating_element::minimum_for(config) == 0.0f);
    config.clearable = false;
    REQUIRE(luil::rating_element::minimum_for(config) == 1.0f);
    config.precision = luil::rating_precision::half;
    REQUIRE(luil::rating_element::minimum_for(config) == 0.5f);
    config.value = 0.0f;
    REQUIRE(luil::rating_element::minimum_for(config) == 0.0f);
}

TEST_CASE("The value at an x is the number of the cell times the step", "[ui][rating]")
{
    const auto rating { make_rating(make_config()) };
    REQUIRE(rating->value_at(100.0f) == 1.0f);
    REQUIRE(rating->value_at(127.9f) == 1.0f);
    REQUIRE(rating->value_at(128.0f) == 2.0f);
    REQUIRE(rating->value_at(239.9f) == 5.0f);
    // 줄의 오른쪽 밖은 마지막 칸이다. 왼쪽 밖은 최소다.
    REQUIRE(rating->value_at(240.0f) == 5.0f);
    REQUIRE(rating->value_at(5000.0f) == 5.0f);
    REQUIRE(rating->value_at(99.9f) == 0.0f);
    REQUIRE(rating->value_at(-20.0f) == 0.0f);

    const auto half { make_rating(make_config(0.0f, luil::rating_precision::half)) };
    REQUIRE(half->value_at(100.0f) == 0.5f);
    REQUIRE(half->value_at(113.9f) == 0.5f);
    REQUIRE(half->value_at(114.0f) == 1.0f);
    REQUIRE(half->value_at(300.0f) == 5.0f);

    // 지울 수 없으면 왼쪽 밖도 한 걸음이다. 끌기가 지금 값을 읽지 않아도 같은 값이다.
    luil::rating_config fixed { make_config(3.0f) };
    fixed.clearable = false;
    REQUIRE(make_rating(fixed)->value_at(50.0f) == 1.0f);
    fixed.value = 0.0f;
    REQUIRE(make_rating(fixed)->value_at(50.0f) == 1.0f);

    // 배율 2에서는 물리 좌표다. 칸이 56이다.
    const auto scaled { make_rating(make_config(), { 100.0f, 40.0f, 400.0f, 56.0f }, 2.0f) };
    REQUIRE(scaled->value_at(155.9f) == 1.0f);
    REQUIRE(scaled->value_at(156.0f) == 2.0f);
}

TEST_CASE("Without a change message the rating is display only", "[ui][rating]")
{
    // 없는 것은 두지 않는다: 칸도 커서도 Tab 자리도 키 역할도 없다.
    luil::rating_config config { make_config(4.3f) };
    config.change_to = nullptr;
    const auto rating { make_rating(config) };
    REQUIRE(rating->children().empty());
    REQUIRE(rating->interactive() == false);
    REQUIRE(rating->cursor() == luil::ui_cursor::inherit);
    REQUIRE(rating->tab_stop() == false);
    REQUIRE(rating->key_step() == nullptr);
    // 표시 전용도 크기와 읽기 값은 같다.
    REQUIRE(rating->bounds().width == 140.0f);
    REQUIRE(rating->accessibility().role == luil::access_role::slider);
}

TEST_CASE("A cell is a pointing hit area that hands the focus to the rating", "[ui][rating]")
{
    const auto rating { make_rating(make_config()) };
    const luil::ui_element& cell { cell_of(*rating, 3) };
    REQUIRE(cell.cursor() == luil::ui_cursor::hand);
    // 칸은 누를 수 있어도 Tab 자리가 아니다. 자리는 root 하나다.
    REQUIRE(cell.tab_stop() == false);
    REQUIRE(rating->tab_stop());
    // 눌러서 얻는 초점은 root로 간다. 칸을 누른 뒤 곧바로 ← →가 먹힌다.
    REQUIRE(cell.pointer_focus_target() == rating->id());
    REQUIRE(cell.accessibility().role == luil::access_role::none);
    REQUIRE(cell.pointer_drag() != nullptr);
    REQUIRE(cell.pointer_drag()->start_distance == luil::rating_drag_start_distance);
    // 키는 초점을 따르므로 가로 축을 root가 든다.
    REQUIRE(rating->key_step() != nullptr);
    REQUIRE(rating->key_step()->axis == luil::focus_axis::horizontal);
}

TEST_CASE("Clicking a cell sends its value and clicking the current value clears it", "[ui][rating]")
{
    REQUIRE(clicked(*make_rating(make_config(0.0f)), 3) == std::vector<float> { 3.0f });
    REQUIRE(clicked(*make_rating(make_config(5.0f)), 3) == std::vector<float> { 3.0f });
    // 칸의 값이 현재 값과 같으면 지운다.
    REQUIRE(clicked(*make_rating(make_config(3.0f)), 3) == std::vector<float> { 0.0f });

    // 반 별은 칸 번호 곱하기 0.5다. 3.5의 칸을 다시 누르면 지운다.
    REQUIRE(clicked(*make_rating(make_config(0.0f, luil::rating_precision::half)), 7) == std::vector<float> { 3.5f });
    REQUIRE(clicked(*make_rating(make_config(3.5f, luil::rating_precision::half)), 7) == std::vector<float> { 0.0f });
    // 소수 값은 어느 칸과도 같지 않아 칸의 값을 낸다.
    REQUIRE(clicked(*make_rating(make_config(4.3f)), 4) == std::vector<float> { 4.0f });

    // 지울 수 없으면 같은 값을 다시 낸다.
    luil::rating_config fixed { make_config(3.0f) };
    fixed.clearable = false;
    REQUIRE(clicked(*make_rating(fixed), 3) == std::vector<float> { 3.0f });
}

TEST_CASE("A disabled rating sends nothing", "[ui][rating]")
{
    auto rating { std::make_unique<luil::rating_element>(make_config(3.0f)) };
    rating->set_enabled(false);
    rating->arrange({ wide_slot, 1.0f });
    // 칸이 root를 따라 꺼진다. controller가 비활성 칸의 액션을 막고 초점도 주지 않는다.
    REQUIRE(cell_of(*rating, 3).enabled() == false);
    REQUIRE(rating->focusable() == false);
    REQUIRE(clicked(*rating, 3).empty());
    const luil::pointer_drag_target* const drag { cell_of(*rating, 3).pointer_drag() };
    REQUIRE(drag != nullptr);
    REQUIRE(drag->on_press({ cell_of(*rating, 3).id(), center_of_cell(3), 54.0f }).empty());
    REQUIRE(drag->on_move({ cell_of(*rating, 3).id(), center_of_cell(3), 54.0f }, { cell_of(*rating, 4).id(), center_of_cell(4), 54.0f }).empty());
    REQUIRE(stepped(*rating, luil::value_step::increase).empty());
    // 보조 기술의 값 정하기도 같다.
    REQUIRE(rating->access_actions({ luil::access_command::set_value, 5.0f })->empty());

    // controller도 비활성 칸에는 액션도 초점도 주지 않는다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(std::move(rating)));
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 0))).empty());
    REQUIRE(values_of(controller.process(release(center_of_cell(3), 54.0f, 10))).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("A drag sends the pressed cell and then every cell the pointer enters", "[ui][rating][pointer-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(0.0f))));

    // 거리 안의 떨림은 클릭이다. 뗄 때 칸의 값이 나간다.
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 0))).empty());
    REQUIRE(values_of(controller.process(move(center_of_cell(3) + 3.0f, 54.0f, 10))).empty());
    REQUIRE(values_of(controller.process(release(center_of_cell(3) + 3.0f, 54.0f, 20))) == std::vector<float> { 3.0f });
    // 초점이 root에 선다.
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::rating, u8"review" });

    // 거리를 넘는 순간 눌린 자리의 칸 값과 포인터의 칸 값이 나간다.
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 1000))).empty());
    REQUIRE(values_of(controller.process(move(center_of_cell(4), 54.0f, 1010))) == std::vector<float> { 3.0f, 4.0f });
    // 같은 칸 안의 이동은 말이 없다.
    REQUIRE(values_of(controller.process(move(center_of_cell(4) + 5.0f, 54.0f, 1020))).empty());
    REQUIRE(values_of(controller.process(move(center_of_cell(5), 54.0f, 1030))) == std::vector<float> { 5.0f });
    // 줄의 오른쪽 밖은 count, 왼쪽 밖은 최소다. 지우기 규칙은 없다.
    REQUIRE(values_of(controller.process(move(400.0f, 54.0f, 1040))).empty());
    REQUIRE(values_of(controller.process(move(50.0f, 54.0f, 1050))) == std::vector<float> { 0.0f });
    REQUIRE(values_of(controller.process(move(center_of_cell(2), 54.0f, 1060))) == std::vector<float> { 2.0f });
    REQUIRE(values_of(controller.process(release(center_of_cell(2), 54.0f, 1070))).empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
}

TEST_CASE("A drag that starts on the current cell does not clear on release", "[ui][rating][pointer-drag]")
{
    // 현재 값과 같은 칸에서 시작한 끌기는 지우지 않는다. 클릭이 아니기 때문이다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(3.0f))));
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 0))).empty());
    REQUIRE(values_of(controller.process(move(center_of_cell(3) + 10.0f, 54.0f, 10))) == std::vector<float> { 3.0f });
    REQUIRE(values_of(controller.process(release(center_of_cell(3) + 10.0f, 54.0f, 20))).empty());
}

TEST_CASE("Two quick clicks set the value and then clear it", "[ui][rating][pointer-drag]")
{
    // 두 번째 클릭도 클릭이다. 앱이 값을 반영하는 frame 사이라면 매기고 바로 지우는 데서 끝난다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(0.0f))));
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 0))).empty());
    REQUIRE(values_of(controller.process(release(center_of_cell(3), 54.0f, 20))) == std::vector<float> { 3.0f });
    controller.set_tree(make_tree(make_rating(make_config(3.0f))));
    REQUIRE(values_of(controller.process(press(center_of_cell(3), 54.0f, 120))).empty());
    REQUIRE(values_of(controller.process(release(center_of_cell(3), 54.0f, 140))) == std::vector<float> { 0.0f });
}

TEST_CASE("Hovering walks the cells without a gap between stars", "[ui][rating]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(0.0f))));

    static_cast<void>(controller.process(move(center_of_cell(3), 54.0f, 0)));
    REQUIRE(controller.snapshot().hovered == cell_id(3));
    // 칸이 이어져 칸 경계의 양쪽 픽셀이 모두 칸이다. 별 사이를 지나도 hover가 끊기지 않는다.
    static_cast<void>(controller.process(move(100.0f + 28.0f * 3.0f - 0.1f, 54.0f, 10)));
    REQUIRE(controller.snapshot().hovered == cell_id(3));
    static_cast<void>(controller.process(move(100.0f + 28.0f * 3.0f, 54.0f, 20)));
    REQUIRE(controller.snapshot().hovered == cell_id(4));
    // 별 줄 밖은 아무것도 아니다. 슬롯의 남는 자리는 비어 있다.
    static_cast<void>(controller.process(move(300.0f, 54.0f, 30)));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
}

TEST_CASE("A touch tap is a click and a fast swipe in a horizontal window scrolls it", "[ui][rating][pointer-drag][touch]")
{
    // 탭은 클릭이다.
    luil::interaction_controller tap {};
    tap.set_tree(make_tree(make_rating(make_config(0.0f)), true));
    REQUIRE(values_of(tap.process(press(center_of_cell(3), 54.0f, 0, luil::pointer_device::touch))).empty());
    REQUIRE(values_of(tap.process(release(center_of_cell(3), 54.0f, 30, luil::pointer_device::touch))) == std::vector<float> { 3.0f });

    // 축에 맞는 빠른 쓸기는 감싼 창의 스크롤이다. 별 줄 위에서 시작해도 값 메시지가 없다.
    luil::interaction_controller swipe {};
    swipe.set_tree(make_tree(make_rating(make_config(0.0f)), true));
    static_cast<void>(swipe.process(press(center_of_cell(3), 54.0f, 0, luil::pointer_device::touch)));
    const std::vector<luil::input_action> moved { swipe.process(move(center_of_cell(3) + 60.0f, 54.0f, 50, luil::pointer_device::touch)) };
    REQUIRE(values_of(moved).empty());
    REQUIRE(moved.size() == 1u);
    const auto* const message { std::get_if<luil::app_message>(&moved[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<scrolled>() != nullptr);
    REQUIRE(values_of(swipe.process(release(center_of_cell(3) + 60.0f, 54.0f, 80, luil::pointer_device::touch))).empty());
}

TEST_CASE("A held touch in a scrolling window drags the stars once it can no longer scroll", "[ui][rating][pointer-drag][touch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(0.0f)), true));
    static_cast<void>(controller.process(press(center_of_cell(3), 54.0f, 0, luil::pointer_device::touch)));
    static_cast<void>(controller.advance(at(700)));
    // 스크롤의 시간 창이 지난 뒤의 이동이다. 눌린 자리의 칸과 포인터의 칸이 나간다.
    REQUIRE(values_of(controller.process(move(center_of_cell(5), 54.0f, 800, luil::pointer_device::touch))) == std::vector<float> { 3.0f, 5.0f });
    REQUIRE(values_of(controller.process(release(center_of_cell(5), 54.0f, 820, luil::pointer_device::touch))).empty());
}

TEST_CASE("Without a window to scroll a touch past the tolerance is a drag", "[ui][rating][pointer-drag][touch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(0.0f))));
    static_cast<void>(controller.process(press(center_of_cell(3), 54.0f, 0, luil::pointer_device::touch)));
    REQUIRE(values_of(controller.process(move(center_of_cell(4), 54.0f, 50, luil::pointer_device::touch))) == std::vector<float> { 3.0f, 4.0f });
}

TEST_CASE("Pressing a cell then walks the value with the arrow, page and end keys", "[ui][rating][focus]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_rating(make_config(2.0f))));
    // 칸을 눌러 초점을 root에 세운다. 누른 클릭의 메시지는 이 test의 관심이 아니다.
    static_cast<void>(controller.process(press(center_of_cell(4), 54.0f, 0)));
    static_cast<void>(controller.process(release(center_of_cell(4), 54.0f, 10)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::rating, u8"review" });

    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_right))) == std::vector<float> { 3.0f });
    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_left))) == std::vector<float> { 1.0f });
    // controller는 PageUp을 감소, PageDown을 증가로 보낸다 (슬라이더와 같다). 정수 별에서는 화살표와 같다.
    REQUIRE(values_of(controller.process(key(luil::key_code::page_down))) == std::vector<float> { 3.0f });
    REQUIRE(values_of(controller.process(key(luil::key_code::page_up))) == std::vector<float> { 1.0f });
    REQUIRE(values_of(controller.process(key(luil::key_code::home))) == std::vector<float> { 0.0f });
    REQUIRE(values_of(controller.process(key(luil::key_code::end))) == std::vector<float> { 5.0f });
    // Ctrl을 든 키는 앱 단축키로 흐른다.
    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_right, true))).empty());
}

TEST_CASE("The arrow keys step to the neighbouring grid points", "[ui][rating][focus]")
{
    // 정수 별이다. 소수 값은 이웃한 격자점으로 간다.
    const auto whole { make_rating(make_config(4.3f)) };
    REQUIRE(stepped(*whole, luil::value_step::increase) == std::vector<float> { 5.0f });
    REQUIRE(stepped(*whole, luil::value_step::decrease) == std::vector<float> { 4.0f });
    REQUIRE(stepped(*make_rating(make_config(3.0f)), luil::value_step::increase) == std::vector<float> { 4.0f });
    REQUIRE(stepped(*make_rating(make_config(3.0f)), luil::value_step::decrease) == std::vector<float> { 2.0f });
    // 부동소수 오차는 걸음의 1/1000 안에서 같다고 본다. 2.9998은 3이다.
    REQUIRE(stepped(*make_rating(make_config(2.9998f)), luil::value_step::increase) == std::vector<float> { 4.0f });
    REQUIRE(stepped(*make_rating(make_config(2.9998f)), luil::value_step::decrease) == std::vector<float> { 2.0f });

    // 반 별은 걸음이 0.5이고 한 별(Page)이 걸음 둘이다.
    const auto half { make_rating(make_config(3.0f, luil::rating_precision::half)) };
    REQUIRE(stepped(*half, luil::value_step::increase) == std::vector<float> { 3.5f });
    REQUIRE(stepped(*half, luil::value_step::decrease) == std::vector<float> { 2.5f });
    REQUIRE(stepped(*half, luil::value_step::increase_page) == std::vector<float> { 4.0f });
    REQUIRE(stepped(*half, luil::value_step::decrease_page) == std::vector<float> { 2.0f });
    const auto odd_half { make_rating(make_config(4.3f, luil::rating_precision::half)) };
    REQUIRE(stepped(*odd_half, luil::value_step::increase) == std::vector<float> { 4.5f });
    REQUIRE(stepped(*odd_half, luil::value_step::decrease) == std::vector<float> { 4.0f });
    REQUIRE(stepped(*odd_half, luil::value_step::increase_page) == std::vector<float> { 5.0f });
    REQUIRE(stepped(*odd_half, luil::value_step::decrease_page) == std::vector<float> { 3.5f });
}

TEST_CASE("A key that reached the end is swallowed instead of leaking", "[ui][rating][focus]")
{
    // 이미 끝인 별점의 Home·End가 앱 단축키로 새면 화면이 함께 맨 위로 뛴다. 빈 목록이 "내 것이고 삼켰다"다.
    const auto lowest { make_rating(make_config(0.0f)) };
    REQUIRE(stepped(*lowest, luil::value_step::minimum).empty());
    REQUIRE(stepped(*lowest, luil::value_step::decrease).empty());
    REQUIRE(stepped(*lowest, luil::value_step::decrease_page).empty());
    const auto highest { make_rating(make_config(5.0f)) };
    REQUIRE(stepped(*highest, luil::value_step::maximum).empty());
    REQUIRE(stepped(*highest, luil::value_step::increase).empty());
    REQUIRE(stepped(*highest, luil::value_step::increase_page).empty());

    // 지울 수 없는 별점은 한 걸음이 아래 끝이다.
    luil::rating_config fixed { make_config(1.0f) };
    fixed.clearable = false;
    const auto floor { make_rating(fixed) };
    REQUIRE(stepped(*floor, luil::value_step::decrease).empty());
    REQUIRE(stepped(*floor, luil::value_step::minimum).empty());
    // 아직 매기지 않았으면 최소가 0이라 Home은 삼킨다. 오른쪽으로는 간다.
    fixed.value = 0.0f;
    const auto unrated { make_rating(fixed) };
    REQUIRE(stepped(*unrated, luil::value_step::minimum).empty());
    REQUIRE(stepped(*unrated, luil::value_step::increase) == std::vector<float> { 1.0f });
    // 끝에서는 Home과 End가 한 번에 끝으로 간다.
    REQUIRE(stepped(*make_rating(make_config(2.5f, luil::rating_precision::half)), luil::value_step::minimum) == std::vector<float> { 0.0f });
    REQUIRE(stepped(*make_rating(make_config(2.5f, luil::rating_precision::half)), luil::value_step::maximum) == std::vector<float> { 5.0f });
}

TEST_CASE("The rating reads as a slider with a step, a page and a value text", "[ui][rating][accessibility]")
{
    const auto rating { make_rating(make_config(3.0f)) };
    const luil::access_info info { rating->accessibility() };
    REQUIRE(info.role == luil::access_role::slider);
    REQUIRE(info.name == u8"별점");
    REQUIRE(info.range == luil::access_range { 0.0f, 5.0f, 3.0f, 1.0f, 1.0f });
    REQUIRE(info.value == u8"3 / 5");

    // 반 별은 걸음이 0.5이고 한 별이 Page다. 소수는 소수 첫째 자리까지 읽는다.
    const luil::access_info half { make_rating(make_config(3.5f, luil::rating_precision::half))->accessibility() };
    REQUIRE(half.range == luil::access_range { 0.0f, 5.0f, 3.5f, 0.5f, 1.0f });
    REQUIRE(half.value == u8"3.5 / 5");
    REQUIRE(make_rating(make_config(4.3f))->accessibility().value == u8"4.3 / 5");
    REQUIRE(make_rating(make_config(0.0f))->accessibility().value == u8"0 / 5");
    REQUIRE(make_rating(make_config(9.0f))->accessibility().value == u8"5 / 5");

    // 이름과 값 글은 앱이 덮는다.
    luil::rating_config named { make_config(2.0f) };
    named.name = u8"리뷰 점수";
    float seen_value { -1.0f };
    std::size_t seen_count { 0 };
    named.value_name = [&seen_value, &seen_count](const float value, const std::size_t count) {
        seen_value = value;
        seen_count = count;
        return std::u8string { u8"두 점" };
    };
    const luil::access_info custom { make_rating(named)->accessibility() };
    REQUIRE(custom.name == u8"리뷰 점수");
    REQUIRE(custom.value == u8"두 점");
    // 자른 값과 별 수를 받는다.
    REQUIRE(seen_value == 2.0f);
    REQUIRE(seen_count == 5u);
}

TEST_CASE("The range minimum says the clear rule", "[ui][rating][accessibility]")
{
    // 지울 수 없고 값이 있으면 최소가 한 걸음이다. 값이 0이면 아직 0이다.
    luil::rating_config fixed { make_config(3.0f) };
    fixed.clearable = false;
    REQUIRE(make_rating(fixed)->accessibility().range->minimum == 1.0f);
    fixed.value = 0.0f;
    REQUIRE(make_rating(fixed)->accessibility().range->minimum == 0.0f);
    // 표시 전용 평균처럼 한 걸음보다 작은 값도 범위 안에 있다.
    fixed.value = 0.4f;
    fixed.change_to = nullptr;
    const luil::access_info small { make_rating(fixed)->accessibility() };
    REQUIRE(small.range->minimum <= small.range->value);
}

TEST_CASE("Setting a value rounds to the nearest grid point and keeps the range", "[ui][rating][accessibility]")
{
    const auto set = [](const luil::rating_element& rating, const float value) {
        const std::optional<std::vector<luil::input_action>> actions { rating.access_actions({ luil::access_command::set_value, value }) };
        REQUIRE(actions.has_value());
        return values_of(*actions);
    };

    const auto rating { make_rating(make_config(3.0f)) };
    REQUIRE(set(*rating, 4.0f) == std::vector<float> { 4.0f });
    REQUIRE(set(*rating, 4.2f) == std::vector<float> { 4.0f });
    // 같은 거리면 큰 쪽이다.
    REQUIRE(set(*rating, 4.5f) == std::vector<float> { 5.0f });
    REQUIRE(set(*rating, 99.0f) == std::vector<float> { 5.0f });
    REQUIRE(set(*rating, 0.0f) == std::vector<float> { 0.0f });
    // 반올림한 값이 현재 값이면 할 일이 없다.
    REQUIRE(set(*rating, 3.0f).empty());
    REQUIRE(set(*rating, 3.2f).empty());

    const auto half { make_rating(make_config(3.0f, luil::rating_precision::half)) };
    REQUIRE(set(*half, 3.2f).empty());
    REQUIRE(set(*half, 3.3f) == std::vector<float> { 3.5f });
    REQUIRE(set(*half, 3.25f) == std::vector<float> { 3.5f });

    // 지울 수 없으면 0 요청이 한 걸음으로 잘린다.
    luil::rating_config fixed { make_config(3.0f) };
    fixed.clearable = false;
    REQUIRE(set(*make_rating(fixed), 0.0f) == std::vector<float> { 1.0f });

    // NaN은 아무 일도 하지 않는다.
    REQUIRE(set(*rating, std::numeric_limits<float>::quiet_NaN()).empty());
}

TEST_CASE("A display only rating is read only and its cells are not in the access tree", "[ui][rating][accessibility]")
{
    luil::rating_config average { make_config(4.3f) };
    average.change_to = nullptr;
    const auto display { make_rating(average) };
    // 값 정하기는 할 수 없는 일이라 읽기 전용으로 읽힌다 (`get_IsReadOnly`가 같은 계획을 묻는다).
    REQUIRE(display->access_actions({ luil::access_command::set_value, 3.0f }).has_value() == false);
    REQUIRE(luil::plan_access_request(*display, { luil::access_command::set_value, 4.3f }).has_value() == false);

    const auto editable { make_rating(make_config(4.3f)) };
    REQUIRE(luil::plan_access_request(*editable, { luil::access_command::set_value, 4.3f }).has_value());
    // 칸은 역할이 없어 접근 tree에서 접힌다.
    REQUIRE(luil::access_children(*editable).empty());
}

TEST_CASE("A coordinate over a star reads the rating itself", "[ui][rating][accessibility]")
{
    const auto tree { make_tree(make_rating(make_config(3.0f))) };
    const luil::ui_element* const found { luil::access_element_at(*tree, center_of_cell(2), 54.0f) };
    REQUIRE(found != nullptr);
    REQUIRE(found->id() == luil::ui_element_id { luil::ui_element_kind::rating, u8"review" });
    // 별 줄 밖은 root가 아니다.
    const luil::ui_element* const outside { luil::access_element_at(*tree, 300.0f, 54.0f) };
    REQUIRE((outside == nullptr || outside->id().kind != luil::ui_element_kind::rating));
}

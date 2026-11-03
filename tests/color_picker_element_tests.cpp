#include "luil/ui/color_picker_element.h"

#include "luil/app/color_picker_popup.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/dropdown_element.h"
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

// 색 고르기다 (docs/color-picker-design.md). 부분의 자리와 값 규칙은 요소를 직접 배치해 보고, 누름·끌기·키·초점은 controller에
// 실제 이벤트를 넣어 본다.
namespace {
    constexpr luil::ui_element_kind kind_hex { luil::application_element_kind(0) };

    struct color_intent
    {
        luil::hsv_color value {};
    };

    struct commit_intent
    {
    };

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

    // 225도, 채도 80%, 명도 100%, 알파 50%다. 8비트로 #3366FF80이다.
    constexpr luil::hsv_color start_value { 225.0f, 0.8f, 1.0f, 0.5f };

    [[nodiscard]] luil::color_picker_config make_config(const bool alpha = true, const luil::hsv_color value = start_value)
    {
        luil::color_picker_config config {};
        config.owner = u8"paint";
        config.value = value;
        config.alpha = alpha;
        config.change_to = [](const luil::hsv_color target) { return luil::make_app_action(color_intent { target }); };
        return config;
    }

    [[nodiscard]] luil::color_picker_config make_full_config()
    {
        luil::color_picker_config config { make_config() };
        config.hex = luil::color_picker_hex { .id = { kind_hex, u8"paint-hex" }, .value = { .text = u8"#3366FF80" }, .commit = luil::make_message_action(commit_intent {}) };
        config.presets = { { luil::make_ui_color(255, 0, 0), u8"" }, { luil::make_ui_color(128, 128, 128), u8"gray" }, { luil::make_ui_color(0, 0, 0, 0), u8"" }, { luil::make_ui_color(0, 128, 0), u8"" } };
        return config;
    }

    // 슬롯은 (10, 20)에서 시작한다. 배율 1이면 판은 (10, 20, 200, 200), 색상 막대는 y 228, 알파 막대는 y 252, 견본 줄은 y 276이다.
    constexpr luil::rect_f slot { 10.0f, 20.0f, 400.0f, 600.0f };

    [[nodiscard]] std::unique_ptr<luil::color_picker_element> make_picker(luil::color_picker_config config, const luil::rect_f& area = slot, const float scale = 1.0f)
    {
        auto picker { std::make_unique<luil::color_picker_element>(std::move(config)) };
        picker->arrange({ area, scale });
        return picker;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(std::unique_ptr<luil::ui_element> content)
    {
        // 내용은 이미 배치했다. root는 자리만 잡고 자식을 다시 배치하지 않는다.
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 600.0f, 700.0f }, 1.0f });
        root->add(std::move(content));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] bool same_rect(const luil::rect_f& first, const luil::rect_f& second)
    {
        return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
    }

    [[nodiscard]] std::u8string part(const std::u8string_view name)
    {
        std::u8string owner { u8"paint" };
        owner += u8'\u001f';
        owner += name;
        return owner;
    }

    const luil::ui_element_id plane_id { luil::ui_element_kind::color_picker_plane, part(u8"plane") };
    const luil::ui_element_id hue_id { luil::ui_element_kind::color_picker_bar, part(u8"hue") };
    const luil::ui_element_id alpha_id { luil::ui_element_kind::color_picker_bar, part(u8"alpha") };
    const luil::ui_element_id hex_id { kind_hex, u8"paint-hex" };

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

    [[nodiscard]] std::vector<luil::hsv_color> values_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<luil::hsv_color> values {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            REQUIRE(message != nullptr);
            if (const color_intent* const intent { message->get<color_intent>() }; intent != nullptr)
                values.push_back(intent->value);
        }
        return values;
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

    [[nodiscard]] std::size_t commits_of(const std::vector<luil::input_action>& actions)
    {
        std::size_t count { 0 };
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr && message->get<commit_intent>() != nullptr)
                ++count;
        return count;
    }

    [[nodiscard]] luil::hsv_color with(luil::hsv_color base, const float saturation, const float value)
    {
        base.saturation = saturation;
        base.value = value;
        return base;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int time)
    {
        return luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time)
    {
        return luil::pointer_moved_event { x, y, at(time) };
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int time)
    {
        return luil::pointer_released_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool control = false, const bool alt = false)
    {
        luil::key_pressed_event event {};
        event.key = code;
        event.control = control;
        event.alt = alt;
        return event;
    }

    [[nodiscard]] std::vector<luil::hsv_color> stepped(const luil::ui_element& element, const luil::value_step step)
    {
        REQUIRE(element.key_step() != nullptr);
        const std::optional<std::vector<luil::input_action>> actions { element.key_step()->on_step(step) };
        REQUIRE(actions.has_value());
        return values_of(*actions);
    }

    [[nodiscard]] luil::color_field_config make_field(const bool open)
    {
        luil::color_field_config config {};
        config.owner = u8"swatch";
        config.value = start_value;
        config.alpha = false;
        config.open = open;
        config.set_open = [](const bool target) { return luil::make_app_action(open_intent { target }); };
        return config;
    }
}

TEST_CASE("The picker builds its parts with owners under the picker owner", "[ui][color-picker]")
{
    const auto picker { make_picker(make_full_config()) };
    REQUIRE(picker->id() == luil::ui_element_id { luil::ui_element_kind::color_picker, u8"paint" });
    REQUIRE(luil::color_picker_element::plane_id(u8"paint") == plane_id);
    // 판, 색상 막대, 알파 막대, 16진 칸, 견본 칸 줄 순이다. 견본은 root가 그리고 노드가 아니다.
    REQUIRE(picker->children().size() == 5u);
    REQUIRE(picker->children()[0]->id() == plane_id);
    REQUIRE(picker->children()[1]->id() == hue_id);
    REQUIRE(picker->children()[2]->id() == alpha_id);
    REQUIRE(picker->children()[3]->id() == hex_id);
    const luil::ui_element& row { *picker->children()[4] };
    REQUIRE(row.children().size() == 4u);
    for (std::size_t index { 0 }; index < 4u; ++index)
        REQUIRE(row.children()[index]->id() == luil::ui_element_id { luil::ui_element_kind::color_picker_preset, part(std::u8string(1u, static_cast<char8_t>(u8'1' + index))) });

    // 알파를 끄면 막대가 없다. 16진 칸과 견본도 설정에 있을 때만 선다.
    const auto plain { make_picker(make_config(false)) };
    REQUIRE(plain->children().size() == 2u);
    REQUIRE(plain->children()[1]->id() == hue_id);
}

TEST_CASE("The logical size is the plane, the bars, the swatch line and the preset rows", "[ui][color-picker]")
{
    // 200 + 8 + 16 + 8 + 32다.
    REQUIRE(luil::color_picker_element::size_for(make_config(false)) == luil::size_f { 200.0f, 264.0f });
    // 알파 막대가 8 + 16을 더한다.
    REQUIRE(luil::color_picker_element::size_for(make_config()) == luil::size_f { 200.0f, 288.0f });
    // 폭 200에 견본 칸 24와 간격 8이면 한 줄에 여섯이다. 넷은 한 줄이다.
    REQUIRE(luil::color_picker_element::size_for(make_full_config()) == luil::size_f { 200.0f, 320.0f });
    luil::color_picker_config many { make_full_config() };
    many.presets.resize(7u);
    REQUIRE(luil::color_picker_element::size_for(many) == luil::size_f { 200.0f, 352.0f });
    // 판과 막대의 크기를 앱이 바꾼다.
    luil::color_picker_config touch { make_config() };
    touch.plane_size = 100.0f;
    touch.bar_height = 32.0f;
    REQUIRE(luil::color_picker_element::size_for(touch) == luil::size_f { 100.0f, 100.0f + 40.0f + 40.0f + 40.0f });
}

TEST_CASE("The parts are stacked with the gap and the slot narrows the plane", "[ui][color-picker]")
{
    const auto picker { make_picker(make_full_config()) };
    REQUIRE(same_rect(picker->bounds(), { 10.0f, 20.0f, 200.0f, 320.0f }));
    REQUIRE(same_rect(find(*picker, plane_id).bounds(), { 10.0f, 20.0f, 200.0f, 200.0f }));
    REQUIRE(same_rect(find(*picker, hue_id).bounds(), { 10.0f, 228.0f, 200.0f, 16.0f }));
    REQUIRE(same_rect(find(*picker, alpha_id).bounds(), { 10.0f, 252.0f, 200.0f, 16.0f }));
    // 16진 칸은 견본(32) 옆이고 견본 줄의 세로 가운데에 선다.
    REQUIRE(same_rect(find(*picker, hex_id).bounds(), { 50.0f, 278.0f, 96.0f, 28.0f }));
    REQUIRE(same_rect(picker->children()[4]->children()[1]->bounds(), { 42.0f, 316.0f, 24.0f, 24.0f }));

    // 자리가 판보다 좁으면 판이 그 폭으로 준다.
    const auto narrow { make_picker(make_config(false), { 0.0f, 0.0f, 150.0f, 400.0f }) };
    REQUIRE(same_rect(find(*narrow, plane_id).bounds(), { 0.0f, 0.0f, 150.0f, 150.0f }));
}

TEST_CASE("The value at a point reads the corners at scale two and cuts outside the plane", "[ui][color-picker]")
{
    // 배율 2에서 판은 물리 400이다.
    const auto picker { make_picker(make_config(), { 0.0f, 0.0f, 800.0f, 1200.0f }, 2.0f) };
    REQUIRE(same_rect(find(*picker, plane_id).bounds(), { 0.0f, 0.0f, 400.0f, 400.0f }));
    REQUIRE(picker->value_at_plane(0.0f, 0.0f) == with(start_value, 0.0f, 1.0f));
    REQUIRE(picker->value_at_plane(400.0f, 0.0f) == with(start_value, 1.0f, 1.0f));
    REQUIRE(picker->value_at_plane(0.0f, 400.0f) == with(start_value, 0.0f, 0.0f));
    REQUIRE(picker->value_at_plane(400.0f, 400.0f) == with(start_value, 1.0f, 0.0f));
    REQUIRE(picker->value_at_plane(200.0f, 100.0f) == with(start_value, 0.5f, 0.75f));
    // 밖은 끝으로 자른다. 색상과 알파는 build 때의 값이다.
    REQUIRE(picker->value_at_plane(-50.0f, 900.0f) == with(start_value, 0.0f, 0.0f));
    REQUIRE(picker->value_at_plane(900.0f, -50.0f) == with(start_value, 1.0f, 1.0f));
    REQUIRE(picker->hue_at(0.0f) == 0.0f);
    REQUIRE(picker->hue_at(200.0f) == 180.0f);
    REQUIRE(picker->hue_at(400.0f) == 360.0f);
    REQUIRE(picker->hue_at(5000.0f) == 360.0f);
    REQUIRE(picker->alpha_at(100.0f) == 0.25f);
    REQUIRE(picker->alpha_at(-1.0f) == 0.0f);
}

TEST_CASE("A click sends the value at the point once and a short wobble is still a click", "[ui][color-picker][pointer-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_picker(make_config())));
    // 판 (110, 70)은 채도 50%, 명도 75%다. 시작 거리 안의 떨림은 클릭이고 뗄 때 뗀 자리 (113, 70)의 값이 한 번 나간다.
    REQUIRE(values_of(controller.process(press(110.0f, 70.0f, 0))).empty());
    REQUIRE(values_of(controller.process(move(113.0f, 70.0f, 10))).empty());
    REQUIRE(values_of(controller.process(release(113.0f, 70.0f, 20))) == std::vector<luil::hsv_color> { with(start_value, 103.0f / 200.0f, 0.75f) });
    REQUIRE(controller.snapshot().focused == plane_id);

    // 색상 막대 (60, 236)은 90도다.
    REQUIRE(values_of(controller.process(press(60.0f, 236.0f, 1000))).empty());
    luil::hsv_color hue { start_value };
    hue.hue = 90.0f;
    REQUIRE(values_of(controller.process(release(60.0f, 236.0f, 1010))) == std::vector<luil::hsv_color> { hue });
    REQUIRE(controller.snapshot().focused == hue_id);
}

TEST_CASE("A drag past the start distance sends the pressed value and then every new value", "[ui][color-picker][pointer-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_picker(make_config())));
    REQUIRE(values_of(controller.process(press(110.0f, 70.0f, 0))).empty());
    // 거리를 넘는 순간 누른 자리의 값과 포인터 자리의 값이다. 판의 메시지는 build 때의 색상과 알파를 담는다.
    REQUIRE(values_of(controller.process(move(130.0f, 70.0f, 10))) == std::vector<luil::hsv_color> { with(start_value, 0.5f, 0.75f), with(start_value, 0.6f, 0.75f) });
    // 판 밖에서 끝으로 잘린 값이 같은 이동은 말이 없다.
    REQUIRE(values_of(controller.process(move(-40.0f, -40.0f, 20))) == std::vector<luil::hsv_color> { with(start_value, 0.0f, 1.0f) });
    REQUIRE(values_of(controller.process(move(-60.0f, -50.0f, 30))).empty());
    REQUIRE(values_of(controller.process(release(-60.0f, -50.0f, 40))).empty());

    // 막대를 세로로 움직이면 값이 같아 말이 없다.
    REQUIRE(values_of(controller.process(press(60.0f, 236.0f, 1000))).empty());
    REQUIRE(values_of(controller.process(move(60.0f, 300.0f, 1010))).size() == 1u);
    REQUIRE(values_of(controller.process(move(60.0f, 400.0f, 1020))).empty());
    REQUIRE(values_of(controller.process(release(60.0f, 400.0f, 1030))).empty());
}

TEST_CASE("The parts carry a hand cursor and a start distance on all three", "[ui][color-picker]")
{
    const auto picker { make_picker(make_full_config()) };
    for (const luil::ui_element_id& id : { plane_id, hue_id, alpha_id })
    {
        const luil::ui_element& element { find(*picker, id) };
        REQUIRE(element.cursor() == luil::ui_cursor::hand);
        REQUIRE(element.tab_stop());
        REQUIRE(element.pointer_drag() != nullptr);
        REQUIRE(element.pointer_drag()->start_distance == luil::color_picker_drag_start_distance);
    }
    REQUIRE(find(*picker, plane_id).key_step()->axis == luil::focus_axis::vertical);
    REQUIRE(find(*picker, plane_id).key_step()->up_increases);
    REQUIRE(find(*picker, hue_id).key_step()->axis == luil::focus_axis::horizontal);
    // root는 Tab 자리가 아니다.
    REQUIRE(picker->tab_stop() == false);
}

TEST_CASE("The plane keys walk the value vertically and the saturation horizontally", "[ui][color-picker][focus]")
{
    const auto picker { make_picker(make_config(true, { 225.0f, 0.4f, 0.5f, 0.5f })) };
    const luil::ui_element& plane { find(*picker, plane_id) };
    const luil::hsv_color base { 225.0f, 0.4f, 0.5f, 0.5f };
    REQUIRE(stepped(plane, luil::value_step::increase) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.51f) });
    REQUIRE(stepped(plane, luil::value_step::decrease) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.49f) });
    REQUIRE(stepped(plane, luil::value_step::increase_page) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.6f) });
    REQUIRE(stepped(plane, luil::value_step::decrease_page) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.4f) });
    REQUIRE(stepped(plane, luil::value_step::minimum) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.0f) });
    REQUIRE(stepped(plane, luil::value_step::maximum) == std::vector<luil::hsv_color> { with(base, 0.4f, 1.0f) });

    // ←→는 키 처리기다. 수식 키를 든 키는 앱 단축키로 흐른다.
    REQUIRE(plane.key_handler_of() != nullptr);
    const auto handler { *plane.key_handler_of() };
    REQUIRE(values_of(*handler(key(luil::key_code::arrow_right))) == std::vector<luil::hsv_color> { with(base, 0.41f, 0.5f) });
    REQUIRE(values_of(*handler(key(luil::key_code::arrow_left))) == std::vector<luil::hsv_color> { with(base, 0.39f, 0.5f) });
    REQUIRE(handler(key(luil::key_code::arrow_right, true)).has_value() == false);
    REQUIRE(handler(key(luil::key_code::arrow_up)).has_value() == false);

    // 끝에 닿은 키는 빈 목록으로 삼킨다.
    const auto top { make_picker(make_config(true, { 225.0f, 1.0f, 1.0f, 1.0f })) };
    REQUIRE(stepped(find(*top, plane_id), luil::value_step::increase).empty());
    REQUIRE(stepped(find(*top, plane_id), luil::value_step::maximum).empty());
    const auto top_handler { *find(*top, plane_id).key_handler_of() };
    const std::optional<std::vector<luil::input_action>> swallowed { top_handler(key(luil::key_code::arrow_right)) };
    REQUIRE(swallowed.has_value());
    REQUIRE(swallowed->empty());
}

TEST_CASE("The controller routes the plane keys with the up arrow raising the value", "[ui][color-picker][focus]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(make_picker(make_config(true, { 225.0f, 0.4f, 0.5f, 0.5f }))));
    static_cast<void>(controller.process(press(110.0f, 120.0f, 0)));
    static_cast<void>(controller.process(release(110.0f, 120.0f, 10)));
    REQUIRE(controller.snapshot().focused == plane_id);
    // 누른 클릭으로 값이 (0.5, 0.5)로 바뀌었지만 tree는 그대로라 아직 (0.4, 0.5)다.
    const luil::hsv_color base { 225.0f, 0.4f, 0.5f, 0.5f };
    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_up))) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.51f) });
    REQUIRE(values_of(controller.process(key(luil::key_code::page_up))) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.6f) });
    REQUIRE(values_of(controller.process(key(luil::key_code::page_down))) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.4f) });
    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_right))) == std::vector<luil::hsv_color> { with(base, 0.41f, 0.5f) });
    REQUIRE(values_of(controller.process(key(luil::key_code::home))) == std::vector<luil::hsv_color> { with(base, 0.4f, 0.0f) });
    REQUIRE(values_of(controller.process(key(luil::key_code::arrow_right, true))).empty());
}

TEST_CASE("The bar keys walk one degree or one percent and ten per page", "[ui][color-picker][focus]")
{
    const auto picker { make_picker(make_config()) };
    const luil::ui_element& hue { find(*picker, hue_id) };
    const auto hue_of = [](const float value) {
        luil::hsv_color color { start_value };
        color.hue = value;
        return std::vector<luil::hsv_color> { color };
    };
    REQUIRE(stepped(hue, luil::value_step::increase) == hue_of(226.0f));
    REQUIRE(stepped(hue, luil::value_step::decrease) == hue_of(224.0f));
    REQUIRE(stepped(hue, luil::value_step::increase_page) == hue_of(235.0f));
    REQUIRE(stepped(hue, luil::value_step::decrease_page) == hue_of(215.0f));
    REQUIRE(stepped(hue, luil::value_step::minimum) == hue_of(0.0f));
    REQUIRE(stepped(hue, luil::value_step::maximum) == hue_of(360.0f));

    const luil::ui_element& alpha { find(*picker, alpha_id) };
    const auto alpha_of = [](const float value) {
        luil::hsv_color color { start_value };
        color.alpha = value;
        return std::vector<luil::hsv_color> { color };
    };
    REQUIRE(stepped(alpha, luil::value_step::increase) == alpha_of(0.51f));
    REQUIRE(stepped(alpha, luil::value_step::decrease_page) == alpha_of(0.4f));
    REQUIRE(stepped(alpha, luil::value_step::maximum) == alpha_of(1.0f));

    // 격자 사이 값은 이웃 격자점으로 간다.
    const auto between { make_picker(make_config(true, { 100.4f, 0.5f, 0.5f, 0.505f })) };
    REQUIRE(values_of(*find(*between, hue_id).key_step()->on_step(luil::value_step::increase))[0].hue == 101.0f);
    REQUIRE(values_of(*find(*between, hue_id).key_step()->on_step(luil::value_step::decrease))[0].hue == 100.0f);
    REQUIRE(values_of(*find(*between, alpha_id).key_step()->on_step(luil::value_step::increase))[0].alpha == 0.51f);
    // 끝은 삼킨다.
    const auto zero { make_picker(make_config(true, { 0.0f, 0.5f, 0.5f, 0.0f })) };
    REQUIRE(stepped(find(*zero, hue_id), luil::value_step::decrease).empty());
    REQUIRE(stepped(find(*zero, alpha_id), luil::value_step::minimum).empty());
}

TEST_CASE("Without a change message the picker is display only", "[ui][color-picker]")
{
    luil::color_picker_config config { make_full_config() };
    config.change_to = nullptr;
    const auto picker { make_picker(config) };
    for (const luil::ui_element_id& id : { plane_id, hue_id, alpha_id })
    {
        const luil::ui_element& element { find(*picker, id) };
        REQUIRE(element.tab_stop() == false);
        REQUIRE(element.pointer_drag() == nullptr);
        REQUIRE(element.action(luil::ui_trigger::left_click) == nullptr);
        REQUIRE(element.key_step() == nullptr);
        REQUIRE(element.cursor() == luil::ui_cursor::inherit);
    }
    // 막대는 읽기만 한다.
    REQUIRE(find(*picker, hue_id).access_actions({ luil::access_command::set_value, 10.0f }).has_value() == false);
    const luil::ui_element& row { *picker->children()[4] };
    REQUIRE(row.focus_group() == luil::focus_axis::none);
    REQUIRE(row.children()[0]->tab_stop() == false);
    REQUIRE(row.children()[0]->action(luil::ui_trigger::left_click) == nullptr);
}

TEST_CASE("A disabled picker sends nothing", "[ui][color-picker]")
{
    auto picker { std::make_unique<luil::color_picker_element>(make_full_config()) };
    picker->set_enabled(false);
    picker->arrange({ slot, 1.0f });
    const luil::ui_element& plane { find(*picker, plane_id) };
    REQUIRE(plane.enabled() == false);
    REQUIRE(find(*picker, hex_id).enabled() == false);
    REQUIRE(picker->children()[4]->children()[0]->enabled() == false);
    REQUIRE((*plane.action(luil::ui_trigger::left_click))({ plane_id, 110.0f, 70.0f }).empty());
    REQUIRE(plane.pointer_drag()->on_press({ plane_id, 110.0f, 70.0f }).empty());
    REQUIRE(stepped(plane, luil::value_step::increase).empty());
    REQUIRE(find(*picker, hue_id).access_actions({ luil::access_command::set_value, 10.0f })->empty());

    luil::interaction_controller controller {};
    controller.set_tree(make_tree(std::move(picker)));
    REQUIRE(values_of(controller.process(press(110.0f, 70.0f, 0))).empty());
    REQUIRE(values_of(controller.process(release(110.0f, 70.0f, 10))).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("A preset sends its color and keeps the hue of a gray", "[ui][color-picker]")
{
    const auto picker { make_picker(make_full_config()) };
    const luil::ui_element& row { *picker->children()[4] };
    REQUIRE(row.focus_group() == luil::focus_axis::horizontal);
    REQUIRE(row.focus_entry() == row.children()[0]->id());
    const auto click = [&row](const std::size_t index) { return values_of((*row.children()[index]->action(luil::ui_trigger::left_click))({ row.children()[index]->id() })); };
    REQUIRE(click(0) == std::vector<luil::hsv_color> { { 0.0f, 1.0f, 1.0f, 1.0f } });
    // 회색 견본은 지금 색상을 지킨다. 투명 검정은 색상과 채도를 지킨다.
    const std::vector<luil::hsv_color> gray { click(1) };
    REQUIRE(gray.size() == 1u);
    REQUIRE(gray[0] == luil::hsv_from_color(luil::make_ui_color(128, 128, 128), start_value));
    REQUIRE(gray[0].hue == 225.0f);
    REQUIRE(click(2) == std::vector<luil::hsv_color> { { 225.0f, 0.8f, 0.0f, 0.0f } });
    // 이름이 비면 16진 글이다.
    REQUIRE(row.children()[0]->accessibility() == luil::access_info { .role = luil::access_role::button, .name = u8"#FF0000FF" });
    REQUIRE(row.children()[1]->accessibility().name == u8"gray");
    REQUIRE(row.children()[1]->cursor() == luil::ui_cursor::hand);

    // 알파를 끄면 견본 색은 불투명으로 읽고 16진 글도 여섯 자리다.
    luil::color_picker_config opaque { make_full_config() };
    opaque.alpha = false;
    const auto plain { make_picker(opaque) };
    const luil::ui_element& plain_row { *plain->children()[3] };
    REQUIRE(plain_row.children()[2]->accessibility().name == u8"#000000");
    REQUIRE(values_of((*plain_row.children()[2]->action(luil::ui_trigger::left_click))({}))[0].alpha == 1.0f);
}

TEST_CASE("The hex field is the app kind and commits when the focus leaves it or on Enter", "[ui][color-picker]")
{
    const auto picker { make_picker(make_full_config()) };
    const luil::ui_element& hex { find(*picker, hex_id) };
    REQUIRE(dynamic_cast<const luil::text_input_element*>(&hex) != nullptr);
    REQUIRE(hex.accessibility().role == luil::access_role::edit);
    REQUIRE(hex.accessibility().name == u8"16진 색");
    REQUIRE(hex.accessibility().value == u8"#3366FF80");
    REQUIRE(hex.focus_leave_action() != nullptr);
    REQUIRE(commits_of((*hex.focus_leave_action())({ hex_id })) == 1u);
    REQUIRE(hex.key_handler_of() != nullptr);
    REQUIRE(commits_of(*(*hex.key_handler_of())(key(luil::key_code::enter))) == 1u);
    REQUIRE((*hex.key_handler_of())(key(luil::key_code::enter, true)).has_value() == false);
    REQUIRE((*hex.key_handler_of())(key(luil::key_code::escape)).has_value() == false);

    // 앱이 정한 오류는 칸의 오류다.
    luil::color_picker_config wrong { make_full_config() };
    wrong.hex->invalid = true;
    wrong.hex->name = u8"hex";
    const auto invalid { make_picker(wrong) };
    REQUIRE(find(*invalid, hex_id).accessibility().invalid);
    REQUIRE(find(*invalid, hex_id).accessibility().name == u8"hex");
}

TEST_CASE("Tab walks the plane, the bars, the hex field and the preset row in order", "[ui][color-picker][focus]")
{
    // 16진 칸이 편집 대상으로 서려면 앱 정책이 있어야 한다.
    class hex_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_hex ? std::optional<luil::text_input_target> { static_cast<luil::text_input_target>(3) } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            return luil::input_action {};
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            return {};
        }
    };
    hex_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(make_tree(make_picker(make_full_config())));
    const luil::ui_element_id first_preset { luil::ui_element_kind::color_picker_preset, part(u8"1") };
    std::vector<luil::ui_element_id> visited {};
    for (int index { 0 }; index < 5; ++index)
    {
        static_cast<void>(controller.process(key(luil::key_code::tab)));
        visited.push_back(controller.snapshot().focused);
    }
    REQUIRE(visited == std::vector<luil::ui_element_id> { plane_id, hue_id, alpha_id, hex_id, first_preset });
    // 견본 칸 사이는 ←→다. 줄은 Tab의 한 자리라 Tab이 다시 판으로 돈다.
    static_cast<void>(controller.process(key(luil::key_code::arrow_right)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::color_picker_preset, part(u8"2") });
    // Enter가 견본을 고른다.
    REQUIRE(values_of(controller.process(key(luil::key_code::enter))).size() == 1u);
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(controller.snapshot().focused == plane_id);
}

TEST_CASE("Leaving the hex field for another part commits the draft", "[ui][color-picker][focus]")
{
    class hex_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_hex ? std::optional<luil::text_input_target> { static_cast<luil::text_input_target>(3) } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            return luil::input_action {};
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            return {};
        }
    };
    hex_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(make_tree(make_picker(make_full_config())));
    for (int index { 0 }; index < 4; ++index)
        static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(controller.snapshot().focused == hex_id);
    REQUIRE(commits_of(controller.process(key(luil::key_code::tab))) == 1u);
    // 판을 누르는 것도 떠남이다.
    for (int index { 0 }; index < 4; ++index)
        static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(controller.snapshot().focused == hex_id);
    std::size_t pressed { commits_of(controller.process(press(110.0f, 70.0f, 0))) };
    pressed += commits_of(controller.process(release(110.0f, 70.0f, 10)));
    REQUIRE(pressed == 1u);
}

TEST_CASE("The color field opens with a click, Alt down, F4 and down and closes when open", "[ui][color-picker][color-field]")
{
    const luil::color_field_element closed { make_field(false) };
    REQUIRE(closed.id() == luil::ui_element_id { luil::ui_element_kind::color_field, u8"swatch" });
    REQUIRE(closed.cursor() == luil::ui_cursor::hand);
    REQUIRE(opens_of((*closed.action(luil::ui_trigger::left_click))({ closed.id() })) == std::vector<bool> { true });
    const auto& handler { *closed.key_handler_of() };
    REQUIRE(opens_of(*handler(key(luil::key_code::arrow_down, false, true))) == std::vector<bool> { true });
    REQUIRE(opens_of(*handler(key(luil::key_code::f4))) == std::vector<bool> { true });
    REQUIRE(opens_of(*handler(key(luil::key_code::arrow_down))) == std::vector<bool> { true });
    REQUIRE(handler(key(luil::key_code::arrow_up)).has_value() == false);
    // 닫혀 있으면 떠남 훅이 없다.
    REQUIRE(closed.focus_leave_action() == nullptr);

    const luil::color_field_element open { make_field(true) };
    REQUIRE(opens_of((*open.action(luil::ui_trigger::left_click))({ open.id() })) == std::vector<bool> { false });
    // 열린 동안 여는 키는 삼킨다.
    const std::optional<std::vector<luil::input_action>> swallowed { (*open.key_handler_of())(key(luil::key_code::f4)) };
    REQUIRE(swallowed.has_value());
    REQUIRE(swallowed->empty());
    REQUIRE(open.focus_leave_action() != nullptr);
    REQUIRE(opens_of((*open.focus_leave_action())({ open.id() })) == std::vector<bool> { false });
}

TEST_CASE("The color field reads as a combo box with its hex value and expands by target", "[ui][color-picker][color-field]")
{
    const luil::color_field_element closed { make_field(false) };
    const luil::access_info info { closed.accessibility() };
    REQUIRE(info.role == luil::access_role::combo_box);
    REQUIRE(info.name == u8"색");
    REQUIRE(info.expanded == false);
    REQUIRE(info.value == u8"#3366FF");
    REQUIRE(opens_of(*closed.access_actions({ luil::access_command::expand })) == std::vector<bool> { true });
    REQUIRE(opens_of(*closed.access_actions({ luil::access_command::collapse })) == std::vector<bool> { false });

    luil::color_field_config alpha { make_field(true) };
    alpha.alpha = true;
    alpha.name = u8"fill";
    const luil::color_field_element shown { alpha };
    REQUIRE(shown.accessibility().value == u8"#3366FF80");
    REQUIRE(shown.accessibility().expanded == true);
    REQUIRE(shown.accessibility().name == u8"fill");

    luil::color_field_config inert { make_field(false) };
    inert.set_open = nullptr;
    const luil::color_field_element still { inert };
    REQUIRE(still.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(still.access_actions({ luil::access_command::expand }).has_value() == false);
}

TEST_CASE("Pressing the open field and releasing without a new frame closes it", "[ui][color-picker][color-field]")
{
    luil::interaction_controller controller {};
    auto field { std::make_unique<luil::color_field_element>(make_field(true)) };
    field->arrange({ { 0.0f, 0.0f, 160.0f, luil::dropdown_height }, 1.0f });
    controller.set_tree(make_tree(std::move(field)));
    std::vector<bool> opens { opens_of(controller.process(press(40.0f, 14.0f, 0))) };
    for (const bool value : opens_of(controller.process(release(40.0f, 14.0f, 10))))
        opens.push_back(value);
    REQUIRE(opens == std::vector<bool> { false });
}

TEST_CASE("The popup is the picker size plus padding placed under the field", "[ui][color-picker][popup]")
{
    luil::color_picker_popup_config config {};
    config.id = u8"color-popup";
    config.scale = 2.0f;
    config.below = luil::field_popup_anchor { .field = { 100.0f, 50.0f, 160.0f, 56.0f }, .limit = { 0.0f, 0.0f, 1600.0f, 1600.0f } };
    config.dismiss = [](const luil::popup_dismiss_reason) { return luil::make_app_action(open_intent { false }); };
    const luil::ui_popup popup { luil::make_color_picker_popup(make_full_config(), config) };
    REQUIRE(popup.id == u8"color-popup");
    REQUIRE(popup.width == 216.0f);
    REQUIRE(popup.height == 336.0f);
    // 필드 아래 간격 2(논리)다. 좌표는 논리 픽셀이다.
    REQUIRE(popup.x == 50.0f);
    REQUIRE(popup.y == 55.0f);
    REQUIRE(popup.dismiss != nullptr);
    REQUIRE(popup.tree != nullptr);
    // tree는 배율 2로 배치된다. 판은 여백 8 안쪽이다.
    const luil::ui_element* const plane { popup.tree->find(plane_id) };
    REQUIRE(plane != nullptr);
    REQUIRE(same_rect(plane->bounds(), { 16.0f, 16.0f, 400.0f, 400.0f }));
    REQUIRE(popup.tree->find(hex_id) != nullptr);

    // 아래가 모자라고 위가 들어가면 위로 뒤집는다.
    config.below.field = { 100.0f, 1400.0f, 160.0f, 56.0f };
    const luil::ui_popup above { luil::make_color_picker_popup(make_full_config(), config) };
    REQUIRE(above.y == (1400.0f - 4.0f - 336.0f * 2.0f) / 2.0f);
}

TEST_CASE("Pressing the open field closes it even once the closed frame is published before the release", "[ui][color-picker][color-field][reopen]")
{
    // 열린 동안 칸을 누르면 바깥 누름이 먼저 닫고, 뗌 전에 `open == false` frame이 게시될 수 있다.
    // 펼쳐 있던 칸의 클릭은 누를 때의 tree의 액션이라 닫힌다 (menu-focus-fixes-design.md의 X-4). 다시 열리지 않는다.
    const auto field_tree = [](const bool open) {
        auto field { std::make_unique<luil::color_field_element>(make_field(open)) };
        field->arrange({ { 0.0f, 0.0f, 160.0f, luil::dropdown_height }, 1.0f });
        return make_tree(std::move(field));
    };
    luil::interaction_controller controller {};
    controller.set_tree(field_tree(true));
    std::vector<bool> opens { opens_of(controller.process(press(40.0f, 14.0f, 0))) };
    controller.set_tree(field_tree(false));
    for (const bool value : opens_of(controller.process(release(40.0f, 14.0f, 10))))
        opens.push_back(value);
    REQUIRE(opens == std::vector<bool> { false });

    // 닫힌 칸의 누름은 지금처럼 뗌 tree의 것이다. 누르는 사이 열린 frame이 게시되면 그 tree의 `set_open(false)`다.
    controller.set_tree(field_tree(false));
    opens = opens_of(controller.process(press(40.0f, 14.0f, 1000)));
    controller.set_tree(field_tree(true));
    for (const bool value : opens_of(controller.process(release(40.0f, 14.0f, 1010))))
        opens.push_back(value);
    REQUIRE(opens == std::vector<bool> { false });
}

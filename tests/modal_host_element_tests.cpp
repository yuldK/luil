#include "luil/ui/modal_host_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "raster_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_dialog { luil::application_element_kind(0) };

    struct close_intent
    {
        std::u8string reason {};
    };

    [[nodiscard]] luil::ui_action make_close(std::u8string reason)
    {
        return [reason = std::move(reason)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(close_intent { reason }) }; };
    }

    // 자식을 자기 자리 그대로 겹쳐 두는 test 컨테이너다 (뒤 화면 + modal host).
    class test_shell final : public luil::ui_element
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
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    // dialog 본체다.
    // 버튼을 아래쪽 절반에만 두어 위쪽에 **빈 자리**가 남는다 — 그 자리를 눌러
    // 뒤의 scrim으로 새는지 볼 수 있어야 한다.
    class test_dialog final : public luil::ui_element
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
            const luil::rect_f lower { context.slot.x, context.slot.y + context.slot.height / 2.0f, context.slot.width, context.slot.height / 2.0f };
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context.for_child(lower));
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] std::unique_ptr<test_dialog> make_content()
    {
        auto body { std::make_unique<test_dialog>(luil::ui_element_id { kind_dialog, u8"body" }) };
        auto confirm { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_dialog, u8"confirm" }, luil::panel_config {}) };
        confirm->set_action(luil::ui_trigger::left_click, make_close(u8"confirm"));
        body->add(std::move(confirm));
        return body;
    }
}

TEST_CASE("A modal host centers its content and follows the app offset", "[ui][modal]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;

    const auto place = [&config] {
        auto host { std::make_unique<luil::modal_host_element>(config) };
        host->set_content(make_content());
        return luil::make_arranged_tree(std::move(host), { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f);
    };

    // 크기는 논리 픽셀이고 arrange가 배율을 곱한다.
    const luil::ui_tree centered { place() };
    const luil::ui_element* const body { centered.find(luil::ui_element_id { kind_dialog, u8"body" }) };
    REQUIRE(body != nullptr);
    REQUIRE(body->bounds().width == 200.0f);
    REQUIRE(body->bounds().height == 80.0f);
    REQUIRE(body->bounds().x == 100.0f);
    REQUIRE(body->bounds().y == 60.0f);

    // scrim은 준 자리를 그대로 덮는다.
    const luil::ui_element* const scrim { centered.find(luil::ui_element_id { luil::ui_element_kind::modal_scrim }) };
    REQUIRE(scrim != nullptr);
    REQUIRE(scrim->bounds().width == 400.0f);
    REQUIRE(scrim->bounds().height == 200.0f);

    // 잡아 끈 만큼 가운데에서 밀려난다. 그 값은 앱 상태라 host가 기억하지 않는다.
    config.offset_x = 30.0f;
    config.offset_y = -10.0f;
    const luil::ui_tree moved { place() };
    const luil::ui_element* const shifted { moved.find(luil::ui_element_id { kind_dialog, u8"body" }) };
    REQUIRE(shifted != nullptr);
    REQUIRE(shifted->bounds().x == 160.0f);
    REQUIRE(shifted->bounds().y == 40.0f);
}

TEST_CASE("A modal host blocks the pointer and traps the focus", "[ui][modal]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;
    config.outside = make_close(u8"outside");

    auto shell { std::make_unique<test_shell>(luil::ui_element_id { luil::ui_element_kind::root }) };
    auto behind { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_dialog, u8"behind" }, luil::panel_config {}) };
    behind->set_action(luil::ui_trigger::left_click, make_close(u8"behind"));
    shell->add(std::move(behind));

    auto host { std::make_unique<luil::modal_host_element>(config) };
    host->set_content(make_content());
    shell->add(std::move(host));

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(shell), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };

    // 뒤의 element는 scrim이 흡수해 눌리지 않는다.
    const luil::ui_element* const behind_hit { tree.hit_test(10.0f, 10.0f) };
    REQUIRE(behind_hit != nullptr);
    REQUIRE(behind_hit->id().kind == luil::ui_element_kind::modal_scrim);

    // dialog의 빈 자리를 눌러도 뒤의 scrim으로 새지 않는다.
    // 앱이 흡수 액션을 손으로 달지 않아도 host가 세운다.
    const luil::ui_element* const body_hit { tree.hit_test(155.0f, 85.0f) };
    REQUIRE(body_hit != nullptr);
    REQUIRE(body_hit->id() == luil::ui_element_id { kind_dialog, u8"body" });

    // Tab은 dialog 안만 돈다. scrim은 눌리지만 자리가 아니다.
    REQUIRE(tree.focus_trap() != nullptr);
    REQUIRE(tree.focus_trap()->id().kind == luil::ui_element_kind::modal_host);
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { luil::ui_element_id { kind_dialog, u8"confirm" } });
}

TEST_CASE("A modal host takes the scrim and the Esc route from its config", "[ui][modal]")
{
    // 없는 것은 두지 않는다: dismiss가 비어 있으면 Esc를 받지 않는다.
    const luil::modal_host_element silent { luil::modal_host_config {} };
    REQUIRE(silent.dismiss_action() == nullptr);
    REQUIRE(silent.focus_trap());

    luil::modal_host_config config {};
    config.owner = u8"confirm";
    config.dismiss = make_close(u8"escape");
    const luil::modal_host_element host { config };
    REQUIRE(host.id() == luil::ui_element_id { luil::ui_element_kind::modal_host, u8"confirm" });
    REQUIRE(host.dismiss_action() != nullptr);

    const std::vector<luil::input_action> actions { (*host.dismiss_action())(luil::ui_action_context {}) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<close_intent>() != nullptr);
    REQUIRE(message->get<close_intent>()->reason == u8"escape");

    // scrim은 진하기와 무관하게 언제나 있다 — 포인터를 막는 것이 modal의 몫이다.
    luil::modal_host_config clear {};
    clear.scrim_opacity = 0.0f;
    const luil::modal_host_element invisible { clear };
    REQUIRE(invisible.children().size() == 1u);
    REQUIRE(invisible.children()[0]->id().kind == luil::ui_element_kind::modal_scrim);
    REQUIRE(invisible.children()[0]->hit_opaque());
    REQUIRE(invisible.children()[0]->tab_stop() == false);
}

TEST_CASE("A scrim selector answers with the alpha it chose, not with the opacity", "[ui][modal][raster]")
{
    // 이 축은 픽셀로 본다. scrim의 색은 host가 만든 panel의 설정 안에 있어 밖에서
    // 물을 수 없고, "선택자가 이겼다"를 말할 수 있는 자리는 실제로 칠해진 색뿐이다.
    const luil::ui_color_palette dark { luil::color_palette_for(luil::color_theme::dark) };
    const auto paint = [&dark](const luil::modal_host_config& config) {
        auto host { std::make_unique<luil::modal_host_element>(config) };
        host->set_content(make_content());
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(host), { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        luil::testing::raster_frame frame { 400, 200, dark, 2.0f };
        frame.draw(tree, luil::interaction_snapshot {});
        // 모서리는 scrim만 덮은 자리다 — 내용은 가운데에 있다.
        return frame.pixel_at(10, 10);
    };

    SECTION("선택자를 주면 scrim_opacity는 쓰이지 않는다")
    {
        // 진하기를 0으로 두어 갈림을 뚜렷하게 만든다. 선택자의 색에 진하기를 다시
        // 곱했다면 아무것도 그려지지 않아 배경색이 나온다.
        luil::modal_host_config config {};
        config.scrim_opacity = 0.0f;
        config.scrim_background = [](const luil::ui_color_palette& palette) { return palette.notice_background; };
        REQUIRE(paint(config) == dark.notice_background);
    }

    SECTION("선택자가 없으면 그림자 역할에 진하기를 얹는다")
    {
        // 지금까지의 scrim이다. 진하기 0은 보이지 않는 scrim이고(막는 것은 그대로다),
        // 기본 진하기는 배경과 섞인 값이라 어느 팔레트 색과도 같지 않다.
        luil::modal_host_config clear {};
        clear.scrim_opacity = 0.0f;
        REQUIRE(paint(clear) == dark.window_background);

        const luil::modal_host_config dimmed {};
        REQUIRE(paint(dimmed) != dark.window_background);
        REQUIRE(paint(dimmed) != dark.content_shadow);
    }
}

TEST_CASE("A modal host can wear the dialog surface for the app", "[ui][modal]")
{
    // dialog를 세우는 앱이 예외 없이 같은 다섯 줄(표면 panel로 내용 감싸기)을 적고
    // 있었다. 그 다섯 줄을 설정 한 줄로 옮긴 것이라, 자리와 흡수는 하나도 달라지지
    // 않아야 한다 — 그것이 이 test가 잠그는 전부다.
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;

    const auto place = [&config] {
        auto host { std::make_unique<luil::modal_host_element>(config) };
        host->set_content(make_content());
        host->arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f });
        return host;
    };

    // 표면이 없는 오늘의 배치다. 아래의 두 SECTION이 이 값과 견준다.
    const std::unique_ptr<luil::modal_host_element> bare { place() };
    REQUIRE(bare->children().size() == 2u);
    const luil::ui_element* const bare_content { bare->children()[1].get() };
    REQUIRE(bare_content->id() == luil::ui_element_id { kind_dialog, u8"body" });

    SECTION("설정에 표면이 없으면 아무것도 끼어들지 않는다")
    {
        // 없는 것은 만들지 않는다 — 지금까지의 host가 이 설정의 특수 경우다.
        REQUIRE(bare_content->bounds().x == 100.0f);
        REQUIRE(bare_content->bounds().y == 60.0f);
        REQUIRE(bare_content->bounds().width == 200.0f);
        REQUIRE(bare_content->bounds().height == 80.0f);
        REQUIRE(bare_content->hit_opaque());
    }

    SECTION("표면이 있으면 host와 내용 사이에 선다")
    {
        luil::panel_config surface {};
        surface.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface.corner_radius = 8.0f;
        config.surface = surface;

        const std::unique_ptr<luil::modal_host_element> host { place() };
        REQUIRE(host->children().size() == 2u);
        const luil::ui_element* const panel { host->children()[1].get() };
        // 가운데 자리를 받는 것은 표면이고, 내용은 그 안에서 같은 자리를 물려받는다 —
        // 크기를 대신 재 주지 않으므로 두 상자가 정확히 겹친다.
        REQUIRE(panel->children().size() == 1u);
        const luil::ui_element* const content { panel->children()[0].get() };
        REQUIRE(content->id() == luil::ui_element_id { kind_dialog, u8"body" });
        REQUIRE(panel->bounds().x == bare_content->bounds().x);
        REQUIRE(panel->bounds().y == bare_content->bounds().y);
        REQUIRE(panel->bounds().width == bare_content->bounds().width);
        REQUIRE(panel->bounds().height == bare_content->bounds().height);
        REQUIRE(content->bounds().x == panel->bounds().x);
        REQUIRE(content->bounds().y == panel->bounds().y);
        REQUIRE(content->bounds().width == panel->bounds().width);
        REQUIRE(content->bounds().height == panel->bounds().height);

        // 흡수는 그대로 내용의 몫이다. 표면이 생겼다고 dialog의 빈 자리가 뒤로 새면
        // 바깥 클릭으로 닫히는 dialog가 자기 몸을 눌러도 닫힌다.
        REQUIRE(content->hit_opaque());
    }
}

namespace {
    // 크기를 묻는 말에 정해 둔 답을 하고 무엇을 물었는지 남기는 내용이다.
    class sized_content final : public luil::ui_element
    {
    public:
        sized_content(luil::ui_element_id id, std::optional<luil::size_f> answer)
            : ui_element { std::move(id) }
            , answer_ { answer }
        {
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }

        [[nodiscard]] std::optional<luil::size_f> preferred_size(const luil::measure_context& context, const float available_width) const override
        {
            ++asks;
            asked_width = available_width;
            asked_scale = context.scale;
            asked_measurer = context.measurer;
            asked_style = context.style;
            return answer_;
        }

        mutable int asks { 0 };
        mutable float asked_width { 0.0f };
        mutable float asked_scale { 0.0f };
        mutable const luil::text_measurer* asked_measurer { nullptr };
        mutable const luil::ui_style* asked_style { nullptr };

    private:
        std::optional<luil::size_f> answer_ {};
    };

    const luil::ui_element_id sized_id { kind_dialog, u8"sized" };

    struct placed_host
    {
        std::unique_ptr<luil::modal_host_element> host {};
        const sized_content* content { nullptr };
    };

    // 내용을 넣고 `slot`(물리 픽셀)에 배치한 host다.
    [[nodiscard]] placed_host place_sized(const luil::modal_host_config& config, const std::optional<luil::size_f> answer, const luil::rect_f& slot, const float scale, const luil::text_measurer* measurer = nullptr, const luil::ui_style* style = nullptr)
    {
        auto content { std::make_unique<sized_content>(sized_id, answer) };
        const sized_content* const observed { content.get() };
        auto host { std::make_unique<luil::modal_host_element>(config) };
        host->set_content(std::move(content));
        host->arrange({ .slot = slot, .scale = scale, .measurer = measurer, .style = style });
        return { std::move(host), observed };
    }
}

TEST_CASE("A modal host that fits its height asks the content, at the width it will have", "[ui][modal][fit]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;
    config.fit_height = true;

    const luil::text_measurer measurer { [](const std::u8string_view text, const float size) { return static_cast<float>(text.size()) * size; } };
    luil::ui_style style { luil::default_ui_style() };
    style.text.wrap_policy = luil::line_break_policy::unicode;
    const placed_host placed { place_sized(config, luil::size_f { 100.0f, 70.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f, &measurer, &style) };

    // 높이는 내용의 답이고 폭은 설정이다. 가운데에 놓이는 것은 전과 같다.
    REQUIRE(placed.content->asks == 1);
    REQUIRE(placed.content->asked_width == 100.0f);
    // 측정 문맥이 배치 문맥이다 — 같은 배율과 같은 측정기, 같은 앱 스타일이다 (본문이 스타일의 줄 나눔 정책으로 잰다).
    REQUIRE(placed.content->asked_scale == 2.0f);
    REQUIRE(placed.content->asked_measurer == &measurer);
    REQUIRE(placed.content->asked_style == &style);
    REQUIRE(placed.content->bounds().width == 200.0f);
    REQUIRE(placed.content->bounds().height == 140.0f);
    REQUIRE(placed.content->bounds().x == 100.0f);
    REQUIRE(placed.content->bounds().y == 30.0f);

    SECTION("내용이 답하지 않으면 content_height다")
    {
        const placed_host silent { place_sized(config, std::nullopt, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(silent.content->asks == 1);
        REQUIRE(silent.content->bounds().height == 80.0f);
        REQUIRE(silent.content->bounds().y == 60.0f);
    }

    SECTION("끈 host는 내용을 묻지 않고 content_height를 쓴다")
    {
        config.fit_height = false;
        const placed_host plain { place_sized(config, luil::size_f { 100.0f, 70.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(plain.content->asks == 0);
        REQUIRE(plain.content->bounds().height == 80.0f);
    }

    SECTION("밀어낸 양은 높이가 달라져도 그대로 더해진다")
    {
        config.offset_x = 30.0f;
        config.offset_y = -10.0f;
        const placed_host moved { place_sized(config, luil::size_f { 100.0f, 70.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(moved.content->bounds().x == 160.0f);
        REQUIRE(moved.content->bounds().y == 10.0f);
    }
}

TEST_CASE("A modal host with a surface asks the content inside it, not the surface", "[ui][modal][fit]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;
    config.fit_height = true;
    config.surface = luil::panel_config {};

    const placed_host placed { place_sized(config, luil::size_f { 100.0f, 60.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };

    // panel은 크기를 답하지 않는다. 표면이 있어도 앱이 넣은 내용의 답이 높이가 된다.
    REQUIRE(placed.content->asks == 1);
    REQUIRE(placed.host->children().size() == 2u);
    const luil::ui_element& surface { *placed.host->children()[1] };
    REQUIRE(surface.bounds().height == 60.0f);
    REQUIRE(surface.bounds().y == 70.0f);
    REQUIRE(placed.content->bounds().height == 60.0f);
    REQUIRE(placed.content->bounds().y == 70.0f);
}

TEST_CASE("A modal host trims its content to the room inside the edge margin", "[ui][modal][fit]")
{
    luil::modal_host_config config {};
    config.content_width = 300.0f;
    config.content_height = 40.0f;
    config.fit_height = true;
    config.edge_margin = 16.0f;

    // 자리는 논리 200 x 100이다. 여백 두 배를 빼면 168 x 68이다.
    const placed_host placed { place_sized(config, luil::size_f { 300.0f, 150.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };

    // 폭을 먼저 자르고 그 폭으로 묻는다. 답한 높이도 자른다.
    REQUIRE(placed.content->asked_width == 168.0f);
    REQUIRE(placed.content->bounds().width == 336.0f);
    REQUIRE(placed.content->bounds().height == 136.0f);
    REQUIRE(placed.content->bounds().x == 32.0f);
    REQUIRE(placed.content->bounds().y == 32.0f);

    SECTION("자리에 넉넉하면 자르지 않는다")
    {
        config.content_width = 100.0f;
        const placed_host roomy { place_sized(config, luil::size_f { 100.0f, 30.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(roomy.content->asked_width == 100.0f);
        REQUIRE(roomy.content->bounds().width == 200.0f);
        REQUIRE(roomy.content->bounds().height == 60.0f);
    }

    SECTION("묻지 않는 host도 여백이 있으면 자른다")
    {
        config.fit_height = false;
        const placed_host plain { place_sized(config, std::nullopt, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(plain.content->asks == 0);
        REQUIRE(plain.content->bounds().width == 336.0f);
        REQUIRE(plain.content->bounds().height == 80.0f);
    }

    SECTION("여백이 0이면 자르지 않는다")
    {
        config.edge_margin = 0.0f;
        const placed_host loose { place_sized(config, luil::size_f { 300.0f, 150.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        REQUIRE(loose.content->asked_width == 300.0f);
        REQUIRE(loose.content->bounds().width == 600.0f);
        REQUIRE(loose.content->bounds().height == 300.0f);
    }

    SECTION("자리가 여백보다 좁아도 음수 크기는 없다")
    {
        const placed_host tiny { place_sized(config, luil::size_f { 300.0f, 150.0f }, { 0.0f, 0.0f, 20.0f, 20.0f }, 1.0f) };
        REQUIRE(tiny.content->bounds().width == 0.0f);
        REQUIRE(tiny.content->bounds().height == 0.0f);
    }
}

#include "luil/ui/scroll_area_element.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/modal_host_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // test 정의 kind다.
    // 앱이 자기 kind를 정의하는 것과 같은 경로다.
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_content { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_split { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_overlay { luil::application_element_kind(3) };

    // 행 하나의 높이다 (논리 픽셀).
    // 내용 높이는 언제나 행 수 × 이 값이라, 어느 행이 창의 어디에 서는지를
    // test가 손으로 셀 수 있다.
    constexpr float row_height { 50.0f };

    // 앱이 logic inbox로 나르는 스크롤 메시지다.
    // owner를 함께 실어 **어느 영역이 낸 것인지**를 본다 — 표 없는 라우팅에서
    // 임자를 잘못 고르는 것이 곧 owner가 다른 메시지다.
    struct scroll_intent
    {
        std::u8string owner {};
        float delta { 0.0f };
    };

    // 흘릴 내용이다. 자식을 위에서부터 행 높이만큼 쌓는다.
    class content_stack final : public luil::ui_element
    {
    public:
        content_stack(luil::ui_element_id id, const float height)
            : ui_element { std::move(id) }
            , height_ { height }
        {}

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        // 이 내용이 배치된 횟수다.
        // 한 frame에 두 번 불리면 배치 중에 자식을 쌓는 내용(가상 목록이 행을 짓는
        // 자리)이 그것을 두 배로 센다.
        [[nodiscard]] int arranges() const noexcept
        {
            return arranges_;
        }

        void arrange(const luil::arrange_context& context) override
        {
            ++arranges_;
            set_bounds(context.slot);
            float y { context.slot.y };
            for (const std::unique_ptr<ui_element>& child : children())
            {
                child->arrange(context.for_child({ context.slot.x, y, context.slot.width, height_ * context.scale }));
                y += height_ * context.scale;
            }
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

    private:
        float height_ { 0.0f };
        int arranges_ { 0 };
    };

    // 내용 안에 놓인 컨트롤이다.
    // 누를 수 있으므로 `tab_stop`의 기본값이 이것을 자리로 만든다 — 영역이
    // 그 판정을 가로채지 않는다는 것을 잠그는 자리다.
    class test_row final : public luil::ui_element
    {
    public:
        explicit test_row(luil::ui_element_id id)
            : ui_element { std::move(id) }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 영역 여럿을 가로로 똑같이 나눠 세우는 판이다.
    class split_panel final : public luil::ui_element
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
            if (children().empty())
                return;
            const float width { context.slot.width / static_cast<float>(children().size()) };
            float x { context.slot.x };
            for (const std::unique_ptr<ui_element>& child : children())
            {
                child->arrange(context.for_child({ x, context.slot.y, width, context.slot.height }));
                x += width;
            }
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 자식 전부에게 자기 자리를 통째로 주는 판이다.
    // 나중에 담긴 것이 위에 선다 — modal이 화면을 덮는 그 순서다.
    class overlay_panel final : public luil::ui_element
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
                child->arrange(context.for_child(context.slot));
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] luil::ui_element_id row_id(const std::u8string_view owner, const std::size_t index)
    {
        return luil::ui_element_id { kind_row, std::u8string { owner } + u8'-' + static_cast<char8_t>(u8'a' + index) };
    }

    [[nodiscard]] luil::ui_element_id bar_id(const std::u8string_view owner)
    {
        return luil::ui_element_id { luil::ui_element_kind::scroll_area_bar, std::u8string { owner } };
    }

    [[nodiscard]] luil::scroll_area_config make_config(const std::u8string_view owner, const std::size_t rows)
    {
        luil::scroll_area_config config {};
        config.owner = std::u8string { owner };
        config.content_height = static_cast<float>(rows) * row_height;
        config.scroll = [key = std::u8string { owner }](const float delta) { return luil::make_app_action(scroll_intent { key, delta }); };
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::scroll_area_element> make_area(luil::scroll_area_config config, const std::size_t rows)
    {
        const std::u8string owner { config.owner };
        auto area { std::make_unique<luil::scroll_area_element>(std::move(config)) };
        auto content { std::make_unique<content_stack>(luil::ui_element_id { kind_content, owner }, row_height) };
        for (std::size_t index = 0; index < rows; ++index)
            content->add(std::make_unique<test_row>(row_id(owner, index)));
        area->set_content(std::move(content));
        return area;
    }

    // 바깥 영역 안에 안쪽 영역을 세운다.
    //
    // 앞의 빈 칸(`band` 높이)이 안쪽 영역을 아래로 민다. 그래서 **안쪽에서는 보이는
    // 행이 바깥에서는 밀려 나가 있는** 자리가 생기고, 그것이 되살리기가 한 겹으로
    // 끝나면 안 되는 이유다.
    [[nodiscard]] std::unique_ptr<luil::scroll_area_element> make_nested(const float band, const std::size_t inner_rows)
    {
        auto outer { std::make_unique<luil::scroll_area_element>(make_config(u8"page", 12)) };
        auto content { std::make_unique<content_stack>(luil::ui_element_id { kind_content, u8"page" }, band) };
        content->add(std::make_unique<test_row>(row_id(u8"page", 0)));
        content->add(make_area(make_config(u8"note", inner_rows), inner_rows));
        outer->set_content(std::move(content));
        return outer;
    }

    // 조립이 바뀌어도 test가 짚는 자리는 이 두 도우미 안에서만 바뀐다.
    [[nodiscard]] const luil::ui_element& area_view(const luil::scroll_area_element& area)
    {
        return *area.children()[0];
    }

    // 창이 실제로 넘겨준 폭을 보는 자리다.
    // **글이 막대 밑을 지나지 않는가**를 묻는 것이 이 값 하나다.
    [[nodiscard]] const luil::ui_element& area_content(const luil::scroll_area_element& area)
    {
        return *area_view(area).children()[0];
    }

    // index번 input_action에서 앱 메시지 T를 꺼낸다.
    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    // 첫 input_action에서 앱 메시지 T를 꺼낸다.
    template<typename message_type>
    [[nodiscard]] const message_type* single_message(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return nullptr;
        return message_at<message_type>(actions, 0);
    }

    [[nodiscard]] bool has_stop(const std::vector<luil::ui_element_id>& order, const luil::ui_element_id& id)
    {
        return std::find(order.begin(), order.end(), id) != order.end();
    }
} // namespace

TEST_CASE("A scroll area reports the same logical metrics at every scale", "[ui][scroll]")
{
    // 치수는 전부 **논리 픽셀**이다. 배율이 바뀌어도 같은 값이 나오는 것이
    // 계약이고, 그 나눗셈이 빠지면 고DPI에서 앱이 두 배로 흘린다.
    SECTION("배율 1에서는 물리와 논리가 같다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.scroll_offset = 120.0f;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        const luil::scroll_metrics& metrics { area->metrics() };
        REQUIRE(metrics.content_height == 500.0f);
        // 창이 **실제로 받은** 높이다. 앱이 배치 상수에서 따로 구하면 그 둘이
        // 어긋나는 frame이 반드시 생긴다.
        REQUIRE(metrics.viewport_height == 300.0f);
        REQUIRE(metrics.maximum_scroll == 200.0f);
        REQUIRE(metrics.scroll_offset == 120.0f);
        REQUIRE(metrics.overflowing());

        // 창이 보이는 자리는 물리 픽셀이고 막대 칸만큼 좁다.
        REQUIRE(area->viewport().height == 300.0f);
        REQUIRE(area->viewport().width == 200.0f - luil::scroll_area_bar_width);
        REQUIRE(tree.unarranged().empty());
        REQUIRE(tree.duplicate_ids().empty());
    }

    SECTION("배율 2에서도 치수는 논리 픽셀 그대로다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.scroll_offset = 120.0f;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        // 같은 논리 크기(200×300)를 배율 2로 준다.
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 400.0f, 600.0f }, 2.0f) };

        const luil::scroll_metrics& metrics { area->metrics() };
        REQUIRE(metrics.content_height == 500.0f);
        REQUIRE(metrics.viewport_height == 300.0f);
        REQUIRE(metrics.maximum_scroll == 200.0f);
        REQUIRE(metrics.scroll_offset == 120.0f);

        // 물리 쪽은 배율이 곱해진다 — 막대 칸도 함께 넓어진다.
        REQUIRE(area->viewport().height == 600.0f);
        REQUIRE(area->viewport().width == 400.0f - luil::scroll_area_bar_width * 2.0f);
        REQUIRE(area_content(*area).bounds().width == 400.0f - luil::scroll_area_bar_width * 2.0f);
        // 내용은 흘러간 만큼(논리 120 × 배율 2) 위로 올라간다.
        REQUIRE(area_content(*area).bounds().y == -240.0f);
        REQUIRE(tree.unarranged().empty());
    }
}

TEST_CASE("A scroll area clamps the offset it was given and the bar reads the same number", "[ui][scroll]")
{
    // 앱 상태가 범위를 벗어나 오는 것은 흔한 일이다 (내용이 줄어든 frame).
    // 다듬은 값 하나를 **셋이 함께** 본다 — 치수·막대·창이 갈리면 thumb가 끝에
    // 붙어 있는데 마지막 줄이 보이지 않는다.
    SECTION("범위를 넘은 값은 최대치로 다듬는다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.scroll_offset = 999.0f;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().maximum_scroll == 200.0f);
        REQUIRE(area->metrics().scroll_offset == 200.0f);

        // 창은 다듬은 값으로 내용을 올렸다.
        REQUIRE(area_content(*area).bounds().y == -200.0f);

        // 막대가 든 값도 같다 — 보조 기술이 읽는 범위가 곧 치수다.
        const luil::ui_element& bar { *area->children()[1] };
        const luil::access_info info { bar.accessibility() };
        REQUIRE(info.range.has_value());
        REQUIRE(info.range->minimum == 0.0f);
        REQUIRE(info.range->maximum == area->metrics().maximum_scroll);
        REQUIRE(info.range->value == area->metrics().scroll_offset);
        REQUIRE(tree.unarranged().empty());
    }

    SECTION("음수는 0으로 다듬는다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.scroll_offset = -80.0f;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().scroll_offset == 0.0f);
        REQUIRE(area_content(*area).bounds().y == 0.0f);
        const luil::access_info info { area->children()[1]->accessibility() };
        REQUIRE(info.range.has_value());
        REQUIRE(info.range->value == 0.0f);
        REQUIRE(tree.unarranged().empty());
    }
}

TEST_CASE("A scrollbar takes its column only where the visibility rule says so", "[ui][scroll]")
{
    // 어느 갈래에서든 **창이 실제로 받은 폭**을 함께 본다.
    // 막대를 겹쳐 그리면 글의 마지막 글자가 그 밑을 지난다.
    SECTION("automatic은 흘릴 것이 있을 때만 막대를 세운다")
    {
        auto owned { make_area(make_config(u8"notes", 2), 2) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 내용 100, 창 300이라 흘릴 것이 없다.
        REQUIRE(area->metrics().overflowing() == false);
        REQUIRE(area->children().size() == 2u);
        // 숨긴 것은 그려지지도 눌리지도 않는다 — "보이지 않는데 잡히는" 칸을
        // 남기지 않는다.
        REQUIRE(area->children()[1]->visible() == false);
        REQUIRE(area->children()[1]->enabled() == false);
        // 칸을 통째로 내주므로 글이 넓게 선다.
        REQUIRE(area_content(*area).bounds().width == 200.0f);
        REQUIRE(tree.unarranged().empty());
    }

    SECTION("automatic은 흘릴 것이 있으면 막대를 세운다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 10.0f, 20.0f, 200.0f, 300.0f }, 1.0f) };

        const luil::ui_element& bar { *area->children()[1] };
        REQUIRE(bar.id() == luil::ui_element_id { luil::ui_element_kind::scroll_area_bar, u8"notes" });
        REQUIRE(bar.visible());
        REQUIRE(bar.bounds().x == 10.0f + 200.0f - luil::scroll_area_bar_width);
        REQUIRE(bar.bounds().width == luil::scroll_area_bar_width);
        REQUIRE(area_content(*area).bounds().width == 200.0f - luil::scroll_area_bar_width);
        REQUIRE(tree.unarranged().empty());
    }

    SECTION("always는 흘릴 것이 없어도 자리를 지킨다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 2) };
        config.bar = luil::scrollbar_visibility::always;
        auto owned { make_area(std::move(config), 2) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing() == false);
        REQUIRE(area->children()[1]->visible());
        REQUIRE(area->children()[1]->bounds().width == luil::scroll_area_bar_width);
        // 내용이 늘고 주는 창에서 글의 폭이 흔들리지 않는다 — 흔들리면 한 줄
        // 늘어난 것만으로 문단 전체가 다시 접힌다.
        REQUIRE(area_content(*area).bounds().width == 200.0f - luil::scroll_area_bar_width);
        REQUIRE(tree.unarranged().empty());
    }

    SECTION("never는 흘릴 것이 있어도 막대를 만들지 않는다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.bar = luil::scrollbar_visibility::never;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing());
        REQUIRE(area->children().size() == 1u);
        REQUIRE(area_content(*area).bounds().width == 200.0f);
        // `scroll` factory가 없는 것과 다르다 — 흘리기는 그대로 산다.
        REQUIRE(area->scroll() != nullptr);
        auto actions { luil::route_wheel(tree, 100.0f, 100.0f, 48.0f) };
        REQUIRE(single_message<scroll_intent>(actions) != nullptr);
        REQUIRE(tree.unarranged().empty());
    }
}

TEST_CASE("A scroll area arranges its content once in a frame", "[ui][scroll]")
{
    // 흘릴 것이 있는지는 내용 높이와 창 높이만의 함수라 **창을 배치하기 전에** 답이
    // 나온다. 창을 한 번 배치해 보고 물으면 그 답을 얻자고 배치를 두 번 하게 되고,
    // 배치 중에 자식을 쌓는 내용(가상 목록이 행을 짓는 자리)이 그것을 두 배로 센다.
    auto owned { make_area(make_config(u8"notes", 10), 10) };
    const luil::scroll_area_element* const area { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

    REQUIRE(area->metrics().overflowing());
    REQUIRE(static_cast<const content_stack&>(area_content(*area)).arranges() == 1);
    // 그 한 번이 **막대가 가져간 칸을 뺀** 폭이다. 두 번 재던 시절의 첫 배치가
    // 전체 폭이었던 자리다.
    REQUIRE(area_content(*area).bounds().width == 200.0f - luil::scroll_area_bar_width);
    REQUIRE(tree.unarranged().empty());
}

TEST_CASE("The scrollbar is a Tab stop only where there is something to scroll", "[ui][scroll][focus]")
{
    SECTION("bar_tab_stop이 거짓이면 흘릴 것이 있어도 자리가 아니다")
    {
        // 담는 쪽이 이미 세로 키를 가진 자리다 (목록·가상 목록). 그러지 않으면 같은
        // 목록에 Tab의 자리가 둘 서고, 사용자는 아무것도 하지 않는 자리를 한 번 더 지난다.
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.bar_tab_stop = false;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing());
        REQUIRE(area->children()[1]->visible());
        REQUIRE(area->children()[1]->enabled());
        REQUIRE(area->children()[1]->tab_stop() == false);
        REQUIRE(has_stop(tree.focus_order(), bar_id(u8"notes")) == false);
    }

    SECTION("흘릴 것이 없으면 참이어도 자리가 아니다")
    {
        // `automatic`은 그 frame에 막대를 숨기기까지 한다.
        auto owned { make_area(make_config(u8"notes", 2), 2) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing() == false);
        REQUIRE(area->children()[1]->visible() == false);
        REQUIRE(area->children()[1]->tab_stop() == false);
        REQUIRE(has_stop(tree.focus_order(), bar_id(u8"notes")) == false);
    }

    SECTION("always로 서 있어도 흘릴 것이 없으면 자리가 아니다")
    {
        // 자리를 지키는 막대는 짧은 내용에서도 보이지만 끌 수도 누를 수도 없다.
        // 그것이 Tab의 자리로 남으면 사용자는 아무 일도 하지 않는 자리를 한 번 더
        // 지나고, 거기서 누른 키는 조용히 사라진다.
        luil::scroll_area_config config { make_config(u8"notes", 2) };
        config.bar = luil::scrollbar_visibility::always;
        auto owned { make_area(std::move(config), 2) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing() == false);
        REQUIRE(area->children()[1]->visible());
        REQUIRE(area->children()[1]->tab_stop() == false);
        REQUIRE(has_stop(tree.focus_order(), bar_id(u8"notes")) == false);
    }

    SECTION("흘릴 것이 있으면 자리다")
    {
        luil::scroll_area_config config { make_config(u8"notes", 10) };
        config.bar = luil::scrollbar_visibility::always;
        auto owned { make_area(std::move(config), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        REQUIRE(area->metrics().overflowing());
        REQUIRE(area->children()[1]->tab_stop());
        REQUIRE(has_stop(tree.focus_order(), bar_id(u8"notes")));
    }
}

TEST_CASE("A scroll area narrower than its bar keeps the bar inside its own bounds", "[ui][scroll]")
{
    // 막대의 폭은 **창보다 넓을 수 없다.** 아주 좁은 칸에서 다듬지 않으면 막대의
    // 왼쪽 끝이 영역 밖으로 나가, 화면에는 남의 자리에 그려지고 휠은 영역 밖이라
    // 아무도 받지 않는다.
    const float narrow { luil::scroll_area_bar_width / 2.0f };
    auto owned { make_area(make_config(u8"notes", 10), 10) };
    const luil::scroll_area_element* const area { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 40.0f, 0.0f, narrow, 300.0f }, 1.0f) };

    const luil::ui_element& bar { *area->children()[1] };
    REQUIRE(bar.visible());
    REQUIRE(bar.bounds().width == narrow);
    REQUIRE(bar.bounds().x == area->bounds().x);
    REQUIRE(bar.bounds().x + bar.bounds().width == area->bounds().x + area->bounds().width);
    // 창에 남은 폭이 없다. 좁기는 어느 쪽이든 마찬가지지만 이쪽은 남의 자리를
    // 침범하지 않는다.
    REQUIRE(area_view(*area).bounds().width == 0.0f);
    // 휠은 그래도 이 영역이 받는다 — 임자를 찾는 것은 영역의 bounds다.
    REQUIRE(single_message<scroll_intent>(luil::route_wheel(tree, 42.0f, 100.0f, 48.0f)) != nullptr);
    REQUIRE(tree.unarranged().empty());
}

TEST_CASE("Without a scroll factory a scroll area is a plain clipping window", "[ui][scroll]")
{
    // "없는 것은 두지 않는다" — 한 값이 넷을 함께 끈다.
    luil::scroll_area_config config { make_config(u8"notes", 10) };
    config.scroll = {};
    auto owned { make_area(std::move(config), 10) };
    const luil::scroll_area_element* const area { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

    // 막대도 없고 휠의 임자도 아니다.
    REQUIRE(area->children().size() == 1u);
    REQUIRE(area->scroll() == nullptr);
    REQUIRE(luil::route_wheel(tree, 100.0f, 100.0f, 48.0f).empty());
    REQUIRE(luil::route_reveal(tree, row_id(u8"notes", 7)).empty());

    // 그래도 치수는 답한다 — 흘리지 않기로 한 것과 재지 못하는 것은 다르다.
    REQUIRE(area->metrics().maximum_scroll == 200.0f);
    REQUIRE(area_content(*area).bounds().width == 200.0f);
    REQUIRE(tree.unarranged().empty());
}

TEST_CASE("route_wheel finds the scroll area under the pointer without a table", "[ui][scroll][interaction]")
{
    // 앱이 표를 짓지 않는다. policy의 몸통이
    // `return route_wheel(tree, event.x, event.y, delta);` 한 줄이 된다.
    SECTION("포인터를 덮는 영역이 임자다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        auto actions { luil::route_wheel(tree, 100.0f, 100.0f, 48.0f) };
        const scroll_intent* const message { single_message<scroll_intent>(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(message->owner == u8"notes");
        REQUIRE(message->delta == 48.0f);

        // 덮는 것이 없으면 빈 목록이다.
        REQUIRE(luil::route_wheel(tree, 500.0f, 500.0f, 48.0f).empty());
    }

    SECTION("나란한 두 영역은 저마다 자기 것을 받는다")
    {
        auto panel { std::make_unique<split_panel>(luil::ui_element_id { kind_split }) };
        panel->add(make_area(make_config(u8"left", 10), 0));
        panel->add(make_area(make_config(u8"right", 10), 0));
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(panel), { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f) };

        auto left { luil::route_wheel(tree, 50.0f, 50.0f, 48.0f) };
        const scroll_intent* const first { single_message<scroll_intent>(left) };
        REQUIRE(first != nullptr);
        REQUIRE(first->owner == u8"left");

        auto right { luil::route_wheel(tree, 250.0f, 50.0f, 48.0f) };
        const scroll_intent* const second { single_message<scroll_intent>(right) };
        REQUIRE(second != nullptr);
        REQUIRE(second->owner == u8"right");
        REQUIRE(tree.duplicate_ids().empty());
    }

    SECTION("겹친 영역에서는 안쪽이 임자다")
    {
        // 바깥 창(200×300) 안에 안쪽 영역(300 높이)이 통째로 든다.
        auto inner { make_area(make_config(u8"note", 18), 0) };
        luil::scroll_area_config outer_config { make_config(u8"page", 12) };
        auto outer { std::make_unique<luil::scroll_area_element>(std::move(outer_config)) };
        auto content { std::make_unique<content_stack>(luil::ui_element_id { kind_content, u8"page" }, 300.0f) };
        content->add(std::move(inner));
        outer->set_content(std::move(content));
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(outer), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 그리기 순서를 거슬러 가장 안쪽·가장 위의 것을 찾는다.
        auto actions { luil::route_wheel(tree, 50.0f, 100.0f, 48.0f) };
        const scroll_intent* const message { single_message<scroll_intent>(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(message->owner == u8"note");

        // 바깥이 자기 막대에 내준 칸은 안쪽 영역 밖이라 바깥의 것이다.
        auto edge { luil::route_wheel(tree, 192.0f, 100.0f, 48.0f) };
        const scroll_intent* const outside { single_message<scroll_intent>(edge) };
        REQUIRE(outside != nullptr);
        REQUIRE(outside->owner == u8"page");
        REQUIRE(tree.unarranged().empty());
    }
}

TEST_CASE("Nested reveal uses the target after the inner scroll at each DPI", "[ui][scroll][interaction]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        auto outer { std::make_unique<luil::scroll_area_element>(make_config(u8"page", 20)) };
        auto content { std::make_unique<content_stack>(luil::ui_element_id { kind_content, u8"page" }, 800.0f) };
        content->add(make_area(make_config(u8"note", 20), 20));
        outer->set_content(std::move(content));
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(outer), { 0.0f, 0.0f, 200.0f * scale, 300.0f * scale }, scale) };
        const auto actions { luil::route_reveal(tree, row_id(u8"note", 18)) };
        REQUIRE(actions.size() == 2u);
        const auto* inner { message_at<scroll_intent>(actions, 0) };
        const auto* outside { message_at<scroll_intent>(actions, 1) };
        REQUIRE(inner != nullptr);
        REQUIRE(outside != nullptr);
        REQUIRE(inner->owner == u8"note");
        REQUIRE(inner->delta == 150.0f);
        REQUIRE(outside->owner == u8"page");
        REQUIRE(outside->delta == 500.0f);
    }
}

TEST_CASE("A modal scrim swallows the wheel that would scroll the page behind it", "[ui][scroll][interaction]")
{
    // 포인터를 막는 것이 modal의 몫이라면 **휠도 포인터다.** 이 방벽이 없으면
    // dialog가 떠 있는 동안 scrim 위에서 굴린 휠이 뒤의 화면을 흘린다.
    const auto page = [](const bool modal) {
        auto panel { std::make_unique<overlay_panel>(luil::ui_element_id { kind_overlay }) };
        panel->add(make_area(make_config(u8"notes", 10), 10));
        if (modal)
            panel->add(std::make_unique<luil::modal_host_element>(luil::modal_host_config { .owner = u8"confirm" }));
        return luil::make_arranged_tree(std::move(panel), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f);
    };

    // 덮은 것이 없으면 영역이 임자다.
    const luil::ui_tree open { page(false) };
    REQUIRE(single_message<scroll_intent>(luil::route_wheel(open, 100.0f, 100.0f, 48.0f)) != nullptr);

    // 흘리지 않고 삼키기만 하는 것이 먼저 걸리면 거기서 끝난다.
    const luil::ui_tree blocked { page(true) };
    const luil::ui_element* const scrim { blocked.find({ luil::ui_element_kind::modal_scrim, u8"confirm" }) };
    REQUIRE(scrim != nullptr);
    REQUIRE(scrim->hit_opaque());
    REQUIRE(luil::route_wheel(blocked, 100.0f, 100.0f, 48.0f).empty());
    REQUIRE(blocked.unarranged().empty());
}

TEST_CASE("route_reveal chains outward through every scrolling ancestor", "[ui][scroll][focus]")
{
    // 한 겹만 보고 끝내면 겹친 창에서 초점이 화면 밖에 남는다 — 행은 안쪽 목록
    // 안에서 보이는데 그 목록이 바깥 판에서 밀려 나가 있는 경우다. 안쪽이 답한
    // 뒤에는 **그 안쪽 창 자체**가 다음 질문의 대상이 된다.
    SECTION("안쪽에서 보이는 행도 안쪽이 밀려 나가 있으면 바깥이 흘린다")
    {
        // 안쪽 영역은 [200, 400)이고 그 안의 마지막 행은 안쪽 창에 온전히 든다.
        // 바깥 창은 [0, 300)이라 안쪽 영역의 아래 100이 밀려 나가 있다.
        const luil::ui_tree tree { luil::make_arranged_tree(make_nested(200.0f, 4), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        auto actions { luil::route_reveal(tree, row_id(u8"note", 3)) };
        REQUIRE(actions.size() == 1u);
        const scroll_intent* const message { message_at<scroll_intent>(actions, 0) };
        REQUIRE(message != nullptr);
        REQUIRE(message->owner == u8"page");
        REQUIRE(message->delta == 100.0f);
    }

    SECTION("둘 다 밀려 나가 있으면 두 메시지가 안쪽부터 나온다")
    {
        // 안쪽 영역(창 200, 내용 400) 안에서 여섯째 행이 아래로 100 넘치고, 그
        // 영역 자신도 바깥 창에서 100 넘친다.
        const luil::ui_tree tree { luil::make_arranged_tree(make_nested(200.0f, 8), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        auto actions { luil::route_reveal(tree, row_id(u8"note", 5)) };
        REQUIRE(actions.size() == 2u);
        const scroll_intent* const inner { message_at<scroll_intent>(actions, 0) };
        const scroll_intent* const outer { message_at<scroll_intent>(actions, 1) };
        REQUIRE(inner != nullptr);
        REQUIRE(outer != nullptr);
        REQUIRE(inner->owner == u8"note");
        REQUIRE(inner->delta == 100.0f);
        REQUIRE(outer->owner == u8"page");
        REQUIRE(outer->delta == 100.0f);
    }

    SECTION("전부 보이면 아무 메시지도 내지 않는다")
    {
        // 안쪽 영역이 [150, 300)이라 바깥 창 [0, 300) 안에 통째로 든다.
        // 방벽이 겹겹이 살아 있어야 화살표 한 번이 0짜리 메시지 둘을 내지 않는다.
        const luil::ui_tree tree { luil::make_arranged_tree(make_nested(150.0f, 3), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 빈 목록이 "찾지 못했다"가 아니라 "움직일 것이 없다"인지 먼저 못 박는다.
        REQUIRE(tree.find(row_id(u8"note", 1)) != nullptr);
        REQUIRE(luil::route_reveal(tree, row_id(u8"note", 1)).empty());
        REQUIRE(tree.duplicate_ids().empty());
        REQUIRE(tree.unarranged().empty());
    }
}

TEST_CASE("route_reveal scrolls the area that contains the focused element", "[ui][scroll][focus]")
{
    // 표가 없으므로 **휠의 임자와 되살리기의 임자가 같은 element다.**
    // 두 표가 서로 다른 id를 이름 대던 자리가 여기서 사라진다.
    SECTION("창 밖의 행은 넘친 만큼만 흘린다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 여덟째 행은 물리 [350, 400)이고 창은 [0, 300)이다.
        auto actions { luil::route_reveal(tree, row_id(u8"notes", 7)) };
        const scroll_intent* const message { single_message<scroll_intent>(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(message->owner == u8"notes");
        REQUIRE(message->delta == 100.0f);

        // 마지막 행을 부르면 최대치까지 흘린다.
        auto last { luil::route_reveal(tree, row_id(u8"notes", 9)) };
        const scroll_intent* const bottom { single_message<scroll_intent>(last) };
        REQUIRE(bottom != nullptr);
        REQUIRE(bottom->delta == 200.0f);
    }

    SECTION("이미 보이면 아무 메시지도 내지 않는다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 방벽이다 — 없으면 화살표를 누를 때마다 0짜리 메시지가 logic을 깨워
        // tree를 통째로 다시 짓는다.
        REQUIRE(luil::route_reveal(tree, row_id(u8"notes", 0)).empty());
        // tree에 없는 초점도 빈 목록이다.
        REQUIRE(luil::route_reveal(tree, row_id(u8"notes", 20)).empty());
    }

    SECTION("막대 칸을 품은 바깥을 이름 대도 답이 같다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::scroll_area_element* const area { owned.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

        // 이 영역의 bounds는 막대 칸까지 품어 **창보다 넓다.** 표를 둘로 나눠야
        // 했던 바로 그 자리다.
        REQUIRE(area->bounds().width == 200.0f);
        REQUIRE(area_view(*area).bounds().width == 200.0f - luil::scroll_area_bar_width);
        // 그런데도 흘릴 메시지를 든 것은 **바깥인 영역 하나**다.
        REQUIRE(area->scroll() != nullptr);
        REQUIRE(area_view(*area).scroll() == nullptr);

        const luil::ui_element* const row { tree.find(row_id(u8"notes", 7)) };
        REQUIRE(row != nullptr);
        // 바깥에 물어도 안쪽 창이 답한다 — 두 값이 갈리면 마지막 행이 막대 밑에
        // 남는다.
        REQUIRE(area->scroll_delta_to_reveal(row->bounds()) == area_view(*area).scroll_delta_to_reveal(row->bounds()));
        REQUIRE(area->scroll_delta_to_reveal(row->bounds()) == 100.0f);

        // 그래서 표를 짓더라도 **영역 하나만** 이름 대면 된다. 표 있는 짝과 표
        // 없는 짝이 같은 답을 낸다.
        const luil::scroll_route routes[] {
            { luil::ui_element_id { luil::ui_element_kind::scroll_area, u8"notes" }, [](const float delta) { return luil::make_app_action(scroll_intent { u8"notes", delta }); } },
        };
        auto tabled { luil::route_reveal(tree, row_id(u8"notes", 7), routes) };
        auto tableless { luil::route_reveal(tree, row_id(u8"notes", 7)) };
        const scroll_intent* const named { single_message<scroll_intent>(tabled) };
        const scroll_intent* const found { single_message<scroll_intent>(tableless) };
        REQUIRE(named != nullptr);
        REQUIRE(found != nullptr);
        REQUIRE(named->delta == found->delta);
    }

    SECTION("배율 2에서도 답은 논리 픽셀이다")
    {
        auto owned { make_area(make_config(u8"notes", 10), 10) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 400.0f, 600.0f }, 2.0f) };

        // 여덟째 행은 물리 [700, 800), 창은 [0, 600)이라 물리로는 200 넘친다.
        auto actions { luil::route_reveal(tree, row_id(u8"notes", 7)) };
        const scroll_intent* const message { single_message<scroll_intent>(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(message->delta == 100.0f);
    }
}

TEST_CASE("A control inside the content keeps its hit test and its Tab stop", "[ui][scroll][focus]")
{
    auto owned { make_area(make_config(u8"notes", 10), 10) };
    const luil::scroll_area_element* const area { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };

    // 창 안의 행은 잡히고, 잘린 행은 잡히지 않는다 (그리기와 hit test가 함께 잘린다).
    REQUIRE(tree.hit_test(100.0f, 25.0f) == tree.find(row_id(u8"notes", 0)));
    REQUIRE(tree.hit_test(100.0f, 350.0f) == nullptr);
    // 막대 칸은 막대의 것이다 — 칸을 내준 것이 그림뿐이 아니라는 확인이다.
    REQUIRE(tree.hit_test(192.0f, 25.0f) == tree.find({ luil::ui_element_kind::scroll_area_bar, u8"notes" }));

    // 자리 판정은 그대로다. 영역은 묶음이 아니라 담는 그릇이라 안의 컨트롤이
    // 저마다 Tab의 자리로 남고, 홀로 서는 막대는 흘릴 것이 있는 frame에서 자기
    // 자리를 자처한다 — 목록이 그것을 거절하는 것은 목록 자신이 ↑/↓를 가진
    // 묶음이기 때문이고 여기에는 그런 이유가 없다.
    const std::vector<luil::ui_element_id> order { tree.focus_order() };
    REQUIRE(order.size() == 11u);
    REQUIRE(order.front() == row_id(u8"notes", 0));
    // 창 밖으로 잘린 행도 자리에 남는다 (가상화는 내용의 몫이다).
    REQUIRE(order[7] == row_id(u8"notes", 7));
    REQUIRE(order.back() == bar_id(u8"notes"));
    REQUIRE(area->metrics().overflowing());
    REQUIRE(area->children()[1]->tab_stop());
}

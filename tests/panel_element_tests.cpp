#include "luil/ui/panel_element.h"

#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <utility>

namespace {
    constexpr float raster_scale { 2.0f };
    constexpr luil::ui_color red { 0xFFFF0000 };

    // 자기 자리를 통째로 한 색으로 칠하는 내용이다.
    // 테두리가 내용 **위에** 서는지를 보려면 내용이 가장자리까지 채워야 한다.
    class fill_element final : public luil::ui_element
    {
    public:
        fill_element(luil::ui_element_id id, const luil::ui_color color)
            : ui_element { std::move(id) }
            , color_ { color }
        {}

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot&) const override
        {
            const luil::rect_f box { bounds() };
            SkPaint paint { luil::solid_paint(color_) };
            paint.setAntiAlias(false);
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), paint);
        }

    private:
        luil::ui_color color_ { 0 };
    };

    // 자식 하나를 정해진 자리에 세우는 판이다.
    // panel 바깥에 자리가 있어야 그림자가 드리울 곳이 생긴다.
    class placed_root final : public luil::ui_element
    {
    public:
        placed_root(luil::ui_element_id id, const luil::rect_f& child_slot)
            : ui_element { std::move(id) }
            , child_slot_ { child_slot }
        {}

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context.for_child({ child_slot_.x * scale, child_slot_.y * scale, child_slot_.width * scale, child_slot_.height * scale }));
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            draw_children(context, interaction);
        }

    private:
        luil::rect_f child_slot_ {};
    };

    // 한 픽셀의 밝기다 (세 채널의 합). 그림자의 진하기를 견주는 자리다.
    [[nodiscard]] int brightness(const luil::ui_color color) noexcept
    {
        return static_cast<int>((color >> 16U) & 0xFFU) + static_cast<int>((color >> 8U) & 0xFFU) + static_cast<int>(color & 0xFFU);
    }

    // panel 하나를 `slot`(논리 픽셀)에 세운 tree다. 내용이 있으면 자리를 통째로 칠한다.
    [[nodiscard]] luil::ui_tree build(luil::panel_config config, const luil::rect_f& root_slot, const luil::rect_f& slot, const bool with_content)
    {
        auto root { std::make_unique<placed_root>(luil::ui_element_id { luil::ui_element_kind::root }, slot) };
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { luil::application_element_kind(0), u8"card" }, std::move(config)) };
        if (with_content)
            panel->set_content(std::make_unique<fill_element>(luil::ui_element_id { luil::application_element_kind(1), u8"content" }, red));
        root->add(std::move(panel));
        return luil::make_arranged_tree(std::move(root), { root_slot.x * raster_scale, root_slot.y * raster_scale, root_slot.width * raster_scale, root_slot.height * raster_scale }, raster_scale);
    }
} // namespace

TEST_CASE("A panel strokes its border over the content", "[ui][panel][raster]")
{
    // 테두리는 내용 **뒤에** 긋는다. 내용이 가장자리까지 채우는 카드(목록을 담은 panel)에서
    // 먼저 그으면 내용이 그 위를 덮어 카드의 경계가 사라진다 — 픽셀만이 그 순서를 말한다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::rect_f slot { 0.0f, 0.0f, 100.0f, 50.0f };

    SECTION("테두리 선택자가 있으면 둘레 1px이 그 색이고 그 안쪽은 내용이다")
    {
        luil::panel_config config {};
        config.border = [](const luil::ui_color_palette& colors) { return colors.accent; };
        const luil::ui_tree tree { build(std::move(config), slot, slot, true) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        // 획은 1 논리 픽셀 = 물리 2픽셀이고 상자 안쪽에 온전히 든다.
        REQUIRE(frame.pixel_at(0, 50) == palette.accent);
        REQUIRE(frame.pixel_at(1, 50) == palette.accent);
        REQUIRE(frame.pixel_at(100, 0) == palette.accent);
        REQUIRE(frame.pixel_at(199, 50) == palette.accent);
        REQUIRE(frame.pixel_at(100, 99) == palette.accent);
        // 내용은 획 바로 안쪽부터 그대로다.
        REQUIRE(frame.pixel_at(2, 50) == red);
        REQUIRE(frame.pixel_at(100, 2) == red);
        REQUIRE(frame.pixel_at(100, 50) == red);
    }

    SECTION("선택자가 없으면 내용이 가장자리까지 그대로다")
    {
        const luil::ui_tree tree { build(luil::panel_config {}, slot, slot, true) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(0, 50) == red);
        REQUIRE(frame.pixel_at(199, 99) == red);
    }
}

TEST_CASE("A panel shadow falls outside the box and deeper below it", "[ui][panel][raster]")
{
    // 그림자는 상자 **밖**의 것이다. 상자 안은 바탕이 덮어 어느 진하기에서도 같고,
    // 바깥은 아래쪽이 위쪽보다 진하다 — 빛이 위에서 온다는 그 규칙이 픽셀에 남는다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::rect_f root_slot { 0.0f, 0.0f, 200.0f, 100.0f };
    // 물리 픽셀로 x 100..300, y 50..150이다.
    const luil::rect_f slot { 50.0f, 25.0f, 100.0f, 50.0f };

    SECTION("진하기를 주면 바깥에 드리우고 안은 바탕 그대로다")
    {
        luil::panel_config config {};
        config.shadow = 0.5f;
        const luil::ui_tree tree { build(std::move(config), root_slot, slot, false) };
        luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(200, 100) == palette.surface_background);
        REQUIRE(frame.pixel_at(101, 51) == palette.surface_background);
        // 바로 바깥은 어두워졌고 멀리 떨어진 자리는 배경 그대로다.
        const luil::ui_color below { frame.pixel_at(200, 152) };
        const luil::ui_color above { frame.pixel_at(200, 47) };
        REQUIRE(below != palette.window_background);
        REQUIRE(frame.pixel_at(10, 10) == palette.window_background);
        REQUIRE(frame.pixel_at(200, 199) == palette.window_background);
        // 아래쪽이 위쪽보다 진하다.
        REQUIRE(brightness(below) < brightness(above));
    }

    SECTION("진하기 0이면 바깥에 아무것도 없다")
    {
        const luil::ui_tree tree { build(luil::panel_config {}, root_slot, slot, false) };
        luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(200, 152) == palette.window_background);
        REQUIRE(frame.pixel_at(200, 47) == palette.window_background);
        REQUIRE(frame.pixel_at(200, 100) == palette.surface_background);
    }
}

#include "host/font_registry.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {
    // 밑줄은 글의 축이라 UI 글꼴이 있어야 선다 (이 실행 파일은 OS 글꼴을 싣는다). 도형만 보는 raster 축(luil_core_tests)에는
    // 글꼴이 없어 글 폭이 0이다.
    constexpr float native_scale { 2.0f };

    class native_group final : public luil::ui_element
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

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            draw_children(context, interaction);
        }
    };

    [[nodiscard]] SkBitmap draw_tree(const luil::ui_tree& tree, const luil::interaction_snapshot& interaction, const int width, const int height)
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        const sk_sp<SkTypeface> typeface { luil::configured_ui_typeface() };
        SkBitmap pixels {};
        pixels.allocN32Pixels(width, height);
        SkCanvas canvas { pixels };
        canvas.clear(palette.window_background);
        luil::draw_context context { .canvas = canvas, .ui_typeface = typeface.get(), .palette = palette, .scale = native_scale };
        tree.draw(context, interaction);
        return pixels;
    }

    // 두 그림이 다른 픽셀의 수와, 그 가운데 가장 위의 줄이다.
    struct difference
    {
        int count { 0 };
        int top { -1 };
    };

    [[nodiscard]] difference compare(const SkBitmap& first, const SkBitmap& second)
    {
        difference result {};
        for (int y { 0 }; y < first.height(); ++y)
            for (int x { 0 }; x < first.width(); ++x)
                if (first.getColor(x, y) != second.getColor(x, y))
                {
                    if (result.top < 0)
                        result.top = y;
                    ++result.count;
                }
        return result;
    }

    [[nodiscard]] int painted(const SkBitmap& image)
    {
        const SkColor background { image.getColor(image.width() - 1, image.height() - 1) };
        int count { 0 };
        for (int y { 0 }; y < image.height(); ++y)
            for (int x { 0 }; x < image.width(); ++x)
                if (image.getColor(x, y) != background)
                    ++count;
        return count;
    }

    [[nodiscard]] SkBitmap draw_bar(const bool keyboard)
    {
        luil::menu_bar_config config {};
        config.owner = u8"bar";
        config.items = { { .key = u8"file", .label = u8"File", .access_key = U'f' }, { .key = u8"tools", .label = u8"Tools", .access_key = U'x' } };
        // 선 항목이 없다. 선 항목의 바탕 없이 글과 밑줄만 견준다.
        config.state = { u8"", false, keyboard };
        config.set_state = [](const luil::menu_bar_state&) { return luil::input_action {}; };
        auto root { std::make_unique<native_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 80.0f }, native_scale });
        auto bar { std::make_unique<luil::menu_bar_element>(std::move(config)) };
        bar->arrange({ .slot = { 0.0f, 0.0f, 400.0f, luil::menu_bar_height * native_scale }, .scale = native_scale });
        root->add(std::move(bar));
        const luil::ui_tree tree { std::move(root) };
        return draw_tree(tree, {}, 400, 80);
    }

    [[nodiscard]] SkBitmap draw_menu(const bool menu_keys)
    {
        luil::menu_config config {};
        config.items = { { .key = u8"new", .label = u8"New", .access_key = U'n' } };
        config.select = [](const std::u8string&) { return luil::input_action {}; };
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::menu_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 68.0f }, native_scale) };
        luil::interaction_snapshot interaction {};
        interaction.menu_keys = menu_keys;
        return draw_tree(tree, interaction, 200, 68);
    }
}

TEST_CASE("A menu bar underlines its access keys only when entered from the keyboard", "[ui][raster][menu-bar]")
{
    const SkBitmap pointer { draw_bar(false) };
    const SkBitmap keyboard { draw_bar(true) };
    // 글이 그려졌다.
    REQUIRE(painted(pointer) > 0);
    // 밑줄만 다르다. 글자 아래, 막대 줄의 가운데보다 아래에 선다.
    const difference underline { compare(pointer, keyboard) };
    REQUIRE(underline.count > 0);
    REQUIRE(underline.top > static_cast<int>(luil::menu_bar_height * native_scale / 2.0f));
    REQUIRE(underline.top < static_cast<int>(luil::menu_bar_height * native_scale));
}

TEST_CASE("A menu item underlines its access key only while the menu bar holds the keys", "[ui][raster][menu-bar]")
{
    const SkBitmap plain { draw_menu(false) };
    const SkBitmap keys { draw_menu(true) };
    REQUIRE(painted(plain) > 0);
    const difference underline { compare(plain, keys) };
    REQUIRE(underline.count > 0);
    // 항목 줄(위 여백 4, 높이 26)의 가운데보다 아래다.
    REQUIRE(underline.top > static_cast<int>((4.0f + 13.0f) * native_scale));
}

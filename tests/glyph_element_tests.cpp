#include "luil/ui/glyph_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "win32/embedded_assets.h"
#include "win32/resource_ids.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {
    constexpr luil::rect_f slot { 24.0f, 24.0f, 48.0f, 48.0f };
    constexpr auto ink { luil::make_ui_color(230, 100, 40) };

    [[nodiscard]] SkBitmap render(const luil::glyph_config& config, SkTypeface* const builtin = nullptr, const float scale = 1.0f)
    {
        SkBitmap bitmap {};
        bitmap.allocN32Pixels(96, 96);
        SkCanvas canvas { bitmap };
        canvas.clear(SK_ColorTRANSPARENT);
        auto palette { luil::color_palette_for(luil::color_theme::dark) };
        palette.primary_foreground = ink;
        luil::draw_context context { .canvas = canvas, .codicon_typeface = builtin, .palette = palette, .scale = scale };
        const int saved { canvas.getSaveCount() };
        luil::glyph_element element { {}, config };
        element.arrange({ .slot = slot, .scale = scale });
        element.draw(context, {});
        REQUIRE(canvas.getSaveCount() == saved);
        return bitmap;
    }

    [[nodiscard]] int ink_count(const SkBitmap& bitmap)
    {
        int count { 0 };
        for (int y = 0; y < bitmap.height(); ++y)
        {
            for (int x = 0; x < bitmap.width(); ++x)
            {
                if (bitmap.getColor(x, y) != SK_ColorTRANSPARENT)
                    ++count;
            }
        }
        return count;
    }
} // namespace

TEST_CASE("Public font loaders own bytes and read Unicode file paths", "[fonts][glyph]")
{
    const auto resource { luil::win32::find_embedded_resource(IDR_CODICONS_FONT) };
    REQUIRE(resource.data != nullptr);
    const auto* begin { static_cast<const std::uint8_t*>(resource.data) };
    std::vector<std::uint8_t> bytes { begin, begin + resource.size };
    const auto font { luil::load_typeface_bytes(bytes) };
    REQUIRE(font != nullptr);
    std::fill(bytes.begin(), bytes.end(), std::uint8_t { 0 });
    REQUIRE(font->unicharToGlyph(luil::codicons::icon_add) != 0);

    struct temporary_font
    {
        std::filesystem::path path;
        ~temporary_font()
        {
            std::error_code error {};
            std::filesystem::remove(path, error);
        }
    } file { std::filesystem::temp_directory_path() / (L"luil-글꼴-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".ttf") };
    {
        std::ofstream output { file.path, std::ios::binary };
        output.write(static_cast<const char*>(resource.data), static_cast<std::streamsize>(resource.size));
        REQUIRE(output.good());
    }
    const auto from_file { luil::load_typeface_file(file.path) };
    REQUIRE(from_file != nullptr);
    REQUIRE(std::filesystem::remove(file.path));
    REQUIRE(from_file->unicharToGlyph(luil::codicons::icon_add) != 0);
    REQUIRE(luil::load_typeface_file(file.path) == nullptr);
    REQUIRE(luil::load_typeface_file(std::filesystem::temp_directory_path()) == nullptr);
    REQUIRE(luil::load_typeface_bytes({}) == nullptr);
    REQUIRE(luil::load_typeface_bytes(bytes) == nullptr);
    REQUIRE(luil::load_typeface_bytes({ begin, resource.size }, -1) == nullptr);
    REQUIRE(luil::load_typeface_bytes({ begin, resource.size }, 999) == nullptr);
}

TEST_CASE("Glyph elements use Codicons by default and retain a custom font", "[ui][fonts][glyph]")
{
    auto font { luil::load_codicon_typeface() };
    REQUIRE(font != nullptr);
    const luil::glyph_config builtin { .glyph = luil::codicons::icon_add, .font_size = 24.0f };
    const SkBitmap expected { render(builtin, font.get()) };
    REQUIRE(ink_count(expected) > 0);
    int left { 96 };
    int right { 0 };
    int top { 96 };
    int bottom { 0 };
    for (int y = 0; y < expected.height(); ++y)
    {
        for (int x = 0; x < expected.width(); ++x)
        {
            if (expected.getColor(x, y) == SK_ColorTRANSPARENT)
                continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    REQUIRE(left + right >= 94);
    REQUIRE(left + right <= 96);
    REQUIRE(top + bottom >= 94);
    REQUIRE(top + bottom <= 96);
    luil::glyph_config custom { builtin };
    custom.typeface = font;
    font.reset();
    const SkBitmap actual { render(custom) };
    for (int y = 0; y < actual.height(); ++y)
    {
        for (int x = 0; x < actual.width(); ++x)
            REQUIRE(actual.getColor(x, y) == expected.getColor(x, y));
    }
    REQUIRE(ink_count(render(custom, nullptr, 1.5f)) > ink_count(actual));
    REQUIRE(ink_count(render(custom, nullptr, 2.0f)) > ink_count(render(custom, nullptr, 1.5f)));
    REQUIRE(ink_count(render(builtin)) == 0);

    // 같은 폰트를 일반 텍스트 API에서도 그대로 사용할 수 있다.
    const SkFont text_font { custom.typeface, 24.0f };
    REQUIRE(luil::measure_text(u8"\uEA60", text_font) > 0.0f);
}

TEST_CASE("Glyph drawing clips to its slot and applies the chosen color", "[ui][glyph]")
{
    const auto font { luil::load_codicon_typeface() };
    REQUIRE(font != nullptr);
    constexpr auto custom_color { luil::make_ui_color(40, 100, 230) };
    const SkBitmap bitmap { render({ .glyph = luil::codicons::icon_add, .font_size = 160.0f, .typeface = font, .color = custom_color }) };
    REQUIRE(ink_count(bitmap) > 0);
    bool solid_pixel { false };
    for (int y = 0; y < bitmap.height(); ++y)
    {
        for (int x = 0; x < bitmap.width(); ++x)
        {
            const auto color { bitmap.getColor(x, y) };
            if (x < 24 || y < 24 || x >= 72 || y >= 72)
                REQUIRE(color == SK_ColorTRANSPARENT);
            if (color == custom_color)
                solid_pixel = true;
        }
    }
    REQUIRE(solid_pixel);
}

TEST_CASE("Missing and invalid glyphs are blank and descriptions are accessible", "[ui][glyph]")
{
    const auto font { luil::load_codicon_typeface() };
    REQUIRE(font != nullptr);
    for (const char32_t codepoint : std::array { U'\0', U'한', char32_t { 0xD800 }, char32_t { 0x110000 } })
        REQUIRE(ink_count(render({ .glyph = codepoint, .typeface = font })) == 0);
    for (const float size : std::array { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        REQUIRE(ink_count(render({ .glyph = luil::codicons::icon_add, .font_size = size, .typeface = font })) == 0);
    const luil::glyph_element decorative { {}, { .glyph = luil::codicons::icon_add } };
    REQUIRE(decorative.accessibility().role == luil::access_role::none);
    const luil::glyph_element described { {}, { .glyph = luil::codicons::icon_add, .description = u8"추가" } };
    REQUIRE(described.accessibility().role == luil::access_role::image);
    REQUIRE(described.accessibility().name == u8"추가");
}

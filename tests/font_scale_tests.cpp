#include "luil/ui/dialog_elements.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/label_element.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {
    struct font_scale_reset
    {
        ~font_scale_reset()
        {
            static_cast<void>(luil::set_system_font_sizes({}));
        }
    };
} // namespace

TEST_CASE("System font sizes preserve nonlinear scaling and reject corrupt settings", "[ui][font-scale]")
{
    const font_scale_reset reset {};
    REQUIRE(luil::scaled_font_size(12.0f, 1.5f) == 18.0f);
    REQUIRE(luil::set_system_font_sizes({ 0.0f, 2.0f, 3.0f, 4.0f }));
    REQUIRE(luil::scaled_font_size(1.5f, 2.0f) == 5.0f);
    REQUIRE(luil::text_line_height(2.0f, 7.0f) == 10.0f);
    REQUIRE_FALSE(luil::set_system_font_sizes({ 1.0f, 2.0f }));
    REQUIRE_FALSE(luil::set_system_font_sizes({ 0.0f, 2.0f, 1.0f }));
    REQUIRE_FALSE(luil::set_system_font_sizes({ 0.0f, std::numeric_limits<float>::infinity() }));
    REQUIRE(luil::scaled_font_size(2.0f) == 3.0f);
}

TEST_CASE("An arranged text input keeps matching draw hit and IME measurements during a font setting update", "[ui][font-scale][ime]")
{
    const font_scale_reset reset {};
    luil::text_input_element element { { luil::application_element_kind(0) }, { .text = u8"abc", .caret = 2, .anchor = 2 }, {} };
    element.arrange({ { 0.0f, 0.0f, 200.0f, 48.0f }, 2.0f });
    float measured_size {};
    const luil::text_measurer measure = [&measured_size](const std::u8string_view text, const float size) {
        measured_size = size;
        return static_cast<float>(text.size()) * size;
    };
    static_cast<void>(element.offset_at(30.0f, measure));
    const float original { measured_size };
    REQUIRE(luil::set_system_font_sizes({ 0.0f, 2.0f }));
    static_cast<void>(element.offset_at(30.0f, measure));
    REQUIRE(measured_size == original);
    element.arrange({ { 0.0f, 0.0f, 200.0f, 48.0f }, 2.0f });
    static_cast<void>(element.offset_at(30.0f, measure));
    REQUIRE(measured_size == original * 2.0f);
    REQUIRE(luil::label_element::height_for({ .font_size = 12.0f }) == 31.0f);
}

#include "luil/theme/ui_theme.h"

#include "luil/generated/accents.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace luil {
    namespace {
        using accent_table = std::array<accent_definition, std::size(generated::accents)>;

        accent_table build_catalog() noexcept
        {
            accent_table values {};
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                const generated::accent_entry& entry { generated::accents[index] };
                values[index] = accent_definition {
                    .id = entry.id,
                    .label = entry.label,
                    .swatch = entry.swatch,
                    .dark = { entry.dark[0], entry.dark[1], entry.dark[2], entry.dark[3] },
                    .light = { entry.light[0], entry.light[1], entry.light[2], entry.light[3] },
                };
            }
            return values;
        }

        // 생성 표(luil/generated/accents.h)를 한 번만 접어 놓는다.
        // 문자열과 색은 모두 정적 수명이라 복사가 없다.
        const accent_table& catalog() noexcept
        {
            static const accent_table built { build_catalog() };
            return built;
        }

        // 키 컬러와 무관한 중립 색이다.
        // 팔레트 합성이 여기에 accent 4역할을 얹는다.
        constexpr ui_color_palette dark_neutral_palette {
            .window_background = make_ui_color(30, 30, 30),
            .surface_background = make_ui_color(45, 45, 48),
            .primary_foreground = make_ui_color(255, 255, 255),
            // 전경색의 낮은 알파들이다.
            // element마다 흩어져 있던 관례 값(0.75·0.45·0.12·0.08·0.35)을 역할로 승격했다.
            .secondary_foreground = make_ui_color(255, 255, 255, 191),
            .disabled_foreground = make_ui_color(255, 255, 255, 115),
            .divider = make_ui_color(255, 255, 255, 31),
            .input_background = make_ui_color(255, 255, 255, 20),
            .input_border = make_ui_color(255, 255, 255, 89),
            .warning_accent = make_ui_color(220, 170, 45),
            .error_accent = make_ui_color(224, 108, 117),
            .button_hover_background = make_ui_color(255, 255, 255, 26),
            .button_hover_foreground = make_ui_color(255, 255, 255),
            .button_pressed_background = make_ui_color(255, 255, 255, 45),
            .tooltip_background = make_ui_color(37, 37, 38),
            .tooltip_border = make_ui_color(90, 90, 92),
            .content_shadow = make_ui_color(0, 0, 0),
            .notice_background = make_ui_color(66, 36, 39),
            .caption = {
                .background = make_ui_color(37, 37, 38),
                .foreground = make_ui_color(255, 255, 255),
                .button_hover_background = make_ui_color(63, 63, 64),
                .button_hover_foreground = make_ui_color(255, 255, 255),
                .close_button_hover_background = make_ui_color(196, 43, 28),
                .close_button_hover_foreground = make_ui_color(255, 255, 255),
            },
        };

        // 밝은 바탕이다.
        // 중립 색은 VSCode Light Modern에 맞췄다.
        // 낮은 알파로 primary_foreground를 겹쳐 쓰는 그리기
        // 코드는 전경색이 뒤집히면서 그대로 성립한다.
        constexpr ui_color_palette light_neutral_palette {
            .window_background = make_ui_color(248, 248, 248),
            .surface_background = make_ui_color(255, 255, 255),
            .primary_foreground = make_ui_color(31, 31, 31),
            .secondary_foreground = make_ui_color(31, 31, 31, 191),
            .disabled_foreground = make_ui_color(31, 31, 31, 115),
            .divider = make_ui_color(31, 31, 31, 31),
            .input_background = make_ui_color(31, 31, 31, 20),
            .input_border = make_ui_color(31, 31, 31, 89),
            .warning_accent = make_ui_color(154, 103, 0),
            .error_accent = make_ui_color(192, 48, 58),
            .button_hover_background = make_ui_color(0, 0, 0, 20),
            .button_hover_foreground = make_ui_color(31, 31, 31),
            .button_pressed_background = make_ui_color(0, 0, 0, 36),
            .tooltip_background = make_ui_color(255, 255, 255),
            .tooltip_border = make_ui_color(200, 200, 200),
            .content_shadow = make_ui_color(0, 0, 0),
            .notice_background = make_ui_color(253, 231, 233),
            .caption = {
                .background = make_ui_color(240, 240, 240),
                .foreground = make_ui_color(31, 31, 31),
                .button_hover_background = make_ui_color(218, 218, 218),
                .button_hover_foreground = make_ui_color(31, 31, 31),
                .close_button_hover_background = make_ui_color(196, 43, 28),
                .close_button_hover_foreground = make_ui_color(255, 255, 255),
            },
        };

        // --- 시스템 accent의 색 계산 ---
        // 내장 표(assets/accents.json)는 OKLCH에서 밝기를 역할마다 고정하고
        // 색상(hue)을 보존해 만들어졌다. 실행 시점 합성도 같은 자리에 세워야
        // 정적 항목들 곁에서 다른 규칙으로 만든 색처럼 보이지 않는다.
        // 변환 행렬은 Björn Ottosson의 OKLab 공개 계수다.

        struct oklab_color
        {
            double lightness { 0.0 };
            double a { 0.0 };
            double b { 0.0 };
        };

        struct linear_rgb
        {
            double red { 0.0 };
            double green { 0.0 };
            double blue { 0.0 };
        };

        [[nodiscard]] double linear_channel(const double value) noexcept
        {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        }

        [[nodiscard]] double encoded_channel(const double value) noexcept
        {
            return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        }

        [[nodiscard]] oklab_color oklab_from(const ui_color color) noexcept
        {
            const double red { linear_channel(static_cast<double>((color >> 16U) & 0xFFU) / 255.0) };
            const double green { linear_channel(static_cast<double>((color >> 8U) & 0xFFU) / 255.0) };
            const double blue { linear_channel(static_cast<double>(color & 0xFFU) / 255.0) };
            const double l { std::cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue) };
            const double m { std::cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue) };
            const double s { std::cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue) };
            return oklab_color {
                .lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                .a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                .b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
            };
        }

        // 감마트 판정에 쓰도록 인코딩 전의 선형 값을 그대로 돌려준다.
        [[nodiscard]] linear_rgb linear_rgb_from(const oklab_color lab) noexcept
        {
            const double l_root { lab.lightness + 0.3963377774 * lab.a + 0.2158037573 * lab.b };
            const double m_root { lab.lightness - 0.1055613458 * lab.a - 0.0638541728 * lab.b };
            const double s_root { lab.lightness - 0.0894841775 * lab.a - 1.2914855480 * lab.b };
            const double l { l_root * l_root * l_root };
            const double m { m_root * m_root * m_root };
            const double s { s_root * s_root * s_root };
            return linear_rgb {
                .red = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                .green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                .blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
            };
        }

        [[nodiscard]] bool inside_srgb(const linear_rgb value) noexcept
        {
            constexpr double slack { 0.0005 };
            return value.red >= -slack && value.red <= 1.0 + slack && value.green >= -slack && value.green <= 1.0 + slack && value.blue >= -slack && value.blue <= 1.0 + slack;
        }

        [[nodiscard]] std::uint8_t encoded_byte(const double value) noexcept
        {
            return static_cast<std::uint8_t>(encoded_channel(std::clamp(value, 0.0, 1.0)) * 255.0 + 0.5);
        }

        // 밝기를 고정하고 색상을 보존한 채 채도만 고른 색이다.
        // 목표 채도가 그 밝기의 sRGB 밖이면 들어올 때까지 줄인다 — 내장 표의
        // 값들도 이렇게 잘려 있다 (표를 OKLCH로 읽으면 같은 자리에 모인다).
        [[nodiscard]] ui_color tone_color(const double hue_a, const double hue_b, const double chroma, const double lightness) noexcept
        {
            double low { 0.0 };
            double high { chroma };
            linear_rgb best { linear_rgb_from(oklab_color { lightness, 0.0, 0.0 }) };
            for (int step = 0; step < 24; ++step)
            {
                const double middle { (low + high) * 0.5 };
                const linear_rgb candidate { linear_rgb_from(oklab_color { lightness, middle * hue_a, middle * hue_b }) };
                if (inside_srgb(candidate))
                {
                    low = middle;
                    best = candidate;
                }
                else
                    high = middle;
            }
            return make_ui_color(encoded_byte(best.red), encoded_byte(best.green), encoded_byte(best.blue));
        }

        // 실행 시점 accent의 키다.
        // platform 스레드가 쓰고 logic·UI 스레드가 읽으므로 원자 하나에 담는다.
        // 하위 32비트가 색, 그 위 1비트가 "서 있음"이다.
        constexpr std::uint64_t system_accent_present_bit { std::uint64_t { 1 } << 32U };
        std::atomic<std::uint64_t> system_accent_key { 0 };
    } // namespace

    ui_color_palette high_contrast_palette_for(const high_contrast_colors& colors) noexcept
    {
        // 고대비는 사용자가 고른 시스템 색을 그대로 옮긴다.
        // 알파를 섞거나 밝기를 바꾸면 사용자가 맞춰 둔 대비가 무너진다.
        return ui_color_palette {
            .window_background = colors.window_background,
            .surface_background = colors.window_background,
            .primary_foreground = colors.window_foreground,
            // 고대비는 알파를 섞지 않는다 — 보조·구분선도 온전한 전경색이고
            // 비활성만 시스템의 GRAYTEXT를 쓴다.
            .secondary_foreground = colors.window_foreground,
            .disabled_foreground = colors.disabled_foreground,
            .divider = colors.window_foreground,
            .input_background = colors.window_background,
            .input_border = colors.window_foreground,
            // 키 컬러는 쓰지 않는다.
            // 강조는 hotlight, 선택·hover는 highlight 짝이 맡는다.
            .accent = colors.emphasis,
            .accent_hover = colors.emphasis,
            .accent_soft = colors.highlight_background,
            .accent_emphasis_foreground = colors.highlight_foreground,
            .warning_accent = colors.window_foreground,
            .error_accent = colors.window_foreground,
            .button_hover_background = colors.highlight_background,
            .button_hover_foreground = colors.highlight_foreground,
            .button_pressed_background = colors.highlight_background,
            .tooltip_background = colors.window_background,
            .tooltip_border = colors.window_foreground,
            .content_shadow = colors.window_foreground,
            .notice_background = colors.window_background,
            .caption = {
                .background = colors.button_background,
                .foreground = colors.button_foreground,
                .button_hover_background = colors.highlight_background,
                .button_hover_foreground = colors.highlight_foreground,
                .close_button_hover_background = colors.highlight_background,
                .close_button_hover_foreground = colors.highlight_foreground,
            },
        };
    }

    const accent_color_set& accent_definition::for_theme(const color_theme theme) const noexcept
    {
        switch (theme)
        {
        case color_theme::light:
            return light;
        case color_theme::high_contrast:
        case color_theme::dark:
        default:
            return dark;
        }
    }

    std::span<const accent_definition> accent_catalog() noexcept
    {
        return { catalog().data(), catalog().size() };
    }

    accent_definition accent_for(const std::u8string_view id) noexcept
    {
        // 실행 시점 항목이 먼저다.
        // 서 있지 않으면 아래의 기본색 경로로 물러선다 — 저장된 값은 그대로 둔다.
        if (id == system_accent_id)
        {
            if (std::optional<accent_definition> value { system_accent() }; value.has_value())
                return *value;
        }
        for (const accent_definition& value : catalog())
            if (value.id == id)
                return value;
        // 목록에 없는 id는 기본 색으로 물러선다.
        // 생성 script가 기본 id의 존재를 보장하므로 첫 항목 fallback은 실제로 도달하지 않는다.
        for (const accent_definition& value : catalog())
            if (value.id == default_accent_id)
                return value;
        return catalog().front();
    }

    bool accent_exists(const std::u8string_view id) noexcept
    {
        // 실행 시점 항목은 서 있을 때만 "있다"다.
        // OS를 읽지 못한 채로 저장된 `system`은 지금 물러설 값이라 경고가 맞다.
        if (id == system_accent_id)
            return system_accent().has_value();
        for (const accent_definition& value : catalog())
            if (value.id == id)
                return true;
        return false;
    }

    accent_definition make_system_accent(const ui_color key) noexcept
    {
        // 밝기 사다리는 내장 표를 OKLCH로 읽어 얻은 자리다:
        // dark 0.58(accent) · 0.65(hover) · 0.84(soft) · 0.90(강조 글자),
        // light 0.49(accent) · 0.43(hover) · 0.40(soft = 강조 글자).
        // 채도 상한도 같은 곳에서 왔다 — 표의 선명한 항목들이 그 값에 모여 있고,
        // 그보다 흐린 키는 흐린 대로 남는다 (내장 슬레이트·세이지가 흐리듯).
        const oklab_color lab { oklab_from(key) };
        const double chroma { std::sqrt(lab.a * lab.a + lab.b * lab.b) };
        const bool neutral { chroma < 1e-6 };
        const double hue_a { neutral ? 0.0 : lab.a / chroma };
        const double hue_b { neutral ? 0.0 : lab.b / chroma };
        const auto tone = [&](const double cap, const double lightness) { return tone_color(hue_a, hue_b, std::min(chroma, cap), lightness); };

        accent_definition value {};
        value.id = system_accent_id;
        value.label = u8"시스템";
        value.dark = accent_color_set {
            .accent = tone(0.12, 0.58),
            .hover = tone(0.12, 0.65),
            .soft = tone(0.09, 0.84),
            .emphasis_foreground = tone(0.06, 0.90),
        };
        const ui_color light_soft { tone(0.14, 0.40) };
        value.light = accent_color_set {
            .accent = tone(0.14, 0.49),
            .hover = tone(0.14, 0.43),
            .soft = light_soft,
            .emphasis_foreground = light_soft,
        };
        // 대표색(색 동그라미)도 표와 같은 규칙이다 — 어두운 테마의 soft가 그 자리다.
        value.swatch = value.dark.soft;
        return value;
    }

    void set_system_accent(const std::optional<ui_color> key) noexcept
    {
        system_accent_key.store(key.has_value() ? (system_accent_present_bit | *key) : 0, std::memory_order_relaxed);
    }

    std::optional<accent_definition> system_accent() noexcept
    {
        const std::uint64_t stored { system_accent_key.load(std::memory_order_relaxed) };
        if ((stored & system_accent_present_bit) == 0)
            return std::nullopt;
        return make_system_accent(static_cast<ui_color>(stored & 0xFFFFFFFFU));
    }

    ui_color_palette color_palette_for(const color_theme theme, const accent_definition& accent) noexcept
    {
        if (theme == color_theme::high_contrast)
            return high_contrast_palette_for(high_contrast_colors {});

        const accent_color_set& colors { accent.for_theme(theme) };
        ui_color_palette palette { theme == color_theme::light ? light_neutral_palette : dark_neutral_palette };
        palette.accent = colors.accent;
        palette.accent_hover = colors.hover;
        palette.accent_soft = colors.soft;
        palette.accent_emphasis_foreground = colors.emphasis_foreground;
        return palette;
    }

    ui_color_palette color_palette_for(const color_theme theme) noexcept
    {
        return color_palette_for(theme, accent_for(default_accent_id));
    }

    color_theme resolve_color_theme(const theme_preference preference, const bool high_contrast, const bool system_prefers_light) noexcept
    {
        // 고대비는 접근성 설정이라 어떤 선호보다 세다.
        if (high_contrast)
            return color_theme::high_contrast;
        switch (preference)
        {
        case theme_preference::light:
            return color_theme::light;
        case theme_preference::dark:
            return color_theme::dark;
        case theme_preference::system:
        default:
            return system_prefers_light ? color_theme::light : color_theme::dark;
        }
    }
} // namespace luil

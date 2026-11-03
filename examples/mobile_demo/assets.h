#pragma once

// 견본 그림들이다. 빌드가 examples/demo/assets의 파일을 바이트 배열로 옮겨 싣는다
// (cmake/embed_binary.cmake). 휴대폰에는 실행 파일 옆 자리가 없고, APK assets를 따로 읽는
// 단계를 예제에 두지 않으려는 것이다.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

extern const std::uint8_t mobile_demo_gradient_png[];
extern const std::size_t mobile_demo_gradient_png_size;
extern const std::uint8_t mobile_demo_sweep_webp[];
extern const std::size_t mobile_demo_sweep_webp_size;
extern const std::uint8_t mobile_demo_spinner_gif[];
extern const std::size_t mobile_demo_spinner_gif_size;
// 앱 빌드가 구운 스타일 파일(LUIL_STYLE)의 글이다. 테마 페이지의 스타일 파일 견본이 이 글을 고쳐 읽는다.
extern const std::uint8_t mobile_demo_style_json[];
extern const std::size_t mobile_demo_style_json_size;
// 마크업 견본 글이다 (examples/demo/assets/markup_sample.xml). 테마 페이지의 마크업 묶음이 처음 읽는다.
extern const std::uint8_t mobile_demo_markup_xml[];
extern const std::size_t mobile_demo_markup_xml_size;

namespace mobile_demo {
    [[nodiscard]] inline std::u8string_view style_json() noexcept
    {
        return { reinterpret_cast<const char8_t*>(mobile_demo_style_json), mobile_demo_style_json_size };
    }

    [[nodiscard]] inline std::u8string_view markup_xml() noexcept
    {
        return { reinterpret_cast<const char8_t*>(mobile_demo_markup_xml), mobile_demo_markup_xml_size };
    }

    [[nodiscard]] inline std::span<const std::uint8_t> gradient_png() noexcept
    {
        return { mobile_demo_gradient_png, mobile_demo_gradient_png_size };
    }

    [[nodiscard]] inline std::span<const std::uint8_t> sweep_webp() noexcept
    {
        return { mobile_demo_sweep_webp, mobile_demo_sweep_webp_size };
    }

    [[nodiscard]] inline std::span<const std::uint8_t> spinner_gif() noexcept
    {
        return { mobile_demo_spinner_gif, mobile_demo_spinner_gif_size };
    }
}

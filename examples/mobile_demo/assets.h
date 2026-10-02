#pragma once

// 견본 그림들이다. 빌드가 examples/demo/assets의 파일을 바이트 배열로 옮겨 싣는다
// (cmake/embed_binary.cmake). 휴대폰에는 실행 파일 옆 자리가 없고, APK assets를 따로 읽는
// 단계를 예제에 두지 않으려는 것이다.

#include <cstddef>
#include <cstdint>
#include <span>

extern const std::uint8_t mobile_demo_gradient_png[];
extern const std::size_t mobile_demo_gradient_png_size;
extern const std::uint8_t mobile_demo_sweep_webp[];
extern const std::size_t mobile_demo_sweep_webp_size;
extern const std::uint8_t mobile_demo_spinner_gif[];
extern const std::size_t mobile_demo_spinner_gif_size;

namespace mobile_demo {
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
} // namespace mobile_demo

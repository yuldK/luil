#pragma once

#include <optional>
#include <string_view>

namespace luil {
    enum class renderer_mode
    {
        // GPU를 먼저 쓰고, 만들거나 그리다 실패하면 CPU로 물러선다.
        automatic,
        // Windows의 GPU 경로다. 실패를 호출자에게 돌려준다. Windows에서는 `gpu`와 같다.
        direct3d,
        // CPU 렌더러만 쓴다.
        cpu,
        // 이 플랫폼의 GPU 경로다 (Windows는 Direct3D). 실패를 호출자에게 돌려준다.
        // 플랫폼마다 GPU API가 달라도 앱이 같은 값을 쓰도록 둔다
        // (docs/android-port-plan.md 결정 6). 기존 값의 수를 바꾸지 않으려고 끝에 둔다.
        gpu,
    };

    enum class renderer_backend
    {
        direct3d,
        cpu,
    };

    enum class renderer_selection_status
    {
        selected,
        unavailable,
    };

    struct renderer_selection
    {
        renderer_selection_status status { renderer_selection_status::unavailable };
        renderer_backend backend { renderer_backend::cpu };
        bool used_fallback { false };
    };

    [[nodiscard]] std::optional<renderer_mode> parse_renderer_mode(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view renderer_mode_name(renderer_mode mode) noexcept;
    [[nodiscard]] std::u8string_view renderer_backend_name(renderer_backend backend) noexcept;
    // `gpu`와 `direct3d`는 같은 규칙이다 — GPU를 쓸 수 없으면 선택이 실패한다.
    [[nodiscard]] renderer_selection select_renderer_backend(renderer_mode mode, bool direct3d_available) noexcept;
} // namespace luil

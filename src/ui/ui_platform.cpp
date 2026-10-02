#include "luil/ui/ui_platform.h"

#include <atomic>

namespace luil {
    namespace {
        // 시작할 때 한 번 쓰고 여러 thread가 읽는다. 값 둘이 따로 바뀌는 일은 없으므로
        // 각각 원자로 둔다.
        std::atomic<ui_form_factor> form_factor { ui_form_factor::desktop };
        std::atomic<bool> window_caption { true };
    } // namespace

    void set_ui_platform(const ui_platform& platform) noexcept
    {
        form_factor.store(platform.form_factor);
        window_caption.store(platform.window_caption);
    }

    ui_platform current_ui_platform() noexcept
    {
        return ui_platform {
            .form_factor = form_factor.load(),
            .window_caption = window_caption.load(),
        };
    }
} // namespace luil

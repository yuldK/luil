#include "macos/dock_plan.h"

#include "host/taskbar_sync.h"

#include <algorithm>
#include <string>

namespace luil::macos {
    namespace {
        // 128점 타일에서 눈으로 맞춘 몫이다. 막대는 아이콘 아래쪽 가장자리를 따라 눕고, 오버레이는 타일 변의 0.42다.
        constexpr float track_inset { 0.12f };
        constexpr float track_bottom { 0.07f };
        constexpr float track_thickness { 0.09f };
        constexpr float overlay_side { 0.42f };
        constexpr float overlay_gap { 0.02f };

        [[nodiscard]] bool has_nul(const std::u8string& text) noexcept
        {
            return text.find(u8'\0') != std::u8string::npos;
        }
    }

    dock_bar make_dock_bar(const taskbar_progress_state state, const std::uint32_t units) noexcept
    {
        const float fraction { static_cast<float>(std::min(units, taskbar_progress_total)) / static_cast<float>(taskbar_progress_total) };
        switch (state)
        {
        case taskbar_progress_state::indeterminate:
            return dock_bar { .visible = true, .striped = true, .tint = dock_bar_tint::accent, .fraction = 1.0f };
        case taskbar_progress_state::normal:
            return dock_bar { .visible = true, .tint = dock_bar_tint::accent, .fraction = fraction };
        case taskbar_progress_state::error:
            return dock_bar { .visible = true, .tint = dock_bar_tint::error, .fraction = fraction };
        case taskbar_progress_state::paused:
            return dock_bar { .visible = true, .tint = dock_bar_tint::paused, .fraction = fraction };
        case taskbar_progress_state::none:
        default:
            return {};
        }
    }

    dock_tile_layout layout_dock_tile(const float width, const float height, const bool bar) noexcept
    {
        dock_tile_layout layout {};
        if (width <= 0.0f || height <= 0.0f)
            return layout;
        if (bar)
            layout.track = dock_rect { .x = width * track_inset, .y = height * track_bottom, .width = width * (1.0f - track_inset * 2.0f), .height = height * track_thickness };
        const float side { std::min(width, height) * overlay_side };
        const float bottom { bar ? layout.track.y + layout.track.height + height * overlay_gap : 0.0f };
        layout.overlay = dock_rect { .x = width - side, .y = bottom, .width = side, .height = side };
        return layout;
    }

    dock_rect fit_dock_rect(const dock_rect& box, const float width, const float height) noexcept
    {
        if (width <= 0.0f || height <= 0.0f || box.width <= 0.0f || box.height <= 0.0f)
            return {};
        const float scale { std::min(box.width / width, box.height / height) };
        const float fitted_width { width * scale };
        const float fitted_height { height * scale };
        return dock_rect {
            .x = box.x + (box.width - fitted_width) / 2.0f
            , .y = box.y + (box.height - fitted_height) / 2.0f
            , .width = fitted_width
            , .height = fitted_height
        };
    }

    std::u8string dock_task_error(const std::size_t index, const std::u8string_view what)
    {
        const std::string number { std::to_string(index + 1) };
        return u8"Jump list task " + std::u8string { number.begin(), number.end() } + u8" " + std::u8string { what };
    }

    std::optional<std::u8string> dock_menu_problem(const taskbar_jump_list& list)
    {
        for (std::size_t index = 0; index < list.tasks.size(); ++index)
        {
            const taskbar_jump_task& task { list.tasks[index] };
            if (task.title.empty())
                return dock_task_error(index, u8"has an empty title.");
            if (has_nul(task.title) || has_nul(task.description))
                return dock_task_error(index, u8"has text that contains NUL.");
            if (std::ranges::any_of(task.arguments, has_nul))
                return dock_task_error(index, u8"has an argument that contains NUL.");
        }
        return std::nullopt;
    }
}

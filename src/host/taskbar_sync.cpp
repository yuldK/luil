#include "host/taskbar_sync.h"

#include <cmath>
#include <utility>

namespace luil {
    namespace {
        // 값을 읽는 상태인가. 불확정과 없음은 값을 보지 않는다.
        [[nodiscard]] bool has_value(const taskbar_progress_state state) noexcept
        {
            return state == taskbar_progress_state::normal
                || state == taskbar_progress_state::error
                || state == taskbar_progress_state::paused
            ;
        }

        [[nodiscard]] bool is_default(const taskbar_request& request) noexcept
        {
            return request.progress.state == taskbar_progress_state::none
                && request.overlay.icon.valid() == false
            ;
        }
    }

    std::uint32_t taskbar_progress_units(const float value) noexcept
    {
        if (std::isnan(value))
            return 0;
        if (value <= 0.0f)
            return 0;
        if (value >= 1.0f)
            return taskbar_progress_total;
        return static_cast<std::uint32_t>(std::lround(value * static_cast<float>(taskbar_progress_total)));
    }

    void taskbar_sync::button_created() noexcept
    {
        ready_ = true;
        applied_state_ = taskbar_progress_state::none;
        applied_units_ = 0;
        applied_icon_ = {};
        applied_description_.clear();
    }

    void taskbar_sync::button_lost() noexcept
    {
        ready_ = false;
        applied_state_ = taskbar_progress_state::none;
        applied_units_ = 0;
        applied_icon_ = {};
        applied_description_.clear();
    }

    void taskbar_sync::forget_overlay() noexcept
    {
        applied_icon_ = {};
        applied_description_.clear();
    }

    std::vector<taskbar_call> taskbar_sync::update(const taskbar_request& wanted)
    {
        std::vector<taskbar_call> calls {};
        if (ready_ == false)
        {
            state_ = is_default(wanted) ? taskbar_sync_state::off : taskbar_sync_state::waiting;
            return calls;
        }

        // 값을 보내면 셸이 불확정·없음 상태를 정상으로 바꾼다고 알려져 있어, 값 뒤에 같은 상태를 이어서 다시 보낸다.
        //  - 값이 없는 상태에서 값이 있는 상태로 들어올 때도 값부터 보낸다.
        const taskbar_progress_state state { wanted.progress.state };
        const std::uint32_t units { has_value(state) ? taskbar_progress_units(wanted.progress.value) : 0 };
        if (state != applied_state_ || units != applied_units_)
        {
            if (has_value(state))
                calls.push_back(taskbar_call { .kind = taskbar_call_kind::progress_value, .units = units });
            calls.push_back(taskbar_call { .kind = taskbar_call_kind::progress_state, .state = state });
            applied_state_ = state;
            applied_units_ = units;
        }

        const ui_image& icon { wanted.overlay.icon };
        if (icon.valid() == false)
        {
            if (applied_icon_.valid())
            {
                calls.push_back(taskbar_call { .kind = taskbar_call_kind::overlay_clear });
                applied_icon_ = {};
                applied_description_.clear();
            }
        }
        else if (applied_icon_.valid() == false || applied_icon_.backing() != icon.backing())
        {
            calls.push_back(taskbar_call { .kind = taskbar_call_kind::overlay_set, .icon = icon, .description = wanted.overlay.description });
            applied_icon_ = icon;
            applied_description_ = wanted.overlay.description;
        }
        else if (applied_description_ != wanted.overlay.description)
        {
            calls.push_back(taskbar_call { .kind = taskbar_call_kind::overlay_description, .description = wanted.overlay.description });
            applied_description_ = wanted.overlay.description;
        }

        state_ = is_default(wanted) ? taskbar_sync_state::off : taskbar_sync_state::applied;
        return calls;
    }
}

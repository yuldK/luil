#include "android/device_request.h"

namespace luil::android {
    std::optional<int> activity_orientation(const screen_orientation request, const int rotation, const bool landscape) noexcept
    {
        switch (request)
        {
        case screen_orientation::system:
            return std::nullopt;
        case screen_orientation::portrait:
            return activity_orientation_user_portrait;
        case screen_orientation::landscape:
            return activity_orientation_user_landscape;
        case screen_orientation::current:
            break;
        }
        if (rotation < 0 || rotation > 3)
            return activity_orientation_locked;
        // 가로이면 0°·90°가 기본 가로, 180°·270°가 반대 가로다. 세로이면 0°·270°가 기본 세로,
        // 90°·180°가 반대 세로다. 자연 방향이 세로인 휴대폰의 90°는 가로가 되고, 자연 방향이 가로인
        // 태블릿의 90°는 세로가 되는데, 두 경우가 이 표에서 함께 맞는다.
        if (landscape)
            return rotation <= 1 ? activity_orientation_landscape : activity_orientation_reverse_landscape;
        return rotation == 0 || rotation == 3 ? activity_orientation_portrait : activity_orientation_reverse_portrait;
    }

    int haptic_feedback_constant(const haptic_feedback feedback, const int api_level) noexcept
    {
        switch (feedback)
        {
        case haptic_feedback::long_press:
            return haptic_constant_long_press;
        case haptic_feedback::drag_start:
            if (api_level >= 34)
                return haptic_constant_drag_start;
            return api_level >= 30 ? haptic_constant_gesture_start : haptic_constant_long_press;
        case haptic_feedback::tick:
            return api_level >= 34 ? haptic_constant_segment_tick : haptic_constant_clock_tick;
        case haptic_feedback::confirm:
            return api_level >= 30 ? haptic_constant_confirm : haptic_constant_virtual_key;
        case haptic_feedback::reject:
            return api_level >= 30 ? haptic_constant_reject : haptic_constant_long_press;
        }
        return haptic_constant_long_press;
    }
    const char* android_permission_name(const app_permission permission, const int api_level, const int target_sdk) noexcept
    {
        switch (permission)
        {
        case app_permission::local_network:
            if (api_level >= api_level_local_network_permission && target_sdk >= api_level_local_network_permission)
                return "android.permission.ACCESS_LOCAL_NETWORK";
            return nullptr;
        }
        return nullptr;
    }
} // namespace luil::android

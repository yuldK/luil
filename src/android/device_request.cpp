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
} // namespace luil::android

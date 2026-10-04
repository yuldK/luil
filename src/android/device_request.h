#pragma once

#include "luil/app/app_host.h"

#include <optional>

// 앱의 기기 요청(`device_request`)을 Android의 값으로 옮기는 순수 층이다.
// JNI도 NDK 헤더도 쓰지 않아 Windows의 core test에서도 돈다. 값은 Java 상수라 NDK 헤더에 없다.
namespace luil::android {
    // `ActivityInfo.SCREEN_ORIENTATION_*`다.
    inline constexpr int activity_orientation_landscape { 0 };
    inline constexpr int activity_orientation_portrait { 1 };
    inline constexpr int activity_orientation_reverse_landscape { 8 };
    inline constexpr int activity_orientation_reverse_portrait { 9 };
    inline constexpr int activity_orientation_user_landscape { 11 };
    inline constexpr int activity_orientation_user_portrait { 12 };
    inline constexpr int activity_orientation_locked { 14 };

    // 방향 요청을 `Activity.setRequestedOrientation`의 값으로 옮긴다.
    //  - `system`은 nullopt다. 앱이 처음 가졌던 값(매니페스트의 `screenOrientation`)으로 되돌린다는 뜻이다.
    //    미지정(-1)으로 덮으면 매니페스트가 정한 방향이 사라진다.
    //  - `portrait`·`landscape`는 사용자 회전 설정을 따르는 값이다 (`USER_PORTRAIT`·`USER_LANDSCAPE`).
    //    자동 회전이 켜져 있으면 반대쪽으로도 돌고, 꺼져 있으면 기본 쪽에 선다.
    //  - `current`는 지금 회전(`Surface.ROTATION_*`, 0~3)과 지금 가로인지로 고정 값을 고른다. 같은 회전이
    //    기기의 자연 방향(휴대폰은 세로, 태블릿은 흔히 가로)에 따라 다른 방향이라 회전만으로는 고를 수 없다.
    //    회전을 읽지 못했으면(범위 밖) 시스템의 "지금 회전에 고정"(`LOCKED`)으로 물러선다.
    [[nodiscard]] std::optional<int> activity_orientation(screen_orientation request, int rotation, bool landscape) noexcept;
} // namespace luil::android

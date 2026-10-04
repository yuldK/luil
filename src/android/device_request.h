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

    // 로컬 네트워크 권한이 생긴 판이다 (Android 17). 이 판 아래와 targetSdk가 이보다 낮은 앱은 묻지 않는다.
    inline constexpr int api_level_local_network_permission { 37 };

    // 권한을 Android의 이름으로 옮긴다. 묻지 않아도 되면(`not_required`) nullptr다.
    //  - `local_network`는 기기와 앱의 targetSdk가 둘 다 37 이상일 때 `ACCESS_LOCAL_NETWORK`다. 그 아래에서는
    //    `INTERNET`이 로컬 주소까지 열어 준다. Android 16의 시험용 강제(`RESTRICT_LOCAL_NETWORK`)는 다른 권한
    //    (`NEARBY_WIFI_DEVICES`)을 보므로 따르지 않는다 — 실제 기기의 동작이 아니다.
    [[nodiscard]] const char* android_permission_name(app_permission permission, int api_level, int target_sdk) noexcept;

    // `HapticFeedbackConstants.*`다.
    inline constexpr int haptic_constant_long_press { 0 };
    inline constexpr int haptic_constant_virtual_key { 1 };
    inline constexpr int haptic_constant_clock_tick { 4 };
    inline constexpr int haptic_constant_gesture_start { 12 };
    inline constexpr int haptic_constant_confirm { 16 };
    inline constexpr int haptic_constant_reject { 17 };
    inline constexpr int haptic_constant_drag_start { 25 };
    inline constexpr int haptic_constant_segment_tick { 26 };

    // 햅틱의 뜻을 `View.performHapticFeedback`의 상수로 옮긴다. 상수는 판마다 늘었으므로 기기의 API 수준을
    // 받아, 그 판에 없는 것은 가장 가까운 옛 상수로 물러선다.
    //  - 끌기 시작: 34부터 `DRAG_START`, 30부터 `GESTURE_START`, 그 아래 `LONG_PRESS`
    //  - 눈금: 34부터 `SEGMENT_TICK`, 그 아래 `CLOCK_TICK`
    //  - 확인·거절: 30부터 `CONFIRM`·`REJECT`, 그 아래 `VIRTUAL_KEY`·`LONG_PRESS`
    [[nodiscard]] int haptic_feedback_constant(haptic_feedback feedback, int api_level) noexcept;
} // namespace luil::android

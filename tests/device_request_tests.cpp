#include "android/device_request.h"
#include "luil/app/app_host.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {
    // 시험이 바꾼 플랫폼 성질을 데스크톱 기본값으로 되돌린다. 성질은 프로세스 전역이다.
    struct platform_scope
    {
        explicit platform_scope(const luil::ui_platform& platform)
        {
            luil::set_ui_platform(platform);
        }

        platform_scope(const platform_scope&) = delete;
        platform_scope(platform_scope&&) = delete;
        platform_scope& operator=(const platform_scope&) = delete;
        platform_scope& operator=(platform_scope&&) = delete;

        ~platform_scope()
        {
            luil::set_ui_platform({});
        }
    };

    // `Surface.ROTATION_*`다.
    constexpr int rotation_0 { 0 };
    constexpr int rotation_90 { 1 };
    constexpr int rotation_180 { 2 };
    constexpr int rotation_270 { 3 };
}

TEST_CASE("A platform does no device requests unless its host says it can", "[ui][platform][device]")
{
    REQUIRE(luil::current_ui_platform().capabilities == luil::ui_platform_capabilities {});
    REQUIRE(luil::current_ui_platform().capabilities.screen_orientation == false);
    REQUIRE(luil::current_ui_platform().capabilities.system_bars == false);
    REQUIRE(luil::current_ui_platform().capabilities.volume_keys == false);
    REQUIRE(luil::current_ui_platform().capabilities.haptics == false);

    const luil::ui_platform mobile {
        .form_factor = luil::ui_form_factor::mobile,
        .window_caption = false,
        .capabilities = { .screen_orientation = true, .system_bars = true },
    };
    {
        const platform_scope scope { mobile };
        REQUIRE(luil::current_ui_platform() == mobile);
        REQUIRE(luil::current_ui_platform().capabilities.screen_orientation);
    }
    REQUIRE(luil::current_ui_platform().capabilities.screen_orientation == false);
}

TEST_CASE("A frame leaves the device to the platform by default", "[app][device]")
{
    const luil::ui_frame frame {};
    REQUIRE(frame.device == luil::device_request {});
    REQUIRE(frame.device.orientation == luil::screen_orientation::system);
    REQUIRE(frame.device.system_bars == luil::system_bars_mode::visible);
    REQUIRE(frame.device.draw_under_cutout == false);
    REQUIRE(frame.device.volume_keys == false);
}

TEST_CASE("Following the system restores the activity's own orientation", "[android][device][orientation]")
{
    REQUIRE(luil::android::activity_orientation(luil::screen_orientation::system, rotation_90, true).has_value() == false);
}

TEST_CASE("Portrait and landscape follow the user's rotation setting", "[android][device][orientation]")
{
    using luil::android::activity_orientation;
    REQUIRE(activity_orientation(luil::screen_orientation::portrait, rotation_90, true) == luil::android::activity_orientation_user_portrait);
    REQUIRE(activity_orientation(luil::screen_orientation::landscape, rotation_0, false) == luil::android::activity_orientation_user_landscape);
}

TEST_CASE("Locking to the current orientation fixes the way a phone is held", "[android][device][orientation]")
{
    // 자연 방향이 세로인 휴대폰이다. 90°·270°가 가로다.
    using luil::android::activity_orientation;
    constexpr auto current { luil::screen_orientation::current };
    REQUIRE(activity_orientation(current, rotation_0, false) == luil::android::activity_orientation_portrait);
    REQUIRE(activity_orientation(current, rotation_90, true) == luil::android::activity_orientation_landscape);
    REQUIRE(activity_orientation(current, rotation_180, false) == luil::android::activity_orientation_reverse_portrait);
    REQUIRE(activity_orientation(current, rotation_270, true) == luil::android::activity_orientation_reverse_landscape);
}

TEST_CASE("Locking to the current orientation fixes the way a landscape tablet is held", "[android][device][orientation]")
{
    // 자연 방향이 가로인 태블릿이다. 같은 회전이 휴대폰과 다른 방향이라 회전만으로는 고를 수 없다.
    using luil::android::activity_orientation;
    constexpr auto current { luil::screen_orientation::current };
    REQUIRE(activity_orientation(current, rotation_0, true) == luil::android::activity_orientation_landscape);
    REQUIRE(activity_orientation(current, rotation_90, false) == luil::android::activity_orientation_reverse_portrait);
    REQUIRE(activity_orientation(current, rotation_180, true) == luil::android::activity_orientation_reverse_landscape);
    REQUIRE(activity_orientation(current, rotation_270, false) == luil::android::activity_orientation_portrait);
}

TEST_CASE("Haptic meanings use the newest constant the device knows", "[android][device][haptic]")
{
    using luil::haptic_feedback;
    using luil::android::haptic_feedback_constant;
    // 길게 누르기는 처음부터 있던 상수다.
    REQUIRE(haptic_feedback_constant(haptic_feedback::long_press, 26) == luil::android::haptic_constant_long_press);
    REQUIRE(haptic_feedback_constant(haptic_feedback::long_press, 36) == luil::android::haptic_constant_long_press);
    // 끌기 시작은 34의 DRAG_START, 30의 GESTURE_START, 그 아래 LONG_PRESS다.
    REQUIRE(haptic_feedback_constant(haptic_feedback::drag_start, 36) == luil::android::haptic_constant_drag_start);
    REQUIRE(haptic_feedback_constant(haptic_feedback::drag_start, 33) == luil::android::haptic_constant_gesture_start);
    REQUIRE(haptic_feedback_constant(haptic_feedback::drag_start, 29) == luil::android::haptic_constant_long_press);
    // 눈금은 34의 SEGMENT_TICK, 그 아래 CLOCK_TICK이다.
    REQUIRE(haptic_feedback_constant(haptic_feedback::tick, 34) == luil::android::haptic_constant_segment_tick);
    REQUIRE(haptic_feedback_constant(haptic_feedback::tick, 33) == luil::android::haptic_constant_clock_tick);
    // 확인·거절은 30부터다.
    REQUIRE(haptic_feedback_constant(haptic_feedback::confirm, 30) == luil::android::haptic_constant_confirm);
    REQUIRE(haptic_feedback_constant(haptic_feedback::confirm, 29) == luil::android::haptic_constant_virtual_key);
    REQUIRE(haptic_feedback_constant(haptic_feedback::reject, 30) == luil::android::haptic_constant_reject);
    REQUIRE(haptic_feedback_constant(haptic_feedback::reject, 29) == luil::android::haptic_constant_long_press);
}

TEST_CASE("An unknown rotation falls back to the system's lock on the current rotation", "[android][device][orientation]")
{
    using luil::android::activity_orientation;
    REQUIRE(activity_orientation(luil::screen_orientation::current, -1, false) == luil::android::activity_orientation_locked);
    REQUIRE(activity_orientation(luil::screen_orientation::current, 4, true) == luil::android::activity_orientation_locked);
}

TEST_CASE("The local network permission is asked only on Android 17 for apps targeting 37", "[android][device][permission]")
{
    // 이름이 없으면(nullptr) 빈 글로 본다. 실패한 단언이 비교 중에 넘어지지 않게 한다.
    const auto android_permission_name = [](const luil::app_permission permission, const int api_level, const int target_sdk) {
        const char* const text { luil::android::android_permission_name(permission, api_level, target_sdk) };
        return text != nullptr ? std::string_view { text } : std::string_view {};
    };
    const std::string_view name { "android.permission.ACCESS_LOCAL_NETWORK" };
    // 기기와 앱이 둘 다 37 이상일 때만 묻는다.
    REQUIRE(android_permission_name(luil::app_permission::local_network, 37, 37) == name);
    REQUIRE(android_permission_name(luil::app_permission::local_network, 38, 40) == name);
    // 그 아래는 INTERNET이 로컬 주소까지 연다. Android 16 기기는 targetSdk가 37이어도 묻지 않는다.
    REQUIRE(android_permission_name(luil::app_permission::local_network, 36, 37) == std::string_view {});
    REQUIRE(android_permission_name(luil::app_permission::local_network, 37, 36) == std::string_view {});
    REQUIRE(android_permission_name(luil::app_permission::local_network, 26, 26) == std::string_view {});
    // targetSdk를 읽지 못했으면(0) 묻지 않는다.
    REQUIRE(android_permission_name(luil::app_permission::local_network, 37, 0) == std::string_view {});
}

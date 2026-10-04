#include "android/device_request.h"
#include "luil/app/app_host.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_test_macros.hpp>

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
} // namespace

TEST_CASE("A platform does no device requests unless its host says it can", "[ui][platform][device]")
{
    REQUIRE(luil::current_ui_platform().capabilities == luil::ui_platform_capabilities {});
    REQUIRE(luil::current_ui_platform().capabilities.screen_orientation == false);
    REQUIRE(luil::current_ui_platform().capabilities.system_bars == false);
    REQUIRE(luil::current_ui_platform().capabilities.volume_keys == false);

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

TEST_CASE("An unknown rotation falls back to the system's lock on the current rotation", "[android][device][orientation]")
{
    using luil::android::activity_orientation;
    REQUIRE(activity_orientation(luil::screen_orientation::current, -1, false) == luil::android::activity_orientation_locked);
    REQUIRE(activity_orientation(luil::screen_orientation::current, 4, true) == luil::android::activity_orientation_locked);
}

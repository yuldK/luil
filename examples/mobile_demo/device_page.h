#pragma once

// 기기 페이지다: 앱이 기기에 바라는 것(`ui_frame::device`)을 고른다.
// 고른 값은 앱 상태라 다른 페이지로 가도, 홈에 갔다 와도, 화면을 돌려도 그대로 frame에 실린다.
// 플랫폼이 하지 못하는 항목은 그 사실을 적고 고를 자리를 두지 않는다 (`ui_platform::capabilities`).

#include "luil/app/app_host.h"
#include "luil/app/platform_services.h"
#include "mobile_demo/common.h"

#include <cstdint>
#include <optional>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    struct orientation_intent
    {
        luil::screen_orientation orientation { luil::screen_orientation::system };
    };

    struct system_bars_intent
    {
        luil::system_bars_mode mode { luil::system_bars_mode::visible };
    };

    struct cutout_intent
    {
        bool draw_under { false };
    };

    struct volume_keys_intent
    {
        bool claim { false };
    };

    // 앱이 받은 볼륨 키다 (policy가 키를 이 메시지로 옮긴다).
    struct volume_key_intent
    {
        bool up { true };
        bool repeat { false };
    };

    // 로컬 네트워크 권한의 상태다. delegate가 시작할 때(`answered`가 거짓)와 사용자가 답했을 때 알린다.
    struct local_network_intent
    {
        luil::permission_state state { luil::permission_state::not_required };
        bool answered { false };
    };

    // 알림 권한의 상태다. `local_network_intent`와 같은 규칙이다.
    struct notification_permission_intent
    {
        luil::permission_state state { luil::permission_state::not_required };
        bool answered { false };
    };

    // 알림 보내기의 답이다 (`show_notification`).
    struct notification_send_intent
    {
        luil::platform_result result {};
    };

    // 알림을 눌러 이 Activity가 떴다 (`notification_event::launched`). 살아 있는 앱의 눌림은 Android에서 오지 않는다.
    struct notification_launch_intent
    {
        std::u8string id {};
    };

    // 비밀 저장소를 한 바퀴 돌려 본다 (Keystore AES-GCM).
    struct secret_check_intent
    {
    };

    // 로컬 네트워크 권한을 묻는 앱 UI 명령이다. delegate가 받아 `app_host::request_permission`을 부른다.
    inline constexpr std::uint32_t command_request_local_network { 0x6c6e };
    // 알림 권한을 묻고 알림 하나를 올리는 앱 UI 명령이다. delegate가 받는다.
    inline constexpr std::uint32_t command_request_notifications { 0x6e70 };
    inline constexpr std::uint32_t command_send_notification { 0x6e73 };
    // 기기 페이지가 올리는 알림의 id다.
    inline constexpr std::u8string_view device_notification_id { u8"mobile-demo-1" };

    class device_page
    {
    public:
        bool handle(const luil::app_message& message);
        // 창 metrics는 창 폭 등급과 움직임 줄이기를 보이려고 받는다.
        [[nodiscard]] page_content build(float width, float scale, const window_metrics_intent& window);

        // 셸이 frame에 싣는 요청이다.
        [[nodiscard]] const luil::device_request& request() const noexcept
        {
            return request_;
        }

    private:
        luil::device_request request_ {};
        // 받은 볼륨 키의 수다. 누르고 있어 이어 온 반복도 센다.
        int volume_up_count_ { 0 };
        int volume_down_count_ { 0 };
        int volume_repeat_count_ { 0 };
        // 로컬 네트워크 권한의 마지막 상태와, 사용자가 답한 적이 있는가다.
        luil::permission_state local_network_ { luil::permission_state::not_required };
        bool local_network_answered_ { false };
        // 알림 권한의 마지막 상태와 보내기의 마지막 답, 알림으로 뜬 id다.
        luil::permission_state notifications_ { luil::permission_state::not_required };
        bool notifications_answered_ { false };
        std::optional<luil::platform_result> notification_sent_ {};
        std::u8string notification_launch_ {};
        // 비밀 저장소를 돌린 결과 한 줄이다. 아직 돌리지 않았으면 비었다.
        std::u8string secret_report_ {};
    };
}

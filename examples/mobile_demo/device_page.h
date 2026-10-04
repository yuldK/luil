#pragma once

// 기기 페이지다: 앱이 기기에 바라는 것(`ui_frame::device`)을 고른다.
// 고른 값은 앱 상태라 다른 페이지로 가도, 홈에 갔다 와도, 화면을 돌려도 그대로 frame에 실린다.
// 플랫폼이 하지 못하는 항목은 그 사실을 적고 고를 자리를 두지 않는다 (`ui_platform::capabilities`).

#include "luil/app/app_host.h"
#include "mobile_demo/common.h"

#include <cstdint>

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

    // 로컬 네트워크 권한을 묻는 앱 UI 명령이다. delegate가 받아 `app_host::request_permission`을 부른다.
    inline constexpr std::uint32_t command_request_local_network { 0x6c6e };

    class device_page
    {
    public:
        bool handle(const luil::app_message& message);
        // 창 metrics는 창 폭 등급을 보이려고 받는다.
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
    };
} // namespace mobile_demo

#pragma once

// 토스트 페이지다: 심각도별 토스트와 액션 버튼이 있는 계속 남는 토스트, 그리고 닫기 전까지 남는 정보 막대.
// 토스트 목록과 만료 판정은 여기(앱)가 소유하고, 다른 페이지도
// toast_request_intent로 토스트를 띄울 수 있다.
// 정보 막대가 열려 있는지는 앱 상태다. 막대는 닫기를 누르면 스스로 사라지지 않고, 이 페이지가 다음 frame에 막대를 짓지 않는다.
// OS 알림 구역은 앱 창 밖 Windows 알림 센터에 올리고 바꾸고 거두는 예다 (os-notification-design.md).
//  - 페이지는 요청(`os_notification_request_intent`)만 던진다. 셸(demo_main)이 `show_notification`·`close_notification`을 부르고
//    결과·상태를 이 페이지에 돌려준다. 사용자가 누른 알림은 라이브러리가 `make_notification_message`로 알리고 셸이 사건으로 넘긴다.
//  - 상태·결과·사건 세 줄이 label이라 UIA 이름으로 되읽힌다.

#include "demo/common.h"

#include "luil/app/notification.h"
#include "luil/ui/severity.h"

#include <array>
#include <chrono>
#include <optional>
#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    // 계속 남는 토스트의 실행 취소·닫기다.
    struct toast_undo_intent
    {
        std::u8string id {};
    };

    // 페이지 본문의 휠 스크롤이다.
    struct toasts_scroll_intent
    {
        float delta { 0.0f };
    };

    // 정보 막대의 동작 단추나 닫기를 눌렀다. 어느 막대인지는 심각도로 가른다.
    struct info_bar_intent
    {
        luil::ui_severity severity { luil::ui_severity::info };
        // 거짓이면 동작 단추이고 참이면 닫기다.
        bool close { false };
    };

    // 닫은 정보 막대를 모두 다시 연다.
    struct info_bar_reopen_intent
    {
    };

    // OS 알림 구역이 올리는 알림의 id다. 바꾸기와 거두기가 같은 알림을 가리킨다.
    inline constexpr std::u8string_view os_notification_id { u8"demo-1" };

    enum class os_notification_action
    {
        send,
        replace,
        close,
    };

    // OS 알림 단추를 눌렀다. 셸이 받아 OS를 부른다 (페이지는 플랫폼을 부르지 않는다).
    struct os_notification_request_intent
    {
        os_notification_action action { os_notification_action::send };
    };

    // 셸이 읽은 지금 상태다 (`notification_status`). 시작할 때와 요청마다 온다.
    struct os_notification_status_intent
    {
        luil::notification_availability availability { luil::notification_availability::unsupported };
    };

    // 마지막 요청의 답이다.
    struct os_notification_result_intent
    {
        luil::platform_result result {};
    };

    // 라이브러리가 알린 알림 사건이다 (`app_delegate::make_notification_message`).
    struct os_notification_event_intent
    {
        luil::notification_event event {};
    };

    class toasts_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 화면 오른쪽 아래에 쌓이는 토스트 오버레이다.
        // 어느 페이지가 보이든 창 전체 위에 얹는다. 토스트가 없으면 nullptr다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_overlay();

        // 다음 토스트가 만료되는 시각이다 (만료될 것이 없으면 nullopt).
        // driver가 next_tick으로 이 값을 예고하면 메시지가 없어도
        // 그 시각에 tick → prune → frame 재게시가 돈다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_expiry() const;

        // 끝난 토스트를 목록에서 지운다 (만료 판정은 앱 몫).
        // driver의 tick이 부른다.
        void prune();

    private:
        struct toast_entry
        {
            std::u8string id {};
            std::u8string text {};
            luil::toast_severity severity { luil::toast_severity::info };
            std::chrono::steady_clock::time_point shown_at {};
            std::chrono::milliseconds duration { 0 };
        };

        void push(std::u8string text, luil::toast_severity severity, std::chrono::milliseconds duration);
        // OS 알림 구역을 페이지에 더한다.
        void add_os_notification_card(page_layout& page) const;

        std::vector<toast_entry> toasts_ {};
        int next_id_ { 0 };
        float scroll_ { 0.0f };
        // 정보 막대가 열려 있는가. 인덱스는 심각도 순서(정보·성공·경고·오류)다.
        std::array<bool, 4> bars_open_ { true, true, true, true };
        // 마지막으로 누른 막대의 동작이다. 비어 있으면 아직 없다.
        std::u8string last_bar_action_ {};
        // OS 알림 구역이 보이는 셋이다. 받은 적이 없으면 비었다.
        std::optional<luil::notification_availability> os_status_ {};
        std::optional<luil::platform_result> os_result_ {};
        std::optional<luil::notification_event> os_event_ {};
    };
}

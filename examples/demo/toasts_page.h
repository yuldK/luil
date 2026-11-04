#pragma once

// 토스트 페이지다: 심각도별 토스트와 액션 버튼이 있는 계속 남는 토스트, 그리고 닫기 전까지 남는 정보 막대.
// 토스트 목록과 만료 판정은 여기(앱)가 소유하고, 다른 페이지도
// toast_request_intent로 토스트를 띄울 수 있다.
// 정보 막대가 열려 있는지는 앱 상태다. 막대는 닫기를 누르면 스스로 사라지지 않고, 이 페이지가 다음 frame에 막대를 짓지 않는다.
// OS 알림 구역은 앱 창 밖 Windows 알림 센터에 올리고 바꾸고 거두는 예다 (os-notification-design.md).
//  - 페이지는 요청(`os_notification_request_intent`)만 던진다. 셸(demo_main)이 `show_notification`·`close_notification`을 부르고
//    결과·상태를 이 페이지에 돌려준다. 사용자가 누른 알림은 라이브러리가 `make_notification_message`로 알리고 셸이 사건으로 넘긴다.
//  - 상태·결과·사건 세 줄이 label이라 UIA 이름으로 되읽힌다.
// 안내 풍선은 "새 기능" 단추를 가리킨다 (teaching-tip-design.md). 열림·덮개 여부·연 시각·마지막 닫힘은 이 페이지가 든다.
//  - 풍선은 토스트 더미와 함께 `make_overlay`가 낸다. 대상의 보이는 자리는 같은 frame에 배치된 이 페이지의 가지에서 찾는다.
//  - 상태 label의 "대상 없음"은 그 자리를 찾지 못한 frame의 답이다. 다음 frame에 그리므로 그때 한 번 더 짓도록 예고한다.

#include "demo/common.h"

#include "luil/app/notification.h"
#include "luil/ui/severity.h"
#include "luil/ui/teaching_tip_element.h"
#include "luil/win32/app_host.h"

#include <array>
#include <chrono>
#include <cstdint>
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

    // 안내 풍선을 연다. 덮개인지와 연 단추(돌아갈 자리)를 나른다.
    struct tip_open_intent
    {
        bool modal { true };
        luil::ui_element_id opener {};
    };

    // 안내 풍선이 닫자고 했다 (닫기 단추·덮개·Esc·뒤로 가기).
    struct tip_dismiss_intent
    {
        luil::teaching_tip_dismiss_reason reason { luil::teaching_tip_dismiss_reason::close_button };
    };

    // 안내 풍선의 행동 단추를 눌렀다. 이 데모는 어느 쪽이든 풍선을 닫는다.
    struct tip_action_intent
    {
        bool primary { true };
    };

    // 풍선이 가리키는 "새 기능" 단추를 눌렀다.
    struct tip_target_intent
    {
    };

    // 안내 풍선의 owner와 대상 단추다.
    inline constexpr std::u8string_view tip_owner { u8"toasts/tip" };
    [[nodiscard]] inline luil::ui_element_id tip_target_id()
    {
        return { kind_toast_trigger, u8"toasts/tip-target" };
    }

    class toasts_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 화면 오른쪽 아래에 쌓이는 토스트 오버레이와 안내 풍선이다.
        // 어느 페이지가 보이든 창 전체 위에 얹는다. 토스트도 풍선도 없으면 nullptr다. 풍선은 끝에 서서 Tab 순서의 끝이다.
        //  - 이번 frame에 build가 만든 가지에서 대상의 보이는 자리를 찾으므로 그 가지를 배치한 뒤 부른다. 쓴 뒤 그 가지를 잊는다.
        //    build가 돌지 않은 frame(다른 페이지)에는 대상이 없어 풍선이 가운데에 꼬리 없이 선다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_overlay();

        // 다음 토스트가 만료되는 시각이다 (만료될 것이 없으면 nullopt).
        // driver가 next_tick으로 이 값을 예고하면 메시지가 없어도
        // 그 시각에 tick → prune → frame 재게시가 돈다.
        //  - 풍선 상태 label이 이번 frame의 대상 자리와 다르면 지금을 답해 한 frame을 더 짓게 한다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_expiry() const;

        // 안내 풍선을 닫는다. 셸이 페이지를 떠날 때 부른다 (대상 가지가 사라진 화면에 풍선을 남기지 않는다).
        void close_tip();
        [[nodiscard]] bool tip_open() const noexcept;
        // 비modal 풍선을 연 명령과 닫기 단추의 초점 요청이다. 열 때 primary로, 닫을 때 연 단추로 옮긴다. 번호가 바뀔 때만 새 요청이다.
        //  - 덮개 풍선은 가둠의 진입·복귀가 초점을 옮기므로 요청을 내지 않는다.
        [[nodiscard]] std::optional<luil::focus_request> make_focus_request() const;

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
        // 안내 풍선 구역(대상 단추, 여는 단추 둘, 상태 label)을 페이지에 더한다.
        void add_tip_card(page_layout& page);
        // 풍선을 닫고 계기를 적는다. 비modal이면 연 단추로 초점을 돌린다.
        void finish_tip(std::u8string reason);
        void request_focus(luil::ui_element_id target);
        // 열려 있으면 풍선을, 토스트가 있으면 토스트 더미를 짓는다. 없으면 nullptr다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_tip();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_toast_stack() const;

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
        // 안내 풍선의 상태다. 연 시각이 나타남 움직임을 그린다.
        bool tip_open_ { false };
        bool tip_modal_ { true };
        luil::ui_element_id tip_opener_ {};
        std::chrono::steady_clock::time_point tip_opened_at_ {};
        std::u8string tip_last_close_ {};
        // 지난 make_overlay가 대상을 찾았는가와, 이번 build의 label이 그린 값이다. 다르면 한 frame을 더 짓는다.
        bool tip_target_found_ { false };
        bool tip_label_found_ { false };
        bool tip_relabel_ { false };
        // 이번 build가 만든 페이지 가지다. 풍선 대상의 자리를 찾는 데만 쓰고 make_overlay가 쓴 뒤 잊는다.
        const luil::ui_element* page_root_ { nullptr };
        std::uint64_t focus_revision_ { 0 };
        luil::ui_element_id focus_target_ {};
    };
}

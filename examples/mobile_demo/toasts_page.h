#pragma once

// 토스트 페이지다: 심각도별 토스트와 액션 버튼이 있는 계속 남는 토스트, 그리고 닫기 전까지 남는 정보 막대.
// 토스트 목록과 만료 판정은 여기(앱)가 소유하고, 다른 페이지도
// toast_request_intent로 토스트를 띄울 수 있다.
// 정보 막대가 열려 있는지는 앱 상태다. 막대는 닫기를 누르면 스스로 사라지지 않고, 이 페이지가 다음 frame에 막대를 짓지 않는다.
// 안내 풍선은 "새 기능" 단추를 가리킨다 (teaching-tip-design.md). 손가락 칸(44)의 단추를 쓰고 뒤로 가기로도 닫힌다.
//  - 풍선은 토스트 더미와 함께 `make_overlay`가 낸다. 대상의 보이는 자리는 셸이 넘긴 이번 frame의 root에서 찾는다.

#include "mobile_demo/common.h"

#include "luil/ui/severity.h"
#include "luil/ui/teaching_tip_element.h"

#include <array>
#include <chrono>
#include <optional>
#include <vector>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    // 계속 남는 토스트의 실행 취소·닫기다.
    struct toast_undo_intent
    {
        std::u8string id {};
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

    // 안내 풍선을 연다. 덮개인지와 연 단추(돌아갈 자리)를 나른다.
    struct tip_open_intent
    {
        bool modal { true };
        luil::ui_element_id opener {};
    };

    // 안내 풍선이 닫자고 했다 (닫기 단추·덮개 탭·Esc·뒤로 가기).
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

    inline constexpr std::u8string_view tip_owner { u8"tip" };

    [[nodiscard]] inline luil::ui_element_id tip_target_id()
    {
        return { kind_toast_trigger, u8"tip-target" };
    }

    class toasts_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 화면 오른쪽 아래에 쌓이는 토스트 오버레이와 안내 풍선이다.
        // 어느 페이지가 보이든 창 전체 위에 얹는다. 토스트도 풍선도 없으면 nullptr다. 풍선은 끝에 서서 Tab 순서의 끝이다.
        //  - `scope`는 이번 frame에 배치를 마친 화면의 root다. 그 안에서 대상의 보이는 자리를 찾는다. 비거나 대상이 없으면
        //    풍선이 가운데에 꼬리 없이 선다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_overlay(const luil::ui_element* scope = nullptr);

        // 다음 토스트가 만료되는 시각이다 (만료될 것이 없으면 nullopt).
        // driver가 next_tick으로 이 값을 예고하면 메시지가 없어도
        // 그 시각에 tick → prune → frame 재게시가 돈다.
        //  - 풍선 상태 label이 이번 frame의 대상 자리와 다르면 지금을 답해 한 frame을 더 짓게 한다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_expiry() const;

        // 안내 풍선을 닫는다. 셸이 페이지를 떠날 때 부른다.
        void close_tip();
        [[nodiscard]] bool tip_open() const noexcept;
        // 뒤로 가기가 풍선을 닫는 메시지다 (`ui_frame::back`). 셸은 풍선이 열려 있을 때 싣는다.
        [[nodiscard]] luil::input_action back_action() const;

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
        // 안내 풍선 구역(대상 단추, 여는 단추 둘, 상태 설명)을 페이지에 더한다.
        void add_tip_section(page_column& column, float width, float scale);
        void finish_tip(std::u8string reason);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_tip(const luil::ui_element* scope);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_toast_stack() const;

        std::vector<toast_entry> toasts_ {};
        int next_id_ { 0 };
        // 정보 막대가 열려 있는가. 인덱스는 심각도 순서(정보·성공·경고·오류)다.
        std::array<bool, 4> bars_open_ { true, true, true, true };
        // 마지막으로 누른 막대의 동작이다. 비어 있으면 아직 없다.
        std::u8string last_bar_action_ {};
        // 안내 풍선의 상태다. 연 시각이 나타남 움직임을 그린다.
        bool tip_open_ { false };
        bool tip_modal_ { true };
        luil::ui_element_id tip_opener_ {};
        std::chrono::steady_clock::time_point tip_opened_at_ {};
        std::u8string tip_last_close_ {};
        // 지난 make_overlay가 대상을 찾았는가, 이번 build의 설명이 그린 값, 이번 frame에 build가 돌았는가다.
        bool tip_target_found_ { false };
        bool tip_label_found_ { false };
        bool tip_built_ { false };
        bool tip_relabel_ { false };
    };
}

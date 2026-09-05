#pragma once

// widgets 예제의 앱 골격이다.
//
// hello 예제가 "가장 작은 앱"이라면, 이 예제는 흔한 element들을
// 하나씩 떼어 보여 주는 진열장이다. 섹션마다 파일 하나씩이다.
//   controls_section.cpp — 버튼(글자·아이콘)·라벨·panel·tooltip·커서
//   inputs_section.cpp   — 한 줄 텍스트 입력 (편집·IME·붙여넣기·필터)
//   choices_section.cpp  — 라디오·토글 묶음, 낱개 컨트롤(체크박스·라디오·스위치), 접이식 그룹
//   status_section.cpp   — 진행률 막대, 상태 배지
//   toasts_section.cpp   — 토스트 알림과 시간 만료(tick)
//
// 구조는 hello와 같다: driver가 상태를 소유하고, element는 상태를 설정으로
// 받아 그리며, 상호작용은 "바꾸자"는 메시지(intent)로만 돌아온다.
// 각 섹션 파일이 자기가 다루는 element의 API를 주석으로 설명한다.

#include "luil/luil.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace widgets {
    // --- 앱 정의 kind ---
    // 라이브러리는 kind를 정체성으로만 쓰고 해석하지 않는다.
    // 배치 컨테이너처럼 상호작용 없는 것은 kind 하나를 owner로 나눠 쓴다.
    constexpr luil::ui_element_kind kind_layout { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_text { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_counter_button { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_icon_button { luil::application_element_kind(3) };
    constexpr luil::ui_element_kind kind_note_input { luil::application_element_kind(4) };
    constexpr luil::ui_element_kind kind_toast_button { luil::application_element_kind(5) };
    constexpr luil::ui_element_kind kind_panel { luil::application_element_kind(6) };
    constexpr luil::ui_element_kind kind_progress { luil::application_element_kind(7) };
    constexpr luil::ui_element_kind kind_badge { luil::application_element_kind(8) };
    constexpr luil::ui_element_kind kind_icon_text_button { luil::application_element_kind(9) };
    constexpr luil::ui_element_kind kind_link { luil::application_element_kind(10) };
    constexpr luil::ui_element_kind kind_slider { luil::application_element_kind(11) };

    // --- 텍스트 입력 대상 ---
    // 텍스트 박스마다 대상 id를 하나 정한다.
    // policy가 "이 kind는 이 대상"이라고 등록해야 초점·IME·클립보드가 이어진다.
    constexpr luil::text_input_target target_note { static_cast<luil::text_input_target>(1) };

    // --- 앱 메시지(intent) ---
    struct close_intent
    {};

    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    // controls: 카운터 버튼.
    struct increment_intent
    {};

    // inputs: 편집·조합 메시지. policy가 라이브러리 요청을 이 봉투에 담는다.
    struct edit_intent
    {
        luil::text_edit_request request {};
    };

    struct composition_intent
    {
        luil::text_composition_event event {};
    };

    // choices: 선택과 접기.
    struct choose_fruit_intent
    {
        std::u8string value {};
    };

    struct choose_view_intent
    {
        std::u8string value {};
    };

    struct collapse_intent
    {};

    // choices: 낱개 컨트롤 하나를 뒤집는다.
    // 어느 것인지는 owner가 말한다 — element는 뒤집지 않고 이 메시지만 낸다.
    struct check_intent
    {
        std::u8string owner {};
    };

    // status: 진행률을 끈 만큼 바꾼다 (값 단위, +가 오른쪽).
    // 범위 다듬기는 값을 가진 앱의 몫이다 — element는 변화량만 나른다.
    struct slide_intent
    {
        float delta { 0.0f };
    };

    // status: 진행률을 이 값으로 정한다 (보조 기술의 SetValue).
    // 목표를 그대로 실어야 같은 명령이 겹쳐도 두 배로 움직이지 않는다.
    struct slide_to_intent
    {
        float value { 0.0f };
    };

    // toasts: 알림 요청.
    struct toast_intent
    {
        std::u8string text {};
        luil::toast_severity severity { luil::toast_severity::info };
    };

    // --- 앱 상태 ---
    // driver가 소유하고 섹션 빌더가 읽는다.
    // element는 이 값들을 설정으로 받아 그릴 뿐, 직접 바꾸지 않는다.
    struct app_state
    {
        int clicks { 0 };
        // 텍스트 초안과 IME 조합 표시 상태다.
        // 확정 글의 진실은 언제나 이 초안이고, 조합 글은 표시 상태일 뿐이다.
        luil::text::text_edit_state note {};
        std::optional<luil::text_composition_event> composition {};
        std::u8string fruit { u8"apple" };
        std::u8string view { u8"list" };
        bool choices_collapsed { false };
        // 낱개 컨트롤 셋의 상태다 (체크박스·라디오·스위치).
        bool wrap_lines { true };
        bool use_metric { false };
        bool dark_preview { false };
        // slider가 정하고 progress가 보여 주는 값이다 (0..1).
        // 하나의 앱 상태를 컨트롤과 표시가 나눠 본다.
        float progress { 0.3f };
        // 토스트 목록이다. 만료 판정도 앱 몫이다 (tick이 지운다).
        struct toast_entry
        {
            std::u8string id {};
            std::u8string text {};
            luil::toast_severity severity { luil::toast_severity::info };
            std::chrono::steady_clock::time_point shown_at {};
        };
        std::vector<toast_entry> toasts {};
        int next_toast_id { 0 };
    };

    // --- 섹션 빌더의 공통 반환형 ---
    // stack에는 길이를 함께 넣어야 하므로(측정 단계가 없다) element와
    // 높이(논리 픽셀)를 짝으로 돌려준다.
    struct section
    {
        std::unique_ptr<luil::ui_element> element {};
        float height { 0.0f };
    };

    // 섹션 빌더들이다. 각 파일이 하나씩 구현한다.
    [[nodiscard]] section build_controls_section(const app_state& state);
    [[nodiscard]] section build_inputs_section(const app_state& state);
    [[nodiscard]] section build_choices_section(const app_state& state);
    [[nodiscard]] section build_status_section(const app_state& state);
    [[nodiscard]] section build_toasts_section(const app_state& state);
    // 토스트 오버레이는 섹션이 아니라 창 전체 위에 얹는다.
    [[nodiscard]] std::unique_ptr<luil::ui_element> build_toast_overlay(const app_state& state);

    // --- logic thread ---
    class widgets_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override;
        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override;
        [[nodiscard]] luil::app_message make_close_message() override;
        [[nodiscard]] bool shutdown_completed() const override;

        // 토스트 만료의 시간 경로다.
        // 다음 만료 시각을 예고하면 메시지가 없어도 그 시각에 tick이 온다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override;
        void tick(std::chrono::steady_clock::time_point now) override;

    private:
        metrics_intent metrics_ {};
        app_state state_ {};
        std::atomic<bool> closed_ { false };
    };

    // --- input thread: 입력을 앱 메시지로 ---
    class widgets_policy final : public luil::interaction_policy
    {
    public:
        // "이 kind는 텍스트 박스다"의 등록부다.
        // 여기 등록해야 클릭 초점·caret·IME·복사·붙여넣기가 전부 이어진다.
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(luil::ui_element_kind kind) const override;
        // 라이브러리가 만든 편집·조합 요청을 앱 메시지 봉투에 담는다.
        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override;
        [[nodiscard]] luil::input_action make_text_composition_action(const luil::text_composition_event& event) const override;
    };

    // --- UI thread ---
    class widgets_delegate final : public luil::win32::window_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(float width, float height, float scale) override;
    };
} // namespace widgets

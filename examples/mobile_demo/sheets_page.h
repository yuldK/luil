#pragma once

// 시트 페이지다: 아래 시트·오른쪽 옆 시트·왼쪽 서랍을 열고, 회전과 크기 변경에도 남는 것을 본다.
// 열림·멈춤점·끄는 양·시트 안 목록의 스크롤은 전부 이 페이지의 상태라 tree를 다시 지을 때마다 그대로 배치된다.

#include "luil/ui/root_element.h"
#include "luil/ui/sheet_host_element.h"
#include "luil/ui/spring.h"
#include "mobile_demo/common.h"

#include <chrono>
#include <optional>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    struct sheet_open_intent
    {
        luil::sheet_kind kind { luil::sheet_kind::bottom };
        bool modal { true };
    };

    struct sheet_drag_intent
    {
        float delta { 0.0f };
    };

    struct sheet_settle_intent
    {
        luil::sheet_detent detent { luil::sheet_detent::half };
    };

    // 끌기를 놓았다. 놓은 순간의 높이·멈출 자리·속도에서 스프링으로 이어 움직인다 (`sheet_host_config::release`).
    struct sheet_release_intent
    {
        luil::sheet_release release {};
    };

    struct sheet_dismiss_intent
    {
        luil::sheet_dismiss_reason reason { luil::sheet_dismiss_reason::scrim };
    };

    struct sheet_scroll_intent
    {
        float delta { 0.0f };
    };

    struct sheet_adaptive_intent
    {
        bool adaptive { false };
    };

    class sheets_page
    {
    public:
        // `now`는 스프링을 시작하거나 이어 끄는 시각이다. 시계는 앱이 읽는다.
        bool handle(const luil::app_message& message, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
        [[nodiscard]] page_content build(float width, float scale);

        // 시트가 열려 있으면 창 전체에 시트 host를 얹는다. `surface`는 그릴 수 있는 표면 전체다 — scrim과 시트 바탕이
        // 안전 영역 밖까지 닿는다. `now`는 놓은 뒤 도는 스프링의 지금 값을 묻는 시각이다.
        void add_overlay(luil::root_element& root, float width, float height, float scale, const luil::rect_f& surface, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

        [[nodiscard]] bool sheet_open() const noexcept
        {
            return open_.has_value();
        }

        // 뒤로 가기가 낼 메시지다 (열린 시트를 뒤로 가기 계기로 닫는다).
        [[nodiscard]] luil::input_action back_action() const;

        // 시스템의 움직임 줄이기 설정이다 (`window_metrics::reduce_motion`). 켜지면 도는 중이던 스프링도 멈춤점에 붙인다.
        void set_reduce_motion(bool reduce_motion) noexcept;

        // 놓은 뒤 시트가 도는 동안 다시 지을 시각이다 (끝났으면 없다). `now`는 마지막으로 지은 frame의 시각이다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(std::chrono::steady_clock::time_point now) const noexcept;

    private:
        struct open_sheet
        {
            luil::sheet_kind kind { luil::sheet_kind::bottom };
            bool modal { true };
        };

        [[nodiscard]] luil::sheet_host_config config(std::chrono::steady_clock::time_point now) const;
        // 지금 끄는 양이다. 손가락이 끈 양에 놓은 뒤 도는 스프링의 값을 더한다.
        [[nodiscard]] float offset_at(std::chrono::steady_clock::time_point now) const noexcept;
        void close(luil::sheet_dismiss_reason reason);
        // 놓은 순간에서 멈출 자리까지 스프링을 시작한다. 닫는 놓기는 전처럼 바로 거둔다.
        void apply_release(const luil::sheet_release& release, std::chrono::steady_clock::time_point now);

        std::optional<open_sheet> open_ {};
        luil::sheet_detent detent_ { luil::sheet_detent::half };
        float drag_offset_ { 0.0f };
        // 놓은 뒤 끄는 양을 0으로 보내는 스프링이다. 높이가 아니라 끄는 양을 태우므로, 도는 중에 다시 잡을 때 화면 높이를
        // 몰라도 지금 끄는 양에서 이어 끌 수 있다. 값이 줄어드는 만큼 시트가 멈춤점 높이에 다가간다.
        luil::spring motion_ { luil::spring_at_rest(0.0f) };
        // 가장 최근 놓기의 멈출 자리 높이다 (논리 픽셀, 화면에 적는다).
        float target_height_ { 0.0f };
        bool reduce_motion_ { false };
        float list_offset_ { 0.0f };
        bool adaptive_ { true };
        // 마지막으로 시트가 닫힌 계기다 (화면에 적는다).
        std::optional<luil::sheet_dismiss_reason> last_dismiss_ {};
    };
}

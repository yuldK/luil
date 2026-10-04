#pragma once

// 시트 페이지다: 아래 시트·오른쪽 옆 시트·왼쪽 서랍을 열고, 회전과 크기 변경에도 남는 것을 본다.
// 열림·멈춤점·끄는 양·시트 안 목록의 스크롤은 전부 이 페이지의 상태라 tree를 다시 지을 때마다 그대로 배치된다.

#include "luil/ui/root_element.h"
#include "luil/ui/sheet_host_element.h"
#include "mobile_demo/common.h"

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
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 시트가 열려 있으면 창 전체에 시트 host를 얹는다. `surface`는 그릴 수 있는 표면 전체다 — scrim과 시트 바탕이
        // 안전 영역 밖까지 닿는다.
        void add_overlay(luil::root_element& root, float width, float height, float scale, const luil::rect_f& surface);

        [[nodiscard]] bool sheet_open() const noexcept
        {
            return open_.has_value();
        }

        // 뒤로 가기가 낼 메시지다 (열린 시트를 뒤로 가기 계기로 닫는다).
        [[nodiscard]] luil::input_action back_action() const;

    private:
        struct open_sheet
        {
            luil::sheet_kind kind { luil::sheet_kind::bottom };
            bool modal { true };
        };

        [[nodiscard]] luil::sheet_host_config config() const;

        std::optional<open_sheet> open_ {};
        luil::sheet_detent detent_ { luil::sheet_detent::half };
        float drag_offset_ { 0.0f };
        float list_offset_ { 0.0f };
        bool adaptive_ { true };
        // 마지막으로 시트가 닫힌 계기다 (화면에 적는다).
        std::optional<luil::sheet_dismiss_reason> last_dismiss_ {};
    };
} // namespace mobile_demo

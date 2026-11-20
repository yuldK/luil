#pragma once

#include "win32/window_surface.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luil::win32 {
    // 도킹 표면 창의 스타일이다 (docked-surface-design.md).
    //  - 캡션·테두리 없는 `WS_POPUP`이라 사용자가 끌어 옮기거나 크기를 바꿀 자리가 없다.
    //  - `WS_EX_TOOLWINDOW`라 작업 표시줄 단추와 Alt+Tab 항목이 없다. 소유자가 없어 주 창이 최소화·숨김이어도 그대로 보인다.
    //  - `WS_EX_TOPMOST`로 언제나 위에, `WS_EX_NOACTIVATE`로 눌러도 활성화되지 않는다. 앱이 청하면 `SetForegroundWindow`로
    //    잠깐 활성화된다 (`focus_request::activate`).
    [[nodiscard]] DWORD docked_window_style() noexcept;
    [[nodiscard]] DWORD docked_extended_style() noexcept;

    // 지금 연결된 모니터들이다 (OS가 알린 순서). 읽지 못하면 빈 목록이다.
    [[nodiscard]] std::vector<display_info> enumerate_displays();

    // 도킹 표면 창 하나다.
    // popup처럼 표면 공통 번역(포인터·휠·키·IME·그리기)만으로 완결되고, 캡션 chrome·최소 크기·저장 배치가 없다.
    // 남는 것은 이 표면만의 것: 앱이 정한 자리를 창에 거는 것, 크기·배율 알림, 활성화를 막는 것이다.
    class docked_surface final : public window_surface
    {
    public:
        docked_surface(surface_context& context, std::u8string id) noexcept;

        // 도킹 표면의 window procedure다.
        // `WM_NCCREATE`의 생성 인자로 받은 표면을 창에 묶고 그 표면에 넘긴다.
        static LRESULT CALLBACK static_procedure(HWND window, UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 새 frame이 실은 값으로 갱신한다. tree가 바뀌었으면 참이다 (소유자가 다시 그리기를 건다).
        //  - 자리는 여기서 걸지 않는다. 가장자리 자리는 모니터 목록이 있어야 풀리므로 소유자가 풀어 `apply_bounds`로 준다.
        [[nodiscard]] bool adopt(const ui_docked_surface& source);

        // 풀린 자리를 창에 건다. 지난번과 같으면 OS를 부르지 않는다. 빈 값이면 숨긴다.
        //  - 걸 때마다 맨 위(`HWND_TOPMOST`)를 다시 세운다. 작업 표시줄처럼 같은 맨 위 창이 앞으로 나서면 그 아래로 밀린다.
        //  - 활성화하지 않는다 (`SWP_NOACTIVATE`).
        void apply_bounds(const std::optional<screen_rect>& bounds);
        [[nodiscard]] const std::optional<screen_rect>& bounds() const noexcept
        {
            return bounds_;
        }

        // 창의 실제 client 크기·배율을 앱에 알린다 (생성 직후·크기 변경·배율 변경).
        void post_metrics() const noexcept;

    private:
        LRESULT procedure(UINT message, WPARAM word_parameter, LPARAM long_parameter);
        [[nodiscard]] bool needs_topmost() const noexcept;

        // 이 표면의 시간 예고를 update timer에 싣는다 (보조 창과 같다).
        void prepare_frame(frame_state& state) override;

        // 주 창·보조 창처럼 설정을 따라 OS 끌기를 받는다. 작업 표시줄 곁의 위젯이 끌어 온 파일·그림을 받는 자리다.
        [[nodiscard]] bool accepts_file_drop() const noexcept override
        {
            return context_.config().accept_file_drop;
        }

        std::function<app_message(float, float, float)> metrics_ {};
        std::u8string title_ {};
        // 마지막으로 창에 건 자리다. 비어 있으면 숨어 있다.
        std::optional<screen_rect> bounds_ {};
    };
}

#pragma once

#include "host/popup_dismiss.h"
#include "host/popup_reconcile.h"
#include "win32/window_surface.h"

#include <functional>

namespace luil::win32 {
    // 살아 있는 popup 창 하나다 (메뉴·드롭다운·긴 tooltip).
    // 자리는 **앵커 표면** client 기준 물리 픽셀이고, 배율도 그 표면 것을
    // 물려받는다 (popup-overlay-design.md, popup-anchor-design.md).
    //  - 초점을 받지 않으므로 키·문자·caption chrome이 없다.
    //    표면 공통 번역(포인터·휠·커서·그리기)만으로 완결된다.
    class popup_surface final : public window_surface
    {
    public:
        // 앵커는 만든 뒤 바뀌지 않는다 — 바뀌면 소유자가 이 표면을 다시 만든다.
        popup_surface(surface_context& context, std::u8string id, std::u8string anchor) noexcept;

        [[nodiscard]] const std::u8string& anchor() const noexcept
        {
            return anchor_;
        }

        // popup 창의 window procedure다.
        // `WM_NCCREATE`의 생성 인자로 받은 표면을 창에 묶고 그 표면에 넘긴다.
        static LRESULT CALLBACK static_procedure(HWND window, UINT message, WPARAM word_parameter, LPARAM long_parameter);

        [[nodiscard]] popup_placement placement() const;
        // popup은 부모 창의 배율을 그대로 쓴다 (`set_dpi`).
        void set_placement(const popup_placement& placement) noexcept;

        // 새 frame이 실은 값으로 갱신한다.
        // 새 frame이 popup을 계속 실었다는 것은 앱이 상태를 유지하기로 했다는 뜻이라
        // 닫힘 메시지를 다시 낼 수 있게 풀고 factory도 새 것으로 바꾼다.
        // 내용이 바뀌었으면 참이다 (소유자가 다시 그리기를 건다).
        [[nodiscard]] bool adopt(const ui_popup& source);

        // 닫힘 계기 하나를 이 popup에 알린다.
        // 한 계기에 한 번만 내며, 낼 것이 없으면 nullopt다.
        [[nodiscard]] std::optional<input_action> take_dismiss_action(popup_dismiss_reason reason);

    private:
        LRESULT procedure(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // popup 위의 클릭은 popup **안** 클릭이라 닫힘 계기가 아니다.
        [[nodiscard]] bool input_dismisses_popups() const noexcept override
        {
            return false;
        }

        // popup 둘레의 테두리는 tree가 아니라 표면이 긋는다 (`ui_popup::border`).
        void prepare_frame(frame_state& state) override
        {
            state.border = border_;
        }

        std::u8string anchor_ {};
        int x_ { 0 };
        int y_ { 0 };
        int width_ { 0 };
        int height_ { 0 };
        std::function<input_action(popup_dismiss_reason)> dismiss_ {};
        // 한 계기에 닫자는 메시지를 한 번만 낸다.
        // 새 frame이 popup을 계속 실으면 다시 낼 수 있게 풀린다.
        bool dismiss_requested_ { false };
        // 새 frame이 실은 값을 그대로 따른다 (`adopt`).
        bool border_ { true };
    };
} // namespace luil::win32

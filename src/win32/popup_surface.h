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
        void set_placement(const popup_placement& placement);

        // 창의 보이는 높이다 (물리 픽셀). 소유자가 모니터 작업 영역에 맞춰 정한다.
        // popup의 내용(`placement().height`)보다 작으면 popup이 내용을 흘려 보인다 — 작업 영역보다 긴 메뉴가 아래가
        // 잘리는 대신 스크롤된다 (popup-overlay-design.md).
        //  - 휠로 흘리고, 위·아래 화살표 칸에 포인터를 두면 그쪽으로 흘러가며, 키보드 강조(`menu_highlight`)와
        //    키보드 초점이 옮겨 가면 그 항목이 보이게 따라간다.
        //  - 흘린 양은 이 표면의 것이다 (UI thread). 앱도 input thread도 모른다 — 포인터는 그만큼 옮겨 tree에 묻는다.
        void set_viewport_height(int height);
        [[nodiscard]] int viewport_height() const noexcept
        {
            return viewport_height_ > 0 ? viewport_height_ : height_;
        }
        // 흘린 양이다 (물리 픽셀). 이 popup에 앵커한 자식 popup의 자리가 그만큼 올라간다.
        [[nodiscard]] int scroll_offset() const noexcept
        {
            return scroll_;
        }
        // 키보드 강조·초점이 새로 옮겨 갔으면 그 항목이 칸에 가리지 않게 흘린다 (`prepare_frame`).
        // `interaction`은 이 표면으로 거른 것이다 (`interaction_for_surface`).
        void follow_keyboard(const interaction_snapshot& interaction);

        // 새 frame이 실은 값으로 갱신한다.
        // 새 frame이 popup을 계속 실었다는 것은 앱이 상태를 유지하기로 했다는 뜻이라
        // 닫힘 메시지를 다시 낼 수 있게 풀고 factory도 새 것으로 바꾼다.
        // 내용이 바뀌었으면 참이다 (소유자가 다시 그리기를 건다).
        //  - `shape`는 소유자가 그 frame의 스타일과 OS 지원으로 푼 모양이다. 둥근 창과 직각 창은 창 클래스가 달라
        //    여기 오기 전에 다시 만들어졌다 — 여기서 바뀌는 것은 모서리 크기와 경계뿐이다.
        [[nodiscard]] bool adopt(const ui_popup& source, const popup_shape& shape);

        [[nodiscard]] const popup_shape& shape() const noexcept
        {
            return shape_;
        }

        // 창의 모서리를 DWM에 건다 (창을 만든 직후와 모서리가 바뀔 때).
        // 걸지 못하면(모서리 선택이 없는 OS) 직각으로 물러서 표면의 선을 다시 켠다.
        void apply_corner() noexcept;

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

        // popup 안 글 칸의 조합은 앵커 표면의 session이 맡는다 (popup-ime-design.md).
        void commit_composition_before_press() override;

        // popup 둘레의 경계다 (`ui_popup::border`). 직각 창은 표면이 1px 선을 긋고, 둥근 창은 OS 테두리를 팔레트에
        // 맞춘다 — 그 색은 frame의 테마에서 오므로 그릴 때마다 견주고 바뀐 때만 DWM에 말한다.
        void prepare_frame(frame_state& state) override;

        [[nodiscard]] int content_offset_y() const noexcept override
        {
            return scroll_;
        }
        [[nodiscard]] std::optional<rect_f> content_viewport() const noexcept override;
        [[nodiscard]] bool handle_pointer_overlay(POINT position, bool pressed) override;
        // 스크롤 변경은 자식 popup의 화면 자리도 바꾼다.
        bool set_scroll(int offset);
        // 내용이 창보다 길어 흘리는가.
        [[nodiscard]] bool scrollable() const noexcept;
        [[nodiscard]] int maximum_scroll() const noexcept;
        // 화살표 칸의 높이다 (물리 픽셀).
        [[nodiscard]] int strip_height() const noexcept;
        // client y가 화살표 칸 위인가다. 위 칸이면 -1, 아래 칸이면 1, 아니면 0이다. 지금 선 칸만 답한다.
        [[nodiscard]] int strip_at(int y) const noexcept;
        // 흘린 양을 바꾼다. 바뀌었으면 참이고, 다시 그리며 포인터 밑의 hover를 새 자리로 다시 묻는다.
        bool scroll_by(int delta);
        // 포인터 밑의 내용이 바뀌었으니 같은 자리의 이동을 다시 알린다.
        void refresh_pointer_hover();
        // 칸이 흘린다 (`direction` 쪽으로 `amount`). 끝에 닿으면 칸이 사라진 자리의 연타를 막아 둔다 (`spent_strip_`).
        bool scroll_from_strip(int direction, int amount);
        // 이 누름이 끝에 닿아 사라진 칸 자리의 연타인가다. 참이면 삼키고 막는 시간을 잇는다.
        [[nodiscard]] bool press_lands_on_spent_strip(int y);
        // 포인터가 지금 실제로 이 요소 위에 있는가다.
        [[nodiscard]] bool pointer_over(const ui_element& element) const noexcept;
        // 요소가 칸에 가리지 않는 자리까지 흘린다.
        void scroll_to_show(const ui_element& element);
        // 보조 기술의 들이기·초점 요청도 키보드처럼 그 항목을 보인다. 흘린 양은 이 표면의 것이라 앱은 할 수 없다.
        [[nodiscard]] bool accessibility_content_scrollable() const noexcept override
        {
            return scrollable();
        }
        void accessibility_reveal(const ui_element_id& id) override;
        // 화살표 칸 위의 포인터를 다룬다. 다뤘으면 결과다.
        [[nodiscard]] std::optional<LRESULT> handle_scroll_message(UINT message, WPARAM word_parameter, LPARAM long_parameter);
        void stop_strip_scroll() noexcept;

        std::u8string anchor_ {};
        int x_ { 0 };
        int y_ { 0 };
        int width_ { 0 };
        int height_ { 0 };
        std::function<input_action(popup_dismiss_reason)> dismiss_ {};
        // 한 계기에 닫자는 메시지를 한 번만 낸다.
        // 새 frame이 popup을 계속 실으면 다시 낼 수 있게 풀린다.
        bool dismiss_requested_ { false };
        // 새 frame이 실은 값을 푼 모양이다 (`adopt`).
        popup_shape shape_ {};
        // DWM에 마지막으로 말한 테두리 색과 명암이다. 같은 답이면 다시 말하지 않는다.
        std::optional<std::pair<COLORREF, BOOL>> dwm_theme_ {};
        // 창의 보이는 높이와 흘린 양이다 (물리 픽셀, `set_viewport_height`).
        int viewport_height_ { 0 };
        int scroll_ { 0 };
        // 화살표 칸이 흘리는 쪽이다 (-1 위, 1 아래, 0 멈춤). 칸 위에 있는 동안 timer가 흘린다.
        int strip_direction_ { 0 };
        // 끝에 닿아 사라진 칸의 쪽과 그 자리의 누름을 삼키는 기한이다 (-1 위, 1 아래, 0 없음).
        //  - 칸을 연타하던 손의 다음 누름이 그 자리로 올라온 항목(흔히 '삭제'·'종료')을 실행하면 안 된다.
        //  - 기한은 더블 클릭 시간이다. 터치에는 포인터를 떼어 풀 이동이 없어 시간으로 푼다.
        int spent_strip_ { 0 };
        ULONGLONG spent_strip_until_ { 0 };
        // 마지막으로 따라간 키보드 강조·초점이다. 바뀔 때만 따라간다 — 휠로 흘린 자리를 그리기마다 되돌리지 않는다.
        ui_element_id followed_ {};
    };
}

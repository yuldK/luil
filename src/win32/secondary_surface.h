#pragma once

#include "win32/caption_surface.h"

#include <functional>

namespace luil::win32 {
    // 주 창이 소유하는 보조 top-level 창 하나다 (도구 창·문서 창).
    // 자기 캡션·테두리·배율·renderer·TSF 문서를 갖고 화면 어디로든 움직인다
    // (multi-window-design.md).
    //  - 입력은 창 id를 표식으로 달고 같은 pump에 합류한다.
    //    그 배선은 전부 `window_surface`가 맡고, 여기 남는 것은 이 창만의
    //    것: 크기·배율 알림, 닫힘 요청, 캡션 버튼의 직접 실행이다.
    class secondary_surface final : public caption_surface
    {
    public:
        secondary_surface(surface_context& context, std::u8string id) noexcept;

        // 보조 창의 window procedure다.
        // `WM_NCCREATE`의 생성 인자로 받은 표면을 창에 묶고 그 표면에 넘긴다.
        static LRESULT CALLBACK static_procedure(HWND window, UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 새 frame이 실은 값으로 갱신한다.
        // 새 frame이 창을 계속 실었으면 닫힘 요청을 다시 낼 수 있게 풀고
        // factory도 새 것으로 바꾼다.
        // 내용이 바뀌었으면 참이다 (소유자가 다시 그리기를 건다).
        //  - **캡션과 최소 크기도 여기서 잇는다.** 그 둘을 읽는 자리는 매번 오는
        //    OS 질문(비클라이언트 hit test·크기 한계)이라, 창을 만들 때 한 번만
        //    싣고 두면 앱이 캡션을 바꾼 뒤로 그리기와 판정이 갈라선다.
        //  - 돌려주는 참·거짓은 여전히 **tree**의 것이다. 보이는 캡션은 앱이
        //    tree에 담은 `caption_element`가 그리므로, 캡션만 바뀌고 tree가
        //    그대로면 픽셀도 그대로다.
        [[nodiscard]] bool adopt(const ui_window& source);

        // 창의 실제 client 크기·배율을 앱에 알린다 (생성 직후·크기 조절·DPI 변경).
        // 앱은 이 값으로 그 창의 tree를 다시 배치해 다음 frame에 싣는다.
        void post_metrics() const noexcept;

    private:
        LRESULT procedure(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 보조 창 caption 버튼은 그 창에 직접 적용한다.
        //  - tree 안 caption 버튼의 `ui_command` 액션은 주 창 전용 값이라
        //    여기서는 타지 않는다.
        [[nodiscard]] bool execute_caption_button(WPARAM hit) override;

        void prepare_frame(frame_state& state) override;

        // 닫힘 요청을 낸다 (캡션 close, Alt+F4).
        // UI thread는 메시지만 내고, 창을 없애는 것은 앱이 목록에서 빼는 것이다.
        void request_close();

        std::function<app_message(float, float, float)> metrics_ {};
        std::function<input_action()> close_ {};
        // 한 계기에 닫힘 요청을 한 번만 낸다.
        // 새 frame이 창을 계속 실으면 다시 낼 수 있게 풀린다.
        bool close_requested_ { false };
    };
} // namespace luil::win32

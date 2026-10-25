#pragma once

// 데모 셸의 명령 팔레트다 (command-palette-design.md): 제목 표시줄의 "명령 찾기…" 단추와 Ctrl+Shift+P가 연다.
// 명령은 페이지 이동과 사이드바 접기이고, 고른 명령은 셸이 실행한다 — 팔레트는 무엇을 골랐는지만 알린다.

#include "demo/common.h"
#include "luil/app/app_host.h"
#include "luil/text/text_edit.h"
#include "luil/ui/commands.h"
#include "luil/ui/ui_interaction.h"
#include "luil/win32/win32_window.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace demo {
    struct palette_open_intent
    {};

    struct palette_close_intent
    {};

    // 팔레트에서 고른 명령이다. 페이지 이동은 "page:" 뒤에 페이지 키가 온다.
    struct palette_run_intent
    {
        std::u8string id {};
    };

    constexpr luil::text_input_target target_palette { static_cast<luil::text_input_target>(12) };

    // 팔레트를 여는 단축키다.
    inline constexpr luil::key_chord palette_shortcut { luil::key_code::key_p, true, true };

    // 팔레트가 부르는 명령표다. 사이드바와 같은 페이지 표에서 짓는다.
    [[nodiscard]] std::vector<luil::app_command> palette_commands();

    class command_palette_host
    {
    public:
        // 열기·닫기·검색 칸 편집을 처리했으면 참이다. 실행(`palette_run_intent`)은 셸이 받는다.
        bool handle(const luil::app_message& message);

        [[nodiscard]] bool open() const noexcept
        {
            return open_;
        }

        // 열려 있으면 창 위쪽 가운데의 popup 하나다. 자리는 주 창 client 기준 논리 픽셀이다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float window_width, float top, float scale) const;
        // 열 때마다 검색 칸에 초점을 옮기는 요청이다. 번호는 열 때만 바뀐다.
        [[nodiscard]] std::optional<luil::focus_request> make_focus_request() const;

    private:
        bool open_ { false };
        std::uint64_t focus_revision_ { 0 };
        luil::text::text_edit_state query_ {};
        std::optional<luil::text_composition_event> composition_ {};
    };
} // namespace demo

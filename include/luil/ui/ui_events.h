#pragma once

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element_id.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace luil {
    enum class pointer_button
    {
        none,
        left,
        right,
    };

    // Win32 메시지의 최소 복사다.
    // `HWND`와 lparam 원문은 담지 않는다.
    // time은 UI thread가 게시 시점에 기록하며 더블 클릭·tooltip 판정의 기준이다.
    // interaction controller는 이 값만 읽고 시계를 직접 조회하지 않아 test가 결정적이다.
    //
    // 포인터 이벤트의 surface는 이벤트가 난 표면이다.
    // 비어 있으면 주 창이고, 값은 popup id다 (`ui_popup::id`).
    // 좌표는 그 표면의 client 좌표라 표면의 tree에 그대로 hit한다.
    struct pointer_moved_event
    {
        float x { 0.0f };
        float y { 0.0f };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
    };

    struct pointer_pressed_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
        std::chrono::steady_clock::time_point time {};
        // Shift+클릭은 텍스트 박스에서 선택을 그 자리까지 늘린다.
        bool shift { false };
        std::u8string surface {};
    };

    struct pointer_released_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
    };

    // 포인터가 이 표면의 창을 벗어났다.
    // hover 강조가 창 밖에서 남지 않게 한다.
    //
    // 표식이 있어야 하는 이유는 popup이다. popup은 앵커 창 **위에** 떠 있어
    // 포인터가 그리로 들어가는 것이 앵커 창에서는 떠남이다 — 곧 이 이벤트는
    // "창 밖으로 나갔다"만이 아니라 "다른 표면으로 옮겨 갔다"로도 온다.
    // 표면을 가리지 않고 거두면 popup에 들어서는 순간 그 popup의 hover가
    // 지워지고, popup에서 나오면 주 창의 hover가 함께 지워진다.
    struct pointer_left_event
    {
        std::u8string surface {};
    };

    // keyboard focus가 이 표면의 창을 떠났다.
    // 늦게 도착한 다른 창의 이벤트가 현재 텍스트 초점을 지우지 않도록
    // 어느 표면에서 떠났는지를 함께 나른다.
    struct surface_focus_lost_event
    {
        std::u8string surface {};
    };

    // keyboard focus가 이 표면의 창에 들어왔다 (상실의 짝).
    // controller는 이것으로 **활성 표면**을 안다 — 사용자가 지금 보고 있는 창이다.
    //  - `focused_surface`(논리 초점이 사는 표면)와는 다른 질문의 답이다. popup에
    //    초점이 서면 그 값은 popup id이고 활성 표면은 앵커 창이다.
    //  - popup은 `WS_EX_NOACTIVATE`라 창 초점을 받지 못하므로 popup id가 이
    //    이벤트에 실릴 일이 없다. 상실과 달리 붙은 popup에 퍼뜨리지 않는 이유다
    //    (active-surface-design.md).
    struct surface_focus_gained_event
    {
        std::u8string surface {};
    };

    struct mouse_wheel_event
    {
        float x { 0.0f };
        float y { 0.0f };
        // WHEEL_DELTA(120) 단위다.
        // 양수가 위로 굴림이다.
        float delta { 0.0f };
        // 스크롤로 내용이 흐른 뒤 hover를 다시 판정할 때의 기준 시각이다.
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
    };

    // OS 파일 끌기가 이 표면에 들어왔다 (os-dragdrop-design.md).
    // 좌표는 그 표면의 client 물리 픽셀 — 포인터 이벤트와 같은 규약이다.
    // 경로 목록은 들어올 때 한 번 실리고, 끌리는 동안은 자리만 갱신된다.
    // 이 셋은 표시(수락 대상 강조)만 세운다 — 수락의 답과 놓기의 실행은
    // UI thread가 같은 tree에 직접 묻는다.
    struct file_drag_entered_event
    {
        float x { 0.0f };
        float y { 0.0f };
        std::vector<std::u8string> files {};
        std::u8string surface {};
    };

    struct file_drag_moved_event
    {
        float x { 0.0f };
        float y { 0.0f };
        std::u8string surface {};
    };

    // 끌기가 표면을 떠났거나 끝났다.
    // 놓기도 표시로는 떠남이다 — 실행은 UI thread가 이미 했다.
    struct file_drag_left_event
    {
        std::u8string surface {};
    };

    enum class key_code : std::uint32_t
    {
        none = 0,
        arrow_up,
        arrow_down,
        enter,
        f5,
        escape,
        // 텍스트 박스 편집용이다.
        // 초점을 가진 박스가 없으면 무시된다.
        arrow_left,
        arrow_right,
        home,
        end,
        delete_forward,
        // 글자 키는 **Ctrl이나 Alt가 눌린 동안에만** 만든다.
        // 그냥 치는 글자는 문자 입력(character_typed_event)의 것이다.
        key_a,
        key_c,
        key_v,
        key_x,
        key_y,
        key_z,
        // Ctrl+Insert는 복사, Shift+Insert는 붙여넣기, Shift+Delete는 잘라내기다.
        // 각 명령은 해당 Ctrl 또는 Shift 수정자와 함께 입력될 때 생성된다.
        insert,
        // 탐색·단축키의 기본 어휘다.
        tab,
        page_up,
        page_down,
        // space는 문자이면서 **누름**이다. 초점을 가진 컨트롤을 실행하는 키라
        // 글자 키와 달리 수정자 없이도 온다.
        //  - 텍스트 박스에 초점이 있으면 controller가 이 키를 흘려보내고
        //    문자(`character_typed_event`)만 먹는다. 같은 Space가 둘 다로 온다.
        space,
        f1,
        f2,
        f3,
        f4,
        f6,
        f7,
        f8,
        f9,
        f10,
        f11,
        f12,
        // 이름 붙지 않은 나머지 키의 대역이다.
        // platform 가상 키(Win32 VK_*)를 이 값 위에 얹어 나른다 —
        // 이름 키에 없는 단축키(Ctrl+S 등)를 앱이 받는 경로다.
        // 문자를 만드는 키는 글자 키와 같은 규칙(Ctrl·Alt 동안에만)으로 온다.
        first_platform_key = 0x1000,
    };

    // platform 가상 키를 key_code로/에서 옮긴다.
    // 값은 Win32 가상 키 코드다 (문자 키는 대문자 ASCII와 같다: 'S' == 0x53).
    [[nodiscard]] constexpr key_code platform_key_code(const std::uint32_t virtual_key) noexcept
    {
        return static_cast<key_code>(static_cast<std::uint32_t>(key_code::first_platform_key) + virtual_key);
    }

    // platform 대역이 아니면 0이다.
    [[nodiscard]] constexpr std::uint32_t platform_key_of(const key_code key) noexcept
    {
        const auto value { static_cast<std::uint32_t>(key) };
        const auto first { static_cast<std::uint32_t>(key_code::first_platform_key) };
        return value >= first ? value - first : 0u;
    }

    struct key_pressed_event
    {
        key_code key { key_code::none };
        bool control { false };
        bool shift { false };
        bool alt { false };
        // 누르고 있는 동안의 자동 반복이다.
        // 한 번만 반응할 단축키(창 전환 등)가 거른다.
        bool repeat { false };
        // UI thread가 게시 시점에 기록한 시각이다.
        // Tab이 초점을 텍스트 박스로 옮기면 이 값이 caret 깜빡임의 위상 기준이
        // 된다 — controller는 시계를 조회하지 않아 test가 결정적이다
        // (포인터 이벤트의 time과 같은 규칙이다).
        std::chrono::steady_clock::time_point time {};
        // 이 키를 나른 표면이다 (표면 표식의 통상 규칙 — 비면 주 창이다).
        //
        // **초점이 없을 때의 시작 표면**으로만 쓴다. Tab이 어느 tree에서 돌지,
        // Enter의 기본 버튼과 Esc의 가둠을 어느 tree에서 찾을지가 그것이다.
        //  - popup은 keyboard focus를 받지 못해 **앵커 창이 키를 나른다.** 그래서
        //    이 값이 초점의 표면과 다른 것이 popup에서는 정상이고, 검문으로 쓰면
        //    popup 안 텍스트 칸과 메뉴 탐색이 함께 죽는다. 논리 초점이 있으면
        //    그것이 이긴다 (key-surface-routing-design.md).
        std::u8string surface {};
    };

    // WM_CHAR가 만드는 문자 입력이다.
    // 텍스트 박스에 초점이 있을 때만 소비되고 그 밖에는 무시된다.
    // backspace는 U+0008로 온다.
    struct character_typed_event
    {
        char32_t character { 0 };
        // 문자가 들어온 시각이다 (키 이벤트의 `time`과 같은 규칙).
        //
        // 상호작용 상태 기계는 **시계를 조회하지 않는다** — 그래야 test가 시간을
        // 손으로 정할 수 있다. 그래서 "연달아 친 글자"처럼 시간이 필요한 판정은
        // 이 값으로만 잴 수 있다.
        std::chrono::steady_clock::time_point time {};
    };

    // 보조 기술이 논리 초점을 이 요소로 옮겨 달라고 청한다 (UIA의 SetFocus).
    // **좌표가 없는 유일한 입력 이벤트다** — 옮기는 것이 자리가 아니라 초점이라서다.
    // 그래서 이벤트가 요소를 이름으로 말하는 유일한 자리이기도 하다 (정체성 어휘가
    // `ui_element_id.h`로 나온 이유다 — accessibility-action-design.md).
    //  - 초점은 tree가 아니라 입력 상태라 UI thread가 직접 세울 수 없다. 실행
    //    (액션)은 표면이 그 자리에서 하고 초점만 이 길로 온다.
    struct access_focus_event
    {
        ui_element_id target {};
        std::u8string surface {};
        std::chrono::steady_clock::time_point time {};
    };

    using raw_input_event = std::variant<pointer_moved_event, pointer_pressed_event, pointer_released_event, pointer_left_event, surface_focus_lost_event, surface_focus_gained_event,
        mouse_wheel_event, file_drag_entered_event, file_drag_moved_event, file_drag_left_event, key_pressed_event, character_typed_event, access_focus_event>;

    // UI thread에서만 실행할 수 있는 창 명령이다.
    // 창 조작은 앱 상태가 아니므로 logic을 거치지 않는다.
    enum class ui_command
    {
        window_minimize,
        window_toggle_maximize,
        // 주 창을 테두리 없는 전체 화면으로 넣고 뺀다.
        // 최대화와 **다른 상태다**: 작업 표시줄 위까지 모니터를 덮고, 크기 조절
        // 가장자리도 캡션 끌기 띠도 그동안 없다. 앱은 이 명령만 내고, 어느 모니터를
        // 덮을지·나올 때 어디로 돌아갈지는 UI thread가 든다
        // (docs/concepts/window.md).
        //  - 주 창에만 듣는다. 보조 창은 배치 저장과 마찬가지로 대상이 아니다.
        window_toggle_fullscreen,
        window_close,
    };

    // UI thread에서 실행해야 하는 **앱 정의** 명령이다 (파일 dialog, 클립보드 밖의 shell 실행 등).
    // 라이브러리는 command 값을 해석하지 않고 앱이 등록한
    // handler (`app_ui_command_handler`)에 그대로 넘긴다.
    // 인자가 필요한 명령(외부 열기 경로 등)은 `argument`에 싣는다.
    struct app_ui_command
    {
        std::uint32_t command { 0 };
        std::u8string argument {};

        [[nodiscard]] bool operator==(const app_ui_command&) const noexcept = default;
    };

    // 텍스트 입력 대상의 불투명 식별자다.
    // 앱이 값을 정의하고 interaction policy가 element kind를 이 값으로 옮긴다.
    // 라이브러리는 값을 해석하지 않는다.
    enum class text_input_target : std::uint32_t
    {
        none = 0,
    };

    // 초점을 가진 텍스트 박스의 선택 글을 클립보드에 넣는다.
    // 글은 입력 thread가 tree에서 읽어 담고, 실제 클립보드 접근은 UI thread의 몫이다.
    struct clipboard_copy_request
    {
        std::u8string text {};

        [[nodiscard]] bool operator==(const clipboard_copy_request&) const noexcept = default;
    };

    // 클립보드의 글을 그 텍스트 박스에 넣으라는 요청이다.
    // UI thread가 클립보드를 읽어 대상 target의 insert 메시지를 앱 경계에서 만들어 logic에 보낸다.
    struct clipboard_paste_request
    {
        text_input_target target { text_input_target::none };

        [[nodiscard]] bool operator==(const clipboard_paste_request&) const noexcept = default;
    };

    // UI thread만 다룰 수 있는 클립보드 요청이다.
    using clipboard_request = std::variant<clipboard_copy_request, clipboard_paste_request>;

    // 지금 화면을 파일로 남긴다 (무손실 WebP).
    //
    // 클립보드 요청과 같은 갈래다 — UI thread만 할 수 있고 인자를 나른다. 앱
    // 상태가 아니므로 logic을 거치지 않는다.
    //  - 화면을 긁지 않고 그리기와 **같은 길**을 raster로 한 번 더 태운다. 창이
    //    가려져 있거나 최소화되어 있어도, 다른 desktop에 있어도 같은 그림이다.
    //  - 웹뷰는 합성이 우리 아래에 얹는 층이라 담기지 않는다.
    struct capture_request
    {
        // 남길 파일 경로다 (UTF-8). 이미 있으면 덮어쓴다.
        std::u8string path {};
        // 찍을 표면이다. 비면 주 창이다 (보조 창·popup은 자기 id를 준다).
        std::u8string surface {};

        [[nodiscard]] bool operator==(const capture_request&) const noexcept = default;
    };

    // element 액션과 interaction controller가 돌려주는 후속 조치다.
    // 액션은 상태를 직접 바꾸지 않고 이 메시지를 반환만 한다.
    // 앱 메시지는 `app_message`에 담겨 다니고 앱이 경계에서 복원한다.
    using input_action = std::variant<std::monostate, app_message, ui_command, app_ui_command, clipboard_copy_request, clipboard_paste_request, capture_request>;

    // intent 하나를 액션 값으로 싸는 도우미다.
    // `input_action { app_message { intent {} } }` 3중 중첩을 대신한다.
    template<typename intent_type>
    [[nodiscard]] input_action make_app_action(intent_type intent)
    {
        return input_action { app_message { std::move(intent) } };
    }

    // 휠 한 눈금이 움직이는 논리 픽셀이다.
    inline constexpr float input_wheel_scroll_step { 48.0f };
} // namespace luil

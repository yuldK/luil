#pragma once

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element_id.h"

#include <chrono>
#include <cstdint>
#include <optional>
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

    // 포인터를 낸 장치다 (touch-pen-input-design.md).
    // 끌어 스크롤하기와 길게 누르기는 터치에만 붙는다 — 장치를 추측하지 않으므로
    // 원격 연결이 터치를 마우스로 바꿔 보내면 그것은 마우스다.
    enum class pointer_device
    {
        mouse,
        touch,
        pen,
    };

    // Win32 메시지의 최소 복사다.
    // `HWND`와 lparam 원문은 담지 않는다.
    // time은 UI thread가 기록하며 더블 클릭·tooltip·길게 누르기 판정의 기준이다.
    // interaction controller는 이 값만 읽고 시계를 직접 조회하지 않아 test가 결정적이다.
    //
    // 포인터 이벤트의 surface는 이벤트가 난 표면이다.
    // 비어 있으면 주 창이고, 값은 popup id다 (`ui_popup::id`).
    // 좌표는 그 표면의 client 좌표라 표면의 tree에 그대로 hit한다.
    //
    // 장치 정보는 뒤에 둔다. 앞 필드만 채운 기존 초기화가 그대로 마우스를 뜻한다.
    //  - `pointer_id`는 접촉 하나의 수명 동안만 안정적이다. 마우스는 0이다.
    struct pointer_moved_event
    {
        float x { 0.0f };
        float y { 0.0f };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
        pointer_device device { pointer_device::mouse };
        std::uint32_t pointer_id { 0 };
        // 펜촉·손가락이 닿아 있는가. 펜의 비접촉 이동(hover)을 접촉과 가른다.
        // 마우스는 버튼 상태를 싣지 않으므로 늘 거짓이다.
        bool in_contact { false };
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
        pointer_device device { pointer_device::mouse };
        std::uint32_t pointer_id { 0 };
        // 누른 표면의 물리 픽셀 / 논리 픽셀 배율이다.
        // 터치 판정 거리는 논리 픽셀이라 이 값으로 나눠 잰다. 누른 동안 고정된다.
        float scale { 1.0f };
    };

    struct pointer_released_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
        pointer_device device { pointer_device::mouse };
        std::uint32_t pointer_id { 0 };
    };

    // 터치·펜 접촉이 정상적인 뗌 없이 끝났다 (캡처 상실·OS 취소·접촉 중 버튼 전환).
    // **뗌이 아니다.** 클릭·우클릭·drop을 실행하지 않고 그 접촉의 몸짓만 거둔다.
    //  - 마우스는 지금처럼 화면 밖 합성 뗌으로 거둔다 — 그 경로를 바꾸지 않는다.
    struct pointer_cancelled_event
    {
        pointer_device device { pointer_device::touch };
        std::uint32_t pointer_id { 0 };
        std::u8string surface {};
        std::chrono::steady_clock::time_point time {};
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
        pointer_device device { pointer_device::mouse };
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
        bool control { false };
        bool shift { false };
        bool horizontal { false };
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
        // 글자 키는 단축키 수정자와 함께일 때만 키 이벤트로 만든다.
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
        // 글자 단축키의 나머지 이름이다. 기존 이름 키의 값을 보존하려고 뒤에 둔다.
        key_b,
        key_d,
        key_e,
        key_f,
        key_g,
        key_h,
        key_i,
        key_j,
        key_k,
        key_l,
        key_m,
        key_n,
        key_o,
        key_p,
        key_q,
        key_r,
        key_s,
        key_t,
        key_u,
        key_w,
        key_0,
        key_1,
        key_2,
        key_3,
        key_4,
        key_5,
        key_6,
        key_7,
        key_8,
        key_9,
        // 이름 붙지 않은 나머지 키의 대역이다.
        // platform 가상 키(Win32 VK_*)를 이 값 위에 얹어 나른다 —
        // 이름 키에 없는 플랫폼 고유 단축키를 앱이 받는 경로다.
        // 문자를 만드는 키는 글자 키와 같은 규칙(단축키 수정자가 있을 때)으로 온다.
        zoom_in,
        zoom_out,
        first_platform_key = 0x1000,
    };

    // Win32의 영문자·숫자 VK는 ASCII와 같다. 공통 단축키는 이름 키로 바꾸고,
    // 종전 platform_key_code('S') 호출도 같은 값으로 이어 준다.
    [[nodiscard]] constexpr key_code alphanumeric_key_code(const std::uint32_t virtual_key) noexcept
    {
        constexpr key_code letters[] {
            key_code::key_a,
            key_code::key_b,
            key_code::key_c,
            key_code::key_d,
            key_code::key_e,
            key_code::key_f,
            key_code::key_g,
            key_code::key_h,
            key_code::key_i,
            key_code::key_j,
            key_code::key_k,
            key_code::key_l,
            key_code::key_m,
            key_code::key_n,
            key_code::key_o,
            key_code::key_p,
            key_code::key_q,
            key_code::key_r,
            key_code::key_s,
            key_code::key_t,
            key_code::key_u,
            key_code::key_v,
            key_code::key_w,
            key_code::key_x,
            key_code::key_y,
            key_code::key_z,
        };
        constexpr key_code digits[] {
            key_code::key_0,
            key_code::key_1,
            key_code::key_2,
            key_code::key_3,
            key_code::key_4,
            key_code::key_5,
            key_code::key_6,
            key_code::key_7,
            key_code::key_8,
            key_code::key_9,
        };
        if (virtual_key >= 'A' && virtual_key <= 'Z')
            return letters[virtual_key - 'A'];
        if (virtual_key >= '0' && virtual_key <= '9')
            return digits[virtual_key - '0'];
        return key_code::none;
    }

    // platform 가상 키를 key_code로/에서 옮긴다.
    // 영문자·숫자는 공통 이름 키다. 그 밖의 값은 Win32 가상 키 코드다.
    [[nodiscard]] constexpr key_code platform_key_code(const std::uint32_t virtual_key) noexcept
    {
        if (const key_code named { alphanumeric_key_code(virtual_key) }; named != key_code::none)
            return named;
        return static_cast<key_code>(static_cast<std::uint32_t>(key_code::first_platform_key) + virtual_key);
    }

    // 영문자·숫자 이름 키는 종전 VK 값을 돌려준다. 나머지 이름 키는 0이다.
    [[nodiscard]] constexpr std::uint32_t platform_key_of(const key_code key) noexcept
    {
        for (std::uint32_t virtual_key = 'A'; virtual_key <= 'Z'; ++virtual_key)
            if (alphanumeric_key_code(virtual_key) == key)
                return virtual_key;
        for (std::uint32_t virtual_key = '0'; virtual_key <= '9'; ++virtual_key)
            if (alphanumeric_key_code(virtual_key) == key)
                return virtual_key;
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
        // 물리 수정키와 편집 의미를 갈라 둔다. 비어 있으면 기존 Windows 규칙인
        // Control을 쓴다. 다른 플랫폼의 입력 뒷단은 각 역할을 명시한다:
        // 예를 들어 macOS는 Command를 주 단축키, Option을 낱말 이동으로 보낸다.
        std::optional<bool> primary_shortcut {};
        std::optional<bool> word_navigation {};
        // Windows 키·Command 키다.
        bool meta { false };

        // Shift를 뺀 수정자가 하나라도 눌렸는가. 눌렸으면 그 키는 앱 단축키라
        // 실행 키·기본 버튼·단계 키가 가로채지 않는다 — 물리 키와 역할 중 어느 쪽으로
        // 알려도 같은 판정이 되게 한곳에 둔다.
        [[nodiscard]] bool shortcut_modifier_down() const noexcept
        {
            return control || alt || meta || primary_shortcut_down();
        }

        [[nodiscard]] bool primary_shortcut_down() const noexcept
        {
            return primary_shortcut.value_or(control);
        }

        [[nodiscard]] bool word_navigation_down() const noexcept
        {
            return word_navigation.value_or(control);
        }
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
        mouse_wheel_event, file_drag_entered_event, file_drag_moved_event, file_drag_left_event, key_pressed_event, character_typed_event, access_focus_event, pointer_cancelled_event>;

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

    enum class record_command
    {
        start,
        stop,
    };

    // 화면을 움직이는 그림으로 남긴다 (애니메이션 WebP).
    //
    // `capture_request`의 여러 장짜리다. 그리는 길도 같다 — 매 frame을 raster로
    // 한 번 더 태워 모으고, 멈출 때 한 번에 인코드한다.
    //  - **상한을 두지 않는다.** 몇 장까지 모을지, 언제 멈출지는 부르는 쪽이 안다.
    //    인코더가 프레임 전부를 한 번에 받으므로 멈출 때까지 메모리에 쌓이고,
    //    한 장이 너비×높이×4바이트다. 오래 켜 두면 그만큼 든다.
    //  - 첫 frame의 크기가 캔버스 크기다. 녹화 중 창 크기가 바뀌면 그 frame들은
    //    담기지 않고, 멈출 때 몇 장을 흘렸는지 알린다.
    struct record_request
    {
        record_command command { record_command::start };
        // `start`일 때 남길 파일 경로다 (UTF-8). 이미 있으면 덮어쓴다.
        std::u8string path {};
        // 찍을 표면이다. 비면 주 창이다.
        std::u8string surface {};

        [[nodiscard]] bool operator==(const record_request&) const noexcept = default;
    };

    // element 액션과 interaction controller가 돌려주는 후속 조치다.
    // 액션은 상태를 직접 바꾸지 않고 이 메시지를 반환만 한다.
    // 앱 메시지는 `app_message`에 담겨 다니고 앱이 경계에서 복원한다.
    using input_action = std::variant<std::monostate, app_message, ui_command, app_ui_command, clipboard_copy_request, clipboard_paste_request, capture_request, record_request>;

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

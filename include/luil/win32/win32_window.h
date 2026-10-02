#pragma once

#include "luil/ui/caption_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/win32/app_host.h"
#include "luil/win32/renderer_policy.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil::win32 {
    // platform 커서의 불투명 핸들이다.
    // 값은 Win32 HCURSOR지만 공개 API는 그 타입을 드러내지 않는다.
    // 아래 load_* 도우미로만 만들며, 빈 핸들은 기본 매핑으로 물러선다.
    struct cursor_handle
    {
        void* value { nullptr };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != nullptr;
        }
    };

    // 시스템 표준 커서다.
    // `ui_cursor`의 기본 매핑에 없는 모양을 앱 정의 커서로 쓸 때 고른다.
    enum class system_cursor
    {
        arrow,
        hand,
        text,
        wait,
        not_allowed,
        resize_horizontal,
        resize_vertical,
        resize_all,
        cross,
        help,
    };

    // 시스템 표준 커서를 읽는다.
    // 실패하면 빈 핸들이다 (기본 매핑으로 물러선다).
    [[nodiscard]] cursor_handle load_system_cursor(system_cursor cursor) noexcept;

    // 실행 파일 resource의 커서를 읽는다.
    // 실패하면 빈 핸들이다.
    [[nodiscard]] cursor_handle load_cursor_resource(int resource_id) noexcept;

    // 창이 처음 뜰 화면 자리다.
    //
    // **물리 픽셀**이고 가상 화면 좌표다 — 왼쪽·위에 놓인 모니터에서는 음수다.
    // 크기(`initial_width`·`initial_height`)가 논리 96 DPI인 것과 단위가 다른데,
    // 그 비대칭이 이 타입의 요점이다.
    //  - 크기는 "얼마나 크게"라 어느 모니터에 뜨는지 알아야 배율을 곱할 수 있고,
    //    그래서 창을 만든 뒤 그 모니터의 배율로 다시 잡는다.
    //  - 자리는 "어느 점에"라 배율을 곱할 대상이 없다. 곱하면 오히려 사용자가
    //    가리킨 그 점이 아닌 곳으로 간다.
    struct window_position
    {
        int x { 0 };
        int y { 0 };
    };

    // "x,y"를 창 자리로 읽는다 (십진 정수 둘, 음수 가능).
    // 숫자가 아니거나 쉼표가 없으면 빈 값이다.
    //  - 예제 셋이 `--position=` 인자를 같은 방식으로 읽으므로 식이 세 벌이 되지
    //    않게 여기 둔다 (`parse_renderer_mode`와 같은 자리).
    [[nodiscard]] std::optional<window_position> parse_window_position(std::u8string_view text) noexcept;

    // 배치의 좌표에 크기를 더한 끝(오른쪽·아래)이 32-bit 화면 좌표 범위 안인가다.
    // `window_placement::valid()`는 크기가 양수인 것만 보므로, 손상된 저장값이
    // 화면 사각형의 int 덧셈에서 넘치는 것은 적용하는 쪽이 이것으로 먼저 거른다.
    [[nodiscard]] bool placement_in_screen_range(const window_placement& placement) noexcept;

    // 시작에 실패했을 때(리소스 검증·창 생성) 사용자에게 내는 메시지 상자다.
    // 창도 delegate도 서기 전이라 값으로 받는다.
    //  - `title`이 비어 있으면 "luil startup error"다.
    //  - `preface`가 비어 있지 않으면 진단 글 앞에 한 문단으로 둔다 — 앱 이름으로
    //    "…을 시작할 수 없습니다" 같은 안내를 쓰는 자리다. 진단 글은 그대로 뒤따른다.
    // 디버거 출력(`OutputDebugString`)은 언제나 진단 글만이며, smoke test에서는
    // 상자를 띄우지 않는다.
    struct startup_error_config
    {
        std::u8string title {};
        std::u8string preface {};
    };

    // 창의 겉모습과 정책이다.
    // 값만 바꾸면 되도록 한곳에 모아 둔다 (논리 96 DPI 기준 px).
    struct window_config
    {
        std::wstring class_name { L"Luil.Window" };
        // caption의 제목·아이콘·툴팁이다.
        // Win32 창 제목도 title에서 만든다.
        caption_config caption {};
        int initial_width { 1280 };
        int initial_height { 720 };
        // 비어 있으면 OS가 자리를 정한다 (`CW_USEDEFAULT`) — 지금까지의 동작이다.
        //  - 없음을 말하는 법은 이 `optional` 하나다. x·y를 따로 optional로 두면
        //    "x만 정한 창"이라는 불가능한 조합이 생긴다.
        std::optional<window_position> initial_position {};
        // 사용자가 이보다 작게 줄일 수 없는 client 영역 크기다.
        int minimum_client_width { 480 };
        int minimum_client_height { 320 };
        // 창 가장자리에서 크기 조절로 잡히는 두께다.
        // 좁을수록 가장자리에 붙은 UI(스크롤 막대)를 잡기 쉽다.
        // 모서리는 가장자리보다 넓게 두어야 잡기 쉽다.
        int resize_border_thickness { 4 };
        int resize_corner_thickness { 10 };
        // 실행 파일 resource의 창 아이콘 id다.
        // 0이면 시스템 기본 아이콘이다.
        int icon_resource { 0 };
        renderer_mode renderer { renderer_mode::automatic };
        // Direct3D를 **만들 때** 실패시킨다. 그러면 스왑체인이 아예 없는 채로
        // 처음부터 CPU로 시작한다.
        bool simulate_direct3d_failure { false };
        // Direct3D를 **성공적으로 그린 뒤** 실패시킨다 (0이면 하지 않는다).
        // 이 frame 수만큼 그리고 나서 다음 `render`가 실패해 CPU로 물러선다.
        //
        // 위의 생성 시점 실패와 갈라 두는 이유: 두 경로가 화면에서 하는 일이
        // 다르다. 생성 시점 실패는 창이 제시에 붙기 전이라 CPU가 곧바로 그리지만,
        // 런타임 물러섬은 이미 제시에 붙은 창을 **놓고** 나서야 CPU가 화면에 닿는다.
        // 그 순서가 틀리면 오류 하나 없이 화면이 마지막 frame에 얼어붙으므로,
        // 재현할 길이 있어야 한다.
        int simulate_direct3d_loss_after_frames { 0 };
        // 한 frame만 그리고 끝낸다 (renderer smoke test).
        // host를 조립하지 않는다.
        // 위의 런타임 손실을 주입하면 그 손실이 실제로 오는 frame까지 그린다.
        bool smoke_test { false };
        // 시작 실패 메시지 상자의 제목과 앞글이다.
        startup_error_config startup_error {};
        // 주 창·보조 창이 OS 파일 끌기를 받는다 (표면마다 IDropTarget 하나).
        // 놓인 자리의 drop 대상 element가 받고, 없으면 delegate의
        // `on_file_dropped`가 본다 (os-dragdrop-design.md).
        bool accept_file_drop { false };
        // `ui_cursor`를 실제 커서로 옮긴다.
        // 비어 있으면 라이브러리의 기본 매핑(시스템 커서)을 쓴다.
        // 앱이 정의한 모양(`application_cursor`)이나 자기 커서 리소스를 여기서 준다
        // (`load_system_cursor`·`load_cursor_resource`로 얻는다).
        //  - 빈 핸들을 돌려주면 그 값도 기본 매핑으로 물러선다.
        std::function<cursor_handle(ui_cursor)> resolve_cursor {};
        // 터치 몸짓의 시작 설정이다 (touch-pen-input-design.md).
        // 잘못된 값(`valid_touch_gesture_config`)이면 창이 시작하지 않고 시작 실패로 알린다.
        // 실행 중에는 `app_host::set_touch_gesture_config`로 바꾼다.
        touch_gesture_config touch {};
    };

    // 앱이 UI thread에 꽂는 훅이다.
    // 모든 메서드는 UI thread에서 불리고 기본 구현은 "아무 일도 하지 않음"이다.
    class window_delegate
    {
    public:
        window_delegate() = default;
        window_delegate(const window_delegate&) = delete;
        window_delegate(window_delegate&&) = delete;
        window_delegate& operator=(const window_delegate&) = delete;
        window_delegate& operator=(window_delegate&&) = delete;
        virtual ~window_delegate() = default;

        // 창과 host가 준비된 직후다 (메시지 루프 전).
        // 시작 메시지(글꼴 목록, 시작 문서 열기 등)를 여기서 게시한다.
        virtual void on_started(app_host& host)
        {
            static_cast<void>(host);
        }

        // input thread가 요청한 앱 UI 명령이다 (파일 dialog, shell 실행 등).
        virtual void execute_app_ui_command(app_host& host, const app_ui_command& command)
        {
            static_cast<void>(host);
            static_cast<void>(command);
        }

        // 창 크기·DPI가 바뀌었다.
        // 반환 메시지는 app inbox로 간다.
        // 빈 메시지는 게시하지 않는다.
        [[nodiscard]] virtual app_message make_window_metrics_message(float width, float height, float scale)
        {
            static_cast<void>(width);
            static_cast<void>(height);
            static_cast<void>(scale);
            return {};
        }

        // 창 배치를 보고한다 (이동·크기 조절 끝, 최대화 전환, 종료 직전).
        // 빈 메시지는 게시하지 않는다.
        [[nodiscard]] virtual app_message make_window_placement_message(const window_placement& placement)
        {
            static_cast<void>(placement);
            return {};
        }

        // 드롭된 파일 경로다 (절대 경로 아님 — 셸이 준 그대로).
        // true면 소비했다는 뜻이라 다음 파일을 보지 않는다.
        //  - 수락한 drop 대상 element가 없는 드롭의 물러섬이다.
        //    자리가 뜻을 갖는 드롭은 element의 `drop_target`이 좌표와 함께 받는다.
        [[nodiscard]] virtual bool on_file_dropped(app_host& host, const std::u8string& path)
        {
            static_cast<void>(host);
            static_cast<void>(path);
            return false;
        }
    };

    // 창을 만들고 메시지 루프를 돈다.
    // driver가 null이면 smoke 모드처럼 host 없이 caption만 있는 화면을 그린다.
    // 반환값은 프로세스 종료 코드다.
    //  - smoke에서 Direct3D가 없으면 77이다.
    //  - CTest의 SKIP_RETURN_CODE와 짝이다.
    struct window_environment
    {
        logic_driver* driver { nullptr };
        interaction_policy* policy { nullptr };
        window_delegate* delegate { nullptr };
    };

    // 실행 module은 라이브러리가 스스로 얻는다 (Win32 HINSTANCE를 받지 않는다).
    int run_application_window(const window_config& config, const window_environment& environment);

    // wWinMain 앞머리의 공통 준비다.
    // DPI 인지는 창을 만들기 전에 한 번 켠다.
    void enable_per_monitor_dpi_awareness() noexcept;

    // COM STA와 OLE를 잡는 RAII다 (파일 dialog·TSF·OS 파일 끌기·접근성이 요구).
    // 실패해도 앱은 뜨되 해당 기능이 조용히 꺼진다.
    //  - 접근성만은 조용히 꺼지지 않는다. UIA provider는 모든 호출을 창 thread로
    //    모으는 것(`ProviderOptions_UseComThreading`)을 전제로 표면 상태를 잠그지
    //    않는데, 그 전제가 곧 이 STA다
    //    (accessibility-action-design.md).
    class com_sta_scope
    {
    public:
        com_sta_scope() noexcept;
        com_sta_scope(const com_sta_scope&) = delete;
        com_sta_scope(com_sta_scope&&) = delete;
        com_sta_scope& operator=(const com_sta_scope&) = delete;
        com_sta_scope& operator=(com_sta_scope&&) = delete;
        ~com_sta_scope();

        [[nodiscard]] bool succeeded() const noexcept;

    private:
        bool succeeded_ { false };
    };

    // 명령행을 UTF-8 인자 목록으로 바꾼다.
    // 변환 실패면 nullopt다.
    [[nodiscard]] std::optional<std::vector<std::u8string>> command_line_arguments();
} // namespace luil::win32

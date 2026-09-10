#pragma once

#include "luil/messaging/channel.h"
#include "luil/messaging/latest_slot.h"
#include "luil/theme/appearance.h"
#include "luil/ui/app_message.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/webview.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luil::win32 {
    // 저장·복원되는 창 배치다.
    struct window_placement
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };
        bool maximized { false };
        // 창이 화면을 덮고 있는가다 (주 창 전용 — 배치 자체가 주 창의 것이다).
        //
        // **참이면 나머지 값은 지금 창의 사각형이 아니라 "돌아갈 자리"다.**
        // 전체 화면 창의 사각형은 모니터가 정하므로 적어 둘 것이 없고, 적어 둘
        // 가치가 있는 것은 나올 때 되돌아갈 배치뿐이다. 그래서 이 값을 그대로
        // 저장했다가 그대로 다시 넣으면 **전체 화면으로 열리면서 돌아갈 자리도 함께
        // 산다** — 둘을 따로 저장하지 않아도 되는 이유다.
        //  - `maximized`는 뜻이 그대로이고 이 값과 **겹쳐 쓴다.** 둘 다 참이면
        //    "지금은 전체 화면, 나오면 최대화된 창"이다. 최대화 상태에서 전체
        //    화면에 들어간 사용자가 나왔을 때 창이 복원 크기로 앉지 않는 길이
        //    그것이다.
        //  - 전체 화면은 테두리 없는 창이다 (독점 전체 화면은 합성 스왑체인에
        //    설 수 없다 — docs/concepts/window.md).
        //  - **꼬리에 둔다.** 이 집합체를 위치 초기화로 짓는 앱이 있어
        //    (examples/demo_main.cpp) 가운데 끼우면 그 자리들의 뜻이 조용히 어긋난다.
        bool fullscreen { false };

        [[nodiscard]] bool valid() const noexcept
        {
            return width > 0 && height > 0;
        }

        [[nodiscard]] bool operator==(const window_placement&) const noexcept = default;
    };

    // popup이 닫힐 만한 계기다.
    // UI thread가 계기를 감지해 `ui_popup::dismiss`에 그대로 넘긴다 —
    // 무엇에 닫히고 무엇에 남을지는 앱이 계기를 보고 정한다.
    enum class popup_dismiss_reason
    {
        // popup 밖을 눌렀다. 캡션·테두리를 잡은 것도 이것이다.
        pointer_press_outside,
        // 앵커가 있던 내용이 휠로 스크롤됐다 — 닻이 움직인다.
        wheel_scrolled,
        // Esc를 눌렀다.
        escape_key,
        // 창 하나가 움직였다. 남는 popup은 자기 앵커를 따라 함께 옮겨진다.
        surface_moved,
        // 창 하나의 크기가 바뀌었다.
        surface_resized,
        // 창 활성이 바뀌었다 (alt-tab, 다른 창 클릭).
        activation_changed,
    };

    // 주 창 위에 띄우는 가벼운 popup 하나다 (메뉴·드롭다운·긴 tooltip).
    // 열림·닫힘은 앱 상태다: frame에 실으면 창이 생기고 빼면 사라진다.
    // 자리는 앵커 표면 client 좌표의 논리 픽셀이며, 화면 좌표 변환과 모니터 경계
    // 다듬기는 UI thread가 한다 (앱은 모니터 배치를 모른다).
    // tree는 popup 창의 (0,0)에서 시작하는 분리 tree다
    // (popup-overlay-design.md).
    struct ui_popup
    {
        // 같은 frame의 popup을 구분하는 앱 정의 키다.
        std::u8string id {};
        // 이 popup이 붙는 표면이다 (`ui_window::id`와 같은 이름 공간).
        // 비어 있으면 주 창이다.
        // 자리·소유자·배율이 전부 이 표면을 따르므로, 보조 창 안에서 연
        // 메뉴는 그 창 기준 자리에 그 창 위로 뜬다
        // (popup-anchor-design.md).
        //  - 앵커 표면이 아직 없는 frame에서는 popup을 만들지 않는다.
        std::u8string anchor {};
        float x { 0.0f };
        float y { 0.0f };
        // 크기가 0 이하면 popup을 만들지 않는다.
        float width { 0.0f };
        float height { 0.0f };
        std::shared_ptr<const ui_tree> tree {};
        // 닫힘 계기에서 낼 메시지다. 어느 계기인지는 인자가 알려 준다.
        // UI thread는 계기를 감지해 이 메시지만 내고, popup을 빼는 것은 앱이다.
        // 없으면 계기를 감지해도 아무것도 내지 않는다 (popup이 계속 남는다).
        //  - **빈 액션은 "이 계기에는 닫지 않는다"는 뜻이다.** 검색 칸이 있는
        //    popup은 창 이동·활성 전환에 남기고 메뉴는 닫는 식으로 앱이 계기마다
        //    가른다.
        //  - 어느 popup도 닫자고 하지 않은 Esc는 삼켜지지 않고 앱의 키 라우팅으로
        //    간다 — 남기로 한 popup이 Esc를 자기 뜻대로 쓰는 길이다.
        std::function<input_action(popup_dismiss_reason)> dismiss {};
        // 표면 둘레에 1px 테두리를 긋는다 (`tooltip_border`).
        // popup은 다른 화면 위에 뜨는 판이라 경계가 있어야 아래 화면과 갈린다 — 그래서
        // 기본이 참이다. 자기 테두리를 긋는 tree(메뉴)는 같은 자리를 다시 긋는 것이라
        // 해가 없고, 경계를 일부러 지우는 popup(꼬리 달린 말풍선)만 끈다.
        //  - 그림자는 여기 없다. popup은 자기 창이라 그림자를 밖으로 드리울 자리가 없고,
        //    그것은 창 클래스의 drop shadow가 맡는다 (popup-overlay-design.md).
        bool border { true };
    };

    // 주 창이 소유하는 보조 top-level 창 하나다 (도구 창·문서 창).
    // 열림·닫힘은 popup처럼 앱 상태다: frame에 실으면 창이 생기고 빼면 사라진다.
    // 자기 캡션(custom caption)·테두리·DPI를 갖고 화면 어디로든 움직인다
    // (multi-window-design.md).
    // tree는 그 창 client의 (0,0)에서 시작하는 분리 tree이고,
    // 주 창처럼 caption_element를 tree 맨 위에 앱이 담는다.
    struct ui_window
    {
        // 표면 id다. popup id와 같은 이름 공간을 쓴다.
        std::u8string id {};
        // custom caption의 제목·아이콘·툴팁이다.
        // 비클라이언트 hit test가 이 metrics를 쓴다.
        caption_config caption {};
        // 초기 배치다 (논리 픽셀). x·y는 주 창 client 기준이다.
        // 창을 만들 때 한 번만 쓰고, 이후의 이동·크기 조절은 사용자의 것이다.
        float x { 0.0f };
        float y { 0.0f };
        // client 크기다. 0 이하면 창을 만들지 않는다.
        float width { 0.0f };
        float height { 0.0f };
        // 사용자가 이보다 작게 줄일 수 없는 client 크기다.
        float minimum_width { 200.0f };
        float minimum_height { 120.0f };
        std::shared_ptr<const ui_tree> tree {};
        // 크기·배율이 정해지거나 바뀔 때 게시할 메시지다 (생성 직후·크기 조절·DPI 변경).
        // 앱은 이것으로 그 창의 tree를 다시 배치한다.
        // 비어 있으면 알리지 않는다 (고정 배치 창).
        std::function<app_message(float width, float height, float scale)> metrics {};
        // 닫힘 요청(캡션 close, Alt+F4)에서 낼 메시지다.
        // UI thread는 이 메시지만 내고, 창을 없애는 것은 앱이 목록에서 빼는 것이다.
        // 비어 있으면 닫힘 요청을 무시한다 (앱이 다른 경로로만 닫는 창).
        std::function<input_action()> close {};
    };

    // logic thread가 게시하고 UI thread가 읽는 한 frame의 상태다.
    // 프레임워크가 아는 값만 담으며, tree 밖의 앱 데이터는 여기에 싣지 않는다.
    //  - 필요하면 앱이 driver 구현에서 따로 공유한다.
    struct ui_frame
    {
        std::shared_ptr<const ui_tree> tree {};
        // 창 경계를 넘을 수 있는 popup들이다.
        // 뒤에 있는 것이 위에 온다.
        std::vector<ui_popup> popups {};
        // 주 창 곁의 보조 top-level 창들이다.
        std::vector<ui_window> windows {};
        // 표면들에 얹힌 웹뷰들이다.
        // popup·보조 창처럼 실으면 서고 빼면 사라진다. **자리는 여기 없다** —
        // tree의 자리표(`webview_element`)가 정한다 (webview.h에 이유가 있다).
        std::vector<ui_webview> webviews {};
        // 외양·글꼴의 유효 선호다.
        // 실제 팔레트는 UI thread가 OS 상태(고대비·밝은 모드)와 함께 해석한다.
        appearance_settings appearance {};
        font_settings fonts {};
        // 창 배치 적용 요청이다.
        // revision이 바뀐 경우에만 UI thread가 적용한다.
        std::uint64_t window_placement_revision { 0 };
        std::optional<window_placement> window_placement_request {};
        // 시간에 따라 변하는 내용(경과 시간 표시 등)의 다시 그리기는 frame이 아니라
        // 그 element가 `ui_element::next_update`로 예고한다.
    };

    // 앱이 logic thread에 꽂는 구동기다.
    // 모든 메서드는 app_host의 logic thread에서 불린다 (shutdown_completed·
    // make_close_message·cancel은 UI thread에서도 불리므로 thread-safe해야 한다).
    //
    // 예외는 오류 전달 수단이 아니다. 콜백이 던지면 logic thread가 그 예외를
    // 삼키고 `app_host::faulted()`에 기록한 뒤 멈춘다 — frame 게시와 메시지
    // 처리가 함께 멎으므로, 오류는 값(메시지·frame 상태)으로 나른다.
    class logic_driver
    {
    public:
        logic_driver() = default;
        logic_driver(const logic_driver&) = delete;
        logic_driver(logic_driver&&) = delete;
        logic_driver& operator=(const logic_driver&) = delete;
        logic_driver& operator=(logic_driver&&) = delete;
        virtual ~logic_driver() = default;

        // logic thread 시작 직후 한 번이다.
        // 초기 상태 로드를 여기서 제출한다.
        virtual void start()
        {}

        // app inbox 메시지 1건을 처리한다.
        virtual void handle(app_message message) = 0;

        // 처리 후 게시할 frame이다.
        // tree를 포함해 불변 값으로 만든다.
        [[nodiscard]] virtual std::shared_ptr<const ui_frame> make_frame() = 0;

        // 종료가 시작됐다는 알림이다.
        // shutdown()이 종료 신호를 게시하기 직전에 UI thread에서 부른다.
        // 오래 걸리는 handle·tick·make_frame이 이를 보고 일찍 돌아와야
        // 종료 예산 안에 logic thread가 빠져나온다. 짧은 콜백만 쓰면 비워 둔다.
        virtual void cancel() noexcept
        {}

        // 종료 신호 메시지다.
        // shutdown()이 app inbox에 넣고, 앱 logic은 이를 처리하며 종료 저장을 제출한다.
        [[nodiscard]] virtual app_message make_close_message() = 0;

        // 종료 신호를 처리해 종료 저장까지 내보냈는지다 (UI thread가 조회).
        [[nodiscard]] virtual bool shutdown_completed() const = 0;

        // 종료 순서 중 worker pool을 멈출 시점이다.
        // worker가 없으면 비워 둔다.
        virtual void stop_workers()
        {}

        // 시간이 흘러야 바뀌는 logic 상태(토스트 만료·진행률·시간 제한)의 예고다.
        // frame 게시 뒤마다 물어, 답한 시각이 되면 메시지가 없어도 `tick`이
        // 불리고 frame이 다시 게시된다. 답이 없으면 시간 경로는 완전히 잠잔다.
        //  - `ui_element::next_update`와 같은 규칙이되, 그쪽은 그림만 다시
        //    그리고 이쪽은 logic 상태가 실제로 바뀐다.
        // 앱이 스레드를 만들지 않고도 "3초 뒤 지운다"를 표현하는 경로다.
        [[nodiscard]] virtual std::optional<std::chrono::steady_clock::time_point> next_tick()
        {
            return std::nullopt;
        }

        // `next_tick`이 예고한 시각이 지나면 logic thread에서 불린다.
        // 만료 처리를 하고 나면 새 frame이 게시된다.
        virtual void tick(std::chrono::steady_clock::time_point now)
        {
            static_cast<void>(now);
        }
    };

    // 스레드 3종(UI는 호출자)·채널·slot의 조립체다.
    // UI thread(창)는 이 객체를 통해서만 나머지 스레드와 통신한다.
    //
    // wake 신호가 비어 있으면 게시를 생략한다.
    // 창 없는 조립 test가 이 경로를 쓴다.
    class app_host
    {
    public:
        // UI thread를 깨우는 신호들이다.
        // platform 창이 자기 메시지 루프에 맞는 구현을 꽂는다
        // (Win32 창은 PostMessage — 공개 API는 그 타입을 드러내지 않는다).
        // logic·input thread에서 불리므로 구현은 thread-safe해야 하고,
        // 신호만 나른다 — 내용(명령·요청)은 내부 큐가 나른다.
        struct wake_signals
        {
            // frame·상호작용 상태가 게시됐다 (다시 그릴 때다).
            // 전달에 성공했는지를 돌려준다 (창 메시지 큐 포화 같은 실패).
            // 실패를 알리면 slot이 다음 게시에서 다시 신호한다 — 한 번의
            // 실패가 영구적인 미갱신으로 굳지 않는 길이다.
            std::function<bool()> snapshot {};
            // input thread가 요청한 창 명령(`ui_command`)이 큐에 들어갔다.
            // 내용은 `take_ui_commands()`가 나른다 — 신호가 유실돼도 명령은
            // 큐에 남아 다음 신호에서 실행된다.
            std::function<void()> ui_command {};
            // 앱 UI 명령이 큐에 들어갔다.
            std::function<void()> app_ui_command {};
            // 클립보드 요청이 큐에 들어갔다.
            std::function<void()> clipboard {};
        };

        struct config
        {
            wake_signals wake {};
            interaction_config interaction {};
        };

        app_host(config configuration, logic_driver& driver, interaction_policy* policy);
        app_host(const app_host&) = delete;
        app_host(app_host&&) = delete;
        app_host& operator=(const app_host&) = delete;
        app_host& operator=(app_host&&) = delete;
        ~app_host();

        // 정해진 순서로 스레드를 정리한다: 취소 알림 → 종료 신호 → logic의
        // 처리 확인 → worker 정지 → logic join → input join.
        // 게시와 처리 대기는 3초 예산을 나눠 쓰고, inbox를 닫은 뒤에는 logic이
        // 실제로 빠져나오는지 별도 상한 안에서 확인한 뒤에만 join한다.
        // cancel·close에도 나오지 않는 logic은 회복 불능이다 — 보이지 않는
        // 무기한 hang 대신 fail-fast한다 (세션 종료 유예도 이 상한들이 지킨다).
        // 멱등이며 UI thread에서 호출한다.
        void shutdown() noexcept;

        // logic 또는 input thread가 예외로 멈췄는지다 (임의 스레드에서 조회 가능).
        // 참이면 새 frame과 입력 처리가 더는 없다 — 창을 닫고 종료하는 것이 맞다.
        [[nodiscard]] bool faulted() const noexcept;

        // UI thread 전용 진입점이다 (창 프로시저가 받은 입력을 그대로 넣는다).
        // 입력 inbox는 drop_oldest라 밀리면 오래된 것부터 버려진다.
        void post_raw_input(raw_input_event event) noexcept;
        // **어느 thread에서 불러도 된다.** app inbox는 MPSC라 넣기가 thread-safe하고
        // 막히지 않는다 — 라이브러리의 input thread가 이미 UI thread가 아닌 곳에서
        // 같은 inbox에 넣고 있고, `http_client_config::deliver`도 client가 든
        // thread에서 이 문으로 답을 넘긴다 (http-client-design.md).
        //  - 포화하면 조용히 버려진다 (reject_newest). 유실 여부는
        //    `app_inbox_statistics`가 답한다.
        void post_app_message(app_message message) noexcept;
        // 아래 셋도 UI thread 전용이다 — 마지막으로 본 판을 멤버에 들고 있어서다.
        // 마지막으로 게시된 frame이다.
        // 새 것이 없으면 이전 값이다.
        [[nodiscard]] std::shared_ptr<const ui_frame> acquire_frame();
        // 마지막으로 게시된 ui tree다.
        // 그리기와 caption 버튼의 동기 실행이 쓴다.
        [[nodiscard]] std::shared_ptr<const ui_tree> acquire_ui_tree();
        // input thread가 게시한 최신 상호작용 상태다.
        // hover·tooltip 그리기가 쓴다.
        [[nodiscard]] interaction_snapshot acquire_interaction();
        // input thread가 큐에 넣은 창 명령을 모두 꺼낸다 (UI thread 전용).
        [[nodiscard]] std::vector<ui_command> take_ui_commands();
        // input thread가 큐에 넣은 앱 UI 명령을 모두 꺼낸다 (UI thread 전용).
        [[nodiscard]] std::vector<app_ui_command> take_app_ui_commands();
        // input thread가 큐에 넣은 클립보드 요청을 모두 꺼낸다 (UI thread 전용).
        [[nodiscard]] std::vector<clipboard_request> take_clipboard_requests();
        // app inbox의 게시·유실 통계다 (임의 스레드에서 조회 가능).
        // reject_newest 정책이라 포화 중의 게시는 조용히 버려진다
        // (의도된 한계 — docs/concepts/input-pump.md). 유실 여부는 rejected로 관찰한다.
        [[nodiscard]] messaging::channel_statistics app_inbox_statistics() const;

    private:
        void logic_thread_main();
        void logic_loop();
        void publish_frame();
        // logic이 종료 신호를 처리할 때까지 기다린다.
        // 한계를 넘으면 포기한다.
        void wait_for_logic_shutdown() noexcept;
        // 닫힌 inbox를 소진한 logic thread가 진입 함수를 벗어날 때까지 기다린다.
        // 한계를 넘으면 포기한다 — 판단은 shutdown()이 한다.
        void wait_for_logic_exit() noexcept;

        struct assembly;
        std::unique_ptr<assembly> assembly_ {};

        logic_driver& driver_;
        std::shared_ptr<const ui_frame> current_frame_ {};
        std::uint64_t seen_frame_version_ { 0 };
        std::shared_ptr<const ui_tree> current_tree_ {};
        std::uint64_t seen_tree_version_ { 0 };
        interaction_snapshot current_interaction_ {};
        std::uint64_t seen_interaction_version_ { 0 };
        bool shut_down_ { false };
    };
} // namespace luil::win32

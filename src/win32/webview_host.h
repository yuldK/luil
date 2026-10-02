#pragma once

#include "luil/app/webview.h"
#include "luil/theme/ui_theme.h"
#include "win32/surface_input.h"
#include "win32/webview_layout.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct IDCompositionDevice;
struct IDCompositionVisual;
// 완료 콜백이 이 둘을 나른다. WebView2.h는 이것을 실제로 부르는 자리에서만 든다
// (2.9 MB 헤더라 표면 계층 전체가 그것을 다시 파싱할 이유가 없다).
struct ICoreWebView2Environment;
struct ICoreWebView2CompositionController;

namespace luil::win32 {
    // 웹뷰와 WebView2 환경을 UI 스레드에서 소유한다.
    // WebView2 호출과 비동기 생성 완료 콜백은 생성한 STA에서 실행한다.
    // 환경은 user data folder마다 하나이며 폴더가 다르면 환경도 분리한다.
    // 각 웹뷰의 parentWindow는 앵커 표면의 창이다.
    // 표면 목록을 아는 호출자가 앵커를 HWND로 해석하며 host는 웹뷰 수명을 관리한다.
    // 앵커 표면이 없는 frame의 웹뷰는 전달 목록에서 제외한다.
    struct webview_target
    {
        const ui_webview* source { nullptr };
        HWND window { nullptr };
    };

    // 웹뷰가 초점에 대해 알려 오는 것이다.
    //
    // **Win32 초점과 논리 초점은 다른 이야기다.** 웹뷰가 창의 초점을 가져가는 것은
    // `window_surface`가 `WM_KILLFOCUS`로 알고, 이쪽은 그 뒤에 오는 통지로
    // **우리 tree의 초점 테와 캐럿이 가리키는 자리**를 맞춘다.
    enum class webview_focus_signal
    {
        // 웹뷰가 초점을 가졌다. 논리 초점을 자리표로 옮긴다.
        //  - 클릭 진입은 raw input이 우리에게 오지 않으므로, 이것을 하지 않으면
        //    초점 테가 이전 자리에 남는다.
        entered,
        // 웹뷰가 초점을 놓았다.
        left,
        // 가둠에서 나가자는 뜻이다 (`Ctrl+Tab`). 앞으로 간다.
        leave_forward,
        // 같은 것의 역방향이다 (`Ctrl+Shift+Tab`).
        leave_backward,
        // 웹뷰 안에서 Esc를 눌렀다. 가둠의 `dismiss_action`과 같은 자리다.
        dismiss,
    };

    // 웹뷰를 끈 구성(LUIL_ENABLE_WEBVIEW=OFF)은 같은 인터페이스의 stub
    // (webview_host_stub.cpp)을 대신 컴파일한다 — 아무것도 세우지 않고 placeholder만
    // 남는다. 창 계층은 어느 쪽인지 묻지 않는다.
    class webview_host
    {
    public:
        // 웹뷰가 낸 일을 앱으로 나르는 배출구다. 비어 있으면 알리지 않는다.
        //  - `http_client_config::deliver`와 같은 자리다: 사건은 입력이 아니라
        //    앱 메시지이고, 받는 곳은 logic thread다.
        void set_deliver(std::function<void(app_message)> deliver);

        // 초점 신호를 창으로 나르는 자리다.
        // 무엇을 할지는 창이 정한다 — host는 tree도 초점 순서도 모른다.
        void set_focus_reporter(std::function<void(const std::u8string& id, const std::u8string& anchor, webview_focus_signal signal)> reporter);

        // 페이지가 자기 배경을 정하기 전에 보이는 바닥색이다 (불투명으로 깐다).
        // 창이 매 frame 팔레트의 창 바탕을 준다. 같은 값이면 아무 일도 하지 않고,
        // 바뀌면 서 있는 웹뷰 전부와 앞으로 설 웹뷰에 적용한다.
        void set_default_background(ui_color color);

        // 생성자도 .cpp에 둔다 — 헤더에서 만들면 unwind 경로가 `entry`의 소멸을
        // 요구하는데 그 타입은 여기서 불완전하다.
        webview_host();
        webview_host(const webview_host&) = delete;
        webview_host(webview_host&&) = delete;
        webview_host& operator=(const webview_host&) = delete;
        webview_host& operator=(webview_host&&) = delete;
        ~webview_host();

        // frame이 실은 목록과 살아 있는 것을 대조해 만들고 없앤다.
        //
        // 만들기는 **비동기**라 이 호출이 돌아온 뒤에도 한동안 서지 않는다. 그동안
        // 자리표의 placeholder가 그 자리를 지킨다 — "웹뷰가 섰는가"를 따로 묻지
        // 않아도 되는 이유다 (webview_element.h).
        //  - 만들다 실패한 id는 기억해 다음 frame에 다시 시도하지 않는다. 앱이
        //    목록에서 뺐다가 다시 실으면 그때 새로 시도한다 (`failed_popups_`와 같은 규칙).
        void synchronize(std::span<const webview_target> wanted, IDCompositionDevice* composition);

        // 이 웹뷰의 자리를 적용한다. 없는 id는 아무 일도 하지 않는다.
        //  - `underlay`는 그 표면의 렌더러가 내준 자리다. nullptr이면(CPU 백엔드)
        //    웹뷰를 감춘다 — 합성이 없으면 설 자리도 없다.
        void apply_layout(const std::u8string& id, const webview_layout& layout, IDCompositionVisual* underlay);

        // 이 웹뷰가 지금 화면에 그려지고 있는가.
        // 거짓이면 그 자리에 구멍을 뚫지 않는다 — 뚫으면 바탕 화면이 비친다.
        [[nodiscard]] bool standing(const std::u8string& id) const noexcept;

        // 이 웹뷰로 키보드 초점을 넘긴다 (Tab이 자리표에 닿았을 때).
        // 넘겼으면 참이다.
        [[nodiscard]] bool move_focus_in(const std::u8string& id, bool backward);

        // 이 표면의 웹뷰 중 그 자리를 가진 것에 포인터 메시지를 넘긴다.
        // 넘겼으면 참 — 그러면 우리 tree는 그 포인터를 보지 않는다.
        //
        // **넘기지 않으면 페이지는 아무것도 받지 못한다.** 웹뷰의 자식 창이 0x0이라
        // 마우스 메시지가 전부 우리에게 오기 때문이다.
        //  - 좌표는 표면 client의 물리 픽셀이다. 웹뷰 기준으로 옮기는 것은 여기서 한다.
        //  - **보이는 웹뷰만 가져간다.** 감춰진 웹뷰는 자리가 없는 것이다 — 그 자리의
        //    포인터는 지금 거기 그려진 우리 tree의 것이다.
        [[nodiscard]] bool relay_pointer(const std::u8string& anchor, UINT message, WPARAM word_parameter, int client_x, int client_y);

        // 이 표면의 웹뷰들에 포인터가 떠났음을 알린다.
        void relay_pointer_left(const std::u8string& anchor);
        void cancel_pointer(const std::u8string& anchor);

        // 터치·펜 원본을 이 표면의 웹뷰에 넘긴다 (`SendPointerInput`). 넘겼으면 참이다.
        //  - **접촉은 시작한 자리가 임자다.** DOWN을 받은 웹뷰가 그 포인터의 이동·뗌을
        //    끝까지 갖는다. 영역 밖으로 나가도, 감춰져 보낼 수 없어도 우리 tree로
        //    넘기지 않는다 — 웹뷰에서 시작한 쓸기가 뒤의 목록 몸짓이 되지 않는다.
        //  - 비접촉(펜 hover)은 매번 자리로 고르고, 떠난 웹뷰에는 LEAVE를 준다.
        //  - 다중 접촉을 웹뷰가 그대로 받는다. luil tree의 한 접촉 제한은 여기 닿지 않는다.
        [[nodiscard]] bool relay_pointer_input(const std::u8string& anchor, const webview_pointer_input& input);
        // 웹뷰가 쥔 그 포인터의 접촉을 취소로 끝낸다 (캡처 상실·조회 실패).
        void cancel_pointer_input(const std::u8string& anchor, std::uint32_t pointer_id);

        // 모두 없앤다. 창이 사라지기 전에 UI thread가 부른다.
        void shutdown() noexcept;

    private:
        struct entry;
        struct environment;

        // 취소는 감춰진 뒤에도 전송해야 하므로 hit test·가시성 라우팅과 분리한다.
        void send_pointer_input(entry& target, const webview_pointer_input& input, UINT event_kind);

        [[nodiscard]] environment* acquire_environment(const std::u8string& user_data_folder);
        void begin_controller(entry& target, HWND window);
        // 기억해 둔 명령 중 아직 실행하지 않은 것을 실행한다.
        void flush_commands(entry& target);
        // 사건 하나를 앱으로 나른다.
        //
        // **페이지가 일으킬 수 있는 사건은 상한을 지난다** (`webview_policy`의 초당
        // 건수). 프로세스 실패와 버림 알림 자체는 지나지 않는다 — 앞엣것은 다시 읽기
        // 간격으로 이미 묶여 있고, 뒤엣것은 창마다 한 번뿐이다.
        //  - `bytes`는 web message의 크기다. 다른 사건은 0이다.
        void report(entry& source, webview_event event, std::size_t bytes);
        void report(entry& source, webview_event_kind kind, std::u8string url, std::u8string error);
        void signal_focus(const entry& source, webview_focus_signal signal);
        [[nodiscard]] entry* find(const std::u8string& id) noexcept;
        [[nodiscard]] const entry* find(const std::u8string& id) const noexcept;
        // 항목 하나를 화면과 런타임에서 떼어 낸다 (visual을 떼고 컨트롤러를 닫는다).
        // 없애는 길 둘(대조·종료)이 전부 이것을 지난다.
        void tear_down(entry& target) noexcept;
        // 이 id를 실패로 돌린다. **항목을 여기서 없애지 않는다** — 실패는 대부분
        // WebView2의 콜백 안에서 알게 되고, 그 안에서 그 웹뷰의 참조를 놓거나
        // `entries_`를 줄이면 콜백을 부른 객체와 돌고 있는 순회를 발밑에서 빼는
        // 일이다. 다음 `synchronize`가 실패한 항목을 대조로 없앤다.
        //  - 앱이 목록에서 뺄 때까지 다시 만들지 않는다 (`synchronize`).
        void fail(const std::u8string& id, std::u8string reason);
        // 비동기 만들기의 완료 자리다. 메시지 pump가 이 thread로 돌려준다.
        void finish_environment(const std::u8string& user_data_folder, HRESULT result, ICoreWebView2Environment* created);
        // `serial`은 이 완료가 답하는 시작의 번호다. 시작과 완료 사이에 같은 id가
        // 없어졌다 다시 만들어졌으면 번호가 달라 옛 완료를 버린다 — 버리지 않으면
        // 옛 창에 매인 컨트롤러가 새 항목에 앉는다.
        void finish_controller(const std::u8string& id, std::uint64_t serial, HRESULT result, ICoreWebView2CompositionController* created);

        // 이 host가 아직 살아 있는가.
        //
        // 완료 콜백은 만들기를 시작한 뒤 **몇 frame 뒤에** 오고, 그 사이에 창이
        // 죽을 수 있다. 콜백이 `this`만 잡으면 그때 죽은 객체를 부른다 — 살아 있음을
        // 값으로 잡아 콜백이 스스로 물러서게 한다.
        std::shared_ptr<bool> alive_ { std::make_shared<bool>(true) };
        // visual tree를 바꾼 것이 화면에 닿으려면 커밋해야 한다.
        // `synchronize`가 매 게시마다 다시 준다.
        IDCompositionDevice* composition_ { nullptr };
        std::function<void(app_message)> deliver_ {};
        std::function<void(const std::u8string&, const std::u8string&, webview_focus_signal)> focus_reporter_ {};
        // 브라우저의 기본과 같은 흰색에서 시작한다. 창이 첫 frame에서 팔레트 값으로 바꾼다.
        ui_color default_background_ { make_ui_color(255, 255, 255) };
        void apply_default_background(entry& target) const;
        std::vector<std::unique_ptr<environment>> environments_ {};
        std::vector<std::unique_ptr<entry>> entries_ {};
        // 컨트롤러 만들기의 시작 번호다 (`finish_controller`의 `serial`).
        std::uint64_t creation_serial_ { 0 };
        // 만들다 실패한 id다. 목록에 남아 있는 한 매 frame 생성→실패가 반복되므로
        // 기억해 건너뛴다 (popup·보조 창과 같은 규칙).
        std::vector<std::u8string> failed_ {};
    };
} // namespace luil::win32

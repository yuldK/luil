#pragma once

#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"
#include "luil/win32/renderer_policy.h"
#include "luil/win32/win32_window.h"
#include "win32/frame_state.h"
#include "win32/skia_renderer.h"
#include "win32/uia_provider.h"
#include "win32/win32_drop.h"
#include "win32/win32_tsf_input.h"

#include "include/core/SkTypeface.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 표면은 포인터로만 나른다 — dcomp.h는 실제로 합성을 세우는 자리에서만 든다.
struct IDCompositionDevice;

namespace luil::win32 {
    // 접근 실행이 낸 액션을 창의 **자기 차례**에 배분하는 신호다.
    // UIA 호출은 COM 마샬링을 타고 thread가 메시지를 푸는 아무 자리에나 도착한다
    // (크기 조절 modal 루프·시스템 메뉴·`UiaRaiseAutomationEvent` 안까지). 거기서
    // 곧바로 창 명령을 실행하면 그리는 도중에 창이 죽을 수 있어, 큐에 담고 신호만
    // 보낸다 — `clipboard_request_message`와 같은 모양이다
    // (accessibility-action-design.md).
    //  - `WM_APP` 대역은 주 창의 신호들(win32_window.cpp의 +1~+4)과 한 창에서
    //    만나므로 번호가 겹치지 않아야 한다.
    inline constexpr UINT access_dispatch_message { WM_APP + 5 };

    // 표면 하나의 포인터 모양을 그 창에 적용한다.
    // 어느 모양인지는 그 표면의 tree가 정하고, 실제 커서로 옮기는 것은
    // 소비자가 준 해석기(`window_config::resolve_cursor`)다 —
    // 없거나 빈 핸들을 돌려주면 라이브러리의 기본 매핑으로 물러선다.
    //  - `surface`는 그 표면의 id다 (주 창은 빈 문자열). 상호작용 발행본을
    //    그리기와 같은 규칙으로 걸러 묻는다 (`interaction_for_surface`).
    void apply_surface_cursor(HWND window, const ui_tree* tree, app_host* host, const window_config& config, const std::u8string& surface);

    class window_surface;

    // 표면 여럿이 함께 보는 창 전체의 상태다.
    // 주 창(`application_window`)이 구현하고 표면들은 참조로만 받는다.
    // 표면이 서로를, 또는 소유자를 직접 알지 않게 하는 경계다
    // (win32-surface-design.md).
    class surface_context
    {
    public:
        surface_context() = default;
        surface_context(const surface_context&) = delete;
        surface_context(surface_context&&) = delete;
        surface_context& operator=(const surface_context&) = delete;
        surface_context& operator=(surface_context&&) = delete;
        virtual ~surface_context() = default;

        // 스레드 조립체다.
        // 아직 없으면 nullptr다 (smoke 모드·조립 전).
        [[nodiscard]] virtual app_host* host() noexcept = 0;
        [[nodiscard]] virtual const window_config& config() const noexcept = 0;
        // 앱이 꽂은 상호작용 정책이다 (텍스트 대상 해석·편집 메시지 factory).
        [[nodiscard]] virtual interaction_policy* policy() noexcept = 0;

        // Direct3D 제시가 붙을 DirectComposition device다.
        // 아직 없으면 만들고, 만들 수 없으면 nullptr다 (그러면 Direct3D 렌더러가
        // 서지 않아 automatic 모드는 CPU로 물러선다).
        //  - 포인터 캡처 플래그와 같은 자리다 — **프로세스에 하나뿐인 자원**이라
        //    표면마다 갖는 것이 틀렸다 (win32-surface-design.md). 창별로 갖는
        //    것은 target·visual·스왑체인이고 device는 소유자 하나가 든다
        //    (webview-composition-design.md).
        [[nodiscard]] virtual IDCompositionDevice* composition_device(std::u8string& error) noexcept = 0;

        // 이 표면의 웹뷰들을 자리에 앉히고, 이번 frame에 **비울** 사각형들을 답한다.
        //
        // 자리는 tree의 자리표가 정하므로 그리기 직전에야 알 수 있다 — 그래서
        // 대조(수명)와 달리 이것은 표면마다 render 안에서 불린다.
        //  - `underlay`는 이 표면의 렌더러가 내준 자리다. nullptr이면(CPU 백엔드)
        //    웹뷰가 설 자리가 없어 아무것도 앉히지 않고 빈 span을 답한다.
        //  - 돌려준 span은 **다음 호출까지만** 유효하다. 표면 하나를 그리는 동안만
        //    쓰이고, 그리기가 끝나면 다음 표면이 같은 자리를 다시 채운다.
        [[nodiscard]] virtual std::span<const pixel_rect> apply_webviews(
            const std::u8string& surface, const ui_tree* tree, IDCompositionVisual* underlay, int client_width, int client_height)
            = 0;

        // 이 표면의 웹뷰가 이 포인터를 가져갔는가.
        // 참이면 우리 tree는 그것을 보지 않는다 — 같은 포인터를 양쪽에 주면 웹뷰
        // 위에서 굴린 휠이 뒤의 목록도 함께 스크롤한다.
        [[nodiscard]] virtual bool relay_webview_pointer(const std::u8string& surface, UINT message, WPARAM word_parameter, int client_x, int client_y) = 0;
        // 포인터가 이 표면을 떠났다.
        virtual void webview_pointer_left(const std::u8string& surface) = 0;
        virtual void cancel_webview_pointer(const std::u8string& surface) = 0;

        // frame의 외양 선호를 `frame_state`로 옮긴다 (테마·고대비·글꼴).
        // 돌려준 typeface는 그 frame을 그리는 동안 살아 있어야 한다.
        [[nodiscard]] virtual sk_sp<SkTypeface> apply_frame_appearance(frame_state& state, const ui_frame* frame) = 0;

        // 닫힘 계기 하나를 모든 popup에 알린다.
        // 무엇 하나라도 냈으면 참이다.
        virtual bool dismiss_popups(popup_dismiss_reason reason) = 0;

        // 이 표면의 창이 움직였다.
        // 닫힐 popup은 닫자는 메시지를 내고, **이 표면에 붙은** popup은 따라 옮긴다
        // (popup-anchor-design.md).
        virtual void surface_moved(const std::u8string& anchor) = 0;

        // 표면이 만든 액션을 통상 규칙대로 배분한다 (앱 메시지·창 명령·클립보드).
        virtual void dispatch_action(input_action action) = 0;

        // 수락한 element가 없는 파일 드롭은 경로 순서대로 delegate에 전달한다.
        // delegate가 true를 반환하면 나머지 경로는 전달하지 않는다.
        virtual void deliver_file_drop(const std::vector<std::u8string>& paths) = 0;

        // 모든 표면 tree가 `next_update`로 예고한 시각 중 가장 이른 것에 timer 하나를 건다.
        //  - `surface`는 부르는 표면의 id이고 `interaction`은 **그 표면의 것**이다
        //    (표면 필터를 지났고 caption hover까지 얹혀 있다). 그것을 다른 표면의
        //    tree에 그대로 주면 그 표면에서 난 초점·hover가 이미 지워져 있어,
        //    예컨대 popup 안 caret이 깜빡일 시각을 아무도 예고하지 않는다.
        //    그래서 나머지 표면에는 발행본을 그 표면의 것으로 다시 걸러 넘긴다
        //    (multi-window-design.md).
        virtual void schedule_update_repaint(const std::u8string& surface, const interaction_snapshot& interaction) noexcept = 0;

        // 이 표면이 keyboard focus를 잃었다.
        // 그 표면과 **그 표면에 붙은 popup들**의 텍스트 초점을 거둔다 —
        // popup은 자기 focus가 없어 앵커 창의 keyboard focus에 얹혀 있었다
        // (popup-ime-design.md).
        //  - popup을 닫지는 않는다. focus 상실은 활성 전환과 **같은 사건에 두 번
        //    오는 알림**이라(`WM_KILLFOCUS`와 `WM_ACTIVATE`) 계기로 삼으면 앱이
        //    같은 일을 두 번 받는다. 텍스트 칸이 있는 popup을 남기는 것은 이제
        //    `activation_changed`를 앱이 빈 액션으로 물리쳐서 한다.
        virtual void surface_focus_lost(const std::u8string& surface) = 0;

        // 이 표면이 keyboard focus를 얻었다 (상실의 짝).
        // input thread가 이것으로 **활성 표면**을 안다 — 가둠 진입이 어느 tree를
        // 고를지가 그 값이다 (active-surface-design.md).
        //  - 상실과 달리 **붙은 popup에 퍼뜨리지 않는다.** 상실이 퍼뜨리는 것은
        //    앵커에 얹혀 있던 popup의 텍스트 초점이 함께 죽기 때문이고, 얻음이
        //    답하는 질문은 "어느 **창**이 활성인가"인데 popup은
        //    `WS_EX_NOACTIVATE`라 그 답이 될 수 없다.
        virtual void surface_focus_gained(const std::u8string& surface) = 0;

        // 이 앵커에 붙은 popup 표면 중 id가 같은 것이다. 없으면 nullptr.
        // popup은 `WS_EX_NOACTIVATE`라 keyboard focus를 받지 못해 자기 TSF
        // session이 없다 — 앵커 표면의 session이 이것으로 popup의 tree와 창을
        // 함께 본다 (popup-ime-design.md).
        //  - popup은 popup에 앵커할 수 없어 한 단계로 끝난다.
        [[nodiscard]] virtual const window_surface* anchored_popup(const std::u8string& anchor, const std::u8string& id) const noexcept = 0;

        // 포인터 캡처 무장 상태다.
        // 캡처는 프로세스에 하나뿐인 자원이라 표면마다 갖지 않는다.
        [[nodiscard]] virtual bool pointer_capture_active() const noexcept = 0;
        virtual void set_pointer_capture_active(bool active) noexcept = 0;

        virtual void report_error(const std::u8string& error) noexcept = 0;
    };

    // 표면 하나의 TSF 창구다.
    // 초점 표면(`focused_surface`)이 이 표면의 것일 때만 대상을 보고해
    // 세션들이 thread manager의 초점을 서로 다투지 않는다
    // (multi-window-design.md).
    //  - 주 창의 표면 id는 **빈 문자열**이라 "다른 표면인가"를 묻는 같은 식이
    //    "보조 창 표면인가"와 정확히 같아진다 (win32-surface-design.md).
    //  - "이 표면의 것"에는 **이 표면에 붙은 popup**도 든다. popup은 focus를
    //    받지 못해 자기 session이 없으므로 앵커의 session이 대신 본다
    //    (popup-ime-design.md).
    class surface_tsf_host final : public tsf_host
    {
    public:
        surface_tsf_host(surface_context& context, const window_surface& surface) noexcept;

        [[nodiscard]] std::optional<text_input_target> focused_text_target() const override;
        [[nodiscard]] shadow_document committed_document() const override;
        [[nodiscard]] std::optional<RECT> text_screen_rect(const shadow_document& document, std::size_t begin, std::size_t end) const override;
        void post_composition(text_composition_event event) override;
        void post_edit(text_edit_request request) override;

    private:
        // 이 host가 TSF로 대신 볼 표면이다. 없으면 nullptr.
        //  - 초점 표면이 자기면 자기다.
        //  - 자기에게 붙은 popup이면 그 popup이다.
        //  - 그 밖이면 없다 — 다른 표면의 몫이다.
        [[nodiscard]] const window_surface* text_surface() const;
        // 그 표면의 tree에서 초점을 가진 텍스트 박스를 찾는다.
        [[nodiscard]] const ui_element* focused_element(const window_surface& surface) const;

        surface_context* context_ { nullptr };
        const window_surface* surface_ { nullptr };
    };

    // 창 하나에 대응하는 표면이다.
    // 주 창·보조 창·popup이 공통으로 갖는 것을 담는다: HWND, 자기 renderer,
    // 그릴 tree, 자기 배율, 표면 id, 마우스 추적, OS 파일 끌기의 창구.
    //  - 표면 id는 입력 이벤트에 붙는 표식이다. **주 창은 비어 있다.**
    class window_surface : public drop_target_host, public uia_surface_host
    {
    public:
        explicit window_surface(surface_context& context, std::u8string id = {}) noexcept;
        window_surface(const window_surface&) = delete;
        window_surface(window_surface&&) = delete;
        window_surface& operator=(const window_surface&) = delete;
        window_surface& operator=(window_surface&&) = delete;
        virtual ~window_surface() = default;

        [[nodiscard]] HWND window() const noexcept
        {
            return window_;
        }

        [[nodiscard]] const std::u8string& id() const noexcept
        {
            return id_;
        }

        // 창이 아직 살아 있는지다.
        // `WM_DESTROY`가 오면 거짓이 되고 소유자가 목록에서 걷어낸다.
        // 이 표면의 웹뷰가 Win32 초점을 쥐고 있는가 (TSF 양보의 판정).
        [[nodiscard]] bool webview_has_focus() const noexcept
        {
            return webview_focused_;
        }

        [[nodiscard]] bool attached() const noexcept
        {
            return window_ != nullptr;
        }

        [[nodiscard]] std::uint32_t dpi() const noexcept
        {
            return dpi_;
        }

        [[nodiscard]] const ui_tree* tree() const noexcept
        {
            return tree_.get();
        }

        // 이 표면이 그릴 tree다.
        // 바뀌면 참이라 소유자가 다시 그리기를 건다.
        bool set_tree(std::shared_ptr<const ui_tree> tree);

        // 창을 이 표면에 붙인다 (`WM_NCCREATE`).
        void attach_window(HWND window) noexcept;
        // 창이 사라졌다 (`WM_DESTROY`).
        void detach_window() noexcept;
        void set_dpi(std::uint32_t dpi) noexcept;

        // 이 창의 renderer를 만든다.
        // 창을 붙인 뒤(`WM_NCCREATE`) 한 번 부른다.
        [[nodiscard]] bool create_renderer(renderer_mode mode, renderer_fault_injection fault, int width, int height, std::u8string& error);
        [[nodiscard]] bool resize_renderer(int width, int height, std::u8string& error);

        // 이 표면의 IME 연결을 만든다 (multi-window-design.md — 표면마다 문서 하나).
        // 만들지 못하면 조용히 없이 간다 — 그 표면은 `WM_CHAR` 경로만으로 동작하고
        // 조합은 시스템 창이 그린다.
        void create_text_input();
        // TSF는 창보다 먼저 정리한다.
        // 조합 중이었다면 확정된 글이 logic으로 간다.
        void reset_text_input() noexcept;

        [[nodiscard]] tsf_input_session* text_input() const noexcept
        {
            return text_input_.get();
        }

        // 표면이 공통으로 번역하는 메시지다 (포인터·휠·커서·배경·그리기).
        // 처리했으면 그 결과고, 아니면 nullopt라 호출자가 자기 처리를 이어 본다.
        [[nodiscard]] std::optional<LRESULT> handle_surface_message(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 이 표면 한 장을 그린다.
        [[nodiscard]] bool render(std::u8string& error);

    protected:
        // 이 표면 위의 입력이 popup 밖 입력인가다.
        // popup 자신만 거짓이다 — 자기 위 클릭은 popup 안 클릭이다.
        [[nodiscard]] virtual bool input_dismisses_popups() const noexcept
        {
            return true;
        }

        // 이 표면이 OS 파일 끌기를 받는가다.
        // 기본은 받지 않는다 — popup은 메뉴류라 드롭 소비자가 없다
        // (os-dragdrop-design.md). 주 창·보조 창(caption_surface)만 설정을 따른다.
        [[nodiscard]] virtual bool accepts_file_drop() const noexcept
        {
            return false;
        }

        // `frame_state`에 표면마다 다른 것을 얹는다
        // (caption hover, TSF 동기, 시간 예고, tree가 없을 때의 대체 화면).
        virtual void prepare_frame(frame_state& state)
        {
            static_cast<void>(state);
        }

        // 그리기가 실패했다.
        // 기본은 알리기만 하고, 주 창은 프로세스를 끝낸다.
        virtual void on_render_failed(const std::u8string& error);

        // 포인터·휠 메시지 하나를 번역해 같은 pump에 넣는다.
        // 번역이 됐으면 참이라 호출자가 메시지를 삼킨다.
        [[nodiscard]] bool post_pointer_message(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        // 누름 동안 포인터를 이 창에 묶는다.
        // 배경 스레드의 `SetCapture`는 실제로 캡처를 얻지 못할 수 있어,
        // 얻은 경우에만 상실 취소를 무장한다.
        void capture_pointer() noexcept;
        // 뗌 처리기가 `ReleaseCapture` **직전에** 무장을 푼다.
        // 자기 `ReleaseCapture`도 `WM_CAPTURECHANGED`를 동기로 보내기 때문이다.
        void release_pointer_capture() noexcept;
        // 버튼을 잡은 채(무장한 채) **빼앗긴** 경우다.
        // 무장 없이 좌표만 보면 정상 클릭의 뗌보다 합성 뗌이 먼저 들어가
        // 모든 클릭이 무효가 된다.
        [[nodiscard]] bool pointer_capture_lost(LPARAM long_parameter) noexcept;
        // 캡처를 빼앗겨 `WM_xBUTTONUP`이 오지 않는 경우의 정리다.
        // 먼 좌표의 왼쪽 뗌을 합성해 press·끌기를 전부 푼다 —
        // hit가 없는 뗌은 클릭이 되지 않고 상태만 정리된다.
        void cancel_pointer_press();

        // 눌린 키를 이벤트로 만들어 input thread에 보낸다.
        // 이벤트가 됐으면 참이라 호출자가 메시지를 삼킨다.
        [[nodiscard]] bool post_key_event(WPARAM virtual_key, LPARAM long_parameter);

        void track_mouse_leave() noexcept;
        // 지금 포인터 자리의 모양을 이 창에 적용한다.
        // 어느 모양인지는 이 표면의 tree가 정하고, 실제 커서로 옮기는 것은
        // 소비자가 준 해석기(없으면 기본 매핑)다.
        void apply_cursor();

    private:
        // --- uia_surface_host ---
        // provider가 묻는 것 전부다. 표면이 이미 frame마다 갱신해 드는 값을
        // 그대로 내준다.
        [[nodiscard]] const ui_tree* accessibility_tree() const noexcept override;
        [[nodiscard]] HWND accessibility_window() const noexcept override;
        [[nodiscard]] ui_element_id accessibility_focus() const override;
        void accessibility_dispatch(std::vector<input_action> actions) override;
        void accessibility_request_focus(const ui_element_id& id) override;
        // 텍스트 칸의 Text pattern·값 쓰기다 — 대상 해석은 앱 정책,
        // 편집 배분은 `accessibility_dispatch`와 같은 미룸, 좌표·측정은 TSF가
        // 쓰는 그 함수다.
        [[nodiscard]] std::optional<text_input_target> accessibility_text_target(ui_element_kind kind) const override;
        void accessibility_text_edit(text_edit_request request) override;
        [[nodiscard]] std::optional<RECT> accessibility_text_span(const ui_element_id& id, std::u8string_view document, std::size_t caret, std::size_t begin,
            std::size_t end) const override;
        [[nodiscard]] std::optional<std::size_t> accessibility_text_offset(const ui_element_id& id, float screen_x, float screen_y) const override;

        // 이 표면으로 걸러진 초점이 바뀌었으면 UIA에 알린다.
        // 그리는 길에 얹는다 — 초점 변화는 언제나 발행본 게시로 오고 게시는 모든
        // 표면을 깨운다 (accessibility-design.md).
        void announce_accessibility_focus(const ui_element_id& focused);
        // 기준선(접근 발행본 사본)이 없으면 지금 발행본으로 심는다 — 알림은
        // 내지 않는다 (`WM_GETOBJECT`의 SendMessage 재진입에서 부르는 자리라
        // 읽어 답만 한다). 기준선이 그리기에서만 서면, 클라이언트가 붙은 뒤 첫
        // 변화의 frame이 곧 첫 그리기가 되어 그 변화가 기준선으로 삼켜진다.
        void seed_accessibility_baseline();
        // 이 표면의 접근 발행본을 직전 것과 견줘 값·상태·구조 변경을 UIA에
        // 알린다. 초점과 같은 자리(그리는 길)에 얹는다.
        //  - provider가 없으면 아무것도 재지 않는다 (평소 비용 0). 선 뒤에는
        //    듣는 클라이언트가 잠시 없어도 사본을 이어 간다 — 기준선을 버리면
        //    다시 붙은 클라이언트의 첫 변화가 새 기준선으로 삼켜진다. 알림
        //    자체는 들을 때만 나간다.
        void announce_accessibility_changes();
        // 창이 죽는다. 클라이언트가 쥔 참조를 풀고 provider를 끊는다.
        void revoke_uia_provider() noexcept;

        // 첫 `WM_GETOBJECT`에서 만든다. UIA 클라이언트가 없으면 영영 없다.
        uia_root_provider* uia_root_ { nullptr };
        // 마지막으로 알린 초점이다 (빈 id면 없음).
        ui_element_id announced_focus_ {};
        // 마지막으로 알린 접근 발행본과 그 tree다.
        // tree 포인터는 "같은 발행본의 재그리기"를 거르는 표식일 뿐이고
        // (`set_tree`가 비운다 — 해제된 주소의 재사용과 헷갈리지 않게), 내용은
        // 사본이 든다 — 이전 tree는 이미 사라졌기 때문이다.
        access_snapshot announced_access_ {};
        const ui_tree* announced_access_tree_ { nullptr };
        bool announced_access_valid_ { false };
        // 아직 배분하지 않은 접근 실행의 액션이다 (`access_dispatch_message`가 비운다).
        std::vector<input_action> pending_access_actions_ {};

        // --- drop_target_host ---
        // 표시(entered·moved·left)는 input thread의 스냅샷으로 보내고,
        // 놓기는 이 표면의 tree에 직접 물어 그 자리에서 실행한다
        // (os-dragdrop-design.md — caption 버튼의 동기 실행과 같은 자리다).
        void file_drag_entered(float x, float y, std::vector<std::u8string> files) override;
        void file_drag_moved(float x, float y) override;
        void file_drag_left() override;
        void file_drag_dropped(float x, float y, const std::vector<std::u8string>& files) override;

        // OS 파일 끌기의 등록·해제다 (attach·detach에서 부른다).
        // OLE가 없거나 등록이 실패하면 드롭만 조용히 꺼진다 (com_sta_scope의 계약).
        void register_drop_target() noexcept;
        void revoke_drop_target() noexcept;

        surface_drop_target* drop_target_ { nullptr };

    protected:
        surface_context& context_;
        HWND window_ { nullptr };
        std::u8string id_ {};
        // 이 표면의 웹뷰가 Win32 초점을 갖는지 나타낸다.
        // 페이지 클릭으로 크기 0인 자식 창에 초점이 옮겨도 부모 창은 활성을 유지한다.
        // WM_KILLFOCUS를 창 비활성으로 처리하지 않아 열린 메뉴와 텍스트 캐럿을 유지한다.
        bool webview_focused_ { false };
        std::uint32_t dpi_ { 96 };
        std::unique_ptr<renderer_host> renderer_ {};
        // host가 session보다 오래 살아야 한다 (선언 순서가 곧 파괴 순서의 역이다).
        std::unique_ptr<surface_tsf_host> text_input_host_ {};
        std::unique_ptr<tsf_input_session> text_input_ {};
        // `WM_CHAR`로 앞쪽만 도착한 surrogate다.
        // 표면마다 따로다 — UI thread 전용이다.
        std::optional<char16_t> pending_high_surrogate_ {};
        std::shared_ptr<const ui_tree> tree_ {};
        bool tracking_mouse_ { false };
    };
} // namespace luil::win32

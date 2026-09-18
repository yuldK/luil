#include "win32/window_surface.h"

#include "luil/ui/draw_primitives.h"
#include "win32/embedded_assets.h"
#include "win32/surface_input.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"
#include "win32/win32_fonts.h"

#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/encode/SkWebpEncoder.h"

#include <windowsx.h>

#include <uiautomationcoreapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace luil::win32 {
    namespace {
        // `ui_cursor`의 기본 매핑이다.
        // 소비자가 `resolve_cursor`를 주지 않았거나 그쪽이 nullptr를 돌려줄 때 쓴다.
        [[nodiscard]] HCURSOR default_cursor_handle(const ui_cursor cursor) noexcept
        {
            switch (cursor)
            {
            case ui_cursor::text:
                return LoadCursorW(nullptr, IDC_IBEAM);
            case ui_cursor::hand:
            case ui_cursor::grab:
                return LoadCursorW(nullptr, IDC_HAND);
            // Win32에는 "쥔 손" 커서가 없다.
            // 옮기는 중이라는 뜻으로는 네 방향 화살표가 관례에 가깝다.
            case ui_cursor::grabbing:
                return LoadCursorW(nullptr, IDC_SIZEALL);
            case ui_cursor::resize_horizontal:
                return LoadCursorW(nullptr, IDC_SIZEWE);
            case ui_cursor::resize_vertical:
                return LoadCursorW(nullptr, IDC_SIZENS);
            case ui_cursor::not_allowed:
                return LoadCursorW(nullptr, IDC_NO);
            case ui_cursor::wait:
                return LoadCursorW(nullptr, IDC_WAIT);
            default:
                return LoadCursorW(nullptr, IDC_ARROW);
            }
        }
    } // namespace

    void apply_surface_cursor(const HWND window, const ui_tree* const tree, app_host* const host, const window_config& config, const std::u8string& surface)
    {
        ui_cursor cursor { ui_cursor::inherit };
        POINT position {};
        // 그리기와 같은 필터를 지난 것으로 묻는다 — `cursor_at`은 끌기와 눌림을
        // 보고 모양을 정하므로, 거르지 않으면 A창에서 끄는 동안 B창 위의 포인터가
        // 함께 "잡은 모양"이 된다 (multi-window-design.md).
        if (tree != nullptr && host != nullptr && GetCursorPos(&position) != FALSE && ScreenToClient(window, &position) != FALSE)
            cursor = tree->cursor_at(static_cast<float>(position.x), static_cast<float>(position.y), interaction_for_surface(host->acquire_interaction(), surface));

        HCURSOR handle { nullptr };
        if (config.resolve_cursor)
            handle = static_cast<HCURSOR>(config.resolve_cursor(cursor).value);
        if (handle == nullptr)
            handle = default_cursor_handle(cursor);
        SetCursor(handle);
    }

    window_surface::window_surface(surface_context& context, std::u8string id) noexcept
        : context_ { context }
        , id_ { std::move(id) }
    {}

    void window_surface::attach_window(const HWND window) noexcept
    {
        window_ = window;
        register_drop_target();
    }

    void window_surface::detach_window() noexcept
    {
        // UIA 클라이언트가 쥔 참조부터 푼다 — 창이 사라진 뒤에는 풀 길이 없다.
        revoke_uia_provider();
        revoke_drop_target();
        // TSF는 창보다 먼저 정리한다.
        reset_text_input();
        renderer_.reset();
        window_ = nullptr;
    }

    const ui_tree* window_surface::accessibility_tree() const noexcept
    {
        return tree_.get();
    }

    HWND window_surface::accessibility_window() const noexcept
    {
        return window_;
    }

    ui_element_id window_surface::accessibility_focus() const
    {
        app_host* const host { context_.host() };
        if (host == nullptr)
            return {};
        // 그리기와 같은 필터를 지난 초점이다 — id는 tree 안에서만 안정적이라
        // 남의 표면의 초점을 자기 것으로 답하면 안 된다 (multi-window-design.md).
        return interaction_for_surface(host->acquire_interaction(), id_).focused;
    }

    void window_surface::accessibility_dispatch(std::vector<input_action> actions)
    {
        // 창이 없으면 배분할 자리도 없다 (죽는 중인 표면).
        if (actions.empty() || window_ == nullptr)
            return;
        pending_access_actions_.insert(pending_access_actions_.end(), std::make_move_iterator(actions.begin()), std::make_move_iterator(actions.end()));
        // 실행은 이 창의 다음 차례다 — 지금 stack에는 남의 pump가 있을 수 있다.
        static_cast<void>(PostMessageW(window_, access_dispatch_message, 0, 0));
    }

    void window_surface::accessibility_request_focus(const ui_element_id& id)
    {
        // 초점은 입력 상태라 input thread가 세운다 (accessibility-action-design.md).
        // 표식은 이 표면의 것이고 시각은 게시 시점이다 — 포인터·키와 같은 규약이다.
        if (app_host* const host { context_.host() }; host != nullptr)
            host->post_raw_input(access_focus_event { id, id_, std::chrono::steady_clock::now() });
    }

    std::optional<text_input_target> window_surface::accessibility_text_target(const ui_element_kind kind) const
    {
        // 대상 해석은 TSF와 같은 정책 훅이다 (`text_target_of`).
        interaction_policy* const policy { context_.policy() };
        if (policy == nullptr)
            return std::nullopt;
        return policy->text_target_of(kind);
    }

    void window_surface::accessibility_text_edit(text_edit_request request)
    {
        // TSF의 post_edit와 같은 변환을 지나, 접근 실행과 같은 미룸으로 배분한다 —
        // UIA 호출은 thread가 메시지를 푸는 아무 자리에나 온다 (3.3).
        interaction_policy* const policy { context_.policy() };
        if (policy == nullptr)
            return;
        std::vector<input_action> actions {};
        actions.push_back(policy->make_text_edit_action(std::move(request)));
        accessibility_dispatch(std::move(actions));
    }

    std::optional<RECT> window_surface::accessibility_text_span(const ui_element_id& id, const std::u8string_view document, const std::size_t caret, const std::size_t begin,
        const std::size_t end) const
    {
        if (tree_ == nullptr || window_ == nullptr)
            return std::nullopt;
        const ui_element* const element { tree_->find(id) };
        if (element == nullptr)
            return std::nullopt;

        // 측정은 TSF의 구간 질의와 같은 함수다 — 글꼴이 다르면 후보 창과 읽기
        // 구간이 서로 다른 자리를 가리킨다.
        const text_measurer measure = [](const std::u8string_view text, const float pixel_size) {
            const SkFont font { configured_ui_typeface(), pixel_size };
            return measure_text(text, font);
        };
        const std::optional<rect_f> span { element->text_span_bounds(text_span_query { document, caret, begin, end }, measure) };
        if (span.has_value() == false)
            return std::nullopt;

        POINT top_left { static_cast<LONG>(span->x), static_cast<LONG>(span->y) };
        POINT bottom_right { static_cast<LONG>(span->x + span->width), static_cast<LONG>(span->y + span->height) };
        if (bottom_right.x < top_left.x)
            bottom_right.x = top_left.x;
        if (ClientToScreen(window_, &top_left) == FALSE || ClientToScreen(window_, &bottom_right) == FALSE)
            return std::nullopt;
        return RECT { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
    }

    std::optional<std::size_t> window_surface::accessibility_text_offset(const ui_element_id& id, const float screen_x, const float screen_y) const
    {
        if (tree_ == nullptr || window_ == nullptr)
            return std::nullopt;
        const ui_element* const element { tree_->find(id) };
        if (element == nullptr)
            return std::nullopt;
        POINT point { static_cast<LONG>(std::lround(screen_x)), static_cast<LONG>(std::lround(screen_y)) };
        if (ScreenToClient(window_, &point) == FALSE)
            return std::nullopt;
        const text_measurer measure = [](const std::u8string_view text, const float pixel_size) {
            const SkFont font { configured_ui_typeface(), pixel_size };
            return measure_text(text, font);
        };
        return element->offset_at(static_cast<float>(point.x), measure);
    }

    void window_surface::announce_accessibility_focus(const ui_element_id& focused)
    {
        if (focused == announced_focus_)
            return;
        announced_focus_ = focused;
        if (uia_root_ == nullptr || focused == ui_element_id {} || UiaClientsAreListening() == FALSE)
            return;
        uia_root_->announce_focus(focused);
    }

    void window_surface::seed_accessibility_baseline()
    {
        // 이미 선 기준선은 덮지 않는다 — 아직 그리지 않은 새 발행본의 diff가
        // 여기서 지워지면 그 변화가 통째로 사라진다.
        if (uia_root_ == nullptr || tree_ == nullptr || announced_access_valid_)
            return;
        announced_access_ = make_access_snapshot(*tree_);
        announced_access_tree_ = tree_.get();
        announced_access_valid_ = true;
    }

    void window_surface::announce_accessibility_changes()
    {
        // provider를 만들기 전에는 접근성 발행본을 비교하지 않는다.
        // provider 생성 이후에는 수신 클라이언트가 잠시 없어도 기준선을 유지한다.
        // 기준선을 버리면 다시 연결한 뒤의 첫 변경 알림을 놓칠 수 있다.
        if (uia_root_ == nullptr || tree_ == nullptr)
        {
            announced_access_valid_ = false;
            announced_access_tree_ = nullptr;
            return;
        }
        // 같은 발행본의 재그리기(크기 조절·가림 복구)는 변화가 아니다.
        if (announced_access_tree_ == tree_.get())
            return;
        access_snapshot current { make_access_snapshot(*tree_) };
        if (announced_access_valid_ && UiaClientsAreListening() != FALSE)
        {
            const std::vector<access_change> changes { diff_access_snapshots(announced_access_, current) };
            if (changes.empty() == false)
                uia_root_->announce_changes(changes);
        }
        announced_access_ = std::move(current);
        announced_access_tree_ = tree_.get();
        announced_access_valid_ = true;
    }

    void window_surface::revoke_uia_provider() noexcept
    {
        // 아직 배분하지 않은 실행은 창과 함께 사라진다 — 신호가 올 창이 없다.
        pending_access_actions_.clear();
        if (uia_root_ == nullptr)
            return;
        if (window_ != nullptr)
            static_cast<void>(UiaReturnRawElementProvider(window_, 0, 0, nullptr));
        // 끊긴 provider는 남은 참조가 있어도 모든 질문에
        // `UIA_E_ELEMENTNOTAVAILABLE`로 답한다.
        uia_root_->detach();
        uia_root_->Release();
        uia_root_ = nullptr;
    }

    void window_surface::register_drop_target() noexcept
    {
        if (window_ == nullptr || accepts_file_drop() == false)
            return;
        // COM 껍데기는 참조 수가 수명이라 new로 만든다.
        // OLE가 등록하며 하나 더 쥐고, 우리 몫은 해제 때 놓는다.
        drop_target_ = new surface_drop_target { window_, *this };
        if (FAILED(RegisterDragDrop(window_, drop_target_)))
        {
            drop_target_->Release();
            drop_target_ = nullptr;
        }
    }

    void window_surface::revoke_drop_target() noexcept
    {
        if (drop_target_ == nullptr)
            return;
        static_cast<void>(RevokeDragDrop(window_));
        drop_target_->Release();
        drop_target_ = nullptr;
    }

    void window_surface::file_drag_entered(const float x, const float y, std::vector<std::u8string> files)
    {
        if (app_host* const host { context_.host() }; host != nullptr)
            host->post_raw_input(file_drag_entered_event { x, y, std::move(files), id_ });
    }

    void window_surface::file_drag_moved(const float x, const float y)
    {
        if (app_host* const host { context_.host() }; host != nullptr)
            host->post_raw_input(file_drag_moved_event { x, y, id_ });
    }

    void window_surface::file_drag_left()
    {
        if (app_host* const host { context_.host() }; host != nullptr)
            host->post_raw_input(file_drag_left_event { id_ });
    }

    void window_surface::file_drag_dropped(const float x, const float y, const std::vector<std::u8string>& files)
    {
        // 답과 실행은 이 표면의 tree에 직접 묻는다 (os-dragdrop-design.md).
        // caption 버튼이 tree의 액션을 동기로 실행하는 것과 같은 자리다.
        drag_payload payload {};
        payload.custom_visual = true;
        payload.files = files;
        const ui_element* const over { tree_ != nullptr ? tree_->find_drop_target(x, y, payload) : nullptr };
        if (over != nullptr && over->drop()->on_drop)
        {
            for (input_action& action : over->drop()->on_drop(payload, ui_action_context { over->id(), x, y, false }))
                context_.dispatch_action(std::move(action));
        }
        else
        {
            // 수락 element가 없으면 지금까지의 물러섬이다 (delegate의 on_file_dropped).
            context_.deliver_file_drop(files);
        }
        // 놓기도 표시로는 떠남이다.
        file_drag_left();
    }

    void window_surface::set_dpi(const std::uint32_t dpi) noexcept
    {
        dpi_ = dpi;
    }

    void window_surface::create_text_input()
    {
        text_input_host_ = std::make_unique<surface_tsf_host>(context_, *this);
        text_input_ = tsf_input_session::create(window_, *text_input_host_);
        if (text_input_ == nullptr)
            text_input_host_.reset();
    }

    void window_surface::reset_text_input() noexcept
    {
        text_input_.reset();
        text_input_host_.reset();
    }

    bool window_surface::set_tree(std::shared_ptr<const ui_tree> tree)
    {
        if (tree_ == tree)
            return false;
        tree_ = std::move(tree);
        // 재그리기 거름의 표식을 비운다 — 새 tree가 옛 주소를 재사용해도
        // "같은 발행본"으로 오인하지 않는다.
        announced_access_tree_ = nullptr;
        return true;
    }

    bool window_surface::create_renderer(const renderer_mode mode, const renderer_fault_injection fault, const int width, const int height, std::u8string& error)
    {
        // CPU로 고정된 표면(popup)은 device를 묻지 않는다 — 만들 이유가 없고,
        // 물으면 그 표면 때문에 프로세스에 device가 생긴다.
        IDCompositionDevice* composition { nullptr };
        if (mode != renderer_mode::cpu && fault.at_creation == false)
        {
            std::u8string composition_error {};
            composition = context_.composition_device(composition_error);
            // device가 없으면 Direct3D를 못 세운다. 그 판단(물러설 것인가 멈출
            // 것인가)은 mode를 아는 renderer_host의 몫이라 여기서는 넘기기만 한다.
            if (composition == nullptr && mode == renderer_mode::direct3d)
            {
                error = std::move(composition_error);
                return false;
            }
        }
        renderer_ = renderer_host::create(window_, mode, fault, composition, error);
        return renderer_ != nullptr && renderer_->resize(std::max(1, width), std::max(1, height), error);
    }

    bool window_surface::resize_renderer(const int width, const int height, std::u8string& error)
    {
        if (renderer_ == nullptr)
            return true;
        return renderer_->resize(std::max(1, width), std::max(1, height), error);
    }

    std::optional<LRESULT> window_surface::handle_surface_message(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            track_mouse_leave();
            if (post_pointer_message(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_MOUSELEAVE:
            // hover 강조가 창 밖에서 남지 않게 한다.
            tracking_mouse_ = false;
            context_.webview_pointer_left(id_);
            if (app_host* const host { context_.host() }; host != nullptr)
                host->post_raw_input(pointer_left_event { id_ });
            return LRESULT { 0 };
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            // 이 표면 위의 누름은 popup 밖 클릭이다.
            // 닫자는 메시지를 내고 클릭 자체는 그대로 진행한다.
            if (input_dismisses_popups())
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::pointer_press_outside));
            capture_pointer();
            if (post_pointer_message(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if ((word_parameter & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0)
                release_pointer_capture();
            if (post_pointer_message(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_MOUSEWHEEL:
            // popup의 자리는 이번 frame element의 자리에서 왔다.
            // 내용이 스크롤되면 닻이 움직이므로 popup도 닫는 계기다 —
            // 안 닫으면 메뉴가 옛 자리에 떠 있게 된다.
            if (input_dismisses_popups())
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::wheel_scrolled));
            if (post_pointer_message(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_CAPTURECHANGED:
            if (pointer_capture_lost(long_parameter))
                cancel_pointer_press();
            return std::nullopt;
        case WM_SETCURSOR:
            // 비클라이언트(크기 조절 테두리)는 시스템에 맡긴다.
            if (LOWORD(long_parameter) != HTCLIENT)
                return std::nullopt;
            apply_cursor();
            return LRESULT { TRUE };
        case WM_KEYUP:
            // TIP이 먼저 볼 기회를 준다.
            // 먹었으면 앱은 손대지 않는다.
            if (text_input_ != nullptr && text_input_->handle_key(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_KEYDOWN:
            // 조합 키는 TIP의 것이다.
            // 먹지 않은 키만 앱의 단축키로 간다.
            if (text_input_ != nullptr && text_input_->handle_key(message, word_parameter, long_parameter))
                return LRESULT { 0 };
            // popup이 떠 있는 동안의 Esc는 popup을 닫는 뜻이 먼저다.
            // 닫자는 메시지를 냈으면 키를 삼켜 앱 라우팅으로 가지 않는다.
            // 어느 popup도 닫자고 하지 않으면(전부 빈 액션) Esc는 그대로 앱으로 간다.
            if (word_parameter == VK_ESCAPE && input_dismisses_popups() && context_.dismiss_popups(popup_dismiss_reason::escape_key))
                return LRESULT { 0 };
            if (post_key_event(word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_SYSKEYDOWN:
            // Alt 조합 키다 — 앱 단축키(Alt+X 등)를 같은 경로로 나른다.
            // Alt+Space(시스템 메뉴)는 caption chrome이 먼저 보고 여기까지 오지 않는다.
            // Alt+F4는 창 닫기라 삼키지 않고 시스템에 맡긴다.
            if (word_parameter != VK_F4 && post_key_event(word_parameter, long_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_IME_SETCONTEXT:
            // TSF가 조합을 그리므로 시스템 조합 창 UI를 끈다.
            // 켜 두면 같은 글자가 두 번 그려진다.
            return DefWindowProcW(window_, message, word_parameter, long_parameter & ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW));
        case WM_CHAR:
            // BMP 밖 문자는 surrogate **쌍**으로 두 번에 나눠 오므로 앞쪽을
            // 잠시 들고 있다가 뒤쪽이 오면 합쳐 codepoint 하나로 보낸다.
            //
            // 조합 중에는 흘리지 않는다.
            //  - 확정 글은 TSF가 이미 보냈고, 여기서 또 넣으면 같은 글자가 두 번 들어간다.
            if (text_input_ != nullptr && text_input_->composing())
                return LRESULT { 0 };
            if (app_host* const host { context_.host() }; host != nullptr)
            {
                if (const std::optional<char32_t> codepoint { merge_utf16_code_unit(pending_high_surrogate_, static_cast<char16_t>(word_parameter)) }; codepoint.has_value())
                    host->post_raw_input(character_typed_event { *codepoint, std::chrono::steady_clock::now() });
                return LRESULT { 0 };
            }
            return std::nullopt;
        case WM_SETFOCUS:
            // 웹뷰에서 돌아온 것이면 잃은 적이 없다. 알린 적도 없으므로 알리지 않는다.
            if (webview_focused_)
            {
                webview_focused_ = false;
                if (text_input_ != nullptr)
                    text_input_->set_window_focused(true);
                return LRESULT { 0 };
            }
            // 창 focus만 돌아왔다고 text focus를 복구하지 않는다.
            // 새 text focus는 사용자가 텍스트 박스를 다시 눌러야 생긴다.
            if (text_input_ != nullptr)
                text_input_->set_window_focused(true);
            // input thread에는 **활성 표면이 바뀌었다**고 알린다. 초점을 세우는
            // 것이 아니라 가둠 진입이 고를 tree를 정하는 값이다
            // (active-surface-design.md).
            context_.surface_focus_gained(id_);
            return std::nullopt;
        case WM_KILLFOCUS:
            // **웹뷰가 가져간 초점은 창이 초점을 잃은 것이 아니다.**
            // 이 저장소에는 웹뷰 말고 자식 창이 없으므로 "자식이 가져갔는가"가 곧
            // 그 판정이다. 창은 여전히 활성이라 popup도 살아 있어야 하고, TSF
            // composition도 끝내지 않는다 — 끝내면 캐럿이 영영 사라진다.
            if (const HWND taker { reinterpret_cast<HWND>(word_parameter) };
                window_ != nullptr && taker != nullptr && IsChild(window_, taker) != FALSE)
            {
                webview_focused_ = true;
                return LRESULT { 0 };
            }
            // 먼저 이전 target이 살아 있을 때 실제 TSF composition을 끝낸다.
            // 그 뒤 input thread에도 알려 이 표면의 text focus를 거둔다.
            if (text_input_ != nullptr)
                text_input_->set_window_focused(false);
            // 이 표면에 붙은 popup의 초점도 함께 거둔다 — popup은 자기 focus가
            // 없어 이 창의 keyboard focus에 얹혀 있었다. 어느 popup이 붙어
            // 있는지는 소유자만 안다 (popup-ime-design.md).
            context_.surface_focus_lost(id_);
            return std::nullopt;
        case WM_GETOBJECT:
            // 언제든 오는 OS 질문은 표면이 든 최신 값으로 답한다.
            // UIA 밖의 질문(다른 object id)은 기본 처리에 맡긴다.
            if (window_ != nullptr && static_cast<LONG>(long_parameter) == static_cast<LONG>(UiaRootObjectId))
            {
                if (uia_root_ == nullptr)
                    uia_root_ = new uia_root_provider { *this };
                // 기준선이 없으면 현재 발행본으로 초기화한다.
                // SendMessage 재진입 경로이므로 여기서는 변경 알림을 보내지 않는다.
                // 첫 WM_GETOBJECT가 첫 frame 전에 올 수 있어 매 조회마다 초기화를 시도한다.
                seed_accessibility_baseline();
                return UiaReturnRawElementProvider(window_, word_parameter, long_parameter, uia_root_);
            }
            return std::nullopt;
        case access_dispatch_message: {
            // 계획은 이미 끝났다. 여기서는 통상 규칙대로 배분만 한다.
            std::vector<input_action> pending {};
            pending.swap(pending_access_actions_);
            for (input_action& action : pending)
                context_.dispatch_action(std::move(action));
            return LRESULT { 0 };
        }
        case WM_ERASEBKGND:
            return LRESULT { 1 };
        case WM_PAINT: {
            PAINTSTRUCT paint {};
            BeginPaint(window_, &paint);
            std::u8string error {};
            const bool rendered { render(error) };
            EndPaint(window_, &paint);
            if (rendered == false)
                on_render_failed(error);
            return LRESULT { 0 };
        }
        default:
            return std::nullopt;
        }
    }

    bool window_surface::build_frame(frame_state& state, std::shared_ptr<const ui_frame>& frame, sk_sp<SkTypeface>& code, std::u8string& error)
    {
        RECT client {};
        if (GetClientRect(window_, &client) == FALSE)
        {
            error = u8"Failed to read the client area before rendering.";
            return false;
        }

        state.width = std::max(1L, client.right - client.left);
        state.height = std::max(1L, client.bottom - client.top);
        state.dpi_scale = static_cast<float>(dpi_) / 96.0F;
        // 최대화는 OS에 묻는다. 전체 화면은 여기서 알 수 없는 상태라 기본값(거짓)으로
        // 두고, 그 상태를 든 표면이 `prepare_frame`에서 둘을 한 모드로 함께 세운다
        // (`caption_surface::prepare_frame` — 둘이 동시에 참이 되지 않는 자리다).
        state.maximized = IsZoomed(window_) != FALSE;

        if (app_host* const host { context_.host() }; host != nullptr)
        {
            frame = host->acquire_frame();
            // **이 표면이 볼 상호작용 상태다.** 발행본은 하나이고 모든 표면이
            // 같은 것을 집으므로, 남의 표면에서 난 hover·눌림·초점·메뉴 강조·
            // 끌기를 여기서 지운다 — `ui_element_id`는 tree 안에서만 안정적이라
            // 두 표면에 같은 id가 사는 것이 유효하다 (multi-window-design.md).
            //  - 거르는 자리가 여기인 이유: 표면 id를 아는 것은 이쪽뿐이다.
            //    `ui_tree::draw`도 element도 자기가 어느 창에 그려지는지 모른다.
            //  - **`prepare_frame`보다 먼저다.** caption 표면은 비클라이언트
            //    hover를 그 뒤에 얹으므로, 순서가 뒤집히면 방금 얹은 것이 도로
            //    지워진다.
            state.interaction = interaction_for_surface(host->acquire_interaction(), id_);
        }
        code = context_.apply_frame_appearance(state, frame.get());
        state.tree = tree_.get();
        prepare_frame(state);
        return true;
    }

    bool window_surface::render(std::u8string& error)
    {
        if (renderer_ == nullptr)
        {
            error = u8"The surface has no renderer.";
            return false;
        }

        frame_state state {};
        std::shared_ptr<const ui_frame> frame {};
        // typeface는 이 호출이 끝날 때까지 살아 있어야 한다.
        sk_sp<SkTypeface> code {};
        if (build_frame(state, frame, code, error) == false)
            return false;

        // 초점 변화는 언제나 발행본 게시로 오고 게시는 모든 표면을 깨우므로,
        // 그리는 길이 곧 알리는 길이다 (accessibility-design.md).
        announce_accessibility_focus(state.interaction.focused);
        // 웹뷰를 자리에 앉히고 비울 자리를 받는다. **`prepare_frame` 뒤라야 한다** —
        // 주 표면은 자기 tree를 거기서 집으므로, 앞에 두면 한 frame 낡은 tree에서
        // 자리표를 찾게 된다.
        state.holes = context_.apply_webviews(id_, state.tree, renderer_->underlay(), state.width, state.height);
        // 값·상태·구조 변경을 prepare_frame 뒤에 직전 접근성 발행본과 비교한다.
        // prepare_frame 전에 비교하면 주 표면이 이전 tree를 사용해 변경 알림을 놓친다.
        announce_accessibility_changes();

        // 이 표면의 초점과 확정된 글이 바뀌었을 수 있다.
        // 조합 중이면 session이 미룬다.
        if (text_input_ != nullptr)
            text_input_->synchronize();
        return renderer_->render(state, error);
    }

    bool window_surface::capture(const std::u8string& path, std::u8string& error)
    {
        if (window_ == nullptr)
        {
            error = u8"The surface has no window to capture.";
            return false;
        }

        frame_state state {};
        std::shared_ptr<const ui_frame> frame {};
        sk_sp<SkTypeface> code {};
        if (build_frame(state, frame, code, error) == false)
            return false;

        // 알리지 않는다. 캡처는 **보는 일**이라 접근성 발행본의 기준선을 옮기면
        // 다음 그리기가 그 사이의 변화를 놓친다 (`announce_accessibility_changes`는
        // 직전 발행본과 대조하고 그 자리에서 기준선을 새로 잡는다).
        //
        // 웹뷰 자리도 비우지 않는다. 그 그림은 합성이 우리 **아래**에 얹는 것이라
        // 어차피 담기지 않고, 구멍을 내면 그 자리가 검게 남는다.
        const SkImageInfo info { SkImageInfo::MakeN32Premul(state.width, state.height) };
        const SkSurfaceProps properties { 0, kRGB_H_SkPixelGeometry };
        const sk_sp<SkSurface> surface { SkSurfaces::Raster(info, &properties) };
        if (surface == nullptr)
        {
            error = u8"Failed to allocate the capture surface.";
            return false;
        }

        // 렌더러가 부르는 것과 **같은 인자**다. codicon 자리에 code(고정폭) typeface를
        // 넘기면 캡션 버튼처럼 글리프로 그리는 것만 조용히 사라진다 — 나머지는
        // 멀쩡해서 캡처를 눈으로 볼 때에야 드러난다.
        //  - `code`는 `state.code_typeface`가 가리키는 것을 살려 두는 몫이다.
        const sk_sp<SkTypeface> codicon { load_codicon_typeface() };
        const sk_sp<SkTypeface> ui { configured_ui_typeface() };
        draw_frame(*surface->getCanvas(), codicon.get(), ui.get(), state);

        SkPixmap pixels {};
        if (surface->peekPixels(&pixels) == false)
        {
            error = u8"Failed to read the captured pixels.";
            return false;
        }

        const auto native { utf8_to_utf16(path) };
        if (native.value.has_value() == false)
        {
            error = u8"The capture path is not valid UTF-8.";
            return false;
        }
        // `SkFILEWStream`은 좁은 경로만 받아 한글 경로에서 끊긴다.
        // 파일은 Win32로 열고 메모리에 담은 것을 그대로 쓴다.
        SkDynamicMemoryWStream memory {};
        // **무손실이다.** 화면을 다시 볼 값이라 손실 압축은 글자 가장자리를
        // 뭉갠다. PNG가 아닌 이유는 이 Skia 패키지에 png 인코더가 없기 때문이다
        // (webp는 있다 — `skia_use_libwebp_encode`).
        SkWebpEncoder::Options options {};
        options.fCompression = SkWebpEncoder::Compression::kLossless;
        if (SkWebpEncoder::Encode(&memory, pixels, options) == false)
        {
            error = u8"Failed to encode the capture.";
            return false;
        }

        const sk_sp<SkData> encoded { memory.detachAsData() };
        const HANDLE file { CreateFileW(native.value->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (file == INVALID_HANDLE_VALUE)
        {
            error = make_hresult_error(u8"Failed to create the capture file.", HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }

        DWORD written { 0 };
        const BOOL wrote { WriteFile(file, encoded->data(), static_cast<DWORD>(encoded->size()), &written, nullptr) };
        const DWORD failure { GetLastError() };
        CloseHandle(file);
        if (wrote == FALSE || written != encoded->size())
        {
            error = make_hresult_error(u8"Failed to write the capture file.", HRESULT_FROM_WIN32(failure));
            return false;
        }
        return true;
    }

    void window_surface::on_render_failed(const std::u8string& error)
    {
        context_.report_error(error);
    }

    bool window_surface::post_pointer_message(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        app_host* const host { context_.host() };
        if (host == nullptr)
            return false;
        POINT point { GET_X_LPARAM(long_parameter), GET_Y_LPARAM(long_parameter) };
        // 휠의 lparam만 화면 좌표다.
        // 나머지는 이미 그 표면의 client 좌표다.
        if (message == WM_MOUSEWHEEL && ScreenToClient(window_, &point) == FALSE)
            return false;

        // **웹뷰가 먼저다.** 그 자리의 포인터는 웹 콘텐츠의 것이고, 같은 포인터를
        // 양쪽에 주면 웹뷰 위에서 굴린 휠이 뒤의 목록도 함께 스크롤한다.
        //  - 자식 창이 0x0이라 마우스 메시지가 전부 우리에게 온다. 넘기지 않으면
        //    페이지는 아무것도 받지 못한다.
        if (context_.relay_webview_pointer(id_, message, word_parameter, point.x, point.y))
            return true;

        pointer_message translated {};
        translated.message = message;
        translated.word_parameter = word_parameter;
        translated.x = static_cast<float>(point.x);
        translated.y = static_cast<float>(point.y);
        translated.time = std::chrono::steady_clock::now();
        translated.surface = id_;
        std::optional<raw_input_event> event { translate_pointer_message(translated) };
        if (event.has_value() == false)
            return false;
        host->post_raw_input(std::move(*event));
        return true;
    }

    void window_surface::capture_pointer() noexcept
    {
        SetCapture(window_);
        context_.set_pointer_capture_active(GetCapture() == window_);
    }

    void window_surface::release_pointer_capture() noexcept
    {
        context_.set_pointer_capture_active(false);
        ReleaseCapture();
    }

    bool window_surface::pointer_capture_lost(const LPARAM long_parameter) noexcept
    {
        if (context_.pointer_capture_active() == false || reinterpret_cast<HWND>(long_parameter) == window_)
            return false;
        context_.set_pointer_capture_active(false);
        return true;
    }

    void window_surface::cancel_pointer_press()
    {
        context_.cancel_webview_pointer(id_);
        app_host* const host { context_.host() };
        if (host == nullptr)
            return;
        pointer_released_event event {};
        event.x = -10000.0f;
        event.y = -10000.0f;
        event.button = pointer_button::left;
        event.time = std::chrono::steady_clock::now();
        event.surface = id_;
        host->post_raw_input(std::move(event));
    }

    bool window_surface::post_key_event(const WPARAM virtual_key, const LPARAM long_parameter)
    {
        app_host* const host { context_.host() };
        if (host == nullptr)
            return false;
        const bool control { (GetKeyState(VK_CONTROL) & 0x8000) != 0 };
        const bool shift { (GetKeyState(VK_SHIFT) & 0x8000) != 0 };
        const bool alt { (GetKeyState(VK_MENU) & 0x8000) != 0 };
        key_code key { named_key_from_virtual(virtual_key) };
        if (key == key_code::none)
        {
            // 문자를 만드는 키인지는 layout이 안다 (번역은 layout을 모른다).
            // 죽은 키(dead key) 표시 비트는 무시한다.
            const bool character_key { (MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_CHAR) & 0x7FFFFFFFu) != 0 };
            key = modified_key_from_virtual(virtual_key, control, alt, character_key);
        }
        if (key == key_code::none)
            return false;
        // bit 30은 "이전에도 눌려 있었다" — 자동 반복이다.
        const bool repeat { (long_parameter & (1LL << 30)) != 0 };
        // 이 표면이 나른 키다 — 포인터·파일 끌기와 같은 표식이다.
        // 초점이 없을 때 Tab·Enter·Esc가 어느 tree에서 시작할지의 근거가 된다
        // (key-surface-routing-design.md).
        host->post_raw_input(key_pressed_event { key, control, shift, alt, repeat, std::chrono::steady_clock::now(), id_ });
        return true;
    }

    void window_surface::track_mouse_leave() noexcept
    {
        if (tracking_mouse_)
            return;
        TRACKMOUSEEVENT tracking {
            static_cast<DWORD>(sizeof(TRACKMOUSEEVENT)),
            TME_LEAVE,
            window_,
            HOVER_DEFAULT,
        };
        tracking_mouse_ = TrackMouseEvent(&tracking) != FALSE;
    }

    void window_surface::apply_cursor()
    {
        apply_surface_cursor(window_, tree_.get(), context_.host(), context_.config(), id_);
    }

    surface_tsf_host::surface_tsf_host(surface_context& context, const window_surface& surface) noexcept
        : context_ { &context }
        , surface_ { &surface }
    {}

    // 이 host가 TSF로 대신 볼 표면이다.
    // popup은 keyboard focus를 받지 못해 자기 session이 없으므로, 자기에게
    // 붙은 popup에 초점이 있으면 앵커인 이 표면이 대신 본다
    // (popup-ime-design.md).
    const window_surface* surface_tsf_host::text_surface() const
    {
        if (context_->host() == nullptr)
            return nullptr;
        const std::u8string focused { context_->host()->acquire_interaction().focused_surface };
        if (focused == surface_->id())
            return surface_;
        // 그 밖의 표면은 그 표면의 몫이다 — 붙은 popup만 대신 본다.
        return context_->anchored_popup(surface_->id(), focused);
    }

    std::optional<text_input_target> surface_tsf_host::focused_text_target() const
    {
        if (context_->host() == nullptr || context_->policy() == nullptr || text_surface() == nullptr)
            return std::nullopt;
        // **웹뷰가 Win32 초점을 쥐고 있으면 TSF를 양보한다.**
        //
        // WebView2는 우리 스레드의 **같은** thread manager에 자기 document manager를
        // 얹는다. 그동안 우리가 초점을 가져오면 `SetFocus`가 S_OK로 성공하면서
        // 웹뷰의 IME가 조용히 죽는다 — 한글 조합 중이면 이어 친 키가 날 라틴으로
        // 들어간다.
        //  - 오류가 아니라 조용한 양보다. 웹뷰가 자기 IME를 갖는 것이 옳고, 우리가
        //    되찾을 계기는 사용자가 우리 쪽을 누르는 것뿐이다.
        if (surface_ != nullptr && surface_->webview_has_focus())
            return std::nullopt;
        return context_->policy()->text_target_of(context_->host()->acquire_interaction().focused_input.kind);
    }

    shadow_document surface_tsf_host::committed_document() const
    {
        const window_surface* const surface { text_surface() };
        const ui_element* const element { surface != nullptr ? focused_element(*surface) : nullptr };
        if (element == nullptr)
            return {};
        const std::optional<text_input_snapshot> value { element->text_input() };
        if (value.has_value() == false)
            return {};
        return { std::u8string { value->text }, value->caret, value->anchor };
    }

    // 그림자 문서의 byte 구간이 화면에서 차지하는 자리다.
    // 글꼴 크기·안쪽 여백·가로 스크롤은 element가 계산한다
    // (`ui_element::text_span_bounds`) — 여기서 상수를 다시 들면
    // element가 값을 바꿀 때 후보 창 자리만 조용히 틀어진다.
    //
    // 화면으로 옮기는 창은 **그 element가 사는 표면**의 창이다.
    // element가 재는 것은 자기 tree의 좌표라, popup에 초점이 있는데 앵커 창으로
    // 옮기면 후보 창이 popup 자리만큼 어긋난다 (popup-ime-design.md).
    std::optional<RECT> surface_tsf_host::text_screen_rect(const shadow_document& document, const std::size_t begin, const std::size_t end) const
    {
        const window_surface* const surface { text_surface() };
        const ui_element* const element { surface != nullptr ? focused_element(*surface) : nullptr };
        if (element == nullptr)
            return std::nullopt;

        text_measurer measure {};
        measure = [](const std::u8string_view text, const float pixel_size) {
            const SkFont font { configured_ui_typeface(), pixel_size };
            return measure_text(text, font);
        };
        const std::optional<rect_f> span { element->text_span_bounds(text_span_query { document.text, document.caret, begin, end }, measure) };
        if (span.has_value() == false)
            return std::nullopt;

        POINT top_left { static_cast<LONG>(span->x), static_cast<LONG>(span->y) };
        POINT bottom_right { static_cast<LONG>(span->x + span->width), static_cast<LONG>(span->y + span->height) };
        if (bottom_right.x < top_left.x)
            bottom_right.x = top_left.x;
        if (ClientToScreen(surface->window(), &top_left) == FALSE || ClientToScreen(surface->window(), &bottom_right) == FALSE)
            return std::nullopt;
        return RECT { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
    }

    void surface_tsf_host::post_composition(text_composition_event event)
    {
        if (context_->policy() != nullptr)
            context_->dispatch_action(context_->policy()->make_text_composition_action(std::move(event)));
    }

    void surface_tsf_host::post_edit(text_edit_request request)
    {
        if (context_->policy() != nullptr)
            context_->dispatch_action(context_->policy()->make_text_edit_action(std::move(request)));
    }

    const ui_element* surface_tsf_host::focused_element(const window_surface& surface) const
    {
        if (focused_text_target().has_value() == false || surface.tree() == nullptr)
            return nullptr;
        return surface.tree()->find(context_->host()->acquire_interaction().focused_input);
    }
} // namespace luil::win32

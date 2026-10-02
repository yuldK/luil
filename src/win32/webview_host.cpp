#include "win32/webview_host.h"

#include "win32/popup_reconcile.h"
#include "win32/utf8.h"
#include "win32/webview_message_gate.h"
#include "win32/win32_error.h"

#include <windowsx.h>

#include <dcomp.h>

#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>

namespace luil::win32 {
    namespace {
        using Microsoft::WRL::Callback;
        using Microsoft::WRL::ComPtr;

        // WebView2가 `CoTaskMemAlloc`으로 준 문자열의 수명이다.
        struct wil_string_scope
        {
            LPWSTR value { nullptr };
            wil_string_scope() = default;
            wil_string_scope(const wil_string_scope&) = delete;
            wil_string_scope(wil_string_scope&&) = delete;
            wil_string_scope& operator=(const wil_string_scope&) = delete;
            wil_string_scope& operator=(wil_string_scope&&) = delete;
            ~wil_string_scope()
            {
                if (value != nullptr)
                    CoTaskMemFree(value);
            }
        };

        // 만들기가 비동기라 완료 콜백이 몇 frame 뒤에 온다.
        enum class creation_state
        {
            pending,
            ready,
            failed,
        };

        // 여는 것을 허락받은 스킴인가.
        // 목록이 비어 있으면 `https`만이다 (webview.h).
        //  - `javascript:`는 여기서 걸러도 실제 네비게이션이 아니라 이벤트가 나지
        //    않는다. 그래서 막는 자리가 **여는 호출부**여야 한다 — 그 자리가 여기다.
        [[nodiscard]] bool scheme_allowed(const std::u8string& url, const std::vector<std::u8string>& allowed)
        {
            const std::size_t colon { url.find(u8':') };
            if (colon == std::u8string::npos || colon == 0)
                return false;
            std::u8string scheme { url.substr(0, colon) };
            for (char8_t& letter : scheme)
                if (letter >= u8'A' && letter <= u8'Z')
                    letter = static_cast<char8_t>(letter - u8'A' + u8'a');
            // 목록에 넣어도 듣지 않는다 (webview.h) — 여는 것이 아니라 실행하는 것이다.
            if (scheme == u8"javascript")
                return false;
            if (allowed.empty())
                return scheme == u8"https";
            return std::find(allowed.begin(), allowed.end(), scheme) != allowed.end();
        }

        // 이 라이브러리가 기대는 가장 새 인터페이스의 런타임 판번이다 (SDK 1.0.NNNN 축).
        //
        // 정책을 집행하는 인터페이스가 없는 런타임은 **조용히 열린 채**로 돈다 —
        // `ICoreWebView2_4`가 없으면 내려받기가 대화상자와 함께 진행되고,
        // `PermissionRequestedEventArgs3`가 없으면 거절이 프로필에 남고,
        // `NewWindowRequestedEventArgs3`가 없으면 요청이 어느 frame에서 왔는지 모른다.
        // 그래서 그보다 오래된 런타임에서는 웹뷰를 세우지 않는다 (placeholder가 남는다).
        // 2210은 `NewWindowRequestedEventArgs3`(SDK 1.0.2210.55, Chromium 120)이다.
        constexpr unsigned minimum_runtime_build { 2210 };

        // 브라우저 판번("152.0.4191.62")의 세 번째 수다 — SDK 판번의 세 번째 수와 같은
        // 축이라 "1.0.NNNN의 인터페이스가 있는가"를 이것으로 판정한다. 읽지 못하면 0이다.
        [[nodiscard]] unsigned runtime_build_number(const wil_string_scope& version) noexcept
        {
            if (version.value == nullptr)
                return 0;
            unsigned component { 0 };
            unsigned value { 0 };
            for (const wchar_t* cursor { version.value };; ++cursor)
            {
                if (*cursor >= L'0' && *cursor <= L'9')
                {
                    value = value * 10 + static_cast<unsigned>(*cursor - L'0');
                    continue;
                }
                if (component == 2)
                    return value;
                if (*cursor != L'.')
                    return 0;
                ++component;
                value = 0;
            }
        }

        // 임자 없는 컨트롤러를 닫는다 (완료가 왔는데 항목이 없거나 다른 시작의 것일 때).
        void close_orphan(ICoreWebView2CompositionController* const created) noexcept
        {
            if (created == nullptr)
                return;
            ComPtr<ICoreWebView2Controller> controller {};
            if (SUCCEEDED(created->QueryInterface(IID_PPV_ARGS(&controller))) && controller != nullptr)
                static_cast<void>(controller->Close());
        }

        // WebView2가 준 문자열을 UTF-8로 옮긴다. 없거나 옮기지 못하면 비어 있다.
        [[nodiscard]] std::u8string to_utf8(const wil_string_scope& text)
        {
            if (text.value == nullptr)
                return {};
            const auto converted { utf16_to_utf8(text.value) };
            return converted.value.has_value() ? *converted.value : std::u8string {};
        }

        // 권한의 이름이다 (`webview_event::error`에 실린다). 모르는 갈래는 그대로 말한다.
        [[nodiscard]] std::u8string permission_name(const COREWEBVIEW2_PERMISSION_KIND kind)
        {
            switch (kind)
            {
            case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:
                return u8"microphone";
            case COREWEBVIEW2_PERMISSION_KIND_CAMERA:
                return u8"camera";
            case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:
                return u8"geolocation";
            case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
                return u8"notifications";
            case COREWEBVIEW2_PERMISSION_KIND_OTHER_SENSORS:
                return u8"other sensors";
            case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
                return u8"clipboard read";
            case COREWEBVIEW2_PERMISSION_KIND_MULTIPLE_AUTOMATIC_DOWNLOADS:
                return u8"multiple automatic downloads";
            case COREWEBVIEW2_PERMISSION_KIND_FILE_READ_WRITE:
                return u8"file read write";
            case COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY:
                return u8"autoplay";
            case COREWEBVIEW2_PERMISSION_KIND_LOCAL_FONTS:
                return u8"local fonts";
            case COREWEBVIEW2_PERMISSION_KIND_MIDI_SYSTEM_EXCLUSIVE_MESSAGES:
                return u8"midi system exclusive messages";
            case COREWEBVIEW2_PERMISSION_KIND_WINDOW_MANAGEMENT:
                return u8"window management";
            case COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE:
                return u8"persistent storage";
            default:
                return u8"unknown permission";
            }
        }

        void apply_settings(ICoreWebView2& view, const webview_policy& policy)
        {
            ComPtr<ICoreWebView2Settings> settings {};
            if (FAILED(view.get_Settings(&settings)) || settings == nullptr)
                return;

            static_cast<void>(settings->put_IsScriptEnabled(policy.script_enabled ? TRUE : FALSE));
            static_cast<void>(settings->put_AreDevToolsEnabled(policy.developer_tools_enabled ? TRUE : FALSE));
            static_cast<void>(settings->put_AreDefaultContextMenusEnabled(policy.default_context_menus_enabled ? TRUE : FALSE));
            static_cast<void>(settings->put_AreDefaultScriptDialogsEnabled(policy.default_script_dialogs_enabled ? TRUE : FALSE));
            // 다리는 web message 하나다.
            static_cast<void>(settings->put_IsWebMessageEnabled(TRUE));
            // **일반 proxy는 열지 않는다.** 기본이 TRUE라 끄지 않으면 늘 열려 있고,
            // 공개 API에 다시 켜는 스위치가 없는 것이 그 결정이다 (webview.h).
            static_cast<void>(settings->put_AreHostObjectsAllowed(FALSE));

            ComPtr<ICoreWebView2Settings3> settings3 {};
            if (SUCCEEDED(settings.As(&settings3)) && settings3 != nullptr)
                static_cast<void>(settings3->put_AreBrowserAcceleratorKeysEnabled(policy.browser_accelerator_keys_enabled ? TRUE : FALSE));

            ComPtr<ICoreWebView2Settings5> settings5 {};
            if (SUCCEEDED(settings.As(&settings5)) && settings5 != nullptr)
                static_cast<void>(settings5->put_IsPinchZoomEnabled(policy.pinch_zoom_enabled ? TRUE : FALSE));

            ComPtr<ICoreWebView2Settings6> settings6 {};
            if (SUCCEEDED(settings.As(&settings6)) && settings6 != nullptr)
                static_cast<void>(settings6->put_IsSwipeNavigationEnabled(policy.swipe_navigation_enabled ? TRUE : FALSE));
        }
    } // namespace

    // 폴더 하나에 대응하는 환경이다.
    struct webview_host::environment
    {
        std::u8string user_data_folder {};
        creation_state state { creation_state::pending };
        ComPtr<ICoreWebView2Environment> value {};
    };

    // 살아 있는 웹뷰 하나다.
    struct webview_host::entry
    {
        std::u8string id {};
        std::u8string anchor {};
        std::u8string user_data_folder {};
        webview_policy policy {};
        creation_state state { creation_state::pending };
        // 컨트롤러 만들기가 이미 떠났는가.
        //
        // 만들기는 비동기라 시작과 완료 사이에 여러 게시가 지나간다. 그 사이에
        // 다시 시작하면 컨트롤러가 둘 생기고, **먼저 것이 받은 명령이 뒤엣것에
        // 덮여 사라진다** — 첫 문서가 열리지 않는 자리가 정확히 그것이었다.
        bool starting { false };
        // 마지막으로 시작한 만들기의 번호다. 완료가 이것과 다르면 옛 시작의 답이다.
        std::uint64_t creation_serial { 0 };
        // 이 웹뷰가 앉은 창이다. 컨트롤러의 parentWindow가 그것이다.
        HWND window { nullptr };
        // 렌더러가 마지막으로 죽은 시각이다 (`GetTickCount64`). 다시 읽기를 잇달아
        // 하지 않는 근거다 — 열자마자 렌더러를 죽이는 페이지는 다시 읽는 만큼 다시 죽는다.
        std::optional<std::uint64_t> last_render_failure_ms {};

        ComPtr<ICoreWebView2CompositionController> composition_controller {};
        ComPtr<ICoreWebView2Controller> controller {};
        ComPtr<ICoreWebView2> view {};
        // 이 웹뷰가 그려지는 visual이다. 표면의 underlay 아래에 든다.
        ComPtr<IDCompositionVisual> visual {};
        // 표면의 underlay visual이 바뀌면 웹뷰를 새 visual에 연결한다.
        // 렌더러가 먼저 소멸해도 RemoveVisual을 안전하게 호출하도록 COM 참조를 유지한다.
        ComPtr<IDCompositionVisual> attached_to {};
        // 지금 실제로 그려지고 있는가.
        //
        // 붙어 있는 것과 보이는 것은 다르다 — 감출 때 visual을 **떼지 않고**
        // `IsVisible`만 끄기 때문이다. 떼었다 붙이면 `put_RootVisualTarget`을 다시
        // 걸어야 하고 그때 페이지가 빈다.
        bool shown { false };
        UINT pressed_buttons { 0 };
        // 이 웹뷰가 쥔 터치·펜 접촉이다 (DOWN을 받은 웹뷰가 끝까지 갖는다).
        // 종류를 함께 든다 — 취소를 보낼 때는 그 접촉의 원본이 더 없다.
        std::vector<std::pair<UINT32, POINTER_INPUT_TYPE>> pointer_contacts {};
        // 이 웹뷰 위에 떠 있는 펜이다. 떠나면 LEAVE를 준다.
        std::vector<UINT32> pointer_hovers {};
        // 한 번이라도 그렸는가.
        //
        // 만들고 보이게 한 직후의 visual은 **비어 있다.** 그때 자리를 비우면 그 아래에
        // 아무것도 없어 바탕 화면이 비친다 — 페이지가 처음 뜨는 동안의 구멍이다.
        //  - `NavigationCompleted`를 그 표식으로 쓴다. 실패해도 참으로 둔다:
        //    WebView2가 자기 오류 페이지를 그리므로 그 자리도 이미 칠해져 있다.
        //  - 아무 데도 가지 않는 웹뷰는 영영 거짓이고, 그동안 자리표의 placeholder가
        //    그 자리를 지킨다 (webview_element.h).
        bool painted { false };
        // 마지막으로 적용한 자리다. 같으면 다시 적용하지 않는다.
        webview_layout applied {};
        bool has_applied { false };

        // 앱이 마지막으로 **원한** 명령이다. 만들기가 비동기라 컨트롤러가 서기 전에
        // 오는 것이 보통이므로 기억해 두었다가 준비되면 흘려보낸다.
        //  - 기억하지 않으면 명령이 지나간 뒤 새 frame이 오지 않는 화면에서 영영
        //    실행되지 않는다. 첫 문서를 여는 것이 정확히 그 자리다.
        // 사건을 앱 메시지로 옮기는 factory다 (앱이 준다).
        std::function<app_message(webview_event)> on_event {};
        std::uint64_t wanted_navigate_revision { 0 };
        std::u8string wanted_navigate_url {};
        std::uint64_t wanted_post_revision { 0 };
        std::u8string wanted_post_message {};
        // 실제로 실행한 것이다.
        std::uint64_t navigate_revision { 0 };
        std::uint64_t post_revision { 0 };
        // 우리가 막은 항해다. 막힌 항해도 `NavigationCompleted`가 취소로 한 번 더
        // 오는데, 막았다고 이미 알렸으므로 그 끝은 알리지 않는다.
        std::vector<UINT64> cancelled_navigations {};
        // 페이지가 일으키는 사건의 상한이다 (webview.h의 `maximum_events_per_second`).
        webview_message_gate gate {};
    };

    webview_host::webview_host() = default;

    void webview_host::set_deliver(std::function<void(app_message)> deliver)
    {
        deliver_ = std::move(deliver);
    }

    void webview_host::set_focus_reporter(std::function<void(const std::u8string&, const std::u8string&, webview_focus_signal)> reporter)
    {
        focus_reporter_ = std::move(reporter);
    }

    bool webview_host::move_focus_in(const std::u8string& id, const bool backward)
    {
        entry* const target { find(id) };
        if (target == nullptr || target->controller == nullptr || target->shown == false)
            return false;
        // 방향을 그대로 넘긴다 — 앞으로 들어가면 페이지의 첫 자리, 뒤로 들어가면
        // 마지막 자리다.
        return SUCCEEDED(target->controller->MoveFocus(backward ? COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS : COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT));
    }

    void webview_host::signal_focus(const entry& source, const webview_focus_signal signal)
    {
        if (focus_reporter_ != nullptr)
            focus_reporter_(source.id, source.anchor, signal);
    }

    void webview_host::report(entry& source, webview_event event, const std::size_t bytes)
    {
        if (source.on_event == nullptr || deliver_ == nullptr)
            return;
        event.id = source.id;
        // 페이지가 일으킬 수 있는 사건만 상한을 지난다 (헤더의 이유).
        const bool process_failure { event.kind == webview_event_kind::render_process_failed || event.kind == webview_event_kind::browser_process_failed };
        const bool gated { event.kind != webview_event_kind::events_dropped && process_failure == false };
        if (gated)
        {
            const webview_message_decision decision {
                source.gate.admit(GetTickCount64(), bytes, source.policy.maximum_message_bytes, source.policy.maximum_events_per_second),
            };
            if (decision.verdict != webview_message_verdict::accept)
            {
                if (decision.notify == false)
                    return;
                // 무엇을 버렸는지 한 마디다 — 창마다 한 번뿐이라 **처음 버린 것**이다.
                std::u8string reason {};
                switch (event.kind)
                {
                case webview_event_kind::web_message_received:
                    reason = u8"web message";
                    break;
                case webview_event_kind::permission_denied:
                    reason = u8"permission request";
                    break;
                case webview_event_kind::download_blocked:
                    reason = u8"download";
                    break;
                default:
                    reason = u8"navigation";
                    break;
                }
                reason += decision.verdict == webview_message_verdict::drop_size ? u8": size" : u8": rate";
                deliver_(source.on_event(webview_event { source.id, webview_event_kind::events_dropped, std::move(event.url), {}, std::move(reason) }));
                return;
            }
        }
        deliver_(source.on_event(std::move(event)));
    }

    void webview_host::report(entry& source, const webview_event_kind kind, std::u8string url, std::u8string error)
    {
        report(source, webview_event { {}, kind, std::move(url), {}, std::move(error) }, 0);
    }

    webview_host::~webview_host()
    {
        shutdown();
    }

    webview_host::entry* webview_host::find(const std::u8string& id) noexcept
    {
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->id == id)
                return candidate.get();
        return nullptr;
    }

    const webview_host::entry* webview_host::find(const std::u8string& id) const noexcept
    {
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->id == id)
                return candidate.get();
        return nullptr;
    }

    void webview_host::tear_down(entry& target) noexcept
    {
        // 붙여 둔 visual을 떼지 않으면 underlay에 빈 visual이 남는다.
        if (target.attached_to != nullptr && target.visual != nullptr)
        {
            static_cast<void>(target.attached_to->RemoveVisual(target.visual.Get()));
            target.attached_to = nullptr;
            if (composition_ != nullptr)
                static_cast<void>(composition_->Commit());
        }
        // 이미 닫힌 컨트롤러(브라우저 프로세스가 죽은 뒤)에 다시 닫으라고 해도 해가 없다.
        if (target.controller != nullptr)
            static_cast<void>(target.controller->Close());
    }

    void webview_host::fail(const std::u8string& id, std::u8string reason)
    {
        static_cast<void>(reason);
        if (std::find(failed_.begin(), failed_.end(), id) == failed_.end())
            failed_.push_back(id);
        // 없애지 않는다 — 다음 대조가 없앤다 (헤더의 이유). 실패한 항목은 더는 자리를
        // 받지도 포인터를 갖지도 않는다 (`state`를 보는 자리들).
        if (entry* const target { find(id) }; target != nullptr)
            target->state = creation_state::failed;
    }

    webview_host::environment* webview_host::acquire_environment(const std::u8string& user_data_folder)
    {
        for (auto slot_it = environments_.begin(); slot_it != environments_.end(); ++slot_it)
        {
            if ((*slot_it)->user_data_folder != user_data_folder)
                continue;
            if ((*slot_it)->state != creation_state::failed)
                return slot_it->get();
            // 실패한 환경은 잊고 다시 만든다 — 앱이 목록에서 뺐다가 다시 실었을 때
            // 새로 시도하는 길이 여기도 지나야 한다. 환경이 실패한 채 남으면 그 폴더의
            // 웹뷰는 영영 못 선다.
            environments_.erase(slot_it);
            break;
        }

        auto created { std::make_unique<environment>() };
        created->user_data_folder = user_data_folder;
        environment* const slot { created.get() };
        environments_.push_back(std::move(created));

        const auto folder { utf8_to_utf16(user_data_folder) };
        if (folder.value.has_value() == false)
        {
            slot->state = creation_state::failed;
            return slot;
        }

        // 완료 콜백은 이 thread의 메시지 pump로 돌아온다. 그 사이에 창이 죽을 수
        // 있으므로 살아 있음을 값으로 잡아 둔다 — `this`만 잡으면 죽은 host를 부른다.
        const std::shared_ptr<bool> alive { alive_ };
        webview_host* const self { this };
        const std::u8string key { user_data_folder };
        const HRESULT begun {
            CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.value->c_str(), nullptr,
                Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([alive, self, key](const HRESULT result, ICoreWebView2Environment* const created_environment) -> HRESULT {
                    if (*alive == false)
                        return S_OK;
                    self->finish_environment(key, result, created_environment);
                    return S_OK;
                }).Get()),
        };
        if (FAILED(begun))
            slot->state = creation_state::failed;
        return slot;
    }

    void webview_host::finish_environment(const std::u8string& user_data_folder, const HRESULT result, ICoreWebView2Environment* const created)
    {
        environment* slot { nullptr };
        for (const std::unique_ptr<environment>& candidate : environments_)
            if (candidate->user_data_folder == user_data_folder)
                slot = candidate.get();
        if (slot == nullptr)
            return;

        // 이 폴더를 쓰던 것은 전부 못 선다.
        const auto fail_all = [this, &user_data_folder, slot](std::u8string reason) {
            slot->state = creation_state::failed;
            for (const std::unique_ptr<entry>& candidate : entries_)
                if (candidate->user_data_folder == user_data_folder && candidate->state == creation_state::pending)
                    fail(candidate->id, reason);
        };
        if (FAILED(result) || created == nullptr)
        {
            fail_all(make_hresult_error(u8"Failed to create the WebView2 environment", result));
            return;
        }
        // 정책을 집행할 인터페이스가 없는 런타임에서는 세우지 않는다 (`minimum_runtime_build`).
        wil_string_scope version {};
        static_cast<void>(created->get_BrowserVersionString(&version.value));
        if (runtime_build_number(version) < minimum_runtime_build)
        {
            fail_all(u8"The WebView2 runtime is too old to enforce the webview policy.");
            return;
        }

        slot->state = creation_state::ready;
        slot->value = created;
        // 이 폴더를 기다리던 것들을 이제 만든다.
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->user_data_folder == user_data_folder && candidate->state == creation_state::pending && candidate->controller == nullptr)
                begin_controller(*candidate, candidate->window);
    }

    void webview_host::begin_controller(entry& target, const HWND window)
    {
        environment* slot { nullptr };
        for (const std::unique_ptr<environment>& candidate : environments_)
            if (candidate->user_data_folder == target.user_data_folder)
                slot = candidate.get();
        if (slot == nullptr || slot->state != creation_state::ready || slot->value == nullptr)
            return;
        if (target.starting || target.controller != nullptr)
            return;
        target.starting = true;
        target.creation_serial = ++creation_serial_;

        const std::shared_ptr<bool> alive { alive_ };
        webview_host* const self { this };
        const std::u8string id { target.id };
        const std::uint64_t serial { target.creation_serial };
        ComPtr<ICoreWebView2Environment3> environment3 {};
        if (FAILED(slot->value.As(&environment3)) || environment3 == nullptr)
        {
            fail(id, u8"This WebView2 runtime has no composition controller.");
            return;
        }

        const HRESULT begun {
            environment3->CreateCoreWebView2CompositionController(window,
                Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>([alive, self, id, serial](
                                                                                                   const HRESULT result, ICoreWebView2CompositionController* const created) -> HRESULT {
                    if (*alive == false)
                    {
                        close_orphan(created);
                        return S_OK;
                    }
                    self->finish_controller(id, serial, result, created);
                    return S_OK;
                }).Get()),
        };
        if (FAILED(begun))
            fail(id, make_hresult_error(u8"Failed to start the WebView2 controller", begun));
    }

    void webview_host::finish_controller(const std::u8string& id, const std::uint64_t serial, const HRESULT result, ICoreWebView2CompositionController* const created)
    {
        entry* const target { find(id) };
        // 항목이 그새 없어졌거나(앵커 표면이 한 frame 비었다) 없어졌다 다시 만들어졌으면
        // 이 완료의 임자가 없다. **닫지 않으면 브라우저 프로세스가 통째로 샌다** —
        // 아무도 가리키지 않는 컨트롤러는 프로세스가 끝날 때까지 산다.
        if (target == nullptr || target->creation_serial != serial || target->state != creation_state::pending)
        {
            close_orphan(created);
            return;
        }
        target->starting = false;
        if (FAILED(result) || created == nullptr)
        {
            fail(id, make_hresult_error(u8"Failed to create the WebView2 controller", result));
            return;
        }
        // 늦게 온 두 번째 완료는 버린다 (번호가 같을 수는 없지만, 안전망은 남긴다).
        if (target->controller != nullptr)
        {
            close_orphan(created);
            return;
        }

        target->composition_controller = created;
        if (FAILED(created->QueryInterface(IID_PPV_ARGS(&target->controller))) || target->controller == nullptr)
        {
            fail(id, u8"The WebView2 composition controller has no controller interface.");
            return;
        }
        if (FAILED(target->controller->get_CoreWebView2(&target->view)) || target->view == nullptr)
        {
            fail(id, u8"The WebView2 controller has no core view.");
            return;
        }

        apply_settings(*target->view.Get(), target->policy);

        // **웹뷰를 불투명하게 만든다.**
        //
        // 합성 호스팅에서 페이지가 배경을 정하지 않으면 그 자리가 알파 그대로
        // 합성된다. 우리는 그 자리를 이미 비워 두었으므로(구멍) 곧바로 **바탕 화면이
        // 비친다.** 창 바탕색을 깔아 그 길을 막는다 (`set_default_background`).
        //  - 페이지가 자기 배경을 정하는 것이 정상이고, 이 값은 정하지 않은 페이지의
        //    바닥일 뿐이다. 그 바닥이 창 바탕과 같아야 페이지가 뜨는 동안 판이 번쩍이지 않는다.
        apply_default_background(*target);

        // **배율은 우리가 소유한다.** 이 값을 끄지 않으면 `put_RasterizationScale`이
        // S_OK를 돌려주고도 아무 일도 하지 않는다.
        ComPtr<ICoreWebView2Controller3> controller3 {};
        if (SUCCEEDED(target->controller.As(&controller3)) && controller3 != nullptr)
        {
            static_cast<void>(controller3->put_ShouldDetectMonitorScaleChanges(FALSE));
            // 크기와 입력 좌표를 **물리 픽셀**로 못 박는다. 그래야 `put_Bounds`와
            // `SendMouseInput`의 단위가 우리 tree의 좌표(이미 배율이 곱해진 값)와
            // 같아져, 중계할 때 CSS로 환산할 일이 없다.
            static_cast<void>(controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS));
            // 배율은 우리가 준다. CSS 픽셀 = 물리 픽셀 / 이 값이다.
            //  - 이것은 **첫 자리를 받기 전의 바닥값**이다. 그 뒤로는 매 자리와
            //    함께 tree의 배율이 온다 (`apply_layout`). 여기서도 한 번 주는
            //    이유: 자리보다 첫 문서가 먼저 열리므로, 창의 배율로 시작해야
            //    첫 자리에서 리플로가 없다.
            const UINT dpi { GetDpiForWindow(target->window) };
            static_cast<void>(controller3->put_RasterizationScale(static_cast<double>(dpi != 0 ? dpi : 96) / 96.0));
        }

        // 자리를 받기 전에는 보이지 않는다. 첫 `apply_layout`이 켠다.
        static_cast<void>(target->controller->put_IsVisible(FALSE));
        target->state = creation_state::ready;
        // 무엇이 일어났는지 앱이 알 수 있어야 한다. 결정은 우리가 정책으로 내리고
        // (webview.h) 이 사건들은 그 결과의 사후 통지다.
        const std::shared_ptr<bool> alive { alive_ };
        webview_host* const self { this };
        const std::u8string key { id };
        EventRegistrationToken token {};
        static_cast<void>(target->view->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>([alive, self, key](ICoreWebView2* const source, ICoreWebView2NavigationCompletedEventArgs* const args) -> HRESULT {
                if (*alive == false)
                    return S_OK;
                entry* const target_entry { self->find(key) };
                if (target_entry == nullptr)
                    return S_OK;
                // 우리가 막은 항해의 끝은 알리지 않는다 — 막았다고 이미 알렸다.
                // 막힌 페이지는 그려지지 않았으므로 `painted`도 그대로 둔다.
                UINT64 navigation { 0 };
                if (args != nullptr && SUCCEEDED(args->get_NavigationId(&navigation)) && std::erase(target_entry->cancelled_navigations, navigation) > 0)
                    return S_OK;
                BOOL succeeded { FALSE };
                if (args != nullptr)
                    static_cast<void>(args->get_IsSuccess(&succeeded));
                std::u8string url {};
                if (source != nullptr)
                {
                    wil_string_scope current {};
                    if (SUCCEEDED(source->get_Source(&current.value)) && current.value != nullptr)
                        if (const auto text { utf16_to_utf8(current.value) }; text.value.has_value())
                            url = *text.value;
                }
                std::u8string reason {};
                if (succeeded == FALSE && args != nullptr)
                {
                    COREWEBVIEW2_WEB_ERROR_STATUS status { COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN };
                    static_cast<void>(args->get_WebErrorStatus(&status));
                    reason = u8"web error status ";
                    for (const char digit : std::to_string(static_cast<int>(status)))
                        reason.push_back(static_cast<char8_t>(digit));
                }
                target_entry->painted = true;
                self->report(*target_entry, succeeded != FALSE ? webview_event_kind::navigation_completed : webview_event_kind::navigation_failed, std::move(url), std::move(reason));
                return S_OK;
            }).Get(),
            &token));

        // --- 정책 ---
        //
        // 결정은 여기서 **동기로** 내린다 — 앱 메시지는 logic thread를 한 바퀴 도는
        // 값이라 제때 돌아오지 못한다 (webview.h). 앱은 결과를 사후 통지로 안다.

        // 스킴 검사의 본 그물이다. 앱이 여는 주소뿐 아니라 페이지가 스스로 가는
        // 주소(링크·리다이렉트)도 여기를 지난다 — 리다이렉트는 같은 id로 다시 온다.
        //  - `javascript:`는 실제 항해가 아니라 여기 오지 않는다. 그래서 여는
        //    호출부(`flush_commands`)가 먼저 거른다.
        //  - 주 문서만이다. iframe의 항해는 다른 이벤트라 이 그물 밖이다.
        static_cast<void>(target->view->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>([alive, self, key](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                entry* const target_entry { self->find(key) };
                // 임자가 없으면 **닫힌 쪽으로** 떨어진다 — 다른 정책 핸들러와 같다.
                if (target_entry == nullptr)
                {
                    static_cast<void>(args->put_Cancel(TRUE));
                    return S_OK;
                }
                wil_string_scope uri {};
                static_cast<void>(args->get_Uri(&uri.value));
                std::u8string url { to_utf8(uri) };
                if (scheme_allowed(url, target_entry->policy.allowed_schemes))
                {
                    self->report(*target_entry, webview_event_kind::navigation_started, std::move(url), {});
                    return S_OK;
                }
                static_cast<void>(args->put_Cancel(TRUE));
                UINT64 navigation { 0 };
                if (SUCCEEDED(args->get_NavigationId(&navigation)))
                {
                    // 끝이 오지 않은 항해가 쌓이지 않게 오래된 것부터 잊는다 — 페이지가
                    // 막히는 항해를 쏟아도 이 목록은 자라지 않는다.
                    constexpr std::size_t remembered_cancellations { 16 };
                    if (target_entry->cancelled_navigations.size() >= remembered_cancellations)
                        target_entry->cancelled_navigations.erase(target_entry->cancelled_navigations.begin());
                    target_entry->cancelled_navigations.push_back(navigation);
                }
                self->report(*target_entry, webview_event_kind::navigation_blocked, std::move(url), u8"scheme is not allowed");
                return S_OK;
            }).Get(),
            &token));

        // 새 창은 열지 않는다. 정책이 허락하면 **이 웹뷰가** 그 주소로 간다 — 그
        // 항해는 위의 그물을 그대로 지난다. `put_NewWindow`에 자기 자신을 주는 길은
        // 문서가 "아직 항해하지 않은 웹뷰"를 요구해 닫혀 있다.
        static_cast<void>(target->view->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>([alive, self, key](ICoreWebView2* const source, ICoreWebView2NewWindowRequestedEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                // **먼저 삼킨다.** 그래야 어느 갈래에서도 두 번째 창이 뜨지 않는다.
                static_cast<void>(args->put_Handled(TRUE));
                entry* const target_entry { self->find(key) };
                if (target_entry == nullptr)
                    return S_OK;
                wil_string_scope uri {};
                static_cast<void>(args->get_Uri(&uri.value));
                std::u8string url { to_utf8(uri) };
                if (target_entry->policy.open_new_windows_here == false)
                {
                    self->report(*target_entry, webview_event_kind::navigation_blocked, std::move(url), u8"new windows are not opened");
                    return S_OK;
                }
                // **주 문서가 사용자 손짓으로 낸 요청만 연다.** iframe이 낸 요청을 주
                // 문서의 항해로 바꾸면 남의 origin의 iframe에게 호스트 문서를 갈아
                // 끼울 힘을 주는 것이고, 손짓 없는 `window.open`은 브라우저의 팝업
                // 차단이 막던 것이다. 어느 frame인지 모르면(인터페이스 없음) 막는다.
                BOOL user_initiated { FALSE };
                static_cast<void>(args->get_IsUserInitiated(&user_initiated));
                bool from_main_frame { false };
                ComPtr<ICoreWebView2NewWindowRequestedEventArgs3> args3 {};
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args3))) && args3 != nullptr)
                {
                    ComPtr<ICoreWebView2FrameInfo> frame {};
                    ComPtr<ICoreWebView2FrameInfo2> frame2 {};
                    COREWEBVIEW2_FRAME_KIND frame_kind { COREWEBVIEW2_FRAME_KIND_UNKNOWN };
                    if (SUCCEEDED(args3->get_OriginalSourceFrameInfo(&frame)) && frame != nullptr && SUCCEEDED(frame.As(&frame2)) && frame2 != nullptr && SUCCEEDED(frame2->get_FrameKind(&frame_kind)))
                        from_main_frame = frame_kind == COREWEBVIEW2_FRAME_KIND_MAIN_FRAME;
                }
                if (user_initiated == FALSE || from_main_frame == false)
                {
                    self->report(*target_entry, webview_event_kind::navigation_blocked, std::move(url), u8"new windows are opened only from the main document's user gesture");
                    return S_OK;
                }
                // 스킴은 여기서도 먼저 거른다 — `javascript:`가 그물 밖인 것은 같다.
                if (source == nullptr || uri.value == nullptr || scheme_allowed(url, target_entry->policy.allowed_schemes) == false)
                {
                    self->report(*target_entry, webview_event_kind::navigation_blocked, std::move(url), u8"scheme is not allowed");
                    return S_OK;
                }
                static_cast<void>(source->Navigate(uri.value));
                return S_OK;
            }).Get(),
            &token));

        // 권한은 **언제나 거절한다.** 여는 스위치는 일부러 없다 (webview.h).
        //  - 아무것도 하지 않으면 브라우저의 기본 대화상자가 뜨는데, 합성 호스팅에서
        //    그것이 어디에 뜨는지는 문서가 말하지 않는다.
        //  - 거절을 프로필에 남기지 않는다. 남기면 정책이 생기는 날 되돌릴 길이 없다.
        static_cast<void>(target->view->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>([alive, self, key](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                static_cast<void>(args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY));
                ComPtr<ICoreWebView2PermissionRequestedEventArgs3> args3 {};
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args3))) && args3 != nullptr)
                    static_cast<void>(args3->put_SavesInProfile(FALSE));
                entry* const target_entry { self->find(key) };
                if (target_entry == nullptr)
                    return S_OK;
                COREWEBVIEW2_PERMISSION_KIND kind { COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION };
                static_cast<void>(args->get_PermissionKind(&kind));
                wil_string_scope uri {};
                static_cast<void>(args->get_Uri(&uri.value));
                self->report(*target_entry, webview_event_kind::permission_denied, to_utf8(uri), permission_name(kind));
                return S_OK;
            }).Get(),
            &token));

        // 내려받기는 **언제나 막는다.** 디스크에 쓰는 일이라 신뢰 경계를 여는 스위치다.
        // 취소하면 저장 대화상자도 뜨지 않는다.
        ComPtr<ICoreWebView2_4> view4 {};
        if (SUCCEEDED(target->view.As(&view4)) && view4 != nullptr)
            static_cast<void>(
                view4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([alive, self, key](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* const args) -> HRESULT {
                    if (*alive == false || args == nullptr)
                        return S_OK;
                    static_cast<void>(args->put_Cancel(TRUE));
                    static_cast<void>(args->put_Handled(TRUE));
                    entry* const target_entry { self->find(key) };
                    if (target_entry == nullptr)
                        return S_OK;
                    wil_string_scope uri {};
                    ComPtr<ICoreWebView2DownloadOperation> download {};
                    if (SUCCEEDED(args->get_DownloadOperation(&download)) && download != nullptr)
                        static_cast<void>(download->get_Uri(&uri.value));
                    self->report(*target_entry, webview_event_kind::download_blocked, to_utf8(uri), u8"downloads are not accepted");
                    return S_OK;
                }).Get(),
                    &token));

        // 페이지가 `window.chrome.webview.postMessage`로 보낸 것이다. 다리는 이것
        // 하나다 (호스트 객체는 닫혀 있다 — `apply_settings`).
        //  - 크기는 UTF-8로 잰다. UTF-16 단위 수가 이미 상한을 넘으면(UTF-8 바이트는
        //    단위 수보다 적을 수 없다) 옮기지 않고 그 수로 버린다 — 큰 메시지를 옮기는
        //    값을 페이지가 우리에게 물리지 못하게 한다.
        //  - 상한과 초당 건수는 `report`가 집행한다.
        static_cast<void>(target->view->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>([alive, self, key](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                entry* const target_entry { self->find(key) };
                if (target_entry == nullptr)
                    return S_OK;
                wil_string_scope json {};
                static_cast<void>(args->get_WebMessageAsJson(&json.value));
                wil_string_scope origin {};
                static_cast<void>(args->get_Source(&origin.value));
                const std::size_t units { json.value != nullptr ? std::wcslen(json.value) : 0 };
                webview_event event {};
                event.kind = webview_event_kind::web_message_received;
                event.url = to_utf8(origin);
                std::size_t bytes { units };
                if (units <= target_entry->policy.maximum_message_bytes)
                {
                    event.message = to_utf8(json);
                    bytes = event.message.size();
                }
                self->report(*target_entry, std::move(event), bytes);
                return S_OK;
            }).Get(),
            &token));

        // 프로세스 실패는 **갈래를 구분한다**. 렌더러는 다시 읽으면 되고,
        // 컨트롤러를 버려야 하는 것은 브라우저 프로세스뿐이다. GPU·utility 같은 것은
        // 런타임이 스스로 되살리므로 손대지 않는다 — 흔한 GPU 재시작마다 과잉 대응하지
        // 않는다.
        static_cast<void>(target->view->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>([alive, self, key](ICoreWebView2* const source, ICoreWebView2ProcessFailedEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                entry* const target_entry { self->find(key) };
                if (target_entry == nullptr)
                    return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind { COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED };
                static_cast<void>(args->get_ProcessFailedKind(&kind));
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED)
                {
                    // 런타임이 새 렌더러를 세우고 오류 페이지를 보인다. 다시 읽으면
                    // 원래 문서로 돌아온다.
                    //  - **잇달아 죽으면 다시 읽지 않는다.** 열자마자 렌더러를 죽이는
                    //    페이지는 다시 읽는 만큼 다시 죽어, 죽음→다시 읽기→죽음이
                    //    렌더러가 서는 속도로 돌고 그때마다 사건 셋이 inbox로 간다.
                    //    오류 페이지가 남는 것이 그 순환보다 낫다.
                    constexpr std::uint64_t reload_interval_ms { 10000 };
                    const std::uint64_t now { GetTickCount64() };
                    const bool again { target_entry->last_render_failure_ms.has_value() && now - *target_entry->last_render_failure_ms < reload_interval_ms };
                    target_entry->last_render_failure_ms = now;
                    self->report(*target_entry, webview_event_kind::render_process_failed, {}, again ? u8"render process exited again; not reloading" : u8"render process exited");
                    if (again == false && source != nullptr)
                        static_cast<void>(source->Reload());
                    return S_OK;
                }
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED)
                {
                    // iframe만의 렌더러다. 주 문서는 온전하고 그 frame에 오류 페이지가
                    // 든다. 다시 읽으면 주 문서의 상태를 잃으므로 알리기만 한다.
                    self->report(*target_entry, webview_event_kind::render_process_failed, {}, u8"frame render process exited; not reloading");
                    return S_OK;
                }
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
                {
                    // 컨트롤러는 이미 닫힌 상태다. 항목을 실패로 돌려 앱이 목록에서
                    // 뺐다가 다시 실으면 새로 선다 — 환경은 그대로 다시 쓴다 (문서:
                    // 같은 환경에서 새 웹뷰를 만들면 새 브라우저 프로세스가 선다).
                    // 없애는 것은 다음 대조다 — 이 콜백을 부른 객체를 여기서 놓지 않는다.
                    self->report(*target_entry, webview_event_kind::browser_process_failed, {}, u8"browser process exited");
                    self->fail(key, u8"The WebView2 browser process exited.");
                }
                return S_OK;
            }).Get(),
            &token));

        // --- 초점 ---
        //
        // 가둠은 **WebView2 쪽에 선다.** 페이지 끝을 넘어가면 이 이벤트가 오는데,
        // 아무것도 하지 않으면 초점이 붕 뜬다(`activeElement`가 `BODY`가 되고
        // `document.hasFocus()`가 거짓). **들어온 reason을 그대로 되돌려** 부르면
        // NEXT는 첫 칸으로 PREVIOUS는 마지막 칸으로 돈다 —
        // `PROGRAMMATIC`으로는 되돌아가지 않는다.
        static_cast<void>(target->controller->add_MoveFocusRequested(
            Callback<ICoreWebView2MoveFocusRequestedEventHandler>([alive, self, key](ICoreWebView2Controller* const source, ICoreWebView2MoveFocusRequestedEventArgs* const args) -> HRESULT {
                if (*alive == false || source == nullptr || args == nullptr)
                    return S_OK;
                COREWEBVIEW2_MOVE_FOCUS_REASON reason { COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT };
                static_cast<void>(args->get_Reason(&reason));
                static_cast<void>(args->put_Handled(TRUE));
                static_cast<void>(source->MoveFocus(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS ? COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS : COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT));
                return S_OK;
            }).Get(),
            &token));

        // 논리 초점을 맞춘다. 클릭 진입은 raw input이 우리에게 오지 않으므로
        // 이것을 하지 않으면 초점 테가 이전 자리에 남는다.
        static_cast<void>(target->controller->add_GotFocus(Callback<ICoreWebView2FocusChangedEventHandler>([alive, self, key](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
            if (*alive == false)
                return S_OK;
            if (const entry* const source { self->find(key) }; source != nullptr)
                self->signal_focus(*source, webview_focus_signal::entered);
            return S_OK;
        }).Get(),
            &token));
        static_cast<void>(target->controller->add_LostFocus(Callback<ICoreWebView2FocusChangedEventHandler>([alive, self, key](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
            if (*alive == false)
                return S_OK;
            if (const entry* const source { self->find(key) }; source != nullptr)
                self->signal_focus(*source, webview_focus_signal::left);
            return S_OK;
        }).Get(),
            &token));

        // **웹뷰가 초점을 쥔 동안 우리에게 오는 유일한 키 통로다.**
        // 평범한 글자·Tab·Enter·Space는 아예 오지 않으므로 이 이벤트는 시끄럽지 않다
        static_cast<void>(target->controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>([alive, self, key](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* const args) -> HRESULT {
                if (*alive == false || args == nullptr)
                    return S_OK;
                COREWEBVIEW2_KEY_EVENT_KIND kind { COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN };
                UINT virtual_key { 0 };
                static_cast<void>(args->get_KeyEventKind(&kind));
                static_cast<void>(args->get_VirtualKey(&virtual_key));
                // **누름만 처리한다.** 뗌까지 처리하면 한 번 누른 키가 두 번 먹힌다.
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                    return S_OK;
                // 이벤트 인자에 없는 수식키 상태는 GetKeyState로 읽는다.
                const bool control { (GetKeyState(VK_CONTROL) & 0x8000) != 0 };
                const bool shift { (GetKeyState(VK_SHIFT) & 0x8000) != 0 };
                const entry* const source { self->find(key) };
                if (source == nullptr)
                    return S_OK;

                if (virtual_key == VK_TAB && control)
                {
                    // **삼키면 페이지는 이 키를 한 건도 받지 않는다.** 그래서
                    // 여기서 우리가 처리하지 않으면 아무 일도 일어나지 않는다.
                    static_cast<void>(args->put_Handled(TRUE));
                    self->signal_focus(*source, shift ? webview_focus_signal::leave_backward : webview_focus_signal::leave_forward);
                    return S_OK;
                }
                if (virtual_key == VK_ESCAPE)
                {
                    static_cast<void>(args->put_Handled(TRUE));
                    self->signal_focus(*source, webview_focus_signal::dismiss);
                }
                // Alt+F4는 소비하지 않아 WM_SYSCOMMAND·WM_CLOSE가 창에 전달되게 한다.
                return S_OK;
            }).Get(),
            &token));

        // 서기 전에 온 명령을 이제 흘려보낸다.
        flush_commands(*target);
    }

    void webview_host::set_default_background(const ui_color color)
    {
        if (default_background_ == color)
            return;
        default_background_ = color;
        for (const std::unique_ptr<entry>& current : entries_)
            if (current->controller != nullptr)
                apply_default_background(*current);
    }

    void webview_host::apply_default_background(entry& target) const
    {
        ComPtr<ICoreWebView2Controller2> controller2 {};
        if (FAILED(target.controller.As(&controller2)) || controller2 == nullptr)
            return;
        // 알파는 언제나 불투명이다 — 바닥이 비치면 그 아래는 바탕 화면이다.
        const auto channel = [this](const int shift) { return static_cast<BYTE>((default_background_ >> shift) & 0xFFu); };
        static_cast<void>(controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR { 255, channel(16), channel(8), channel(0) }));
    }

    void webview_host::synchronize(const std::span<const webview_target> wanted, IDCompositionDevice* const composition)
    {
        composition_ = composition;
        std::vector<webview_placement> alive {};
        alive.reserve(entries_.size());
        for (const std::unique_ptr<entry>& current : entries_)
            alive.push_back(webview_placement { current->id, current->anchor });

        // frame이 더 싣지 않는 실패 기록은 잊는다 — 앱이 목록에서 뺐다가 다시 실으면
        // 그때 새로 시도한다 (`failed_popups_`와 같은 규칙). 브라우저 프로세스가
        // 죽은 뒤 되살리는 길이 이것이다.
        std::erase_if(failed_, [&wanted](const std::u8string& id) {
            for (const webview_target& target : wanted)
                if (target.source != nullptr && target.source->id == id)
                    return false;
            return true;
        });

        // 만들 수 없는 것은 원하는 목록에서 미리 뺀다 — 그래야 대조가 그것을
        // "없앨 것"으로 보지 않는다.
        std::vector<webview_placement> requested {};
        std::vector<webview_target> sources {};
        for (const webview_target& target : wanted)
        {
            // 앵커 표면이 아직 없으면 창이 비어 온다. 그 frame에는 만들지 않는다.
            if (target.source == nullptr || target.window == nullptr)
                continue;
            const ui_webview& want { *target.source };
            // id도 프로필 자리도 없는 것은 만들지 않는다 (webview.h의 계약).
            if (want.id.empty() || want.user_data_folder.empty())
                continue;
            if (std::find(failed_.begin(), failed_.end(), want.id) != failed_.end())
                continue;
            requested.push_back(webview_placement { want.id, want.anchor });
            sources.push_back(target);
        }

        const webview_reconcile_result plan { reconcile_webviews(alive, requested) };
        for (const std::u8string& id : plan.destroy)
            std::erase_if(entries_, [this, &id](const std::unique_ptr<entry>& candidate) {
                if (candidate->id != id)
                    return false;
                tear_down(*candidate);
                return true;
            });

        for (const std::size_t index : plan.create)
        {
            const ui_webview& want { *sources[index].source };
            auto created { std::make_unique<entry>() };
            created->id = want.id;
            created->anchor = want.anchor;
            created->user_data_folder = want.user_data_folder;
            created->policy = want.policy;
            created->window = sources[index].window;
            if (composition != nullptr)
                static_cast<void>(composition->CreateVisual(&created->visual));
            entry* const slot { created.get() };
            entries_.push_back(std::move(created));

            const environment* const owner { acquire_environment(want.user_data_folder) };
            if (owner != nullptr && owner->state == creation_state::failed)
            {
                fail(want.id, u8"The WebView2 environment is unavailable.");
                continue;
            }
            begin_controller(*slot, sources[index].window);
        }

        // 명령은 **기억하고** 준비된 것만 흘려보낸다. frame이 매번 다시 게시되므로
        // 값이 아니라 revision을 본다.
        for (const webview_target& source : sources)
        {
            const ui_webview* const want { source.source };
            entry* const target { find(want->id) };
            if (target == nullptr)
                continue;
            target->wanted_navigate_revision = want->navigate_revision;
            target->wanted_navigate_url = want->navigate_url;
            target->wanted_post_revision = want->post_revision;
            target->wanted_post_message = want->post_message;
            // 정책은 매 frame 살아 있다. 사건 핸들러가 읽는 갈래는 곧바로, 브라우저
            // 설정 갈래는 다시 적어 **다음 항해부터** 든다 (WebView2의 규칙).
            if (target->policy != want->policy)
            {
                target->policy = want->policy;
                if (target->state == creation_state::ready && target->view != nullptr)
                    apply_settings(*target->view.Get(), target->policy);
            }
            target->on_event = want->on_event;
            flush_commands(*target);
        }
    }

    void webview_host::flush_commands(entry& target)
    {
        if (target.state != creation_state::ready || target.view == nullptr)
            return;

        if (target.wanted_navigate_revision != target.navigate_revision)
        {
            target.navigate_revision = target.wanted_navigate_revision;
            // **스킴은 여는 자리에서 먼저 거른다.** `javascript:`는 실제 네비게이션이
            // 아니라 `NavigationStarting`이 아예 나지 않아 그물 밖이다.
            // 나머지 주소는 `NavigationStarting`이 한 번 더 본다.
            // 주소가 비어 있으면 아무 데도 가지 않는다 — 막힌 것이 아니라 갈 곳이 없는 것이다.
            if (target.navigate_revision != 0 && target.wanted_navigate_url.empty() == false)
            {
                if (scheme_allowed(target.wanted_navigate_url, target.policy.allowed_schemes) == false)
                    report(target, webview_event_kind::navigation_blocked, target.wanted_navigate_url, u8"scheme is not allowed");
                else if (const auto url { utf8_to_utf16(target.wanted_navigate_url) }; url.value.has_value())
                    static_cast<void>(target.view->Navigate(url.value->c_str()));
            }
        }
        if (target.wanted_post_revision != target.post_revision)
        {
            target.post_revision = target.wanted_post_revision;
            // 앱이 보내는 것에는 상한이 없다 — 앱은 믿는다 (webview.h).
            if (target.post_revision != 0)
                if (const auto message { utf8_to_utf16(target.wanted_post_message) }; message.value.has_value())
                    static_cast<void>(target.view->PostWebMessageAsJson(message.value->c_str()));
        }
    }

    void webview_host::apply_layout(const std::u8string& id, const webview_layout& layout, IDCompositionVisual* const underlay)
    {
        entry* const target { find(id) };
        if (target == nullptr || target->state != creation_state::ready || target->controller == nullptr)
            return;

        // 합성이 없으면(CPU 백엔드) 설 자리가 없다. 감추고 자리표에 맡긴다.
        const bool showing { layout.visible() && underlay != nullptr && target->visual != nullptr };
        if ((showing == false || layout.punch_hole == false) && target->pressed_buttons != 0)
            cancel_pointer(target->anchor);
        // 브라우저가 DOWN만 받은 채 감춰지지 않도록 먼저 접촉을 끝낸다.
        // 표면은 남은 시퀀스를 계속 소유하므로 뒤의 luil UI에 누름이 새지 않는다.
        if (showing == false || layout.punch_hole == false)
            while (target->pointer_contacts.empty() == false)
                cancel_pointer_input(target->anchor, target->pointer_contacts.back().first);
        if (showing == false)
        {
            // **떼지 않는다.** 보이지 않는 visual은 아무것도 그리지 않으므로 떼어 둘
            // 이유가 없고, 떼었다 붙이면 `put_RootVisualTarget`을 다시 걸어야 해서
            // 되살아난 페이지가 빈다.
            if (target->shown == false)
                return;
            target->shown = false;
            target->has_applied = false;
            static_cast<void>(target->controller->put_IsVisible(FALSE));
            if (composition_ != nullptr)
                static_cast<void>(composition_->Commit());
            return;
        }

        if (target->attached_to.Get() != underlay)
        {
            if (target->attached_to != nullptr)
                static_cast<void>(target->attached_to->RemoveVisual(target->visual.Get()));
            static_cast<void>(underlay->AddVisual(target->visual.Get(), FALSE, nullptr));
            target->attached_to = underlay;
            static_cast<void>(target->composition_controller->put_RootVisualTarget(target->visual.Get()));
        }
        target->shown = true;

        if (target->has_applied && target->applied == layout)
            return;
        const bool size_changed { target->has_applied == false || target->applied.width != layout.width || target->applied.height != layout.height };
        const bool scale_changed { target->has_applied == false || target->applied.scale != layout.scale };
        target->applied = layout;
        target->has_applied = true;

        // **자리는 visual의 변환이 정한다.** `put_Bounds`의 left·top은 그리기에도
        // 입력에도 쓰이지 않아 늘 0이다 — 그 둘을 주어도 페이지는 여전히 visual
        // 원점에 그려진다.
        static_cast<void>(target->visual->SetOffsetX(static_cast<float>(layout.x)));
        static_cast<void>(target->visual->SetOffsetY(static_cast<float>(layout.y)));
        // **없으면 삐져나온다.** bounds보다 큰 내용은 visual 밖까지 그려지므로
        // 우리 UI를 덮는다. clip 좌표는 visual 자기 기준이라 offset을 따라 움직인다.
        static_cast<void>(target->visual->SetClip(D2D_RECT_F {
            0.0f,
            0.0f,
            static_cast<float>(layout.width),
            static_cast<float>(layout.height),
        }));
        // 배율과 크기를 메시지 펌프 없이 연달아 적용한다.
        // 둘 사이에 그리면 서로 다른 배율과 크기가 섞여 추가 reflow가 생긴다.
        // 배율은 WM_DPICHANGED에서 직접 전달하지 않고 tree의 webview_layout::scale을 따른다.
        // 따라서 새 tree의 자리와 배율이 같은 frame에 반영된다.
        // 배율·크기는 값이 바뀔 때만 적용하고 위치만 바뀌면 offset만 갱신한다.
        if (scale_changed)
        {
            bool accepted { false };
            ComPtr<ICoreWebView2Controller3> controller3 {};
            if (SUCCEEDED(target->controller.As(&controller3)) && controller3 != nullptr)
                accepted = SUCCEEDED(controller3->put_RasterizationScale(static_cast<double>(layout.scale)));
            // 받아 주지 않은 배율은 적용한 것으로 적지 않는다 — 0은 자리의 배율이
            // 될 수 없는 값이라 다음 frame에 다시 준다. 적어 두면 자리는 새 배율인데
            // 래스터는 옛 배율인 어긋난 쌍이 영영 남는다.
            if (accepted == false)
                target->applied.scale = 0.0f;
        }
        if (size_changed)
        {
            const RECT bounds { 0, 0, layout.width, layout.height };
            static_cast<void>(target->controller->put_Bounds(bounds));
        }
        static_cast<void>(target->controller->put_IsVisible(TRUE));
        // **바꾼 것을 커밋해야 화면에 닿는다.** visual을 붙이고 옮기고 잘라 놓아도
        // 커밋 전에는 아무 일도 일어나지 않는다 (0.5us — 자리가 바뀐 frame에만 든다).
        if (composition_ != nullptr)
            static_cast<void>(composition_->Commit());
    }

    namespace {
        // Win32 포인터 메시지를 WebView2의 것으로 옮긴다.
        // 옮길 수 없는 것은 넘기지 않는다 (그 메시지는 우리 tree의 것이다).
        [[nodiscard]] std::optional<COREWEBVIEW2_MOUSE_EVENT_KIND> mouse_kind(const UINT message) noexcept
        {
            switch (message)
            {
            case WM_MOUSEMOVE:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
            case WM_LBUTTONDOWN:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
            case WM_LBUTTONUP:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
            case WM_LBUTTONDBLCLK:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;
            case WM_RBUTTONDOWN:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;
            case WM_RBUTTONUP:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
            case WM_RBUTTONDBLCLK:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK;
            case WM_MBUTTONDOWN:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN;
            case WM_MBUTTONDBLCLK:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK;
            case WM_MBUTTONUP:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
            case WM_MOUSEWHEEL:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;
            case WM_MOUSEHWHEEL:
                return COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL;
            default:
                return std::nullopt;
            }
        }

        // 지금 눌려 있는 것과 수식키다. 인자의 wparam이 그대로 그 값이다.
        [[nodiscard]] COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS mouse_keys(const UINT message, const WPARAM word_parameter) noexcept
        {
            // 휠의 wparam은 상위 워드가 delta라 하위 워드만 키 상태다.
            const WORD flags { message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ? GET_KEYSTATE_WPARAM(word_parameter) : static_cast<WORD>(word_parameter) };
            return static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(flags);
        }
    } // namespace

    bool webview_host::relay_pointer(const std::u8string& anchor, const UINT message, const WPARAM word_parameter, const int client_x, const int client_y)
    {
        const std::optional<COREWEBVIEW2_MOUSE_EVENT_KIND> kind { mouse_kind(message) };
        if (kind.has_value() == false)
            return false;

        entry* captured { nullptr };
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->anchor == anchor && candidate->pressed_buttons != 0)
                captured = candidate.get();

        for (const std::unique_ptr<entry>& candidate : entries_)
        {
            // **보이는 웹뷰만 포인터를 갖는다.** 감출 때 visual을 떼지 않고 `applied`도
            // 지우지 않으므로(되살릴 때 그대로 쓴다), 붙어 있음만 보면 감춰진 웹뷰가
            // 옛 자리에서 우리 tree의 클릭과 휠을 계속 먹는다 — 웹뷰 탭을 한 번 본
            // 뒤의 모든 화면에서 그 자리가 죽어 있던 자리다.
            if (candidate->anchor != anchor || candidate->state != creation_state::ready || candidate->composition_controller == nullptr || candidate->attached_to == nullptr
                || candidate->shown == false)
                continue;
            if (captured != nullptr && captured != candidate.get())
                continue;
            const webview_pointer local { translate_webview_pointer(candidate->applied, client_x, client_y, captured == candidate.get()) };
            if (local.inside == false)
                continue;

            // 휠은 delta가, 나머지는 0이 mouseData다.
            const UINT32 data { message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ? static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(word_parameter)) : 0 };
            const POINT point { local.x, local.y };
            static_cast<void>(candidate->composition_controller->SendMouseInput(*kind, mouse_keys(message, word_parameter), data, point));
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
                candidate->pressed_buttons |= MK_LBUTTON;
            if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK)
                candidate->pressed_buttons |= MK_RBUTTON;
            if (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK)
                candidate->pressed_buttons |= MK_MBUTTON;
            if (message == WM_LBUTTONUP)
                candidate->pressed_buttons &= ~static_cast<UINT>(MK_LBUTTON);
            if (message == WM_RBUTTONUP)
                candidate->pressed_buttons &= ~static_cast<UINT>(MK_RBUTTON);
            if (message == WM_MBUTTONUP)
                candidate->pressed_buttons &= ~static_cast<UINT>(MK_MBUTTON);
            return true;
        }
        return false;
    }

    void webview_host::relay_pointer_left(const std::u8string& anchor)
    {
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->anchor == anchor && candidate->state == creation_state::ready && candidate->composition_controller != nullptr && candidate->attached_to != nullptr && candidate->shown
                && candidate->pressed_buttons == 0)
                static_cast<void>(candidate->composition_controller->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT { 0, 0 }));
    }

    void webview_host::cancel_pointer(const std::u8string& anchor)
    {
        const std::pair<UINT, COREWEBVIEW2_MOUSE_EVENT_KIND> buttons[] {
            { MK_LBUTTON, COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP },
            { MK_RBUTTON, COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP },
            { MK_MBUTTON, COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP },
        };
        for (const std::unique_ptr<entry>& candidate : entries_)
        {
            if (candidate->anchor != anchor || candidate->composition_controller == nullptr)
                continue;
            for (const auto [button, kind] : buttons)
                if ((candidate->pressed_buttons & button) != 0)
                    static_cast<void>(candidate->composition_controller->SendMouseInput(kind, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT { -10000, -10000 }));
            candidate->pressed_buttons = 0;
        }
    }

    namespace {
        [[nodiscard]] std::optional<COREWEBVIEW2_POINTER_EVENT_KIND> pointer_event_kind(const UINT message) noexcept
        {
            switch (message)
            {
            case WM_POINTERDOWN:
                return COREWEBVIEW2_POINTER_EVENT_KIND_DOWN;
            case WM_POINTERUPDATE:
                return COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE;
            case WM_POINTERUP:
                return COREWEBVIEW2_POINTER_EVENT_KIND_UP;
            case WM_POINTERENTER:
                return COREWEBVIEW2_POINTER_EVENT_KIND_ENTER;
            case WM_POINTERLEAVE:
                return COREWEBVIEW2_POINTER_EVENT_KIND_LEAVE;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] POINT moved_by(const POINT point, const POINT offset) noexcept
        {
            return { point.x + offset.x, point.y + offset.y };
        }

        [[nodiscard]] RECT moved_by(const RECT rect, const POINT offset) noexcept
        {
            return { rect.left + offset.x, rect.top + offset.y, rect.right + offset.x, rect.bottom + offset.y };
        }

        template<typename container_type>
        [[nodiscard]] bool holds_pointer(const container_type& values, const UINT32 pointer_id) noexcept
        {
            return std::any_of(values.begin(), values.end(), [pointer_id](const auto& value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, UINT32>)
                    return value == pointer_id;
                else
                    return value.first == pointer_id;
            });
        }
    } // namespace

    void webview_host::send_pointer_input(entry& target, const webview_pointer_input& input, const UINT event_kind)
    {
        if (target.composition_controller == nullptr)
            return;

        ComPtr<ICoreWebView2Environment3> environment3 {};
        for (const std::unique_ptr<environment>& candidate : environments_)
            if (candidate->user_data_folder == target.user_data_folder && candidate->value != nullptr)
                static_cast<void>(candidate->value.As(&environment3));
        ComPtr<ICoreWebView2PointerInfo> info {};
        if (environment3 == nullptr || FAILED(environment3->CreateCoreWebView2PointerInfo(&info)) || info == nullptr)
            return;
        // 종료는 modal 차단·감춤 뒤에도 원래 웹뷰 기준 좌표로 보낸다.
        // 가시성용 translate_webview_pointer는 여기서 쓰지 않는다.
        const POINT offset { input.screen_to_client.x - target.applied.x, input.screen_to_client.y - target.applied.y };
        static_cast<void>(info->put_PointerKind(input.info.pointerType));
        static_cast<void>(info->put_PointerId(input.info.pointerId));
        static_cast<void>(info->put_FrameId(input.info.frameId));
        static_cast<void>(info->put_PointerFlags(input.info.pointerFlags));
        static_cast<void>(info->put_PointerDeviceRect(input.device_rect));
        static_cast<void>(info->put_DisplayRect(input.display_rect));
        static_cast<void>(info->put_PixelLocation(moved_by(input.info.ptPixelLocation, offset)));
        static_cast<void>(info->put_HimetricLocation(input.info.ptHimetricLocation));
        static_cast<void>(info->put_PixelLocationRaw(moved_by(input.info.ptPixelLocationRaw, offset)));
        static_cast<void>(info->put_HimetricLocationRaw(input.info.ptHimetricLocationRaw));
        static_cast<void>(info->put_Time(input.info.dwTime));
        static_cast<void>(info->put_HistoryCount(input.info.historyCount));
        static_cast<void>(info->put_InputData(input.info.InputData));
        static_cast<void>(info->put_KeyStates(input.info.dwKeyStates));
        static_cast<void>(info->put_PerformanceCount(input.info.PerformanceCount));
        static_cast<void>(info->put_ButtonChangeKind(static_cast<INT32>(input.info.ButtonChangeType)));
        if (input.info.pointerType == PT_PEN)
        {
            static_cast<void>(info->put_PenFlags(input.pen.penFlags));
            static_cast<void>(info->put_PenMask(input.pen.penMask));
            static_cast<void>(info->put_PenPressure(input.pen.pressure));
            static_cast<void>(info->put_PenRotation(input.pen.rotation));
            static_cast<void>(info->put_PenTiltX(input.pen.tiltX));
            static_cast<void>(info->put_PenTiltY(input.pen.tiltY));
        }
        else if (input.info.pointerType == PT_TOUCH)
        {
            static_cast<void>(info->put_TouchFlags(input.touch.touchFlags));
            static_cast<void>(info->put_TouchMask(input.touch.touchMask));
            static_cast<void>(info->put_TouchContact(moved_by(input.touch.rcContact, offset)));
            static_cast<void>(info->put_TouchContactRaw(moved_by(input.touch.rcContactRaw, offset)));
            static_cast<void>(info->put_TouchOrientation(input.touch.orientation));
            static_cast<void>(info->put_TouchPressure(input.touch.pressure));
        }
        static_cast<void>(target.composition_controller->SendPointerInput(static_cast<COREWEBVIEW2_POINTER_EVENT_KIND>(event_kind), info.Get()));
    }

    bool webview_host::relay_pointer_input(const std::u8string& anchor, const webview_pointer_input& input)
    {
        const std::optional<COREWEBVIEW2_POINTER_EVENT_KIND> kind { pointer_event_kind(input.message) };
        if (kind.has_value() == false)
            return false;
        const UINT32 pointer_id { input.info.pointerId };
        const auto usable = [&anchor](const entry& candidate) {
            return candidate.anchor == anchor && candidate.state == creation_state::ready && candidate.composition_controller != nullptr && candidate.attached_to != nullptr && candidate.shown;
        };
        // 원본을 그 웹뷰 기준으로 옮겨 보낸다.
        //  - 픽셀 자리와 접촉 사각형은 화면 좌표라 client로, 다시 웹뷰 왼쪽 위 기준으로
        //    옮긴다. himetric과 장치 사각형은 장치 단위라 그대로 둔다.
        const auto send = [this, &input](entry& target, const COREWEBVIEW2_POINTER_EVENT_KIND event_kind) { send_pointer_input(target, input, static_cast<UINT>(event_kind)); };

        // 접촉을 쥔 웹뷰가 끝까지 받는다. 보낼 수 없게 됐어도(감춤) 삼킨다 —
        // 그 접촉을 뒤의 우리 tree가 새 누름으로 받으면 안 된다.
        const bool ends { input.message == WM_POINTERUP || (input.info.pointerFlags & POINTER_FLAG_CANCELED) != 0 };
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate->anchor == anchor && holds_pointer(candidate->pointer_contacts, pointer_id))
            {
                if (usable(*candidate))
                    send(*candidate, *kind);
                if (ends)
                    std::erase_if(candidate->pointer_contacts, [pointer_id](const auto& value) { return value.first == pointer_id; });
                return true;
            }

        // 떠나는 펜은 그 위에 떠 있던 웹뷰에 알린다.
        if (input.message == WM_POINTERLEAVE)
        {
            bool relayed { false };
            for (const std::unique_ptr<entry>& candidate : entries_)
                if (candidate->anchor == anchor && holds_pointer(candidate->pointer_hovers, pointer_id))
                {
                    if (usable(*candidate))
                        send(*candidate, *kind);
                    std::erase(candidate->pointer_hovers, pointer_id);
                    relayed = true;
                }
            return relayed;
        }

        // 우리가 시작을 보지 못한 접촉의 나머지는 웹뷰의 것이 아니다.
        const bool contact { (input.info.pointerFlags & POINTER_FLAG_INCONTACT) != 0 };
        if (input.message != WM_POINTERDOWN && (contact || input.message == WM_POINTERUP))
            return false;

        entry* picked { nullptr };
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (usable(*candidate) && translate_webview_pointer(candidate->applied, input.client.x, input.client.y).inside)
            {
                picked = candidate.get();
                break;
            }
        // hover가 다른 자리로 옮겨 갔으면 떠난 웹뷰에 LEAVE를 준다.
        for (const std::unique_ptr<entry>& candidate : entries_)
            if (candidate.get() != picked && candidate->anchor == anchor && holds_pointer(candidate->pointer_hovers, pointer_id))
            {
                if (usable(*candidate))
                    send(*candidate, COREWEBVIEW2_POINTER_EVENT_KIND_LEAVE);
                std::erase(candidate->pointer_hovers, pointer_id);
            }
        if (picked == nullptr)
            return false;

        send(*picked, *kind);
        if (input.message == WM_POINTERDOWN)
        {
            std::erase(picked->pointer_hovers, pointer_id);
            picked->pointer_contacts.emplace_back(pointer_id, input.info.pointerType);
        }
        else if (holds_pointer(picked->pointer_hovers, pointer_id) == false)
            picked->pointer_hovers.push_back(pointer_id);
        return true;
    }

    void webview_host::cancel_pointer_input(const std::u8string& anchor, const std::uint32_t pointer_id)
    {
        for (const std::unique_ptr<entry>& candidate : entries_)
        {
            if (candidate->anchor != anchor)
                continue;
            const auto found { std::find_if(candidate->pointer_contacts.begin(), candidate->pointer_contacts.end(), [pointer_id](const auto& value) { return value.first == pointer_id; }) };
            if (found == candidate->pointer_contacts.end())
                continue;
            // 원본이 더 없으므로 취소 표식만 실은 뗌을 보낸다. 자리는 화면 밖이다.
            // 가시성 라우팅을 거치지 않는다. 이미 감춘 웹뷰도 종료를 받아야 한다.
            webview_pointer_input cancelled {};
            cancelled.message = WM_POINTERUP;
            cancelled.info.pointerType = found->second;
            cancelled.info.pointerId = pointer_id;
            cancelled.info.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;
            cancelled.info.ptPixelLocation = POINT { -10000, -10000 };
            cancelled.info.ptPixelLocationRaw = POINT { -10000, -10000 };
            cancelled.client = POINT { -10000, -10000 };
            send_pointer_input(*candidate, cancelled, static_cast<UINT>(COREWEBVIEW2_POINTER_EVENT_KIND_UP));
            std::erase_if(candidate->pointer_contacts, [pointer_id](const auto& value) { return value.first == pointer_id; });
            return;
        }
    }

    bool webview_host::standing(const std::u8string& id) const noexcept
    {
        const entry* const target { find(id) };
        // **보이는 것까지 요구한다.** 붙어만 있고 감춰진 웹뷰의 자리를 비우면
        // 그 아래에 아무것도 없어 바탕 화면이 비친다.
        return target != nullptr && target->state == creation_state::ready && target->attached_to != nullptr && target->shown && target->painted;
    }

    void webview_host::shutdown() noexcept
    {
        // 아직 오지 않은 완료 콜백이 죽은 host를 부르지 않게 먼저 끊는다.
        *alive_ = false;
        // 배출구도 끊는다. 그것이 잡은 `app_host`는 창이 죽을 때 우리보다 먼저 간다.
        deliver_ = nullptr;
        composition_ = nullptr;
        for (const std::unique_ptr<entry>& current : entries_)
            tear_down(*current);
        entries_.clear();
        environments_.clear();
    }
} // namespace luil::win32

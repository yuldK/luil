#include "loopback_http_server.h"
#include "luil/luil.h"

#include <windows.h>

#include <commctrl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace luil::testing {
    namespace {
        constexpr ui_element_kind label_kind { application_element_kind(0) };
        constexpr ui_element_kind button_kind { application_element_kind(1) };
        constexpr ui_element_kind text_kind { application_element_kind(2) };
        constexpr ui_element_kind modal_kind { application_element_kind(3) };
        constexpr text_input_target text_target { static_cast<text_input_target>(1) };

        struct metrics_intent
        {
            float width { 800.0f };
            float height { 620.0f };
            float scale { 1.0f };
        };
        struct close_intent
        {};
        struct refresh_intent
        {};
        struct click_intent
        {
            bool right { false };
        };
        struct scroll_intent
        {
            float value { 0.0f };
            bool absolute { false };
        };
        struct edit_intent
        {
            text_edit_request request {};
        };
        struct zoom_intent
        {
            float factor { 1.0f };
            zoom_point anchor {};
        };
        struct pan_intent
        {
            zoom_point delta {};
        };
        struct zoom_to_intent
        {
            float value { 1.0f };
        };
        struct command_intent
        {
            std::u8string command {};
        };

        // UIA가 읽는 결과와 OS 메시지 관측을 함께 남긴다. 입력을 우회해 만들지 않는다.
        struct native_counts
        {
            std::atomic<int> touch_down { 0 };
            std::atomic<int> touch_up { 0 };
            std::atomic<int> touch_up_at_mouse_down { 0 };
            std::atomic<int> touch_up_at_pen_down { 0 };
            std::atomic<int> pen_down { 0 };
            std::atomic<int> pen_hover { 0 };
            std::atomic<int> mouse_down { 0 };
            std::atomic<int> set_cursor { 0 };
            std::atomic<int> capture_changed { 0 };
            std::atomic<int> touch_canceled { 0 };
            std::atomic<UINT32> touch_up_flags { 0 };
            std::atomic<UINT32> touch_up_word_flags { 0 };
            std::atomic<int> canceled_message { 0 };
            std::atomic<bool> observer_ready { false };
            std::atomic<int> pointer_down { 0 };
            std::mutex trace_mutex {};
            nlohmann::json trace = nlohmann::json::array();
            nlohmann::json touch_frame = nlohmann::json::array();
        };

        [[nodiscard]] std::u8string utf8(const std::string& value)
        {
            return { value.begin(), value.end() };
        }

        class fixture_driver final : public win32::logic_driver
        {
        public:
            explicit fixture_driver(std::u8string profile)
                : profile_ { std::move(profile) }
            {
                server_.set_handler([](const loopback_request&) {
                    loopback_response response {};
                    response.headers.emplace_back("Content-Type", "text/html; charset=utf-8");
                    response.body = loopback_bytes(R"html(<!doctype html><meta charset="utf-8">
<style>html,body{margin:0;width:100%;height:100%;touch-action:none;background:#ccddee}</style>
<body>Native pointer target<script>
const counts={ready:true,down:0,up:0,cancel:0,orphan:0,type:'',buttons:0};
const active=new Set();
const send=()=>chrome.webview.postMessage(counts);
document.addEventListener('pointerdown',e=>{active.add(e.pointerId);counts.down++;counts.type=e.pointerType;counts.buttons=e.buttons;send();});
for(const name of ['pointerup','pointercancel'])document.addEventListener(name,e=>{
if(!active.delete(e.pointerId))counts.orphan++;
counts[name==='pointerup'?'up':'cancel']++;send();});
send();
</script>)html");
                    return response;
                });
            }

            void attach(win32::app_host& host)
            {
                host_.store(&host);
            }

            native_counts native {};

            void handle(app_message message) override
            {
                if (message.get<close_intent>() != nullptr)
                    closed_.store(true);
                else if (const auto* metrics { message.get<metrics_intent>() })
                    metrics_ = *metrics;
                else if (const auto* click { message.get<click_intent>() })
                    ++(click->right ? right_ : left_);
                else if (const auto* scroll { message.get<scroll_intent>() })
                    scroll_ = std::clamp(scroll->absolute ? scroll->value : scroll_ + scroll->value, 0.0f, 840.0f);
                else if (const auto* edit { message.get<edit_intent>() })
                    apply_text_edit(text_, edit->request);
                else if (const auto* zoom { message.get<zoom_intent>() })
                    zoom_ = zoom_about(zoom_config(), { 630.0f, 310.0f }, zoom->factor, zoom->anchor);
                else if (const auto* pan { message.get<pan_intent>() })
                    zoom_ = pan_by(zoom_config(), { 630.0f, 310.0f }, pan->delta);
                else if (const auto* value { message.get<zoom_to_intent>() })
                    zoom_ = zoom_about(zoom_config(), { 630.0f, 310.0f }, value->value / zoom_.zoom, {});
                else if (const auto* command { message.get<command_intent>() })
                {
                    if (command->command == u8"pan")
                        touch_.pan_enabled = !touch_.pan_enabled;
                    else if (command->command == u8"hold")
                        touch_.long_press_enabled = !touch_.long_press_enabled;
                    else if (command->command == u8"target")
                        enabled_ = !enabled_;
                    else if (command->command == u8"web-visible")
                        web_visible_ = !web_visible_;
                    else if (command->command == u8"zoom-visible")
                        zoom_visible_ = !zoom_visible_;
                    else if (command->command == u8"modal")
                        modal_ = !modal_;

                    if (command->command == u8"pan" || command->command == u8"hold")
                    {
                        auto* host { host_.load() };
                        config_ok_ = host != nullptr && host->set_touch_gesture_config(touch_);
                        ++config_revision_;
                    }
                }
                else if (const auto* event { message.get<win32::webview_event>() })
                {
                    if (event->kind == win32::webview_event_kind::web_message_received)
                    {
                        const std::string payload { event->message.begin(), event->message.end() };
                        const auto parsed { nlohmann::json::parse(payload, nullptr, false) };
                        if (parsed.is_object())
                            web_ = parsed;
                    }
                    else if (!event->error.empty())
                        web_error_ = event->error;
                }
            }

            [[nodiscard]] std::shared_ptr<const win32::ui_frame> make_frame() override
            {
                const float scale { metrics_.scale };
                auto root { std::make_unique<root_element>() };
                root->arrange({ { 0.0f, 0.0f, metrics_.width, metrics_.height }, scale });
                caption_config caption {};
                caption.title = u8"luil native pointer integration";
                auto caption_element_value { std::make_unique<caption_element>(caption) };
                caption_element_value->arrange({ { 0.0f, 0.0f, metrics_.width, 32.0f * scale }, scale });
                root->add(std::move(caption_element_value));

                auto add_button = [&](std::u8string owner, std::u8string title, const float x, const float y, ui_action action) {
                    auto button { std::make_unique<text_button_element>(ui_element_id { button_kind, std::move(owner) }, text_button_config { .text = std::move(title) }) };
                    button->set_action(ui_trigger::left_click, std::move(action));
                    button->arrange({ { x * scale, y * scale, 150.0f * scale, 32.0f * scale }, scale });
                    root->add(std::move(button));
                };
                add_button(u8"pan", u8"Toggle touch pan", 16.0f, 40.0f, make_message_action(command_intent { u8"pan" }));
                add_button(u8"hold", u8"Toggle touch hold", 176.0f, 40.0f, make_message_action(command_intent { u8"hold" }));
                add_button(u8"target", u8"Toggle target", 336.0f, 40.0f, make_message_action(command_intent { u8"target" }));
                add_button(u8"web-visible", u8"Toggle web visibility", 496.0f, 40.0f, make_message_action(command_intent { u8"web-visible" }));

                auto button { std::make_unique<text_button_element>(ui_element_id { button_kind, u8"click" }, text_button_config { .text = u8"Pointer click target" }) };
                button->set_enabled(enabled_);
                button->set_action(ui_trigger::left_click, make_message_action(click_intent {}));
                button->set_action(ui_trigger::right_click, make_message_action(click_intent { true }));
                button->arrange({ { 16.0f * scale, 86.0f * scale, 300.0f * scale, 44.0f * scale }, scale });
                root->add(std::move(button));

                auto input { std::make_unique<text_input_element>(ui_element_id { text_kind, u8"text" }, make_text_input_view(text_, {}, text_target), text_input_config {}) };
                input->arrange({ { 336.0f * scale, 86.0f * scale, 310.0f * scale, 44.0f * scale }, scale });
                root->add(std::move(input));

                scroll_area_config scroll_config {};
                scroll_config.owner = u8"scroll";
                scroll_config.content_height = 1000.0f;
                scroll_config.scroll_offset = scroll_;
                scroll_config.scroll = [](const float delta) { return make_app_action(scroll_intent { delta }); };
                scroll_config.scroll_to = [](const float offset) { return make_app_action(scroll_intent { offset, true }); };
                auto area { std::make_unique<scroll_area_element>(scroll_config) };
                label_config content_config {};
                content_config.text = u8"Touch pan target";
                area->set_content(std::make_unique<label_element>(ui_element_id { label_kind, u8"scroll-content" }, content_config));
                area->arrange({ { 16.0f * scale, 144.0f * scale, 630.0f * scale, 160.0f * scale }, scale });
                root->add(std::move(area));

                auto web_slot { std::make_unique<webview_element>(ui_element_id { ui_element_kind::webview, u8"web" }, webview_config { .webview = u8"web", .name = u8"Native web pointer target" }) };
                web_slot->set_visible(web_visible_);
                web_slot->arrange({ { 16.0f * scale, 320.0f * scale, 630.0f * scale, 130.0f * scale }, scale });
                root->add(std::move(web_slot));

                auto zoom { std::make_unique<zoom_view_element>(zoom_config()) };
                zoom->set_access_name(u8"Native zoom target");
                zoom->set_visible(zoom_visible_);
                zoom->arrange({ { 16.0f * scale, 140.0f * scale, 630.0f * scale, 310.0f * scale }, scale });
                root->add(std::move(zoom));

                if (modal_)
                {
                    auto scrim { std::make_unique<panel_element>(ui_element_id { modal_kind, u8"modal-scrim" }, panel_config {}) };
                    scrim->set_hit_opaque(true);
                    scrim->set_focus_trap(true);
                    scrim->arrange({ { 0.0f, 80.0f * scale, metrics_.width, 380.0f * scale }, scale });
                    root->add(std::move(scrim));
                }
                add_button(u8"modal", u8"Toggle modal blocker", 16.0f, 520.0f, make_message_action(command_intent { u8"modal" }));

                add_button(u8"zoom-visible", u8"Toggle zoom target", 176.0f, 520.0f, make_message_action(command_intent { u8"zoom-visible" }));

                nlohmann::json status {};
                status["zoom_visible"] = zoom_visible_;
                status["zoom"] = zoom_.zoom;
                status["zoom_x"] = zoom_.origin.x;
                status["zoom_y"] = zoom_.origin.y;
                status["left"] = left_;
                status["right"] = right_;
                status["scroll"] = scroll_;
                status["caret"] = text_.caret;
                status["anchor"] = text_.anchor;
                status["pan"] = touch_.pan_enabled;
                status["hold"] = touch_.long_press_enabled;
                status["config_ok"] = config_ok_;
                status["config_revision"] = config_revision_;
                status["enabled"] = enabled_;
                status["web_visible"] = web_visible_;
                status["modal"] = modal_;
                status["web"] = web_;
                status["web_error"] = std::string { web_error_.begin(), web_error_.end() };
                status["touch_down"] = native.touch_down.load();
                status["touch_up"] = native.touch_up.load();
                status["touch_up_at_mouse_down"] = native.touch_up_at_mouse_down.load();
                status["touch_up_at_pen_down"] = native.touch_up_at_pen_down.load();
                status["pen_down"] = native.pen_down.load();
                status["pen_hover"] = native.pen_hover.load();
                status["mouse_down"] = native.mouse_down.load();
                status["set_cursor"] = native.set_cursor.load();
                status["capture_changed"] = native.capture_changed.load();
                status["touch_canceled"] = native.touch_canceled.load();
                status["touch_up_flags"] = native.touch_up_flags.load();
                status["touch_up_word_flags"] = native.touch_up_word_flags.load();
                status["canceled_message"] = native.canceled_message.load();
                status["observer_ready"] = native.observer_ready.load();
                status["pointer_down"] = native.pointer_down.load();
                {
                    const std::lock_guard lock { native.trace_mutex };
                    status["native_trace"] = native.trace;
                    status["native_touch_frame"] = native.touch_frame;
                }
                label_config status_config {};
                status_config.text = utf8(status.dump());
                auto status_label { std::make_unique<label_element>(ui_element_id { label_kind, u8"status" }, status_config) };
                status_label->arrange({ { 16.0f * scale, 470.0f * scale, 630.0f * scale, 40.0f * scale }, scale });
                root->add(std::move(status_label));

                auto frame { std::make_shared<win32::ui_frame>() };
                frame->tree = std::make_shared<const ui_tree>(std::move(root));
#if LUIL_POINTER_TEST_WEBVIEW
                win32::ui_webview webview {};
                webview.id = u8"web";
                webview.user_data_folder = profile_;
                webview.policy.allowed_schemes = { u8"http" };
                webview.navigate_revision = 1;
                webview.navigate_url = server_.url("/pointer");
                webview.on_event = [](win32::webview_event event) { return app_message { std::move(event) }; };
                frame->webviews.push_back(std::move(webview));
#endif
                return frame;
            }

            [[nodiscard]] app_message make_close_message() override
            {
                return app_message { close_intent {} };
            }
            [[nodiscard]] bool shutdown_completed() const override
            {
                return closed_.load();
            }

        private:
            [[nodiscard]] zoom_view_config zoom_config() const
            {
                return {
                    .owner = u8"zoom",
                    .zoom = zoom_.zoom,
                    .origin = zoom_.origin,
                    .zoom_by = [](const float factor, const zoom_point anchor) { return make_app_action(zoom_intent { factor, anchor }); },
                    .pan_by = [](const zoom_point delta) { return make_app_action(pan_intent { delta }); },
                    .zoom_to = [](const float value) { return make_app_action(zoom_to_intent { value }); },
                };
            }
            zoom_view_state zoom_ {};
            std::atomic<win32::app_host*> host_ { nullptr };
            std::atomic<bool> closed_ { false };
            metrics_intent metrics_ {};
            int left_ { 0 };
            int right_ { 0 };
            float scroll_ { 0.0f };
            bool enabled_ { true };
            bool web_visible_ { true };
            bool modal_ { false };
            bool zoom_visible_ { false };
            bool config_ok_ { true };
            int config_revision_ { 0 };
            touch_gesture_config touch_ {};
            text::text_edit_state text_ { .text = u8"alpha beta gamma delta" };
            nlohmann::json web_ = nlohmann::json::object();
            std::u8string web_error_ {};
            std::u8string profile_ {};
            loopback_http_server server_ {};
        };

        class fixture_policy final : public interaction_policy
        {
        public:
            [[nodiscard]] std::optional<text_input_target> text_target_of(const ui_element_kind kind) const override
            {
                return kind == text_kind ? std::optional { text_target } : std::nullopt;
            }
            [[nodiscard]] input_action make_text_edit_action(const text_edit_request& request) const override
            {
                return make_app_action(edit_intent { request });
            }
        };

        class fixture_delegate final : public win32::window_delegate
        {
        public:
            explicit fixture_delegate(fixture_driver& driver)
                : driver_ { driver }
            {}
            void on_started(win32::app_host& host) override
            {
                host_ = &host;
                driver_.attach(host);
                EnumThreadWindows(
                    GetCurrentThreadId(),
                    [](const HWND window, const LPARAM context) -> BOOL {
                        auto* self { reinterpret_cast<fixture_delegate*>(context) };
                        wchar_t class_name[64] {};
                        GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
                        if (std::wstring_view { class_name } != L"Luil.Pointer.Integration")
                            return TRUE;
                        self->driver_.native.observer_ready.store(SetWindowSubclass(window, observe, 1, reinterpret_cast<DWORD_PTR>(self)) != FALSE);
                        return FALSE;
                    },
                    reinterpret_cast<LPARAM>(this));
                host.post_app_message(app_message { refresh_intent {} });
            }
            [[nodiscard]] app_message make_window_metrics_message(const float width, const float height, const float scale) override
            {
                return app_message { metrics_intent { width, height, scale } };
            }

        private:
            static LRESULT CALLBACK observe(const HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam, const UINT_PTR id, const DWORD_PTR context)
            {
                auto* self { reinterpret_cast<fixture_delegate*>(context) };
                bool changed { false };
                if (message == WM_SETCURSOR)
                {
                    ++self->driver_.native.set_cursor;
                    changed = true;
                }
                if (message == WM_POINTERDOWN)
                    ++self->driver_.native.pointer_down;
                if ((message == WM_POINTERUP || message == WM_POINTERUPDATE) && IS_POINTER_CANCELED_WPARAM(wparam))
                {
                    ++self->driver_.native.canceled_message;
                    changed = true;
                }
                POINTER_INPUT_TYPE type {};
                const UINT32 pointer_id { GET_POINTERID_WPARAM(wparam) };
                if ((message >= WM_POINTERUPDATE && message <= WM_POINTERCAPTURECHANGED) || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_CAPTURECHANGED)
                {
                    changed = true;
                    POINTER_INFO info {};
                    if (message >= WM_POINTERUPDATE && message <= WM_POINTERCAPTURECHANGED)
                        static_cast<void>(GetPointerInfo(pointer_id, &info));
                    const std::lock_guard lock { self->driver_.native.trace_mutex };
                    auto& trace { self->driver_.native.trace };
                    trace.push_back({ message, info.pointerId, info.pointerType, info.pointerFlags, GetMessageExtraInfo(), HIWORD(wparam), info.frameId });
                    if ((message == WM_POINTERDOWN || message == WM_POINTERUPDATE) && info.pointerType == PT_TOUCH)
                    {
                        std::array<POINTER_TOUCH_INFO, 2> contacts {};
                        UINT32 count { 2 };
                        if (GetPointerFrameTouchInfo(pointer_id, &count, contacts.data()))
                        {
                            auto& frame { self->driver_.native.touch_frame };
                            frame = nlohmann::json::array();
                            for (UINT32 index { 0 }; index < count; ++index)
                                frame.push_back({ contacts[index].pointerInfo.pointerId, contacts[index].pointerInfo.pointerFlags });
                        }
                    }
                    if (trace.size() > 24)
                        trace.erase(trace.begin());
                }
                if (message == WM_POINTERDOWN && GetPointerType(pointer_id, &type))
                {
                    if (type == PT_TOUCH)
                        ++self->driver_.native.touch_down;
                    else if (type == PT_PEN)
                    {
                        ++self->driver_.native.pen_down;
                        self->driver_.native.touch_up_at_pen_down.store(self->driver_.native.touch_up.load());
                    }
                    changed = true;
                }
                else if (message == WM_POINTERUPDATE && GetPointerType(pointer_id, &type) && type == PT_PEN)
                {
                    POINTER_INFO info {};
                    if (GetPointerInfo(pointer_id, &info) && !(info.pointerFlags & POINTER_FLAG_INCONTACT))
                    {
                        ++self->driver_.native.pen_hover;
                        changed = true;
                    }
                }
                else if (message == WM_LBUTTONDOWN)
                {
                    ++self->driver_.native.mouse_down;
                    self->driver_.native.touch_up_at_mouse_down.store(self->driver_.native.touch_up.load());
                    changed = true;
                }
                else if (message == WM_POINTERCAPTURECHANGED)
                {
                    ++self->driver_.native.capture_changed;
                    changed = true;
                }
                if (message == WM_POINTERUP && GetPointerType(pointer_id, &type) && type == PT_TOUCH)
                {
                    ++self->driver_.native.touch_up;
                    POINTER_INFO info {};
                    if (GetPointerInfo(pointer_id, &info))
                    {
                        self->driver_.native.touch_up_flags.store(info.pointerFlags);
                        self->driver_.native.touch_up_word_flags.store(HIWORD(wparam));
                        if (info.pointerFlags & POINTER_FLAG_CANCELED)
                            ++self->driver_.native.touch_canceled;
                        changed = true;
                    }
                }
                if (message == WM_NCDESTROY)
                    RemoveWindowSubclass(window, observe, id);

                const LRESULT result { DefSubclassProc(window, message, wparam, lparam) };
                if (changed)
                    self->host_->post_app_message(app_message { refresh_intent {} });
                return result;
            }
            fixture_driver& driver_;
            win32::app_host* host_ { nullptr };
        };
    } // namespace

    int run_pointer_fixture(const std::filesystem::path& profile)
    {
        win32::enable_per_monitor_dpi_awareness();
        const win32::com_sta_scope com {};
        win32::window_config config {};
        config.class_name = L"Luil.Pointer.Integration";
        config.caption.title = u8"luil native pointer integration";
        config.initial_width = 800;
        config.initial_height = 620;
        config.initial_position = win32::window_position { 40, 40 };
        fixture_driver driver { profile.u8string() };
        fixture_policy policy {};
        fixture_delegate delegate { driver };
        return win32::run_application_window(config, { &driver, &policy, &delegate });
    }
} // namespace luil::testing

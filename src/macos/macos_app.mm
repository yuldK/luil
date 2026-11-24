#include "luil/macos/macos_app.h"

#include "host/file_dialog_gate.h"
#include "host/focus_request_tracker.h"
#include "host/font_registry.h"
#include "host/frame_state.h"
#include "host/overlay_input.h"
#include "host/popup_dismiss.h"
#include "host/popup_reconcile.h"
#include "host/skia_renderer.h"
#include "host/system_appearance_observer.h"
#include "host/text_input_host.h"
#include "luil/text/utf8_text.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_platform.h"
#include "macos/cpu_skia_renderer.h"
#include "macos/input_script.h"
#include "macos/macos_input.h"
#include "macos/macos_text.h"
#include "macos/webview_host.h"
#include "luil/ui/webview_element.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace luil::macos {
    class application;
}

// 창 내용 영역의 view다. 이벤트를 C++ 앱으로 넘기고, 입력기(NSTextInputClient)의 창구가 된다.
@interface LuilView : NSView <NSTextInputClient>
@property(nonatomic, assign) luil::macos::application* owner;
@end

// 창 내용의 그릇이다. Skia view와 웹뷰가 형제로 선다. 자식의 자리를 왼쪽 위 원점(물리 픽셀을 배율로 나눈 점)으로 적도록 뒤집는다.
@interface LuilContainerView : NSView
@end

// 창의 닫기·크기·배율·초점 알림을 C++ 앱으로 넘긴다.
@interface LuilWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) luil::macos::application* owner;
@end

// 앱 종료 요청(Command+Q, Dock의 종료)과 시스템 모양 변화를 C++ 앱으로 넘긴다.
@interface LuilApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) luil::macos::application* owner;
@end

namespace luil::macos {
    namespace {
        // 시간이 흘러야 바뀌는 그림을 이 간격보다 촘촘히 그리지 않는다 (Win32·Android와 같다).
        constexpr std::chrono::milliseconds continuous_repaint_interval { 16 };

        void log_error(const std::u8string& message)
        {
            std::fprintf(stderr, "luil: %.*s\n", static_cast<int>(message.size()), reinterpret_cast<const char*>(message.data()));
        }

        [[nodiscard]] std::chrono::steady_clock::time_point now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        // UI thread를 깨우는 길이다. 다른 thread가 부르면 메인 run loop에 블록을 하나 올린다.
        //  - 블록은 모든 공통 모드(`kCFRunLoopCommonModes`)에서 돈다 — 창 크기를 끄는 동안(이벤트 추적 모드)에도 깨어난다.
        //  - 같은 깨우기가 이미 올라가 있으면 다시 올리지 않는다.
        //  - 앱이 사라진 뒤 도착한 블록은 아무 일도 하지 않는다 (`target`이 비었다).
        struct wake_state
        {
            std::atomic<bool> scheduled { false };
            // 메인 thread만 읽고 쓴다.
            application* target { nullptr };
        };

        // 가족 이름 → typeface와 글자 → 대체 typeface를 함께 맡는다 (Android·Win32 host와 같은 모양이다).
        struct registry_font_resolver final : font_resolver, font_fallback
        {
            [[nodiscard]] SkTypeface* family(const std::u8string_view name) const override
            {
                return family_typeface(name).get();
            }

            [[nodiscard]] sk_sp<SkTypeface> for_codepoint(const char32_t codepoint) const override
            {
                return fallback_typeface(codepoint);
            }

            [[nodiscard]] sk_sp<SkTypeface> for_styled_codepoint(const char32_t codepoint, const font_style style) const override
            {
                return fallback_typeface(codepoint, style);
            }
        };

        [[nodiscard]] std::optional<ui_color> color_of(NSColor* const color)
        {
            NSColor* const rgb { [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] };
            if (rgb == nil)
                return std::nullopt;
            const auto channel = [](const CGFloat value) { return static_cast<std::uint8_t>(std::lround(std::clamp(value, CGFloat { 0 }, CGFloat { 1 }) * 255.0)); };
            return make_ui_color(channel(rgb.redComponent), channel(rgb.greenComponent), channel(rgb.blueComponent));
        }

        [[nodiscard]] NSCursor* cursor_for(const ui_cursor cursor)
        {
            switch (cursor)
            {
            case ui_cursor::text:
                return [NSCursor IBeamCursor];
            case ui_cursor::hand:
                return [NSCursor pointingHandCursor];
            case ui_cursor::grab:
                return [NSCursor openHandCursor];
            case ui_cursor::grabbing:
                return [NSCursor closedHandCursor];
            case ui_cursor::resize_horizontal:
                return [NSCursor resizeLeftRightCursor];
            case ui_cursor::resize_vertical:
                return [NSCursor resizeUpDownCursor];
            case ui_cursor::not_allowed:
                return [NSCursor operationNotAllowedCursor];
            default:
                return [NSCursor arrowCursor];
            }
        }
    }

    // AppKit 앱 하나의 수명이다. 메인 thread에서만 산다 — 그 thread가 luil의 UI thread다.
    //  - 입력기가 묻는 창구(`text_input_host`)도 맡는다. 초점을 가진 텍스트 칸을 tree에서 찾는다.
    class application final : public text_input_host
    {
        // 창 위에 겹쳐 그리는 popup 하나다 (Android host와 같은 모양이다).
        struct overlay_entry
        {
            std::u8string id {};
            std::shared_ptr<const ui_tree> tree {};
            // 표면 좌표다 (물리 픽셀).
            pixel_rect bounds {};
            bool border { true };
            std::function<input_action(popup_dismiss_reason)> dismiss {};
            bool requested { false };
            std::u8string anchor {};
            int anchor_x { 0 };
            int anchor_y { 0 };
            float scroll_y { 0.0f };
            float max_scroll_y { 0.0f };
            std::function<app_message(const rect_f&)> placed {};
            std::optional<rect_f> reported_bounds {};

            [[nodiscard]] float content_origin_y() const noexcept
            {
                return static_cast<float>(bounds.y) - scroll_y;
            }
        };

        // 입력기의 조합 상태다. 조합 글은 화면에만 보이고(`post_composition`), 확정되면 칸에 넣는다.
        struct composition_state
        {
            text_input_target target { text_input_target::none };
            // 조합을 시작한 때의 확정 글과, 조합 글이 대신하는 그 안의 구간이다 (UTF-8 byte).
            text_input_document base {};
            std::size_t begin { 0 };
            std::size_t end { 0 };
            std::u8string marked {};
        };

    public:
        application(const application_config& config, const application_environment& environment)
            : config_ { config }
            , environment_ { environment }
        {
        }

        application(const application&) = delete;
        application(application&&) = delete;
        application& operator=(const application&) = delete;
        application& operator=(application&&) = delete;

        ~application() override
        {
            wake_->target = nullptr;
            if (timer_ != nil)
                [timer_ invalidate];
            renderer_.reset();
            set_font_fallback(nullptr);
            set_system_accent(std::nullopt);
        }

        // --- text_input_host ---

        [[nodiscard]] std::optional<text_input_target> focused_text_target() const override
        {
            if (host_ == nullptr || environment_.policy == nullptr)
                return std::nullopt;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            if (interaction.focused_surface.empty() == false && find_overlay(interaction.focused_surface) == nullptr)
                return std::nullopt;
            return environment_.policy->text_target_of(interaction.focused_input.kind);
        }

        [[nodiscard]] text_input_document committed_document() const override
        {
            const text_location location { focused_text_location() };
            const std::optional<text_input_snapshot> value { location.element != nullptr ? location.element->text_input() : std::nullopt };
            if (value.has_value() == false)
                return {};
            return { std::u8string { value->text }, value->caret, value->anchor, value->applied_sequence, value->secret, value->multiline, value->read_only };
        }

        [[nodiscard]] std::optional<rect_f> text_rect(const text_input_document& document, const std::size_t begin, const std::size_t end) const override
        {
            const text_location location { focused_text_location() };
            if (location.element == nullptr)
                return std::nullopt;
            const text_measurer measure = [](const std::u8string_view text, const float pixel_size) {
                const SkFont font { configured_ui_typeface(), pixel_size };
                return measure_text(text, font);
            };
            std::optional<rect_f> span { location.element->text_span_bounds(text_span_query { document.text, document.caret, begin, end }, measure) };
            if (span.has_value())
            {
                span->x += location.origin_x;
                span->y += location.origin_y;
            }
            return span;
        }

        void post_composition(text_composition_event event) override
        {
            if (environment_.policy != nullptr)
                dispatch(environment_.policy->make_text_composition_action(event));
        }

        void post_edit(text_edit_request request) override
        {
            if (environment_.policy != nullptr)
                dispatch(environment_.policy->make_text_edit_action(request));
        }

        // --- 시작과 끝 ---

        [[nodiscard]] int run()
        {
            if (start() == false)
                return 1;
            if (config_.smoke_test)
            {
                // 첫 frame이 게시될 때까지 기다렸다가 한 번 그리고 끝낸다.
                const auto deadline { now() + std::chrono::seconds { 5 } };
                while (host_->acquire_ui_tree() == nullptr && now() < deadline && host_->faulted() == false)
                    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                const bool drawn { host_->acquire_ui_tree() != nullptr && render() };
                stop();
                return drawn ? 0 : 1;
            }
            [window_ makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            pump();
            luil_run_input_script(window_, view_);
            [NSApp run];
            stop();
            return 0;
        }

        // 다른 thread가 깨웠다.
        void on_wake()
        {
            execute_host_commands();
            dirty_ = true;
            pump();
        }

        // 창의 닫기 단추·Command+Q다. 앱이 닫힘을 받아 가면(`ui_frame::close`) 그 동작만 낸다.
        void request_close(const bool from_app)
        {
            if (exiting_ == false && host_ != nullptr && host_->faulted() == false)
            {
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame != nullptr && frame->close != nullptr)
                {
                    if (close_requested_frame_ == frame && from_app == false)
                        return;
                    close_requested_frame_ = frame;
                    try
                    {
                        dispatch(frame->close());
                    }
                    catch (...)
                    {
                        log_error(u8"The main window close action threw an exception.");
                    }
                    return;
                }
            }
            quit();
        }

        // 스레드를 모두 정리하고 run loop를 끝낸다. 창은 그 뒤에 닫는다.
        void quit()
        {
            if (quitting_)
                return;
            quitting_ = true;
            stop();
            [window_ orderOut:nil];
            [NSApp stop:nil];
            // `stop:`은 이벤트 하나를 처리한 뒤에야 run을 끝낸다. 빈 이벤트를 넣어 곧바로 끝나게 한다.
            NSEvent* const event { [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
                                                  windowNumber:0 context:nil subtype:0 data1:0 data2:0] };
            [NSApp postEvent:event atStart:YES];
        }

        // --- 창 알림 ---

        void on_resized()
        {
            refresh_metrics();
            pump();
        }

        void on_key_window(const bool key)
        {
            if (host_ == nullptr)
                return;
            if (key)
                host_->post_raw_input(surface_focus_gained_event { {} });
            else
            {
                commit_composition();
                static_cast<void>(dismiss_popups(popup_dismiss_reason::activation_changed));
                host_->post_raw_input(surface_focus_lost_event { {} });
            }
            post_lifecycle(key ? app_lifecycle::foreground : app_lifecycle::background);
            dirty_ = true;
            pump();
        }

        void on_moved()
        {
            static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_moved));
            pump();
        }

        void on_appearance_changed()
        {
            refresh_appearance();
            post_appearance();
            post_metrics();
            dirty_ = true;
            pump();
        }

        // --- 입력 ---

        void mouse_moved(NSEvent* const event)
        {
            const NSPoint point { surface_point(event) };
            // 웹뷰 위다. 웹뷰가 포인터를 받으므로 tree의 hover를 거두고 커서는 웹뷰에 맡긴다.
            if (webviews_ != nullptr && webviews_->covers(static_cast<float>(point.x), static_cast<float>(point.y)))
            {
                if (pointer_over_webview_ == false)
                {
                    pointer_over_webview_ = true;
                    post_events({ raw_input_event { pointer_left_event { {}, pointer_device::mouse } } });
                }
                return;
            }
            pointer_over_webview_ = false;
            pointer_moved_event moved {};
            moved.x = static_cast<float>(point.x);
            moved.y = static_cast<float>(point.y);
            moved.time = now();
            moved.device = pointer_device::mouse;
            moved.in_contact = false;
            moved.scale = scale_;
            apply_modifiers(event, moved.control, moved.shift, moved.alt);
            post_events({ raw_input_event { std::move(moved) } });
            update_cursor(point);
        }

        void mouse_dragged(NSEvent* const event)
        {
            mouse_moved(event);
        }

        void mouse_button(NSEvent* const event, const pointer_button button, const bool pressed)
        {
            const NSPoint point { surface_point(event) };
            if (pressed)
            {
                pointer_pressed_event value {};
                value.x = static_cast<float>(point.x);
                value.y = static_cast<float>(point.y);
                value.button = button;
                value.time = now();
                value.device = pointer_device::mouse;
                value.scale = scale_;
                apply_modifiers(event, value.control, value.shift, value.alt);
                post_events({ raw_input_event { std::move(value) } });
                return;
            }
            pointer_released_event value {};
            value.x = static_cast<float>(point.x);
            value.y = static_cast<float>(point.y);
            value.button = button;
            value.time = now();
            value.device = pointer_device::mouse;
            apply_modifiers(event, value.control, value.shift, value.alt);
            post_events({ raw_input_event { std::move(value) } });
        }

        void mouse_exited()
        {
            post_events({ raw_input_event { pointer_left_event { {}, pointer_device::mouse } } });
            [[NSCursor arrowCursor] set];
        }

        void scroll_wheel(NSEvent* const event)
        {
            const NSPoint point { surface_point(event) };
            // 정밀 스크롤(트랙패드)은 점 단위, 휠은 줄 단위다. 휠 한 눈금(줄 세 개 남짓)을 120으로 본다.
            //  - 자연스러운 스크롤을 켠 사용자는 delta가 이미 뒤집혀 온다. 그대로 쓰면 손가락이 내용을 끄는 방향이다.
            const CGFloat per_detent { [event hasPreciseScrollingDeltas] ? CGFloat { 40 } : CGFloat { 1 } };
            const auto wheel = [&](const CGFloat value, const bool horizontal) {
                mouse_wheel_event wheel_event {};
                wheel_event.x = static_cast<float>(point.x);
                wheel_event.y = static_cast<float>(point.y);
                // 가로는 Win32와 같이 오른쪽이 양수다. AppKit은 왼쪽으로 미는 것이 양수다.
                wheel_event.delta = static_cast<float>((horizontal ? -value : value) / per_detent * 120.0);
                wheel_event.time = now();
                bool alt { false };
                apply_modifiers(event, wheel_event.control, wheel_event.shift, alt);
                wheel_event.horizontal = horizontal;
                return raw_input_event { std::move(wheel_event) };
            };
            std::vector<raw_input_event> events {};
            if (const CGFloat dy { [event scrollingDeltaY] }; dy != 0)
                events.push_back(wheel(dy, false));
            if (const CGFloat dx { [event scrollingDeltaX] }; dx != 0)
                events.push_back(wheel(dx, true));
            if (events.empty() == false)
                post_events(std::move(events));
        }

        // 키 하나다. 앞 갈래는 키 이벤트를, 뒤 갈래(입력기)는 글자를 낸다.
        void key_down(NSEvent* const event, LuilView* const view)
        {
            if (host_ == nullptr)
                return;
            const key_input input { key_input_of(event) };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            const bool key_capture { interaction.key_capture || interaction.menu_keys };

            // Esc가 popup을 닫았으면 키를 삼킨다 (Win32·Android와 같다).
            if (input.key_code == 0x35 && composition_.has_value() == false && dismiss_popups(popup_dismiss_reason::escape_key))
            {
                pump();
                return;
            }
            // Command·Control 조합과 키 잡기는 입력기를 거치지 않는 키다.
            if (input.command || input.control || key_capture)
            {
                commit_composition();
                if (const auto key { key_event_for(input, key_capture) }; key.has_value())
                    post_events({ raw_input_event { *key } });
                pump();
                return;
            }
            // 그 밖의 키는 입력기가 먼저 본다. 입력기가 먹지 않은 키는 `do_command`로 돌아온다.
            current_key_ = input;
            key_handled_ = false;
            [view interpretKeyEvents:@[ event ]];
            current_key_.reset();
            pump();
        }

        // 입력기가 넘긴 명령이다 (`doCommandBySelector:`). 이 키는 앱의 키다.
        void do_command()
        {
            if (current_key_.has_value() == false || key_handled_)
                return;
            key_handled_ = true;
            const key_input input { *current_key_ };
            std::vector<raw_input_event> events {};
            if (const auto key { key_event_for(input, false) }; key.has_value())
                events.push_back(raw_input_event { *key });
            // Backspace는 누름이면서 글자다 (Win32의 WM_KEYDOWN 뒤 WM_CHAR U+0008).
            if (backspace_key(input.key_code))
                events.push_back(raw_input_event { character_typed_event { backspace_character(input), input.time } });
            post_events(std::move(events));
        }

        // 입력기가 확정한 글이다 (`insertText:`).
        void insert_text(const std::u8string& text)
        {
            if (composition_.has_value())
            {
                finish_composition(text);
                return;
            }
            if (current_key_.has_value() && key_handled_)
                return;
            const auto time { now() };
            std::vector<raw_input_event> events {};
            // Space는 글자이면서 실행 키다 (Win32와 같이 둘 다 보낸다).
            if (current_key_.has_value())
            {
                key_handled_ = true;
                if (text == u8" ")
                    if (const auto key { key_event_for(*current_key_, false) }; key.has_value())
                        events.push_back(raw_input_event { *key });
            }
            bool function_key { false };
            for (const char32_t character : decode_utf8(text))
            {
                if (function_key_character(character))
                {
                    function_key = true;
                    continue;
                }
                events.push_back(raw_input_event { character_typed_event { character, time } });
            }
            // 기능 키(F5 등)는 사설 글자로 온다. 글자가 아니라 키다.
            if (function_key && current_key_.has_value())
                if (const auto key { key_event_for(*current_key_, false) }; key.has_value())
                    events.push_back(raw_input_event { *key });
            post_events(std::move(events));
        }

        // 입력기의 조합 글이다 (`setMarkedText:`). `selected`는 조합 글 안의 caret이다 (UTF-8 byte).
        void set_marked_text(const std::u8string& marked, const std::size_t selected)
        {
            if (current_key_.has_value())
                key_handled_ = true;
            const std::optional<text_input_target> target { focused_text_target() };
            if (target.has_value() == false)
            {
                composition_.reset();
                return;
            }
            if (composition_.has_value() == false || composition_->target != *target)
            {
                composition_state state {};
                state.target = *target;
                state.base = committed_document();
                state.begin = std::min({ state.base.caret, state.base.anchor, state.base.text.size() });
                state.end = std::min(std::max(state.base.caret, state.base.anchor), state.base.text.size());
                composition_ = std::move(state);
            }
            if (marked.empty())
            {
                // 조합 글을 다 지웠다. 확정할 것 없이 조합을 끝낸다.
                post_composition(text_composition_event { composition_->target, {}, 0, 0, 0, false });
                composition_.reset();
                return;
            }
            composition_->marked = marked;
            const std::u8string& text { composition_->base.text };
            std::u8string shown { text.substr(0, composition_->begin) };
            shown += marked;
            shown += text.substr(composition_->end);
            text_composition_event event {};
            event.target = composition_->target;
            event.text = std::move(shown);
            event.caret = composition_->begin + std::min(selected, marked.size());
            event.composing_begin = composition_->begin;
            event.composing_end = composition_->begin + marked.size();
            event.composing = true;
            post_composition(std::move(event));
        }

        [[nodiscard]] bool has_marked_text() const noexcept
        {
            return composition_.has_value();
        }

        [[nodiscard]] std::size_t marked_length_utf16() const
        {
            return composition_.has_value() ? utf16_length(composition_->marked) : 0;
        }

        [[nodiscard]] std::size_t marked_begin_utf16() const
        {
            return composition_.has_value() ? utf16_length(std::u8string_view { composition_->base.text }.substr(0, composition_->begin)) : 0;
        }

        // 입력기가 조합을 그대로 확정해 달라고 했다 (`unmarkText`), 또는 누름·초점 이동이 조합을 끝낸다.
        void commit_composition()
        {
            if (composition_.has_value() == false)
                return;
            const std::u8string marked { composition_->marked };
            finish_composition(marked);
            [[NSTextInputContext currentInputContext] discardMarkedText];
        }

        // 후보 창이 설 자리다 (화면 좌표, 점).
        [[nodiscard]] NSRect candidate_rect() const
        {
            const text_location location { focused_text_location() };
            text_input_document document { committed_document() };
            std::size_t begin { std::min(document.caret, document.text.size()) };
            if (composition_.has_value())
            {
                document.text = composition_->base.text.substr(0, composition_->begin) + composition_->marked + composition_->base.text.substr(composition_->end);
                begin = composition_->begin;
                document.caret = begin + composition_->marked.size();
            }
            const std::optional<rect_f> span { text_rect(document, begin, begin) };
            if (span.has_value() == false || window_ == nil)
                return NSZeroRect;
            const NSRect local { NSMakeRect(span->x / scale_, span->y / scale_, std::max(1.0f, span->width / scale_), span->height / scale_) };
            const NSRect in_window { [view_ convertRect:local toView:nil] };
            return [window_ convertRectToScreen:in_window];
        }

        // 창 내용의 합성을 PNG로 적는다 (개발용, 입력 대본의 `snapshot`). 아래층 웹뷰 → Skia frame → 위층 웹뷰의 차례다.
        void snapshot(NSString* const path)
        {
            if (webviews_ == nullptr || path == nil)
                return;
            CGImageRef const frame { (__bridge CGImageRef)view_.layer.contents };
            if (frame == nullptr)
                return;
            CGImageRetain(frame);
            const int width { surface_width_ };
            const int height { surface_height_ };
            NSString* const target { [path copy] };
            webviews_->snapshot([frame, width, height, target](std::vector<webview_host::snapshot_layer> layers) {
                CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
                CGContextRef const context { CGBitmapContextCreate(nullptr, static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 0, space,
                    static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little)) };
                CGColorSpaceRelease(space);
                if (context != nullptr)
                {
                    // CoreGraphics는 왼쪽 아래가 원점이다. 위에서부터 잰 자리를 뒤집는다.
                    const auto rect = [height](const int x, const int y, const int w, const int h) { return CGRectMake(x, height - y - h, w, h); };
                    CGContextSetFillColorWithColor(context, [NSColor windowBackgroundColor].CGColor);
                    CGContextFillRect(context, CGRectMake(0, 0, width, height));
                    for (const auto underlay : { true, false })
                    {
                        if (underlay == false)
                            CGContextDrawImage(context, CGRectMake(0, 0, width, height), frame);
                        for (const webview_host::snapshot_layer& layer : layers)
                            if (layer.image != nullptr && (layer.layer == webview_layer::underlay) == underlay)
                                CGContextDrawImage(context, rect(layer.x, layer.y, layer.width, layer.height), static_cast<CGImageRef>(layer.image));
                    }
                    if (CGImageRef const composed { CGBitmapContextCreateImage(context) }; composed != nullptr)
                    {
                        NSBitmapImageRep* const bitmap { [[NSBitmapImageRep alloc] initWithCGImage:composed] };
                        [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:target atomically:YES];
                        CGImageRelease(composed);
                    }
                    CGContextRelease(context);
                }
                for (const webview_host::snapshot_layer& layer : layers)
                    if (layer.image != nullptr)
                        CGImageRelease(static_cast<CGImageRef>(layer.image));
                CGImageRelease(frame);
            });
        }

        // view 좌표의 점(점 단위)이 아래층 웹뷰의 구멍 안인가. 참이면 포인터가 Skia view를 지나 웹뷰로 간다.
        [[nodiscard]] bool passes_through(const NSPoint point) const
        {
            return webviews_ != nullptr && webviews_->passes_through(static_cast<float>(point.x * scale_), static_cast<float>(point.y * scale_));
        }

        // 메뉴의 편집 명령이다 (오려 두기·복사하기·붙여넣기·전체 선택). Command 단축키와 같은 키로 보낸다.
        void menu_edit(const key_code key, const bool shift)
        {
            if (host_ == nullptr)
                return;
            key_pressed_event event { key, false, shift, false, false, now() };
            event.primary_shortcut = true;
            event.word_navigation = false;
            event.meta = true;
            post_events({ raw_input_event { std::move(event) } });
            pump();
        }

    private:
        [[nodiscard]] bool start()
        {
            if (environment_.driver == nullptr)
            {
                log_error(u8"run_application needs a logic driver.");
                return false;
            }
            if (renderer_mode_requires_gpu(config_.renderer))
            {
                log_error(u8"The macOS host has no GPU renderer yet. Use the automatic or cpu renderer.");
                return false;
            }

            refresh_user_ui_language();
            // 데스크톱이다. 창의 제목 막대는 시스템의 것이라 앱이 caption을 그리지 않는다.
            set_ui_platform({
                .form_factor = ui_form_factor::desktop,
                .window_caption = false,
                .rounded_popup_corners = true,
            });
            set_font_fallback(&font_resolver_);

            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            application_delegate_ = [LuilApplicationDelegate new];
            application_delegate_.owner = this;
            [NSApp setDelegate:application_delegate_];
            install_menu();
            create_window();
            refresh_appearance();

            wake_->target = this;
            const std::shared_ptr<wake_state> wake { wake_ };
            const auto signal = [wake]() noexcept {
                if (wake->scheduled.exchange(true))
                    return true;
                // 블록이 shared_ptr를 값으로 잡게 지역 사본을 둔다 (람다의 멤버는 람다 객체를 거쳐 잡힌다).
                const std::shared_ptr<wake_state> held { wake };
                CFRunLoopRef const loop { CFRunLoopGetMain() };
                CFRunLoopPerformBlock(loop, kCFRunLoopCommonModes, ^{
                  held->scheduled.store(false);
                  if (held->target != nullptr)
                      held->target->on_wake();
                });
                CFRunLoopWakeUp(loop);
                return true;
            };
            app_host::config host_config {};
            host_config.wake.snapshot = signal;
            host_config.wake.ui_command = [signal] { static_cast<void>(signal()); };
            host_config.wake.app_ui_command = [signal] { static_cast<void>(signal()); };
            host_config.wake.clipboard = [signal] { static_cast<void>(signal()); };
            host_config.interaction.double_click_time = std::chrono::milliseconds { static_cast<int>(std::lround([NSEvent doubleClickInterval] * 1000.0)) };
            host_config.interaction.touch = config_.touch;
            router_.set_touch_config(config_.touch);
            if (app_message message { observe_appearance() }; message.empty() == false)
                host_config.initial_messages.push_back(std::move(message));
            if (app_message message { metrics_message() }; message.empty() == false)
                host_config.initial_messages.push_back(std::move(message));
            host_ = std::make_unique<app_host>(std::move(host_config), *environment_.driver, environment_.policy);
            webviews_->set_deliver([this](app_message message) {
                if (host_ != nullptr)
                    host_->post_app_message(std::move(message));
            });
            // 페이지를 눌러 키보드 초점이 웹뷰로 갔다. 논리 초점을 그 자리표로 옮긴다 (Windows의 `GotFocus`와 같은 자리).
            webviews_->set_focus_reporter([this](const std::u8string& id) {
                if (host_ != nullptr)
                    host_->post_raw_input(access_focus_event { ui_element_id { ui_element_kind::webview, id }, {}, now() });
            });

            create_renderer();
            if (renderer_ == nullptr)
                return false;
            if (environment_.delegate != nullptr)
                environment_.delegate->on_started(*host_);
            return true;
        }

        void stop()
        {
            if (stopped_ || host_ == nullptr)
                return;
            stopped_ = true;
            if (timer_ != nil)
            {
                [timer_ invalidate];
                timer_ = nil;
            }
            // 웹뷰 사건이 끝나는 host로 가지 않게 먼저 거둔다.
            if (webviews_ != nullptr)
                webviews_->shutdown();
            host_->shutdown();
        }

        void install_menu()
        {
            NSMenu* const bar { [NSMenu new] };
            NSMenuItem* const app_item { [NSMenuItem new] };
            [bar addItem:app_item];
            NSMenu* const app_menu { [NSMenu new] };
            NSString* const title { make_ns_string(config_.title) };
            NSString* const quit_title { [@"Quit " stringByAppendingString:title != nil ? title : @"luil"] };
            [app_menu addItemWithTitle:quit_title action:@selector(terminate:) keyEquivalent:@"q"];
            [app_item setSubmenu:app_menu];

            // 편집 메뉴는 메뉴 막대의 자리만 세운다. 단축키는 view가 직접 받는다 (Command 조합은 입력기를 거치지 않는다).
            NSMenuItem* const edit_item { [NSMenuItem new] };
            [bar addItem:edit_item];
            NSMenu* const edit_menu { [[NSMenu alloc] initWithTitle:@"Edit"] };
            [edit_menu addItemWithTitle:@"Undo" action:@selector(luilUndo:) keyEquivalent:@""];
            [edit_menu addItemWithTitle:@"Redo" action:@selector(luilRedo:) keyEquivalent:@""];
            [edit_menu addItem:[NSMenuItem separatorItem]];
            [edit_menu addItemWithTitle:@"Cut" action:@selector(luilCut:) keyEquivalent:@""];
            [edit_menu addItemWithTitle:@"Copy" action:@selector(luilCopy:) keyEquivalent:@""];
            [edit_menu addItemWithTitle:@"Paste" action:@selector(luilPaste:) keyEquivalent:@""];
            [edit_menu addItemWithTitle:@"Select All" action:@selector(luilSelectAll:) keyEquivalent:@""];
            [edit_item setSubmenu:edit_menu];
            [NSApp setMainMenu:bar];
        }

        void create_window()
        {
            const NSRect frame { NSMakeRect(0, 0, config_.initial_width, config_.initial_height) };
            const NSWindowStyleMask style { NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable };
            window_ = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
            window_.releasedWhenClosed = NO;
            window_.title = make_ns_string(config_.title) ?: @"luil";
            window_.contentMinSize = NSMakeSize(config_.minimum_width, config_.minimum_height);
            window_.acceptsMouseMovedEvents = YES;
            window_.tabbingMode = NSWindowTabbingModeDisallowed;
            window_delegate_ = [LuilWindowDelegate new];
            window_delegate_.owner = this;
            window_.delegate = window_delegate_;

            // 창 내용은 그릇 view 하나이고, 그 안에 Skia view와 웹뷰들이 형제로 선다 (macos/webview_host.h).
            //  - Skia view는 불투명하지 않다. 아래층 웹뷰의 구멍(알파 0)으로 그 아래가 보인다.
            container_ = [[LuilContainerView alloc] initWithFrame:frame];
            container_.wantsLayer = YES;
            container_.layer.backgroundColor = [NSColor windowBackgroundColor].CGColor;
            view_ = [[LuilView alloc] initWithFrame:container_.bounds];
            view_.owner = this;
            view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            view_.wantsLayer = YES;
            view_.layer.contentsGravity = kCAGravityTopLeft;
            view_.layer.opaque = NO;
            [container_ addSubview:view_];
            window_.contentView = container_;
            [window_ makeFirstResponder:view_];
            webviews_ = std::make_unique<webview_host>(container_, view_);
            NSTrackingArea* const tracking { [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                                          options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                                            owner:view_
                                                                         userInfo:nil] };
            [view_ addTrackingArea:tracking];
            [window_ center];
            read_surface_size();
        }

        void create_renderer()
        {
            CALayer* const layer { view_.layer };
            renderer_factories factories {};
            factories.gpu_name = u8"Metal";
            factories.create_cpu = [layer] { return create_cpu_skia_renderer((__bridge void*)layer); };
            std::u8string error {};
            renderer_ = renderer_host::create(config_.renderer, renderer_fault_injection {}, std::move(factories), error);
            if (renderer_ == nullptr)
            {
                log_error(error);
                return;
            }
            if (renderer_->resize(surface_width_, surface_height_, error) == false)
                log_error(error);
        }

        // 창 내용 영역의 물리 크기와 배율을 읽는다.
        void read_surface_size()
        {
            const NSRect bounds { view_.bounds };
            scale_ = static_cast<float>(window_.backingScaleFactor > 0 ? window_.backingScaleFactor : 1.0);
            surface_width_ = std::max(1, static_cast<int>(std::lround(bounds.size.width * scale_)));
            surface_height_ = std::max(1, static_cast<int>(std::lround(bounds.size.height * scale_)));
        }

        void refresh_metrics()
        {
            const int previous_width { surface_width_ };
            const int previous_height { surface_height_ };
            read_surface_size();
            if (surface_width_ != previous_width || surface_height_ != previous_height)
            {
                static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_resized));
                overlay_frame_.reset();
            }
            if (renderer_ != nullptr)
            {
                std::u8string error {};
                if (renderer_->resize(surface_width_, surface_height_, error) == false)
                    log_error(error);
            }
            dirty_ = true;
            post_metrics();
        }

        [[nodiscard]] app_message metrics_message()
        {
            if (environment_.delegate == nullptr)
                return {};
            const float width { static_cast<float>(surface_width_) };
            const float height { static_cast<float>(surface_height_) };
            if (width == reported_width_ && height == reported_height_ && scale_ == reported_scale_ && system_.reduce_motion == reported_reduce_motion_)
                return {};
            reported_width_ = width;
            reported_height_ = height;
            reported_scale_ = scale_;
            reported_reduce_motion_ = system_.reduce_motion;
            window_metrics metrics { make_window_metrics(width, height, scale_) };
            metrics.reduce_motion = system_.reduce_motion;
            return environment_.delegate->make_metrics_message(metrics);
        }

        void post_metrics()
        {
            if (host_ == nullptr)
                return;
            if (app_message message { metrics_message() }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 시스템의 밝은·어두운 모양, 강조색, 대비, 움직임 줄이기, 투명도 줄이기를 읽는다.
        void refresh_appearance()
        {
            NSAppearance* const appearance { [NSApp effectiveAppearance] };
            NSAppearanceName const match { [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] };
            system_.prefers_light = [match isEqualToString:NSAppearanceNameDarkAqua] == NO;
            NSWorkspace* const workspace { [NSWorkspace sharedWorkspace] };
            system_.high_contrast = workspace.accessibilityDisplayShouldIncreaseContrast;
            system_.reduce_motion = workspace.accessibilityDisplayShouldReduceMotion;
            system_.transparency = workspace.accessibilityDisplayShouldReduceTransparency == NO;
            system_.accent = color_of([NSColor controlAccentColor]);
            set_system_accent(system_.accent);
        }

        [[nodiscard]] app_message observe_appearance()
        {
            try
            {
                return appearance_observer_.observe(system_, environment_.delegate);
            }
            catch (...)
            {
                log_error(u8"The system appearance delegate threw an exception.");
                return {};
            }
        }

        void post_appearance()
        {
            if (app_message message { observe_appearance() }; host_ != nullptr && message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        void post_lifecycle(const app_lifecycle lifecycle)
        {
            if (environment_.delegate == nullptr || host_ == nullptr)
                return;
            if (app_message message { environment_.delegate->make_lifecycle_message(lifecycle) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 그릴 것과 다음에 깰 시각을 정리한다. 이벤트·깨우기·시계가 모두 여기로 온다.
        void pump()
        {
            if (host_ == nullptr || stopped_)
                return;
            update_overlays();
            process_file_dialog();
            process_focus_request();
            if (host_->faulted() && quitting_ == false)
            {
                log_error(u8"The logic or input thread stopped on an exception. Closing the window.");
                quit();
                return;
            }
            if (update_deadline_.has_value() && now() >= *update_deadline_)
                dirty_ = true;
            if (dirty_ && renderer_ != nullptr)
                static_cast<void>(render());
            arm_timer();
        }

        // 다음에 깰 시각에 시계를 건다. 그림의 예고와 초점 요청의 기한 가운데 이른 쪽이다.
        void arm_timer()
        {
            std::optional<std::chrono::steady_clock::time_point> wake { update_deadline_ };
            if (const auto deadline { focus_tracker_.deadline() }; deadline.has_value())
                wake = wake.has_value() ? std::min(*wake, *deadline) : *deadline;
            if (wake == timer_deadline_)
                return;
            if (timer_ != nil)
            {
                [timer_ invalidate];
                timer_ = nil;
            }
            timer_deadline_ = wake;
            if (wake.has_value() == false)
                return;
            const double seconds { std::max(0.0, std::chrono::duration<double> { *wake - now() }.count()) };
            const std::shared_ptr<wake_state> state { wake_ };
            timer_ = [NSTimer timerWithTimeInterval:seconds
                                            repeats:NO
                                              block:^(NSTimer*) {
                                                if (state->target != nullptr)
                                                    state->target->on_timer();
                                              }];
            [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
        }

    public:
        void on_timer()
        {
            timer_ = nil;
            timer_deadline_.reset();
            pump();
        }

    private:
        void execute_host_commands()
        {
            if (host_ == nullptr)
                return;
            for (const ui_command command : host_->take_ui_commands())
                execute_command(command);
            for (const app_ui_command& command : host_->take_app_ui_commands())
                if (environment_.delegate != nullptr)
                    environment_.delegate->execute_app_ui_command(*host_, command);
            for (const clipboard_request& request : host_->take_clipboard_requests())
                execute_clipboard(request);
        }

        void execute_command(const ui_command command)
        {
            switch (command)
            {
            case ui_command::window_close:
                request_close(true);
                break;
            case ui_command::application_exit:
                exiting_ = true;
                quit();
                break;
            case ui_command::window_minimize:
                [window_ miniaturize:nil];
                break;
            case ui_command::window_toggle_maximize:
                [window_ zoom:nil];
                break;
            case ui_command::window_toggle_fullscreen:
                [window_ toggleFullScreen:nil];
                break;
            default:
                break;
            }
        }

        // 이벤트를 popup 자리로 옮겨 input thread에 보낸다 (Android host와 같은 길이다).
        void post_events(std::vector<raw_input_event> events)
        {
            if (host_ == nullptr)
                return;
            for (raw_input_event& event : events)
            {
                routed_input routed { router_.route(std::move(event)) };
                const bool closed_popup { routed.pressed_outside && dismiss_popups(popup_dismiss_reason::pointer_press_outside) };
                if (routed.wheel_outside)
                    static_cast<void>(dismiss_popups(popup_dismiss_reason::wheel_scrolled));
                if (routed.scroll.has_value())
                    scroll_overlay(*routed.scroll);
                for (raw_input_event& value : routed.events)
                {
                    if (auto* const pressed { std::get_if<pointer_pressed_event>(&value) }; pressed != nullptr)
                    {
                        pressed->closed_popup = closed_popup;
                        // 조합 중의 누름은 조합을 먼저 확정한다. 확정 글이 먼저 들어가야 누름이 낸 caret 이동이 그 위에 선다.
                        commit_composition();
                    }
                    host_->post_raw_input(std::move(value));
                }
            }
        }

        [[nodiscard]] NSPoint surface_point(NSEvent* const event) const
        {
            const NSPoint local { [view_ convertPoint:[event locationInWindow] fromView:nil] };
            return NSMakePoint(local.x * scale_, local.y * scale_);
        }

        static void apply_modifiers(NSEvent* const event, bool& control, bool& shift, bool& alt)
        {
            const NSEventModifierFlags flags { [event modifierFlags] };
            // macOS의 주 수정자는 Command다. Command+클릭은 Windows의 Ctrl+클릭(여럿 고르기)이다.
            control = (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
            shift = (flags & NSEventModifierFlagShift) != 0;
            alt = (flags & NSEventModifierFlagOption) != 0;
        }

        [[nodiscard]] static key_input key_input_of(NSEvent* const event)
        {
            key_input input {};
            input.key_code = [event keyCode];
            const NSEventModifierFlags flags { [event modifierFlags] };
            input.command = (flags & NSEventModifierFlagCommand) != 0;
            input.control = (flags & NSEventModifierFlagControl) != 0;
            input.option = (flags & NSEventModifierFlagOption) != 0;
            input.shift = (flags & NSEventModifierFlagShift) != 0;
            input.repeat = [event isARepeat];
            NSString* const base { [event charactersIgnoringModifiers] };
            if (base.length > 0)
                input.base_character = static_cast<char32_t>([base characterAtIndex:0]);
            input.time = now();
            return input;
        }

        void update_cursor(const NSPoint point)
        {
            if (host_ == nullptr)
                return;
            const float x { static_cast<float>(point.x) };
            const float y { static_cast<float>(point.y) };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            ui_cursor cursor { ui_cursor::inherit };
            for (auto entry { overlays_.rbegin() }; entry != overlays_.rend(); ++entry)
                if (x >= entry->bounds.x && y >= entry->bounds.y && x < entry->bounds.x + entry->bounds.width && y < entry->bounds.y + entry->bounds.height)
                {
                    if (entry->tree != nullptr)
                        cursor = entry->tree->cursor_at(x - static_cast<float>(entry->bounds.x), y - entry->content_origin_y(), interaction_for_surface(interaction, entry->id));
                    break;
                }
            if (cursor == ui_cursor::inherit && (overlays_.empty() || router_.has_areas() == false || cursor == ui_cursor::inherit))
                if (const std::shared_ptr<const ui_tree> tree { host_->acquire_ui_tree() }; tree != nullptr)
                    cursor = tree->cursor_at(x, y, interaction_for_surface(interaction, std::u8string {}));
            if (cursor == ui_cursor::none)
            {
                if (cursor_hidden_ == false)
                    [NSCursor setHiddenUntilMouseMoves:YES];
                cursor_hidden_ = true;
                return;
            }
            cursor_hidden_ = false;
            [cursor_for(cursor) set];
        }

        void process_file_dialog()
        {
            const auto frame { host_->acquire_frame() };
            if (frame == nullptr)
                return;
            if (frame->file_dialog.has_value() == false)
            {
                file_dialog_gate_.idle();
                return;
            }
            auto result { file_dialog_gate_.rejection(*frame->file_dialog) };
            if (result.has_value() == false)
            {
                if (file_dialog_gate_.try_begin(frame->file_dialog->id) == false)
                    return;
                result.emplace(frame->file_dialog->id, file_dialog_status::unsupported);
            }
            if (environment_.delegate != nullptr)
                host_->post_file_dialog_message(environment_.delegate->make_file_dialog_message(*result));
            file_dialog_gate_.finish();
        }

        void process_focus_request()
        {
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            const std::chrono::steady_clock::time_point time { now() };
            const std::optional<focus_request> published { frame != nullptr ? frame->focus : std::nullopt };
            const auto ready = [this, &frame](const focus_request& request) {
                if (request.target == ui_element_id {})
                    return true;
                const ui_tree* tree { nullptr };
                if (request.surface.empty())
                    tree = frame != nullptr ? frame->tree.get() : nullptr;
                else if (const overlay_entry* const overlay { find_overlay(request.surface) }; overlay != nullptr)
                    tree = overlay->tree.get();
                return tree != nullptr && focus_target_ready(*tree, request.target);
            };
            const focus_request_tracker::step step { focus_tracker_.advance(published, time, ready) };
            if (step.fulfil.has_value() && step.fulfil->target != ui_element_id {})
                host_->post_raw_input(access_focus_event { step.fulfil->target, step.fulfil->surface, time });
            if (frame != nullptr && frame->focus_report != nullptr)
                for (const focus_request_tracker::report& report : step.reports)
                    if (app_message message { frame->focus_report(report.id, report.outcome) }; message.empty() == false)
                        host_->post_app_message(std::move(message));
        }

        // frame의 popup을 창 위의 layer로 세운다. frame이 바뀔 때만 다시 세운다 (Android host와 같다).
        void update_overlays()
        {
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            if (frame == overlay_frame_ && frame != nullptr)
                return;
            overlay_frame_ = frame;
            if (webviews_ != nullptr)
                webviews_->synchronize(frame != nullptr ? std::span<const ui_webview> { frame->webviews } : std::span<const ui_webview> {});
            auto previous { std::move(overlays_) };
            overlays_.clear();
            if (frame != nullptr)
            {
                const float scale { scale_ };
                std::vector<popup_placement> logical {};
                for (const auto& popup : frame->popups)
                    logical.push_back({ popup.id, 0, 0, popup.tree != nullptr && popup.width > 0.0f ? 1 : 0, popup.height > 0.0f ? 1 : 0, popup.anchor });
                const std::vector<std::u8string> roots { u8"" };
                for (const auto index : order_popup_placements(logical, roots))
                {
                    const ui_popup& popup { frame->popups[index] };
                    const int width { std::min(surface_width_, static_cast<int>(std::lround(popup.width * scale))) };
                    const int full_height { static_cast<int>(std::lround(popup.height * scale)) };
                    const int height { std::min(surface_height_, full_height) };
                    const float maximum { static_cast<float>(full_height - height) };
                    const auto old { std::ranges::find(previous, popup.id, &overlay_entry::id) };
                    const float offset { old != previous.end() ? std::clamp(old->scroll_y, 0.0f, maximum) : 0.0f };
                    const std::optional<rect_f> reported_bounds { popup.placed != nullptr && old != previous.end() && old->anchor == popup.anchor ? old->reported_bounds : std::nullopt };
                    overlays_.push_back(overlay_entry {
                        popup.id, popup.tree, { 0, 0, width, height }, popup.border, popup.dismiss, false, popup.anchor, static_cast<int>(std::lround(popup.x * scale)),
                        static_cast<int>(std::lround(popup.y * scale)), offset, maximum, popup.placed, reported_bounds });
                }
                if (frame->windows.empty() == false && warned_windows_ == false)
                {
                    warned_windows_ = true;
                    log_error(u8"Secondary windows (ui_frame::windows) are not supported on macOS yet and are ignored.");
                }
                if (frame->docked_surfaces.empty() == false && warned_docked_ == false)
                {
                    warned_docked_ = true;
                    log_error(u8"Docked surfaces (ui_frame::docked_surfaces) are not supported on macOS and are ignored.");
                }
            }
            for (auto& old : previous)
                if (old.anchor.empty() == false && std::none_of(overlays_.begin(), overlays_.end(), [&old](const auto& entry) { return entry.id == old.anchor; }))
                    if (auto action { take_popup_dismiss_action(old.dismiss, old.requested, popup_dismiss_reason::anchor_closed) }; action.has_value())
                        dispatch(std::move(*action));
            place_overlays();
            dirty_ = true;
        }

        void place_overlays()
        {
            std::vector<overlay_area> areas {};
            areas.reserve(overlays_.size());
            for (overlay_entry& entry : overlays_)
            {
                const overlay_entry* const parent { entry.anchor.empty() ? nullptr : find_overlay(entry.anchor) };
                const int origin_x { parent != nullptr ? parent->bounds.x : 0 };
                const float origin_y { parent != nullptr ? parent->content_origin_y() : 0.0f };
                int desired_x { origin_x + entry.anchor_x };
                if (parent != nullptr && desired_x + entry.bounds.width > surface_width_)
                    desired_x = origin_x - entry.bounds.width;
                const int x { std::clamp(desired_x, 0, std::max(0, surface_width_ - entry.bounds.width)) };
                const int y { std::clamp(static_cast<int>(std::lround(origin_y + static_cast<float>(entry.anchor_y))), 0, std::max(0, surface_height_ - entry.bounds.height)) };
                entry.bounds.x = x;
                entry.bounds.y = y;
                areas.push_back(overlay_area {
                    entry.id, { static_cast<float>(x), static_cast<float>(y), static_cast<float>(entry.bounds.width), static_cast<float>(entry.bounds.height) }, entry.scroll_y,
                    entry.max_scroll_y, entry.tree.get(), scale_ });
                if (entry.placed != nullptr)
                {
                    const rect_f bounds { (x - origin_x) / scale_, (y - origin_y) / scale_, entry.bounds.width / scale_, entry.bounds.height / scale_ };
                    if (entry.reported_bounds != bounds)
                    {
                        entry.reported_bounds = bounds;
                        if (app_message message { entry.placed(bounds) }; message.empty() == false)
                            host_->post_app_message(std::move(message));
                    }
                }
            }
            router_.set_areas(std::move(areas));
        }

        void scroll_overlay(const overlay_scroll& scroll)
        {
            const auto entry { std::ranges::find(overlays_, scroll.surface, &overlay_entry::id) };
            if (entry == overlays_.end() || entry->scroll_y == scroll.offset)
                return;
            entry->scroll_y = scroll.offset;
            place_overlays();
            dirty_ = true;
        }

        [[nodiscard]] const overlay_entry* find_overlay(const std::u8string& id) const
        {
            const auto found { std::ranges::find_if(overlays_, [&id](const overlay_entry& entry) { return entry.id == id; }) };
            return found != overlays_.end() ? &*found : nullptr;
        }

        bool dismiss_popups(const popup_dismiss_reason reason)
        {
            bool dismissed { false };
            if (reason == popup_dismiss_reason::escape_key)
                for (auto position { overlays_.rbegin() }; position != overlays_.rend(); ++position)
                    if (position->anchor.empty() == false)
                    {
                        if (auto action { take_popup_dismiss_action(position->dismiss, position->requested, reason) }; action.has_value())
                        {
                            dispatch(std::move(*action));
                            return true;
                        }
                        return false;
                    }
            for (overlay_entry& entry : overlays_)
                if (std::optional<input_action> action { take_popup_dismiss_action(entry.dismiss, entry.requested, reason) }; action.has_value())
                {
                    dispatch(std::move(*action));
                    dismissed = true;
                }
            return dismissed;
        }

        // 초점을 가진 텍스트 칸과 그것이 사는 tree, 그 tree의 표면 원점이다 (물리 픽셀).
        struct text_location
        {
            std::shared_ptr<const ui_tree> tree {};
            const ui_element* element { nullptr };
            float origin_x { 0.0f };
            float origin_y { 0.0f };
        };

        [[nodiscard]] text_location focused_text_location() const
        {
            text_location location {};
            if (host_ == nullptr || focused_text_target().has_value() == false)
                return location;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            if (interaction.focused_surface.empty())
                location.tree = host_->acquire_ui_tree();
            else if (const overlay_entry* const overlay { find_overlay(interaction.focused_surface) }; overlay != nullptr)
            {
                location.tree = overlay->tree;
                location.origin_x = static_cast<float>(overlay->bounds.x);
                location.origin_y = overlay->content_origin_y();
            }
            if (location.tree != nullptr)
                location.element = location.tree->find(interaction.focused_input);
            return location;
        }

        // 조합을 끝내고 확정 글을 칸에 넣는다. 조합이 대신하던 구간(시작할 때의 선택)을 확정 글로 바꾼다.
        void finish_composition(const std::u8string& text)
        {
            if (composition_.has_value() == false)
                return;
            const composition_state state { std::move(*composition_) };
            composition_.reset();
            post_composition(text_composition_event { state.target, {}, 0, 0, 0, false });
            if (text.empty())
                return;
            text_edit_request request {};
            request.target = state.target;
            request.command = text::text_edit_command::insert;
            request.text = text;
            post_edit(std::move(request));
        }

        void dispatch(input_action action)
        {
            if (host_ == nullptr)
                return;
            if (auto* const message { std::get_if<app_message>(&action) }; message != nullptr && message->empty() == false)
                host_->post_app_message(std::move(*message));
            else if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr)
                execute_command(*command);
            else if (const auto* const command { std::get_if<app_ui_command>(&action) }; command != nullptr && environment_.delegate != nullptr)
                environment_.delegate->execute_app_ui_command(*host_, *command);
            else if (const auto* const copy { std::get_if<clipboard_copy_request>(&action) }; copy != nullptr)
                execute_clipboard(*copy);
            else if (const auto* const paste { std::get_if<clipboard_paste_request>(&action) }; paste != nullptr)
                execute_clipboard(*paste);
            else if (auto* const files { std::get_if<clipboard_files_copy_request>(&action) }; files != nullptr)
                execute_clipboard(std::move(*files));
            else if (const auto* const read { std::get_if<clipboard_files_paste_request>(&action) }; read != nullptr)
                execute_clipboard(*read);
        }

        // 클립보드 요청이다. 글은 일반 붙여넣기 판(NSPasteboard)의 문자열이다. 붙여넣기는 그 칸의 insert 편집으로 보낸다.
        void execute_clipboard(const clipboard_request& request)
        {
            NSPasteboard* const board { [NSPasteboard generalPasteboard] };
            if (std::holds_alternative<clipboard_files_copy_request>(request))
                return;
            if (const auto* const read { std::get_if<clipboard_files_paste_request>(&request) }; read != nullptr)
            {
                if (environment_.delegate == nullptr)
                    return;
                file_clipboard_content content {};
                content.supported = false;
                content.tag = read->tag;
                if (app_message message { environment_.delegate->make_file_clipboard_message(content) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (const auto* const read { std::get_if<clipboard_data_paste_request>(&request) }; read != nullptr)
            {
                if (environment_.delegate == nullptr)
                    return;
                clipboard_data data {};
                data.text = from_ns_string([board stringForType:NSPasteboardTypeString]);
                data.tag = read->tag;
                if (app_message message { environment_.delegate->make_clipboard_data_message(data) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (const auto* const copy { std::get_if<clipboard_copy_request>(&request) }; copy != nullptr)
            {
                [board clearContents];
                if (NSString* const text { make_ns_string(copy->text) }; text != nil)
                    [board setString:text forType:NSPasteboardTypeString];
                return;
            }
            if (const auto* const data { std::get_if<clipboard_data_copy_request>(&request) }; data != nullptr)
            {
                [board clearContents];
                if (NSString* const text { make_ns_string(data->text) }; text != nil)
                    [board setString:text forType:NSPasteboardTypeString];
                if (NSString* const html { make_ns_string(data->html) }; html != nil && html.length > 0)
                    [board setString:html forType:NSPasteboardTypeHTML];
                return;
            }
            const auto* const paste { std::get_if<clipboard_paste_request>(&request) };
            if (paste == nullptr || environment_.policy == nullptr)
                return;
            std::u8string text { from_ns_string([board stringForType:NSPasteboardTypeString]) };
            if (text.empty())
                return;
            // 한 줄 칸에는 줄 바꿈을 넣지 않는다 (Win32 붙여넣기와 같다).
            if (paste->multiline == false)
                std::ranges::replace_if(text, [](const char8_t character) { return character == u8'\r' || character == u8'\n'; }, u8' ');
            text_edit_request edit {};
            edit.target = paste->target;
            edit.command = text::text_edit_command::insert;
            edit.text = std::move(text);
            dispatch(environment_.policy->make_text_edit_action(edit));
        }

        // frame의 웹뷰마다 자리표의 자리를 웹뷰에 앉히고, 아래층이 비울 구멍을 낸다 (Win32 `apply_webviews`와 같은 규칙).
        //  - 가림은 자리표의 가운데와 안쪽 네 모서리 hit test다. popup은 같은 view에 겹쳐 그리므로 자리와 겹치는 popup도 가림이다.
        //  - 초점이 자리표에 **닿는 순간** 키보드 초점을 페이지로 넘긴다.
        [[nodiscard]] std::span<const pixel_rect> apply_webviews(const ui_tree* const tree, const interaction_snapshot& interaction)
        {
            webview_holes_.clear();
            if (webviews_ == nullptr || overlay_frame_ == nullptr)
                return webview_holes_;
            const ui_element_id focused { interaction_for_surface(interaction, std::u8string {}).focused };
            for (const ui_webview& want : overlay_frame_->webviews)
            {
                if (want.anchor.empty() == false || want.id.empty())
                    continue;
                const ui_element_id placeholder_id { ui_element_kind::webview, want.id };
                const auto* const placeholder { tree != nullptr ? dynamic_cast<const webview_element*>(tree->find(placeholder_id)) : nullptr };
                const std::optional<rect_f> visible { placeholder != nullptr ? tree->visible_bounds(*placeholder) : std::nullopt };
                const float tree_scale { placeholder != nullptr ? placeholder->scale() : scale_ };
                const webview_layout layout {
                    plan_webview_layout(visible, surface_width_, surface_height_, tree != nullptr && webview_occluded(*tree, placeholder, visible), tree_scale, want.layer),
                };
                // 다른 배율로 배치된 tree(화면을 옮긴 직후)의 자리는 주지 않는다. 새 배율의 tree가 오면 크기와 배율을 함께 준다.
                const bool arranged_for_surface { std::abs(tree_scale - scale_) < 0.001f };
                if (arranged_for_surface || placeholder == nullptr)
                    webviews_->apply_layout(want.id, layout, scale_);
                if (last_webview_focus_ != placeholder_id && focused == placeholder_id)
                    static_cast<void>(webviews_->move_focus_in(want.id));
                if (layout.punch_hole() && webviews_->opens_hole(want.id))
                    webview_holes_.push_back(pixel_rect { layout.x, layout.y, layout.width, layout.height });
            }
            last_webview_focus_ = focused;
            return webview_holes_;
        }

        [[nodiscard]] bool webview_occluded(const ui_tree& tree, const ui_element* const placeholder, const std::optional<rect_f>& visible) const
        {
            if (placeholder == nullptr || visible.has_value() == false)
                return true;
            for (const overlay_entry& entry : overlays_)
            {
                const pixel_rect& area { entry.bounds };
                if (static_cast<float>(area.x) < visible->x + visible->width && visible->x < static_cast<float>(area.x + area.width)
                    && static_cast<float>(area.y) < visible->y + visible->height && visible->y < static_cast<float>(area.y + area.height))
                    return true;
            }
            const float inset { 1.0f };
            const std::array<std::pair<float, float>, 5> probes {
                std::pair { visible->x + visible->width / 2.0f, visible->y + visible->height / 2.0f },
                std::pair { visible->x + inset, visible->y + inset },
                std::pair { visible->x + visible->width - inset, visible->y + inset },
                std::pair { visible->x + inset, visible->y + visible->height - inset },
                std::pair { visible->x + visible->width - inset, visible->y + visible->height - inset },
            };
            for (const auto& [x, y] : probes)
                if (tree.hit_test(x, y) != placeholder)
                    return true;
            return false;
        }

        [[nodiscard]] bool render()
        {
            frame_state state {};
            state.width = surface_width_;
            state.height = surface_height_;
            state.dpi_scale = scale_;
            state.reduce_motion = system_.reduce_motion;

            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            state.interaction = interaction_for_surface(interaction, std::u8string {});
            update_overlays();
            const std::shared_ptr<const ui_tree> tree { host_->acquire_ui_tree() };
            state.tree = tree.get();
            std::vector<overlay_layer> layers {};
            layers.reserve(overlays_.size());
            for (const overlay_entry& entry : overlays_)
                layers.push_back(overlay_layer { entry.tree.get(), entry.bounds, entry.border, interaction_for_surface(interaction, entry.id), entry.scroll_y });
            state.overlays = layers;
            state.holes = apply_webviews(tree.get(), interaction);
            const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
            state.theme = resolve_color_theme(appearance.theme, system_.high_contrast, system_.prefers_light);
            state.high_contrast = system_.contrast_colors;
            state.accent_id = appearance.accent_id;
            state.style = frame != nullptr ? frame->style.get() : nullptr;
            state.inspector = frame != nullptr ? &frame->inspector : nullptr;
            set_single_line_break(frame_style(state).text.line_break);

            const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
            set_configured_fonts(fonts.ui_family, fonts.code_family);
            const sk_sp<SkTypeface> code { configured_code_typeface() };
            state.code_typeface = code.get();
            state.fonts = &font_resolver_;

            std::u8string error {};
            const bool drawn { renderer_->render(state, error) };
            if (drawn == false || error.empty() == false)
                log_error(error);
            dirty_ = false;

            // 시간이 흘러야 바뀌는 그림(애니메이션·tooltip 지연·caret 깜빡임)의 다음 시각이다.
            const auto time { now() };
            const update_context update { .now = time, .reduce_motion = system_.reduce_motion };
            update_deadline_ = tree != nullptr ? tree->next_update(update, state.interaction) : std::nullopt;
            for (const overlay_layer& layer : layers)
                if (layer.tree != nullptr)
                    if (const auto next { layer.tree->next_update(update, layer.interaction) }; next.has_value())
                        if (update_deadline_.has_value() == false || *next < *update_deadline_)
                            update_deadline_ = next;
            if (update_deadline_.has_value() && *update_deadline_ <= time)
                update_deadline_ = time + continuous_repaint_interval;
            return drawn;
        }

        [[nodiscard]] static std::vector<char32_t> decode_utf8(const std::u8string_view text)
        {
            std::vector<char32_t> characters {};
            std::size_t offset { 0 };
            while (offset < text.size())
            {
                const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
                characters.push_back(decoded.codepoint);
                offset += std::max<std::size_t>(1, decoded.length);
            }
            return characters;
        }

        [[nodiscard]] static std::size_t utf16_length(const std::u8string_view text)
        {
            std::size_t length { 0 };
            for (const char32_t character : decode_utf8(text))
                length += character > 0xFFFF ? 2 : 1;
            return length;
        }

        application_config config_;
        application_environment environment_;
        std::shared_ptr<wake_state> wake_ { std::make_shared<wake_state>() };
        std::unique_ptr<app_host> host_ {};
        std::unique_ptr<renderer_host> renderer_ {};
        NSWindow* window_ { nil };
        NSView* container_ { nil };
        LuilView* view_ { nil };
        std::unique_ptr<webview_host> webviews_ {};
        std::vector<pixel_rect> webview_holes_ {};
        ui_element_id last_webview_focus_ {};
        bool pointer_over_webview_ { false };
        LuilWindowDelegate* window_delegate_ { nil };
        LuilApplicationDelegate* application_delegate_ { nil };
        NSTimer* timer_ { nil };
        std::optional<std::chrono::steady_clock::time_point> timer_deadline_ {};
        std::vector<overlay_entry> overlays_ {};
        std::shared_ptr<const ui_frame> overlay_frame_ {};
        std::shared_ptr<const ui_frame> close_requested_frame_ {};
        file_dialog_gate file_dialog_gate_ {};
        focus_request_tracker focus_tracker_ {};
        overlay_input_router router_ {};
        registry_font_resolver font_resolver_ {};
        system_appearance system_ {};
        system_appearance_observer appearance_observer_ {};
        std::optional<composition_state> composition_ {};
        // `interpretKeyEvents:`가 도는 동안의 키다. 입력기가 먹었는지(`key_handled_`)를 함께 본다.
        std::optional<key_input> current_key_ {};
        bool key_handled_ { false };
        bool warned_windows_ { false };
        bool warned_docked_ { false };
        bool cursor_hidden_ { false };
        bool dirty_ { true };
        bool stopped_ { false };
        bool quitting_ { false };
        bool exiting_ { false };
        int surface_width_ { 1 };
        int surface_height_ { 1 };
        float scale_ { 1.0f };
        std::optional<std::chrono::steady_clock::time_point> update_deadline_ {};
        float reported_width_ { -1.0f };
        float reported_height_ { -1.0f };
        float reported_scale_ { -1.0f };
        bool reported_reduce_motion_ { false };
    };

    int run_application(const application_config& config, const application_environment& environment)
    {
        @autoreleasepool
        {
            if ([NSThread isMainThread] == NO)
            {
                log_error(u8"run_application must be called on the main thread.");
                return 1;
            }
            application app { config, environment };
            return app.run();
        }
    }

    std::vector<std::u8string> command_line_arguments(const int argc, const char* const* const argv)
    {
        std::vector<std::u8string> arguments {};
        for (int index { 0 }; index < argc; ++index)
            if (argv[index] != nullptr)
                arguments.push_back(widen(argv[index]));
        return arguments;
    }
}

namespace {
    [[nodiscard]] std::u8string utf8_of(id const value)
    {
        NSString* const text { [value isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)value string] : (NSString*)value };
        return luil::macos::from_ns_string(text);
    }

    // NSString의 UTF-16 자리를 그 글의 UTF-8 byte 자리로 옮긴다.
    [[nodiscard]] std::size_t utf8_offset(NSString* const text, const NSUInteger utf16)
    {
        const NSUInteger clamped { std::min<NSUInteger>(utf16, text.length) };
        NSString* const prefix { [text substringToIndex:clamped] };
        return std::strlen([prefix UTF8String]);
    }
}

@implementation LuilContainerView

- (BOOL)isFlipped
{
    return YES;
}

@end

@implementation LuilView {
    BOOL secondaryPress;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    static_cast<void>(event);
    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

// 아래층 웹뷰의 구멍 자리는 이 view를 지나 그 아래의 웹뷰로 간다.
- (NSView*)hitTest:(NSPoint)point
{
    if (self.owner != nullptr && self.superview != nil && self.owner->passes_through([self convertPoint:point fromView:self.superview]))
        return nil;
    return [super hitTest:point];
}

- (void)updateLayer
{
}

- (void)mouseMoved:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->mouse_moved(event);
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->mouse_dragged(event);
}

- (void)rightMouseDragged:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->mouse_dragged(event);
}

- (void)mouseDown:(NSEvent*)event
{
    if (self.owner == nullptr)
        return;
    // 웹뷰가 쥐고 있던 키보드 초점을 되찾는다.
    if (self.window.firstResponder != self)
        [self.window makeFirstResponder:self];
    // Control+클릭은 macOS의 오른쪽 클릭이다.
    const bool secondary { ([event modifierFlags] & NSEventModifierFlagControl) != 0 };
    self.owner->mouse_button(event, secondary ? luil::pointer_button::right : luil::pointer_button::left, true);
    secondaryPress = secondary;
}

- (void)mouseUp:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->mouse_button(event, secondaryPress ? luil::pointer_button::right : luil::pointer_button::left, false);
    secondaryPress = NO;
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (self.window.firstResponder != self)
        [self.window makeFirstResponder:self];
    if (self.owner != nullptr)
        self.owner->mouse_button(event, luil::pointer_button::right, true);
}

- (void)rightMouseUp:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->mouse_button(event, luil::pointer_button::right, false);
}

- (void)otherMouseDown:(NSEvent*)event
{
    if (self.owner != nullptr && [event buttonNumber] == 2)
        self.owner->mouse_button(event, luil::pointer_button::middle, true);
}

- (void)otherMouseUp:(NSEvent*)event
{
    if (self.owner != nullptr && [event buttonNumber] == 2)
        self.owner->mouse_button(event, luil::pointer_button::middle, false);
}

- (void)mouseExited:(NSEvent*)event
{
    static_cast<void>(event);
    if (self.owner != nullptr)
        self.owner->mouse_exited();
}

- (void)scrollWheel:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->scroll_wheel(event);
}

- (void)keyDown:(NSEvent*)event
{
    if (self.owner != nullptr)
        self.owner->key_down(event, self);
}

// Command 조합은 메뉴보다 먼저 view가 받는다. 앱 메뉴의 Command+Q만 메뉴에 남긴다.
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (self.owner == nullptr || [self.window firstResponder] != self)
        return NO;
    const NSEventModifierFlags flags { [event modifierFlags] };
    NSString* const base { [event charactersIgnoringModifiers] };
    if ((flags & NSEventModifierFlagCommand) != 0 && [base isEqualToString:@"q"])
        return NO;
    if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) == 0)
        return NO;
    self.owner->key_down(event, self);
    return YES;
}

- (void)doCommandBySelector:(SEL)selector
{
    static_cast<void>(selector);
    if (self.owner != nullptr)
        self.owner->do_command();
}

- (void)luilSnapshot:(NSString*)path
{
    if (self.owner != nullptr)
        self.owner->snapshot(path);
}

- (void)luilUndo:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_z, false);
}

- (void)luilRedo:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_z, true);
}

- (void)luilCut:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_x, false);
}

- (void)luilCopy:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_c, false);
}

- (void)luilPaste:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_v, false);
}

- (void)luilSelectAll:(id)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->menu_edit(luil::key_code::key_a, false);
}

// --- NSTextInputClient ---

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    static_cast<void>(replacementRange);
    if (self.owner != nullptr)
        self.owner->insert_text(utf8_of(string));
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    static_cast<void>(replacementRange);
    if (self.owner == nullptr)
        return;
    NSString* const text { [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : (NSString*)string };
    self.owner->set_marked_text(luil::macos::from_ns_string(text), utf8_offset(text, selectedRange.location + selectedRange.length));
}

- (void)unmarkText
{
    if (self.owner != nullptr)
        self.owner->commit_composition();
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    if (self.owner == nullptr || self.owner->has_marked_text() == false)
        return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(self.owner->marked_begin_utf16(), self.owner->marked_length_utf16());
}

- (BOOL)hasMarkedText
{
    return self.owner != nullptr && self.owner->has_marked_text();
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    static_cast<void>(range);
    static_cast<void>(actualRange);
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    static_cast<void>(range);
    static_cast<void>(actualRange);
    return self.owner != nullptr ? self.owner->candidate_rect() : NSZeroRect;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    static_cast<void>(point);
    return NSNotFound;
}

@end

@implementation LuilWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->request_close(false);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_resized();
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_resized();
}

- (void)windowDidMove:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_moved();
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_key_window(true);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_key_window(false);
}

@end

@implementation LuilApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    static_cast<void>(notification);
    [NSApp addObserver:self forKeyPath:@"effectiveAppearance" options:0 context:nullptr];
    NSNotificationCenter* const workspace { [[NSWorkspace sharedWorkspace] notificationCenter] };
    [workspace addObserver:self selector:@selector(systemSettingsChanged:) name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(systemSettingsChanged:) name:NSSystemColorsDidChangeNotification object:nil];
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    static_cast<void>(notification);
    [NSApp removeObserver:self forKeyPath:@"effectiveAppearance"];
    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context
{
    static_cast<void>(keyPath);
    static_cast<void>(object);
    static_cast<void>(change);
    static_cast<void>(context);
    if (self.owner != nullptr)
        self.owner->on_appearance_changed();
}

- (void)systemSettingsChanged:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_appearance_changed();
}

// Command+Q와 Dock의 종료는 창의 닫기와 같은 길이다. 끝은 앱이 정한다.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->request_close(false);
    return NSTerminateCancel;
}

@end

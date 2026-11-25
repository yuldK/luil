#include "luil/macos/macos_app.h"

#include "host/file_dialog_gate.h"
#include "host/focus_request_tracker.h"
#include "host/font_registry.h"
#include "host/docked_placement.h"
#include "host/frame_state.h"
#include "host/notification_queue.h"
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
#include "luil/ui/webview_element.h"
#include "luil/win32/win32_window.h"
#include "macos/accessibility_macos.h"
#include "macos/cpu_skia_renderer.h"
#include "macos/dock_macos.h"
#include "macos/file_dialog_macos.h"
#include "macos/frame_capture.h"
#include "macos/hotkey_macos.h"
#include "macos/input_script.h"
#include "macos/macos_clipboard.h"
#include "macos/macos_input.h"
#include "macos/macos_text.h"
#include "macos/metal_skia_renderer.h"
#include "macos/notification_center.h"
#include "macos/single_instance_receiver.h"
#include "macos/surface_capture.h"
#include "macos/tray_icon_macos.h"
#include "macos/webview_host.h"
#include "macos/window_rules.h"
#include "luil/text/fonts.h"
#include "luil/ui/caption_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace luil::macos {
    class application;
    struct native_surface;
}

// 창 내용 영역의 view다. 이벤트를 C++ 앱으로 넘기고, 입력기(NSTextInputClient)의 창구가 된다.
@interface LuilView : NSView <NSTextInputClient>
@property(nonatomic, assign) luil::macos::application* owner;
@property(nonatomic, assign) luil::macos::native_surface* surface;
@end

// 도킹 표면과 테두리 없는 소유 보조 창의 판이다. 테두리가 없어도 활성화를 청하면(`focus_request::activate`) 키보드를 받는다.
@interface LuilDockPanel : NSPanel
@end

// 주 창과 독립 창이다. 테두리 없는 창(`application_config::borderless`, 앱이 스스로 바꾼 `styleMask`)도 키·주 창이 된다.
@interface LuilWindow : NSWindow
@end

// popup 하나의 판이다 (`ui_frame::popups`). 키 창·주 창이 되지 않고 눌러도 앱을 활성화하지 않는다 (Win32 `WS_EX_NOACTIVATE`).
@interface LuilPopupPanel : NSPanel
@end

// 시스템 제목 막대에 세운 caption 앱 단추의 누름을 C++ 앱으로 넘긴다 (`caption_config::app_buttons`).
@interface LuilTitlebarTarget : NSObject
@property(nonatomic, assign) luil::macos::application* owner;
@property(nonatomic, assign) luil::macos::native_surface* surface;
- (void)press:(NSButton*)sender;
@end

// 창 내용의 그릇이다. Skia view와 웹뷰가 형제로 선다. 자식의 자리를 왼쪽 위 원점(물리 픽셀을 배율로 나눈 점)으로 적도록 뒤집는다.
@interface LuilContainerView : NSView
@end

// 창의 닫기·크기·배율·초점 알림을 C++ 앱으로 넘긴다.
@interface LuilWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) luil::macos::application* owner;
@property(nonatomic, assign) luil::macos::native_surface* surface;
@end

// 앱 종료 요청(Command+Q, Dock의 종료)·앱 활성화·시스템 모양 변화를 C++ 앱으로 넘긴다.
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
            // macOS에는 공개된 기다림 커서가 없다 (시스템이 바쁜 앱에 회전 커서를 스스로 보인다).
            case ui_cursor::wait:
                return [NSCursor arrowCursor];
            default:
                return [NSCursor arrowCursor];
            }
        }

        // 화면 좌표의 약속이다 (`screen_point`, `window_placement`, `window_screen_anchor`).
        //  - 원점은 주 화면(메뉴 막대가 있는 화면)의 왼쪽 위이고 아래로 늘어난다 (Win32의 가상 화면처럼).
        //  - 단위는 점에 **주 화면의 배율**을 곱한 픽셀이다. 배율이 다른 화면이 섞여도 한 값으로 자리를 되돌리기 위해서다.
        [[nodiscard]] CGFloat primary_scale()
        {
            NSArray<NSScreen*>* const screens { [NSScreen screens] };
            return screens.count > 0 && screens[0].backingScaleFactor > 0 ? screens[0].backingScaleFactor : 1.0;
        }

        [[nodiscard]] CGFloat primary_height()
        {
            NSArray<NSScreen*>* const screens { [NSScreen screens] };
            return screens.count > 0 ? screens[0].frame.size.height : 0.0;
        }

        // AppKit의 화면 점(왼쪽 아래 원점) → luil의 화면 픽셀(왼쪽 위 원점).
        [[nodiscard]] screen_point to_screen_pixels(const NSPoint point)
        {
            const CGFloat scale { primary_scale() };
            return { static_cast<int>(std::lround(point.x * scale)), static_cast<int>(std::lround((primary_height() - point.y) * scale)) };
        }

        // luil의 화면 픽셀(왼쪽 위 원점) → AppKit의 화면 점(왼쪽 아래 원점).
        [[nodiscard]] NSPoint from_screen_pixels(const int x, const int y)
        {
            const CGFloat scale { primary_scale() };
            return NSMakePoint(x / scale, primary_height() - y / scale);
        }

        // 이벤트에 실린 표면 표식을 채운다. 비어 있으면 그 표면의 것이다 — router가 popup 밖의 사건을 빈 표식(주 창)으로 두므로
        // 보조 창에서는 그 창의 id로 바꾼다. popup의 사건은 이미 popup id를 갖는다.
        void label_surface(raw_input_event& event, const std::u8string& surface)
        {
            if (surface.empty())
                return;
            std::visit(
                [&surface](auto& value) {
                    if constexpr (requires { value.surface; })
                        if (value.surface.empty())
                            value.surface = surface;
                },
                event);
        }
    }

    // popup 판 하나의 상태다 (`ui_frame::popups`). 자리는 앵커 기준이고, 판이 선 화면 자리는 host가 정한다.
    struct popup_state
    {
        std::u8string anchor {};
        // 앵커 표면 내용 기준의 자리와 크기다 (논리 픽셀 = 점).
        float x { 0.0f };
        float y { 0.0f };
        float width { 0.0f };
        float height { 0.0f };
        // 내용이 판보다 길면 흘린 양과 그 끝이다 (물리 픽셀).
        float scroll_y { 0.0f };
        float max_scroll_y { 0.0f };
        popup_shape shape {};
        bool transparent { false };
        window_backdrop backdrop { window_backdrop::none };
        std::function<input_action(popup_dismiss_reason)> dismiss {};
        bool requested { false };
        std::function<app_message(const rect_f&)> placed {};
        std::optional<rect_f> reported_bounds {};
        // 둥근 판의 테두리에 마지막으로 칠한 색이다.
        std::optional<ui_color> border_color {};
    };

    // 창 하나다. 주 창(`id`가 빈 것)과 보조 창(`ui_window::id`), 도킹 표면, popup 판이 같은 모양이다.
    //  - 자기 view·렌더러·크기·배율, 입력 router, 다시 그릴 표식과 다음 그림 예고를 갖는다.
    //  - 보조 창은 frame이 실은 `ui_window`의 사본(`want`)과 그 tree를 갖는다. 주 창의 tree는 host가 준다. popup 판은 `want.tree`에
    //    그 tree를 든다.
    struct native_surface
    {
        std::u8string id {};
        ui_window want {};
        NSWindow* window { nil };
        LuilContainerView* container { nil };
        LuilView* view { nil };
        LuilWindowDelegate* delegate { nil };
        std::unique_ptr<renderer_host> renderer {};
        std::optional<renderer_backend> reported_backend {};
        int width { 1 };
        int height { 1 };
        float scale { 1.0f };
        // popup 판이면 그 상태다. 판의 입력 router는 판 하나를 흘리는 자리로만 쓴다.
        std::optional<popup_state> popup {};
        overlay_input_router router {};
        // 앱이 caption을 그리는 창이다 (`application_config::borderless`). caption 띠의 누름은 host가 먼저 본다.
        bool custom_caption { false };
        // caption 끌기 중이다. 끌기를 시작한 창 사각형과 그때의 포인터다 (화면 픽셀).
        bool dragging { false };
        screen_rect drag_start {};
        screen_point drag_pressed {};
        // 눌린 caption 창 단추다. 같은 단추 위에서 떼면 실행한다 (Win32 `caption_click_executes`).
        caption_hit pressed_caption { caption_hit::client };
        // 제목 막대가 없는 창의 확대다. 시스템 확대(`isZoomed`)는 제목 막대가 없는 창에서 거짓 참을 내므로 host가 직접 든다.
        bool zoomed { false };
        std::optional<NSRect> restore_frame {};
        // 시스템 제목 막대에 세운 caption 앱 단추다 (`caption_config::app_buttons`). 마지막으로 세운 단추들의 모양을 견준다.
        NSTitlebarAccessoryViewController* titlebar { nil };
        LuilTitlebarTarget* titlebar_target { nil };
        std::vector<std::tuple<std::u8string, char32_t, std::u8string, bool>> titlebar_buttons {};
        bool dirty { true };
        std::optional<std::chrono::steady_clock::time_point> update_deadline {};
        float reported_width { -1.0f };
        float reported_height { -1.0f };
        float reported_scale { -1.0f };
        bool reported_reduce_motion { false };
        std::optional<window_placement> reported_placement {};
        bool pointer_over_webview { false };
        bool cursor_hidden { false };
        // 비치는 창의 그림자를 다시 잴 때다 (크기·모양이 바뀌었다).
        bool shadow_stale { true };
        // 배경 효과 view(창 내용 맨 아래)와 마지막으로 알린 상태다. 효과가 섰으면 바탕을 `backdrop_background`로 칠한다.
        NSVisualEffectView* effect { nil };
        bool backdrop_applied { false };
        std::optional<std::pair<window_backdrop, int>> reported_backdrop {};
        // 도킹 표면이다 (`ui_frame::docked_surfaces`). `want`에는 그 tree와 metrics만 옮겨 든다. 자리가 풀리지 않으면 숨는다.
        bool docked { false };
        std::optional<screen_rect> docked_bounds {};
        // 이 창에 선 웹뷰들과, 아래층이 비울 구멍, 마지막으로 본 초점이다 (`ui_webview::anchor`가 이 창).
        std::unique_ptr<webview_host> webviews {};
        std::vector<pixel_rect> webview_holes {};
        ui_element_id last_webview_focus {};
        // VoiceOver의 창구다 (NSAccessibility). view보다 먼저 끊는다.
        std::unique_ptr<accessibility_bridge> accessibility {};
        // 이 표면의 녹화다 (`record_request`).
        frame_recorder recorder {};
        // 닫힘 요청을 낸 frame이다. 같은 frame에 거듭 온 요청은 삼킨다.
        std::shared_ptr<const ui_frame> close_requested_frame {};

        [[nodiscard]] bool main() const noexcept
        {
            return id.empty();
        }
    };

    // AppKit 앱 하나의 수명이다. 메인 thread에서만 산다 — 그 thread가 luil의 UI thread다.
    //  - 입력기가 묻는 창구(`text_input_host`)도 맡는다. 초점을 가진 텍스트 칸을 tree에서 찾는다.
    class application final : public text_input_host
    {
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
            if (clipboard_timer_ != nil)
                [clipboard_timer_ invalidate];
            for (auto popup { popups_.rbegin() }; popup != popups_.rend(); ++popup)
                destroy_popup(**popup);
            popups_.clear();
            for (std::unique_ptr<native_surface>& surface : secondary_)
                destroy_window(*surface);
            secondary_.clear();
            if (main_.accessibility != nullptr)
                main_.accessibility->detach();
            main_.accessibility.reset();
            main_.renderer.reset();
            set_font_fallback(nullptr);
            set_system_accent(std::nullopt);
        }

        // --- text_input_host ---

        [[nodiscard]] std::optional<text_input_target> focused_text_target() const override
        {
            if (host_ == nullptr || environment_.policy == nullptr)
                return std::nullopt;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            // 초점이 선 곳이 이 앱이 그리는 표면(창이나 그 위의 popup)이어야 한다.
            if (interaction.focused_surface.empty() == false && find_surface(interaction.focused_surface) == nullptr && find_popup(interaction.focused_surface) == nullptr)
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
                const bool drawn { host_->acquire_ui_tree() != nullptr && render(main_) };
                stop();
                return drawn ? 0 : 1;
            }
            [main_.window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            // 저장해 둔 전체 화면은 창이 보인 뒤에야 들어갈 수 있다.
            if (initial_fullscreen_)
                set_fullscreen(true);
            pump();
            // 처음 자리를 정하는 동안(가운데 놓기, 저장 배치, 앱이 `on_native_window_created`에서 고친 창)의 이동은 알리지 않았다.
            // 창이 보인 지금의 실제 자리를 한 번 알린다. 앱이 저장해 둘 첫 배치다.
            placement_ready_ = true;
            report_placement(main_);
            luil_run_input_script(main_.window, main_.view);
            [NSApp run];
            stop();
            return 0;
        }

        // 다른 thread가 깨웠다.
        void on_wake()
        {
            execute_host_commands();
            // 앞서 inbox가 차서 내지 못한 알림 사건을 다시 낸다.
            deliver_notification_events();
            mark_all_dirty();
            pump();
        }

        // 주 창의 닫기 단추·Command+Q다. 앱이 닫힘을 받아 가면(`ui_frame::close`) 그 동작만 낸다.
        void request_close(const bool from_app)
        {
            if (exiting_ == false && host_ != nullptr && host_->faulted() == false)
            {
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame != nullptr && frame->close != nullptr)
                {
                    if (main_.close_requested_frame == frame && from_app == false)
                        return;
                    main_.close_requested_frame = frame;
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

        // 보조 창의 닫기 단추다. 창을 없애는 것은 앱이 목록에서 빼는 것이다 (`ui_window::close`). 게시된 frame당 한 번만 낸다.
        void request_window_close(native_surface& surface)
        {
            if (surface.main())
            {
                request_close(false);
                return;
            }
            if (surface.want.close == nullptr || host_ == nullptr)
                return;
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            if (surface.close_requested_frame == frame)
                return;
            surface.close_requested_frame = frame;
            try
            {
                dispatch(surface.want.close());
            }
            catch (...)
            {
                log_error(u8"A window close action threw an exception.");
            }
            pump();
        }

        // 스레드를 모두 정리하고 run loop를 끝낸다. 창은 그 뒤에 닫는다.
        void quit()
        {
            if (quitting_)
                return;
            quitting_ = true;
            stop();
            for (std::unique_ptr<native_surface>& surface : secondary_)
                [surface->window orderOut:nil];
            [main_.window orderOut:nil];
            [NSApp stop:nil];
            // `stop:`은 이벤트 하나를 처리한 뒤에야 run을 끝낸다. 빈 이벤트를 넣어 곧바로 끝나게 한다.
            NSEvent* const event { [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
                                                  windowNumber:0 context:nil subtype:0 data1:0 data2:0] };
            [NSApp postEvent:event atStart:YES];
        }

        // --- 창 알림 ---

        void on_resized(native_surface& surface)
        {
            refresh_metrics(surface);
            report_placement(surface);
            pump();
        }

        void on_key_window(native_surface& surface, const bool key)
        {
            if (host_ == nullptr)
                return;
            if (key)
                host_->post_raw_input(surface_focus_gained_event { surface.id });
            else
            {
                commit_composition();
                static_cast<void>(dismiss_popups(popup_dismiss_reason::activation_changed));
                host_->post_raw_input(surface_focus_lost_event { surface.id });
            }
            surface.dirty = true;
            pump();
        }

        // 앱이 앞에 섰거나 다른 앱에 자리를 내줬다. 창 사이를 오가는 것은 수명 주기가 아니다.
        void on_active(const bool active)
        {
            post_lifecycle(active ? app_lifecycle::foreground : app_lifecycle::background);
            watch_clipboard(active);
            // 걸지 못한 전역 단축키는 앱이 앞으로 올 때 한 번 다시 건다 (Win32의 주 창 활성화와 같은 자리).
            if (active && hotkeys_ != nullptr && host_ != nullptr)
            {
                hotkeys_->reactivated();
                apply_hotkeys(host_->acquire_frame().get());
            }
            pump();
        }

        // 앞에 있는 동안 클립보드 번호를 본다 (`check_clipboard`).
        void watch_clipboard(const bool active)
        {
            if (clipboard_timer_ != nil)
            {
                [clipboard_timer_ invalidate];
                clipboard_timer_ = nil;
            }
            if (active == false || stopped_)
                return;
            check_clipboard();
            const std::shared_ptr<wake_state> state { wake_ };
            clipboard_timer_ = [NSTimer timerWithTimeInterval:0.5
                                                      repeats:YES
                                                        block:^(NSTimer*) {
                                                          if (state->target != nullptr)
                                                              state->target->check_clipboard();
                                                        }];
            [[NSRunLoop mainRunLoop] addTimer:clipboard_timer_ forMode:NSRunLoopCommonModes];
        }

        void on_occlusion_changed(native_surface& surface)
        {
            surface.dirty = true;
            pump();
        }

        // 창 하나가 움직였다. 어느 창이든 popup 밖의 상호작용이라 닫힘 계기는 전부에게 가고, 남는 popup은 앵커를 따라 다시 선다
        // (판은 앵커 창의 자식 창이라 함께 움직였다. 옮긴 자리를 화면 안으로 다시 다듬는다).
        //  - caption 끌기 안에서는 배치를 알리지 않는다. 끝에 한 번 알린다 (Windows와 같다).
        void on_moved(native_surface& surface)
        {
            static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_moved));
            place_popups();
            if (surface.dragging == false)
                report_placement(surface);
            pump();
        }

        void on_appearance_changed()
        {
            refresh_appearance();
            post_appearance();
            post_metrics(main_);
            if (host_ != nullptr)
            {
                apply_backdrops(host_->acquire_frame().get());
                if (tray_ != nullptr)
                {
                    const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                    tray_->update(frame != nullptr ? frame->tray : std::nullopt, system_.high_contrast);
                }
            }
            mark_all_dirty();
            pump();
        }

        // --- 입력 ---

        void mouse_moved(native_surface& surface, NSEvent* const event)
        {
            if (surface.dragging)
            {
                drag_window(surface, event);
                return;
            }
            const NSPoint point { surface_point(surface, event) };
            // 웹뷰 위다. 웹뷰가 포인터를 받으므로 tree의 hover를 거두고 커서는 웹뷰에 맡긴다.
            if (surface.webviews != nullptr && surface.webviews->covers(static_cast<float>(point.x), static_cast<float>(point.y)))
            {
                if (surface.pointer_over_webview == false)
                {
                    surface.pointer_over_webview = true;
                    post_events(surface, { raw_input_event { pointer_left_event { {}, pointer_device::mouse } } });
                }
                return;
            }
            surface.pointer_over_webview = false;
            pointer_moved_event moved {};
            moved.x = static_cast<float>(point.x);
            moved.y = static_cast<float>(point.y);
            moved.time = now();
            moved.device = pointer_device::mouse;
            moved.in_contact = false;
            moved.scale = surface.scale;
            moved.screen = to_screen_pixels([NSEvent mouseLocation]);
            apply_modifiers(event, moved.control, moved.shift, moved.alt);
            post_events(surface, { raw_input_event { std::move(moved) } });
            update_cursor(surface, point);
        }

        void mouse_button(native_surface& surface, NSEvent* const event, const pointer_button button, const bool pressed)
        {
            const NSPoint point { surface_point(surface, event) };
            if (button == pointer_button::left && caption_mouse(surface, event, point, pressed))
                return;
            if (pressed)
            {
                pointer_pressed_event value {};
                value.x = static_cast<float>(point.x);
                value.y = static_cast<float>(point.y);
                value.button = button;
                value.time = now();
                value.device = pointer_device::mouse;
                value.scale = surface.scale;
                apply_modifiers(event, value.control, value.shift, value.alt);
                post_events(surface, { raw_input_event { std::move(value) } });
                return;
            }
            pointer_released_event value {};
            value.x = static_cast<float>(point.x);
            value.y = static_cast<float>(point.y);
            value.button = button;
            value.time = now();
            value.device = pointer_device::mouse;
            value.screen = to_screen_pixels([NSEvent mouseLocation]);
            apply_modifiers(event, value.control, value.shift, value.alt);
            post_events(surface, { raw_input_event { std::move(value) } });
        }

        void mouse_exited(native_surface& surface)
        {
            post_events(surface, { raw_input_event { pointer_left_event { {}, pointer_device::mouse } } });
            [[NSCursor arrowCursor] set];
        }

        // --- 앱이 그리는 caption (`application_config::borderless`, Win32 비클라이언트 판정의 자리) ---

        // caption 띠의 왼쪽 단추 누름·뗌을 tree보다 먼저 본다. host가 맡았으면 참이고, 그 누름과 뗌은 tree에 가지 않는다.
        //  - 창 단추는 누른 단추 위에서 떼면 그 창의 최소화·확대·닫기다 (Win32 `caption_click_executes`).
        //  - 끌기 자리는 창 끌기다. 두 번 누르면 시스템 설정(제목 막대를 두 번 클릭할 때)의 동작이다.
        //  - 띠를 누르는 것도 popup 밖 누름이다 (Win32 `WM_NCLBUTTONDOWN`).
        //  - 포인터 이동은 그대로 tree에 가므로 창 단추의 hover와 tooltip은 tree가 그린다.
        bool caption_mouse(native_surface& surface, NSEvent* const event, const NSPoint point, const bool pressed)
        {
            if (surface.custom_caption == false || host_ == nullptr || surface.window == nil)
                return false;
            if (pressed == false)
            {
                if (surface.dragging)
                {
                    surface.dragging = false;
                    report_placement(surface);
                    pump();
                    return true;
                }
                if (surface.pressed_caption == caption_hit::client)
                    return false;
                const caption_hit pressed_hit { std::exchange(surface.pressed_caption, caption_hit::client) };
                if (caption_hit_at(surface, point) == pressed_hit)
                    execute_caption_button(surface, pressed_hit);
                pump();
                return true;
            }
            const caption_hit hit { caption_hit_at(surface, point) };
            if (hit == caption_hit::client)
                return false;
            static_cast<void>(dismiss_popups(popup_dismiss_reason::pointer_press_outside));
            commit_composition();
            if (hit != caption_hit::drag)
            {
                surface.pressed_caption = hit;
                pump();
                return true;
            }
            if ([event clickCount] >= 2)
            {
                switch (title_double_click_action())
                {
                case title_double_click::zoom:
                    toggle_zoom(surface);
                    break;
                case title_double_click::minimize:
                    [surface.window miniaturize:nil];
                    break;
                case title_double_click::none:
                    break;
                }
                pump();
                return true;
            }
            surface.dragging = true;
            surface.drag_start = window_bounds(surface.window.frame);
            surface.drag_pressed = event_screen_pixels(event);
            return true;
        }

        // caption 끌기 한 걸음이다. 주 창이면 앱이 갈 자리를 고친다 (`win32::window_delegate::on_window_moving`).
        void drag_window(native_surface& surface, NSEvent* const event)
        {
            const screen_point pointer { event_screen_pixels(event) };
            // 확대된 창을 끌어 옮기기 시작하면 돌아갈 크기로 돌아와 포인터 밑에 선다. 잡은 자리의 가로 비율을 지킨다 (Windows와 같다).
            //  - 누르기만 하고 움직이지 않은 것(두 번 누르기의 첫 누름)은 끌기가 아니다. 시스템 끌기 문턱만큼 움직인 뒤에 돌린다.
            if (surface.zoomed && surface.restore_frame.has_value() && has_title_bar(surface.window) == false)
            {
                const CGFloat threshold { 3.0 * primary_scale() };
                if (std::abs(pointer.x - surface.drag_pressed.x) < threshold && std::abs(pointer.y - surface.drag_pressed.y) < threshold)
                    return;
                const screen_rect zoomed { surface.drag_start };
                const NSRect restore { *surface.restore_frame };
                const double ratio { zoomed.width() > 0 ? static_cast<double>(surface.drag_pressed.x - zoomed.left) / zoomed.width() : 0.5 };
                const CGFloat scale { primary_scale() };
                const NSPoint top_left { from_screen_pixels(surface.drag_pressed.x - static_cast<int>(std::lround(ratio * restore.size.width * scale)), zoomed.top) };
                surface.zoomed = false;
                [surface.window setFrame:NSMakeRect(top_left.x, top_left.y - restore.size.height, restore.size.width, restore.size.height) display:YES];
                surface.drag_start = window_bounds(surface.window.frame);
            }
            screen_rect bounds { dragged_window_bounds(surface.drag_start, surface.drag_pressed, pointer) };
            if (surface.main() && window_delegate_ != nullptr && host_ != nullptr)
            {
                screen_rect adjusted { bounds };
                try
                {
                    window_delegate_->on_window_moving(*host_, adjusted);
                }
                catch (...)
                {
                    log_error(u8"The window moving delegate threw an exception.");
                }
                bounds = moving_bounds_kept_size(bounds, adjusted);
            }
            const NSPoint top_left { from_screen_pixels(bounds.left, bounds.top) };
            [surface.window setFrameOrigin:NSMakePoint(top_left.x, top_left.y - surface.window.frame.size.height)];
        }

        [[nodiscard]] caption_hit caption_hit_at(const native_surface& surface, const NSPoint point) const
        {
            return custom_caption_hit(surface_tree(surface), static_cast<float>(point.x), static_cast<float>(point.y), surface.width, surface.scale);
        }

        void execute_caption_button(native_surface& surface, const caption_hit hit)
        {
            switch (hit)
            {
            case caption_hit::minimize:
                [surface.window miniaturize:nil];
                break;
            case caption_hit::maximize:
                toggle_zoom(surface);
                break;
            case caption_hit::close:
                request_window_close(surface);
                break;
            default:
                break;
            }
        }

        // 사용자 기본값의 "제목 막대를 두 번 클릭할 때"다. 옛 설정(`AppleMiniaturizeOnDoubleClick`)만 있는 계정은 그것을 따른다.
        [[nodiscard]] static title_double_click title_double_click_action()
        {
            NSUserDefaults* const defaults { [NSUserDefaults standardUserDefaults] };
            if (NSString* const action { [defaults stringForKey:@"AppleActionOnDoubleClick"] }; action != nil)
                return parse_title_double_click(from_ns_string(action));
            return [defaults boolForKey:@"AppleMiniaturizeOnDoubleClick"] ? title_double_click::minimize : title_double_click::zoom;
        }

        // 포인터 사건이 난 화면 자리다 (화면 픽셀). 끌기가 창을 옮기는 동안에도 창 좌표가 아니라 화면에서 읽는다.
        [[nodiscard]] static screen_point event_screen_pixels(NSEvent* const event)
        {
            if (CGEventRef const value { event.CGEvent }; value != nullptr)
            {
                // CoreGraphics의 전역 좌표는 주 화면 왼쪽 위 원점의 점이다 (이 파일의 화면 좌표 약속과 원점이 같다).
                const CGPoint location { CGEventGetLocation(value) };
                const CGFloat scale { primary_scale() };
                return { static_cast<int>(std::lround(location.x * scale)), static_cast<int>(std::lround(location.y * scale)) };
            }
            return to_screen_pixels([NSEvent mouseLocation]);
        }

        // 창 바깥 사각형이다 (화면 픽셀).
        [[nodiscard]] static screen_rect window_bounds(const NSRect frame)
        {
            const screen_point top_left { to_screen_pixels(NSMakePoint(NSMinX(frame), NSMaxY(frame))) };
            const screen_point bottom_right { to_screen_pixels(NSMakePoint(NSMaxX(frame), NSMinY(frame))) };
            return { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
        }

        [[nodiscard]] static bool has_title_bar(NSWindow* const window)
        {
            return window != nil && (window.styleMask & NSWindowStyleMaskTitled) != 0;
        }

        // 창이 확대돼 있는가다. 제목 막대가 없는 창은 host가 든 값이다 — `isZoomed`는 제목 막대가 없는 창(앱이 `styleMask`를 바꾼 창,
        // 높이가 작은 창 포함)에서 참을 돌려준다.
        [[nodiscard]] static bool is_zoomed(const native_surface& surface)
        {
            if (surface.window == nil)
                return false;
            return has_title_bar(surface.window) ? surface.window.zoomed == YES : surface.zoomed;
        }

        void toggle_zoom(native_surface& surface)
        {
            set_zoomed(surface, is_zoomed(surface) == false);
        }

        // 창을 확대하거나 돌린다. 제목 막대가 있으면 시스템 확대(`zoom:`)이고, 없으면 그 화면의 보이는 영역을 채우고 돌아갈 자리를 든다.
        void set_zoomed(native_surface& surface, const bool zoom)
        {
            NSWindow* const window { surface.window };
            if (window == nil || is_zoomed(surface) == zoom)
                return;
            if (has_title_bar(window))
            {
                [window zoom:nil];
                return;
            }
            if (zoom)
            {
                if (surface.main())
                    normal_placement_ = placement_of(surface);
                surface.restore_frame = window.frame;
                NSScreen* const screen { window.screen != nil ? window.screen : screen_for(window.frame) };
                surface.zoomed = true;
                if (screen != nil)
                    [window setFrame:screen.visibleFrame display:YES];
            }
            else
            {
                surface.zoomed = false;
                if (surface.restore_frame.has_value())
                    [window setFrame:*surface.restore_frame display:YES];
            }
            surface.dirty = true;
            report_placement(surface);
        }

        void scroll_wheel(native_surface& surface, NSEvent* const event)
        {
            const NSPoint point { surface_point(surface, event) };
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
                post_events(surface, std::move(events));
        }

        // 키 하나다. 앞 갈래는 키 이벤트를, 뒤 갈래(입력기)는 글자를 낸다.
        void key_down(native_surface& surface, NSEvent* const event, LuilView* const view)
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
                if (auto key { key_event_for(input, key_capture) }; key.has_value())
                    post_events(surface, { raw_input_event { std::move(*key) } });
                pump();
                return;
            }
            // 그 밖의 키는 입력기가 먼저 본다. 입력기가 먹지 않은 키는 `do_command`로 돌아온다.
            current_key_ = input;
            key_surface_ = &surface;
            key_handled_ = false;
            [view interpretKeyEvents:@[ event ]];
            current_key_.reset();
            key_surface_ = nullptr;
            pump();
        }

        // 입력기가 넘긴 명령이다 (`doCommandBySelector:`). 이 키는 앱의 키다.
        void do_command()
        {
            if (current_key_.has_value() == false || key_handled_ || key_surface_ == nullptr)
                return;
            key_handled_ = true;
            const key_input input { *current_key_ };
            std::vector<raw_input_event> events {};
            if (auto key { key_event_for(input, false) }; key.has_value())
                events.push_back(raw_input_event { std::move(*key) });
            // Backspace는 누름이면서 글자다 (Win32의 WM_KEYDOWN 뒤 WM_CHAR U+0008).
            if (backspace_key(input.key_code))
                events.push_back(raw_input_event { character_typed_event { backspace_character(input), input.time } });
            post_events(*key_surface_, std::move(events));
        }

        // 입력기가 확정한 글이다 (`insertText:`).
        void insert_text(native_surface& surface, const std::u8string& text)
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
                    if (auto key { key_event_for(*current_key_, false) }; key.has_value())
                        events.push_back(raw_input_event { std::move(*key) });
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
                if (auto key { key_event_for(*current_key_, false) }; key.has_value())
                    events.push_back(raw_input_event { std::move(*key) });
            post_events(surface, std::move(events));
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

        // 후보 창이 설 자리다 (화면 좌표, 점). 초점 칸이 사는 창의 좌표로 옮긴다.
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
            const native_surface* const surface { location.surface };
            if (span.has_value() == false || surface == nullptr || surface->window == nil)
                return NSZeroRect;
            const float scale { surface->scale };
            const NSRect local { NSMakeRect(span->x / scale, span->y / scale, std::max(1.0f, span->width / scale), span->height / scale) };
            const NSRect in_window { [surface->view convertRect:local toView:nil] };
            return [surface->window convertRectToScreen:in_window];
        }

        void script_capture(const std::u8string& path, const std::u8string& surface)
        {
            dispatch(capture_request { path, surface });
        }

        // 빈 경로는 멈춤이다.
        void script_record(const std::u8string& path, const std::u8string& surface)
        {
            dispatch(record_request { path.empty() ? record_command::stop : record_command::start, path, surface });
        }

        void snapshot(NSString* const path)
        {
            snapshot_composite(main_, path);
        }

        // 창 내용의 합성을 PNG로 적는다 (개발용, 입력 대본의 `snapshot`). 아래층 웹뷰 → Skia frame → 위층 웹뷰의 차례다.
        void snapshot_composite(native_surface& surface, NSString* const path)
        {
            if (path == nil)
                return;
            // 바탕은 Skia가 그린 frame이다. Metal은 GPU에서 되읽어야 하므로 한 frame을 되읽기로 다시 그린다.
            if (surface.renderer != nullptr && host_ != nullptr)
            {
                request_frame_readback();
                surface.dirty = true;
                static_cast<void>(render(surface));
            }
            CGImageRef const frame { copy_latest_frame() };
            if (frame == nullptr)
                return;
            const int width { surface.width };
            const int height { surface.height };
            NSString* const target { [path copy] };
            const auto compose = [frame, width, height, target](std::vector<webview_host::snapshot_layer> layers) {
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
            };
            // 웹뷰가 없는 표면(popup 판)은 Skia frame만이다.
            if (surface.webviews != nullptr)
                surface.webviews->snapshot(compose);
            else
                compose({});
        }

        // 창 id의 Skia view다 (개발용, 입력 대본의 `window <id>`). 없으면 nil이다.
        [[nodiscard]] NSView* view_of(const std::u8string& id)
        {
            native_surface* surface { find_surface(id) };
            if (surface == nullptr)
                surface = find_popup(id);
            return surface != nullptr ? surface->view : nil;
        }

        // 보조 창이나 popup 판 하나의 Skia frame을 PNG로 적는다 (개발용, 입력 대본의 `snapshot <png> <창 id>`).
        void snapshot_surface(const std::u8string& id, NSString* const path)
        {
            native_surface* surface { find_surface(id) };
            if (surface == nullptr)
                surface = find_popup(id);
            if (surface != nullptr)
                snapshot_composite(*surface, path);
        }

        // view 좌표의 점(점 단위)이 아래층 웹뷰의 구멍 안인가. 참이면 포인터가 Skia view를 지나 웹뷰로 간다.
        [[nodiscard]] bool passes_through(const native_surface& surface, const NSPoint point) const
        {
            return surface.webviews != nullptr && surface.webviews->passes_through(static_cast<float>(point.x * surface.scale), static_cast<float>(point.y * surface.scale));
        }

        // 메뉴의 편집 명령이다 (오려 두기·복사하기·붙여넣기·전체 선택). Command 단축키와 같은 키로, 키 창에 보낸다.
        void menu_edit(const key_code key, const bool shift)
        {
            if (host_ == nullptr)
                return;
            native_surface& surface { key_surface() };
            key_pressed_event event { key, false, shift, false, false, now() };
            event.primary_shortcut = true;
            event.word_navigation = false;
            event.meta = true;
            post_events(surface, { raw_input_event { std::move(event) } });
            pump();
        }

        // --- OS 끌어 놓기 (os-dragdrop-design.md의 macOS 갈래) ---
        //  - 끄는 동안의 들어옴·움직임·떠남은 표시(수락 대상 강조)만 세운다. 답(받음·안 받음)과 놓기의 실행은 그 창의 tree에
        //    직접 묻는다 (Win32와 같다).
        //  - popup 판은 끌기를 받지 않는다 (Win32의 popup은 drop을 등록하지 않는다).

        // 끌기가 창에 들어왔다. 받을 수 있는 자리면 참이다.
        bool drag_entered(native_surface& surface, const NSPoint point, std::vector<std::u8string> files, std::vector<std::u8string> formats)
        {
            drag_payload_ = {};
            drag_payload_.custom_visual = true;
            drag_payload_.files = files;
            drag_payload_.formats = formats;
            const float x { static_cast<float>(point.x) };
            const float y { static_cast<float>(point.y) };
            if (host_ != nullptr)
            {
                file_drag_entered_event event { x, y, std::move(files), surface.id };
                event.formats = std::move(formats);
                host_->post_raw_input(std::move(event));
            }
            return drop_open(surface, x, y);
        }

        bool drag_moved(native_surface& surface, const NSPoint point)
        {
            const float x { static_cast<float>(point.x) };
            const float y { static_cast<float>(point.y) };
            if (host_ != nullptr)
                host_->post_raw_input(file_drag_moved_event { x, y, surface.id });
            return drop_open(surface, x, y);
        }

        void drag_left(native_surface& surface)
        {
            drag_payload_ = {};
            if (host_ != nullptr)
                host_->post_raw_input(file_drag_left_event { surface.id });
        }

        // 놓았다. 받았으면 참이다. `pasteboard`는 끌기의 붙여넣기 판이다 — `on_drop` 동안만 앱에 보인다.
        bool drag_dropped(native_surface& surface, const NSPoint point, NSPasteboard* const pasteboard)
        {
            drag_payload payload { drag_payload_ };
            const float x { static_cast<float>(point.x) };
            const float y { static_cast<float>(point.y) };
            const std::shared_ptr<const ui_tree> held { surface.main() && host_ != nullptr ? host_->acquire_ui_tree() : nullptr };
            const ui_tree* const tree { surface.main() ? held.get() : surface.want.tree.get() };
            const drop_hit_result hit { tree != nullptr ? tree->drop_hit_test(x, y, payload) : drop_hit_result {} };
            const ui_element* const over { hit.target };
            if (over != nullptr && over->drop()->on_drop)
            {
                // 놓는 순간의 수식 키다 (내부 끌기의 놓기와 같은 규약, Command가 Ctrl의 자리다).
                ui_action_context context { over->id(), x, y };
                const NSEventModifierFlags flags { [NSEvent modifierFlags] };
                context.control = (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
                context.shift = (flags & NSEventModifierFlagShift) != 0;
                context.alt = (flags & NSEventModifierFlagOption) != 0;
                // 원시 데이터는 끌기의 `NSPasteboard*`다. 이 호출 동안만 유효하다.
                context.native_drop_data = (__bridge void*)pasteboard;
                for (input_action& action : over->drop()->on_drop(payload, context))
                    dispatch(std::move(action));
                drag_left(surface);
                return true;
            }
            drag_left(surface);
            // 불투명 표면(modal·메뉴·토스트)이 가렸거나, 파일 없는 끌기면 아무도 받지 않았다. popup 판은 끌기를 받지 않는다.
            if (hit.blocked || payload.files.empty())
                return false;
            // 수락 element가 없으면 창 전체의 물러섬이다 (`win32::window_delegate::on_file_dropped`, 경로 순서대로).
            if (host_ != nullptr && window_delegate_ != nullptr)
            {
                try
                {
                    for (const std::u8string& path : payload.files)
                        if (window_delegate_->on_file_dropped(*host_, path))
                            break;
                }
                catch (...)
                {
                    log_error(u8"The file drop delegate threw an exception.");
                }
            }
            return true;
        }

        [[nodiscard]] bool accepts_file_drop() const noexcept
        {
            return config_.accept_file_drop;
        }

        // 대기열의 OS 알림 사건을 앱에 낸다 (os-notification-design.md). 게시하지 못한 사건은 되돌려 다음 깨우기 때 다시 낸다.
        //  - 눌림을 냈으면 보이는 주 창을 앞으로 세운다. 숨은 주 창(`ui_frame::main_hidden`)은 보이게 하지 않는다.
        void deliver_notification_events()
        {
            if (host_ == nullptr || stopped_ || notification_events_.empty())
                return;
            const auto make = [this](const notification_event& event) { return environment_.delegate != nullptr ? environment_.delegate->make_notification_message(event) : app_message {}; };
            const auto post = [this](app_message message) { return host_->post_app_message_ticketed(std::move(message)); };
            const notification_delivery delivery { luil::deliver_notification_events(notification_events_, make, post) };
            if (delivery.failed_calls > 0)
                log_error(u8"The notification delegate threw an exception.");
            if (delivery.activated && main_hidden_ == false && main_.window != nil)
            {
                [NSApp activateIgnoringOtherApps:YES];
                [main_.window makeKeyAndOrderFront:nil];
            }
        }

        // 둘째 인스턴스가 보낸 인자다. 앱이 받았으면 참이다 (둘째의 `ok`).
        [[nodiscard]] bool receive_instance_arguments(const instance_arguments& arguments, const bool activate)
        {
            if (host_ == nullptr || stopped_ || window_delegate_ == nullptr)
                return false;
            bool accepted { false };
            try
            {
                accepted = window_delegate_->on_instance_arguments(*host_, arguments);
            }
            catch (...)
            {
                log_error(u8"The instance arguments delegate threw an exception.");
            }
            if (activate && main_hidden_ == false && main_.window != nil)
            {
                [NSApp activateIgnoringOtherApps:YES];
                [main_.window makeKeyAndOrderFront:nil];
            }
            return accepted;
        }

        void post_permission(const app_permission permission, const permission_state state)
        {
            if (host_ == nullptr || environment_.delegate == nullptr || stopped_)
                return;
            if (app_message message { environment_.delegate->make_permission_message(permission, state) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 파일 대화 상자의 답이다. 앱에 알리고 다음 요청을 받을 수 있게 한다.
        void complete_file_dialog(const file_dialog_result& result)
        {
            if (host_ != nullptr && environment_.delegate != nullptr && stopped_ == false)
                host_->post_file_dialog_message(environment_.delegate->make_file_dialog_message(result));
            file_dialog_gate_.finish();
        }

        void on_timer()
        {
            timer_ = nil;
            timer_deadline_.reset();
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
            refresh_user_ui_language();
            // 데스크톱이다. 창의 제목 막대는 시스템의 것이라 앱이 caption을 그리지 않는다. 테두리 없는 창이면 앱이 그린다.
            set_ui_platform({
                .form_factor = ui_form_factor::desktop,
                .window_caption = config_.borderless,
                .rounded_popup_corners = true,
                .command_key_shortcuts = true,
                .capabilities = {
                    .haptics = true,
                    .taskbar = true,
                    .notifications = notification_center_available(),
                    .clipboard_image_copy = true,
                    .tray = true,
                    .global_hotkeys = true,
                },
            });
            set_font_fallback(&font_resolver_);
            window_delegate_ = dynamic_cast<win32::window_delegate*>(environment_.delegate);

            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:activation_policy_for(config_.activation)];
            application_delegate_ = [LuilApplicationDelegate new];
            application_delegate_.owner = this;
            [NSApp setDelegate:application_delegate_];
            install_menu();
            main_.id.clear();
            create_window(main_, NSMakeSize(config_.initial_width, config_.initial_height), NSMakeSize(config_.minimum_width, config_.minimum_height), make_ns_string(config_.title), false);
            place_main_window();
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
            host_config.wake.capture = [signal] { static_cast<void>(signal()); };
            host_config.interaction.double_click_time = std::chrono::milliseconds { static_cast<int>(std::lround([NSEvent doubleClickInterval] * 1000.0)) };
            host_config.interaction.touch = config_.touch;
            // 햅틱은 Force Touch 트랙패드의 되울림이다. 장치가 없거나 꺼졌으면 조용하다. 아무 thread에서 오므로 메인으로 넘긴다.
            host_config.haptic = [](const haptic_feedback feedback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                  const NSHapticFeedbackPattern pattern { feedback == haptic_feedback::tick ? NSHapticFeedbackPatternAlignment
                          : feedback == haptic_feedback::confirm || feedback == haptic_feedback::reject ? NSHapticFeedbackPatternLevelChange
                                                                                                         : NSHapticFeedbackPatternGeneric };
                  [[NSHapticFeedbackManager defaultPerformer] performFeedbackPattern:pattern performanceTime:NSHapticFeedbackPerformanceTimeNow];
                });
            };
            // 묻는 권한은 OS 알림 하나다 (UserNotifications의 허락). 답은 메인 thread에서 앱 메시지로 낸다.
            host_config.permission = [](const app_permission permission) {
                return permission == app_permission::notifications ? notification_permission() : permission_state::not_required;
            };
            host_config.request_permission = [wake](const app_permission permission) {
                if (permission != app_permission::notifications)
                    return false;
                return request_notification_permission([wake](const permission_state state) {
                    if (wake->target != nullptr)
                        wake->target->post_permission(app_permission::notifications, state);
                });
            };
            main_.router.set_touch_config(config_.touch);
            if (app_message message { observe_appearance() }; message.empty() == false)
                host_config.initial_messages.push_back(std::move(message));
            if (app_message message { metrics_message(main_) }; message.empty() == false)
                host_config.initial_messages.push_back(std::move(message));
            host_ = std::make_unique<app_host>(std::move(host_config), *environment_.driver, environment_.policy);
            create_webviews(main_);

            create_renderer(main_);
            if (main_.renderer == nullptr)
                return false;
            // 알림의 눌림·닫음이다. 꺼진 앱을 띄운 눌림도 받도록 `[NSApp run]` 전에 건다.
            set_notification_receiver([state = wake](notification_event event) {
                if (state->target == nullptr)
                    return;
                state->target->notification_events_.push(std::move(event));
                state->target->deliver_notification_events();
            });
            create_shell_hosts();
            // 주 창의 네이티브 표면을 앱에 건넨다 (`NSWindow*`). on_started 앞에 한 번이다.
            if (window_delegate_ != nullptr)
                window_delegate_->on_native_window_created(*host_, (__bridge void*)main_.window);
            if (environment_.delegate != nullptr)
                environment_.delegate->on_started(*host_);
            // 둘째 인스턴스의 인자를 받는다. on_started 뒤, 창을 보이기 전에 단다.
            if (environment_.instance != nullptr && environment_.instance->primary())
                if (const platform_result opened { set_instance_receiver(*environment_.instance, [state = wake_](const instance_arguments& arguments, const bool activate) {
                        return state->target != nullptr && state->target->receive_instance_arguments(arguments, activate);
                    }) };
                    opened.status != platform_status::ok)
                    log_error(u8"Single instance receiver: " + opened.error);
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
            set_notification_receiver(nullptr);
            if (environment_.instance != nullptr)
                clear_instance_receiver(*environment_.instance);
            // 전역 단축키를 풀고 메뉴 막대 항목과 Dock 타일을 거둔다.
            hotkeys_.reset();
            tray_.reset();
            dock_.reset();
            if (window_delegate_ != nullptr)
            {
                try
                {
                    window_delegate_->on_native_window_destroying(*host_);
                }
                catch (...)
                {
                    log_error(u8"The native window delegate threw an exception.");
                }
            }
            // 웹뷰 사건이 끝나는 host로 가지 않게 먼저 거둔다.
            if (main_.webviews != nullptr)
                main_.webviews->shutdown();
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->webviews != nullptr)
                    surface->webviews->shutdown();
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

        // 창 하나와 그 view를 세운다 (자리는 부르는 쪽이 정한다).
        //  - 주 창과 독립 창은 보통 창이다. 소유된 보조 창은 떠 있는 판(`NSPanel`)이라 주 창 위에 서고, 앱이 뒤로 가면 함께 숨는다.
        //  - 도킹 표면은 테두리 없이 활성화되지 않는 판이다. 언제나 위에 뜨고 모든 데스크톱 공간에 함께 선다.
        //  - `borderless`면 주 창·보조 창에 제목 막대가 없고 앱이 caption을 그린다. 가장자리 크기 조절과 최소화는 남는다.
        void create_window(native_surface& surface, const NSSize content, const NSSize minimum, NSString* const title, const bool panel)
        {
            const NSRect frame { NSMakeRect(0, 0, content.width, content.height) };
            const bool borderless { config_.borderless && surface.docked == false };
            surface.custom_caption = borderless;
            NSWindowStyleMask style { borderless ? NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
                                                 : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable };
            if (panel == false)
                style |= NSWindowStyleMaskMiniaturizable;
            if (surface.docked)
            {
                LuilDockPanel* const window { [[LuilDockPanel alloc] initWithContentRect:frame
                                                                               styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                                                 backing:NSBackingStoreBuffered
                                                                                   defer:NO] };
                window.floatingPanel = YES;
                window.level = NSStatusWindowLevel;
                window.becomesKeyOnlyIfNeeded = YES;
                window.hidesOnDeactivate = NO;
                window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle
                    | NSWindowCollectionBehaviorFullScreenAuxiliary;
                surface.window = window;
            }
            else if (panel)
            {
                // 테두리 없는 판은 키 창이 되게 한다 (기본 `NSPanel`은 제목 막대가 없으면 키 창이 되지 않는다).
                NSPanel* const window { borderless ? [[LuilDockPanel alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO]
                                                   : [[NSPanel alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO] };
                window.floatingPanel = YES;
                // 주 창이 늘 위에 뜨면 소유된 판은 그 위의 층이다.
                window.level = owned_panel_level();
                window.becomesKeyOnlyIfNeeded = NO;
                window.hidesOnDeactivate = YES;
                surface.window = window;
            }
            else
                surface.window = [[LuilWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
            NSWindow* const window { surface.window };
            window.releasedWhenClosed = NO;
            window.title = title ?: @"luil";
            window.contentMinSize = minimum;
            window.acceptsMouseMovedEvents = YES;
            window.tabbingMode = NSWindowTabbingModeDisallowed;
            surface.delegate = [LuilWindowDelegate new];
            surface.delegate.owner = this;
            surface.delegate.surface = &surface;
            window.delegate = surface.delegate;
            install_content(surface, frame);
            if (surface.main())
                apply_main_look(surface);
            // OS 끌어 놓기다. 파일 경로와, 파일 없이 형식만 실린 끌기(주소·글·그림)를 받는다.
            if (config_.accept_file_drop)
                [surface.view registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeURL, NSPasteboardTypeString, NSPasteboardTypePNG, NSPasteboardTypeTIFF ]];
            read_surface_size(surface);
        }

        // 창 내용을 세운다: 그릇 view 하나와 그 안의 Skia view, 포인터 추적, 접근성 다리다 (창과 popup 판이 같다).
        //  - 그릇 안에 Skia view와 웹뷰들이 형제로 선다 (macos/webview_host.h). Skia view는 불투명하지 않다. 아래층 웹뷰의 구멍(알파 0)으로
        //    그 아래가 보인다.
        //  - 포인터 이동·들어옴·나감은 `inactive_hover`면 앱이 앞에 있지 않아도 온다 (Windows처럼 보이는 창이면 hover를 받는다).
        void install_content(native_surface& surface, const NSRect frame)
        {
            NSWindow* const window { surface.window };
            surface.container = [[LuilContainerView alloc] initWithFrame:frame];
            surface.container.wantsLayer = YES;
            surface.container.layer.backgroundColor = content_background(surface);
            surface.view = [[LuilView alloc] initWithFrame:surface.container.bounds];
            surface.view.owner = this;
            surface.view.surface = &surface;
            surface.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            surface.view.wantsLayer = YES;
            surface.view.layer.contentsGravity = kCAGravityTopLeft;
            surface.view.layer.opaque = NO;
            [surface.container addSubview:surface.view];
            window.contentView = surface.container;
            [window makeFirstResponder:surface.view];
            const NSTrackingAreaOptions active { config_.inactive_hover ? NSTrackingActiveAlways : NSTrackingActiveInActiveApp };
            NSTrackingArea* const tracking { [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                                          options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | active | NSTrackingInVisibleRect
                                                                            owner:surface.view
                                                                         userInfo:nil] };
            [surface.view addTrackingArea:tracking];
            create_accessibility(surface);
        }

        // 주 창의 모양과 층이다: 비치는 바탕·둥근 모서리, 늘 위, 모든 공간.
        void apply_main_look(native_surface& surface)
        {
            NSWindow* const window { surface.window };
            if (main_translucent())
            {
                window.opaque = NO;
                window.backgroundColor = [NSColor clearColor];
                window.hasShadow = YES;
                if (config_.corner_radius > 0.0f)
                {
                    surface.container.layer.cornerRadius = config_.corner_radius;
                    surface.container.layer.masksToBounds = YES;
                    if (@available(macOS 11.0, *))
                        surface.container.layer.cornerCurve = kCACornerCurveContinuous;
                }
                // 알파 0인 자리의 누름도 이 창이 받는다 (Windows의 알파 1 올림과 같은 뜻). 정하지 않으면 시스템이 그 누름을 뒤 창으로 보낸다.
                if (config_.presentation == win32::window_presentation::layered && config_.layered_click_through == false)
                    window.ignoresMouseEvents = NO;
            }
            applied_topmost_ = config_.topmost;
            window.level = config_.topmost ? NSFloatingWindowLevel : NSNormalWindowLevel;
            if (config_.all_spaces)
                window.collectionBehavior = window.collectionBehavior | NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        }

        // 소유된 보조 창의 층이다. 주 창보다 늘 위다.
        [[nodiscard]] NSWindowLevel owned_panel_level() const noexcept
        {
            return applied_topmost_.value_or(config_.topmost) ? NSFloatingWindowLevel + 1 : NSFloatingWindowLevel;
        }

        // 앱이 frame에 실은 주 창의 층을 옮긴다 (`ui_frame::topmost`). 빈 값이면 그대로다.
        void apply_topmost(const ui_frame* const frame)
        {
            if (frame == nullptr || frame->topmost.has_value() == false || frame->topmost == applied_topmost_ || main_.window == nil)
                return;
            applied_topmost_ = frame->topmost;
            main_.window.level = *frame->topmost ? NSFloatingWindowLevel : NSNormalWindowLevel;
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->docked == false && surface->want.independent == false)
                    surface->window.level = owned_panel_level();
        }

        [[nodiscard]] static NSApplicationActivationPolicy activation_policy_for(const activation_policy policy)
        {
            switch (policy)
            {
            case activation_policy::regular:
                return NSApplicationActivationPolicyRegular;
            case activation_policy::accessory:
                return NSApplicationActivationPolicyAccessory;
            case activation_policy::automatic:
                break;
            }
            id const element { [[NSBundle mainBundle] objectForInfoDictionaryKey:@"LSUIElement"] };
            const bool agent { [element respondsToSelector:@selector(boolValue)] && [element boolValue] };
            return agent ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular;
        }

        // 주 창의 처음 자리다. 저장 배치(`initial_placement`) → 왼쪽 위 자리(`initial_position`) → 주 화면 가운데의 차례다.
        //  - 저장 배치는 Windows와 같은 규칙으로 거른다 (`accepted_initial_placement`). 확대·전체 화면이었으면 그 상태로 선다.
        void place_main_window()
        {
            NSWindow* const window { main_.window };
            const CGFloat scale { primary_scale() };
            if (config_.initial_placement.has_value())
            {
                const std::vector<display_info> displays { current_displays() };
                const int minimum_width { static_cast<int>(std::lround(config_.minimum_width * scale)) };
                const int minimum_height { static_cast<int>(std::lround(config_.minimum_height * scale)) };
                if (const std::optional<window_placement> placement { accepted_initial_placement(*config_.initial_placement, minimum_width, minimum_height, displays) };
                    placement.has_value())
                {
                    const NSSize size { placement->width / scale, placement->height / scale };
                    const NSPoint top_left { from_screen_pixels(placement->x, placement->y) };
                    [window setFrame:[window frameRectForContentRect:NSMakeRect(top_left.x, top_left.y - size.height, size.width, size.height)] display:NO];
                    normal_placement_ = content_placement_of(window.frame, window);
                    if (placement->maximized)
                        set_zoomed(main_, true);
                    initial_fullscreen_ = placement->fullscreen;
                    zoomed_before_fullscreen_ = placement->maximized;
                    return;
                }
            }
            if (config_.initial_position.has_value())
            {
                NSRect frame { window.frame };
                const NSPoint top_left { from_screen_pixels(config_.initial_position->x, config_.initial_position->y) };
                frame.origin = NSMakePoint(top_left.x, top_left.y - frame.size.height);
                // 화면 밖으로 나가지 않게 그 화면의 보이는 자리 안으로 민다 (보조 창의 처음 자리와 같은 규칙).
                if (NSScreen* const screen { screen_for(frame) }; screen != nil)
                {
                    const NSRect visible { screen.visibleFrame };
                    frame.origin.x = std::clamp(frame.origin.x, NSMinX(visible), std::max(NSMinX(visible), NSMaxX(visible) - frame.size.width));
                    frame.origin.y = std::clamp(frame.origin.y, NSMinY(visible), std::max(NSMinY(visible), NSMaxY(visible) - frame.size.height));
                }
                [window setFrame:frame display:NO];
                return;
            }
            [window center];
        }

        // --- 시스템 제목 막대의 caption 앱 단추 (`caption_config::app_buttons`) ---
        //  - 제목 막대가 있는 창이면 tree의 `caption_element`는 자리를 차지하지 않는다. 그 설정의 앱 단추를 제목 막대 오른쪽 끝의 부속 view
        //    (`NSTitlebarAccessoryViewController`)에 같은 글리프·tooltip·접근성 이름·켬 상태로 세운다. 누르면 그 단추의 액션을 낸다.
        //  - 게시된 tree를 frame마다 읽는다. 단추를 더하거나 빼거나 끄는 것이 다음 frame부터 그대로 선다 (Win32의 판정과 같다).

        void update_titlebars()
        {
            update_titlebar(main_);
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->docked == false)
                    update_titlebar(*surface);
        }

        void update_titlebar(native_surface& surface)
        {
            NSWindow* const window { surface.window };
            if (window == nil || surface.custom_caption || has_title_bar(window) == false)
                return;
            const ui_tree* const tree { surface_tree(surface) };
            const auto* const caption { tree != nullptr ? dynamic_cast<const caption_element*>(tree->find(ui_element_id { ui_element_kind::caption })) : nullptr };
            std::vector<std::tuple<std::u8string, char32_t, std::u8string, bool>> wanted {};
            if (caption != nullptr)
                for (const caption_app_button& button : caption->config().app_buttons)
                    if (button.action != nullptr)
                        wanted.emplace_back(button.key, button.glyph, button.tooltip, button.enabled);
            if (wanted == surface.titlebar_buttons)
                return;
            surface.titlebar_buttons = wanted;
            if (surface.titlebar != nil)
            {
                if (const NSUInteger index { [window.titlebarAccessoryViewControllers indexOfObject:surface.titlebar] }; index != NSNotFound)
                    [window removeTitlebarAccessoryViewControllerAtIndex:static_cast<NSInteger>(index)];
                surface.titlebar = nil;
            }
            if (wanted.empty())
                return;
            if (surface.titlebar_target == nil)
            {
                surface.titlebar_target = [LuilTitlebarTarget new];
                surface.titlebar_target.owner = this;
                surface.titlebar_target.surface = &surface;
            }
            const CGFloat icon { caption != nullptr ? static_cast<CGFloat>(caption->config().metrics.button_icon_size) : 14.0 };
            const CGFloat button_width { 28.0 };
            const CGFloat button_height { 22.0 };
            NSView* const row { [[NSView alloc] initWithFrame:NSMakeRect(0, 0, button_width * static_cast<CGFloat>(wanted.size()) + 6.0, button_height)] };
            CGFloat x { 0.0 };
            for (const auto& [key, glyph, tooltip, enabled] : wanted)
            {
                NSButton* const button { [NSButton buttonWithImage:glyph_image(glyph, icon) target:surface.titlebar_target action:@selector(press:)] };
                button.frame = NSMakeRect(x, 0, button_width, button_height);
                button.bordered = NO;
                button.imagePosition = NSImageOnly;
                button.identifier = make_ns_string(key);
                button.toolTip = make_ns_string(tooltip);
                // 보조 기술이 읽는 요소는 단추의 셀이다. 글리프뿐인 단추라 이름은 tooltip이다 (`caption_app_button::tooltip`).
                button.accessibilityLabel = make_ns_string(tooltip);
                button.cell.accessibilityLabel = make_ns_string(tooltip);
                button.enabled = enabled ? YES : NO;
                [row addSubview:button];
                x += button_width;
            }
            NSTitlebarAccessoryViewController* const controller { [NSTitlebarAccessoryViewController new] };
            controller.view = row;
            controller.layoutAttribute = NSLayoutAttributeRight;
            [window addTitlebarAccessoryViewController:controller];
            surface.titlebar = controller;
        }

    public:
        // 제목 막대의 앱 단추를 눌렀다. 지금 게시된 tree의 같은 key 단추의 액션을 낸다 (그 단추를 tree에서 누른 것과 같다).
        void titlebar_pressed(native_surface& surface, const std::u8string& key)
        {
            const ui_tree* const tree { surface_tree(surface) };
            const auto* const caption { tree != nullptr ? dynamic_cast<const caption_element*>(tree->find(ui_element_id { ui_element_kind::caption })) : nullptr };
            if (caption == nullptr)
                return;
            for (const caption_app_button& button : caption->config().app_buttons)
            {
                if (button.key != key || button.action == nullptr || button.enabled == false)
                    continue;
                try
                {
                    for (input_action& action : button.action(ui_action_context { ui_element_id { ui_element_kind::caption_app_button, key } }))
                        dispatch(std::move(action));
                }
                catch (...)
                {
                    log_error(u8"A caption app button action threw an exception.");
                }
                break;
            }
            pump();
        }

    private:
        // codicon 글리프 하나의 그림이다. 제목 막대 단추가 시스템 색으로 칠하도록 틀 그림(template)이다. 글리프마다 한 번 짓는다.
        [[nodiscard]] NSImage* glyph_image(const char32_t glyph, const CGFloat size)
        {
            const std::pair<char32_t, int> key { glyph, static_cast<int>(std::lround(size)) };
            if (const auto found { glyph_images_.find(key) }; found != glyph_images_.end())
                return found->second;
            if (codicon_ == nullptr)
                codicon_ = load_codicon_typeface();
            // 레티나 화면에서도 또렷하게 두 배로 그린다.
            const int pixels { std::max(1, static_cast<int>(std::ceil(size * 2.0))) };
            sk_sp<SkSurface> raster { SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pixels, pixels)) };
            NSImage* image { [[NSImage alloc] initWithSize:NSMakeSize(size, size)] };
            if (raster != nullptr && codicon_ != nullptr)
            {
                SkCanvas* const canvas { raster->getCanvas() };
                canvas->clear(SK_ColorTRANSPARENT);
                const SkFont font { codicon_, static_cast<SkScalar>(pixels) };
                const std::uint32_t text { static_cast<std::uint32_t>(glyph) };
                SkRect bounds {};
                font.measureText(&text, sizeof(text), SkTextEncoding::kUTF32, &bounds);
                SkPaint paint {};
                paint.setColor(SK_ColorBLACK);
                paint.setAntiAlias(true);
                canvas->drawSimpleText(&text, sizeof(text), SkTextEncoding::kUTF32, (pixels - bounds.width()) / 2.0f - bounds.left(),
                    (pixels - bounds.height()) / 2.0f - bounds.top(), font, paint);
                SkPixmap pixmap {};
                if (raster->peekPixels(&pixmap))
                {
                    CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
                    CGContextRef const context { CGBitmapContextCreate(pixmap.writable_addr(), static_cast<std::size_t>(pixels), static_cast<std::size_t>(pixels), 8, pixmap.rowBytes(),
                        space, static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little)) };
                    CGColorSpaceRelease(space);
                    if (context != nullptr)
                    {
                        if (CGImageRef const picture { CGBitmapContextCreateImage(context) }; picture != nullptr)
                        {
                            image = [[NSImage alloc] initWithCGImage:picture size:NSMakeSize(size, size)];
                            CGImageRelease(picture);
                        }
                        CGContextRelease(context);
                    }
                }
            }
            // `template`는 C++의 낱말이라 속성 대신 설정 메서드를 부른다.
            [image setTemplate:YES];
            glyph_images_.emplace(key, image);
            return image;
        }

        void create_renderer(native_surface& surface)
        {
            CALayer* const layer { surface.view.layer };
            renderer_factories factories {};
            factories.gpu_name = u8"Metal";
            factories.create_gpu = [layer, simulate = config_.simulate_gpu_failure]() -> renderer_factory_result {
                if (simulate)
                    return { nullptr, u8"The smoke test injected a Metal creation failure." };
                renderer_factory_result result { create_metal_skia_renderer((__bridge void*)layer) };
                // `automatic`의 물러섬은 이 이유를 버리므로 여기서 남긴다.
                if (result.renderer == nullptr)
                    log_error(u8"Metal renderer: " + result.error);
                return result;
            };
            factories.create_cpu = [layer] { return create_cpu_skia_renderer((__bridge void*)layer); };
            const renderer_fault_injection fault { .at_creation = false, .after_frames = surface.main() ? config_.simulate_gpu_loss_after_frames : 0 };
            std::u8string error {};
            surface.renderer = renderer_host::create(config_.renderer, fault, std::move(factories), error);
            if (surface.renderer == nullptr)
            {
                log_error(error);
                return;
            }
            if (surface.renderer->resize(surface.width, surface.height, error) == false)
                log_error(error);
            report_renderer(surface);
        }

        // 지금 그리는 렌더러를 한 줄 남긴다. 주 창의 처음과 물러선 뒤에만이다 (Android의 logcat 줄과 같은 자리).
        void report_renderer(native_surface& surface)
        {
            if (surface.renderer == nullptr || surface.main() == false)
                return;
            const std::u8string_view name { renderer_backend_name(surface.renderer->backend()) };
            if (surface.reported_backend.has_value() && *surface.reported_backend == surface.renderer->backend())
                return;
            surface.reported_backend = surface.renderer->backend();
            std::fprintf(stderr, "luil: renderer %.*s%s\n", static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()),
                surface.renderer->used_fallback() ? " (fallback)" : "");
        }

        // 창 내용 영역의 물리 크기와 배율을 읽는다.
        static void read_surface_size(native_surface& surface)
        {
            const NSRect bounds { surface.view.bounds };
            surface.scale = static_cast<float>(surface.window.backingScaleFactor > 0 ? surface.window.backingScaleFactor : 1.0);
            surface.width = std::max(1, static_cast<int>(std::lround(bounds.size.width * surface.scale)));
            surface.height = std::max(1, static_cast<int>(std::lround(bounds.size.height * surface.scale)));
        }

        void refresh_metrics(native_surface& surface)
        {
            const int previous_width { surface.width };
            const int previous_height { surface.height };
            read_surface_size(surface);
            if (surface.width != previous_width || surface.height != previous_height)
            {
                static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_resized));
                place_popups();
            }
            if (surface.renderer != nullptr)
            {
                std::u8string error {};
                if (surface.renderer->resize(surface.width, surface.height, error) == false)
                    log_error(error);
            }
            surface.dirty = true;
            post_metrics(surface);
        }

        // 크기·배율을 앱에 알릴 메시지다. 같은 값이면 비었다. 주 창은 delegate가, 보조 창은 그 창의 `metrics`가 짓는다.
        [[nodiscard]] app_message metrics_message(native_surface& surface)
        {
            const float width { static_cast<float>(surface.width) };
            const float height { static_cast<float>(surface.height) };
            if (width == surface.reported_width && height == surface.reported_height && surface.scale == surface.reported_scale
                && (surface.main() == false || system_.reduce_motion == surface.reported_reduce_motion))
                return {};
            if (surface.main())
            {
                if (environment_.delegate == nullptr)
                    return {};
                surface.reported_width = width;
                surface.reported_height = height;
                surface.reported_scale = surface.scale;
                surface.reported_reduce_motion = system_.reduce_motion;
                window_metrics metrics { make_window_metrics(width, height, surface.scale) };
                metrics.reduce_motion = system_.reduce_motion;
                return environment_.delegate->make_metrics_message(metrics);
            }
            if (surface.want.metrics == nullptr)
                return {};
            surface.reported_width = width;
            surface.reported_height = height;
            surface.reported_scale = surface.scale;
            return surface.want.metrics(width, height, surface.scale);
        }

        void post_metrics(native_surface& surface)
        {
            if (host_ == nullptr)
                return;
            if (app_message message { metrics_message(surface) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 창 내용의 지금 자리다. x·y는 창 내용의 왼쪽 위, 크기는 내용의 픽셀이다. 둘 다 이 파일 첫머리의 화면 좌표 약속(주 화면
        // 배율)을 따른다.
        //  - 확대 여부는 `is_zoomed`다. 제목 막대가 없는 창은 앱이 확대한 때만 참이다 (`isZoomed`는 그런 창에서 거짓 참을 낸다).
        [[nodiscard]] static window_placement placement_of(const native_surface& surface)
        {
            window_placement placement { content_placement_of(surface.window.frame, surface.window) };
            placement.maximized = is_zoomed(surface);
            return placement;
        }

        // 창의 자리를 앱에 알린다. 같은 값이면 다시 알리지 않는다.
        //  - 보조 창은 `ui_window::placement`, 주 창은 delegate의 `win32::window_delegate::make_window_placement_message`다.
        //  - 주 창이 확대(zoom)됐거나 전체 화면이면 x·y·크기는 지금 사각형이 아니라 돌아갈 자리다 (`window_placement`의 약속).
        //    그 자리는 마지막으로 보통 상태였을 때의 사각형이다.
        //  - 전체 화면으로 드나드는 동안의 크기 변화는 알리지 않는다. 다 들어가고 나온 뒤에 한 번 알린다.
        void report_placement(native_surface& surface)
        {
            if (host_ == nullptr || surface.window == nil || surface.window.miniaturized || surface.popup.has_value())
                return;
            window_placement placement { placement_of(surface) };
            if (surface.main())
            {
                // 처음 자리를 정하는 동안은 알리지 않는다. 창이 보인 뒤 실제 자리를 한 번 알린다 (`run`).
                if (window_delegate_ == nullptr || fullscreen_transition_ || placement_ready_ == false)
                    return;
                const bool fullscreen { (surface.window.styleMask & NSWindowStyleMaskFullScreen) != 0 };
                // 확대해 들어가는 동안(애니메이션)의 중간 크기는 보통 자리가 아니다. 확대가 다 선 뒤에 풀린다.
                if (placement.maximized)
                    zooming_in_ = false;
                if (fullscreen == false && placement.maximized == false && zooming_in_ == false)
                    normal_placement_ = placement;
                else if (normal_placement_.has_value())
                {
                    const bool maximized { fullscreen ? zoomed_before_fullscreen_ : placement.maximized };
                    placement = *normal_placement_;
                    placement.maximized = maximized;
                }
                placement.fullscreen = fullscreen;
                if (surface.reported_placement == placement)
                    return;
                surface.reported_placement = placement;
                if (app_message message { window_delegate_->make_window_placement_message(placement) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (surface.want.placement == nullptr || surface.reported_placement == placement)
                return;
            surface.reported_placement = placement;
            if (app_message message { surface.want.placement(placement) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 앱이 낸 주 창 배치 요청을 옮긴다 (`ui_frame::window_placement_request`). revision이 바뀐 때만이다.
        //  - 너무 작은 크기는 최소 크기로 올리고, 어느 화면에도 걸치지 않는 자리는 주 화면 가운데로 옮긴다.
        //  - 전체 화면 값은 `ui_frame::fullscreen`이 없을 때만 따른다 (그쪽이 앞선다).
        void apply_placement_request(const ui_frame& frame)
        {
            if (frame.window_placement_revision == applied_placement_revision_ || frame.window_placement_request.has_value() == false || main_.window == nil)
                return;
            applied_placement_revision_ = frame.window_placement_revision;
            const window_placement& want { *frame.window_placement_request };
            if (want.valid() == false)
                return;
            NSWindow* const window { main_.window };
            const bool fullscreen { (window.styleMask & NSWindowStyleMaskFullScreen) != 0 };
            const CGFloat scale { primary_scale() };
            const NSSize minimum { window.contentMinSize };
            const NSSize size { std::max<CGFloat>(want.width / scale, minimum.width), std::max<CGFloat>(want.height / scale, minimum.height) };
            const NSPoint top_left { from_screen_pixels(want.x, want.y) };
            NSRect frame_rect { [window frameRectForContentRect:NSMakeRect(top_left.x, top_left.y - size.height, size.width, size.height)] };
            bool on_screen { false };
            for (NSScreen* const screen in [NSScreen screens])
                on_screen = on_screen || NSIntersectsRect(screen.visibleFrame, frame_rect);
            if (on_screen == false)
                if (NSScreen* const screen { [NSScreen screens].firstObject }; screen != nil)
                {
                    const NSRect visible { screen.visibleFrame };
                    frame_rect.origin = NSMakePoint(NSMidX(visible) - frame_rect.size.width / 2, NSMidY(visible) - frame_rect.size.height / 2);
                }
            // 전체 화면 안에서는 돌아갈 자리만 바꾼다. 나오면 그 자리로 간다.
            if (fullscreen)
            {
                normal_placement_ = content_placement_of(frame_rect, window);
                zoomed_before_fullscreen_ = want.maximized;
            }
            else
            {
                set_zoomed(main_, false);
                [window setFrame:frame_rect display:YES];
                normal_placement_ = content_placement_of(window.frame, window);
                if (want.maximized)
                    set_zoomed(main_, true);
            }
            if (frame.fullscreen.has_value() == false && want.fullscreen != fullscreen)
                set_fullscreen(want.fullscreen);
            report_placement(main_);
        }

        [[nodiscard]] static window_placement content_placement_of(const NSRect frame_rect, NSWindow* const window)
        {
            const NSRect content { [window contentRectForFrameRect:frame_rect] };
            const screen_point top_left { to_screen_pixels(NSMakePoint(NSMinX(content), NSMaxY(content))) };
            const CGFloat scale { primary_scale() };
            return { top_left.x, top_left.y, static_cast<int>(std::lround(content.size.width * scale)), static_cast<int>(std::lround(content.size.height * scale)) };
        }

        // 주 창의 전체 화면을 바라는 상태로 옮긴다. 드나드는 중이거나 최소화·숨김이면 미루고, 끝난 뒤 다시 대조한다.
        void set_fullscreen(const bool enter)
        {
            NSWindow* const window { main_.window };
            if (window == nil || fullscreen_transition_ || window.miniaturized || window.visible == NO || config_.smoke_test)
                return;
            const bool fullscreen { (window.styleMask & NSWindowStyleMaskFullScreen) != 0 };
            if (fullscreen != enter)
                [window toggleFullScreen:nil];
        }

        // 앱이 frame에 실은 전체 화면 상태와 대조한다 (`ui_frame::fullscreen`).
        void reconcile_fullscreen()
        {
            if (host_ == nullptr)
                return;
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            if (frame != nullptr && frame->fullscreen.has_value())
                set_fullscreen(*frame->fullscreen);
        }

    public:
        // 전체 화면으로 드나든다. 드는 순간 확대 여부를 갈무리하고, 다 끝나면 배치를 한 번 알리고 frame과 다시 대조한다.
        void on_fullscreen_transition(native_surface& surface, const bool entering, const bool finished)
        {
            if (surface.main() == false)
                return;
            if (finished == false)
            {
                if (entering)
                    zoomed_before_fullscreen_ = is_zoomed(surface);
                fullscreen_transition_ = true;
                return;
            }
            fullscreen_transition_ = false;
            report_placement(surface);
            reconcile_fullscreen();
            pump();
        }

        // 창이 확대(zoom)되려 한다. 보통 상태에서 들어가면 지금 자리를 돌아갈 자리로 쥐고, 다 설 때까지 붙든다.
        //  - 확대가 서지 않는 경우(이미 화면을 채운 창)에 붙든 채 남지 않게 잠시 뒤 푼다.
        void on_will_zoom(native_surface& surface)
        {
            if (surface.main() == false || surface.window.zoomed || fullscreen_transition_)
                return;
            normal_placement_ = content_placement_of(surface.window.frame, surface.window);
            zooming_in_ = true;
            const std::shared_ptr<wake_state> state { wake_ };
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1'000'000'000), dispatch_get_main_queue(), ^{
              if (state->target != nullptr)
                  state->target->zoom_settled();
            });
        }

        void zoom_settled()
        {
            zooming_in_ = false;
            report_placement(main_);
        }

        // 최소화에서 되살아났다. 그동안 미룬 전체 화면 대조를 한다.
        void on_deminiaturized(native_surface& surface)
        {
            report_placement(surface);
            if (surface.main())
                reconcile_fullscreen();
            pump();
        }

    private:
        [[nodiscard]] bool window_visible(const native_surface& surface) const
        {
            // smoke test는 창을 보이지 않은 채 한 frame을 그린다.
            return config_.smoke_test || surface.window == nil || (surface.window.occlusionState & NSWindowOcclusionStateVisible) != 0;
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

        void mark_all_dirty()
        {
            main_.dirty = true;
            for (std::unique_ptr<native_surface>& surface : secondary_)
                surface->dirty = true;
            for (std::unique_ptr<native_surface>& popup : popups_)
                popup->dirty = true;
        }

        // 그릴 것과 다음에 깰 시각을 정리한다. 이벤트·깨우기·시계가 모두 여기로 온다.
        void pump()
        {
            if (host_ == nullptr || stopped_)
                return;
            reconcile_frame();
            process_file_dialog();
            process_focus_request();
            if (host_->faulted() && quitting_ == false)
            {
                log_error(u8"The logic or input thread stopped on an exception. Closing the window.");
                quit();
                return;
            }
            const auto time { now() };
            const auto paint = [this, time](native_surface& surface) {
                if (surface.update_deadline.has_value() && time >= *surface.update_deadline)
                    surface.dirty = true;
                // 보이지 않는 창(최소화, 다른 창이 다 가림)에는 그리지 않는다. Metal의 `nextDrawable`이 그동안 최대 1초를 막고,
                // caret 깜빡임 같은 예고가 그 막힘을 frame마다 되풀이한다. 다시 보이면 `on_occlusion_changed`가 그린다.
                //  - 접근성 계층은 그리지 않아도 맞춘다. 보조 기술과 자동화 도구는 가려진 창도 읽는다.
                if (window_visible(surface) == false)
                {
                    surface.update_deadline.reset();
                    if (surface.dirty)
                        update_accessibility(surface, surface.main() ? host_->acquire_ui_tree() : surface.want.tree);
                    return;
                }
                if (surface.dirty && surface.renderer != nullptr)
                    static_cast<void>(render(surface));
            };
            paint(main_);
            for (std::unique_ptr<native_surface>& surface : secondary_)
                paint(*surface);
            for (std::unique_ptr<native_surface>& popup : popups_)
                paint(*popup);
            arm_timer();
        }

        // 다음에 깰 시각에 시계를 건다. 창마다의 그림 예고와 초점 요청의 기한 가운데 가장 이른 쪽이다.
        void arm_timer()
        {
            std::optional<std::chrono::steady_clock::time_point> wake { main_.update_deadline };
            const auto earlier = [&wake](const std::optional<std::chrono::steady_clock::time_point>& candidate) {
                if (candidate.has_value())
                    wake = wake.has_value() ? std::min(*wake, *candidate) : *candidate;
            };
            for (const std::unique_ptr<native_surface>& surface : secondary_)
                earlier(surface->update_deadline);
            for (const std::unique_ptr<native_surface>& popup : popups_)
                earlier(popup->update_deadline);
            earlier(focus_tracker_.deadline());
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
            for (capture_command& request : host_->take_capture_requests())
                std::visit([this](auto&& value) { dispatch(input_action { std::move(value) }); }, std::move(request));
        }

        // 표면 표식으로 찍을 창을 찾는다. 빈 표식은 주 창이다. popup은 그 판을 찍는다 (Win32와 같다).
        [[nodiscard]] native_surface* surface_for_capture(const std::u8string& id)
        {
            if (native_surface* const surface { find_surface(id) }; surface != nullptr)
                return surface;
            return find_popup(id);
        }

        // 찍기·녹화는 실패해도 창을 끝내지 않고 알리기만 한다 (Win32와 같다).
        void execute_capture(const capture_request& request)
        {
            native_surface* const target { surface_for_capture(request.surface) };
            if (target == nullptr || host_ == nullptr)
            {
                log_error(u8"The capture target surface is gone.");
                return;
            }
            prepared_frame prepared {};
            prepare(*target, prepared, false);
            if (std::u8string error {}; write_capture(prepared.state, request.path, error) == false)
                log_error(error);
        }

        void execute_record(const record_request& request)
        {
            native_surface* const target { surface_for_capture(request.surface) };
            if (target == nullptr)
            {
                log_error(u8"The recording target surface is gone.");
                return;
            }
            std::u8string error {};
            const bool done { request.command == record_command::start ? target->recorder.start(request.path, error) : target->recorder.stop(error) };
            if (done == false)
                log_error(error);
            // 녹화는 그리는 장을 모은다. 시작하면 바로 한 장을 그린다.
            target->dirty = true;
        }

        // 창 명령은 주 창의 것이다 (Win32와 같다). 닫기는 주 창의 닫힘 훅을 거친다.
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
                [main_.window miniaturize:nil];
                break;
            case ui_command::window_toggle_maximize:
                toggle_zoom(main_);
                break;
            case ui_command::window_toggle_fullscreen:
                [main_.window toggleFullScreen:nil];
                break;
            default:
                break;
            }
        }

        // 이벤트를 input thread에 보낸다. popup 판의 사건은 판의 router가 흘린 만큼 옮겨 popup id를 단다.
        //  - popup 아닌 표면(주 창·보조 창·도킹 표면)의 누름과 휠은 popup 밖의 상호작용이다 (Win32 `input_dismisses_popups`).
        //    닫자는 메시지를 내고 누름은 그대로 간다. 닫았으면 누름에 싣는다 (`pointer_pressed_event::closed_popup`).
        void post_events(native_surface& surface, std::vector<raw_input_event> events)
        {
            if (host_ == nullptr)
                return;
            for (raw_input_event& event : events)
            {
                bool closed_popup { false };
                if (surface.popup.has_value() == false)
                {
                    if (std::holds_alternative<pointer_pressed_event>(event))
                        closed_popup = dismiss_popups(popup_dismiss_reason::pointer_press_outside);
                    else if (std::holds_alternative<mouse_wheel_event>(event))
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::wheel_scrolled));
                }
                routed_input routed { surface.router.route(std::move(event)) };
                if (routed.scroll.has_value())
                    scroll_popup(surface, *routed.scroll);
                for (raw_input_event& value : routed.events)
                {
                    label_surface(value, surface.id);
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

        [[nodiscard]] static NSPoint surface_point(const native_surface& surface, NSEvent* const event)
        {
            const NSPoint local { [surface.view convertPoint:[event locationInWindow] fromView:nil] };
            return NSMakePoint(local.x * surface.scale, local.y * surface.scale);
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

        // 키보드를 받는 창이다. 이 앱의 키 창이 없으면 주 창이다.
        [[nodiscard]] native_surface& key_surface()
        {
            NSWindow* const key { [NSApp keyWindow] };
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->window == key)
                    return *surface;
            return main_;
        }

        [[nodiscard]] const ui_tree* surface_tree(const native_surface& surface) const
        {
            if (surface.main())
                return host_ != nullptr ? host_->acquire_ui_tree().get() : nullptr;
            return surface.want.tree.get();
        }

        // 그 자리에 놓으면 누가 받는가다. 놓기와 같은 탐색이다 — 커서가 "받음"을 보인 자리에서 놓기가 사라지지 않는다.
        //  - 받는 element가 없어도 파일이 실렸으면 창 전체가 받는다 (물러섬).
        [[nodiscard]] bool drop_open(const native_surface& surface, const float x, const float y)
        {
            const std::shared_ptr<const ui_tree> held { surface.main() && host_ != nullptr ? host_->acquire_ui_tree() : nullptr };
            const ui_tree* const tree { surface.main() ? held.get() : surface.want.tree.get() };
            if (tree == nullptr)
                return drag_payload_.files.empty() == false;
            const drop_hit_result hit { tree->drop_hit_test(x, y, drag_payload_) };
            if (hit.blocked)
                return false;
            return drag_payload_.files.empty() == false || hit.target != nullptr;
        }

        void update_cursor(native_surface& surface, const NSPoint point)
        {
            if (host_ == nullptr)
                return;
            const float x { static_cast<float>(point.x) };
            const float y { static_cast<float>(point.y) };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            ui_cursor cursor { ui_cursor::inherit };
            // popup 판의 tree는 흘린 만큼 위로 올라가 있다.
            const float scroll { surface.popup.has_value() ? surface.popup->scroll_y : 0.0f };
            const std::shared_ptr<const ui_tree> main_tree { surface.main() ? host_->acquire_ui_tree() : nullptr };
            const ui_tree* const tree { surface.main() ? main_tree.get() : surface.want.tree.get() };
            if (tree != nullptr)
                cursor = tree->cursor_at(x, y + scroll, interaction_for_surface(interaction, surface.id));
            if (cursor == ui_cursor::none)
            {
                if (surface.cursor_hidden == false)
                    [NSCursor setHiddenUntilMouseMoves:YES];
                surface.cursor_hidden = true;
                return;
            }
            surface.cursor_hidden = false;
            NSCursor* resolved { nil };
            if (config_.resolve_cursor != nullptr)
                resolved = (__bridge NSCursor*)config_.resolve_cursor(cursor);
            [(resolved != nil ? resolved : cursor_for(cursor)) set];
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
            // id 없는 요청은 열지 않고 실패로 답한다. 조용히 버리면 앱이 답을 영영 기다린다.
            if (const std::optional<file_dialog_result> rejected { file_dialog_gate_.rejection(*frame->file_dialog) }; rejected.has_value())
            {
                complete_file_dialog(*rejected);
                return;
            }
            if (file_dialog_gate_.try_begin(frame->file_dialog->id) == false)
                return;
            // popup은 소유 창으로 쓰지 않고 그 사슬의 뿌리(주 창·보조 창)를 쓴다 (Win32와 같다). sheet가 열리면 popup은 초점을 잃어 닫힌다.
            std::u8string owner_id { frame->file_dialog->owner_surface };
            for (std::size_t depth { 0 }; depth < frame->popups.size(); ++depth)
            {
                const auto parent { std::find_if(frame->popups.begin(), frame->popups.end(), [&owner_id](const ui_popup& popup) { return popup.id == owner_id; }) };
                if (parent == frame->popups.end())
                    break;
                owner_id = parent->anchor;
            }
            const native_surface* const owner { find_surface(owner_id) };
            const std::shared_ptr<wake_state> state { wake_ };
            // 패널은 비동기다. 답은 run loop가 나중에 돌려준다 — 그 사이 앱이 끝났으면 버린다.
            run_file_dialog(owner != nullptr ? owner->window : main_.window, *frame->file_dialog, [state](file_dialog_result result) {
                if (state->target != nullptr)
                    state->target->complete_file_dialog(result);
            });
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
                else if (const native_surface* const surface { find_surface(request.surface) }; surface != nullptr)
                    tree = surface->want.tree.get();
                else if (const native_surface* const popup { find_popup(request.surface) }; popup != nullptr)
                    tree = popup->want.tree.get();
                return tree != nullptr && focus_target_ready(*tree, request.target);
            };
            const focus_request_tracker::step step { focus_tracker_.advance(published, time, ready) };
            if (step.fulfil.has_value())
            {
                // 활성화를 함께 청하면 그 창을 앞으로 세운다.
                if (native_surface* const surface { step.fulfil->surface.empty() ? &main_ : find_surface(step.fulfil->surface) }; surface != nullptr && surface->window != nil)
                    if (surface->window.keyWindow == NO && surface->window.visible)
                        [surface->window makeKeyAndOrderFront:nil];
                if (step.fulfil->target != ui_element_id {})
                    host_->post_raw_input(access_focus_event { step.fulfil->target, step.fulfil->surface, time });
            }
            if (frame != nullptr && frame->focus_report != nullptr)
                for (const focus_request_tracker::report& report : step.reports)
                    if (app_message message { frame->focus_report(report.id, report.outcome) }; message.empty() == false)
                        host_->post_app_message(std::move(message));
        }

        // frame이 바뀌면 창·popup·웹뷰를 거기에 맞춘다. 같은 frame이면 아무 일도 하지 않는다.
        void reconcile_frame()
        {
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            if (frame == reconciled_frame_ && frame != nullptr)
                return;
            reconciled_frame_ = frame;
            const std::span<const ui_webview> webviews { frame != nullptr ? std::span<const ui_webview> { frame->webviews } : std::span<const ui_webview> {} };
            if (main_.webviews != nullptr)
                main_.webviews->synchronize(webviews);
            reconcile_windows(frame.get());
            // 보조 창의 웹뷰는 창이 선 뒤에 맞춘다. 창이 아직 없는 앵커의 웹뷰는 서지 않는다 (popup 앵커와 같은 규칙).
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->webviews != nullptr)
                    surface->webviews->synchronize(webviews);
            apply_main_visibility(frame != nullptr && frame->main_hidden);
            apply_topmost(frame.get());
            update_popups(frame.get());
            update_titlebars();
            apply_backdrops(frame.get());
            report_displays(frame.get(), false);
            apply_shell(frame.get());
            if (frame != nullptr && window_delegate_ != nullptr)
            {
                try
                {
                    window_delegate_->on_native_frame(*host_, *frame);
                }
                catch (...)
                {
                    log_error(u8"The native frame delegate threw an exception.");
                }
            }
            if (frame != nullptr)
            {
                apply_placement_request(*frame);
                if (frame->fullscreen.has_value())
                    set_fullscreen(*frame->fullscreen);
            }
            mark_all_dirty();
        }

        // 창 하나의 접근성 다리를 세운다 (accessibility-design.md의 macOS 갈래).
        //  - 누름·초점은 곧바로 입력으로, 나머지 실행(값·펼침·여럿 고르기)은 액션으로, 글 바꿈은 편집 요청으로 낸다.
        //  - 다리가 부르는 함수는 앱이 사라진 뒤에 올 수 있다 (run loop에 미룬 실행). 깨우기 상태로 살아 있는지 본다.
        void create_accessibility(native_surface& surface)
        {
            const std::shared_ptr<wake_state> state { wake_ };
            accessibility_callbacks callbacks {};
            callbacks.post_input = [state](raw_input_event event) {
                if (state->target != nullptr && state->target->host_ != nullptr)
                    state->target->host_->post_raw_input(std::move(event));
            };
            callbacks.dispatch = [state](std::vector<input_action> actions) {
                for (input_action& action : actions)
                    if (state->target != nullptr)
                        state->target->dispatch(std::move(action));
            };
            callbacks.text_target = [state](const ui_element_kind kind) -> std::optional<text_input_target> {
                if (state->target == nullptr || state->target->environment_.policy == nullptr)
                    return std::nullopt;
                return state->target->environment_.policy->text_target_of(kind);
            };
            callbacks.text_edit = [state](text_edit_request request) {
                if (state->target != nullptr)
                    state->target->post_edit(std::move(request));
            };
            surface.accessibility = std::make_unique<accessibility_bridge>(surface.view, std::move(callbacks));
        }

        // 그린 상태를 접근성 다리에 건넨다. 바뀐 tree만 견줘 알림을 쏜다.
        void update_accessibility(native_surface& surface, const std::shared_ptr<const ui_tree>& tree)
        {
            if (surface.accessibility == nullptr || host_ == nullptr)
                return;
            accessibility_update update {};
            update.surface = surface.id;
            // popup 판은 판 하나를 덮는 겹 하나로 건넨다. 흘린 양을 다리가 함께 본다.
            if (surface.popup.has_value())
                update.overlays.push_back(accessibility_overlay { surface.id, tree,
                    rect_f { 0.0f, 0.0f, static_cast<float>(surface.width), static_cast<float>(surface.height) }, surface.popup->scroll_y });
            else
                update.tree = tree;
            update.interaction = host_->acquire_interaction();
            update.scale = surface.scale;
            surface.accessibility->update(std::move(update));
        }

        // 창 하나의 웹뷰 host를 세운다. 페이지의 메시지는 앱으로, 페이지를 눌러 옮겨 간 키보드 초점은 그 자리표의 논리 초점으로 낸다
        // (Windows의 `GotFocus`와 같은 자리).
        void create_webviews(native_surface& surface)
        {
            surface.webviews = std::make_unique<webview_host>(surface.container, surface.view, surface.id);
            surface.webviews->set_deliver([this](app_message message) {
                if (host_ != nullptr)
                    host_->post_app_message(std::move(message));
            });
            surface.webviews->set_focus_reporter([this, surface_id = surface.id](const std::u8string& id) {
                if (host_ != nullptr)
                    host_->post_raw_input(access_focus_event { ui_element_id { ui_element_kind::webview, id }, surface_id, now() });
            });
        }

        // 셸 쪽 표시다: 메뉴 막대 상태 항목(알림 영역 아이콘), Dock 타일·메뉴(작업 표시줄), 전역 단축키.
        //  - 보고는 Win32와 같은 `win32::window_delegate`의 메시지로 낸다. delegate가 그것을 구현하지 않았으면 내지 않는다.
        void create_shell_hosts()
        {
            const auto post = [this](app_message message) {
                if (host_ != nullptr && message.empty() == false)
                    host_->post_app_message(std::move(message));
            };
            const auto report = [this, post](auto make) {
                if (window_delegate_ == nullptr)
                    return;
                try
                {
                    post(make(*window_delegate_));
                }
                catch (...)
                {
                    log_error(u8"The shell report delegate threw an exception.");
                }
            };
            tray_ = std::make_unique<tray_icon_host>();
            tray_->set_dispatch([this](input_action action) { dispatch(std::move(action)); });
            tray_->set_report([report](const win32::tray_report& value) { report([&value](win32::window_delegate& delegate) { return delegate.make_tray_message(value); }); });
            tray_->set_error_log([](const std::u8string& error) { log_error(error); });
            dock_ = std::make_unique<dock_host>();
            dock_->set_report([report](const win32::taskbar_report& value) { report([&value](win32::window_delegate& delegate) { return delegate.make_taskbar_message(value); }); });
            dock_->set_jump_list_report(
                [report](const win32::jump_list_report& value) { report([&value](win32::window_delegate& delegate) { return delegate.make_jump_list_message(value); }); });
            hotkeys_ = std::make_unique<hotkey_host>();
            hotkeys_->set_report([report](const win32::hotkey_report& value) { report([&value](win32::window_delegate& delegate) { return delegate.make_hotkey_message(value); }); });
            // 눌림은 눌린 순간의 frame에 실린 같은 id의 것을 부른다.
            hotkeys_->set_pressed([this](const std::u8string& id) {
                if (host_ == nullptr)
                    return;
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame == nullptr)
                    return;
                for (const global_hotkey& hotkey : frame->hotkeys)
                    if (hotkey.id == id && hotkey.pressed != nullptr)
                    {
                        try
                        {
                            dispatch(hotkey.pressed());
                        }
                        catch (...)
                        {
                            log_error(u8"The global hotkey action threw an exception.");
                        }
                        return;
                    }
            });
        }

        void apply_shell(const ui_frame* const frame)
        {
            if (tray_ != nullptr)
                tray_->update(frame != nullptr ? frame->tray : std::nullopt, system_.high_contrast);
            // Dock 타일은 앱마다 하나라 주 창의 요청만 읽는다. 독립 창의 `ui_window::taskbar`는 받기만 한다.
            if (dock_ != nullptr)
                dock_->update(frame != nullptr ? frame->taskbar : taskbar_request {});
            apply_hotkeys(frame);
        }

        void apply_hotkeys(const ui_frame* const frame)
        {
            if (hotkeys_ == nullptr || host_ == nullptr)
                return;
            // 키 잡기 칸이 초점을 가진 동안은 건 조합을 풀어 그 칸이 키를 받게 한다 (Win32와 같다).
            const bool key_capture { frame != nullptr && frame->hotkeys.empty() == false && host_->acquire_interaction().key_capture };
            hotkeys_->update(frame != nullptr ? std::span<const global_hotkey> { frame->hotkeys } : std::span<const global_hotkey> {}, key_capture);
        }

    public:
        // Dock 아이콘의 메뉴다 (점프 목록). 작업이 없으면 nil이다.
        [[nodiscard]] NSMenu* dock_menu() const noexcept
        {
            return dock_ != nullptr ? dock_->dock_menu() : nil;
        }

    private:
        // 창마다 배경 효과를 세우거나 거둔다 (docs/backdrop-design.md의 macOS 갈래).
        //  - 효과는 창 내용 맨 아래의 `NSVisualEffectView`(창 뒤를 섞는다)다. mica는 창 바탕 재질, mica_alt는 창 아래 바탕 재질,
        //    acrylic은 떠 있는 판(HUD) 재질이다. Skia view는 그 위에 `backdrop_background`의 알파만큼 비치게 칠한다.
        //  - 투명도 줄이기와 대비 늘리기가 켜져 있으면 세우지 않고 그 까닭을 알린다.
        //  - popup 판은 활성화되지 않는 표면이라 acrylic만 선다 (Win32와 같다). 판의 상태는 알리지 않는다 (`ui_popup::backdrop`).
        void apply_backdrops(const ui_frame* const frame)
        {
            if (frame == nullptr)
                return;
            apply_backdrop(main_, frame->backdrop);
            for (std::unique_ptr<native_surface>& surface : secondary_)
                apply_backdrop(*surface, surface->want.backdrop);
            for (std::unique_ptr<native_surface>& popup : popups_)
                apply_backdrop(*popup, popup->popup->transparent ? window_backdrop::none : popup->popup->backdrop);
        }

        // 창 내용 그릇의 바탕이다. 비치는 창(`layered`·둥근 주 창)과 popup 판은 비워 두고 Skia가 바탕을 칠한다.
        [[nodiscard]] CGColorRef content_background(const native_surface& surface) const
        {
            if (surface.popup.has_value() || (surface.main() && main_translucent()))
                return [NSColor clearColor].CGColor;
            return [NSColor windowBackgroundColor].CGColor;
        }

        // 주 창이 불투명하지 않은가다. 투명 표시(`layered`)이거나 모서리를 둥글게 자른 창이다.
        [[nodiscard]] bool main_translucent() const noexcept
        {
            return config_.presentation == win32::window_presentation::layered || config_.corner_radius > 0.0f;
        }

        void apply_backdrop(native_surface& surface, const window_backdrop requested)
        {
            if (surface.window == nil)
                return;
            win32::backdrop_status status { win32::backdrop_status::off };
            if (requested != window_backdrop::none)
                status = surface.popup.has_value() && requested != window_backdrop::acrylic ? win32::backdrop_status::unsupported_presentation
                    : system_.transparency == false                                       ? win32::backdrop_status::transparency_disabled
                    : system_.high_contrast                                               ? win32::backdrop_status::high_contrast
                                                                                          : win32::backdrop_status::applied;
            const bool applied { status == win32::backdrop_status::applied };
            if (applied)
            {
                if (surface.effect == nil)
                {
                    surface.effect = [[NSVisualEffectView alloc] initWithFrame:surface.container.bounds];
                    surface.effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                    surface.effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
                    // popup 판은 키 창이 되지 않는다. 창 활성을 따르면 늘 꺼진 재질이다.
                    surface.effect.state = surface.popup.has_value() ? NSVisualEffectStateActive : NSVisualEffectStateFollowsWindowActiveState;
                    [surface.container addSubview:surface.effect positioned:NSWindowBelow relativeTo:nil];
                }
                surface.effect.material = requested == window_backdrop::acrylic ? NSVisualEffectMaterialHUDWindow
                    : requested == window_backdrop::mica_alt                    ? NSVisualEffectMaterialUnderWindowBackground
                                                                                : NSVisualEffectMaterialWindowBackground;
                surface.container.layer.backgroundColor = [NSColor clearColor].CGColor;
            }
            else if (surface.effect != nil)
            {
                [surface.effect removeFromSuperview];
                surface.effect = nil;
                surface.container.layer.backgroundColor = content_background(surface);
            }
            if (surface.backdrop_applied != applied)
            {
                surface.backdrop_applied = applied;
                surface.dirty = true;
            }
            // 상태가 바뀔 때 한 번 알린다. 보조 창은 효과를 고른 적이 없으면 알리지 않는다. popup 판은 알리지 않는다.
            const std::pair<window_backdrop, int> key { requested, static_cast<int>(status) };
            if (surface.popup.has_value() || surface.reported_backdrop == key || window_delegate_ == nullptr || host_ == nullptr)
                return;
            if (surface.main() == false && surface.reported_backdrop.has_value() == false && requested == window_backdrop::none)
                return;
            surface.reported_backdrop = key;
            if (app_message message { window_delegate_->make_backdrop_message(win32::backdrop_report { requested, status, surface.id }) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        [[nodiscard]] static std::vector<display_info> current_displays()
        {
            std::vector<display_info> displays {};
            const auto rect_of = [](const NSRect rect) {
                const screen_point top_left { to_screen_pixels(NSMakePoint(NSMinX(rect), NSMaxY(rect))) };
                const screen_point bottom_right { to_screen_pixels(NSMakePoint(NSMaxX(rect), NSMinY(rect))) };
                return screen_rect { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
            };
            NSArray<NSScreen*>* const screens { [NSScreen screens] };
            for (NSUInteger index { 0 }; index < screens.count; ++index)
            {
                NSScreen* const screen { screens[index] };
                NSNumber* const number { screen.deviceDescription[@"NSScreenNumber"] };
                display_info display {};
                display.id = from_ns_string([NSString stringWithFormat:@"display-%u", number != nil ? number.unsignedIntValue : static_cast<unsigned>(index)]);
                display.bounds = rect_of(screen.frame);
                display.work_area = rect_of(screen.visibleFrame);
                display.scale = static_cast<float>(screen.backingScaleFactor);
                display.primary = index == 0;
                displays.push_back(std::move(display));
            }
            return displays;
        }

        // frame의 도킹 표면 목록에 맞춘다 (docked-surface-design.md의 macOS 갈래). 새 id는 세우고, 빠진 id는 거두고, 있는 id는
        // tree·metrics를 바꿔 들고 자리를 다시 푼다.
        //  - 자리는 `resolve_docked_bounds`가 모니터 목록으로 푼 화면 사각형이다 (이 파일 첫머리의 화면 좌표 약속). 풀리지 않으면 숨긴다.
        //  - 판은 테두리·제목 막대·Dock 항목이 없고, 눌러도 앱을 앞으로 세우지 않는다. 키보드는 앱이 활성화를 청할 때만 받는다.
        //  - 주 창을 숨겨도 남는다 (`main_hidden`과 무관하다).
        void reconcile_docked(const ui_frame* const frame)
        {
            std::vector<const ui_docked_surface*> wanted {};
            if (frame != nullptr)
                for (const ui_docked_surface& docked : frame->docked_surfaces)
                    if (docked.id.empty() == false && std::ranges::none_of(wanted, [&docked](const ui_docked_surface* const other) { return other->id == docked.id; }))
                        wanted.push_back(&docked);
            std::erase_if(secondary_, [this, &wanted](std::unique_ptr<native_surface>& surface) {
                if (surface->docked == false || std::ranges::any_of(wanted, [&surface](const ui_docked_surface* const docked) { return docked->id == surface->id; }))
                    return false;
                destroy_window(*surface);
                return true;
            });
            if (wanted.empty())
                return;
            const std::vector<display_info> displays { current_displays() };
            for (const ui_docked_surface* const docked : wanted)
            {
                native_surface* surface { find_surface(docked->id) };
                if (surface != nullptr && surface->docked == false)
                    continue;
                const bool created { surface == nullptr };
                if (created)
                {
                    secondary_.push_back(std::make_unique<native_surface>());
                    surface = secondary_.back().get();
                    surface->id = docked->id;
                    surface->docked = true;
                    surface->want.independent = true;
                }
                surface->want.id = docked->id;
                surface->want.tree = docked->tree;
                surface->want.metrics = docked->metrics;
                if (created)
                {
                    create_window(*surface, NSMakeSize(1, 1), NSMakeSize(1, 1), make_ns_string(docked->title), false);
                    surface->router.set_touch_config(config_.touch);
                    create_renderer(*surface);
                    create_webviews(*surface);
                }
                if (NSString* const title { make_ns_string(docked->title) }; title != nil)
                    surface->window.title = title;
                place_docked(*surface, resolve_docked_bounds(docked->placement, displays));
                surface->dirty = true;
            }
        }

        // 도킹 표면 하나를 풀린 자리에 앉힌다. 같은 자리면 그대로 둔다.
        void place_docked(native_surface& surface, const std::optional<screen_rect> bounds)
        {
            if (bounds == surface.docked_bounds && surface.window.visible == (bounds.has_value() ? YES : NO))
                return;
            surface.docked_bounds = bounds;
            if (bounds.has_value() == false)
            {
                [surface.window orderOut:nil];
                return;
            }
            const CGFloat scale { primary_scale() };
            const NSPoint top_left { from_screen_pixels(bounds->left, bounds->top) };
            const NSSize size { std::max<CGFloat>(1, bounds->width() / scale), std::max<CGFloat>(1, bounds->height() / scale) };
            [surface.window setFrame:NSMakeRect(top_left.x, top_left.y - size.height, size.width, size.height) display:NO];
            [surface.window orderFrontRegardless];
            refresh_metrics(surface);
        }

        // 모니터 목록을 앱에 알린다 (`ui_frame::displays`). 처음 실린 frame에서 한 번, 그 뒤로는 화면 구성이 바뀔 때마다다.
        //  - id는 CoreGraphics 화면 번호다. 사각형은 이 파일 첫머리의 화면 좌표 약속을 따르고, 작업 영역은 메뉴 막대와 Dock을 뺀 곳이다.
        void report_displays(const ui_frame* const frame, const bool changed)
        {
            if (frame == nullptr || frame->displays == nullptr || host_ == nullptr || (displays_reported_ && changed == false))
                return;
            displays_reported_ = true;
            if (app_message message { frame->displays(current_displays()) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

    public:
        // 화면이 붙거나 떨어졌거나, 해상도·배율·작업 영역이 바뀌었다.
        void on_screens_changed()
        {
            if (host_ == nullptr)
                return;
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            report_displays(frame.get(), true);
            report_placement(main_);
            // 도킹 표면의 자리는 모니터에 달렸다. 같은 요청으로 다시 푼다.
            reconcile_docked(frame.get());
            pump();
        }

    private:
        // frame의 보조 창 목록에 맞춘다. 새 id는 세우고, 빠진 id는 거두고, 있는 id는 tree와 훅을 바꿔 든다.
        //  - 소유 여부(`independent`)는 만들 때만 본다 (app_host.h의 약속).
        void reconcile_windows(const ui_frame* const frame)
        {
            std::vector<const ui_window*> wanted {};
            if (frame != nullptr)
                for (const ui_window& window : frame->windows)
                    if (window.id.empty() == false && window.width > 0.0f && window.height > 0.0f)
                        wanted.push_back(&window);
            std::erase_if(secondary_, [this, &wanted](std::unique_ptr<native_surface>& surface) {
                if (surface->docked)
                    return false;
                if (std::ranges::any_of(wanted, [&surface](const ui_window* const window) { return window->id == surface->id; }))
                    return false;
                destroy_window(*surface);
                return true;
            });
            for (const ui_window* const window : wanted)
            {
                native_surface* surface { find_surface(window->id) };
                if (surface == nullptr)
                {
                    secondary_.push_back(std::make_unique<native_surface>());
                    surface = secondary_.back().get();
                    surface->id = window->id;
                    surface->want = *window;
                    open_window(*surface);
                }
                else
                {
                    const bool independent { surface->want.independent };
                    surface->want = *window;
                    surface->want.independent = independent;
                    if (NSString* const title { make_ns_string(window->caption.title) }; title != nil && [title isEqualToString:surface->window.title] == NO)
                        surface->window.title = title;
                    surface->window.contentMinSize = NSMakeSize(window->minimum_width, window->minimum_height);
                }
                surface->dirty = true;
            }
            reconcile_docked(frame);
        }

        // 보조 창 하나를 세운다. 자리는 저장 배치 → 화면 닻 → 주 창 기준 자리의 차례로 정한다 (app_host.h의 약속).
        void open_window(native_surface& surface)
        {
            const ui_window& want { surface.want };
            NSSize content { want.width, want.height };
            const CGFloat scale { primary_scale() };
            std::optional<NSPoint> top_left {};
            if (want.initial_placement.has_value() && want.initial_placement->valid())
            {
                const window_placement& placement { *want.initial_placement };
                content = NSMakeSize(placement.width / scale, placement.height / scale);
                top_left = from_screen_pixels(placement.x, placement.y);
            }
            else if (want.screen_anchor.has_value())
            {
                const NSPoint anchor { from_screen_pixels(want.screen_anchor->screen.x, want.screen_anchor->screen.y) };
                top_left = NSMakePoint(anchor.x - want.screen_anchor->client_x, anchor.y + want.screen_anchor->client_y);
            }
            else if (main_.window != nil)
            {
                const NSRect main_content { [main_.window contentRectForFrameRect:main_.window.frame] };
                top_left = NSMakePoint(NSMinX(main_content) + want.x, NSMaxY(main_content) - want.y);
            }
            create_window(surface, content, NSMakeSize(want.minimum_width, want.minimum_height), make_ns_string(want.caption.title), want.independent == false);
            surface.router.set_touch_config(config_.touch);
            if (top_left.has_value())
            {
                const NSRect content_rect { NSMakeRect(top_left->x, top_left->y - content.height, content.width, content.height) };
                NSRect frame { [surface.window frameRectForContentRect:content_rect] };
                // 화면 밖으로 나가지 않게 그 화면의 보이는 자리 안으로 민다 (다른 초기 배치와 같은 규칙).
                if (NSScreen* const screen { screen_for(frame) }; screen != nil)
                {
                    const NSRect visible { screen.visibleFrame };
                    frame.origin.x = std::clamp(frame.origin.x, NSMinX(visible), std::max(NSMinX(visible), NSMaxX(visible) - frame.size.width));
                    frame.origin.y = std::clamp(frame.origin.y, NSMinY(visible), std::max(NSMinY(visible), NSMaxY(visible) - frame.size.height));
                }
                [surface.window setFrame:frame display:NO];
            }
            if (want.initial_placement.has_value() && want.initial_placement->maximized)
                [surface.window zoom:nil];
            read_surface_size(surface);
            create_renderer(surface);
            create_webviews(surface);
            if (main_hidden_ == false || want.independent)
                [surface.window makeKeyAndOrderFront:nil];
            post_metrics(surface);
            report_placement(surface);
        }

        // 그 사각형이 가장 많이 걸친 화면이다. 없으면 주 화면이다.
        [[nodiscard]] static NSScreen* screen_for(const NSRect frame)
        {
            NSScreen* best { [NSScreen mainScreen] };
            CGFloat area { 0 };
            for (NSScreen* const screen in [NSScreen screens])
            {
                const NSRect overlap { NSIntersectionRect(screen.frame, frame) };
                if (const CGFloat value { overlap.size.width * overlap.size.height }; value > area)
                {
                    area = value;
                    best = screen;
                }
            }
            return best;
        }

        void destroy_window(native_surface& surface)
        {
            // 웹뷰 사건과 접근성 질문이 사라지는 창으로 가지 않게 먼저 거둔다.
            if (surface.accessibility != nullptr)
                surface.accessibility->detach();
            surface.accessibility.reset();
            if (surface.webviews != nullptr)
                surface.webviews->shutdown();
            surface.webviews.reset();
            surface.renderer.reset();
            if (surface.window == nil)
                return;
            surface.view.owner = nullptr;
            surface.view.surface = nullptr;
            surface.delegate.owner = nullptr;
            surface.delegate.surface = nullptr;
            surface.window.delegate = nil;
            [surface.window orderOut:nil];
            [surface.window close];
            surface.window = nil;
            surface.view = nil;
            surface.container = nil;
            surface.delegate = nil;
        }

        // 앱이 주 창을 숨기자고 했는가를 창에 옮긴다 (`ui_frame::main_hidden`). 소유된 보조 창도 함께 숨는다.
        void apply_main_visibility(const bool hidden)
        {
            if (hidden == main_hidden_ || config_.smoke_test || main_.window == nil)
                return;
            main_hidden_ = hidden;
            if (hidden)
            {
                [main_.window orderOut:nil];
                for (std::unique_ptr<native_surface>& surface : secondary_)
                    if (surface->want.independent == false)
                        [surface->window orderOut:nil];
                return;
            }
            [main_.window makeKeyAndOrderFront:nil];
            for (std::unique_ptr<native_surface>& surface : secondary_)
                if (surface->want.independent == false)
                    [surface->window orderFront:nil];
        }

        // --- popup 판 (popup-overlay-design.md의 macOS 갈래) ---
        //  - popup마다 테두리 없는 판(`LuilPopupPanel`) 하나다. 앵커 표면 창의 자식 창이라 그 위에 서고, 함께 움직이고 함께 숨는다.
        //  - 자리는 앵커 표면 내용 기준 그대로 화면에 선다 (`place_popup_on_screen`, Win32 `popup_screen_bounds`와 같은 규칙). 창 밖으로
        //    나가고, 그 화면의 작업 영역 안으로만 민다. 작업 영역보다 긴 판은 그 높이로 줄고 휠로 흘린다.
        //  - 판은 앵커 표면의 배율을 쓴다. 판의 tree는 앱이 앵커의 배율로 배치했다 (Win32와 같다).
        //  - 키 창이 되지 않고 눌러도 앱을 활성화하지 않는다. 키보드는 앵커 쪽 키 창이 받고 논리 초점이 popup으로 보낸다
        //    (popup-ime-design.md와 같은 길이다).

        // popup이 붙는 표면이다. 창(주 창·보조 창·도킹 표면)이거나 먼저 선 popup 판이다.
        [[nodiscard]] native_surface* anchor_surface(const std::u8string& anchor)
        {
            if (native_surface* const surface { find_surface(anchor) }; surface != nullptr)
                return surface;
            return find_popup(anchor);
        }

        [[nodiscard]] static popup_shape popup_shape_for(const ui_frame& frame, const ui_popup& popup) noexcept
        {
            const ui_style& style { frame.style != nullptr ? *frame.style : default_ui_style() };
            if (popup.transparent)
                return { .drop_shadow = false, .line = false };
            return resolve_popup_shape(popup.corner.value_or(style.popup.corner), popup.border, true);
        }

        // frame의 popup 목록에 판을 맞춘다. 빠진 id와 앵커·투명이 바뀐 id는 거두고, 새 id는 세우고, 있는 id는 tree와 훅을 바꿔 든다.
        //  - 앵커가 아직 없는 popup은 세우지 않는다 (`ui_popup::anchor`). 부모가 먼저인 차례로 세운다.
        //  - 거둔 popup의 앵커가 사라졌으면 그 popup에 `anchor_closed`를 묻는다.
        void update_popups(const ui_frame* const frame)
        {
            std::vector<const ui_popup*> wanted {};
            if (frame != nullptr)
            {
                std::vector<popup_placement> logical {};
                for (const ui_popup& popup : frame->popups)
                    logical.push_back({ popup.id, 0, 0, popup.tree != nullptr && popup.width > 0.0f ? 1 : 0, popup.height > 0.0f ? 1 : 0, popup.anchor });
                std::vector<std::u8string> roots { u8"" };
                for (const std::unique_ptr<native_surface>& surface : secondary_)
                    roots.push_back(surface->id);
                for (const std::size_t index : order_popup_placements(logical, roots))
                    wanted.push_back(&frame->popups[index]);
            }
            const auto wanted_index = [&wanted](const std::u8string& id) {
                const auto found { std::ranges::find_if(wanted, [&id](const ui_popup* const popup) { return popup->id == id; }) };
                return static_cast<std::size_t>(found - wanted.begin());
            };
            std::vector<std::unique_ptr<native_surface>> removed {};
            for (auto position { popups_.begin() }; position != popups_.end();)
            {
                const std::size_t index { wanted_index((*position)->id) };
                const bool keep { index < wanted.size() && wanted[index]->anchor == (*position)->popup->anchor && wanted[index]->transparent == (*position)->popup->transparent };
                if (keep)
                {
                    ++position;
                    continue;
                }
                removed.push_back(std::move(*position));
                position = popups_.erase(position);
            }
            for (const ui_popup* const want : wanted)
            {
                native_surface* const anchor { anchor_surface(want->anchor) };
                if (anchor == nullptr || anchor->window == nil)
                    continue;
                native_surface* popup { find_popup(want->id) };
                const bool created { popup == nullptr };
                if (created)
                {
                    popups_.push_back(std::make_unique<native_surface>());
                    popup = popups_.back().get();
                    popup->id = want->id;
                    popup->popup = popup_state {};
                    popup->popup->anchor = want->anchor;
                    popup->popup->transparent = want->transparent;
                }
                popup_state& state { *popup->popup };
                state.x = want->x;
                state.y = want->y;
                state.width = want->width;
                state.height = want->height;
                state.shape = popup_shape_for(*frame, *want);
                state.backdrop = want->backdrop;
                // 새 frame이 popup을 계속 실었다는 것은 앱이 상태를 유지하기로 했다는 뜻이다. 닫힘 빗장을 푼다.
                state.dismiss = want->dismiss;
                state.requested = false;
                if (want->placed == nullptr)
                    state.reported_bounds.reset();
                state.placed = want->placed;
                popup->want.id = want->id;
                popup->want.tree = want->tree;
                popup->dirty = true;
                if (created)
                    create_popup(*popup, *anchor);
            }
            // 부모가 먼저인 차례로 둔다. 자리를 잡을 때 앵커 판의 자리가 먼저 서 있어야 한다.
            std::ranges::stable_sort(popups_, {}, [&wanted_index](const std::unique_ptr<native_surface>& popup) { return wanted_index(popup->id); });
            for (std::unique_ptr<native_surface>& gone : removed)
            {
                popup_state& state { *gone->popup };
                if (state.anchor.empty() == false && anchor_surface(state.anchor) == nullptr)
                    if (auto action { take_popup_dismiss_action(state.dismiss, state.requested, popup_dismiss_reason::anchor_closed) }; action.has_value())
                        dispatch(std::move(*action));
            }
            // 자식 판부터 거둔다.
            for (auto position { removed.rbegin() }; position != removed.rend(); ++position)
                destroy_popup(**position);
            place_popups();
        }

        void create_popup(native_surface& popup, const native_surface& anchor)
        {
            LuilPopupPanel* const panel { [[LuilPopupPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                                                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                                              backing:NSBackingStoreBuffered
                                                                                defer:NO] };
            panel.releasedWhenClosed = NO;
            panel.floatingPanel = NO;
            panel.hidesOnDeactivate = NO;
            panel.becomesKeyOnlyIfNeeded = YES;
            panel.worksWhenModal = YES;
            panel.opaque = NO;
            panel.backgroundColor = [NSColor clearColor];
            // 그림자는 판이 그린 모양(둥근 모서리)을 따른다. 투명 popup은 모양을 스스로 그리므로 그림자도 없다 (Win32와 같다).
            panel.hasShadow = popup.popup->transparent == false;
            panel.acceptsMouseMovedEvents = YES;
            panel.collectionBehavior = NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
            panel.title = make_ns_string(popup.id) ?: @"";
            popup.window = panel;
            popup.scale = anchor.scale;
            install_content(popup, NSMakeRect(0, 0, 1, 1));
            popup.router.set_touch_config(config_.touch);
            create_renderer(popup);
        }

        void destroy_popup(native_surface& popup)
        {
            if (popup.window != nil && popup.window.parentWindow != nil)
                [popup.window.parentWindow removeChildWindow:popup.window];
            destroy_window(popup);
        }

        void place_popups()
        {
            for (std::unique_ptr<native_surface>& popup : popups_)
                place_popup(*popup);
        }

        // popup 판 하나를 앵커 기준 자리에서 화면 자리로 옮겨 앉힌다. 앵커 창이 보이지 않으면 판도 숨긴다.
        void place_popup(native_surface& popup)
        {
            popup_state& state { *popup.popup };
            native_surface* const anchor { anchor_surface(state.anchor) };
            if (anchor == nullptr || anchor->window == nil || popup.window == nil)
                return;
            NSWindow* const panel { popup.window };
            const CGFloat primary { primary_scale() };
            // 앵커 내용의 왼쪽 위다 (화면 픽셀). 앵커가 흘려 보이는 판이면 그 안의 자리는 흘린 만큼 올라가 있다.
            const NSRect content { [anchor->window contentRectForFrameRect:anchor->window.frame] };
            screen_point origin { to_screen_pixels(NSMakePoint(NSMinX(content), NSMaxY(content))) };
            const bool anchor_popup { anchor->popup.has_value() };
            if (anchor_popup)
                origin.y -= static_cast<int>(std::lround(anchor->popup->scroll_y / anchor->scale * primary));
            const auto pixels = [primary](const float points) { return static_cast<int>(std::lround(points * primary)); };
            const int width { std::max(1, pixels(state.width)) };
            const int height { std::max(1, pixels(state.height)) };
            const int offset_x { pixels(state.x) };
            const int offset_y { pixels(state.y) };
            const NSPoint desired { from_screen_pixels(origin.x + offset_x, origin.y + offset_y) };
            NSScreen* const screen { screen_for(NSMakeRect(desired.x, desired.y - height / primary, width / primary, height / primary)) };
            const screen_rect visible { window_bounds(screen != nil ? screen.visibleFrame : content) };
            const popup_screen_frame placed { place_popup_on_screen(origin, offset_x, offset_y, width, height, { visible.left, visible.top, visible.right, visible.bottom }, anchor_popup) };
            const NSPoint top_left { from_screen_pixels(placed.x, placed.y) };
            const NSRect frame { NSMakeRect(top_left.x, top_left.y - placed.height / primary, placed.width / primary, placed.height / primary) };
            if (NSEqualRects(panel.frame, frame) == NO)
                [panel setFrame:frame display:NO];

            // 판의 그림은 앵커의 배율로 그린다. 보이는 높이가 내용보다 짧으면 흘린다.
            popup.scale = anchor->scale;
            const int physical_width { std::max(1, static_cast<int>(std::lround(state.width * popup.scale))) };
            const int physical_height { std::max(1, static_cast<int>(std::lround(placed.height / primary * popup.scale))) };
            const float content_height { static_cast<float>(std::lround(state.height * popup.scale)) };
            state.max_scroll_y = std::max(0.0f, content_height - static_cast<float>(physical_height));
            state.scroll_y = std::clamp(state.scroll_y, 0.0f, state.max_scroll_y);
            if (physical_width != popup.width || physical_height != popup.height)
            {
                popup.width = physical_width;
                popup.height = physical_height;
                popup.shadow_stale = true;
                if (popup.renderer != nullptr)
                {
                    std::u8string error {};
                    if (popup.renderer->resize(popup.width, popup.height, error) == false)
                        log_error(error);
                }
            }
            const CGFloat radius { state.shape.rounded() ? popup_corner_logical_radius(state.shape.corner) : 0.0 };
            if (popup.container.layer.cornerRadius != radius)
            {
                popup.container.layer.cornerRadius = radius;
                popup.container.layer.masksToBounds = radius > 0 ? YES : NO;
                popup.shadow_stale = true;
            }
            popup.router.set_areas({ overlay_area { popup.id, { 0.0f, 0.0f, static_cast<float>(popup.width), static_cast<float>(popup.height) }, state.scroll_y,
                state.max_scroll_y, popup.want.tree.get(), popup.scale } });

            // 앵커 창의 자식 창으로 그 위에 선다. 앵커 창이 숨었으면 판도 숨긴다.
            panel.level = anchor->window.level;
            if (anchor->window.visible)
            {
                if (panel.parentWindow != anchor->window)
                {
                    if (panel.parentWindow != nil)
                        [panel.parentWindow removeChildWindow:panel];
                    [anchor->window addChildWindow:panel ordered:NSWindowAbove];
                }
                if (panel.visible == NO)
                    [panel orderFront:nil];
            }
            else if (panel.visible)
                [panel orderOut:nil];

            // 작업 영역에 맞춘 최종 자리를 앵커 기준 논리 픽셀로 알린다 (`ui_popup::placed`).
            if (state.placed != nullptr && host_ != nullptr)
            {
                const rect_f bounds { (placed.x - origin.x) / static_cast<float>(primary), (placed.y - origin.y) / static_cast<float>(primary),
                    placed.width / static_cast<float>(primary), placed.height / static_cast<float>(primary) };
                if (state.reported_bounds != bounds)
                {
                    state.reported_bounds = bounds;
                    if (app_message message { state.placed(bounds) }; message.empty() == false)
                        host_->post_app_message(std::move(message));
                }
            }
            popup.dirty = true;
        }

        // 판의 router가 흘리자고 했다. 흘린 판에 붙은 자식 판도 따라 옮긴다.
        void scroll_popup(native_surface& surface, const overlay_scroll& scroll)
        {
            if (surface.popup.has_value() == false || scroll.surface != surface.id || surface.popup->scroll_y == scroll.offset)
                return;
            surface.popup->scroll_y = scroll.offset;
            place_popups();
            surface.dirty = true;
        }

        [[nodiscard]] native_surface* find_surface(const std::u8string& id)
        {
            if (id.empty())
                return &main_;
            const auto found { std::ranges::find_if(secondary_, [&id](const std::unique_ptr<native_surface>& surface) { return surface->id == id; }) };
            return found != secondary_.end() ? found->get() : nullptr;
        }

        [[nodiscard]] const native_surface* find_surface(const std::u8string& id) const
        {
            return const_cast<application*>(this)->find_surface(id);
        }

        [[nodiscard]] native_surface* find_popup(const std::u8string& id)
        {
            if (id.empty())
                return nullptr;
            const auto found { std::ranges::find_if(popups_, [&id](const std::unique_ptr<native_surface>& popup) { return popup->id == id; }) };
            return found != popups_.end() ? found->get() : nullptr;
        }

        [[nodiscard]] const native_surface* find_popup(const std::u8string& id) const
        {
            return const_cast<application*>(this)->find_popup(id);
        }

        // 닫힘 계기 하나를 popup들에 묻는다. 하나라도 닫자는 메시지를 냈으면 참이다.
        //  - Esc는 맨 위의 자식 popup 하나만 닫는다 (Android·Win32와 같다).
        bool dismiss_popups(const popup_dismiss_reason reason)
        {
            if (reason == popup_dismiss_reason::escape_key)
                for (auto position { popups_.rbegin() }; position != popups_.rend(); ++position)
                {
                    popup_state& state { *(*position)->popup };
                    if (state.anchor.empty() || find_surface(state.anchor) != nullptr)
                        continue;
                    if (auto action { take_popup_dismiss_action(state.dismiss, state.requested, reason) }; action.has_value())
                    {
                        dispatch(std::move(*action));
                        return true;
                    }
                    return false;
                }
            bool dismissed { false };
            for (std::unique_ptr<native_surface>& popup : popups_)
                if (std::optional<input_action> action { take_popup_dismiss_action(popup->popup->dismiss, popup->popup->requested, reason) }; action.has_value())
                {
                    dispatch(std::move(*action));
                    dismissed = true;
                }
            return dismissed;
        }

        // 초점을 가진 텍스트 칸과 그것이 사는 tree, 그 tree의 표면 원점(물리 픽셀), 그 표면의 창이다.
        struct text_location
        {
            std::shared_ptr<const ui_tree> tree {};
            const ui_element* element { nullptr };
            float origin_x { 0.0f };
            float origin_y { 0.0f };
            const native_surface* surface { nullptr };
        };

        [[nodiscard]] text_location focused_text_location() const
        {
            text_location location {};
            if (host_ == nullptr || focused_text_target().has_value() == false)
                return location;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            if (interaction.focused_surface.empty())
            {
                location.tree = host_->acquire_ui_tree();
                location.surface = &main_;
            }
            else if (const native_surface* const surface { find_surface(interaction.focused_surface) }; surface != nullptr)
            {
                location.tree = surface->want.tree;
                location.surface = surface;
            }
            else if (const native_surface* const popup { find_popup(interaction.focused_surface) }; popup != nullptr)
            {
                // popup 판의 tree는 흘린 만큼 위로 올라가 있다. 후보 창은 판의 view 좌표로 선다.
                location.tree = popup->want.tree;
                location.origin_y = -popup->popup->scroll_y;
                location.surface = popup;
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
            else if (const auto* const shot { std::get_if<capture_request>(&action) }; shot != nullptr)
                execute_capture(*shot);
            else if (const auto* const record { std::get_if<record_request>(&action) }; record != nullptr)
                execute_record(*record);
        }

        // 클립보드 요청이다 (macos/macos_clipboard.h). 텍스트 칸의 붙여넣기는 그 칸의 insert 편집으로 보낸다.
        //  - 넣은 뒤의 번호를 기억한다. 번호가 그대로면 클립보드의 임자가 이 앱이다 (`clipboard_change::owned`).
        void execute_clipboard(const clipboard_request& request)
        {
            if (const auto* const files { std::get_if<clipboard_files_copy_request>(&request) }; files != nullptr)
            {
                if (write_clipboard_files(*files))
                    note_clipboard_written();
                return;
            }
            if (const auto* const read { std::get_if<clipboard_files_paste_request>(&request) }; read != nullptr)
            {
                if (environment_.delegate == nullptr || host_ == nullptr)
                    return;
                if (app_message message { environment_.delegate->make_file_clipboard_message(read_clipboard_files(read->tag)) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (const auto* const read { std::get_if<clipboard_data_paste_request>(&request) }; read != nullptr)
            {
                if (environment_.delegate == nullptr || host_ == nullptr)
                    return;
                if (app_message message { environment_.delegate->make_clipboard_data_message(read_clipboard_data(read->tag)) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (const auto* const copy { std::get_if<clipboard_copy_request>(&request) }; copy != nullptr)
            {
                NSPasteboard* const board { [NSPasteboard generalPasteboard] };
                [board clearContents];
                if (NSString* const text { make_ns_string(copy->text) }; text != nil)
                    [board setString:text forType:NSPasteboardTypeString];
                note_clipboard_written();
                return;
            }
            if (const auto* const data { std::get_if<clipboard_data_copy_request>(&request) }; data != nullptr)
            {
                std::u8string error {};
                if (write_clipboard_data(*data, error))
                    note_clipboard_written();
                else if (error.empty() == false)
                    log_error(error);
                return;
            }
            const auto* const paste { std::get_if<clipboard_paste_request>(&request) };
            if (paste == nullptr || environment_.policy == nullptr)
                return;
            std::u8string text { from_ns_string([[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString]) };
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

        void note_clipboard_written()
        {
            owned_clipboard_sequence_ = clipboard_sequence();
            check_clipboard();
        }

    public:
        // 클립보드가 바뀌었으면 앱에 알린다 (`clipboard_change`). macOS에는 바뀜 알림이 없어 번호(`changeCount`)를 견준다.
        //  - 이 앱이 넣은 뒤, 앱이 앞으로 올 때, 앞에 있는 동안 반 초마다 본다. 뒤에 있는 동안은 다른 앱만 넣을 수 있고, 앞으로
        //    오면 바로 본다.
        void check_clipboard()
        {
            if (host_ == nullptr || environment_.delegate == nullptr || stopped_)
                return;
            const std::uint32_t sequence { clipboard_sequence() };
            if (reported_clipboard_sequence_ == sequence)
                return;
            reported_clipboard_sequence_ = sequence;
            const clipboard_change change { owned_clipboard_sequence_ == sequence, sequence, clipboard_has_files(), clipboard_has_image() };
            if (app_message message { environment_.delegate->make_clipboard_changed_message(change) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

    private:

        // frame의 웹뷰마다 자리표의 자리를 웹뷰에 앉히고, 아래층이 비울 구멍을 낸다 (Win32 `apply_webviews`와 같은 규칙).
        //  - 가림은 자리표의 가운데와 안쪽 네 모서리 hit test다.
        //  - 초점이 자리표에 **닿는 순간** 키보드 초점을 페이지로 넘긴다.
        [[nodiscard]] std::span<const pixel_rect> apply_webviews(native_surface& surface, const ui_tree* const tree, const interaction_snapshot& interaction)
        {
            surface.webview_holes.clear();
            if (surface.webviews == nullptr || reconciled_frame_ == nullptr)
                return surface.webview_holes;
            const ui_element_id focused { interaction_for_surface(interaction, surface.id).focused };
            for (const ui_webview& want : reconciled_frame_->webviews)
            {
                if (want.anchor != surface.id || want.id.empty())
                    continue;
                const ui_element_id placeholder_id { ui_element_kind::webview, want.id };
                const auto* const placeholder { tree != nullptr ? dynamic_cast<const webview_element*>(tree->find(placeholder_id)) : nullptr };
                const std::optional<rect_f> visible { placeholder != nullptr ? tree->visible_bounds(*placeholder) : std::nullopt };
                const float tree_scale { placeholder != nullptr ? placeholder->scale() : surface.scale };
                const webview_layout layout {
                    plan_webview_layout(visible, surface.width, surface.height, tree != nullptr && webview_occluded(*tree, placeholder, visible), tree_scale, want.layer),
                };
                // 다른 배율로 배치된 tree(화면을 옮긴 직후)의 자리는 주지 않는다. 새 배율의 tree가 오면 크기와 배율을 함께 준다.
                const bool arranged_for_surface { std::abs(tree_scale - surface.scale) < 0.001f };
                if (arranged_for_surface || placeholder == nullptr)
                    surface.webviews->apply_layout(want.id, layout, surface.scale);
                if (surface.last_webview_focus != placeholder_id && focused == placeholder_id)
                    static_cast<void>(surface.webviews->move_focus_in(want.id));
                if (layout.punch_hole() && surface.webviews->opens_hole(want.id))
                    surface.webview_holes.push_back(pixel_rect { layout.x, layout.y, layout.width, layout.height });
            }
            surface.last_webview_focus = focused;
            return surface.webview_holes;
        }

        // popup은 따로 선 판이라 웹뷰를 가리지 않는다 (판이 웹뷰 창 위에 선다).
        [[nodiscard]] static bool webview_occluded(const ui_tree& tree, const ui_element* const placeholder, const std::optional<rect_f>& visible)
        {
            if (placeholder == nullptr || visible.has_value() == false)
                return true;
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

        // 한 표면을 그릴 상태다. `state`가 가리키는 tree·popup 층·글꼴을 함께 쥔다.
        struct prepared_frame
        {
            frame_state state {};
            std::shared_ptr<const ui_frame> frame {};
            std::shared_ptr<const ui_tree> tree {};
            sk_sp<SkTypeface> code {};
        };

        // 그리기와 남기기가 같은 상태를 짓는다. 웹뷰의 자리 맞춤과 구멍은 화면 그리기에서만 한다 (`with_webviews`).
        void prepare(native_surface& surface, prepared_frame& prepared, const bool with_webviews)
        {
            frame_state& state { prepared.state };
            state.width = surface.width;
            state.height = surface.height;
            state.dpi_scale = surface.scale;
            state.reduce_motion = system_.reduce_motion;

            prepared.frame = host_->acquire_frame();
            const ui_frame* const frame { prepared.frame.get() };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            state.interaction = interaction_for_surface(interaction, surface.id);
            // 보조 창·popup 판은 frame의 그 tree를, 주 창은 host가 준 tree를 그린다.
            prepared.tree = surface.main() ? host_->acquire_ui_tree() : surface.want.tree;
            state.tree = prepared.tree.get();
            // 창 상태다. caption이 확대 글리프를 고른다 (Win32 `caption_surface::prepare_frame`).
            state.maximized = is_zoomed(surface);
            state.fullscreen = surface.window != nil && (surface.window.styleMask & NSWindowStyleMaskFullScreen) != 0;
            if (state.fullscreen)
                state.maximized = false;
            // popup 판이다 (Win32 `popup_surface::prepare_frame`). 직각 판은 표면이 1px 선을 긋고, 흘린 만큼 tree를 올려 그린다.
            if (surface.popup.has_value())
            {
                state.popup = true;
                state.border = surface.popup->shape.line;
                state.transparent = surface.popup->transparent;
                state.origin_y = -static_cast<int>(std::lround(surface.popup->scroll_y));
            }
            // 투명 표시의 주 창은 바탕을 비운다. 그린 알파대로 뒤가 비친다.
            else if (surface.main() && config_.presentation == win32::window_presentation::layered)
                state.transparent = true;
            if (with_webviews)
                state.holes = apply_webviews(surface, prepared.tree.get(), interaction);
            const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
            state.theme = resolve_color_theme(appearance.theme, system_.high_contrast, system_.prefers_light);
            state.high_contrast = system_.contrast_colors;
            state.accent_id = appearance.accent_id;
            state.style = frame != nullptr ? frame->style.get() : nullptr;
            state.inspector = frame != nullptr && surface.main() ? &frame->inspector : nullptr;
            state.backdrop = surface.backdrop_applied;
            set_single_line_break(frame_style(state).text.line_break);

            const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
            set_configured_fonts(fonts.ui_family, fonts.code_family);
            prepared.code = configured_code_typeface();
            state.code_typeface = prepared.code.get();
            state.fonts = &font_resolver_;
        }

        // 둥근 popup 판의 테두리다. 시스템이 긋는 테두리가 없어 판 그릇 layer의 1px 선을 팔레트의 `tooltip_border`로 긋는다
        // (Win32의 DWM 테두리 색과 같은 값). 직각 판은 표면의 선이 경계라 긋지 않는다.
        void apply_popup_border(native_surface& surface, const frame_state& state)
        {
            if (surface.popup.has_value() == false || surface.container == nil)
                return;
            popup_state& popup { *surface.popup };
            const std::optional<ui_color> color { popup.shape.system_border ? std::optional<ui_color> { frame_palette(state).tooltip_border } : std::nullopt };
            if (popup.border_color == color)
                return;
            popup.border_color = color;
            CALayer* const layer { surface.container.layer };
            if (color.has_value() == false)
            {
                layer.borderWidth = 0;
                return;
            }
            const auto channel = [value = *color](const int shift) { return static_cast<CGFloat>((value >> shift) & 0xFFu) / 255.0; };
            layer.borderColor = [NSColor colorWithSRGBRed:channel(16) green:channel(8) blue:channel(0) alpha:channel(24)].CGColor;
            layer.borderWidth = 1.0 / std::max(1.0f, surface.scale);
        }

        [[nodiscard]] bool render(native_surface& surface)
        {
            prepared_frame prepared {};
            prepare(surface, prepared, true);
            update_accessibility(surface, prepared.tree);
            const frame_state& state { prepared.state };
            const std::shared_ptr<const ui_tree>& tree { prepared.tree };
            apply_popup_border(surface, state);

            std::u8string error {};
            const bool drawn { surface.renderer->render(state, error) };
            if (drawn == false || error.empty() == false)
                log_error(error);
            report_renderer(surface);
            // 녹화 중이면 같은 상태를 한 장 더 태워 모은다.
            if (drawn && surface.recorder.recording())
                surface.recorder.collect(state);
            // 비치는 창의 그림자는 그린 알파의 모양이다. 크기나 모양이 바뀐 뒤 처음 그린 장으로 다시 잰다.
            if (drawn && surface.shadow_stale && surface.window != nil && surface.window.opaque == NO)
            {
                surface.shadow_stale = false;
                [surface.window invalidateShadow];
            }
            // GPU를 요구한 모드는 물러서지 않는다. 그릴 길이 없으므로 끝낸다 (Android와 같다).
            if (drawn == false && renderer_mode_requires_gpu(config_.renderer) && quitting_ == false)
                quit();
            surface.dirty = false;

            // 시간이 흘러야 바뀌는 그림(애니메이션·tooltip 지연·caret 깜빡임)의 다음 시각이다.
            const auto time { now() };
            const update_context update { .now = time, .reduce_motion = system_.reduce_motion };
            surface.update_deadline = tree != nullptr ? tree->next_update(update, state.interaction) : std::nullopt;
            if (surface.update_deadline.has_value() && *surface.update_deadline <= time)
                surface.update_deadline = time + continuous_repaint_interval;
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
        native_surface main_ {};
        // 보조 창들이다. 주소가 변하지 않게 따로 잡는다 (view와 delegate가 그 주소를 쥔다).
        std::vector<std::unique_ptr<native_surface>> secondary_ {};
        // popup 판들이다. 부모가 먼저인 차례다.
        std::vector<std::unique_ptr<native_surface>> popups_ {};
        LuilApplicationDelegate* application_delegate_ { nil };
        NSTimer* timer_ { nil };
        std::optional<std::chrono::steady_clock::time_point> timer_deadline_ {};
        std::shared_ptr<const ui_frame> reconciled_frame_ {};
        file_dialog_gate file_dialog_gate_ {};
        // 지금 들어와 있는 OS 끌기의 내용이다 (들어올 때 한 번 읽는다).
        drag_payload drag_payload_ {};
        notification_queue notification_events_ {};
        std::unique_ptr<tray_icon_host> tray_ {};
        std::unique_ptr<dock_host> dock_ {};
        std::unique_ptr<hotkey_host> hotkeys_ {};
        // 이 앱이 마지막으로 넣은 클립보드 번호와 마지막으로 알린 번호다.
        std::optional<std::uint32_t> owned_clipboard_sequence_ {};
        std::optional<std::uint32_t> reported_clipboard_sequence_ {};
        NSTimer* clipboard_timer_ { nil };
        // 앱 delegate가 Windows의 창 delegate도 구현했으면 그것이다. 플랫폼 중립 밖의 훅(파일 놓기의 물러섬 등)을 그리로 부른다.
        win32::window_delegate* window_delegate_ { nullptr };
        focus_request_tracker focus_tracker_ {};
        registry_font_resolver font_resolver_ {};
        system_appearance system_ {};
        system_appearance_observer appearance_observer_ {};
        std::optional<composition_state> composition_ {};
        // `interpretKeyEvents:`가 도는 동안의 키와 그 창이다. 입력기가 먹었는지(`key_handled_`)를 함께 본다.
        std::optional<key_input> current_key_ {};
        native_surface* key_surface_ { nullptr };
        bool key_handled_ { false };
        // 주 창 배치다. 마지막으로 옮긴 요청의 revision, 마지막 보통 상태의 사각형, 전체 화면에 들기 전의 확대 여부다.
        std::uint64_t applied_placement_revision_ { 0 };
        std::optional<window_placement> normal_placement_ {};
        bool zoomed_before_fullscreen_ { false };
        bool fullscreen_transition_ { false };
        bool displays_reported_ { false };
        bool zooming_in_ { false };
        bool main_hidden_ { false };
        // 주 창이 보여 처음 자리를 알렸는가다. 그 전의 이동·크기 변경(가운데 놓기, 저장 배치, 앱이 고친 창)은 알리지 않는다.
        bool placement_ready_ { false };
        // 저장 배치가 전체 화면이었다. 창이 보인 뒤에 들어간다.
        bool initial_fullscreen_ { false };
        // 제목 막대 단추의 글리프 그림과 그 서체다 (글리프·크기마다 한 번).
        std::map<std::pair<char32_t, int>, NSImage*> glyph_images_ {};
        sk_sp<SkTypeface> codicon_ {};
        // 마지막으로 옮긴 주 창의 층이다 (`ui_frame::topmost`).
        std::optional<bool> applied_topmost_ {};
        bool stopped_ { false };
        bool quitting_ { false };
        bool exiting_ { false };
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

    void* system_cursor(const ui_cursor cursor) noexcept
    {
        return (__bridge void*)cursor_for(cursor);
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

@implementation LuilDockPanel

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

@end

@implementation LuilWindow

// 제목 막대가 없는 창도 키보드를 받고 주 창이 된다. `NSWindow`의 기본은 제목 막대(또는 크기 조절 막대)가 있는 창만 참이다.
- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)canBecomeMainWindow
{
    return YES;
}

@end

@implementation LuilPopupPanel

- (BOOL)canBecomeKeyWindow
{
    return NO;
}

- (BOOL)canBecomeMainWindow
{
    return NO;
}

@end

@implementation LuilTitlebarTarget

- (void)press:(NSButton*)sender
{
    if (self.owner != nullptr && self.surface != nullptr && sender.identifier != nil)
        self.owner->titlebar_pressed(*self.surface, luil::macos::from_ns_string(sender.identifier));
}

@end

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
    if (self.owner != nullptr && self.surface != nullptr && self.superview != nil && self.owner->passes_through(*self.surface, [self convertPoint:point fromView:self.superview]))
        return nil;
    return [super hitTest:point];
}

- (void)updateLayer
{
}

// --- OS 끌어 놓기 ---

- (NSPoint)luilDragPoint:(id<NSDraggingInfo>)info
{
    const NSPoint local { [self convertPoint:[info draggingLocation] fromView:nil] };
    const CGFloat scale { self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 1.0 };
    return NSMakePoint(local.x * scale, local.y * scale);
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info
{
    if (self.owner == nullptr || self.surface == nullptr)
        return NSDragOperationNone;
    NSPasteboard* const board { [info draggingPasteboard] };
    std::vector<std::u8string> files {};
    NSArray<NSURL*>* const urls { [board readObjectsForClasses:@[ [NSURL class] ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }] };
    for (NSURL* const url in urls)
        if (url.fileURL && url.path != nil)
            files.push_back(luil::macos::from_ns_string(url.path));
    std::vector<std::u8string> formats {};
    for (NSPasteboardType const type in board.types)
        formats.push_back(luil::macos::from_ns_string(type));
    // 받는 쪽이 놓기를 복사로 본다 (Win32와 같다). 원본은 끄는 쪽이 지우지 않는다.
    return self.owner->drag_entered(*self.surface, [self luilDragPoint:info], std::move(files), std::move(formats)) ? NSDragOperationCopy : NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info
{
    if (self.owner == nullptr || self.surface == nullptr)
        return NSDragOperationNone;
    return self.owner->drag_moved(*self.surface, [self luilDragPoint:info]) ? NSDragOperationCopy : NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)info
{
    static_cast<void>(info);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->drag_left(*self.surface);
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)info
{
    static_cast<void>(info);
    return self.owner != nullptr && self.surface != nullptr;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
{
    if (self.owner == nullptr || self.surface == nullptr)
        return NO;
    return self.owner->drag_dropped(*self.surface, [self luilDragPoint:info], [info draggingPasteboard]) ? YES : NO;
}

// 놓기가 끝나지 않고 끝났다 (Esc, 창 밖에서 놓음). 떠남은 `draggingExited:`가 이미 알렸다.
- (void)draggingEnded:(id<NSDraggingInfo>)info
{
    static_cast<void>(info);
}

- (void)mouseMoved:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_moved(*self.surface, event);
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_moved(*self.surface, event);
}

- (void)rightMouseDragged:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_moved(*self.surface, event);
}

- (void)mouseDown:(NSEvent*)event
{
    if (self.owner == nullptr || self.surface == nullptr)
        return;
    // 웹뷰가 쥐고 있던 키보드 초점을 되찾는다.
    if (self.window.firstResponder != self)
        [self.window makeFirstResponder:self];
    // Control+클릭은 macOS의 오른쪽 클릭이다.
    const bool secondary { ([event modifierFlags] & NSEventModifierFlagControl) != 0 };
    self.owner->mouse_button(*self.surface, event, secondary ? luil::pointer_button::right : luil::pointer_button::left, true);
    secondaryPress = secondary;
}

- (void)mouseUp:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_button(*self.surface, event, secondaryPress ? luil::pointer_button::right : luil::pointer_button::left, false);
    secondaryPress = NO;
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (self.window.firstResponder != self)
        [self.window makeFirstResponder:self];
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_button(*self.surface, event, luil::pointer_button::right, true);
}

- (void)rightMouseUp:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_button(*self.surface, event, luil::pointer_button::right, false);
}

- (void)otherMouseDown:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr && [event buttonNumber] == 2)
        self.owner->mouse_button(*self.surface, event, luil::pointer_button::middle, true);
}

- (void)otherMouseUp:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr && [event buttonNumber] == 2)
        self.owner->mouse_button(*self.surface, event, luil::pointer_button::middle, false);
}

- (void)mouseExited:(NSEvent*)event
{
    static_cast<void>(event);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->mouse_exited(*self.surface);
}

- (void)scrollWheel:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->scroll_wheel(*self.surface, event);
}

- (void)keyDown:(NSEvent*)event
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->key_down(*self.surface, event, self);
}

// Command 조합은 메뉴보다 먼저 view가 받는다. 앱 메뉴의 Command+Q만 메뉴에 남긴다.
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (self.owner == nullptr || self.surface == nullptr || [self.window firstResponder] != self)
        return NO;
    const NSEventModifierFlags flags { [event modifierFlags] };
    NSString* const base { [event charactersIgnoringModifiers] };
    if ((flags & NSEventModifierFlagCommand) != 0 && [base isEqualToString:@"q"])
        return NO;
    if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) == 0)
        return NO;
    self.owner->key_down(*self.surface, event, self);
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

// 개발용 입력 대본의 `capture`·`record`다. 앱 액션과 같은 길(`dispatch`)로 낸다.
- (void)luilCapture:(NSString*)path
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->script_capture(luil::macos::from_ns_string(path), self.surface->id);
}

- (void)luilRecord:(NSString*)path
{
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->script_record(luil::macos::from_ns_string(path), self.surface->id);
}

// --- 접근성: 요소는 다리가 세운다. view 자신은 접근 요소가 아니다 (`isAccessibilityElement`는 기본값 NO). ---

- (NSArray*)accessibilityChildren
{
    luil::macos::accessibility_bridge* const bridge { self.owner != nullptr && self.surface != nullptr ? self.surface->accessibility.get() : nullptr };
    return bridge != nullptr ? bridge->children() : @[];
}

- (id)accessibilityHitTest:(NSPoint)point
{
    luil::macos::accessibility_bridge* const bridge { self.owner != nullptr && self.surface != nullptr ? self.surface->accessibility.get() : nullptr };
    id const hit { bridge != nullptr ? bridge->hit_test(point) : nil };
    return hit != nil ? hit : [super accessibilityHitTest:point];
}

- (id)accessibilityFocusedUIElement
{
    luil::macos::accessibility_bridge* const bridge { self.owner != nullptr && self.surface != nullptr ? self.surface->accessibility.get() : nullptr };
    id const focused { bridge != nullptr ? bridge->focused_element() : nil };
    return focused != nil ? focused : [super accessibilityFocusedUIElement];
}

- (NSView*)luilViewForSurface:(NSString*)surface
{
    return self.owner != nullptr ? self.owner->view_of(luil::macos::from_ns_string(surface)) : nil;
}

- (void)luilSnapshot:(NSString*)path surface:(NSString*)surface
{
    if (self.owner != nullptr)
        self.owner->snapshot_surface(luil::macos::from_ns_string(surface), path);
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
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->insert_text(*self.surface, utf8_of(string));
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
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->request_window_close(*self.surface);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_resized(*self.surface);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_resized(*self.surface);
}

- (void)windowDidChangeOcclusionState:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_occlusion_changed(*self.surface);
}

- (void)windowDidMove:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_moved(*self.surface);
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, true, false);
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, true, true);
}

- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, false, false);
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, false, true);
}

- (void)windowDidFailToEnterFullScreen:(NSWindow*)window
{
    static_cast<void>(window);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, true, true);
}

- (void)windowDidFailToExitFullScreen:(NSWindow*)window
{
    static_cast<void>(window);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_fullscreen_transition(*self.surface, false, true);
}

- (BOOL)windowShouldZoom:(NSWindow*)window toFrame:(NSRect)frame
{
    static_cast<void>(window);
    static_cast<void>(frame);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_will_zoom(*self.surface);
    return YES;
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_deminiaturized(*self.surface);
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_key_window(*self.surface, true);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr && self.surface != nullptr)
        self.owner->on_key_window(*self.surface, false);
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
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(screensChanged:) name:NSApplicationDidChangeScreenParametersNotification object:nil];
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    static_cast<void>(notification);
    [NSApp removeObserver:self forKeyPath:@"effectiveAppearance"];
    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)applicationDidBecomeActive:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_active(true);
}

- (void)applicationDidResignActive:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_active(false);
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

- (NSMenu*)applicationDockMenu:(NSApplication*)sender
{
    static_cast<void>(sender);
    return self.owner != nullptr ? self.owner->dock_menu() : nil;
}

- (void)screensChanged:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_screens_changed();
}

- (void)systemSettingsChanged:(NSNotification*)notification
{
    static_cast<void>(notification);
    if (self.owner != nullptr)
        self.owner->on_appearance_changed();
}

// Command+Q와 Dock의 종료는 주 창의 닫기와 같은 길이다. 끝은 앱이 정한다.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    static_cast<void>(sender);
    if (self.owner != nullptr)
        self.owner->request_close(false);
    return NSTerminateCancel;
}

@end

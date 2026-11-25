#include "luil/ios/ios_app.h"

#include "android/android_text_input.h"
#include "host/file_dialog_gate.h"
#include "host/focus_request_tracker.h"
#include "host/font_registry.h"
#include "host/frame_state.h"
#include "host/notification_queue.h"
#include "host/overlay_input.h"
#include "host/pointer_sequence.h"
#include "host/popup_dismiss.h"
#include "host/popup_reconcile.h"
#include "host/skia_renderer.h"
#include "host/system_appearance_observer.h"
#include "host/text_edit_menu.h"
#include "host/text_input_host.h"
#include "host/webview_layout.h"
#include "ios/accessibility_ios.h"
#include "ios/file_dialog_ios.h"
#include "ios/ios_clipboard.h"
#include "ios/ios_input.h"
#include "ios/webview_host_ios.h"
#include "luil/text/utf8_text.h"
#include "text/library_text.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/webview_element.h"
#include "macos/cpu_skia_renderer.h"
#include "macos/macos_input.h"
#include "macos/macos_text.h"
#include "macos/metal_skia_renderer.h"
#include "macos/notification_center.h"
#include "macos/surface_capture.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#import <CoreHaptics/CoreHaptics.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

// 최소 판(`CMAKE_OSX_DEPLOYMENT_TARGET`)이 그 판보다 낮은가다. 낮을 때만 그 판에서 deprecated된 옛 길을 컴파일한다.
//  - `@available`의 else 갈래도 컴파일은 되므로, 최소 판을 올린 구성에서는 옛 API가 deprecated 경고(-Werror)가 된다. 옛 갈래를
//    이것으로 감싸 최소 판 15.0에서는 그대로, 16·17 이상에서는 새 길만 남긴다.
#define LUIL_IOS_BEFORE_16 (__IPHONE_OS_VERSION_MIN_REQUIRED < 160000)
#define LUIL_IOS_BEFORE_17 (__IPHONE_OS_VERSION_MIN_REQUIRED < 170000)

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace luil::ios {
    class application;
}

// 글 안의 자리 하나다. 단위는 UTF-16 코드 단위다 (NSString의 index).
@interface LuilTextPosition : UITextPosition
@property(nonatomic, readonly) NSUInteger index;
+ (instancetype)positionWithIndex:(NSUInteger)index;
@end

// 글 안의 구간 하나다.
@interface LuilTextRange : UITextRange
@property(nonatomic, readonly) NSRange range;
+ (instancetype)rangeWithRange:(NSRange)range;
@end

// Skia가 그리는 view다. 터치·하드웨어 키·입력기(UITextInput)를 받아 host에 넘긴다.
//  - 글 입력 체계가 보는 문서(글·선택·조합 구간)를 쥔다. 사용자의 편집은 host가 `ime_session`으로 앱에 보내고, 앱이 바꾼
//    글은 host가 `luilSetText:`로 되돌려 놓는다.
//  - 늘 첫 응답자(first responder)로 선다. 하드웨어 키는 첫 응답자에게만 오기 때문이다. 텍스트 칸에 초점이 없으면
//    빈 입력 view(`inputView`)를 내어 소프트 키보드를 띄우지 않는다.
@interface LuilView : UIView <UITextInput>
@property(nonatomic, assign) luil::ios::application* owner;
@property(nonatomic, assign) BOOL luilKeyboardEnabled;
@property(nonatomic) UIKeyboardType keyboardType;
@property(nonatomic) UIReturnKeyType returnKeyType;
@property(nonatomic) UITextAutocorrectionType autocorrectionType;
@property(nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property(nonatomic) UITextSpellCheckingType spellCheckingType;
@property(nonatomic) UITextSmartQuotesType smartQuotesType;
@property(nonatomic) UITextSmartDashesType smartDashesType;
@property(nonatomic) UITextSmartInsertDeleteType smartInsertDeleteType;
@property(nonatomic) UIKeyboardAppearance keyboardAppearance;
@property(nonatomic, getter=isSecureTextEntry) BOOL secureTextEntry;
@property(nonatomic) BOOL enablesReturnKeyAutomatically;
@property(nonatomic, copy) UITextContentType textContentType;
- (NSString*)luilText;
- (NSRange)luilSelection;
- (NSRange)luilMarked;
- (void)luilSetText:(NSString*)text selection:(NSRange)selection marked:(NSRange)marked;
- (void)luilOpenLink:(id)sender;
- (void)luilCopyLink:(id)sender;
@end

// 창의 뿌리 view controller다. 상태 표시줄·홈 표시줄·화면 방향을 앱의 기기 요청대로 답한다.
@interface LuilViewController : UIViewController <UIEditMenuInteractionDelegate>
@property(nonatomic, assign) luil::ios::application* owner;
@property(nonatomic, strong) LuilView* luilView;
@end

@interface LuilSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@interface LuilApplicationDelegate : UIResponder <UIApplicationDelegate>
@end

namespace luil::ios {
    namespace {
        // 시간이 흘러야 바뀌는 그림을 이 간격보다 촘촘히 그리지 않는다 (macOS와 같다).
        constexpr std::chrono::milliseconds continuous_repaint_interval { 16 };
        // 하드웨어 키의 자동 반복이다. iOS는 앱이 받은 키(`pressesBegan:`)를 되풀이해 주지 않는다.
        constexpr double key_repeat_delay { 0.5 };
        constexpr double key_repeat_interval { 0.05 };
        // 정밀 스크롤(트랙패드)의 점 몇 개를 휠 한 눈금(120)으로 보는가다 (macOS와 같다).
        constexpr double points_per_detent { 40.0 };

        void log_error(const std::u8string& message)
        {
            std::fprintf(stderr, "luil: %.*s\n", static_cast<int>(message.size()), reinterpret_cast<const char*>(message.data()));
        }

        [[nodiscard]] std::chrono::steady_clock::time_point now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        // UI thread를 깨우는 길이다 (macOS와 같다). 다른 thread가 부르면 메인 run loop에 블록을 하나 올린다.
        //  - 블록은 모든 공통 모드에서 돈다 — 스크롤 추적 중에도 깨어난다.
        //  - 같은 깨우기가 이미 올라가 있으면 다시 올리지 않는다. 앱이 멈춘 뒤 도착한 블록은 아무 일도 하지 않는다.
        struct wake_state
        {
            std::atomic<bool> scheduled { false };
            // 메인 thread만 읽고 쓴다.
            application* target { nullptr };
        };

        // 배경 작업 시간 하나다 (`beginBackgroundTaskWithName:expirationHandler:`). 쥐기와 놓기는 UIKit이 아무 thread에서 부르도록
        // 허락한 두 함수라 logic·worker thread에서 그대로 부른다. 만료 처리기는 메인 thread에서 불리고, 그 안에서 시간을 돌려준다.
        class background_grant final : public background_work::grant
        {
        public:
            struct state
            {
                UIApplication* application { nil };
                std::atomic<UIBackgroundTaskIdentifier> task { UIBackgroundTaskInvalid };
                std::atomic<bool> expired { false };

                // 한 번만 돌려준다. 놓기와 만료가 엇갈려도 표를 먼저 비운 쪽만 돌려준다.
                void end() noexcept
                {
                    if (const UIBackgroundTaskIdentifier held { task.exchange(UIBackgroundTaskInvalid) }; held != UIBackgroundTaskInvalid)
                        [application endBackgroundTask:held];
                }
            };

            explicit background_grant(std::shared_ptr<state> held) noexcept
                : state_ { std::move(held) }
            {
            }

            ~background_grant() override
            {
                state_->end();
            }

            void release() noexcept override
            {
                state_->end();
            }

            [[nodiscard]] bool expired() const noexcept override
            {
                return state_->expired.load();
            }

        private:
            std::shared_ptr<state> state_ {};
        };

        // 가족 이름 → typeface와 글자 → 대체 typeface를 함께 맡는다 (Android·macOS host와 같은 모양이다).
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

        // 표면 가장자리 중 시스템이 덮는 몫이다 (물리 픽셀).
        struct edge_insets
        {
            int left { 0 };
            int top { 0 };
            int right { 0 };
            int bottom { 0 };

            [[nodiscard]] bool operator==(const edge_insets&) const noexcept = default;
        };

        // 앱이 바란 방향을 UIKit의 방향 묶음으로 옮긴다. 시스템이 Info.plist의 방향과 겹치는 것만 쓴다.
        [[nodiscard]] UIInterfaceOrientationMask orientation_mask(const screen_orientation orientation, const UIInterfaceOrientation current)
        {
            switch (orientation)
            {
            case screen_orientation::portrait:
                return UIInterfaceOrientationMaskPortrait | UIInterfaceOrientationMaskPortraitUpsideDown;
            case screen_orientation::landscape:
                return UIInterfaceOrientationMaskLandscape;
            case screen_orientation::current:
                switch (current)
                {
                case UIInterfaceOrientationPortrait:
                    return UIInterfaceOrientationMaskPortrait;
                case UIInterfaceOrientationPortraitUpsideDown:
                    return UIInterfaceOrientationMaskPortraitUpsideDown;
                case UIInterfaceOrientationLandscapeLeft:
                    return UIInterfaceOrientationMaskLandscapeLeft;
                case UIInterfaceOrientationLandscapeRight:
                    return UIInterfaceOrientationMaskLandscapeRight;
                default:
                    return UIInterfaceOrientationMaskAll;
                }
            default:
                return UIInterfaceOrientationMaskAll;
            }
        }

        // 고대비 팔레트에 쓸 색이다. iOS는 Windows 같은 고대비 색 집합을 주지 않아 밝은 모양이면 흰 바탕, 어두운 모양이면
        // 검정 바탕의 기본 색이다 (Android와 같은 값).
        [[nodiscard]] high_contrast_colors default_high_contrast_colors(const bool prefers_light) noexcept
        {
            high_contrast_colors colors {};
            if (prefers_light == false)
                return colors;
            const ui_color black { make_ui_color(0, 0, 0) };
            const ui_color white { make_ui_color(255, 255, 255) };
            colors.window_background = white;
            colors.window_foreground = black;
            colors.highlight_background = black;
            colors.highlight_foreground = white;
            colors.emphasis = make_ui_color(0, 0, 160);
            colors.button_background = white;
            colors.button_foreground = black;
            colors.disabled_foreground = make_ui_color(96, 96, 96);
            return colors;
        }

        // UTF-8 글을 NSString으로 옮긴다. 옮기지 못하면 빈 글이다.
        [[nodiscard]] NSString* ns_text(const std::u8string_view text)
        {
            NSString* const value { macos::make_ns_string(text) };
            return value != nil ? value : @"";
        }

        // Apple Pencil의 고도·방위각(라디안)을 W3C의 두 기울기 축(도)으로 옮긴다 (ink-input-design.md).
        //  - 고도 π/2가 화면에 수직이다. 기울기 θ = π/2 − 고도, `tilt_x = atan(tan θ · cos φ)`, `tilt_y = atan(tan θ · sin φ)`다.
        [[nodiscard]] std::pair<float, float> pencil_tilt(const CGFloat altitude, const CGFloat azimuth)
        {
            const double theta { std::clamp(M_PI_2 - static_cast<double>(altitude), 0.0, M_PI_2) };
            constexpr double degrees { 180.0 / M_PI };
            if (theta >= M_PI_2 - 1e-6)
                return { static_cast<float>(std::cos(azimuth) >= 0.0 ? 90.0 : -90.0), static_cast<float>(std::sin(azimuth) >= 0.0 ? 90.0 : -90.0) };
            const double slope { std::tan(theta) };
            const double x { std::atan(slope * std::cos(azimuth)) * degrees };
            const double y { std::atan(slope * std::sin(azimuth)) * degrees };
            return { std::isfinite(x) ? static_cast<float>(x) : 0.0f, std::isfinite(y) ? static_cast<float>(y) : 0.0f };
        }
    }

    // UIKit 앱 하나의 수명이다. 메인 thread에서만 산다 — 그 thread가 luil의 UI thread다.
    //  - 입력기가 묻는 창구(`text_input_host`)도 맡는다. 초점을 가진 텍스트 칸을 tree에서 찾는다 (Android host와 같다).
    //  - 프로세스와 수명이 같다. iOS 앱은 스스로 끝나지 않고, 시스템이 알리면(`applicationWillTerminate:`) host만 멈춘다.
    class application final : public text_input_host
    {
        // 표면 위에 겹쳐 그리는 popup 하나다 (Android host와 같은 모양이다).
        struct overlay_entry
        {
            std::u8string id {};
            std::shared_ptr<const ui_tree> tree {};
            // 표면 좌표다 (물리 픽셀).
            pixel_rect bounds {};
            bool border { true };
            std::function<input_action(popup_dismiss_reason)> dismiss {};
            // 이 frame에서 이미 닫자고 했다 (`take_popup_dismiss_action`).
            bool requested { false };
            std::u8string anchor {};
            int anchor_x { 0 };
            int anchor_y { 0 };
            float scroll_y { 0.0f };
            float max_scroll_y { 0.0f };
            struct followed_focus
            {
                ui_element_id target {};
                float y { 0.0f };
                float height { 0.0f };
                int viewport_height { 0 };

                [[nodiscard]] bool operator==(const followed_focus&) const noexcept = default;
            };
            std::optional<followed_focus> followed {};
            std::function<app_message(const rect_f&)> placed {};
            std::optional<rect_f> reported_bounds {};

            [[nodiscard]] float content_origin_y() const noexcept
            {
                return static_cast<float>(bounds.y) - scroll_y;
            }
        };

        // 초점을 가진 텍스트 칸과 그것이 사는 tree, 그 tree의 표면 원점이다 (물리 픽셀).
        struct text_location
        {
            std::shared_ptr<const ui_tree> tree {};
            const ui_element* element { nullptr };
            float origin_x { 0.0f };
            float origin_y { 0.0f };
        };

        // 손가락·펜·마우스 접촉 하나다. 터치 객체는 접촉이 끝날 때까지 같은 객체다.
        struct contact
        {
            std::uint32_t pointer_id { 0 };
            pointer_device device { pointer_device::touch };
            pointer_button button { pointer_button::left };
        };

        // 한 표면을 그릴 상태다. `state`가 가리키는 tree·popup 층·글꼴을 함께 쥔다.
        struct prepared_frame
        {
            frame_state state {};
            std::shared_ptr<const ui_frame> frame {};
            std::shared_ptr<const ui_tree> tree {};
            std::vector<overlay_layer> layers {};
            sk_sp<SkTypeface> code {};
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
        ~application() override = default;

        // --- text_input_host ---

        [[nodiscard]] std::optional<text_input_target> focused_text_target() const override
        {
            if (host_ == nullptr || environment_.policy == nullptr)
                return std::nullopt;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            // 주 표면이거나 겹쳐 그린 popup의 초점이다. 그 밖의 표면(보조 창)은 없다.
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

        [[nodiscard]] text_input_options input_options() const override
        {
            const text_location location { focused_text_location() };
            const auto value { location.element != nullptr ? location.element->text_input() : std::nullopt };
            return value.has_value() ? value->options : text_input_options {};
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
            // 칸이 사는 tree의 원점(안전 영역이나 popup 자리)만큼 옮겨 표면 좌표로 낸다.
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

        // 앱이 시작을 마치기 전이다 (`application:willFinishLaunchingWithOptions:`). host를 세운다.
        //  - 알림 센터의 delegate를 여기서 건다. 꺼진 앱을 띄운 눌림이 시작을 마칠 때 온다.
        [[nodiscard]] bool launch()
        {
            if (environment_.driver == nullptr)
            {
                log_error(u8"run_application needs a logic driver.");
                return false;
            }
            refresh_user_ui_language();
            // 휴대폰·태블릿이다. 창 caption이 없고 맨 위는 앱 바의 자리다. 하드웨어 키보드의 단축키는 Command다.
            // logic thread가 첫 frame을 짓기 전에 정한다.
            //  - 햅틱은 기기에 진동 장치(Taptic Engine)가 있을 때만 참이다 (Core Haptics의 하드웨어 답). iPad와 시뮬레이터는 거짓이다.
            set_ui_platform({
                .form_factor = ui_form_factor::mobile,
                .window_caption = false,
                .command_key_shortcuts = true,
                .capabilities = {
                    .screen_orientation = true,
                    .system_bars = true,
                    .haptics = CHHapticEngine.capabilitiesForHardware.supportsHaptics == YES,
                    .notifications = macos::notification_center_available(),
                    .clipboard_image_copy = true,
                },
            });
            set_font_fallback(&font_resolver_);
            refresh_appearance(nil);
            refresh_font_sizes(nil);

            wake_->target = this;
            const std::shared_ptr<wake_state> wake { wake_ };
            const auto signal = [wake]() noexcept {
                if (wake->scheduled.exchange(true))
                    return true;
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
            // iOS에는 두 번 누름 간격 설정이 없다. UIKit의 두 번 탭 인식기와 같은 값이다.
            host_config.interaction.double_click_time = std::chrono::milliseconds { 350 };
            host_config.interaction.touch = config_.touch;
            host_config.interaction.text_edit_menu = true;
            router_.set_touch_config(config_.touch);
            // 햅틱은 아무 thread에서 오므로 메인으로 넘긴다. 진동 장치가 없는 기기(iPad)에서는 조용하다 (capability도 거짓이다).
            host_config.haptic = [](const haptic_feedback feedback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                  switch (feedback)
                  {
                  case haptic_feedback::tick:
                      [[UISelectionFeedbackGenerator new] selectionChanged];
                      break;
                  case haptic_feedback::confirm:
                      [[UINotificationFeedbackGenerator new] notificationOccurred:UINotificationFeedbackTypeSuccess];
                      break;
                  case haptic_feedback::reject:
                      [[UINotificationFeedbackGenerator new] notificationOccurred:UINotificationFeedbackTypeError];
                      break;
                  case haptic_feedback::drag_start:
                      [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
                      break;
                  default:
                      [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium] impactOccurred];
                      break;
                  }
                });
            };
            // 묻는 권한은 OS 알림 하나다 (UserNotifications의 허락). 로컬 네트워크는 iOS가 처음 쓸 때 스스로 묻고, 묻기 전에
            // 상태를 알 길이 없어 "묻지 않아도 됨"이다.
            // 배경 작업 시간이다. 앱이 뒤로 갈 때의 lifecycle 메시지가 쥐고(`post_lifecycle`), 앱도 따로 쥔다.
            //  - 만료 처리기는 시간을 돌려준 뒤 앱에 한 번 알린다 (`app_delegate::make_background_expired_message`).
            //  - 처리기가 표를 적기 전에 불렸으면(쥔 thread가 메인이 아니다) 적은 뒤에 돌려준다.
            host_config.hold_background = [wake, application = UIApplication.sharedApplication]() -> background_work {
                if (application == nil)
                    return {};
                const auto held { std::make_shared<background_grant::state>() };
                held->application = application;
                const UIBackgroundTaskIdentifier task { [application beginBackgroundTaskWithName:@"luil"
                                                                               expirationHandler:^{
                                                                                 held->expired.store(true);
                                                                                 held->end();
                                                                                 if (wake->target != nullptr)
                                                                                     wake->target->on_background_expired();
                                                                               }] };
                if (task == UIBackgroundTaskInvalid)
                    return {};
                held->task.store(task);
                if (held->expired.load())
                    held->end();
                return background_work { std::make_unique<background_grant>(held) };
            };
            host_config.permission = [](const app_permission permission) {
                return permission == app_permission::notifications ? macos::notification_permission() : permission_state::not_required;
            };
            host_config.request_permission = [wake](const app_permission permission) {
                if (permission != app_permission::notifications)
                    return false;
                return macos::request_notification_permission([wake](const permission_state state) {
                    if (wake->target != nullptr)
                        wake->target->post_permission(app_permission::notifications, state);
                });
            };
            if (app_message message { observe_appearance() }; message.empty() == false)
                host_config.initial_messages.push_back(std::move(message));
            host_ = std::make_unique<app_host>(std::move(host_config), *environment_.driver, environment_.policy);

            ime_ = std::make_unique<android::ime_session>(*this,
                android::ime_session::platform {
                    .set_state = [this](const android::ime_state& state) { set_ime_state(state); },
                    .show_keyboard = [this](const bool show, const bool secret) { show_keyboard(show, secret); },
                    .submit = [this] { submit_text(); },
                    .configure = [this](const text_input_options&) { configure_keyboard(); },
                });

            // 알림의 눌림·닫음이다. 꺼진 앱을 띄운 눌림도 받도록 시작을 마치기 전에 건다.
            macos::set_notification_receiver([state = wake](notification_event event) {
                if (state->target == nullptr)
                    return;
                state->target->notification_events_.push(std::move(event));
                state->target->deliver_notification_events();
            });
            observe_system();
            if (environment_.delegate != nullptr)
                environment_.delegate->on_started(*host_);
            return true;
        }

        // 시스템이 앱을 끝낸다 (`applicationWillTerminate:`). 종료 신호와 종료 저장을 돌린다.
        void stop()
        {
            if (stopped_ || host_ == nullptr)
                return;
            stopped_ = true;
            cancel_key_repeat();
            if (timer_ != nil)
            {
                [timer_ invalidate];
                timer_ = nil;
            }
            macos::set_notification_receiver(nullptr);
            for (const auto& observer : observers_)
                [[NSNotificationCenter defaultCenter] removeObserver:observer];
            observers_.clear();
            if (webviews_ != nullptr)
                webviews_->shutdown();
            if (accessibility_ != nullptr)
                accessibility_->detach();
            host_->shutdown();
            wake_->target = nullptr;
        }

        // 장면이 섰다. 창·view controller·Skia view를 세운다. 렌더러는 view가 첫 배치를 마치면 선다 (`refresh_metrics`).
        void attach(UIWindowScene* const scene, LuilSceneDelegate* const delegate)
        {
            if (host_ == nullptr || window_ != nil)
                return;
            scene_ = scene;
            window_ = [[UIWindow alloc] initWithWindowScene:scene];
            controller_ = [LuilViewController new];
            controller_.owner = this;
            // view는 처음 물을 때 선다 (`loadView`). Skia view를 꺼내기 전에 세운다.
            [controller_ loadViewIfNeeded];
            view_ = controller_.luilView;
            view_.owner = this;
            window_.rootViewController = controller_;
            delegate.window = window_;
            [window_ makeKeyAndVisible];
            create_webviews();
            create_accessibility();
            install_gestures();
            refresh_appearance(controller_.traitCollection);
            refresh_font_sizes(controller_.traitCollection);
            post_appearance();
        }

        // 장면이 떨어져 나갔다 (시스템이 자원을 거두려고 장면을 버렸다). 다시 이어지면 `attach`가 새로 세운다.
        void detach()
        {
            if (window_ == nil)
                return;
            commit_and_cancel_input();
            ime_->detach();
            if (webviews_ != nullptr)
                webviews_->shutdown();
            webviews_.reset();
            if (accessibility_ != nullptr)
                accessibility_->detach();
            accessibility_.reset();
            renderer_.reset();
            view_.owner = nullptr;
            controller_.owner = nullptr;
            view_ = nil;
            controller_ = nil;
            window_ = nil;
            scene_ = nil;
            width_ = 0;
            height_ = 0;
            visible_ = false;
        }

        // --- 장면 알림 ---

        void on_foreground(const bool foreground)
        {
            visible_ = foreground;
            if (foreground)
                background_expired_ = false;
            post_lifecycle(foreground ? app_lifecycle::foreground : app_lifecycle::background);
            if (foreground)
            {
                refresh_appearance(controller_ != nil ? controller_.traitCollection : nil);
                post_appearance();
                dirty_ = true;
            }
            else
                commit_and_cancel_input();
            pump();
        }

        void on_active(const bool active)
        {
            if (host_ == nullptr)
                return;
            if (active)
            {
                host_->post_raw_input(surface_focus_gained_event {});
                [view_ becomeFirstResponder];
                check_clipboard();
                deliver_notification_events();
            }
            else
            {
                if (ime_ != nullptr && ime_->composing())
                    ime_->commit_composition();
                cancel_key_repeat();
                static_cast<void>(dismiss_popups(popup_dismiss_reason::activation_changed));
                host_->post_raw_input(surface_focus_lost_event {});
            }
            dirty_ = true;
            pump();
        }

        void on_trait_change()
        {
            if (controller_ == nil)
                return;
            refresh_appearance(controller_.traitCollection);
            refresh_font_sizes(controller_.traitCollection);
            post_appearance();
            refresh_metrics();
            pump();
        }

        void on_keyboard_frame(NSNotification* const notification, const bool hiding)
        {
            if (view_ == nil)
                return;
            CGFloat overlap { 0 };
            if (hiding == false)
            {
                const CGRect screen_frame { [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue] };
                UIScreen* const screen { view_.window.screen };
                const CGRect frame { screen != nil ? [view_ convertRect:screen_frame fromCoordinateSpace:screen.coordinateSpace] : CGRectZero };
                const CGRect bounds { view_.bounds };
                // 떠 있는 키보드(iPad의 작은 키보드, 떼어 낸 키보드)는 아래 가장자리에 붙지 않는다. 내용을 밀지 않는다.
                if (CGRectIsEmpty(frame) == false && CGRectGetMaxY(frame) >= CGRectGetMaxY(bounds) - 1.0)
                    overlap = std::max<CGFloat>(0, CGRectGetMaxY(bounds) - CGRectGetMinY(frame));
            }
            if (overlap == keyboard_overlap_)
                return;
            keyboard_overlap_ = overlap;
            refresh_metrics();
            pump();
        }

        // 사용자가 키보드를 내렸다 (iPad의 키보드 내리기 단추). 같은 칸을 다시 누르면 다시 띄운다.
        void on_keyboard_hidden()
        {
            if (ime_ != nullptr)
                ime_->set_keyboard_visible(false);
            reveal_pending_ = false;
            follow_ime_focus_ = false;
        }

        // view의 크기나 안전 영역이 바뀌었다.
        void on_layout()
        {
            refresh_metrics();
            pump();
        }

        // --- view가 묻는 것 ---

        [[nodiscard]] UIInterfaceOrientationMask supported_orientations() const
        {
            return supported_orientations_;
        }

        [[nodiscard]] bool bars_hidden() const noexcept
        {
            return applied_device_.system_bars == system_bars_mode::immersive;
        }

        [[nodiscard]] UIStatusBarStyle status_bar_style() const noexcept
        {
            if (status_bar_light_.has_value() == false)
                return UIStatusBarStyleDefault;
            return *status_bar_light_ ? UIStatusBarStyleDarkContent : UIStatusBarStyleLightContent;
        }

        // view 점이 보이는 아래층 웹뷰의 구멍 안인가. 참이면 터치가 Skia view를 지나 웹뷰로 간다.
        [[nodiscard]] bool passes_through(const CGPoint point) const
        {
            return webviews_ != nullptr && webviews_->passes_through(static_cast<float>(point.x * scale_), static_cast<float>(point.y * scale_));
        }

        [[nodiscard]] NSArray* accessibility_elements()
        {
            return accessibility_ != nullptr ? accessibility_->elements() : nil;
        }

        [[nodiscard]] id accessibility_focused()
        {
            return accessibility_ != nullptr ? accessibility_->focused_element() : nil;
        }

        // --- 터치·펜·마우스 ---

        void touches(NSSet<UITouch*>* const set, UIEvent* const event, const pointer_phase phase)
        {
            if (host_ == nullptr || view_ == nil)
                return;
            // 사용자가 키보드를 내려 첫 응답자를 놓았으면 되찾는다. 하드웨어 키가 다시 온다.
            if (phase == pointer_phase::down && view_.isFirstResponder == NO)
                [view_ becomeFirstResponder];
            for (UITouch* const touch in set)
            {
                if (touch.type == UITouchTypeIndirectPointer)
                {
                    mouse_touch(touch, event, phase);
                    continue;
                }
                const auto key { (__bridge const void*)touch };
                if (phase == pointer_phase::down)
                    contacts_[key] = contact { next_pointer_id_++, touch.type == UITouchTypePencil ? pointer_device::pen : pointer_device::touch };
                const auto found { contacts_.find(key) };
                if (found == contacts_.end())
                    continue;
                const contact current { found->second };
                if (phase == pointer_phase::up && touch.phase == UITouchPhaseCancelled)
                {
                    post_events(tracker_.cancel(current.pointer_id, now(), {}));
                    contacts_.erase(found);
                    continue;
                }
                // 펜은 한 frame 사이의 표본을 모두 싣는다 (잉크가 매끄럽다). 손가락은 마지막 것이면 된다.
                NSArray<UITouch*>* const samples { current.device == pointer_device::pen && phase == pointer_phase::update ? [event coalescedTouchesForTouch:touch] : nil };
                if (samples.count > 0)
                    for (UITouch* const sample : samples)
                        post_events(tracker_.accept(sample_of(sample, event, current, phase)));
                else
                    post_events(tracker_.accept(sample_of(touch, event, current, phase)));
                if (phase == pointer_phase::up)
                    contacts_.erase(found);
            }
            pump();
        }

        // 트랙패드·마우스의 누름이다 (`UIApplicationSupportsIndirectInputEvents`). 두 번째 단추는 우클릭이다.
        void mouse_touch(UITouch* const touch, UIEvent* const event, const pointer_phase phase)
        {
            const auto key { (__bridge const void*)touch };
            const CGPoint point { [touch locationInView:view_] };
            const float x { static_cast<float>(point.x * scale_) - static_cast<float>(insets_.left) };
            const float y { static_cast<float>(point.y * scale_) - static_cast<float>(insets_.top) };
            const UIKeyModifierFlags flags { event.modifierFlags };
            const bool control { (flags & UIKeyModifierCommand) != 0 || (flags & UIKeyModifierControl) != 0 };
            const bool shift { (flags & UIKeyModifierShift) != 0 };
            const bool alt { (flags & UIKeyModifierAlternate) != 0 };
            if (phase == pointer_phase::down)
            {
                const pointer_button button { (event.buttonMask & UIEventButtonMaskSecondary) != 0 ? pointer_button::right : pointer_button::left };
                contacts_[key] = contact { 0, pointer_device::mouse, button };
                pointer_pressed_event pressed {};
                pressed.x = x;
                pressed.y = y;
                pressed.button = button;
                pressed.time = now();
                pressed.device = pointer_device::mouse;
                pressed.scale = scale_;
                pressed.control = control;
                pressed.shift = shift;
                pressed.alt = alt;
                post_events({ raw_input_event { std::move(pressed) } });
                return;
            }
            const auto found { contacts_.find(key) };
            if (found == contacts_.end())
                return;
            const pointer_button button { found->second.button };
            if (phase == pointer_phase::update)
            {
                pointer_moved_event moved {};
                moved.x = x;
                moved.y = y;
                moved.time = now();
                moved.device = pointer_device::mouse;
                moved.in_contact = true;
                moved.scale = scale_;
                moved.control = control;
                moved.shift = shift;
                moved.alt = alt;
                post_events({ raw_input_event { std::move(moved) } });
                return;
            }
            contacts_.erase(found);
            pointer_released_event released {};
            released.x = x;
            released.y = y;
            released.button = button;
            released.time = now();
            released.device = pointer_device::mouse;
            released.control = control;
            released.shift = shift;
            released.alt = alt;
            released.cancelled = touch.phase == UITouchPhaseCancelled;
            post_events({ raw_input_event { std::move(released) } });
        }

        // 마우스·트랙패드 포인터가 누르지 않고 지나간다 (`UIHoverGestureRecognizer`).
        void hover(UIHoverGestureRecognizer* const recognizer)
        {
            if (host_ == nullptr || view_ == nil)
                return;
            if (recognizer.state == UIGestureRecognizerStateEnded || recognizer.state == UIGestureRecognizerStateCancelled)
            {
                post_events({ raw_input_event { pointer_left_event { {}, pointer_device::mouse } } });
                pump();
                return;
            }
            const CGPoint point { [recognizer locationInView:view_] };
            pointer_moved_event moved {};
            moved.x = static_cast<float>(point.x * scale_) - static_cast<float>(insets_.left);
            moved.y = static_cast<float>(point.y * scale_) - static_cast<float>(insets_.top);
            moved.time = now();
            moved.device = pointer_device::mouse;
            moved.in_contact = false;
            moved.scale = scale_;
            if (@available(iOS 13.4, *))
            {
                const UIKeyModifierFlags flags { recognizer.modifierFlags };
                moved.control = (flags & UIKeyModifierCommand) != 0 || (flags & UIKeyModifierControl) != 0;
                moved.shift = (flags & UIKeyModifierShift) != 0;
                moved.alt = (flags & UIKeyModifierAlternate) != 0;
            }
            post_events({ raw_input_event { std::move(moved) } });
            pump();
        }

        // 트랙패드의 두 손가락 스크롤·마우스 휠이다. 손가락의 이동이 곧 내용의 이동이라 macOS의 자연스러운 스크롤과 같은 부호다.
        void scroll(UIPanGestureRecognizer* const recognizer)
        {
            if (host_ == nullptr || view_ == nil)
                return;
            if (recognizer.state == UIGestureRecognizerStateBegan)
                scroll_translation_ = CGPointZero;
            if (recognizer.state != UIGestureRecognizerStateBegan && recognizer.state != UIGestureRecognizerStateChanged)
                return;
            const CGPoint translation { [recognizer translationInView:view_] };
            const CGFloat dx { translation.x - scroll_translation_.x };
            const CGFloat dy { translation.y - scroll_translation_.y };
            scroll_translation_ = translation;
            const CGPoint point { [recognizer locationInView:view_] };
            const auto wheel = [&](const CGFloat value, const bool horizontal) {
                mouse_wheel_event event {};
                event.x = static_cast<float>(point.x * scale_) - static_cast<float>(insets_.left);
                event.y = static_cast<float>(point.y * scale_) - static_cast<float>(insets_.top);
                // 가로는 Win32와 같이 오른쪽이 양수다.
                event.delta = static_cast<float>((horizontal ? -value : value) / points_per_detent * 120.0);
                event.time = now();
                event.horizontal = horizontal;
                return raw_input_event { std::move(event) };
            };
            std::vector<raw_input_event> events {};
            if (dy != 0)
                events.push_back(wheel(dy, false));
            if (dx != 0)
                events.push_back(wheel(dx, true));
            if (events.empty() == false)
                post_events(std::move(events));
            pump();
        }

        // --- 하드웨어 키 ---

        // 눌린 키다. 참이면 앱이 먹었다. 거짓이면 view가 UIKit에 넘겨 글 입력 체계(입력기 조합 포함)가 맡는다.
        [[nodiscard]] bool press_began(UIPress* const press)
        {
            if (host_ == nullptr)
                return false;
            UIKey* const key { press.key };
            if (key == nil)
                return false;
            const std::optional<std::uint16_t> carbon { carbon_key_of_hid(static_cast<std::uint32_t>(key.keyCode)) };
            if (carbon.has_value() == false)
                return false;
            const macos::key_input input { key_input_of(key, *carbon, false) };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            const bool key_capture { interaction.key_capture || interaction.menu_keys };
            const bool editing { ime_ != nullptr && ime_->target().has_value() && view_.luilKeyboardEnabled };

            // Esc가 popup·편집 메뉴를 닫았으면 키를 삼킨다 (Win32·Android와 같다).
            if (input.key_code == 0x35 && dismiss_popups(popup_dismiss_reason::escape_key))
            {
                pump();
                return true;
            }
            // 글을 입력하는 중의 글자 키는 글 입력 체계가 맡는다. Command·Control 조합과 키 잡기는 앱의 키다.
            if (editing && key_capture == false && input.command == false && input.control == false && text_system_key(static_cast<std::uint32_t>(key.keyCode)))
                return false;
            if (ime_ != nullptr && ime_->composing())
                ime_->commit_composition();
            post_key(key, input, key_capture);
            start_key_repeat(press, input, key_capture);
            pump();
            return true;
        }

        void press_ended(UIPress* const press)
        {
            if (repeat_press_ == press)
                cancel_key_repeat();
        }

        // --- 글 입력 체계 ---

        // 사용자가 view의 문서를 고쳤다 (`insertText:`·`deleteBackward`·조합·선택 이동). 그 상태를 IME 연결에 넘긴다.
        void text_changed_by_system()
        {
            if (ime_ == nullptr || view_ == nil)
                return;
            NSString* const text { [view_ luilText] };
            const NSRange selection { [view_ luilSelection] };
            const NSRange marked { [view_ luilMarked] };
            android::ime_state state {};
            state.text = macos::from_ns_string(text);
            state.selection = { static_cast<std::int32_t>(selection.location), static_cast<std::int32_t>(NSMaxRange(selection)) };
            if (marked.location != NSNotFound)
                state.composing = { static_cast<std::int32_t>(marked.location), static_cast<std::int32_t>(NSMaxRange(marked)) };
            ime_->accept(state);
            pump();
        }

        // 그 UTF-16 구간이 서는 자리다 (view 점). 첫 줄의 것이다. 모르면 빈 사각형이다.
        [[nodiscard]] CGRect text_range_rect(const NSRange range)
        {
            if (view_ == nil)
                return CGRectNull;
            NSString* const text { [view_ luilText] };
            const NSRange selection { [view_ luilSelection] };
            text_input_document document {};
            document.text = macos::from_ns_string(text);
            document.caret = android::utf8_offset_from_utf16(document.text, static_cast<std::int32_t>(NSMaxRange(selection)));
            document.anchor = android::utf8_offset_from_utf16(document.text, static_cast<std::int32_t>(selection.location));
            const std::size_t begin { android::utf8_offset_from_utf16(document.text, static_cast<std::int32_t>(range.location)) };
            const std::size_t end { android::utf8_offset_from_utf16(document.text, static_cast<std::int32_t>(NSMaxRange(range))) };
            const std::optional<rect_f> span { text_rect(document, begin, end) };
            if (span.has_value() == false || scale_ <= 0.0f)
                return CGRectNull;
            return CGRectMake(span->x / scale_, span->y / scale_, std::max(1.0f, span->width) / scale_, span->height / scale_);
        }

        // --- 깨우기 ---

        void on_wake()
        {
            execute_host_commands();
            deliver_notification_events();
            dirty_ = true;
            pump();
        }

        void on_timer()
        {
            timer_ = nil;
            timer_deadline_.reset();
            pump();
        }

        void on_key_repeat()
        {
            if (repeat_key_.has_value() == false || host_ == nullptr)
                return;
            macos::key_input input { repeat_key_->input };
            input.repeat = true;
            input.time = now();
            if (auto key { macos::key_event_for(input, repeat_key_->key_capture) }; key.has_value())
                post_events({ raw_input_event { std::move(*key) } });
            pump();
        }

        void post_permission(const app_permission permission, const permission_state state)
        {
            if (host_ == nullptr || environment_.delegate == nullptr || stopped_)
                return;
            if (app_message message { environment_.delegate->make_permission_message(permission, state) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        void complete_file_dialog(const file_dialog_result& result)
        {
            if (host_ != nullptr && environment_.delegate != nullptr && stopped_ == false)
                host_->post_file_dialog_message(environment_.delegate->make_file_dialog_message(result));
            file_dialog_gate_.finish();
            pump();
        }

        // 클립보드가 바뀌었으면 앱에 알린다 (`clipboard_change`). 내용은 읽지 않는다 (붙여넣음 알림이 뜨지 않는다).
        void check_clipboard()
        {
            if (host_ == nullptr || environment_.delegate == nullptr || stopped_)
                return;
            const std::uint32_t sequence { clipboard_sequence() };
            if (reported_clipboard_sequence_ == sequence)
                return;
            reported_clipboard_sequence_ = sequence;
            const clipboard_change change { owned_clipboard_sequence_ == sequence, sequence, false, clipboard_has_image() };
            if (app_message message { environment_.delegate->make_clipboard_changed_message(change) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

    private:
        // 시스템 설정의 바뀜을 받는다. 움직임·대비·투명도 줄이기는 trait 바뀜으로 오지 않는다.
        void observe_system()
        {
            NSNotificationCenter* const center { [NSNotificationCenter defaultCenter] };
            const std::shared_ptr<wake_state> state { wake_ };
            const auto settings = ^(NSNotification*) {
              if (state->target != nullptr)
                  state->target->on_trait_change();
            };
            for (NSNotificationName const name in @[ UIAccessibilityReduceMotionStatusDidChangeNotification, UIAccessibilityDarkerSystemColorsStatusDidChangeNotification,
                     UIAccessibilityReduceTransparencyStatusDidChangeNotification, UIContentSizeCategoryDidChangeNotification ])
                observers_.push_back([center addObserverForName:name object:nil queue:[NSOperationQueue mainQueue] usingBlock:settings]);
            observers_.push_back([center addObserverForName:UIKeyboardWillChangeFrameNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification* notification) {
                                                   if (state->target != nullptr)
                                                       state->target->on_keyboard_frame(notification, false);
                                                 }]);
            observers_.push_back([center addObserverForName:UIKeyboardWillHideNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification* notification) {
                                                   if (state->target != nullptr)
                                                       state->target->on_keyboard_frame(notification, true);
                                                 }]);
            observers_.push_back([center addObserverForName:UIKeyboardDidHideNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification*) {
                                                   if (state->target != nullptr)
                                                       state->target->on_keyboard_hidden();
                                                 }]);
            // 이 앱 안의 바뀜은 바로 온다. 다른 앱이 바꾼 것은 앞으로 올 때 번호로 본다 (`on_active`).
            observers_.push_back([center addObserverForName:UIPasteboardChangedNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification*) {
                                                   if (state->target != nullptr)
                                                       state->target->check_clipboard();
                                                 }]);
        }

        void install_gestures()
        {
            if (@available(iOS 13.0, *))
            {
                UIHoverGestureRecognizer* const hover { [[UIHoverGestureRecognizer alloc] initWithTarget:controller_ action:@selector(luilHover:)] };
                [view_ addGestureRecognizer:hover];
            }
            // 스크롤 사건만 받는다 (직접 터치는 받지 않는다). 터치는 view의 `touchesBegan:`이 그대로 받는다.
            UIPanGestureRecognizer* const scroll { [[UIPanGestureRecognizer alloc] initWithTarget:controller_ action:@selector(luilScroll:)] };
            scroll.allowedTouchTypes = @[];
            scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
            scroll.cancelsTouchesInView = NO;
            [view_ addGestureRecognizer:scroll];
            if (@available(iOS 16.0, *))
            {
                UIEditMenuInteraction* const interaction { [[UIEditMenuInteraction alloc] initWithDelegate:controller_] };
                [view_ addInteraction:interaction];
                edit_interaction_ = interaction;
            }
        }

        void create_webviews()
        {
            webviews_ = std::make_unique<webview_host>(controller_.view, view_);
            webviews_->set_deliver([this](app_message message) {
                if (host_ != nullptr)
                    host_->post_app_message(std::move(message));
            });
            webviews_->set_focus_reporter([this](const std::u8string& id) {
                if (host_ != nullptr)
                    host_->post_raw_input(access_focus_event { ui_element_id { ui_element_kind::webview, id }, {}, now() });
            });
        }

        void create_accessibility()
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
            accessibility_ = std::make_unique<accessibility_bridge>(view_, std::move(callbacks));
        }

        void update_accessibility(const std::shared_ptr<const ui_tree>& tree)
        {
            if (accessibility_ == nullptr || host_ == nullptr)
                return;
            accessibility_update update {};
            update.tree = tree;
            for (const overlay_entry& entry : overlays_)
                update.overlays.push_back(accessibility_overlay { entry.id, entry.tree,
                    rect_f { static_cast<float>(entry.bounds.x), static_cast<float>(entry.bounds.y), static_cast<float>(entry.bounds.width), static_cast<float>(entry.bounds.height) },
                    entry.scroll_y });
            update.interaction = host_->acquire_interaction();
            update.scale = scale_;
            update.origin_x = static_cast<float>(insets_.left);
            update.origin_y = static_cast<float>(insets_.top);
            accessibility_->update(std::move(update));
        }

        void create_renderer()
        {
            if (view_ == nil || renderer_ != nullptr || width_ <= 0 || height_ <= 0)
                return;
            CALayer* const layer { view_.layer };
            renderer_factories factories {};
            factories.gpu_name = u8"Metal";
            // 한 번 물러선 앱은 Metal을 다시 쓰지 않는다 (장면이 다시 서도 CPU다).
            if (gpu_abandoned_ == false)
                factories.create_gpu = [layer, simulate = config_.simulate_gpu_failure]() -> renderer_factory_result {
                    if (simulate)
                        return { nullptr, u8"The smoke test injected a Metal creation failure." };
                    renderer_factory_result result { macos::create_metal_skia_renderer((__bridge void*)layer) };
                    if (result.renderer == nullptr)
                        log_error(u8"Metal renderer: " + result.error);
                    return result;
                };
            factories.create_cpu = [layer] { return macos::create_cpu_skia_renderer((__bridge void*)layer); };
            const renderer_fault_injection fault { .at_creation = false, .after_frames = config_.simulate_gpu_loss_after_frames };
            std::u8string error {};
            renderer_ = renderer_host::create(config_.renderer, fault, std::move(factories), error);
            if (renderer_ == nullptr)
            {
                log_error(error);
                return;
            }
            if (renderer_->resize(width_, height_, error) == false)
                log_error(error);
            report_renderer();
        }

        // 지금 그리는 렌더러를 한 줄 남긴다. 처음과 물러선 뒤에만이다 (Android의 logcat 줄과 같은 자리).
        void report_renderer()
        {
            if (renderer_ == nullptr)
                return;
            if (reported_backend_.has_value() && *reported_backend_ == renderer_->backend())
                return;
            reported_backend_ = renderer_->backend();
            const std::u8string_view name { renderer_backend_name(renderer_->backend()) };
            std::fprintf(stderr, "luil: renderer %.*s%s\n", static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()), renderer_->used_fallback() ? " (fallback)" : "");
            if (renderer_->backend() == renderer_backend::cpu && renderer_->used_fallback())
                gpu_abandoned_ = true;
        }

        // view의 크기·배율과 안전 영역을 다시 읽어 렌더러와 앱에 알린다.
        void refresh_metrics()
        {
            if (view_ == nil || controller_ == nil)
                return;
            const int previous_window_width { width_ };
            const int previous_window_height { height_ };
            const edge_insets previous_insets { insets_ };
            const int previous_width { content_width() };
            const int previous_height { content_height() };

            const CGFloat display { view_.traitCollection.displayScale };
            scale_ = static_cast<float>(display > 0 ? display : 1.0);
            const CGRect bounds { view_.bounds };
            width_ = static_cast<int>(std::lround(bounds.size.width * scale_));
            height_ = static_cast<int>(std::lround(bounds.size.height * scale_));
            insets_ = read_insets();

            // 내용 크기가 바뀌면 popup은 새 크기로 다시 자리 잡는다. 닫는 계기는 창 자체가 바뀐 때(회전, 화면 나누기)뿐이다.
            // 키보드는 창이 아니라 안전 영역만 줄인다 — popup 안 검색 칸을 누르면 키보드가 뜨는데, 그것으로 popup이 닫히면 칠 수가 없다.
            if (content_width() != previous_width || content_height() != previous_height)
            {
                if (previous_window_width > 0 && previous_window_height > 0 && (width_ != previous_window_width || height_ != previous_window_height))
                    static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_resized));
                overlay_frame_.reset();
            }
            else if (insets_.left != previous_insets.left || insets_.top != previous_insets.top)
                place_overlays();
            // 키보드가 올라와 아래가 줄었다. 줄어든 크기로 다시 지은 frame이 오면 초점 칸을 다시 드러내 달라고 보낸다 (`render`).
            if (insets_.bottom > previous_insets.bottom && ime_ != nullptr && ime_->target().has_value())
                reveal_pending_ = true;
            else if (insets_.bottom < previous_insets.bottom)
            {
                reveal_pending_ = false;
                follow_ime_focus_ = false;
            }
            if (width_ <= 0 || height_ <= 0)
                return;
            if (renderer_ == nullptr)
                create_renderer();
            else if (width_ != previous_window_width || height_ != previous_window_height)
            {
                std::u8string error {};
                if (renderer_->resize(width_, height_, error) == false)
                    log_error(error);
            }
            dirty_ = true;
            post_metrics();
        }

        // 상태 표시줄·노치·홈 표시줄·키보드가 덮는 가장자리다 (물리 픽셀).
        //  - 몰입 모드면 상태 표시줄은 숨고 홈 표시줄은 세지 않는다 (Android와 같은 뜻). 노치는 그대로 비킨다.
        //  - 앱이 노치 밑까지 그리겠다고 했으면 옆·위의 안전 영역을 비키지 않는다. 위는 보이는 상태 표시줄만 비킨다 — iOS는
        //    상태 표시줄과 노치를 나누어 알리지 않는다.
        [[nodiscard]] edge_insets read_insets() const
        {
            const UIEdgeInsets safe { controller_.view.safeAreaInsets };
            const bool immersive { applied_device_.system_bars == system_bars_mode::immersive };
            const bool under_cutout { applied_device_.draw_under_cutout };
            CGFloat bar { 0 };
            if (immersive == false && scene_ != nil)
                bar = scene_.statusBarManager.statusBarFrame.size.height;
            const CGFloat top { under_cutout ? bar : std::max(safe.top, bar) };
            const CGFloat left { under_cutout ? 0 : safe.left };
            const CGFloat right { under_cutout ? 0 : safe.right };
            const CGFloat bottom { std::max(immersive ? CGFloat { 0 } : safe.bottom, keyboard_overlap_) };
            const auto pixels = [this](const CGFloat points) { return static_cast<int>(std::lround(points * scale_)); };
            return edge_insets { pixels(left), pixels(top), pixels(right), pixels(bottom) };
        }

        [[nodiscard]] int content_width() const noexcept
        {
            return std::max(1, width_ - insets_.left - insets_.right);
        }

        [[nodiscard]] int content_height() const noexcept
        {
            return std::max(1, height_ - insets_.top - insets_.bottom);
        }

        // 앱에 안전 영역의 크기와 배율을 알린다. 같은 값이면 다시 알리지 않는다.
        void post_metrics()
        {
            if (host_ == nullptr || environment_.delegate == nullptr || width_ <= 0 || height_ <= 0)
                return;
            const float width { static_cast<float>(content_width()) };
            const float height { static_cast<float>(content_height()) };
            const luil::edge_insets outside { static_cast<float>(insets_.left), static_cast<float>(insets_.top), static_cast<float>(insets_.right), static_cast<float>(insets_.bottom) };
            const float text_scale { scaled_font_size(12.0f) / 12.0f };
            if (width == reported_width_ && height == reported_height_ && scale_ == reported_scale_ && outside == reported_outside_ && system_.reduce_motion == reported_reduce_motion_
                && text_scale == reported_text_scale_)
                return;
            reported_width_ = width;
            reported_height_ = height;
            reported_scale_ = scale_;
            reported_outside_ = outside;
            reported_reduce_motion_ = system_.reduce_motion;
            reported_text_scale_ = text_scale;
            window_metrics metrics { make_window_metrics(width, height, scale_) };
            metrics.outside = outside;
            metrics.text_scale = text_scale;
            metrics.reduce_motion = system_.reduce_motion;
            if (app_message message { environment_.delegate->make_metrics_message(metrics) }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

        // 시스템의 밝은·어두운 모양, 대비, 움직임·투명도 줄이기를 읽는다.
        //  - iOS에는 사용자가 고르는 강조색이 없다. 앱의 강조색을 그대로 둔다.
        //  - 고대비 색은 시스템이 주지 않아 밝은·어두운 모양에 맞춘 기본 색이다 (Android와 같다).
        void refresh_appearance(UITraitCollection* const traits)
        {
            UITraitCollection* const current { traits != nil ? traits : UITraitCollection.currentTraitCollection };
            system_.prefers_light = current.userInterfaceStyle != UIUserInterfaceStyleDark;
            system_.high_contrast = UIAccessibilityDarkerSystemColorsEnabled() || current.accessibilityContrast == UIAccessibilityContrastHigh;
            system_.contrast_colors = default_high_contrast_colors(system_.prefers_light);
            system_.reduce_motion = UIAccessibilityIsReduceMotionEnabled();
            system_.transparency = UIAccessibilityIsReduceTransparencyEnabled() == NO;
            system_.accent = std::nullopt;
            set_system_accent(std::nullopt);
        }

        // 글자 크기 설정(Dynamic Type)을 변환 표로 옮긴다. 본문 글꼴의 확대를 따른다 — 기본 크기(Large)에서 1배다.
        void refresh_font_sizes(UITraitCollection* const traits)
        {
            UIFontMetrics* const metrics { [UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] };
            UITraitCollection* const current { traits != nil ? traits : UITraitCollection.currentTraitCollection };
            std::vector<float> sizes {};
            sizes.reserve(257);
            for (int size { 0 }; size <= 256; ++size)
            {
                const float value { static_cast<float>([metrics scaledValueForValue:size compatibleWithTraitCollection:current]) };
                if (std::isfinite(value) == false || (size == 0 ? value != 0.0f : value <= sizes.back()))
                    return;
                sizes.push_back(value);
            }
            if (sizes == system_font_sizes_)
                return;
            system_font_sizes_ = sizes;
            static_cast<void>(set_system_font_sizes(std::move(sizes)));
            dirty_ = true;
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
            post_metrics();
            if (view_ != nil)
                view_.keyboardAppearance = system_.prefers_light ? UIKeyboardAppearanceLight : UIKeyboardAppearanceDark;
        }

        // 뒤로 갈 때는 배경 작업 시간을 먼저 쥐고, logic이 그 메시지를 처리하면 놓는다 (`post_app_message_holding`). 앱이 그
        // 메시지를 받아 logic thread에서 저장하는 동안 iOS가 앱을 멈추지 않는다. 메시지가 비었으면 곧바로 놓는다.
        void post_lifecycle(const app_lifecycle lifecycle)
        {
            if (environment_.delegate == nullptr || host_ == nullptr)
                return;
            background_work work { lifecycle == app_lifecycle::background ? host_->hold_background_work() : background_work {} };
            if (app_message message { environment_.delegate->make_lifecycle_message(lifecycle) }; message.empty() == false)
                host_->post_app_message_holding(std::move(message), std::move(work));
        }

    public:
        // 배경 작업 시간이 만료됐다 (메인 thread의 만료 처리기). 뒤에 머문 한 번에 한 번만 앱에 알린다.
        void on_background_expired()
        {
            if (background_expired_ || host_ == nullptr || stopped_ || environment_.delegate == nullptr)
                return;
            background_expired_ = true;
            if (app_message message { environment_.delegate->make_background_expired_message() }; message.empty() == false)
                host_->post_app_message(std::move(message));
        }

    private:

        // 뒤로 가거나 장면이 떨어질 때다. 조합을 확정하고 진행 중인 접촉·키 반복을 거둔다.
        void commit_and_cancel_input()
        {
            if (ime_ != nullptr && ime_->composing())
                ime_->commit_composition();
            cancel_key_repeat();
            close_edit_menu();
            for (const auto& [key, value] : contacts_)
                if (value.device != pointer_device::mouse)
                    post_events(tracker_.cancel(value.pointer_id, now(), {}));
            contacts_.clear();
        }

        void deliver_notification_events()
        {
            if (host_ == nullptr || stopped_ || notification_events_.empty())
                return;
            const auto make = [this](const notification_event& event) { return environment_.delegate != nullptr ? environment_.delegate->make_notification_message(event) : app_message {}; };
            const auto post = [this](app_message message) { return host_->post_app_message_ticketed(std::move(message)); };
            const notification_delivery delivery { luil::deliver_notification_events(notification_events_, make, post) };
            if (delivery.failed_calls > 0)
                log_error(u8"The notification delegate threw an exception.");
        }

        // 그릴 것과 다음에 깰 시각을 정리한다. 이벤트·깨우기·시계가 모두 여기로 온다.
        void pump()
        {
            if (host_ == nullptr || stopped_)
                return;
            update_overlays();
            process_file_dialog();
            process_focus_request();
            // IME 연결은 표면이 있는 동안만 칸에 붙인다.
            if (ime_ != nullptr && view_ != nil)
                ime_->synchronize();
            // 전체 선택이 앱에 닿아 선택이 섰으면 편집 메뉴를 다시 띄운다. 초점이 떠났으면 그만둔다.
            if (reopen_edit_menu_)
            {
                if (focused_text_target().has_value() == false)
                    reopen_edit_menu_ = false;
                else if (const text_input_document document { committed_document() }; document.caret != document.anchor)
                {
                    reopen_edit_menu_ = false;
                    open_edit_menu();
                }
            }
            if (host_->faulted() && fault_reported_ == false)
            {
                // iOS 앱은 스스로 끝나지 않는다. 남기고 그리기를 멈춘다.
                fault_reported_ = true;
                log_error(u8"The logic or input thread stopped on an exception.");
            }
            const auto time { now() };
            if (update_deadline_.has_value() && time >= *update_deadline_)
                dirty_ = true;
            // 뒤에 있는 동안은 그리지 않는다. GPU를 쓸 수 없고 보이지도 않는다. 접근성 계층은 그래도 맞춘다.
            if (visible_ == false || renderer_ == nullptr)
            {
                update_deadline_.reset();
                if (dirty_)
                    update_accessibility(host_->acquire_ui_tree());
            }
            else if (dirty_)
                render();
            arm_timer();
        }

        // 다음에 깰 시각에 시계를 건다. 그림 예고와 초점 요청의 기한 가운데 이른 쪽이다.
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

        // 창 하나가 화면을 덮고 앱은 스스로 끝나지 않으므로 창 명령은 편집 메뉴만 뜻이 있다.
        void execute_command(const ui_command command)
        {
            switch (command)
            {
            case ui_command::text_edit_menu:
                open_edit_menu();
                break;
            case ui_command::window_close:
            case ui_command::application_exit:
                if (warned_exit_ == false)
                {
                    warned_exit_ = true;
                    log_error(u8"iOS apps cannot close their window or exit; the command is ignored.");
                }
                break;
            default:
                break;
            }
        }

        // 찍기·녹화는 실패해도 알리기만 한다 (Win32와 같다).
        void execute_capture(const capture_request& request)
        {
            if (host_ == nullptr || (request.surface.empty() == false && find_overlay(request.surface) == nullptr))
            {
                log_error(u8"The capture target surface is gone.");
                return;
            }
            prepared_frame prepared {};
            prepare(prepared, false);
            if (std::u8string error {}; macos::write_capture(prepared.state, request.path, error) == false)
                log_error(error);
        }

        void execute_record(const record_request& request)
        {
            std::u8string error {};
            const bool done { request.command == record_command::start ? recorder_.start(request.path, error) : recorder_.stop(error) };
            if (done == false)
                log_error(error);
            dirty_ = true;
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
            else if (const auto* const haptic { std::get_if<haptic_request>(&action) }; haptic != nullptr)
                host_->perform_haptic(haptic->feedback);
            else if (const auto* const shot { std::get_if<capture_request>(&action) }; shot != nullptr)
                execute_capture(*shot);
            else if (const auto* const record { std::get_if<record_request>(&action) }; record != nullptr)
                execute_record(*record);
        }

        // 클립보드 요청이다 (ios/ios_clipboard.h). 텍스트 칸의 붙여넣기는 그 칸의 insert 편집으로 보낸다.
        void execute_clipboard(const clipboard_request& request)
        {
            // 파일 목록 클립보드는 없다. 넣기는 아무 일도 없고, 읽기는 "지원 안 함"과 빈 목록으로 답한다 (Android와 같다).
            if (std::holds_alternative<clipboard_files_copy_request>(request))
                return;
            if (const auto* const read { std::get_if<clipboard_files_paste_request>(&request) }; read != nullptr)
            {
                if (environment_.delegate == nullptr || host_ == nullptr)
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
                if (environment_.delegate == nullptr || host_ == nullptr)
                    return;
                if (app_message message { environment_.delegate->make_clipboard_data_message(read_clipboard_data(read->tag)) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
                return;
            }
            if (const auto* const copy { std::get_if<clipboard_copy_request>(&request) }; copy != nullptr)
            {
                write_clipboard_text(copy->text);
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
            std::u8string text { read_clipboard_text() };
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

        // 이벤트를 popup 자리로 옮겨 input thread에 보낸다 (Android host와 같은 길이다).
        void post_events(std::vector<raw_input_event> events)
        {
            if (host_ == nullptr)
                return;
            for (raw_input_event& event : events)
            {
                routed_input routed { router_.route(std::move(event)) };
                // popup 밖의 누름·휠은 닫는 계기다. 누름·휠 자체는 그대로 간다 (Win32와 같다).
                const bool closed_popup { routed.pressed_outside && dismiss_popups(popup_dismiss_reason::pointer_press_outside) };
                if (routed.wheel_outside)
                    static_cast<void>(dismiss_popups(popup_dismiss_reason::wheel_scrolled));
                if (routed.scroll.has_value())
                    scroll_overlay(*routed.scroll);
                for (raw_input_event& value : routed.events)
                {
                    if (auto* const pressed { std::get_if<pointer_pressed_event>(&value) }; pressed != nullptr)
                        pressed->closed_popup = closed_popup;
                    // 조합 중의 누름은 조합을 먼저 확정한다. 확정 글이 먼저 들어가야 누름이 낸 caret 이동이 그 위에 선다.
                    if (std::holds_alternative<pointer_pressed_event>(value) && ime_ != nullptr && ime_->composing())
                        ime_->commit_composition();
                    // 키보드를 내린 뒤 초점을 가진 텍스트 칸을 다시 누르면 키보드를 다시 띄운다.
                    if (const auto* const released { std::get_if<pointer_released_event>(&value) }; released != nullptr && released->device != pointer_device::mouse)
                        request_keyboard_at(released->surface, released->x, released->y);
                    host_->post_raw_input(std::move(value));
                }
            }
        }

        // 터치 하나를 추적기의 표본으로 옮긴다. 좌표는 안전 영역 원점을 뺀 tree의 물리 픽셀이다.
        [[nodiscard]] pointer_sample sample_of(UITouch* const touch, UIEvent* const event, const contact& current, const pointer_phase phase) const
        {
            const CGPoint point { [touch preciseLocationInView:view_] };
            pointer_sample sample {};
            sample.phase = phase;
            sample.device = current.device;
            sample.pointer_id = current.pointer_id;
            sample.in_contact = phase != pointer_phase::up;
            sample.x = static_cast<float>(point.x * scale_) - static_cast<float>(insets_.left);
            sample.y = static_cast<float>(point.y * scale_) - static_cast<float>(insets_.top);
            sample.scale = scale_;
            sample.time = now();
            const UIKeyModifierFlags flags { event.modifierFlags };
            sample.shift = (flags & UIKeyModifierShift) != 0;
            sample.control = (flags & UIKeyModifierCommand) != 0 || (flags & UIKeyModifierControl) != 0;
            sample.alt = (flags & UIKeyModifierAlternate) != 0;
            if (current.device == pointer_device::pen)
            {
                if (touch.maximumPossibleForce > 0)
                    sample.pressure = static_cast<float>(std::clamp<CGFloat>(touch.force / touch.maximumPossibleForce, 0, 1));
                const auto [tilt_x, tilt_y] { pencil_tilt(touch.altitudeAngle, [touch azimuthAngleInView:view_]) };
                sample.tilt_x = tilt_x;
                sample.tilt_y = tilt_y;
            }
            return sample;
        }

        [[nodiscard]] static macos::key_input key_input_of(UIKey* const key, const std::uint16_t carbon, const bool repeat)
        {
            macos::key_input input {};
            input.key_code = carbon;
            NSString* const base { key.charactersIgnoringModifiers };
            if (base.length > 0)
            {
                const std::u8string text { macos::from_ns_string(base) };
                if (text.empty() == false)
                    input.base_character = text::utf8_decode(text, 0).codepoint;
            }
            const UIKeyModifierFlags flags { key.modifierFlags };
            input.command = (flags & UIKeyModifierCommand) != 0;
            input.control = (flags & UIKeyModifierControl) != 0;
            input.option = (flags & UIKeyModifierAlternate) != 0;
            input.shift = (flags & UIKeyModifierShift) != 0;
            input.repeat = repeat;
            input.time = now();
            return input;
        }

        // 앱이 받는 키 하나를 보낸다. 글을 입력하는 중이 아니면 글자 키의 글자도 보낸다 (macOS의 `insertText:` 갈래와 같다).
        void post_key(UIKey* const key, const macos::key_input& input, const bool key_capture)
        {
            std::vector<raw_input_event> events {};
            if (auto value { macos::key_event_for(input, key_capture) }; value.has_value())
                events.push_back(raw_input_event { std::move(*value) });
            const bool editing { ime_ != nullptr && ime_->target().has_value() };
            if (key_capture == false && input.command == false && input.control == false && editing == false)
            {
                // Backspace는 누름이면서 글자다 (Win32의 WM_KEYDOWN 뒤 WM_CHAR U+0008).
                if (macos::backspace_key(input.key_code))
                    events.push_back(raw_input_event { character_typed_event { macos::backspace_character(input), input.time } });
                else if (text_system_key(static_cast<std::uint32_t>(key.keyCode)))
                {
                    const std::u8string text { macos::from_ns_string(key.characters) };
                    for (std::size_t offset { 0 }; offset < text.size();)
                    {
                        const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
                        offset += std::max<std::size_t>(1, decoded.length);
                        if (decoded.codepoint >= 0x20 && decoded.codepoint != 0x7F && macos::function_key_character(decoded.codepoint) == false)
                            events.push_back(raw_input_event { character_typed_event { decoded.codepoint, input.time } });
                    }
                }
            }
            if (events.empty() == false)
                post_events(std::move(events));
        }

        // 앱이 받은 키를 누르고 있는 동안 되풀이한다. 수정자만 바뀌는 키·글자 키는 되풀이하지 않는다 (글자는 시스템이 되풀이한다).
        void start_key_repeat(UIPress* const press, const macos::key_input& input, const bool key_capture)
        {
            cancel_key_repeat();
            if (macos::key_event_for(input, key_capture).has_value() == false)
                return;
            repeat_press_ = press;
            repeat_key_ = repeat_state { input, key_capture };
            const std::shared_ptr<wake_state> state { wake_ };
            repeat_timer_ = [NSTimer timerWithTimeInterval:key_repeat_delay
                                                   repeats:NO
                                                     block:^(NSTimer*) {
                                                       if (state->target != nullptr)
                                                           state->target->begin_repeating();
                                                     }];
            [[NSRunLoop mainRunLoop] addTimer:repeat_timer_ forMode:NSRunLoopCommonModes];
        }

    public:
        void begin_repeating()
        {
            if (repeat_key_.has_value() == false)
                return;
            on_key_repeat();
            const std::shared_ptr<wake_state> state { wake_ };
            repeat_timer_ = [NSTimer timerWithTimeInterval:key_repeat_interval
                                                   repeats:YES
                                                     block:^(NSTimer*) {
                                                       if (state->target != nullptr)
                                                           state->target->on_key_repeat();
                                                     }];
            [[NSRunLoop mainRunLoop] addTimer:repeat_timer_ forMode:NSRunLoopCommonModes];
        }

    private:
        void cancel_key_repeat()
        {
            if (repeat_timer_ != nil)
                [repeat_timer_ invalidate];
            repeat_timer_ = nil;
            repeat_press_ = nil;
            repeat_key_.reset();
        }

        // --- IME 연결 ---

        // IME 연결이 칸의 글을 view의 문서로 넘긴다 (앱이 글을 바꿨다).
        void set_ime_state(const android::ime_state& state)
        {
            if (view_ == nil)
                return;
            NSString* const text { ns_text(state.text) };
            const auto clamp = [length = text.length](const std::int32_t value) { return std::min<NSUInteger>(static_cast<NSUInteger>(std::max(0, value)), length); };
            const NSUInteger start { clamp(state.selection.start) };
            const NSUInteger end { clamp(state.selection.end) };
            const NSRange selection { std::min(start, end), std::max(start, end) - std::min(start, end) };
            NSRange marked { NSNotFound, 0 };
            if (state.composing.defined() && state.composing.start != state.composing.end)
            {
                const NSUInteger begin { clamp(std::min(state.composing.start, state.composing.end)) };
                marked = NSMakeRange(begin, clamp(std::max(state.composing.start, state.composing.end)) - begin);
            }
            [view_ luilSetText:text selection:selection marked:marked];
        }

        // 키보드를 띄우거나 내린다. view는 늘 첫 응답자로 남는다 — 하드웨어 키를 받기 위해서다.
        void show_keyboard(const bool show, const bool secret)
        {
            if (view_ == nil)
                return;
            view_.secureTextEntry = secret;
            view_.luilKeyboardEnabled = show;
            configure_keyboard();
            if (view_.isFirstResponder == NO)
                [view_ becomeFirstResponder];
            [view_ reloadInputViews];
            represent_edit_menu();
        }

        // 키보드를 다시 세우면(`reloadInputViews`) 시스템이 떠 있던 편집 메뉴를 닫는다. 길게 눌러 칸에 초점이 서는 순간 키보드와 메뉴가
        // 함께 오므로, 방금 청한 메뉴면 키보드가 자리 잡은 뒤 다시 띄운다.
        void represent_edit_menu()
        {
            if (edit_menu_requested_.has_value() == false || now() - *edit_menu_requested_ > std::chrono::milliseconds { 1500 })
                return;
            const std::shared_ptr<wake_state> state { wake_ };
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
              if (state->target != nullptr)
                  state->target->open_edit_menu_again();
            });
        }

    public:
        void open_edit_menu_again()
        {
            if (focused_text_target().has_value() == false)
                return;
            edit_menu_requested_.reset();
            open_edit_menu();
            edit_menu_requested_.reset();
        }

    private:

        // 칸의 종류에 맞는 키보드를 고른다. 키보드가 떠 있으면 다시 세운다.
        void configure_keyboard()
        {
            if (view_ == nil)
                return;
            const text_input_options options { input_options() };
            const text_input_document document { committed_document() };
            UIKeyboardType type { UIKeyboardTypeDefault };
            switch (options.kind)
            {
            case text_input_kind::number:
                type = options.signed_number ? UIKeyboardTypeNumbersAndPunctuation : UIKeyboardTypeNumberPad;
                break;
            case text_input_kind::decimal:
                type = options.signed_number ? UIKeyboardTypeNumbersAndPunctuation : UIKeyboardTypeDecimalPad;
                break;
            case text_input_kind::phone:
                type = UIKeyboardTypePhonePad;
                break;
            case text_input_kind::email:
                type = UIKeyboardTypeEmailAddress;
                break;
            case text_input_kind::url:
                type = UIKeyboardTypeURL;
                break;
            default:
                break;
            }
            UIReturnKeyType action { UIReturnKeyDone };
            switch (options.action)
            {
            case text_input_action::search:
                action = UIReturnKeySearch;
                break;
            case text_input_action::go:
                action = UIReturnKeyGo;
                break;
            case text_input_action::send:
                action = UIReturnKeySend;
                break;
            case text_input_action::next:
            case text_input_action::previous:
                action = UIReturnKeyNext;
                break;
            case text_input_action::none:
                action = UIReturnKeyDefault;
                break;
            default:
                break;
            }
            // 여러 줄 칸은 Return이 줄 바꿈이다.
            if (document.multiline)
                action = UIReturnKeyDefault;
            const bool plain { document.secret == false && options.kind == text_input_kind::text };
            view_.keyboardType = type;
            view_.returnKeyType = action;
            view_.autocorrectionType = plain ? UITextAutocorrectionTypeDefault : UITextAutocorrectionTypeNo;
            view_.spellCheckingType = plain ? UITextSpellCheckingTypeDefault : UITextSpellCheckingTypeNo;
            view_.autocapitalizationType = plain ? UITextAutocapitalizationTypeSentences : UITextAutocapitalizationTypeNone;
            view_.smartQuotesType = plain ? UITextSmartQuotesTypeDefault : UITextSmartQuotesTypeNo;
            view_.smartDashesType = plain ? UITextSmartDashesTypeDefault : UITextSmartDashesTypeNo;
            view_.secureTextEntry = document.secret;
            if (view_.isFirstResponder)
            {
                [view_ reloadInputViews];
                represent_edit_menu();
            }
        }

        void submit_text()
        {
            const text_location location { focused_text_location() };
            if (const auto* const element { dynamic_cast<const text_input_element*>(location.element) }; element != nullptr && element->config().submit)
            {
                for (input_action& action : element->config().submit(ui_action_context { element->id() }))
                    dispatch(std::move(action));
                return;
            }
            const text_input_action action { input_options().action };
            if (action == text_input_action::none)
                return;
            if (action == text_input_action::next || action == text_input_action::previous)
                host_->post_raw_input(key_pressed_event { .key = key_code::tab, .shift = action == text_input_action::previous, .time = now() });
            else
                host_->post_raw_input(key_pressed_event { key_code::enter, false, false, false, false, now() });
        }

        void request_keyboard_at(const std::u8string& surface, const float x, const float y)
        {
            if (ime_ == nullptr || ime_->target().has_value() == false || host_->acquire_interaction().focused_surface != surface)
                return;
            const text_location location { focused_text_location() };
            if (location.element != nullptr && location.tree->hit_test(x, y) == location.element)
                ime_->request_keyboard();
        }

        [[nodiscard]] text_location focused_text_location() const
        {
            text_location location {};
            if (host_ == nullptr || focused_text_target().has_value() == false)
                return location;
            const interaction_snapshot interaction { host_->acquire_interaction() };
            if (interaction.focused_surface.empty())
            {
                location.tree = host_->acquire_ui_tree();
                location.origin_x = static_cast<float>(insets_.left);
                location.origin_y = static_cast<float>(insets_.top);
            }
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

        // --- 편집 메뉴 ---
        // 텍스트 칸을 길게 누르면 시스템 편집 메뉴를 띄운다 (iOS 16부터 `UIEditMenuInteraction`, 그 전은 `UIMenuController`).
        //  - 시스템 메뉴의 붙여넣기는 사용자가 허락한 붙여넣기다. 다른 앱(앞선 실행 포함)이 넣은 글도 묻지 않고 읽힌다. 앱이 그린
        //    메뉴에서 읽으면 iOS가 거절한다 (`PBErrorDomain` 13 "Operation not authorized").
        //  - 항목은 view의 표준 편집 동작(`copy:`·`cut:`·`paste:`·`selectAll:`)이고, 켜고 끄는 규칙은 Android의 그린 메뉴와 같다
        //    (`edit_action_allowed`). caret이 링크 안이면 링크 열기·주소 복사가 더 선다.

        void close_edit_menu()
        {
            reopen_edit_menu_ = false;
            if (edit_menu_open_ == false)
                return;
            edit_menu_open_ = false;
            if (@available(iOS 16.0, *))
            {
                if (edit_interaction_ != nil)
                    [(UIEditMenuInteraction*)edit_interaction_ dismissMenu];
            }
#if LUIL_IOS_BEFORE_16
            else
                [[UIMenuController sharedMenuController] hideMenu];
#endif
        }

        void open_edit_menu()
        {
            if (view_ == nil || focused_text_target().has_value() == false)
                return;
            // 조합 중이면 먼저 확정한다. 그 뒤 전체 선택을 같은 앱 inbox에 넣는다 (Android와 같다).
            if (ime_->composing())
            {
                ime_->commit_composition();
                if (environment_.policy != nullptr)
                    dispatch(environment_.policy->make_text_edit_action(text_edit_request { .target = *focused_text_target(), .command = text::text_edit_command::select_all }));
            }
            const text_location location { focused_text_location() };
            if (location.element == nullptr || scale_ <= 0.0f)
                return;
            const rect_f bounds { location.element->bounds() };
            const CGRect anchor { CGRectMake((location.origin_x + bounds.x) / scale_, (location.origin_y + bounds.y) / scale_, bounds.width / scale_, bounds.height / scale_) };
            // 편집 동작은 첫 응답자에게 간다. 사용자가 키보드를 내려 놓았으면 되찾는다.
            if (view_.isFirstResponder == NO)
                [view_ becomeFirstResponder];
            edit_menu_open_ = true;
            edit_menu_requested_ = now();
            if (@available(iOS 16.0, *))
            {
                if (edit_interaction_ == nil)
                    return;
                UIEditMenuConfiguration* const configuration { [UIEditMenuConfiguration configurationWithIdentifier:nil sourcePoint:CGPointMake(CGRectGetMidX(anchor), CGRectGetMinY(anchor))] };
                [(UIEditMenuInteraction*)edit_interaction_ presentEditMenuWithConfiguration:configuration];
            }
#if LUIL_IOS_BEFORE_16
            else
            {
                UIMenuController* const menu { [UIMenuController sharedMenuController] };
                NSMutableArray<UIMenuItem*>* const items { [NSMutableArray array] };
                for (const auto& [key, label] : link_actions())
                    [items addObject:[[UIMenuItem alloc] initWithTitle:ns_text(label) action:key == u8"open-link" ? NSSelectorFromString(@"luilOpenLink:") : NSSelectorFromString(@"luilCopyLink:")]];
                menu.menuItems = items;
                [menu showMenuFromView:view_ rect:anchor];
            }
#endif
        }

    public:
        // 그 편집 동작을 지금 할 수 있는가 (`canPerformAction:withSender:`). 키는 앱의 그린 메뉴와 같다 (`copy`·`cut`·`paste`·`all`·
        // `open-link`·`copy-link`).
        [[nodiscard]] bool edit_action_allowed(const std::u8string_view key) const
        {
            if (ime_ == nullptr || ime_->target().has_value() == false)
                return false;
            const text_location location { focused_text_location() };
            const auto value { location.element != nullptr ? location.element->text_input() : std::nullopt };
            if (value.has_value() == false)
                return false;
            const bool selected { value->caret != value->anchor && value->composing == false };
            const bool link { text_edit_menu_has_link(*value) };
            if (key == u8"copy")
                return selected && value->secret == false;
            if (key == u8"cut")
                return selected && value->secret == false && value->read_only == false;
            if (key == u8"paste")
                return value->read_only == false && [UIPasteboard generalPasteboard].hasStrings;
            if (key == u8"all")
                return value->text.empty() == false && value->composing == false;
            if (key == u8"open-link")
                return link && value->activate_link != nullptr && *value->activate_link != nullptr;
            if (key == u8"copy-link")
                return link;
            return false;
        }

        // 시스템 메뉴·단축키가 낸 편집 동작이다.
        void edit_action(const std::u8string& key)
        {
            execute_edit_menu(key);
            pump();
        }

        // 링크 줄의 항목이다 (키와 글). 링크 밖이면 비었다. 글은 라이브러리 표의 편집 메뉴 문구다.
        [[nodiscard]] std::vector<std::pair<std::u8string, std::u8string>> link_actions() const
        {
            std::vector<std::pair<std::u8string, std::u8string>> actions {};
            const std::string os_language { user_ui_language() };
            const string_lookup strings { text_edit_menu_strings(std::u8string { os_language.begin(), os_language.end() }) };
            if (edit_action_allowed(u8"open-link"))
                actions.emplace_back(u8"open-link", std::u8string { library_text::text(strings, library_text::key::text_edit_menu_open_link) });
            if (edit_action_allowed(u8"copy-link"))
                actions.emplace_back(u8"copy-link", std::u8string { library_text::text(strings, library_text::key::text_edit_menu_copy_link) });
            return actions;
        }

        void edit_menu_dismissed() noexcept
        {
            edit_menu_open_ = false;
        }

    private:
        void execute_edit_menu(const std::u8string& key)
        {
            const auto target { focused_text_target() };
            const text_location location { focused_text_location() };
            const auto value { location.element != nullptr ? location.element->text_input() : std::nullopt };
            if (target.has_value() == false || value.has_value() == false || environment_.policy == nullptr)
                return;
            if (value->composing && ime_ != nullptr)
                ime_->commit_composition();
            if (key == u8"copy" || key == u8"cut")
            {
                if (value->secret || value->caret == value->anchor)
                    return;
                const std::size_t begin { std::min({ value->caret, value->anchor, value->text.size() }) };
                const std::size_t end { std::min(std::max(value->caret, value->anchor), value->text.size()) };
                write_clipboard_text(std::u8string { value->text.substr(begin, end - begin) });
                note_clipboard_written();
                if (key == u8"cut")
                    dispatch(environment_.policy->make_text_edit_action(text_edit_request { .target = *target, .command = text::text_edit_command::cut }));
            }
            else if (key == u8"paste")
                execute_clipboard(clipboard_paste_request { *target, value->multiline });
            else if (key == u8"all")
            {
                dispatch(environment_.policy->make_text_edit_action(text_edit_request { .target = *target, .command = text::text_edit_command::select_all }));
                // 시스템 메뉴는 누르면 닫힌다. 앱이 선택을 넓힌 frame이 오면 복사할 수 있게 다시 띄운다 (`pump`).
                edit_menu_open_ = false;
                reopen_edit_menu_ = true;
                return;
            }
            else if (key == u8"copy-link")
            {
                if (value->link.empty())
                    return;
                write_clipboard_text(std::u8string { value->link });
                note_clipboard_written();
            }
            else if (key == u8"open-link")
            {
                if (value->link.empty() || value->activate_link == nullptr || *value->activate_link == nullptr)
                    return;
                dispatch((*value->activate_link)(value->link));
            }
            close_edit_menu();
        }

        // --- 파일 대화 상자·초점 요청 ---

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
            if (controller_ == nil || file_dialog_gate_.try_begin(frame->file_dialog->id) == false)
                return;
            // 선택기가 뜨면 popup은 초점을 잃어 닫힌다 (`on_active`).
            const std::shared_ptr<wake_state> state { wake_ };
            run_file_dialog(controller_, *frame->file_dialog, [state](file_dialog_result result) {
                if (state->target != nullptr)
                    state->target->complete_file_dialog(result);
            });
        }

        // 게시된 초점 요청을 받고, 대상이 섰으면 한 번 이행한다 (deferred-focus-design.md). 표면은 주 표면과 그 위의 popup뿐이다.
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

        // --- popup ---

        // frame의 popup을 표면 위의 layer로 세운다. frame이 바뀔 때만 다시 세운다 (Android host와 같다).
        void update_overlays()
        {
            const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
            if (frame == overlay_frame_ && frame != nullptr)
                return;
            overlay_frame_ = frame;
            auto previous { std::move(overlays_) };
            overlays_.clear();
            if (frame != nullptr)
            {
                const float scale { scale_ };
                const int limit_width { content_width() };
                const int limit_height { content_height() };
                std::vector<popup_placement> logical {};
                for (const auto& popup : frame->popups)
                    logical.push_back({ popup.id, 0, 0, popup.tree != nullptr && popup.width > 0.0f ? 1 : 0, popup.height > 0.0f ? 1 : 0, popup.anchor });
                const std::vector<std::u8string> roots { u8"" };
                for (const auto index : order_popup_placements(logical, roots))
                {
                    const ui_popup& popup { frame->popups[index] };
                    const int width { std::min(limit_width, static_cast<int>(std::lround(popup.width * scale))) };
                    const int full_height { static_cast<int>(std::lround(popup.height * scale)) };
                    const int height { std::min(limit_height, full_height) };
                    const float maximum { static_cast<float>(full_height - height) };
                    const auto old { std::ranges::find(previous, popup.id, &overlay_entry::id) };
                    const float offset { old != previous.end() ? std::clamp(old->scroll_y, 0.0f, maximum) : 0.0f };
                    const std::optional<rect_f> reported_bounds { popup.placed != nullptr && old != previous.end() && old->anchor == popup.anchor ? old->reported_bounds : std::nullopt };
                    overlays_.push_back(overlay_entry {
                        popup.id, popup.tree, { 0, 0, width, height }, popup.border, popup.dismiss, false, popup.anchor, static_cast<int>(std::lround(popup.x * scale)),
                        static_cast<int>(std::lround(popup.y * scale)), offset, maximum, old != previous.end() ? old->followed : std::nullopt, popup.placed, reported_bounds });
                }
                if (frame->windows.empty() == false && warned_windows_ == false)
                {
                    warned_windows_ = true;
                    log_error(u8"Secondary windows (ui_frame::windows) are not supported on iOS and are ignored.");
                }
                if (frame->docked_surfaces.empty() == false && warned_docked_ == false)
                {
                    warned_docked_ = true;
                    log_error(u8"Docked surfaces (ui_frame::docked_surfaces) are not supported on iOS and are ignored.");
                }
            }
            for (auto& old : previous)
                if (old.anchor.empty() == false && std::none_of(overlays_.begin(), overlays_.end(), [&old](const auto& entry) { return entry.id == old.anchor; }))
                    if (auto action { take_popup_dismiss_action(old.dismiss, old.requested, popup_dismiss_reason::anchor_closed) }; action.has_value())
                        dispatch(std::move(*action));
            place_overlays();
            if (webviews_ != nullptr)
                webviews_->synchronize(frame != nullptr ? std::span<const ui_webview> { frame->webviews } : std::span<const ui_webview> {});
            apply_device_request(frame != nullptr ? frame->device : device_request {});
            dirty_ = true;
        }

        void place_overlays()
        {
            const int limit_width { content_width() };
            const int limit_height { content_height() };
            std::vector<overlay_area> areas {};
            areas.reserve(overlays_.size());
            for (overlay_entry& entry : overlays_)
            {
                const overlay_entry* const parent { entry.anchor.empty() ? nullptr : find_overlay(entry.anchor) };
                const int origin_x { parent != nullptr ? parent->bounds.x - insets_.left : 0 };
                const float origin_y { parent != nullptr ? parent->content_origin_y() - static_cast<float>(insets_.top) : 0.0f };
                int desired_x { origin_x + entry.anchor_x };
                if (parent != nullptr && desired_x + entry.bounds.width > limit_width)
                    desired_x = origin_x - entry.bounds.width;
                const int x { std::clamp(desired_x, 0, std::max(0, limit_width - entry.bounds.width)) };
                const int y { std::clamp(static_cast<int>(std::lround(origin_y + static_cast<float>(entry.anchor_y))), 0, std::max(0, limit_height - entry.bounds.height)) };
                entry.bounds.x = insets_.left + x;
                entry.bounds.y = insets_.top + y;
                areas.push_back(overlay_area {
                    entry.id, { static_cast<float>(x), static_cast<float>(y), static_cast<float>(entry.bounds.width), static_cast<float>(entry.bounds.height) }, entry.scroll_y,
                    entry.max_scroll_y, entry.tree.get(), scale_ });
                if (entry.placed != nullptr && host_ != nullptr)
                {
                    // 자식 popup의 기준은 부모의 보이는 판이 아니라 스크롤을 적용한 내용 원점이다.
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

        void reveal_overlay_focus(const interaction_snapshot& interaction, const bool ime_reveal)
        {
            bool moved { false };
            for (overlay_entry& entry : overlays_)
            {
                if (entry.max_scroll_y <= 0.0f)
                    continue;
                const auto local { interaction_for_surface(interaction, entry.id) };
                const bool show_text { follow_ime_focus_ && local.focused_input != ui_element_id {} };
                ui_element_id target { show_text ? local.focused_input : local.menu_highlight };
                if (target == ui_element_id {} && local.focus_visible)
                    target = local.focused;
                const ui_element* const element { entry.tree->find(target) };
                const auto bounds { element != nullptr ? entry.tree->visible_bounds(*element) : std::nullopt };
                if (target == ui_element_id {} || bounds.has_value() == false)
                {
                    entry.followed.reset();
                    continue;
                }
                const overlay_entry::followed_focus next { target, bounds->y, bounds->height, entry.bounds.height };
                if ((ime_reveal == false || show_text == false) && entry.followed == next)
                    continue;
                entry.followed = next;
                const float height { static_cast<float>(entry.bounds.height) };
                if (target == local.hovered && show_text == false && bounds->y < entry.scroll_y + height && bounds->y + bounds->height > entry.scroll_y)
                    continue;
                const float offset { overlay_scroll_to_show(entry.scroll_y, entry.max_scroll_y, height, *bounds) };
                if (offset != entry.scroll_y)
                {
                    entry.scroll_y = offset;
                    moved = true;
                }
            }
            if (moved)
                place_overlays();
        }

        [[nodiscard]] const overlay_entry* find_overlay(const std::u8string& id) const
        {
            const auto found { std::ranges::find_if(overlays_, [&id](const overlay_entry& entry) { return entry.id == id; }) };
            return found != overlays_.end() ? &*found : nullptr;
        }

        // 닫힘 계기 하나를 popup들에 묻는다. 하나라도 닫자는 메시지를 냈으면 참이다.
        bool dismiss_popups(const popup_dismiss_reason reason)
        {
            bool dismissed { edit_menu_open_ };
            if (edit_menu_open_)
            {
                close_edit_menu();
                if (reason == popup_dismiss_reason::escape_key)
                    return true;
            }
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

        // --- 기기 요청 ---

        // 앱이 기기에 바라는 것 중 바뀐 것만 UIKit에 옮긴다 (device-request-design.md).
        //  - 방향은 view controller의 방향 묶음이다. iPad에서 여러 창을 받는 앱(전체 화면을 요구하지 않는 앱)은 시스템이 방향
        //    고정을 따르지 않는다.
        //  - 볼륨 키는 받을 길이 없어 무시한다.
        void apply_device_request(const device_request& request)
        {
            if (controller_ == nil)
                return;
            bool remeasure { false };
            if (request.orientation != applied_device_.orientation)
            {
                applied_device_.orientation = request.orientation;
                supported_orientations_ = orientation_mask(request.orientation, scene_ != nil ? scene_.interfaceOrientation : UIInterfaceOrientationUnknown);
                if (@available(iOS 16.0, *))
                {
                    [controller_ setNeedsUpdateOfSupportedInterfaceOrientations];
                    if (scene_ != nil && request.orientation != screen_orientation::system)
                    {
                        UIWindowSceneGeometryPreferencesIOS* const preferences { [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:supported_orientations_] };
                        [scene_ requestGeometryUpdateWithPreferences:preferences errorHandler:nil];
                    }
                }
#if LUIL_IOS_BEFORE_16
                else
                    [UIViewController attemptRotationToDeviceOrientation];
#endif
            }
            if (request.system_bars != applied_device_.system_bars)
            {
                applied_device_.system_bars = request.system_bars;
                [controller_ setNeedsStatusBarAppearanceUpdate];
                [controller_ setNeedsUpdateOfHomeIndicatorAutoHidden];
                [controller_ setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
                remeasure = true;
            }
            if (request.draw_under_cutout != applied_device_.draw_under_cutout)
            {
                applied_device_.draw_under_cutout = request.draw_under_cutout;
                remeasure = true;
            }
            applied_device_.volume_keys = request.volume_keys;
            if (remeasure)
                refresh_metrics();
        }

        // 상태 표시줄의 글자를 그린 바탕에 맞춘다. 바뀔 때만 다시 묻게 한다.
        void update_status_bar(const color_theme theme)
        {
            const bool light { theme == color_theme::light || (theme == color_theme::high_contrast && system_.prefers_light) };
            if (status_bar_light_.has_value() && *status_bar_light_ == light)
                return;
            status_bar_light_ = light;
            [controller_ setNeedsStatusBarAppearanceUpdate];
        }

        // --- 웹뷰 ---

        // frame의 웹뷰마다 자리표의 자리를 웹뷰에 앉히고, 아래층이 비울 구멍을 낸다 (macOS `apply_webviews`와 같은 규칙).
        //  - tree는 안전 영역 원점에서 그려지므로 자리는 그만큼 옮긴 표면 좌표다. 가림 표본은 tree 좌표로 본다.
        [[nodiscard]] std::span<const pixel_rect> apply_webviews(const ui_tree* const tree, const interaction_snapshot& interaction)
        {
            webview_holes_.clear();
            if (webviews_ == nullptr || overlay_frame_ == nullptr)
                return webview_holes_;
            const ui_element_id focused { interaction_for_surface(interaction, {}).focused };
            for (const ui_webview& want : overlay_frame_->webviews)
            {
                if (want.anchor.empty() == false || want.id.empty())
                    continue;
                const ui_element_id placeholder_id { ui_element_kind::webview, want.id };
                const auto* const placeholder { tree != nullptr ? dynamic_cast<const webview_element*>(tree->find(placeholder_id)) : nullptr };
                const std::optional<rect_f> visible { placeholder != nullptr ? tree->visible_bounds(*placeholder) : std::nullopt };
                std::optional<rect_f> surface_visible { visible };
                if (surface_visible.has_value())
                {
                    surface_visible->x += static_cast<float>(insets_.left);
                    surface_visible->y += static_cast<float>(insets_.top);
                }
                const float tree_scale { placeholder != nullptr ? placeholder->scale() : scale_ };
                const bool occluded { tree == nullptr || webview_occluded(*tree, placeholder, visible, surface_visible) };
                const webview_layout layout { plan_webview_layout(surface_visible, width_, height_, occluded, tree_scale, want.layer) };
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

        [[nodiscard]] bool webview_occluded(const ui_tree& tree, const ui_element* const placeholder, const std::optional<rect_f>& visible, const std::optional<rect_f>& surface_visible) const
        {
            if (placeholder == nullptr || visible.has_value() == false || surface_visible.has_value() == false)
                return true;
            const auto overlaps = [&surface_visible](const pixel_rect& area) {
                return static_cast<float>(area.x) < surface_visible->x + surface_visible->width && surface_visible->x < static_cast<float>(area.x + area.width)
                    && static_cast<float>(area.y) < surface_visible->y + surface_visible->height && surface_visible->y < static_cast<float>(area.y + area.height);
            };
            for (const overlay_entry& entry : overlays_)
                if (overlaps(entry.bounds))
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

        // --- 그리기 ---

        // 그리기와 남기기가 같은 상태를 짓는다. 웹뷰의 자리 맞춤과 구멍은 화면 그리기에서만 한다 (`with_webviews`).
        void prepare(prepared_frame& prepared, const bool with_webviews)
        {
            frame_state& state { prepared.state };
            state.width = content_width();
            state.height = content_height();
            state.origin_x = insets_.left;
            state.origin_y = insets_.top;
            state.dpi_scale = scale_;
            state.reduce_motion = system_.reduce_motion;

            prepared.frame = host_->acquire_frame();
            const ui_frame* const frame { prepared.frame.get() };
            const interaction_snapshot interaction { host_->acquire_interaction() };
            state.interaction = interaction_for_surface(interaction, std::u8string {});
            prepared.tree = host_->acquire_ui_tree();
            state.tree = prepared.tree.get();
            // 키보드가 올라온 뒤 줄어든 크기로 지은 frame까지 기다려 내부 스크롤과 popup을 함께 들인다.
            const bool ime_reveal { with_webviews && reveal_pending_ && prepared.tree != nullptr && prepared.tree->root() != nullptr
                && std::abs(prepared.tree->root()->bounds().height - static_cast<float>(content_height())) < 1.0f };
            if (ime_reveal)
            {
                reveal_pending_ = false;
                follow_ime_focus_ = true;
                host_->post_raw_input(focus_reveal_event { interaction.focused_surface });
            }
            if (with_webviews)
                reveal_overlay_focus(interaction, ime_reveal);
            prepared.layers.reserve(overlays_.size());
            for (const overlay_entry& entry : overlays_)
                prepared.layers.push_back(overlay_layer { entry.tree.get(), entry.bounds, entry.border, interaction_for_surface(interaction, entry.id), entry.scroll_y });
            state.overlays = prepared.layers;
            if (with_webviews)
                state.holes = apply_webviews(prepared.tree.get(), interaction);
            const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
            state.theme = resolve_color_theme(appearance.theme, system_.high_contrast, system_.prefers_light);
            state.high_contrast = system_.contrast_colors;
            state.accent_id = appearance.accent_id;
            state.style = frame != nullptr ? frame->style.get() : nullptr;
            state.inspector = frame != nullptr ? &frame->inspector : nullptr;
            set_single_line_break(frame_style(state).text.line_break);

            const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
            set_configured_fonts(fonts.ui_family, fonts.code_family);
            prepared.code = configured_code_typeface();
            state.code_typeface = prepared.code.get();
            state.fonts = &font_resolver_;
        }

        void render()
        {
            prepared_frame prepared {};
            prepare(prepared, true);
            update_accessibility(prepared.tree);
            update_status_bar(prepared.state.theme);
            const frame_state& state { prepared.state };

            std::u8string error {};
            const bool drawn { renderer_->render(state, error) };
            if (drawn == false || error.empty() == false)
                log_error(error);
            report_renderer();
            if (drawn && recorder_.recording())
                recorder_.collect(state);
            // GPU를 요구한 모드는 물러서지 않는다. 그릴 길이 없으므로 렌더러를 놓는다 (앱은 끝나지 않는다).
            if (drawn == false && renderer_mode_requires_gpu(config_.renderer))
                renderer_.reset();
            dirty_ = false;

            // 시간이 흘러야 바뀌는 그림(애니메이션·tooltip 지연·caret 깜빡임)의 다음 시각이다.
            const auto time { now() };
            const update_context update { .now = time, .reduce_motion = system_.reduce_motion };
            update_deadline_ = prepared.tree != nullptr ? prepared.tree->next_update(update, state.interaction) : std::nullopt;
            for (const overlay_layer& layer : prepared.layers)
                if (layer.tree != nullptr)
                    if (const auto next { layer.tree->next_update(update, layer.interaction) }; next.has_value())
                        if (update_deadline_.has_value() == false || *next < *update_deadline_)
                            update_deadline_ = next;
            if (update_deadline_.has_value() && *update_deadline_ <= time)
                update_deadline_ = time + continuous_repaint_interval;
        }

        struct repeat_state
        {
            macos::key_input input {};
            bool key_capture { false };
        };

        application_config config_;
        application_environment environment_;
        std::shared_ptr<wake_state> wake_ { std::make_shared<wake_state>() };
        std::unique_ptr<app_host> host_ {};
        std::unique_ptr<renderer_host> renderer_ {};
        std::optional<renderer_backend> reported_backend_ {};
        bool gpu_abandoned_ { false };
        UIWindowScene* scene_ { nil };
        UIWindow* window_ { nil };
        LuilViewController* controller_ { nil };
        LuilView* view_ { nil };
        std::vector<id> observers_ {};
        NSTimer* timer_ { nil };
        std::optional<std::chrono::steady_clock::time_point> timer_deadline_ {};
        std::optional<std::chrono::steady_clock::time_point> update_deadline_ {};
        std::vector<overlay_entry> overlays_ {};
        // 시스템 편집 메뉴다 (iOS 16부터 `UIEditMenuInteraction`). 떠 있는가, 전체 선택 뒤 다시 띄울 것인가다.
        id edit_interaction_ { nil };
        bool edit_menu_open_ { false };
        // 마지막으로 편집 메뉴를 청한 때다. 키보드가 그 직후 다시 서면 메뉴를 다시 띄운다 (`represent_edit_menu`).
        std::optional<std::chrono::steady_clock::time_point> edit_menu_requested_ {};
        bool reopen_edit_menu_ { false };
        std::shared_ptr<const ui_frame> overlay_frame_ {};
        overlay_input_router router_ {};
        pointer_sequence_tracker tracker_ {};
        std::unordered_map<const void*, contact> contacts_ {};
        std::uint32_t next_pointer_id_ { 1 };
        CGPoint scroll_translation_ { CGPointZero };
        std::unique_ptr<android::ime_session> ime_ {};
        // 키보드가 올라와 줄어든 frame이 오면 초점 칸을 드러내 달라고 보낸다.
        bool reveal_pending_ { false };
        // 키보드가 보이는 동안 내부 스크롤이 뒤늦게 옮긴 초점 칸도 따라간다.
        bool follow_ime_focus_ { false };
        UIPress* repeat_press_ { nil };
        std::optional<repeat_state> repeat_key_ {};
        NSTimer* repeat_timer_ { nil };
        file_dialog_gate file_dialog_gate_ {};
        focus_request_tracker focus_tracker_ {};
        notification_queue notification_events_ {};
        std::unique_ptr<webview_host> webviews_ {};
        std::vector<pixel_rect> webview_holes_ {};
        ui_element_id last_webview_focus_ {};
        std::unique_ptr<accessibility_bridge> accessibility_ {};
        macos::frame_recorder recorder_ {};
        registry_font_resolver font_resolver_ {};
        system_appearance system_ {};
        system_appearance_observer appearance_observer_ {};
        std::vector<float> system_font_sizes_ {};
        device_request applied_device_ {};
        UIInterfaceOrientationMask supported_orientations_ { UIInterfaceOrientationMaskAll };
        std::optional<bool> status_bar_light_ {};
        std::optional<std::uint32_t> owned_clipboard_sequence_ {};
        std::optional<std::uint32_t> reported_clipboard_sequence_ {};
        int width_ { 0 };
        int height_ { 0 };
        edge_insets insets_ {};
        CGFloat keyboard_overlap_ { 0 };
        float scale_ { 1.0f };
        float reported_width_ { -1.0f };
        float reported_height_ { -1.0f };
        float reported_scale_ { -1.0f };
        float reported_text_scale_ { -1.0f };
        luil::edge_insets reported_outside_ {};
        bool reported_reduce_motion_ { false };
        bool dirty_ { true };
        bool visible_ { false };
        bool stopped_ { false };
        bool fault_reported_ { false };
        // 이번에 뒤에 머문 동안 배경 작업 시간의 만료를 앱에 알렸는가다 (`on_background_expired`).
        bool background_expired_ { false };
        bool warned_windows_ { false };
        bool warned_docked_ { false };
        bool warned_exit_ { false };
    };

    namespace {
        // `UIApplicationMain`이 delegate를 이름으로 만들어 인자를 넘길 길이 없다. 앱 하나는 여기 산다.
        std::unique_ptr<application> current_application {};
    }

    void apply_debug_arguments(application_config& config, const int argc, const char* const* const argv)
    {
        for (int index { 1 }; index < argc; ++index)
        {
            if (argv[index] == nullptr)
                continue;
            const std::string_view argument { argv[index] };
            if (argument.starts_with("--renderer="))
            {
                const std::string_view value { argument.substr(std::strlen("--renderer=")) };
                if (const auto mode { parse_renderer_mode(std::u8string_view { reinterpret_cast<const char8_t*>(value.data()), value.size() }) }; mode.has_value())
                    config.renderer = *mode;
            }
            else if (argument == "--simulate-gpu-failure")
                config.simulate_gpu_failure = true;
            else if (argument.starts_with("--simulate-gpu-loss-after-frames="))
            {
                const std::string_view value { argument.substr(std::strlen("--simulate-gpu-loss-after-frames=")) };
                if (int frames { 0 }; std::from_chars(value.data(), value.data() + value.size(), frames).ec == std::errc {} && frames >= 0)
                    config.simulate_gpu_loss_after_frames = frames;
            }
        }
    }

    int run_application(const int argc, char** const argv, const application_config& config, const application_environment& environment)
    {
        @autoreleasepool
        {
            if ([NSThread isMainThread] == NO)
            {
                log_error(u8"run_application must be called on the main thread.");
                return 1;
            }
            if (environment.driver == nullptr)
            {
                log_error(u8"run_application needs a logic driver.");
                return 1;
            }
            current_application = std::make_unique<application>(config, environment);
        }
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([LuilApplicationDelegate class]));
    }

    std::vector<std::u8string> command_line_arguments(const int argc, const char* const* const argv)
    {
        std::vector<std::u8string> arguments {};
        for (int index { 0 }; index < argc; ++index)
            if (argv[index] != nullptr)
                arguments.push_back(macos::widen(argv[index]));
        return arguments;
    }

    [[nodiscard]] application* running_application() noexcept
    {
        return current_application.get();
    }
}

@implementation LuilTextPosition

+ (instancetype)positionWithIndex:(NSUInteger)index
{
    LuilTextPosition* const position { [LuilTextPosition new] };
    position->_index = index;
    return position;
}

@end

@implementation LuilTextRange {
    NSRange _value;
}

+ (instancetype)rangeWithRange:(NSRange)range
{
    LuilTextRange* const value { [LuilTextRange new] };
    value->_value = range;
    return value;
}

- (NSRange)range
{
    return _value;
}

- (UITextPosition*)start
{
    return [LuilTextPosition positionWithIndex:_value.location];
}

- (UITextPosition*)end
{
    return [LuilTextPosition positionWithIndex:NSMaxRange(_value)];
}

- (BOOL)isEmpty
{
    return _value.length == 0;
}

@end

@implementation LuilView {
    NSMutableString* _text;
    NSRange _selection;
    NSRange _marked;
    NSDictionary* _markedStyle;
    UITextInputStringTokenizer* _tokenizer;
    UIView* _emptyInput;
    // 글 입력 체계의 호출 안이다. 그 안에서 host가 문서를 되돌려 놓으면 호출이 끝난 뒤에 알린다.
    int _systemEditDepth;
    BOOL _pendingNotify;
    NSMutableSet<UIPress*>* _forwarded;
}

@synthesize inputDelegate = _inputDelegate;

+ (Class)layerClass
{
    return [CALayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        _text = [NSMutableString string];
        _selection = NSMakeRange(0, 0);
        _marked = NSMakeRange(NSNotFound, 0);
        _forwarded = [NSMutableSet set];
        _emptyInput = [[UIView alloc] initWithFrame:CGRectZero];
        _tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
        self.multipleTouchEnabled = YES;
        self.opaque = YES;
        self.autocorrectionType = UITextAutocorrectionTypeDefault;
        self.spellCheckingType = UITextSpellCheckingTypeDefault;
        self.autocapitalizationType = UITextAutocapitalizationTypeSentences;
        // 키보드 위의 단축 막대(실행 취소·붙여넣기)는 앱의 편집 메뉴와 겹친다. 비워 둔다.
        self.inputAssistantItem.leadingBarButtonGroups = @[];
        self.inputAssistantItem.trailingBarButtonGroups = @[];
    }
    return self;
}

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

// 표준 편집 동작이다. 시스템 편집 메뉴, 키보드의 붙여넣기 추천, 하드웨어 키보드의 Command+C·X·V·A가 부른다.
//  - 글 칸에 초점이 없으면 모두 거절한다. 그때의 Command 단축키는 `pressesBegan:`을 거쳐 앱의 키로 간다.
- (BOOL)canPerformAction:(SEL)action withSender:(id)sender
{
    if (action == @selector(copy:))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"copy");
    if (action == @selector(cut:))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"cut");
    if (action == @selector(paste:))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"paste");
    if (action == @selector(selectAll:))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"all");
    if (action == NSSelectorFromString(@"luilOpenLink:"))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"open-link");
    if (action == NSSelectorFromString(@"luilCopyLink:"))
        return self.owner != nullptr && self.owner->edit_action_allowed(u8"copy-link");
    if (action == @selector(select:) || action == @selector(delete:))
        return NO;
    return [super canPerformAction:action withSender:sender];
}

- (void)luilEdit:(const char8_t*)key
{
    if (self.owner != nullptr)
        self.owner->edit_action(key);
}

- (void)copy:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"copy"];
}

- (void)cut:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"cut"];
}

- (void)paste:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"paste"];
}

- (void)selectAll:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"all"];
}

- (void)luilOpenLink:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"open-link"];
}

- (void)luilCopyLink:(id)sender
{
    static_cast<void>(sender);
    [self luilEdit:u8"copy-link"];
}

// 텍스트 칸에 초점이 없으면 빈 입력 view다. 첫 응답자로 남아 하드웨어 키를 받되 소프트 키보드는 뜨지 않는다.
- (UIView*)inputView
{
    return self.luilKeyboardEnabled ? nil : _emptyInput;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    if (self.owner != nullptr)
        self.owner->on_layout();
}

- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event
{
    // 아래층 웹뷰의 구멍 자리는 터치가 이 view를 지나 웹뷰로 간다.
    if (self.owner != nullptr && self.owner->passes_through(point))
        return nil;
    return [super hitTest:point withEvent:event];
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (self.owner != nullptr)
        self.owner->touches(touches, event, luil::pointer_phase::down);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (self.owner != nullptr)
        self.owner->touches(touches, event, luil::pointer_phase::update);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (self.owner != nullptr)
        self.owner->touches(touches, event, luil::pointer_phase::up);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (self.owner != nullptr)
        self.owner->touches(touches, event, luil::pointer_phase::up);
}

// 앱이 먹지 않은 키만 UIKit에 넘긴다. 넘긴 키는 뗌·취소도 넘긴다 (짝이 맞아야 글 입력 체계가 키를 놓는다).
- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    NSMutableSet<UIPress*>* const rest { [NSMutableSet set] };
    for (UIPress* const press in presses)
        if (self.owner == nullptr || self.owner->press_began(press) == false)
        {
            [rest addObject:press];
            [_forwarded addObject:press];
        }
    if (rest.count > 0)
        [super pressesBegan:rest withEvent:event];
}

- (void)luilPressesFinished:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event cancelled:(BOOL)cancelled
{
    NSMutableSet<UIPress*>* const rest { [NSMutableSet set] };
    for (UIPress* const press in presses)
    {
        if (self.owner != nullptr)
            self.owner->press_ended(press);
        if ([_forwarded containsObject:press])
        {
            [rest addObject:press];
            [_forwarded removeObject:press];
        }
    }
    if (rest.count == 0)
        return;
    if (cancelled)
        [super pressesCancelled:rest withEvent:event];
    else
        [super pressesEnded:rest withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self luilPressesFinished:presses withEvent:event cancelled:NO];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self luilPressesFinished:presses withEvent:event cancelled:YES];
}

// --- 접근성: 요소는 다리가 짓는다 ---

- (BOOL)isAccessibilityElement
{
    return NO;
}

- (NSArray*)accessibilityElements
{
    return self.owner != nullptr ? self.owner->accessibility_elements() : nil;
}

// --- 문서 ---

- (NSString*)luilText
{
    return [_text copy];
}

- (NSRange)luilSelection
{
    return _selection;
}

- (NSRange)luilMarked
{
    return _marked;
}

// host가 문서를 바꾼다 (앱이 글을 바꿨다). 글 입력 체계에 알려 조합·자동 고침을 새로 세우게 한다.
- (void)luilSetText:(NSString*)text selection:(NSRange)selection marked:(NSRange)marked
{
    const BOOL notify { _systemEditDepth == 0 };
    if (notify)
    {
        [self.inputDelegate selectionWillChange:self];
        [self.inputDelegate textWillChange:self];
    }
    [_text setString:text];
    _selection = selection;
    _marked = marked;
    if (notify)
    {
        [self.inputDelegate textDidChange:self];
        [self.inputDelegate selectionDidChange:self];
    }
    else
        _pendingNotify = YES;
}

- (NSRange)luilClamp:(NSRange)range
{
    const NSUInteger length { _text.length };
    const NSUInteger location { std::min<NSUInteger>(range.location, length) };
    return NSMakeRange(location, std::min<NSUInteger>(range.length, length - location));
}

// 글 입력 체계가 문서를 고친 뒤다. host에 넘기고, 그 사이 host가 되돌려 놓은 것이 있으면 이제 알린다.
- (void)luilSystemEdit:(void (^)(void))edit
{
    ++_systemEditDepth;
    edit();
    if (self.owner != nullptr)
        self.owner->text_changed_by_system();
    --_systemEditDepth;
    if (_systemEditDepth == 0 && _pendingNotify)
    {
        _pendingNotify = NO;
        [self.inputDelegate selectionWillChange:self];
        [self.inputDelegate textWillChange:self];
        [self.inputDelegate textDidChange:self];
        [self.inputDelegate selectionDidChange:self];
    }
}

// --- UIKeyInput ---

- (BOOL)hasText
{
    return _text.length > 0;
}

- (void)insertText:(NSString*)text
{
    [self luilSystemEdit:^{
      const NSRange target { self->_marked.location != NSNotFound ? [self luilClamp:self->_marked] : [self luilClamp:self->_selection] };
      [self->_text replaceCharactersInRange:target withString:text];
      self->_selection = NSMakeRange(target.location + text.length, 0);
      self->_marked = NSMakeRange(NSNotFound, 0);
    }];
}

- (void)deleteBackward
{
    [self luilSystemEdit:^{
      NSRange target { [self luilClamp:self->_selection] };
      if (self->_marked.location != NSNotFound)
      {
          target = [self luilClamp:self->_marked];
          self->_marked = NSMakeRange(NSNotFound, 0);
      }
      else if (target.length == 0)
      {
          if (target.location == 0)
              return;
          target = [self->_text rangeOfComposedCharacterSequenceAtIndex:target.location - 1];
      }
      [self->_text deleteCharactersInRange:target];
      self->_selection = NSMakeRange(target.location, 0);
    }];
}

// --- UITextInput ---

- (NSString*)textInRange:(UITextRange*)range
{
    if ([range isKindOfClass:[LuilTextRange class]] == NO)
        return @"";
    return [_text substringWithRange:[self luilClamp:((LuilTextRange*)range).range]];
}

- (void)replaceRange:(UITextRange*)range withText:(NSString*)text
{
    if ([range isKindOfClass:[LuilTextRange class]] == NO)
        return;
    const NSRange target { [self luilClamp:((LuilTextRange*)range).range] };
    [self luilSystemEdit:^{
      [self->_text replaceCharactersInRange:target withString:text];
      self->_selection = NSMakeRange(target.location + text.length, 0);
      self->_marked = NSMakeRange(NSNotFound, 0);
    }];
}

- (UITextRange*)selectedTextRange
{
    return [LuilTextRange rangeWithRange:[self luilClamp:_selection]];
}

- (void)setSelectedTextRange:(UITextRange*)range
{
    if ([range isKindOfClass:[LuilTextRange class]] == NO)
        return;
    const NSRange target { [self luilClamp:((LuilTextRange*)range).range] };
    if (NSEqualRanges(target, _selection))
        return;
    [self luilSystemEdit:^{
      self->_selection = target;
    }];
}

- (UITextRange*)markedTextRange
{
    if (_marked.location == NSNotFound)
        return nil;
    return [LuilTextRange rangeWithRange:[self luilClamp:_marked]];
}

- (NSDictionary<NSAttributedStringKey, id>*)markedTextStyle
{
    return _markedStyle;
}

- (void)setMarkedTextStyle:(NSDictionary<NSAttributedStringKey, id>*)style
{
    _markedStyle = style;
}

- (void)setMarkedText:(NSString*)markedText selectedRange:(NSRange)selectedRange
{
    NSString* const text { markedText != nil ? markedText : @"" };
    [self luilSystemEdit:^{
      const NSRange target { self->_marked.location != NSNotFound ? [self luilClamp:self->_marked] : [self luilClamp:self->_selection] };
      [self->_text replaceCharactersInRange:target withString:text];
      self->_marked = text.length > 0 ? NSMakeRange(target.location, text.length) : NSMakeRange(NSNotFound, 0);
      const NSUInteger caret { std::min<NSUInteger>(selectedRange.location, text.length) };
      self->_selection = NSMakeRange(target.location + caret, std::min<NSUInteger>(selectedRange.length, text.length - caret));
    }];
}

- (void)unmarkText
{
    if (_marked.location == NSNotFound)
        return;
    [self luilSystemEdit:^{
      self->_marked = NSMakeRange(NSNotFound, 0);
    }];
}

- (UITextPosition*)beginningOfDocument
{
    return [LuilTextPosition positionWithIndex:0];
}

- (UITextPosition*)endOfDocument
{
    return [LuilTextPosition positionWithIndex:_text.length];
}

- (UITextRange*)textRangeFromPosition:(UITextPosition*)fromPosition toPosition:(UITextPosition*)toPosition
{
    const NSUInteger from { ((LuilTextPosition*)fromPosition).index };
    const NSUInteger to { ((LuilTextPosition*)toPosition).index };
    return [LuilTextRange rangeWithRange:NSMakeRange(std::min(from, to), std::max(from, to) - std::min(from, to))];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset
{
    const NSInteger index { static_cast<NSInteger>(((LuilTextPosition*)position).index) + offset };
    if (index < 0 || index > static_cast<NSInteger>(_text.length))
        return nil;
    return [LuilTextPosition positionWithIndex:static_cast<NSUInteger>(index)];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset
{
    // 한 줄로 본다. 위·아래는 문서의 처음·끝이다.
    switch (direction)
    {
    case UITextLayoutDirectionRight:
        return [self positionFromPosition:position offset:offset];
    case UITextLayoutDirectionLeft:
        return [self positionFromPosition:position offset:-offset];
    case UITextLayoutDirectionUp:
        return [self beginningOfDocument];
    default:
        return [self endOfDocument];
    }
}

- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other
{
    const NSUInteger left { ((LuilTextPosition*)position).index };
    const NSUInteger right { ((LuilTextPosition*)other).index };
    return left < right ? NSOrderedAscending : left > right ? NSOrderedDescending : NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)toPosition
{
    return static_cast<NSInteger>(((LuilTextPosition*)toPosition).index) - static_cast<NSInteger>(((LuilTextPosition*)from).index);
}

- (id<UITextInputTokenizer>)tokenizer
{
    return _tokenizer;
}

- (UITextPosition*)positionWithinRange:(UITextRange*)range farthestInDirection:(UITextLayoutDirection)direction
{
    const NSRange value { ((LuilTextRange*)range).range };
    const bool backward { direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp };
    return [LuilTextPosition positionWithIndex:backward ? value.location : NSMaxRange(value)];
}

- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position inDirection:(UITextLayoutDirection)direction
{
    const NSUInteger index { std::min<NSUInteger>(((LuilTextPosition*)position).index, _text.length) };
    const bool backward { direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp };
    return backward ? [LuilTextRange rangeWithRange:NSMakeRange(0, index)] : [LuilTextRange rangeWithRange:NSMakeRange(index, _text.length - index)];
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position inDirection:(UITextStorageDirection)direction
{
    static_cast<void>(position);
    static_cast<void>(direction);
    return NSWritingDirectionNatural;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range
{
    static_cast<void>(writingDirection);
    static_cast<void>(range);
}

- (CGRect)firstRectForRange:(UITextRange*)range
{
    if (self.owner == nullptr || [range isKindOfClass:[LuilTextRange class]] == NO)
        return CGRectNull;
    return self.owner->text_range_rect([self luilClamp:((LuilTextRange*)range).range]);
}

- (CGRect)caretRectForPosition:(UITextPosition*)position
{
    if (self.owner == nullptr)
        return CGRectZero;
    const CGRect rect { self.owner->text_range_rect(NSMakeRange(std::min<NSUInteger>(((LuilTextPosition*)position).index, _text.length), 0)) };
    if (CGRectIsNull(rect))
        return CGRectZero;
    return CGRectMake(rect.origin.x, rect.origin.y, 2, rect.size.height);
}

- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range
{
    static_cast<void>(range);
    // 선택은 앱이 그린다. 시스템의 선택 손잡이를 세우지 않는다.
    return @[];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point
{
    static_cast<void>(point);
    // 자리는 앱이 누름으로 옮긴다 (caret 이동은 tree가 한다). 시스템에는 지금 caret을 준다.
    return [LuilTextPosition positionWithIndex:NSMaxRange([self luilClamp:_selection])];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range
{
    static_cast<void>(point);
    return ((LuilTextRange*)range).end;
}

- (UITextRange*)characterRangeAtPoint:(CGPoint)point
{
    static_cast<void>(point);
    return [LuilTextRange rangeWithRange:NSMakeRange(NSMaxRange([self luilClamp:_selection]), 0)];
}

- (UIView*)textInputView
{
    return self;
}

@end

@implementation LuilViewController

- (void)loadView
{
    // 바탕 view가 웹뷰와 Skia view를 함께 담는다. 웹뷰는 Skia view의 형제다 (위층은 위, 아래층은 아래).
    UIView* const container { [[UIView alloc] initWithFrame:UIScreen.mainScreen.bounds] };
    container.backgroundColor = UIColor.systemBackgroundColor;
    self.luilView = [[LuilView alloc] initWithFrame:container.bounds];
    self.luilView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [container addSubview:self.luilView];
    self.view = container;
}

- (void)viewSafeAreaInsetsDidChange
{
    [super viewSafeAreaInsetsDidChange];
    if (self.owner != nullptr)
        self.owner->on_layout();
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    if (self.owner != nullptr)
        self.owner->on_layout();
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    // iOS 17부터는 바뀜을 등록해 받는다 (`traitCollectionDidChange:`가 deprecated다). host가 읽는 특성만 본다 — 밝기·대비(외양),
    // 글자 크기(Dynamic Type), 배율과 크기 등급(metrics).
    if (@available(iOS 17.0, *))
        [self registerForTraitChanges:@[
            UITraitUserInterfaceStyle.class, UITraitAccessibilityContrast.class, UITraitPreferredContentSizeCategory.class, UITraitDisplayScale.class,
            UITraitHorizontalSizeClass.class, UITraitVerticalSizeClass.class
        ]
                           withAction:@selector(luilTraitsChanged)];
}

- (void)luilTraitsChanged
{
    if (self.owner != nullptr)
        self.owner->on_trait_change();
}

#if LUIL_IOS_BEFORE_17
// iOS 15·16의 길이다. 17부터는 위의 등록이 같은 일을 하므로 여기서는 부르지 않는다 (두 번 불리지 않게).
- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection
{
    [super traitCollectionDidChange:previousTraitCollection];
    if (@available(iOS 17.0, *))
        return;
    [self luilTraitsChanged];
}
#endif

- (BOOL)prefersStatusBarHidden
{
    return self.owner != nullptr && self.owner->bars_hidden();
}

- (UIStatusBarStyle)preferredStatusBarStyle
{
    return self.owner != nullptr ? self.owner->status_bar_style() : UIStatusBarStyleDefault;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return self.owner != nullptr && self.owner->bars_hidden();
}

// 몰입 모드에서는 화면 가장자리의 첫 쓸기를 앱이 받는다. 두 번째 쓸기가 시스템의 것이다.
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures
{
    return self.owner != nullptr && self.owner->bars_hidden() ? UIRectEdgeAll : UIRectEdgeNone;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return self.owner != nullptr ? self.owner->supported_orientations() : UIInterfaceOrientationMaskAll;
}

- (void)luilHover:(UIHoverGestureRecognizer*)recognizer API_AVAILABLE(ios(13.0))
{
    if (self.owner != nullptr)
        self.owner->hover(recognizer);
}

- (void)luilScroll:(UIPanGestureRecognizer*)recognizer
{
    if (self.owner != nullptr)
        self.owner->scroll(recognizer);
}

// 시스템 편집 메뉴의 항목이다. 시스템이 고른 표준 동작(첫 응답자의 `canPerformAction:` 순서) 뒤에 링크 줄을 붙인다.
- (UIMenu*)editMenuInteraction:(UIEditMenuInteraction*)interaction
          menuForConfiguration:(UIEditMenuConfiguration*)configuration
              suggestedActions:(NSArray<UIMenuElement*>*)suggestedActions API_AVAILABLE(ios(16.0))
{
    static_cast<void>(interaction);
    static_cast<void>(configuration);
    NSMutableArray<UIMenuElement*>* const elements { [NSMutableArray arrayWithArray:suggestedActions] };
    if (self.owner != nullptr)
    {
        LuilView* const view { self.luilView };
        for (const auto& [key, label] : self.owner->link_actions())
        {
            const bool open { key == u8"open-link" };
            NSString* const title { luil::macos::make_ns_string(label) };
            [elements addObject:[UIAction actionWithTitle:title != nil ? title : @""
                                                    image:nil
                                               identifier:nil
                                                  handler:^(UIAction*) {
                                                    if (open)
                                                        [view luilOpenLink:nil];
                                                    else
                                                        [view luilCopyLink:nil];
                                                  }]];
        }
    }
    return [UIMenu menuWithChildren:elements];
}

- (void)editMenuInteraction:(UIEditMenuInteraction*)interaction
    willDismissMenuForConfiguration:(UIEditMenuConfiguration*)configuration
                           animator:(id<UIEditMenuInteractionAnimating>)animator API_AVAILABLE(ios(16.0))
{
    static_cast<void>(interaction);
    static_cast<void>(configuration);
    static_cast<void>(animator);
    if (self.owner != nullptr)
        self.owner->edit_menu_dismissed();
}

@end

@implementation LuilSceneDelegate

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions
{
    static_cast<void>(session);
    static_cast<void>(connectionOptions);
    if ([scene isKindOfClass:[UIWindowScene class]] == NO)
        return;
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->attach((UIWindowScene*)scene, self);
}

- (void)sceneDidDisconnect:(UIScene*)scene
{
    static_cast<void>(scene);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->detach();
    self.window = nil;
}

- (void)sceneWillEnterForeground:(UIScene*)scene
{
    static_cast<void>(scene);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->on_foreground(true);
}

- (void)sceneDidEnterBackground:(UIScene*)scene
{
    static_cast<void>(scene);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->on_foreground(false);
}

- (void)sceneDidBecomeActive:(UIScene*)scene
{
    static_cast<void>(scene);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->on_active(true);
}

- (void)sceneWillResignActive:(UIScene*)scene
{
    static_cast<void>(scene);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->on_active(false);
}

@end

@implementation LuilApplicationDelegate

- (BOOL)application:(UIApplication*)application willFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey, id>*)launchOptions
{
    static_cast<void>(application);
    static_cast<void>(launchOptions);
    luil::ios::application* const app { luil::ios::running_application() };
    return app != nullptr && app->launch();
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
                                   options:(UISceneConnectionOptions*)options
{
    static_cast<void>(application);
    static_cast<void>(options);
    UISceneConfiguration* const configuration { [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:connectingSceneSession.role] };
    configuration.delegateClass = [LuilSceneDelegate class];
    return configuration;
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    static_cast<void>(application);
    if (luil::ios::application* const app { luil::ios::running_application() }; app != nullptr)
        app->stop();
}

@end

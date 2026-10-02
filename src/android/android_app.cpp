#include "luil/android/android_app.h"

#include "android/android_fonts.h"
#include "android/cpu_skia_renderer.h"
#include "host/font_registry.h"
#include "host/frame_state.h"
#include "host/skia_renderer.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_platform.h"

#include "include/core/SkTypeface.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <android/configuration.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace luil::android {
    namespace {
        constexpr const char* log_tag { "luil" };
        // 두 번 탭의 판정 간격이다. Android ViewConfiguration의 기본값이다.
        constexpr std::chrono::milliseconds double_tap_timeout { 300 };
        // 연속 애니메이션(next_update가 "지금"을 답하는 것)의 다시 그리기 간격이다.
        // Win32와 같은 30fps다.
        constexpr std::chrono::milliseconds continuous_repaint_interval { 33 };
        // Android 밀도의 기준(mdpi)이다. 배율은 이 값에 대한 비라 논리 픽셀 1이 1dp다.
        constexpr int density_baseline { ACONFIGURATION_DENSITY_MEDIUM };

        void log_error(const std::u8string& message)
        {
            __android_log_print(ANDROID_LOG_ERROR, log_tag, "%.*s", static_cast<int>(message.size()), reinterpret_cast<const char*>(message.data()));
        }

        // 표면 가장자리 중 시스템이 덮는 몫이다 (물리 픽셀).
        struct edge_insets
        {
            int left { 0 };
            int top { 0 };
            int right { 0 };
            int bottom { 0 };

            [[nodiscard]] bool operator==(const edge_insets&) const noexcept = default;
        };

        // 시스템 막대와 디스플레이 컷아웃이 덮는 가장자리다. 가장자리마다 큰 쪽을 비킨다.
        // targetSdk 35부터 창은 화면 끝까지 그려지므로(edge-to-edge) 이것을 비키지 않으면
        // 내용이 상태 표시줄과 제스처 막대 밑에 깔린다.
        [[nodiscard]] edge_insets read_safe_insets(GameActivity* const activity)
        {
            ARect bars {};
            GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, &bars);
            ARect cutout {};
            GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_DISPLAY_CUTOUT, &cutout);
            return edge_insets {
                .left = std::max(bars.left, cutout.left),
                .top = std::max(bars.top, cutout.top),
                .right = std::max(bars.right, cutout.right),
                .bottom = std::max(bars.bottom, cutout.bottom),
            };
        }

        // 화면 밀도를 배율로 옮긴다 (160 → 1.0, 480 → 3.0).
        // 밀도를 모르는 값(기본·없음·임의)이면 1이다.
        [[nodiscard]] float read_scale(AConfiguration* const configuration)
        {
            const std::int32_t density { AConfiguration_getDensity(configuration) };
            if (density <= 0 || density >= ACONFIGURATION_DENSITY_ANY || density == ACONFIGURATION_DENSITY_NONE)
                return 1.0f;
            return static_cast<float>(density) / static_cast<float>(density_baseline);
        }

        // 사용자 UI 언어의 BCP-47 태그다 (`ko-KR` 꼴). 읽지 못하면 빈 문자열이다.
        [[nodiscard]] std::string read_language(AConfiguration* const configuration)
        {
            char language[2] {};
            AConfiguration_getLanguage(configuration, language);
            if (language[0] == '\0')
                return {};
            std::string tag { language, 2 };
            char country[2] {};
            AConfiguration_getCountry(configuration, country);
            if (country[0] != '\0')
            {
                tag.push_back('-');
                tag.append(country, 2);
            }
            return tag;
        }

        // 시스템의 밝은 모드 여부다. 어두운 모드가 아니면 밝다고 본다.
        [[nodiscard]] bool read_prefers_light(AConfiguration* const configuration)
        {
            return AConfiguration_getUiModeNight(configuration) != ACONFIGURATION_UI_MODE_NIGHT_YES;
        }

        // 뒤로 가기는 Activity에 남긴다. Activity의 기본 처리(앱 끝내기, 예측 뒤로 가기
        // 애니메이션)를 그대로 받고, 끝날 때 `app_host::shutdown()`이 돈다.
        bool key_event_filter(const GameActivityKeyEvent* const event)
        {
            return event->keyCode != AKEYCODE_BACK;
        }

        // 가족 이름 → typeface(글꼴 미리 보기)와 글자 → 대체 typeface를 함께 맡는다.
        // 둘 다 core 글꼴 registry의 같은 cache를 본다 (Win32 host와 같은 모양이다).
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
        };

        // GameActivity Activity 하나의 수명이다.
        // native_app_glue의 `android_main` thread에서만 산다 — 그 thread가 luil의 UI thread다.
        class application final
        {
        public:
            application(android_app* const app, const application_config& config, const application_environment& environment)
                : app_ { app }
                , config_ { config }
                , environment_ { environment }
            {}

            application(const application&) = delete;
            application(application&&) = delete;
            application& operator=(const application&) = delete;
            application& operator=(application&&) = delete;

            ~application()
            {
                // 렌더러가 창보다 먼저 사라진다. 창은 glue가 쥐고 있다.
                renderer_.reset();
                if (wake_fd_ >= 0)
                {
                    ALooper_removeFd(app_->looper, wake_fd_);
                    close(wake_fd_);
                }
                set_font_fallback(nullptr);
                app_->userData = nullptr;
                app_->onAppCmd = nullptr;
            }

            [[nodiscard]] int run()
            {
                if (start() == false)
                    return 1;

                while (app_->destroyRequested == 0)
                {
                    int events { 0 };
                    android_poll_source* source { nullptr };
                    const int identifier { ALooper_pollOnce(poll_timeout(), nullptr, &events, reinterpret_cast<void**>(&source)) };
                    if (identifier == LOOPER_ID_MAIN || identifier == LOOPER_ID_INPUT)
                    {
                        if (source != nullptr)
                            source->process(app_, source);
                    }
                    else if (identifier == LOOPER_ID_USER)
                    {
                        drain_wake();
                        execute_host_commands();
                        dirty_ = true;
                    }

                    // 입력은 5단계에서 옮긴다. 지금은 쌓이지 않게 비우기만 한다.
                    drain_input();

                    if (host_->faulted() && finishing_ == false)
                    {
                        log_error(u8"The logic or input thread stopped on an exception. Finishing the activity.");
                        finish();
                    }
                    if (update_deadline_.has_value() && std::chrono::steady_clock::now() >= *update_deadline_)
                        dirty_ = true;
                    if (dirty_ && renderer_ != nullptr)
                        render();
                }

                stop();
                return 0;
            }

        private:
            [[nodiscard]] bool start()
            {
                if (environment_.driver == nullptr)
                {
                    log_error(u8"run_application needs a logic driver.");
                    return false;
                }

                // 글꼴 registry가 첫 조회 때 한 번 읽는다. 무엇보다 먼저 넣는다.
                set_user_language(read_language(app_->config));
                // 휴대폰·태블릿이다. 창 caption이 없고 맨 위는 앱 바의 자리다.
                // logic thread가 첫 frame을 짓기 전에 정한다.
                set_ui_platform({ .form_factor = ui_form_factor::mobile, .window_caption = false });
                prefers_light_ = read_prefers_light(app_->config);
                scale_ = read_scale(app_->config);
                set_font_fallback(&font_resolver_);

                // UI thread를 깨우는 길이다. 다른 thread가 eventfd에 쓰면 looper가 깨어
                // 그 큐를 비운다. Win32의 PostMessageW에 해당한다.
                wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (wake_fd_ < 0 || ALooper_addFd(app_->looper, wake_fd_, LOOPER_ID_USER, ALOOPER_EVENT_INPUT, nullptr, nullptr) != 1)
                {
                    log_error(u8"Failed to set up the UI thread wake signal.");
                    return false;
                }

                const int wake_fd { wake_fd_ };
                const auto signal = [wake_fd]() noexcept {
                    const std::uint64_t one { 1 };
                    return write(wake_fd, &one, sizeof(one)) == static_cast<ssize_t>(sizeof(one));
                };
                app_host::config host_config {};
                host_config.wake.snapshot = signal;
                host_config.wake.ui_command = [signal] { static_cast<void>(signal()); };
                host_config.wake.app_ui_command = [signal] { static_cast<void>(signal()); };
                host_config.wake.clipboard = [signal] { static_cast<void>(signal()); };
                host_config.interaction.double_click_time = double_tap_timeout;
                host_config.interaction.touch = config_.touch;
                host_ = std::make_unique<app_host>(std::move(host_config), *environment_.driver, environment_.policy);

                app_->userData = this;
                app_->onAppCmd = &application::on_app_command;
                android_app_set_key_event_filter(app_, &key_event_filter);

                if (environment_.delegate != nullptr)
                    environment_.delegate->on_started(*host_);
                return true;
            }

            void stop()
            {
                const auto started { std::chrono::steady_clock::now() };
                host_->shutdown();
                const auto elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started) };
                __android_log_print(ANDROID_LOG_INFO, log_tag, "shutdown finished in %lld ms", static_cast<long long>(elapsed.count()));
            }

            static void on_app_command(android_app* const app, const std::int32_t command)
            {
                if (auto* const self { static_cast<application*>(app->userData) }; self != nullptr)
                    self->handle_command(command);
            }

            void handle_command(const std::int32_t command)
            {
                switch (command)
                {
                case APP_CMD_INIT_WINDOW:
                    create_renderer();
                    refresh_metrics();
                    break;
                case APP_CMD_TERM_WINDOW:
                    // glue는 이 알림이 끝날 때까지 창을 쥐고 있다가 놓는다.
                    // 그 안에서 렌더러를 버려야 놓인 창을 붙잡은 채로 남지 않는다.
                    renderer_.reset();
                    window_width_ = 0;
                    window_height_ = 0;
                    break;
                case APP_CMD_WINDOW_RESIZED:
                case APP_CMD_CONTENT_RECT_CHANGED:
                case APP_CMD_WINDOW_INSETS_CHANGED:
                    refresh_metrics();
                    break;
                case APP_CMD_CONFIG_CHANGED:
                    // 회전·밀도·어두운 모드는 Activity를 다시 만들지 않고 여기로 온다
                    // (매니페스트의 configChanges). glue가 구성을 이미 새로 읽어 두었다.
                    prefers_light_ = read_prefers_light(app_->config);
                    scale_ = read_scale(app_->config);
                    refresh_metrics();
                    break;
                case APP_CMD_WINDOW_REDRAW_NEEDED:
                    dirty_ = true;
                    if (renderer_ != nullptr)
                        render();
                    break;
                case APP_CMD_START:
                    post_lifecycle(app_lifecycle::foreground);
                    break;
                case APP_CMD_STOP:
                    post_lifecycle(app_lifecycle::background);
                    break;
                case APP_CMD_RESUME:
                case APP_CMD_GAINED_FOCUS:
                    dirty_ = true;
                    break;
                default:
                    break;
                }
            }

            void create_renderer()
            {
                if (app_->window == nullptr)
                    return;
                ANativeWindow* const window { app_->window };
                renderer_factories factories {};
                factories.gpu_name = u8"Vulkan";
                // GPU(Vulkan) 렌더러는 4단계다. 생성 함수가 없으면 GPU가 없는 것이다.
                factories.create_cpu = [window] { return create_cpu_skia_renderer(window); };
                std::u8string error {};
                renderer_ = renderer_host::create(config_.renderer, {}, std::move(factories), error);
                if (renderer_ == nullptr)
                {
                    log_error(error);
                    finish();
                }
            }

            // 창 크기·안전 영역·배율을 다시 읽어 렌더러와 앱에 알린다.
            void refresh_metrics()
            {
                if (app_->window != nullptr)
                {
                    window_width_ = ANativeWindow_getWidth(app_->window);
                    window_height_ = ANativeWindow_getHeight(app_->window);
                }
                insets_ = read_safe_insets(app_->activity);
                // 표면과 안전 영역이 바뀔 때만 한 줄 남긴다. 기기마다 다른 막대·컷아웃 배치를
                // logcat에서 바로 맞대 보는 자리다.
                __android_log_print(ANDROID_LOG_INFO, log_tag, "surface %dx%d, safe insets %d,%d,%d,%d, scale %.2f", window_width_, window_height_, insets_.left, insets_.top, insets_.right,
                    insets_.bottom, static_cast<double>(scale_));
                if (renderer_ != nullptr)
                {
                    std::u8string error {};
                    if (renderer_->resize(window_width_, window_height_, error) == false)
                        log_error(error);
                }
                dirty_ = true;
                post_metrics();
            }

            [[nodiscard]] int content_width() const noexcept
            {
                return std::max(1, window_width_ - insets_.left - insets_.right);
            }

            [[nodiscard]] int content_height() const noexcept
            {
                return std::max(1, window_height_ - insets_.top - insets_.bottom);
            }

            // 앱에 안전 영역의 크기와 배율을 알린다. 같은 값이면 다시 알리지 않는다.
            void post_metrics()
            {
                if (environment_.delegate == nullptr || window_width_ <= 0 || window_height_ <= 0)
                    return;
                const float width { static_cast<float>(content_width()) };
                const float height { static_cast<float>(content_height()) };
                if (width == reported_width_ && height == reported_height_ && scale_ == reported_scale_)
                    return;
                reported_width_ = width;
                reported_height_ = height;
                reported_scale_ = scale_;
                if (app_message message { environment_.delegate->make_window_metrics_message(width, height, scale_) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
            }

            void post_lifecycle(const app_lifecycle lifecycle)
            {
                if (environment_.delegate == nullptr)
                    return;
                if (app_message message { environment_.delegate->make_lifecycle_message(lifecycle) }; message.empty() == false)
                    host_->post_app_message(std::move(message));
            }

            void finish()
            {
                if (finishing_)
                    return;
                finishing_ = true;
                GameActivity_finish(app_->activity);
            }

            [[nodiscard]] int poll_timeout() const
            {
                if (dirty_ && renderer_ != nullptr)
                    return 0;
                if (update_deadline_.has_value() == false)
                    return -1;
                const auto remaining { std::chrono::duration_cast<std::chrono::milliseconds>(*update_deadline_ - std::chrono::steady_clock::now()) };
                return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(remaining.count(), 0, 60'000));
            }

            void drain_wake() const
            {
                std::uint64_t count { 0 };
                while (read(wake_fd_, &count, sizeof(count)) == static_cast<ssize_t>(sizeof(count)))
                {
                }
            }

            // input thread가 큐에 넣은 명령을 UI thread에서 실행한다.
            void execute_host_commands()
            {
                for (const ui_command command : host_->take_ui_commands())
                {
                    // 창 하나가 화면을 덮으므로 최소화·최대화·전체 화면은 뜻이 없다.
                    // 닫기는 Activity를 끝낸다.
                    if (command == ui_command::window_close)
                        finish();
                }
                for (const app_ui_command& command : host_->take_app_ui_commands())
                    if (environment_.delegate != nullptr)
                        environment_.delegate->execute_app_ui_command(*host_, command);
                // 클립보드는 6단계다. 요청은 쌓이지 않게 비운다.
                static_cast<void>(host_->take_clipboard_requests());
            }

            void drain_input()
            {
                if (android_input_buffer* const inputs { android_app_swap_input_buffers(app_) }; inputs != nullptr)
                {
                    android_app_clear_motion_events(inputs);
                    android_app_clear_key_events(inputs);
                }
            }

            void render()
            {
                frame_state state {};
                state.width = content_width();
                state.height = content_height();
                state.origin_x = insets_.left;
                state.origin_y = insets_.top;
                state.dpi_scale = scale_;

                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                state.interaction = interaction_for_surface(host_->acquire_interaction(), std::u8string {});
                const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
                // 고대비는 아직 읽지 않는다 (접근성 단계). 밝은 모드만 시스템을 따른다.
                state.theme = resolve_color_theme(appearance.theme, false, prefers_light_);
                state.accent_id = appearance.accent_id;
                state.style = frame != nullptr ? frame->style.get() : nullptr;

                const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
                set_configured_fonts(fonts.ui_family, fonts.code_family);
                const sk_sp<SkTypeface> code { configured_code_typeface() };
                state.code_typeface = code.get();
                state.fonts = &font_resolver_;

                const std::shared_ptr<const ui_tree> tree { host_->acquire_ui_tree() };
                state.tree = tree.get();

                std::u8string error {};
                if (renderer_->render(state, error) == false)
                    log_error(error);
                dirty_ = false;

                // 시간이 흘러야 바뀌는 그림(애니메이션·tooltip 지연)의 다음 시각이다.
                const auto now { std::chrono::steady_clock::now() };
                update_deadline_ = tree != nullptr ? tree->next_update(update_context { now }, state.interaction) : std::nullopt;
                if (update_deadline_.has_value() && *update_deadline_ <= now)
                    update_deadline_ = now + continuous_repaint_interval;
            }

            android_app* app_;
            application_config config_;
            application_environment environment_;
            int wake_fd_ { -1 };
            std::unique_ptr<app_host> host_ {};
            std::unique_ptr<renderer_host> renderer_ {};
            registry_font_resolver font_resolver_ {};
            int window_width_ { 0 };
            int window_height_ { 0 };
            edge_insets insets_ {};
            float scale_ { 1.0f };
            bool prefers_light_ { false };
            bool dirty_ { false };
            bool finishing_ { false };
            std::optional<std::chrono::steady_clock::time_point> update_deadline_ {};
            // 마지막으로 앱에 알린 크기와 배율이다.
            float reported_width_ { -1.0f };
            float reported_height_ { -1.0f };
            float reported_scale_ { -1.0f };
        };
    } // namespace

    int run_application(android_app* const app, const application_config& config, const application_environment& environment)
    {
        if (app == nullptr)
            return 1;
        application instance { app, config, environment };
        return instance.run();
    }
} // namespace luil::android

#include "luil/android/android_app.h"

#include "android/android_clipboard.h"
#include "android/android_fonts.h"
#include "android/android_input.h"
#include "android/android_system_theme.h"
#include "android/android_text_input.h"
#include "android/cpu_skia_renderer.h"
#include "android/java_vm.h"
#include "android/main_thread.h"
#include "android/vulkan_device.h"
#include "android/vulkan_skia_renderer.h"
#include "host/font_registry.h"
#include "host/frame_state.h"
#include "host/overlay_input.h"
#include "host/popup_dismiss.h"
#include "host/skia_renderer.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_platform.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <game-text-input/gametextinput.h>

#include <android/configuration.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>

#include <sys/eventfd.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
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

        // 시스템 속성 하나를 읽는다. 없거나 비었으면 빈 문자열이다.
        [[nodiscard]] std::string read_property(const char* const name)
        {
            char value[PROP_VALUE_MAX] {};
            const int length { __system_property_get(name, value) };
            return std::string { value, static_cast<std::size_t>(std::max(0, length)) };
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

        // 시스템 막대·디스플레이 컷아웃·소프트 키보드가 덮는 가장자리다. 가장자리마다 큰 쪽을 비킨다.
        // targetSdk 35부터 창은 화면 끝까지 그려지므로(edge-to-edge) 이것을 비키지 않으면
        // 내용이 상태 표시줄과 제스처 막대 밑에 깔린다. 키보드가 올라오면 아래 가장자리가 그만큼
        // 커져 앱이 줄어든 크기로 다시 배치한다.
        [[nodiscard]] edge_insets read_safe_insets(GameActivity* const activity)
        {
            ARect bars {};
            GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, &bars);
            ARect cutout {};
            GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_DISPLAY_CUTOUT, &cutout);
            ARect keyboard {};
            GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_IME, &keyboard);
            return edge_insets {
                .left = std::max(bars.left, cutout.left),
                .top = std::max(bars.top, cutout.top),
                .right = std::max(bars.right, cutout.right),
                .bottom = std::max({ bars.bottom, cutout.bottom, keyboard.bottom }),
            };
        }

        // glue가 GameActivity의 IME 알림에 단 처리기와 UI thread를 깨울 eventfd다.
        // glue는 IME 상태가 바뀌면 표시만 세우고 looper를 깨우지 않는다. 우리는 이벤트가
        // 없으면 잠들어 있으므로, 알림을 가로채 glue의 처리기를 부른 뒤 깨운다.
        //  - 알림은 Java 메인 thread에서 온다. 그래서 값을 원자로 둔다.
        std::atomic<void (*)(GameActivity*, const GameTextInputState*)> glue_text_input_handler { nullptr };
        std::atomic<int> text_input_wake_fd { -1 };

        void on_text_input_event(GameActivity* const activity, const GameTextInputState* const state)
        {
            if (auto* const glue { glue_text_input_handler.load() }; glue != nullptr)
                glue(activity, state);
            if (const int wake_fd { text_input_wake_fd.load() }; wake_fd >= 0)
            {
                const std::uint64_t one { 1 };
                static_cast<void>(write(wake_fd, &one, sizeof(one)));
            }
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
        //  - glue의 `AConfiguration`이 아니라 GameActivity가 Java의 `Configuration`에서 받아 둔 값을 읽는다.
        //    앱이 도는 중에 어두운 모드를 바꾸면 glue가 AssetManager에서 다시 읽은 구성은 옛 값이다.
        [[nodiscard]] bool read_prefers_light(GameActivity* const activity)
        {
            // `Configuration.UI_MODE_NIGHT_MASK`와 `UI_MODE_NIGHT_YES`다.
            constexpr int night_mask { 0x30 };
            constexpr int night_yes { 0x20 };
            return (GameActivity_getUIMode(activity) & night_mask) != night_yes;
        }

        // popup이 떠 있는가, 앱이 뒤로 가기를 받겠다고 했는가(`ui_frame::back`)다. 어느 쪽이든
        // 뒤로 가기를 Activity에 넘기지 않고 받는다. 키 필터는 glue가 Java thread에서 부르므로 원자로 둔다.
        std::atomic<bool> popups_visible { false };
        std::atomic<bool> back_claimed { false };

        // 시스템이 맡는 키는 Activity에 남긴다. 거짓을 돌려주면 Activity의 기본 처리를 받는다.
        //  - 뒤로 가기: 앱 끝내기와 예측 뒤로 가기 애니메이션. 끝날 때 `app_host::shutdown()`이 돈다.
        //  - 볼륨·카메라·전원·미디어 키: 앱이 먹으면 소리 크기도 못 바꾼다. GameActivity의
        //    기본 필터가 거르던 것을 그대로 거른다.
        bool key_event_filter(const GameActivityKeyEvent* const event)
        {
            switch (event->keyCode)
            {
            case AKEYCODE_BACK:
                // popup이 떠 있으면 뒤로 가기는 Esc처럼 popup을 닫고, 앱이 받겠다고 했으면 앱에 간다.
                return popups_visible.load() || back_claimed.load();
            case AKEYCODE_HOME:
            case AKEYCODE_VOLUME_UP:
            case AKEYCODE_VOLUME_DOWN:
            case AKEYCODE_VOLUME_MUTE:
            case AKEYCODE_MUTE:
            case AKEYCODE_CAMERA:
            case AKEYCODE_FOCUS:
            case AKEYCODE_POWER:
            case AKEYCODE_APP_SWITCH:
            case AKEYCODE_MEDIA_PLAY_PAUSE:
            case AKEYCODE_MEDIA_PLAY:
            case AKEYCODE_MEDIA_PAUSE:
            case AKEYCODE_MEDIA_STOP:
            case AKEYCODE_MEDIA_NEXT:
            case AKEYCODE_MEDIA_PREVIOUS:
            case AKEYCODE_HEADSETHOOK:
                return false;
            default:
                return true;
            }
        }

        constexpr std::int64_t nanoseconds_per_millisecond { 1'000'000 };

        // 손가락·펜·마우스(포인터 계열)를 받는다. GameActivity의 기본 필터는 터치스크린만
        // 받아 펜 호버와 마우스가 오지 않는다.
        bool motion_event_filter(const GameActivityMotionEvent* const event)
        {
            return (event->source & AINPUT_SOURCE_CLASS_MASK) == AINPUT_SOURCE_CLASS_POINTER;
        }

        // GameActivity의 움직임 이벤트를 변환층의 값으로 옮긴다. 구조체는 이 호출 동안만 유효하다.
        [[nodiscard]] motion_input copy_motion(const GameActivityMotionEvent& event)
        {
            motion_input input {};
            input.source = event.source;
            input.action = event.action;
            input.flags = event.flags;
            input.action_button = event.actionButton;
            input.button_state = event.buttonState;
            input.meta_state = event.metaState;
            // 현재 표본의 시각은 Java `MotionEvent.getEventTime`과 같은 밀리초다. 과거 표본은
            // 나노초 배열로 온다 (기기에서 `CLOCK_MONOTONIC`과 맞대 확인했다). 키 이벤트의 시각은
            // 나노초라 단위가 서로 다르다.
            input.current.time_ns = event.eventTime * nanoseconds_per_millisecond;
            for (std::uint32_t index { 0 }; index < event.pointerCount; ++index)
            {
                const GameActivityPointerAxes& axes { event.pointers[index] };
                input.current.pointers.push_back({ axes.id, axes.toolType, GameActivityPointerAxes_getX(&axes), GameActivityPointerAxes_getY(&axes) });
            }
            if (event.pointerCount > 0)
            {
                input.vertical_scroll = GameActivityPointerAxes_getAxisValue(&event.pointers[0], AMOTION_EVENT_AXIS_VSCROLL);
                input.horizontal_scroll = GameActivityPointerAxes_getAxisValue(&event.pointers[0], AMOTION_EVENT_AXIS_HSCROLL);
            }
            for (int position { 0 }; position < event.historySize; ++position)
            {
                motion_frame frame {};
                frame.time_ns = event.historicalEventTimesNanos[position];
                for (std::uint32_t index { 0 }; index < event.pointerCount; ++index)
                {
                    const int pointer { static_cast<int>(index) };
                    const float x { GameActivityMotionEvent_getHistoricalX(&event, pointer, position) };
                    const float y { GameActivityMotionEvent_getHistoricalY(&event, pointer, position) };
                    frame.pointers.push_back({ event.pointers[index].id, event.pointers[index].toolType, x, y });
                }
                input.history.push_back(std::move(frame));
            }
            return input;
        }

        [[nodiscard]] key_input copy_key(const GameActivityKeyEvent& event)
        {
            return key_input {
                .action = event.action,
                .key_code = event.keyCode,
                .meta_state = event.metaState,
                .repeat_count = event.repeatCount,
                .unicode_char = event.unicodeChar,
                .time_ns = event.eventTime,
            };
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
        //  - IME가 묻는 창구(`text_input_host`)도 맡는다. 초점을 가진 텍스트 칸을 tree에서 찾는다.
        class application final : public text_input_host
        {
            // 주 표면 위에 겹쳐 그리는 popup 하나다.
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
            };

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

            ~application() override
            {
                // IME 알림이 더는 이 객체를 깨우지 않게 먼저 떼어 낸다.
                text_input_wake_fd.store(-1);
                if (auto* const glue { glue_text_input_handler.exchange(nullptr) }; glue != nullptr)
                    app_->activity->callbacks->onTextInputEvent = glue;
                ime_.reset();
                // 렌더러가 창보다, 그리고 그것이 빌려 쓰는 Vulkan 장치보다 먼저 사라진다.
                // 창은 glue가 쥐고 있다.
                renderer_.reset();
                vulkan_.reset();
                if (wake_fd_ >= 0)
                {
                    ALooper_removeFd(app_->looper, wake_fd_);
                    close(wake_fd_);
                }
                set_font_fallback(nullptr);
                set_system_accent(std::nullopt);
                app_->userData = nullptr;
                app_->onAppCmd = nullptr;
                if (jni_attached_)
                    app_->activity->vm->DetachCurrentThread();
            }

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
                return { std::u8string { value->text }, value->caret, value->anchor, value->applied_sequence };
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

                    update_overlays();
                    process_input();
                    process_text_input();

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
                //  - 시스템 언어를 바꾸면 프로세스는 남고 Activity만 다시 선다. 앞 Activity에서
                //    읽어 둔 언어와 그 언어로 고른 대체 글꼴을 거둔다.
                set_user_language(read_language(app_->config));
                refresh_user_ui_language();
                // 휴대폰·태블릿이다. 창 caption이 없고 맨 위는 앱 바의 자리다.
                // logic thread가 첫 frame을 짓기 전에 정한다.
                set_ui_platform({ .form_factor = ui_form_factor::mobile, .window_caption = false });
                prefers_light_ = read_prefers_light(app_->activity);
                scale_ = read_scale(app_->config);
                set_font_fallback(&font_resolver_);

                // 클립보드와 시스템 테마는 JNI로 다룬다. glue는 이 thread를 JVM에 붙이지 않으므로 여기서
                // 붙인다. 시스템 accent는 logic thread가 첫 frame을 짓기 전에 세운다.
                JavaVM* const vm { app_->activity->vm };
                if (vm->GetEnv(reinterpret_cast<void**>(&jni_), JNI_VERSION_1_6) == JNI_EDETACHED)
                {
                    jni_attached_ = vm->AttachCurrentThread(&jni_, nullptr) == JNI_OK;
                    if (jni_attached_ == false)
                        jni_ = nullptr;
                }
                refresh_system_theme();
                input_.set_dead_key_combiner([this](const char32_t accent, const char32_t character) {
                    if (jni_ == nullptr)
                        return char32_t {};
                    const jclass klass { jni_->FindClass("android/view/KeyCharacterMap") };
                    char32_t combined {};
                    if (klass != nullptr && jni_->ExceptionCheck() == false)
                    {
                        const jmethodID method { jni_->GetStaticMethodID(klass, "getDeadChar", "(II)I") };
                        if (method != nullptr && jni_->ExceptionCheck() == false)
                            combined = static_cast<char32_t>(jni_->CallStaticIntMethod(klass, method, static_cast<jint>(accent), static_cast<jint>(character)));
                    }
                    if (jni_->ExceptionCheck())
                    {
                        jni_->ExceptionClear();
                        combined = {};
                    }
                    if (klass != nullptr)
                        jni_->DeleteLocalRef(klass);
                    return combined;
                });

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

                // IME다. 상태를 넘기고 키보드를 띄우는 일만 GameActivity에 맡긴다.
                ime_ = std::make_unique<ime_session>(*this,
                    ime_session::platform {
                        .set_state = [this](const ime_state& state) { set_ime_state(state); },
                        .show_keyboard =
                            [this](const bool show) {
                                if (show)
                                {
                                    // 한 줄 칸이다. 가로 화면에서 키보드가 칸을 가린 전체 화면 편집기로 바뀌지
                                    // 않게 하고, 완료 동작은 Enter로 보낸다 (APP_CMD_EDITOR_ACTION). IME 연결을
                                    // 다시 세워야 값이 적용된다.
                                    GameActivity_setImeEditorInfo(
                                        app_->activity, TYPE_CLASS_TEXT, IME_ACTION_DONE, static_cast<GameTextInputImeOptions>(IME_FLAG_NO_FULLSCREEN | IME_FLAG_NO_EXTRACT_UI));
                                    GameActivity_restartInput(app_->activity);
                                    GameActivity_showSoftInput(app_->activity, 0);
                                }
                                else
                                    GameActivity_hideSoftInput(app_->activity, 0);
                            },
                        .submit = [this] { post_enter(); },
                    });
                glue_text_input_handler.store(app_->activity->callbacks->onTextInputEvent);
                text_input_wake_fd.store(wake_fd_);
                app_->activity->callbacks->onTextInputEvent = &on_text_input_event;

                app_->userData = this;
                app_->onAppCmd = &application::on_app_command;
                android_app_set_key_event_filter(app_, &key_event_filter);
                android_app_set_motion_event_filter(app_, &motion_event_filter);
                // 휠 값은 켠 축만 GameActivity가 옮겨 준다.
                GameActivityPointerAxes_enableAxis(AMOTION_EVENT_AXIS_VSCROLL);
                GameActivityPointerAxes_enableAxis(AMOTION_EVENT_AXIS_HSCROLL);

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
                    // 창이 사라지면 그 위의 접촉은 뗌 없이 끝난다. 조합 중이면 확정하고 키보드를
                    // 내린다. 창이 다시 생기면 초점 칸을 IME에 다시 붙인다.
                    post_events(input_.cancel_all(std::chrono::steady_clock::now()));
                    ime_->detach();
                    // glue는 이 알림이 끝날 때까지 창을 쥐고 있다가 놓는다.
                    // 그 안에서 렌더러를 버려야 놓인 창을 붙잡은 채로 남지 않는다.
                    renderer_.reset();
                    update_deadline_.reset();
                    reveal_pending_ = false;
                    // 장치는 남긴다. 창이 다시 생기면 스왑체인만 새로 선다. 그릴 표면이 없는
                    // 동안 GPU 메모리는 돌려준다.
                    if (vulkan_ != nullptr)
                        vulkan_->release_resources();
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
                    prefers_light_ = read_prefers_light(app_->activity);
                    scale_ = read_scale(app_->config);
                    refresh_system_theme();
                    refresh_metrics();
                    break;
                case APP_CMD_SOFTWARE_KB_VIS_CHANGED:
                    // 사용자가 내린 키보드다 (뒤로 가기, 키보드의 내리기 단추). 같은 칸을 다시
                    // 누르면 다시 띄운다.
                    ime_->set_keyboard_visible(app_->softwareKeyboardVisible);
                    break;
                case APP_CMD_EDITOR_ACTION:
                    // 키보드의 완료 단추는 Enter다 (기본 단추 실행, 한 줄 칸의 확정).
                    //  - IME는 조합을 확정한 **뒤** 완료 동작을 보내지만, 확정은 깨우기 fd로, 완료는
                    //    명령 pipe로 와서 looper가 완료를 먼저 꺼낼 수 있다. 확정을 먼저 초안으로
                    //    보내야 앱이 마지막 글자가 빠진 글로 제출·검색하지 않는다. 편집은 곧바로
                    //    logic으로 가고 Enter는 입력 thread를 거치므로 이 순서가 그대로 이어진다.
                    process_text_input();
                    post_enter();
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
                    // 대비 설정은 구성 변경으로 오지 않는다. 설정 앱에서 돌아올 때 다시 읽는다.
                    refresh_system_theme();
                    dirty_ = true;
                    break;
                case APP_CMD_GAINED_FOCUS:
                    dirty_ = true;
                    break;
                case APP_CMD_LOST_FOCUS:
                    // 다른 창(알림 창, 다른 앱)으로 초점이 갔다.
                    static_cast<void>(dismiss_popups(popup_dismiss_reason::activation_changed));
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
                // 한 번 물러선 Activity는 Vulkan을 다시 시도하지 않는다. 생성 함수가 없으면
                // `automatic`은 처음부터 CPU다 (물러선 것으로 기록된다).
                if (gpu_abandoned_ == false)
                    factories.create_gpu = [this, window] { return create_gpu_renderer(window); };
                factories.create_cpu = [window] { return create_cpu_skia_renderer(window); };
                // 생성 시점 실패는 위의 생성 함수가 낸다 (`create_gpu_renderer`).
                const renderer_fault_injection fault { .at_creation = false, .after_frames = config_.simulate_gpu_loss_after_frames };
                std::u8string error {};
                renderer_ = renderer_host::create(config_.renderer, fault, std::move(factories), error);
                if (renderer_ == nullptr)
                {
                    log_error(error);
                    finish();
                    return;
                }

                const renderer_backend backend { renderer_->backend() };
                const std::u8string_view name { renderer_backend_name(backend) };
                const bool vulkan { backend == renderer_backend::vulkan };
                __android_log_print(ANDROID_LOG_INFO, log_tag, "renderer %.*s%s%s%s", static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()), vulkan ? " on " : "",
                    vulkan ? vulkan_->name() : "", renderer_->used_fallback() ? " (fallback)" : "");
                settle_gpu_state();
            }

            // Vulkan 렌더러를 만든다. 장치는 처음 한 번만 세우고 창마다 다시 쓴다.
            [[nodiscard]] renderer_factory_result create_gpu_renderer(ANativeWindow* const window)
            {
                renderer_factory_result result {};
                if (config_.simulate_gpu_failure)
                    result.error = u8"The smoke test injected a Vulkan creation failure.";
                else
                {
                    if (vulkan_ == nullptr)
                    {
                        vulkan_ = vulkan_device::create(result.error);
                        if (vulkan_ != nullptr)
                            __android_log_print(ANDROID_LOG_INFO, log_tag, "vulkan device created: %s", vulkan_->name());
                    }
                    if (vulkan_ != nullptr)
                        result = create_vulkan_skia_renderer(*vulkan_, window);
                }
                // `automatic`의 물러섬은 이 이유를 버리므로 여기서 남긴다.
                if (result.renderer == nullptr)
                    log_error(u8"Vulkan renderer: " + result.error);
                return result;
            }

            // CPU로 물러섰으면 그 Activity에서는 Vulkan을 다시 쓰지 않고 장치를 놓는다.
            // 물러선 렌더러는 이미 사라졌으므로(`renderer_host::switch_to_cpu`) 장치를 빌려 쓰는
            // 것이 남아 있지 않다.
            void settle_gpu_state()
            {
                if (renderer_ == nullptr || renderer_->backend() != renderer_backend::cpu || renderer_->used_fallback() == false)
                    return;
                if (gpu_abandoned_ == false)
                    __android_log_print(ANDROID_LOG_WARN, log_tag, "renderer fell back to cpu for the rest of this activity");
                gpu_abandoned_ = true;
                vulkan_.reset();
            }

            // 창 크기·안전 영역·배율을 다시 읽어 렌더러와 앱에 알린다.
            void refresh_metrics()
            {
                const int previous_window_width { window_width_ };
                const int previous_window_height { window_height_ };
                if (app_->window != nullptr)
                {
                    window_width_ = ANativeWindow_getWidth(app_->window);
                    window_height_ = ANativeWindow_getHeight(app_->window);
                }
                const int previous_bottom { insets_.bottom };
                const int previous_width { content_width() };
                const int previous_height { content_height() };
                insets_ = read_safe_insets(app_->activity);
                // 내용 크기가 바뀌면 popup은 새 크기로 다시 자리 잡는다. 닫는 계기는 창 자체가 바뀐
                // 때(회전, 화면 나누기)뿐이다. 키보드는 창이 아니라 안전 영역만 줄인다 — popup 안 검색
                // 칸을 누르면 키보드가 뜨는데, 그것으로 popup이 닫히면 칠 수가 없다.
                if (content_width() != previous_width || content_height() != previous_height)
                {
                    if (window_width_ != previous_window_width || window_height_ != previous_window_height)
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_resized));
                    overlay_frame_.reset();
                }
                // 키보드가 올라와 아래가 줄었다. 줄어든 크기로 다시 지은 frame이 오면 초점 칸을
                // 다시 드러내 달라고 보낸다 (`render`).
                if (insets_.bottom > previous_bottom && ime_ != nullptr && ime_->target().has_value())
                    reveal_pending_ = true;
                else if (insets_.bottom < previous_bottom)
                    reveal_pending_ = false;
                // 표면과 안전 영역이 바뀔 때만 한 줄 남긴다. 기기마다 다른 막대·컷아웃 배치를
                // logcat에서 바로 맞대 보는 자리다.
                __android_log_print(ANDROID_LOG_INFO, log_tag, "surface %dx%d, safe insets %d,%d,%d,%d, scale %.2f", window_width_, window_height_, insets_.left, insets_.top, insets_.right,
                    insets_.bottom, static_cast<double>(scale_));
                if (renderer_ != nullptr)
                {
                    std::u8string error {};
                    if (renderer_->resize(window_width_, window_height_, error) == false)
                        log_error(error);
                    settle_gpu_state();
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

            // 시스템의 동적 색과 대비 설정을 읽는다. accent는 앱이 테마 목록에 보일 수 있게 프로세스에 세운다.
            void refresh_system_theme()
            {
                if (jni_ == nullptr)
                    return;
                set_system_accent(read_dynamic_accent(*jni_, app_->activity->javaGameActivity));
                high_contrast_ = read_high_contrast(*jni_, app_->activity->javaGameActivity);
            }

            // 상태 표시줄·내비게이션 막대의 아이콘을 그린 바탕에 맞춘다. 바뀔 때만 메인 thread에 넘긴다.
            void update_system_bars(const color_theme theme)
            {
                const bool light { theme == color_theme::light || (theme == color_theme::high_contrast && prefers_light_) };
                if (system_bars_light_.has_value() && *system_bars_light_ == light)
                    return;
                if (post_to_main_thread([light](JNIEnv& env, const jobject activity) { apply_system_bar_icons(env, activity, light); }))
                    system_bars_light_ = light;
            }

            // 앱이 실은 뒤로 가기 동작을 낸다. 없으면 Activity를 끝낸다.
            void navigate_back()
            {
                if (overlay_frame_ != nullptr && overlay_frame_->back != nullptr)
                {
                    dispatch(overlay_frame_->back());
                    return;
                }
                finish();
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
                if (renderer_ == nullptr)
                    return -1;
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
                for (const clipboard_request& request : host_->take_clipboard_requests())
                    execute_clipboard(request);
            }

            // glue가 모아 둔 입력을 luil 입력 이벤트로 옮겨 input thread에 보낸다.
            void process_input()
            {
                android_input_buffer* const inputs { android_app_swap_input_buffers(app_) };
                if (inputs == nullptr)
                    return;
                // tree는 안전 영역에서 그려지므로 창 좌표에서 그 원점을 뺀다.
                const surface_mapping mapping { static_cast<float>(insets_.left), static_cast<float>(insets_.top), scale_ };
                for (std::uint64_t index { 0 }; index < inputs->motionEventsCount; ++index)
                    post_events(input_.translate(copy_motion(inputs->motionEvents[index]), mapping));
                for (std::uint64_t index { 0 }; index < inputs->keyEventsCount; ++index)
                {
                    const GameActivityKeyEvent& key { inputs->keyEvents[index] };
                    // 뒤로 가기는 popup이 떠 있거나 앱이 받겠다고 했을 때만 여기로 온다
                    // (`key_event_filter`). 떼는 순간 Esc처럼 popup을 닫고, 닫을 popup이 없으면 앱의
                    // 뒤로 가기 동작을, 그것도 없으면 뒤로 가기의 기본 동작을 한다.
                    if (key.keyCode == AKEYCODE_BACK)
                    {
                        if (key.action == AKEY_EVENT_ACTION_UP && dismiss_popups(popup_dismiss_reason::escape_key) == false)
                            navigate_back();
                        continue;
                    }
                    // Esc가 popup을 닫았으면 키를 삼킨다 (Win32와 같다).
                    if (key.keyCode == AKEYCODE_ESCAPE && key.action == AKEY_EVENT_ACTION_DOWN && dismiss_popups(popup_dismiss_reason::escape_key))
                        continue;
                    post_events(input_.translate(copy_key(key)));
                }
                android_app_clear_motion_events(inputs);
                android_app_clear_key_events(inputs);
            }

            void post_events(std::vector<raw_input_event> events)
            {
                for (raw_input_event& event : events)
                {
                    // 겹쳐 그린 popup의 자리면 그 표면 id와 popup 좌표를 싣는다.
                    routed_input routed { router_.route(std::move(event)) };
                    // popup 밖의 누름·휠은 닫는 계기다. 누름·휠 자체는 그대로 간다 (Win32와 같다).
                    if (routed.pressed_outside)
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::pointer_press_outside));
                    if (routed.wheel_outside)
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::wheel_scrolled));
                    for (raw_input_event& value : routed.events)
                    {
                        // 키보드를 내린 뒤 초점을 가진 텍스트 칸을 다시 누르면 키보드를 다시 띄운다.
                        if (const auto* const released { std::get_if<pointer_released_event>(&value) }; released != nullptr && released->device != pointer_device::mouse)
                            request_keyboard_at(released->surface, released->x, released->y);
                        host_->post_raw_input(std::move(value));
                    }
                }
            }

            // frame의 popup을 주 표면 위의 layer로 세운다. frame이 바뀔 때만 다시 세운다.
            //  - 자리는 앵커 기준 논리 픽셀이다. 주 표면(앵커가 빈 것)의 popup만 그린다 — 보조 창이
            //    없으니 다른 앵커는 붙을 곳이 없다.
            //  - 화면 밖으로 나가면 안으로 들인다. 앱 모델의 자리는 바꾸지 않는다.
            //  - 새 frame이 같은 popup을 실으면 닫자고 한 표식을 푼다 (Win32 `popup_surface::adopt`와 같다).
            void update_overlays()
            {
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame == overlay_frame_ && frame != nullptr)
                    return;
                overlay_frame_ = frame;
                overlays_.clear();
                std::vector<overlay_area> areas {};
                if (frame != nullptr)
                {
                    const float scale { scale_ };
                    const int limit_width { content_width() };
                    const int limit_height { content_height() };
                    for (const ui_popup& popup : frame->popups)
                    {
                        if (popup.id.empty() || popup.anchor.empty() == false || popup.tree == nullptr || popup.width <= 0.0f || popup.height <= 0.0f)
                            continue;
                        const int width { std::min(limit_width, static_cast<int>(std::lround(popup.width * scale))) };
                        const int height { std::min(limit_height, static_cast<int>(std::lround(popup.height * scale))) };
                        const int x { std::clamp(static_cast<int>(std::lround(popup.x * scale)), 0, limit_width - width) };
                        const int y { std::clamp(static_cast<int>(std::lround(popup.y * scale)), 0, limit_height - height) };
                        overlays_.push_back(overlay_entry { popup.id, popup.tree, { insets_.left + x, insets_.top + y, width, height }, popup.border, popup.dismiss, false });
                        areas.push_back(overlay_area { popup.id, { static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height) } });
                    }
                    if (frame->windows.empty() == false && warned_windows_ == false)
                    {
                        warned_windows_ = true;
                        log_error(u8"Secondary windows (ui_frame::windows) are not supported on Android and are ignored.");
                    }
                }
                router_.set_areas(std::move(areas));
                popups_visible.store(overlays_.empty() == false);
                back_claimed.store(frame != nullptr && frame->back != nullptr);
                dirty_ = true;
            }

            [[nodiscard]] const overlay_entry* find_overlay(const std::u8string& id) const
            {
                const auto found { std::ranges::find_if(overlays_, [&id](const overlay_entry& entry) { return entry.id == id; }) };
                return found != overlays_.end() ? &*found : nullptr;
            }

            // 닫힘 계기 하나를 popup들에 묻는다. 하나라도 닫자는 메시지를 냈으면 참이다.
            bool dismiss_popups(const popup_dismiss_reason reason)
            {
                bool dismissed { false };
                for (overlay_entry& entry : overlays_)
                    if (std::optional<input_action> action { take_popup_dismiss_action(entry.dismiss, entry.requested, reason) }; action.has_value())
                    {
                        dispatch(std::move(*action));
                        dismissed = true;
                    }
                return dismissed;
            }

            void post_enter()
            {
                host_->post_raw_input(key_pressed_event { key_code::enter, false, false, false, false, std::chrono::steady_clock::now() });
            }

            void request_keyboard_at(const std::u8string& surface, const float x, const float y)
            {
                if (ime_ == nullptr || ime_->target().has_value() == false || host_->acquire_interaction().focused_surface != surface)
                    return;
                const text_location location { focused_text_location() };
                if (location.element != nullptr && location.tree->hit_test(x, y) == location.element)
                    ime_->request_keyboard();
            }

            // IME가 고친 상태를 받고, 앱의 초점·글이 바뀌었으면 IME에 넘긴다.
            void process_text_input()
            {
                // TERM_WINDOW에서 떼어 낸 IME를 표면 없는 동안 다시 붙이지 않는다.
                if (renderer_ == nullptr)
                {
                    app_->textInputState = 0;
                    return;
                }
                // 표시는 glue가 Java thread에서 세운다. 읽는 쪽은 이 thread 하나다.
                if (app_->textInputState != 0)
                {
                    app_->textInputState = 0;
                    GameActivity_getTextInputState(
                        app_->activity, [](void* const context, const GameTextInputState* const state) { static_cast<application*>(context)->accept_ime_state(*state); }, this);
                }
                ime_->synchronize();
            }

            void accept_ime_state(const GameTextInputState& state)
            {
                ime_state value {};
                // Java의 글은 modified UTF-8로 온다.
                value.text = utf8_from_modified_utf8(std::string_view { state.text_UTF8, static_cast<std::size_t>(std::max(0, state.text_length)) });
                value.selection = { state.selection.start, state.selection.end };
                value.composing = { state.composingRegion.start, state.composingRegion.end };
                ime_->accept(value);
            }

            void set_ime_state(const ime_state& state)
            {
                const std::string text { modified_utf8_from_utf8(state.text) };
                GameTextInputState value {};
                value.text_UTF8 = text.c_str();
                value.text_length = static_cast<std::int32_t>(text.size());
                value.selection = { state.selection.start, state.selection.end };
                value.composingRegion = { state.composing.start, state.composing.end };
                GameActivity_setTextInputState(app_->activity, &value);
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
                {
                    location.tree = host_->acquire_ui_tree();
                    location.origin_x = static_cast<float>(insets_.left);
                    location.origin_y = static_cast<float>(insets_.top);
                }
                else if (const overlay_entry* const overlay { find_overlay(interaction.focused_surface) }; overlay != nullptr)
                {
                    location.tree = overlay->tree;
                    location.origin_x = static_cast<float>(overlay->bounds.x);
                    location.origin_y = static_cast<float>(overlay->bounds.y);
                }
                if (location.tree != nullptr)
                    location.element = location.tree->find(interaction.focused_input);
                return location;
            }

            void dispatch(input_action action)
            {
                if (auto* const message { std::get_if<app_message>(&action) }; message != nullptr && message->empty() == false)
                    host_->post_app_message(std::move(*message));
            }

            // 클립보드 요청이다. 붙여넣기는 그 칸의 insert 편집으로 앱에 보낸다 (Win32와 같다).
            void execute_clipboard(const clipboard_request& request)
            {
                if (jni_ == nullptr)
                    return;
                const jobject context { app_->activity->javaGameActivity };
                if (const auto* const copy { std::get_if<clipboard_copy_request>(&request) }; copy != nullptr)
                {
                    if (copy_text_to_clipboard(*jni_, context, copy->text) == false)
                        log_error(u8"Failed to copy the text to the clipboard.");
                    return;
                }
                const auto* const paste { std::get_if<clipboard_paste_request>(&request) };
                if (paste == nullptr || environment_.policy == nullptr)
                    return;
                std::optional<std::u8string> text { read_text_from_clipboard(*jni_, context) };
                if (text.has_value() == false || text->empty())
                    return;
                text_edit_request edit {};
                edit.target = paste->target;
                edit.command = text::text_edit_command::insert;
                edit.text = std::move(*text);
                dispatch(environment_.policy->make_text_edit_action(edit));
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
                const interaction_snapshot interaction { host_->acquire_interaction() };
                state.interaction = interaction_for_surface(interaction, std::u8string {});
                // popup은 주 표면 위의 layer다. 표면마다 자기 상호작용만 남겨 그린다.
                update_overlays();
                std::vector<overlay_layer> layers {};
                layers.reserve(overlays_.size());
                for (const overlay_entry& entry : overlays_)
                    layers.push_back(overlay_layer { entry.tree.get(), entry.bounds, entry.border, interaction_for_surface(interaction, entry.id) });
                state.overlays = layers;
                const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
                // 고대비는 시스템 대비 설정이 높음일 때다. Android는 시스템 고대비 색을 주지 않아 밝은
                // 모드에 맞춘 기본 색을 쓴다.
                state.theme = resolve_color_theme(appearance.theme, high_contrast_, prefers_light_);
                state.high_contrast = default_high_contrast_colors(prefers_light_);
                update_system_bars(state.theme);
                state.accent_id = appearance.accent_id;
                state.style = frame != nullptr ? frame->style.get() : nullptr;

                const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
                set_configured_fonts(fonts.ui_family, fonts.code_family);
                const sk_sp<SkTypeface> code { configured_code_typeface() };
                state.code_typeface = code.get();
                state.fonts = &font_resolver_;

                const std::shared_ptr<const ui_tree> tree { host_->acquire_ui_tree() };
                state.tree = tree.get();
                // 키보드가 올라온 뒤 줄어든 크기로 지은 frame이다. 초점 칸을 드러내 달라고 보낸다.
                if (reveal_pending_ && tree != nullptr && tree->root() != nullptr && std::abs(tree->root()->bounds().height - static_cast<float>(content_height())) < 1.0f)
                {
                    reveal_pending_ = false;
                    host_->post_raw_input(focus_reveal_event { interaction.focused_surface });
                }

                std::u8string error {};
                if (renderer_->render(state, error) == false)
                {
                    log_error(error);
                    // GPU를 요구한 모드는 물러서지 않는다. 그릴 길이 없으므로 끝낸다.
                    if (renderer_mode_requires_gpu(config_.renderer))
                        finish();
                }
                else if (error.empty() == false)
                {
                    // CPU로 물러서며 그린 frame이다. 물러선 이유가 여기 남는다.
                    log_error(error);
                }
                settle_gpu_state();
                dirty_ = false;

                // 시간이 흘러야 바뀌는 그림(애니메이션·tooltip 지연)의 다음 시각이다.
                const auto now { std::chrono::steady_clock::now() };
                update_deadline_ = tree != nullptr ? tree->next_update(update_context { now }, state.interaction) : std::nullopt;
                for (const overlay_layer& layer : layers)
                    if (layer.tree != nullptr)
                        if (const auto next { layer.tree->next_update(update_context { now }, layer.interaction) }; next.has_value())
                            if (update_deadline_.has_value() == false || *next < *update_deadline_)
                                update_deadline_ = next;
                if (update_deadline_.has_value() && *update_deadline_ <= now)
                    update_deadline_ = now + continuous_repaint_interval;
            }

            android_app* app_;
            application_config config_;
            application_environment environment_;
            int wake_fd_ { -1 };
            std::unique_ptr<app_host> host_ {};
            std::unique_ptr<renderer_host> renderer_ {};
            input_translator input_ {};
            std::vector<overlay_entry> overlays_ {};
            // layer를 세운 frame이다. 바뀌면 다시 세운다.
            std::shared_ptr<const ui_frame> overlay_frame_ {};
            overlay_input_router router_ {};
            bool warned_windows_ { false };
            std::unique_ptr<ime_session> ime_ {};
            // 키보드가 올라와 줄어든 frame이 오면 초점 칸을 드러내 달라고 보낸다.
            bool reveal_pending_ { false };
            // 이 thread의 JNI 환경이다. 우리가 붙였으면 끝날 때 뗀다.
            JNIEnv* jni_ { nullptr };
            bool jni_attached_ { false };
            // Activity 하나의 Vulkan 장치다. 창이 사라져도 남고, CPU로 물러서면 놓는다.
            std::unique_ptr<vulkan_device> vulkan_ {};
            // CPU로 물러선 뒤다. 다시 생긴 창도 CPU로 그린다.
            bool gpu_abandoned_ { false };
            registry_font_resolver font_resolver_ {};
            int window_width_ { 0 };
            int window_height_ { 0 };
            edge_insets insets_ {};
            float scale_ { 1.0f };
            bool prefers_light_ { false };
            bool high_contrast_ { false };
            // 마지막으로 맞춘 막대 아이콘이다 (밝은 바탕이면 참).
            std::optional<bool> system_bars_light_ {};
            bool dirty_ { false };
            bool finishing_ { false };
            std::optional<std::chrono::steady_clock::time_point> update_deadline_ {};
            // 마지막으로 앱에 알린 크기와 배율이다.
            float reported_width_ { -1.0f };
            float reported_height_ { -1.0f };
            float reported_scale_ { -1.0f };
        };
    } // namespace

    void apply_debug_properties(application_config& config)
    {
        const std::string renderer { read_property("debug.luil.renderer") };
        if (const auto mode { parse_renderer_mode(std::u8string_view { reinterpret_cast<const char8_t*>(renderer.data()), renderer.size() }) }; mode.has_value())
            config.renderer = *mode;
        if (const std::string failure { read_property("debug.luil.simulate_gpu_failure") }; failure.empty() == false)
            config.simulate_gpu_failure = failure == "1" || failure == "true";
        const std::string loss { read_property("debug.luil.simulate_gpu_loss_after_frames") };
        if (int frames { 0 }; std::from_chars(loss.data(), loss.data() + loss.size(), frames).ec == std::errc {} && frames >= 0)
            config.simulate_gpu_loss_after_frames = frames;
    }

    int run_application(android_app* const app, const application_config& config, const application_environment& environment)
    {
        if (app == nullptr)
            return 1;
        // JNI를 쓰는 층(HTTP client)이 Activity 없이 JavaVM을 얻는 자리다. 앱이 `on_started`에서
        // client를 세울 수 있도록 무엇보다 먼저 적는다.
        set_java_vm(app->activity->vm);
        application instance { app, config, environment };
        return instance.run();
    }
} // namespace luil::android

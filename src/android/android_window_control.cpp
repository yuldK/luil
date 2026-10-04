#include "android/android_window_control.h"

#include "android/device_request.h"

#include <android/api-level.h>
#include <android/log.h>

namespace luil::android {
    namespace {
        // Java 예외가 났으면 거두고 참이다.
        [[nodiscard]] bool failed(JNIEnv& env) noexcept
        {
            if (env.ExceptionCheck() == false)
                return false;
            env.ExceptionClear();
            return true;
        }

        // 지역 참조 틀이다. 한 번의 조작이 쓴 지역 참조를 한 번에 놓는다.
        class local_frame
        {
        public:
            explicit local_frame(JNIEnv& env) noexcept
                : env_ { env }
                , pushed_ { env.PushLocalFrame(16) == 0 }
            {}

            local_frame(const local_frame&) = delete;
            local_frame(local_frame&&) = delete;
            local_frame& operator=(const local_frame&) = delete;
            local_frame& operator=(local_frame&&) = delete;

            ~local_frame()
            {
                if (pushed_)
                    env_.PopLocalFrame(nullptr);
            }

            [[nodiscard]] bool pushed() const noexcept
            {
                return pushed_;
            }

        private:
            JNIEnv& env_;
            bool pushed_ { false };
        };

        // `Configuration.ORIENTATION_LANDSCAPE`다.
        constexpr jint configuration_landscape { 2 };
        // `WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE`(API 30)다.
        constexpr jint transient_bars_by_swipe { 2 };
        // `View.SYSTEM_UI_FLAG_HIDE_NAVIGATION`·`FULLSCREEN`·`IMMERSIVE_STICKY`다 (API 30 아래).
        constexpr jint immersive_flags { 0x2 | 0x4 | 0x1000 };

        // 객체의 인자 없는 메서드 하나를 불러 객체를 받는다. 실패하면 nullptr다.
        [[nodiscard]] jobject call_object(JNIEnv& env, const jobject target, const char* const name, const char* const signature)
        {
            if (target == nullptr)
                return nullptr;
            const jclass type { env.GetObjectClass(target) };
            const jmethodID method { env.GetMethodID(type, name, signature) };
            if (method == nullptr || failed(env))
                return nullptr;
            const jobject result { env.CallObjectMethod(target, method) };
            return failed(env) ? nullptr : result;
        }

        // 객체의 인자 없는 int 메서드다. 실패하면 nullopt다.
        [[nodiscard]] std::optional<jint> call_int(JNIEnv& env, const jobject target, const char* const name)
        {
            if (target == nullptr)
                return std::nullopt;
            const jclass type { env.GetObjectClass(target) };
            const jmethodID method { env.GetMethodID(type, name, "()I") };
            if (method == nullptr || failed(env))
                return std::nullopt;
            const jint result { env.CallIntMethod(target, method) };
            if (failed(env))
                return std::nullopt;
            return result;
        }

        // 지금 화면의 회전이다 (`Surface.ROTATION_*`). 읽지 못하면 -1이다.
        //  - Android 11부터는 Activity가 사는 디스플레이(`getDisplay`)를, 그 아래는 기본 디스플레이를 본다.
        [[nodiscard]] jint read_rotation(JNIEnv& env, const jobject activity)
        {
            jobject display { nullptr };
            if (android_get_device_api_level() >= 30)
                display = call_object(env, activity, "getDisplay", "()Landroid/view/Display;");
            else
                display = call_object(env, call_object(env, activity, "getWindowManager", "()Landroid/view/WindowManager;"), "getDefaultDisplay", "()Landroid/view/Display;");
            return call_int(env, display, "getRotation").value_or(-1);
        }

        // 지금 구성이 가로인가다. 회전과 함께 읽어야 자연 방향이 가로인 기기에서도 맞는 고정 값을 고른다.
        [[nodiscard]] bool read_landscape(JNIEnv& env, const jobject activity)
        {
            const jobject resources { call_object(env, activity, "getResources", "()Landroid/content/res/Resources;") };
            const jobject configuration { call_object(env, resources, "getConfiguration", "()Landroid/content/res/Configuration;") };
            if (configuration == nullptr)
                return false;
            const jclass type { env.GetObjectClass(configuration) };
            const jfieldID field { env.GetFieldID(type, "orientation", "I") };
            if (field == nullptr || failed(env))
                return false;
            return env.GetIntField(configuration, field) == configuration_landscape;
        }
    } // namespace

    void apply_screen_orientation(JNIEnv& env, const jobject activity, const screen_orientation request, std::optional<int>& original)
    {
        const local_frame frame { env };
        if (frame.pushed() == false)
            return;
        if (original.has_value() == false)
        {
            const std::optional<jint> value { call_int(env, activity, "getRequestedOrientation") };
            if (value.has_value() == false)
                return;
            original = *value;
        }

        const jint rotation { request == screen_orientation::current ? read_rotation(env, activity) : -1 };
        const bool landscape { request == screen_orientation::current && read_landscape(env, activity) };
        const int value { activity_orientation(request, rotation, landscape).value_or(*original) };

        const jclass type { env.GetObjectClass(activity) };
        const jmethodID set_requested { env.GetMethodID(type, "setRequestedOrientation", "(I)V") };
        if (set_requested == nullptr || failed(env))
            return;
        env.CallVoidMethod(activity, set_requested, static_cast<jint>(value));
        if (failed(env))
            return;
        // 기기마다 다른 회전과 고른 값을 logcat에서 바로 맞대 보는 자리다.
        __android_log_print(ANDROID_LOG_INFO, "luil", "requested orientation %d (rotation %d, landscape %d)", value, static_cast<int>(rotation), landscape ? 1 : 0);
    }

    void apply_system_bars(JNIEnv& env, const jobject activity, const system_bars_mode mode)
    {
        const local_frame frame { env };
        if (frame.pushed() == false)
            return;
        const bool hide { mode == system_bars_mode::immersive };
        const jobject window { call_object(env, activity, "getWindow", "()Landroid/view/Window;") };
        if (window == nullptr)
            return;

        if (android_get_device_api_level() >= 30)
        {
            const jobject controller { call_object(env, window, "getInsetsController", "()Landroid/view/WindowInsetsController;") };
            const jclass types { env.FindClass("android/view/WindowInsets$Type") };
            const jclass controller_type { env.FindClass("android/view/WindowInsetsController") };
            if (controller == nullptr || types == nullptr || controller_type == nullptr || failed(env))
                return;
            const jmethodID system_bars { env.GetStaticMethodID(types, "systemBars", "()I") };
            const jmethodID set_behavior { env.GetMethodID(controller_type, "setSystemBarsBehavior", "(I)V") };
            const jmethodID change { env.GetMethodID(controller_type, hide ? "hide" : "show", "(I)V") };
            if (system_bars == nullptr || set_behavior == nullptr || change == nullptr || failed(env))
                return;
            const jint bars { env.CallStaticIntMethod(types, system_bars) };
            if (failed(env))
                return;
            // 쓸어 꺼낸 막대가 잠시 뒤 다시 숨는 몰입 모드다. 보일 때도 행동을 그대로 두어 다음 숨김이 같다.
            if (hide)
            {
                env.CallVoidMethod(controller, set_behavior, transient_bars_by_swipe);
                if (failed(env))
                    return;
            }
            env.CallVoidMethod(controller, change, bars);
            static_cast<void>(failed(env));
            return;
        }

        // API 30 아래는 decor view의 표시 플래그다. 막대 아이콘 색의 플래그(`apply_system_bar_icons`)와 같은
        // 칸을 나눠 쓰므로 읽어서 몰입 플래그만 바꾼다.
        const jobject decor { call_object(env, window, "getDecorView", "()Landroid/view/View;") };
        const std::optional<jint> flags { call_int(env, decor, "getSystemUiVisibility") };
        if (flags.has_value() == false)
            return;
        const jclass view_type { env.GetObjectClass(decor) };
        const jmethodID set_flags { env.GetMethodID(view_type, "setSystemUiVisibility", "(I)V") };
        if (set_flags == nullptr || failed(env))
            return;
        env.CallVoidMethod(decor, set_flags, hide ? (*flags | immersive_flags) : (*flags & ~immersive_flags));
        static_cast<void>(failed(env));
    }
} // namespace luil::android

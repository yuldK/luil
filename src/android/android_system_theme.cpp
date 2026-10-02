#include "android/android_system_theme.h"

#include <android/api-level.h>

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

        // 지역 참조 틀이다. 한 번의 조회가 쓴 지역 참조를 한 번에 놓는다.
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

        // `View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR`(API 23)와 `SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR`(API 26)다.
        constexpr jint light_status_bar_flag { 0x2000 };
        constexpr jint light_navigation_bar_flag { 0x10 };
        // `WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS`·`APPEARANCE_LIGHT_NAVIGATION_BARS`(API 30)다.
        constexpr jint light_status_bars_appearance { 8 };
        constexpr jint light_navigation_bars_appearance { 16 };

        [[nodiscard]] jobject activity_window(JNIEnv& env, const jobject activity)
        {
            const jclass type { env.GetObjectClass(activity) };
            const jmethodID get_window { env.GetMethodID(type, "getWindow", "()Landroid/view/Window;") };
            if (get_window == nullptr || failed(env))
                return nullptr;
            const jobject window { env.CallObjectMethod(activity, get_window) };
            return failed(env) ? nullptr : window;
        }
    } // namespace

    std::optional<ui_color> read_dynamic_accent(JNIEnv& env, const jobject context)
    {
        if (android_get_device_api_level() < 31)
            return std::nullopt;
        const local_frame frame { env };
        if (frame.pushed() == false)
            return std::nullopt;

        // 자원 번호는 판마다 다를 수 있어 상수로 박지 않고 `android.R.color`에서 읽는다.
        const jclass colors { env.FindClass("android/R$color") };
        if (colors == nullptr || failed(env))
            return std::nullopt;
        const jfieldID field { env.GetStaticFieldID(colors, "system_accent1_500", "I") };
        if (field == nullptr || failed(env))
            return std::nullopt;
        const jint id { env.GetStaticIntField(colors, field) };

        const jclass context_type { env.FindClass("android/content/Context") };
        if (context_type == nullptr || failed(env))
            return std::nullopt;
        const jmethodID get_color { env.GetMethodID(context_type, "getColor", "(I)I") };
        if (get_color == nullptr || failed(env))
            return std::nullopt;
        const jint color { env.CallIntMethod(context, get_color, id) };
        if (failed(env))
            return std::nullopt;
        // Android의 색은 ARGB 정수이고 `ui_color`도 같은 차례다.
        return static_cast<ui_color>(static_cast<std::uint32_t>(color));
    }

    bool read_high_contrast(JNIEnv& env, const jobject context)
    {
        if (android_get_device_api_level() < 34)
            return false;
        const local_frame frame { env };
        if (frame.pushed() == false)
            return false;

        const jclass context_type { env.FindClass("android/content/Context") };
        if (context_type == nullptr || failed(env))
            return false;
        const jmethodID get_service { env.GetMethodID(context_type, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") };
        if (get_service == nullptr || failed(env))
            return false;
        const jstring name { env.NewStringUTF("uimode") };
        const jobject manager { env.CallObjectMethod(context, get_service, name) };
        if (manager == nullptr || failed(env))
            return false;
        const jclass manager_type { env.FindClass("android/app/UiModeManager") };
        if (manager_type == nullptr || failed(env))
            return false;
        const jmethodID get_contrast { env.GetMethodID(manager_type, "getContrast", "()F") };
        if (get_contrast == nullptr || failed(env))
            return false;
        const jfloat contrast { env.CallFloatMethod(manager, get_contrast) };
        if (failed(env))
            return false;
        // 표준 0, 중간 0.5, 높음 1이다. 높음만 고대비로 본다.
        return contrast >= 0.99f;
    }

    high_contrast_colors default_high_contrast_colors(const bool prefers_light) noexcept
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

    void apply_system_bar_icons(JNIEnv& env, const jobject activity, const bool light_background)
    {
        const local_frame frame { env };
        if (frame.pushed() == false)
            return;
        const jobject window { activity_window(env, activity) };
        if (window == nullptr)
            return;
        const jclass window_type { env.FindClass("android/view/Window") };
        if (window_type == nullptr || failed(env))
            return;

        if (android_get_device_api_level() >= 30)
        {
            const jmethodID get_controller { env.GetMethodID(window_type, "getInsetsController", "()Landroid/view/WindowInsetsController;") };
            if (get_controller == nullptr || failed(env))
                return;
            const jobject controller { env.CallObjectMethod(window, get_controller) };
            if (controller == nullptr || failed(env))
                return;
            const jclass controller_type { env.FindClass("android/view/WindowInsetsController") };
            if (controller_type == nullptr || failed(env))
                return;
            const jmethodID set_appearance { env.GetMethodID(controller_type, "setSystemBarsAppearance", "(II)V") };
            if (set_appearance == nullptr || failed(env))
                return;
            const jint mask { light_status_bars_appearance | light_navigation_bars_appearance };
            env.CallVoidMethod(controller, set_appearance, light_background ? mask : 0, mask);
            static_cast<void>(failed(env));
            return;
        }

        const jmethodID get_decor { env.GetMethodID(window_type, "getDecorView", "()Landroid/view/View;") };
        if (get_decor == nullptr || failed(env))
            return;
        const jobject decor { env.CallObjectMethod(window, get_decor) };
        if (decor == nullptr || failed(env))
            return;
        const jclass view_type { env.FindClass("android/view/View") };
        if (view_type == nullptr || failed(env))
            return;
        const jmethodID get_flags { env.GetMethodID(view_type, "getSystemUiVisibility", "()I") };
        const jmethodID set_flags { env.GetMethodID(view_type, "setSystemUiVisibility", "(I)V") };
        if (get_flags == nullptr || set_flags == nullptr || failed(env))
            return;
        jint flags { env.CallIntMethod(decor, get_flags) };
        if (failed(env))
            return;
        const jint light { light_status_bar_flag | light_navigation_bar_flag };
        flags = light_background ? (flags | light) : (flags & ~light);
        env.CallVoidMethod(decor, set_flags, flags);
        static_cast<void>(failed(env));
    }
} // namespace luil::android

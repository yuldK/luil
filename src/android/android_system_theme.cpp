#include "android/android_system_theme.h"

#include "android/java_vm.h"

#include <android/api-level.h>

namespace luil::android {
    namespace {
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
            if (jni_failed(env) || get_window == nullptr)
                return nullptr;
            const jobject window { env.CallObjectMethod(activity, get_window) };
            return jni_failed(env) ? nullptr : window;
        }
    }

    std::optional<ui_color> read_dynamic_accent(JNIEnv& env, const jobject context)
    {
        if (android_get_device_api_level() < 31)
            return std::nullopt;
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return std::nullopt;

        // 자원 번호는 판마다 다를 수 있어 상수로 박지 않고 `android.R.color`에서 읽는다.
        const jclass colors { env.FindClass("android/R$color") };
        if (jni_failed(env) || colors == nullptr)
            return std::nullopt;
        const jfieldID field { env.GetStaticFieldID(colors, "system_accent1_500", "I") };
        if (jni_failed(env) || field == nullptr)
            return std::nullopt;
        const jint id { env.GetStaticIntField(colors, field) };
        if (jni_failed(env))
            return std::nullopt;

        const jclass context_type { env.FindClass("android/content/Context") };
        if (jni_failed(env) || context_type == nullptr)
            return std::nullopt;
        const jmethodID get_color { env.GetMethodID(context_type, "getColor", "(I)I") };
        if (jni_failed(env) || get_color == nullptr)
            return std::nullopt;
        const jint color { env.CallIntMethod(context, get_color, id) };
        if (jni_failed(env))
            return std::nullopt;
        // Android의 색은 ARGB 정수이고 `ui_color`도 같은 차례다.
        return static_cast<ui_color>(static_cast<std::uint32_t>(color));
    }

    bool read_high_contrast(JNIEnv& env, const jobject context)
    {
        if (android_get_device_api_level() < 34)
            return false;
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return false;

        const jclass context_type { env.FindClass("android/content/Context") };
        if (jni_failed(env) || context_type == nullptr)
            return false;
        const jmethodID get_service { env.GetMethodID(context_type, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") };
        if (jni_failed(env) || get_service == nullptr)
            return false;
        const jstring name { env.NewStringUTF("uimode") };
        if (jni_failed(env) || name == nullptr)
            return false;
        const jobject manager { env.CallObjectMethod(context, get_service, name) };
        if (jni_failed(env) || manager == nullptr)
            return false;
        const jclass manager_type { env.FindClass("android/app/UiModeManager") };
        if (jni_failed(env) || manager_type == nullptr)
            return false;
        const jmethodID get_contrast { env.GetMethodID(manager_type, "getContrast", "()F") };
        if (jni_failed(env) || get_contrast == nullptr)
            return false;
        const jfloat contrast { env.CallFloatMethod(manager, get_contrast) };
        if (jni_failed(env))
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
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return;
        const jobject window { activity_window(env, activity) };
        if (window == nullptr)
            return;
        const jclass window_type { env.FindClass("android/view/Window") };
        if (jni_failed(env) || window_type == nullptr)
            return;

        if (android_get_device_api_level() >= 30)
        {
            const jmethodID get_controller { env.GetMethodID(window_type, "getInsetsController", "()Landroid/view/WindowInsetsController;") };
            if (jni_failed(env) || get_controller == nullptr)
                return;
            const jobject controller { env.CallObjectMethod(window, get_controller) };
            if (jni_failed(env) || controller == nullptr)
                return;
            const jclass controller_type { env.FindClass("android/view/WindowInsetsController") };
            if (jni_failed(env) || controller_type == nullptr)
                return;
            const jmethodID set_appearance { env.GetMethodID(controller_type, "setSystemBarsAppearance", "(II)V") };
            if (jni_failed(env) || set_appearance == nullptr)
                return;
            const jint mask { light_status_bars_appearance | light_navigation_bars_appearance };
            env.CallVoidMethod(controller, set_appearance, light_background ? mask : 0, mask);
            static_cast<void>(jni_failed(env));
            return;
        }

        const jmethodID get_decor { env.GetMethodID(window_type, "getDecorView", "()Landroid/view/View;") };
        if (jni_failed(env) || get_decor == nullptr)
            return;
        const jobject decor { env.CallObjectMethod(window, get_decor) };
        if (jni_failed(env) || decor == nullptr)
            return;
        const jclass view_type { env.FindClass("android/view/View") };
        if (jni_failed(env) || view_type == nullptr)
            return;
        const jmethodID get_flags { env.GetMethodID(view_type, "getSystemUiVisibility", "()I") };
        if (jni_failed(env) || get_flags == nullptr)
            return;
        const jmethodID set_flags { env.GetMethodID(view_type, "setSystemUiVisibility", "(I)V") };
        if (jni_failed(env) || set_flags == nullptr)
            return;
        jint flags { env.CallIntMethod(decor, get_flags) };
        if (jni_failed(env))
            return;
        const jint light { light_status_bar_flag | light_navigation_bar_flag };
        flags = light_background ? (flags | light) : (flags & ~light);
        env.CallVoidMethod(decor, set_flags, flags);
        static_cast<void>(jni_failed(env));
    }
}

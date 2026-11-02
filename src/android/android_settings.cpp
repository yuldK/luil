#include "android/android_settings.h"

#include "android/java_vm.h"

#include <algorithm>
#include <cmath>

namespace luil::android {
    // 실패 검사는 `jni_failed`를 nullptr 검사보다 앞에 둔다. nullptr 검사가 먼저 걸리면 예외를 거두지 못하고
    // 돌아가서, 호출한 쪽의 다음 JNI 호출이 깨진다. 틀을 얻지 못한 경우도 같은 자리에서 거둔다.
    std::optional<system_input_settings> read_system_input_settings(JNIEnv& env, const jobject context, const float density)
    {
        const jni_local_frame frame { env };
        if (jni_failed(env) || frame.pushed() == false || context == nullptr || density <= 0.0f)
            return std::nullopt;
        const jclass type { env.FindClass("android/view/ViewConfiguration") };
        if (jni_failed(env) || type == nullptr)
            return std::nullopt;
        const jmethodID long_press { env.GetStaticMethodID(type, "getLongPressTimeout", "()I") };
        if (jni_failed(env) || long_press == nullptr)
            return std::nullopt;
        const jint long_press_ms { env.CallStaticIntMethod(type, long_press) };
        if (jni_failed(env))
            return std::nullopt;
        const jmethodID double_tap { env.GetStaticMethodID(type, "getDoubleTapTimeout", "()I") };
        if (jni_failed(env) || double_tap == nullptr)
            return std::nullopt;
        const jint double_tap_ms { env.CallStaticIntMethod(type, double_tap) };
        if (jni_failed(env))
            return std::nullopt;
        const jmethodID get { env.GetStaticMethodID(type, "get", "(Landroid/content/Context;)Landroid/view/ViewConfiguration;") };
        if (jni_failed(env) || get == nullptr)
            return std::nullopt;
        const jobject instance { env.CallStaticObjectMethod(type, get, context) };
        if (jni_failed(env) || instance == nullptr)
            return std::nullopt;
        const jmethodID slop { env.GetMethodID(type, "getScaledTouchSlop", "()I") };
        if (jni_failed(env) || slop == nullptr)
            return std::nullopt;
        const jint slop_pixels { env.CallIntMethod(instance, slop) };
        if (jni_failed(env) || long_press_ms <= 1 || double_tap_ms <= 0 || slop_pixels <= 0)
            return std::nullopt;
        return system_input_settings { std::chrono::milliseconds { long_press_ms }, std::chrono::milliseconds { double_tap_ms }, static_cast<float>(slop_pixels) / density };
    }

    std::optional<std::vector<float>> read_system_font_sizes(JNIEnv& env, const jobject context, const float density)
    {
        const jni_local_frame frame { env };
        if (jni_failed(env) || frame.pushed() == false || context == nullptr || density <= 0.0f)
            return std::nullopt;
        const jclass context_type { env.FindClass("android/content/Context") };
        if (jni_failed(env) || context_type == nullptr)
            return std::nullopt;
        const jmethodID get_resources { env.GetMethodID(context_type, "getResources", "()Landroid/content/res/Resources;") };
        if (jni_failed(env) || get_resources == nullptr)
            return std::nullopt;
        const jobject resources { env.CallObjectMethod(context, get_resources) };
        if (jni_failed(env) || resources == nullptr)
            return std::nullopt;
        const jclass resource_type { env.GetObjectClass(resources) };
        const jmethodID get_metrics { env.GetMethodID(resource_type, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;") };
        if (jni_failed(env) || get_metrics == nullptr)
            return std::nullopt;
        const jobject metrics { env.CallObjectMethod(resources, get_metrics) };
        if (jni_failed(env) || metrics == nullptr)
            return std::nullopt;
        const jclass value_type { env.FindClass("android/util/TypedValue") };
        if (jni_failed(env) || value_type == nullptr)
            return std::nullopt;
        const jmethodID convert { env.GetStaticMethodID(value_type, "applyDimension", "(IFLandroid/util/DisplayMetrics;)F") };
        if (jni_failed(env) || convert == nullptr)
            return std::nullopt;
        std::vector<float> sizes {};
        sizes.reserve(257);
        for (int size { 0 }; size <= 256; ++size)
        {
            // COMPLEX_UNIT_SP는 2다. dp 여백은 글자 크기 설정의 영향을 받지 않는다.
            const float value { env.CallStaticFloatMethod(value_type, convert, jint { 2 }, static_cast<jfloat>(size), metrics) / density };
            if (jni_failed(env) || std::isfinite(value) == false || (size == 0 ? value != 0.0f : value <= sizes.back()))
                return std::nullopt;
            sizes.push_back(value);
        }
        return sizes;
    }

    bool read_reduce_motion(JNIEnv& env, const jobject context)
    {
        const jni_local_frame frame { env };
        if (jni_failed(env) || frame.pushed() == false || context == nullptr)
            return false;
        const jclass context_type { env.FindClass("android/content/Context") };
        if (jni_failed(env) || context_type == nullptr)
            return false;
        const jmethodID get_resolver { env.GetMethodID(context_type, "getContentResolver", "()Landroid/content/ContentResolver;") };
        if (jni_failed(env) || get_resolver == nullptr)
            return false;
        const jobject resolver { env.CallObjectMethod(context, get_resolver) };
        if (jni_failed(env) || resolver == nullptr)
            return false;
        const jclass global_type { env.FindClass("android/provider/Settings$Global") };
        if (jni_failed(env) || global_type == nullptr)
            return false;
        // 값이 없으면 예외를 던지는 두 인자 짝이 아니라 기본값을 받는 짝을 쓴다. 기본값 1은 "움직임을 둔다"다.
        const jmethodID get_float { env.GetStaticMethodID(global_type, "getFloat", "(Landroid/content/ContentResolver;Ljava/lang/String;F)F") };
        if (jni_failed(env) || get_float == nullptr)
            return false;
        const jstring name { env.NewStringUTF("animator_duration_scale") };
        if (jni_failed(env) || name == nullptr)
            return false;
        const jfloat scale { env.CallStaticFloatMethod(global_type, get_float, resolver, name, jfloat { 1.0f }) };
        if (jni_failed(env))
            return false;
        return scale <= 0.0f;
    }

    touch_gesture_config apply_system_input_settings(touch_gesture_config config, const system_input_settings& settings) noexcept
    {
        config.long_press_time = settings.long_press_time;
        config.pan_start_time = std::min(config.pan_start_time, settings.long_press_time - std::chrono::milliseconds { 1 });
        config.press_move_tolerance = settings.touch_slop;
        config.pan_start_distance = settings.touch_slop;
        return config;
    }
}

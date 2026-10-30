#include "android/android_settings.h"

#include <algorithm>
#include <cmath>

namespace luil::android {
    namespace {
        class local_frame
        {
        public:
            explicit local_frame(JNIEnv& env)
                : env_ { env }
                , pushed_ { env.PushLocalFrame(16) == JNI_OK }
            {
            }
            local_frame(const local_frame&) = delete;
            local_frame(local_frame&&) = delete;
            local_frame& operator=(const local_frame&) = delete;
            local_frame& operator=(local_frame&&) = delete;
            ~local_frame()
            {
                if (env_.ExceptionCheck())
                    env_.ExceptionClear();
                if (pushed_)
                    env_.PopLocalFrame(nullptr);
            }
            [[nodiscard]] bool ready() const noexcept
            {
                return pushed_;
            }

        private:
            JNIEnv& env_;
            bool pushed_;
        };
    }

    std::optional<system_input_settings> read_system_input_settings(JNIEnv& env, const jobject context, const float density)
    {
        const local_frame frame { env };
        if (frame.ready() == false || context == nullptr || density <= 0.0f)
            return std::nullopt;
        const jclass type { env.FindClass("android/view/ViewConfiguration") };
        if (type == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jmethodID long_press { env.GetStaticMethodID(type, "getLongPressTimeout", "()I") };
        if (long_press == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jint long_press_ms { env.CallStaticIntMethod(type, long_press) };
        if (env.ExceptionCheck())
            return std::nullopt;
        const jmethodID double_tap { env.GetStaticMethodID(type, "getDoubleTapTimeout", "()I") };
        if (double_tap == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jint double_tap_ms { env.CallStaticIntMethod(type, double_tap) };
        if (env.ExceptionCheck())
            return std::nullopt;
        const jmethodID get { env.GetStaticMethodID(type, "get", "(Landroid/content/Context;)Landroid/view/ViewConfiguration;") };
        if (get == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jobject instance { env.CallStaticObjectMethod(type, get, context) };
        if (instance == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jmethodID slop { env.GetMethodID(type, "getScaledTouchSlop", "()I") };
        if (slop == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jint slop_pixels { env.CallIntMethod(instance, slop) };
        if (env.ExceptionCheck() || long_press_ms <= 1 || double_tap_ms <= 0 || slop_pixels <= 0)
            return std::nullopt;
        return system_input_settings { std::chrono::milliseconds { long_press_ms }, std::chrono::milliseconds { double_tap_ms }, static_cast<float>(slop_pixels) / density };
    }

    std::optional<std::vector<float>> read_system_font_sizes(JNIEnv& env, const jobject context, const float density)
    {
        const local_frame frame { env };
        if (frame.ready() == false || context == nullptr || density <= 0.0f)
            return std::nullopt;
        const jclass context_type { env.FindClass("android/content/Context") };
        if (context_type == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jmethodID get_resources { env.GetMethodID(context_type, "getResources", "()Landroid/content/res/Resources;") };
        if (get_resources == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jobject resources { env.CallObjectMethod(context, get_resources) };
        if (resources == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jclass resource_type { env.GetObjectClass(resources) };
        const jmethodID get_metrics { env.GetMethodID(resource_type, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;") };
        if (get_metrics == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jobject metrics { env.CallObjectMethod(resources, get_metrics) };
        if (metrics == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jclass value_type { env.FindClass("android/util/TypedValue") };
        if (value_type == nullptr || env.ExceptionCheck())
            return std::nullopt;
        const jmethodID convert { env.GetStaticMethodID(value_type, "applyDimension", "(IFLandroid/util/DisplayMetrics;)F") };
        if (convert == nullptr || env.ExceptionCheck())
            return std::nullopt;
        std::vector<float> sizes {};
        sizes.reserve(257);
        for (int size { 0 }; size <= 256; ++size)
        {
            // COMPLEX_UNIT_SP는 2다. dp 여백은 글자 크기 설정의 영향을 받지 않는다.
            const float value { env.CallStaticFloatMethod(value_type, convert, jint { 2 }, static_cast<jfloat>(size), metrics) / density };
            if (env.ExceptionCheck() || std::isfinite(value) == false || (size == 0 ? value != 0.0f : value <= sizes.back()))
                return std::nullopt;
            sizes.push_back(value);
        }
        return sizes;
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

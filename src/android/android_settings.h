#pragma once

#include "luil/ui/ui_interaction.h"

#include <jni.h>

#include <optional>
#include <vector>

namespace luil::android {
    struct system_input_settings
    {
        std::chrono::milliseconds long_press_time { 500 };
        std::chrono::milliseconds double_tap_time { 300 };
        float touch_slop { 8.0f };
    };

    // ViewConfiguration의 사용자 설정을 읽는다. 거리는 dp다.
    [[nodiscard]] std::optional<system_input_settings> read_system_input_settings(JNIEnv& env, jobject context, float density);
    // 정수 sp 0..256을 dp로 바꾼 표다. Android 14 이후의 비선형 변환도 시스템에 맡긴다.
    [[nodiscard]] std::optional<std::vector<float>> read_system_font_sizes(JNIEnv& env, jobject context, float density);
    [[nodiscard]] touch_gesture_config apply_system_input_settings(touch_gesture_config config, const system_input_settings& settings) noexcept;
}

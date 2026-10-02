#pragma once

#include "luil/theme/ui_theme.h"

#include <jni.h>

#include <optional>

namespace luil::android {
    // 시스템의 동적 색(Android 12의 Material You)에서 accent 키 컬러를 읽는다.
    //  - 배경화면에서 뽑은 accent 톤 하나(`system_accent1_500`)다. 밝기는 `make_system_accent`가 정하므로
    //    색상과 채도만 쓰인다.
    //  - Android 11 이하이거나 읽지 못하면 nullopt다.
    //  - Resources를 읽기만 하므로 JVM에 붙은 어느 thread에서든 부를 수 있다.
    [[nodiscard]] std::optional<ui_color> read_dynamic_accent(JNIEnv& env, jobject context);

    // 시스템 대비 설정이 "높음"인가다 (Android 14의 `UiModeManager.getContrast()`가 1).
    //  - Android 13 이하에는 공개된 신호가 없어 거짓이다.
    [[nodiscard]] bool read_high_contrast(JNIEnv& env, jobject context);

    // 고대비 팔레트에 쓸 색이다. Android는 시스템 고대비 색을 주지 않으므로 밝은 모드면 흰 바탕,
    // 어두운 모드면 검정 바탕의 기본 색을 쓴다.
    [[nodiscard]] high_contrast_colors default_high_contrast_colors(bool prefers_light) noexcept;

    // 상태 표시줄·내비게이션 막대의 아이콘을 바탕에 맞춘다. 밝은 바탕이면 어두운 아이콘이다.
    //  - **메인 thread에서만** 부른다 (`post_to_main_thread`). View 계층을 만진다.
    //  - Android 11부터는 `WindowInsetsController`, 그 아래는 `View.setSystemUiVisibility`다.
    void apply_system_bar_icons(JNIEnv& env, jobject activity, bool light_background);
} // namespace luil::android

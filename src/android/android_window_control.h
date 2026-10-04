#pragma once

#include "luil/app/app_host.h"

#include <jni.h>

#include <optional>

// 앱의 기기 요청(`device_request`)을 Activity와 창에 옮기는 JNI 층이다 (device-request-design.md).
// 전부 View 계층이나 Activity 상태를 만지므로 **메인 thread에서만** 부른다 (`post_to_main_thread`).
namespace luil::android {
    // 화면 방향 요청을 Activity에 옮긴다.
    //  - `original`은 Activity가 처음 가졌던 요청 값(매니페스트)이다. 비어 있으면 처음 바꾸기 전에 읽어 둔다.
    //    `system`은 그 값으로 되돌린다.
    //  - `current`는 지금 회전을 읽어 그 방향의 고정 값으로 바꾼다.
    void apply_screen_orientation(JNIEnv& env, jobject activity, screen_orientation request, std::optional<int>& original);

    // 시스템 막대를 숨기거나 보인다.
    //  - Android 11부터는 `WindowInsetsController`로 숨기고 쓸면 잠시 나타나게 한다
    //    (`BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE`). 그 아래는 `SYSTEM_UI_FLAG_IMMERSIVE_STICKY`다.
    //  - 시스템이 다른 창(알림 창, 다른 앱)에 다녀오며 막대를 다시 보일 수 있어, host는 초점을 되찾을 때마다
    //    다시 부른다. 같은 값을 거듭 불러도 해가 없다.
    void apply_system_bars(JNIEnv& env, jobject activity, system_bars_mode mode);
} // namespace luil::android

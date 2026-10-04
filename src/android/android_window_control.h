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
} // namespace luil::android

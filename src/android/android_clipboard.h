#pragma once

#include <jni.h>

#include <optional>
#include <string>
#include <string_view>

namespace luil::android {
    // 시스템 클립보드(`ClipboardManager`)를 JNI로 다룬다.
    //  - UI thread(`android_main`)에서 부른다. 그 thread는 앱 host가 시작할 때 JVM에 붙인다.
    //  - Android 10부터는 입력 초점을 가진 앱만 읽을 수 있다. 12부터는 다른 앱이 넣은 글을
    //    읽으면 시스템이 알림을 띄운다.
    //  - Java 예외는 거두고 실패로 돌려준다.
    [[nodiscard]] bool copy_text_to_clipboard(JNIEnv& env, jobject context, std::u8string_view text);
    // 글이 없거나 읽지 못하면 nullopt다.
    [[nodiscard]] std::optional<std::u8string> read_text_from_clipboard(JNIEnv& env, jobject context);
} // namespace luil::android

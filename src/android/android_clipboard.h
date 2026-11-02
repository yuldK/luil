#pragma once

#include "luil/ui/ui_events.h"

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
    //  - 첫 항목에 글(`getText`)이 없고 URI의 형식이 `text/`가 아니면(그림 URI 같은) 글이 없다고 본다. `coerceToText`가
    //    그 URI를 `content://…` 글로 바꿔 칸에 붙이는 것을 막는다 (image-clipboard-design.md 결정 8).
    [[nodiscard]] std::optional<std::u8string> read_text_from_clipboard(JNIEnv& env, jobject context);

    // 클립보드의 글과 그림을 함께 읽는다 (`clipboard_data_paste_request`, image-clipboard-design.md).
    //  - 항목을 차례로 보아 첫 `getText()`가 글이고, `getUri()`의 형식(`ContentResolver.getType`)이 `image/`로 시작하는 첫
    //    항목을 `openInputStream`으로 읽어 `encoded` 바이트로 싣는다.
    //  - `max_clipboard_image_bytes`(256 MiB)에서 멈추고 `image_too_large`다. 번호(`sequence`)는 0이다.
    //  - 다른 프로세스의 provider가 느리면 그만큼 이 thread가 멈춘다. 읽지 못하면 빈 답이다.
    [[nodiscard]] clipboard_data read_clipboard_data(JNIEnv& env, jobject context);
}

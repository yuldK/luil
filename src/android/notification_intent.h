#pragma once

#include <jni.h>

#include <string_view>

namespace luil::android {
    // 검증된 알림 id 전체를 Intent 비교에 넣는다. extra와 32비트 요청 번호만으로는 충돌을 가르지 못한다.
    //  - 실패하면 예외를 거두고 false다. intent의 data를 바꾸므로 알림 전용 런처 Intent에만 쓴다.
    [[nodiscard]] bool set_notification_intent_identity(JNIEnv& env, jobject intent, std::u8string_view id);
}

#pragma once

#include <jni.h>

namespace luil::android {
    // 알림 작은 아이콘을 어디서 얻었는가다. 못 얻은 두 경우는 진단 글이 달라 나눈다.
    enum class small_icon_source
    {
        app,
        framework,
        unreadable,
        missing,
    };

    struct small_icon_choice
    {
        small_icon_source source { small_icon_source::unreadable };
        // 자원 번호다. `source`가 `app`이나 `framework`일 때만 뜻이 있다.
        jint id { 0 };
    };

    // 알림 작은 아이콘을 고른다 (os-notification-design.md의 "Android").
    //  - 앱 아이콘(`ApplicationInfo.icon`)이 있으면 그것이다. framework는 읽지 않는다.
    //  - 매니페스트에 `android:icon`이 없어 0이면 `android.R.drawable.ic_dialog_info`를 정적 필드로 읽는다.
    //    `setSmallIcon(0)`은 아이콘을 null로 두고, targetSdk가 22를 넘는 앱의 `notify`는 그 알림을 거절한다.
    //  - 앱 정보를 읽지 못하면 `unreadable`, 앱 아이콘이 없고 framework 값도 못 얻으면 `missing`이다.
    [[nodiscard]] small_icon_choice read_notification_small_icon(JNIEnv& env, jobject context);
}

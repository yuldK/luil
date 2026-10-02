#pragma once

#include <jni.h>

#include <functional>

namespace luil::android {
    // Activity의 Java UI thread(메인 thread)에서 돌릴 일이다. 그 thread의 JNIEnv와 Activity를 받는다.
    using main_thread_work = std::function<void(JNIEnv& env, jobject activity)>;

    // 일을 메인 thread에 넘긴다. 어느 thread에서든 부를 수 있고 기다리지 않는다.
    //
    // 창의 장식(상태 표시줄 아이콘 색 등)은 View 계층을 만지므로 메인 thread에서만 바꿀 수 있는데,
    // luil의 UI thread는 glue가 띄운 다른 thread다. 앱 쪽 Java 코드 없이 그 thread에 닿으려고,
    // GameActivity가 메인 thread에서 부르는 생성 함수(`GameActivity_onCreate`)를 링크의 `--wrap`으로
    // luil이 먼저 받는다. 그 함수가 메인 looper에 깨우기 fd를 걸고 glue의 것으로 넘긴다.
    //  - GameActivity 4.4.2의 Java 쪽은 매니페스트의 `android.app.func_name`을 읽지 않는다 (헤더의
    //    주석은 NativeActivity에서 남은 것이다). 그래서 링크로 돌린다.
    //  - 다리가 서지 않았으면(luil 밖에서 GameActivity를 링크한 경우) 일을 버리고 거짓을 돌려준다.
    bool post_to_main_thread(main_thread_work work);
} // namespace luil::android

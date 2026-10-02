#pragma once

#include <jni.h>

namespace luil::android {
    // 프로세스의 JavaVM이다. JNI를 쓰는 층(HTTP client 등)이 Activity 없이 여기서 얻는다.
    //
    // 앱 host가 `android_main`에 들어서자마자 적는다 (앱의 `on_started`보다 먼저다). JVM
    // 안에서 도는 test 진입점도 같은 자리에 적는다. 한 번 적으면 프로세스가 끝날 때까지
    // 바뀌지 않는다 (Android 프로세스의 JavaVM은 하나다).
    void set_java_vm(JavaVM* vm) noexcept;
    [[nodiscard]] JavaVM* java_vm() noexcept;

    // 이 thread의 JNIEnv다. JVM에 붙지 않은 thread면 붙이고, 범위를 벗어날 때 뗀다.
    //
    // 이미 붙은 thread(Java가 띄운 thread, 남이 붙인 thread)는 그대로 두고 떼지 않는다 —
    // 남이 붙인 것을 떼면 그쪽이 쥔 지역 참조와 thread 이름이 사라진다.
    //  - `env()`가 nullptr이면 JavaVM이 없거나 붙지 못한 것이다.
    class jni_thread_scope
    {
    public:
        explicit jni_thread_scope(const char* thread_name = nullptr) noexcept;
        jni_thread_scope(const jni_thread_scope&) = delete;
        jni_thread_scope(jni_thread_scope&&) = delete;
        jni_thread_scope& operator=(const jni_thread_scope&) = delete;
        jni_thread_scope& operator=(jni_thread_scope&&) = delete;
        ~jni_thread_scope();

        [[nodiscard]] JNIEnv* env() const noexcept;

    private:
        JNIEnv* env_ { nullptr };
        bool attached_ { false };
    };
} // namespace luil::android

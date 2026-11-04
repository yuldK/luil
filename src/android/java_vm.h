#pragma once

#include <jni.h>

#include <string>

namespace luil::android {
    // 프로세스의 JavaVM이다. JNI를 쓰는 층(HTTP client 등)이 Activity 없이 여기서 얻는다.
    //
    // 앱 host가 `android_main`에 들어서자마자 적는다 (앱의 `on_started`보다 먼저다). JVM
    // 안에서 도는 test 진입점도 같은 자리에 적는다. 한 번 적으면 프로세스가 끝날 때까지
    // 바뀌지 않는다 (Android 프로세스의 JavaVM은 하나다).
    void set_java_vm(JavaVM* vm) noexcept;
    [[nodiscard]] JavaVM* java_vm() noexcept;

    // 앱 내부 저장소의 파일 폴더다 (`ANativeActivity::internalDataPath`). 비밀 저장소가 암호문을 둔다.
    //  - JavaVM과 같은 자리에서 한 번 적는다. 적기 전이면 빈 글이다.
    void set_files_directory(const char* path);
    [[nodiscard]] std::string files_directory();

    // 앱(Application)의 Context 전역 참조다. 다른 앱을 여는 Intent(`open_url`)가 어느 thread에서나 쓴다.
    //  - Activity가 아니라 Application을 쥐는 이유는 Activity는 회전·종료로 지고 다시 서지만
    //    Application은 프로세스와 함께 살아 전역 참조가 낡지 않아서다.
    //  - GameActivity가 메인 thread에서 부르는 생성 함수에서 처음 한 번 적는다 (android/main_thread.cpp).
    //    적기 전이면 nullptr이다.
    void set_application_context(JNIEnv& env, jobject activity) noexcept;
    [[nodiscard]] jobject application_context() noexcept;

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

    // 이 호출 동안의 지역 참조를 모아 끝에서 한꺼번에 지운다. 호출 하나가 만드는 참조 수는 몇 개뿐이다.
    //  - 붙인 thread는 오래 살 수 있어 지역 참조를 남기지 않는다. 틀을 얻지 못하면 `pushed()`가 거짓이다.
    //  - 틀을 얻지 못하면 `PushLocalFrame`이 `OutOfMemoryError`를 남기는데, 생성자가 그 예외를 거둔다. `pushed()`만 보고
    //    돌아가는 자리가 많아, 호출부마다 거두게 하면 하나만 빠져도 예외가 남은 채 다음 JNI 호출이 불린다.
    class jni_local_frame final
    {
    public:
        explicit jni_local_frame(JNIEnv& env, const jint capacity = 16) noexcept
            : env_ { &env }
            , pushed_ { env.PushLocalFrame(capacity) == JNI_OK }
        {
            if (pushed_ == false)
                env.ExceptionClear();
        }

        jni_local_frame(const jni_local_frame&) = delete;
        jni_local_frame(jni_local_frame&&) = delete;
        jni_local_frame& operator=(const jni_local_frame&) = delete;
        jni_local_frame& operator=(jni_local_frame&&) = delete;

        ~jni_local_frame()
        {
            if (pushed_)
                static_cast<void>(env_->PopLocalFrame(nullptr));
        }

        [[nodiscard]] bool pushed() const noexcept
        {
            return pushed_;
        }

    private:
        JNIEnv* env_ { nullptr };
        bool pushed_ { false };
    };

    // Java 예외가 났으면 거두고 참이다.
    //  - 예외가 남은 채로는 대부분의 JNI 함수를 부를 수 없어 호출한 자리에서 바로 거둔다.
    [[nodiscard]] inline bool jni_failed(JNIEnv& env) noexcept
    {
        if (env.ExceptionCheck() == JNI_FALSE)
            return false;
        env.ExceptionClear();
        return true;
    }
}

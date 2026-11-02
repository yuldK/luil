#include "android/android_settings.h"

#include <catch2/catch_test_macros.hpp>

#include <jni.h>

#include <cstdarg>
#include <string_view>

// 가짜 JNIEnv로 설정 읽기의 JNI 흐름을 잠근다. JVM이 없어도 기기 셸에서 돈다.
//  - 실패 지점 번호의 호출 하나가 예외를 남기고 null이나 0을 돌려준다. 지점을 바꿔 가며 모두 친다.
//  - 읽기가 돌아온 뒤에는 예외가 남지 않고, 연 틀은 닫히고, 예외가 남은 채 부른 JNI 호출이 없어야 한다.
namespace {
    struct fake_state
    {
        int calls { 0 };
        int fail_at { 0 };
        bool pending { false };
        int pushed { 0 };
        int popped { 0 };
        int calls_while_pending { 0 };
        float animator_scale { 1.0f };
    };

    fake_state* current { nullptr };
    int marker { 0 };

    [[nodiscard]] jobject object() noexcept
    {
        return reinterpret_cast<jobject>(&marker);
    }

    [[nodiscard]] std::string_view method_name(const jmethodID method) noexcept
    {
        return reinterpret_cast<const char*>(method);
    }

    // 실패 지점을 세는 호출 하나다. 예외가 남은 채 부른 호출은 따로 센다.
    [[nodiscard]] bool step() noexcept
    {
        if (current->pending)
            ++current->calls_while_pending;
        ++current->calls;
        if (current->calls != current->fail_at)
            return false;
        current->pending = true;
        return true;
    }

    jint JNICALL push_local_frame(JNIEnv*, const jint)
    {
        if (step())
            return JNI_ERR;
        ++current->pushed;
        return JNI_OK;
    }

    jobject JNICALL pop_local_frame(JNIEnv*, jobject)
    {
        if (current->pending)
            ++current->calls_while_pending;
        ++current->popped;
        return nullptr;
    }

    jboolean JNICALL exception_check(JNIEnv*)
    {
        return current->pending ? JNI_TRUE : JNI_FALSE;
    }

    void JNICALL exception_clear(JNIEnv*)
    {
        current->pending = false;
    }

    jclass JNICALL find_class(JNIEnv*, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jclass>(&marker);
    }

    jclass JNICALL get_object_class(JNIEnv*, jobject)
    {
        return reinterpret_cast<jclass>(&marker);
    }

    // 메서드 번호는 이름 글의 주소를 그대로 쓴다. 이름은 코드에 든 글자라 수명이 길다.
    jmethodID JNICALL get_method_id(JNIEnv*, jclass, const char* name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jmethodID>(const_cast<char*>(name));
    }

    jmethodID JNICALL get_static_method_id(JNIEnv*, jclass, const char* name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jmethodID>(const_cast<char*>(name));
    }

    jstring JNICALL new_string_utf(JNIEnv*, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jstring>(&marker);
    }

    jobject JNICALL call_object_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return nullptr;
        return object();
    }

    jobject JNICALL call_static_object_v(JNIEnv*, jclass, jmethodID, va_list)
    {
        if (step())
            return nullptr;
        return object();
    }

    jint JNICALL call_int_v(JNIEnv*, jobject, const jmethodID method, va_list)
    {
        if (step())
            return 0;
        return method_name(method) == "getScaledTouchSlop" ? 8 : 0;
    }

    jint JNICALL call_static_int_v(JNIEnv*, jclass, const jmethodID method, va_list)
    {
        if (step())
            return 0;
        if (method_name(method) == "getLongPressTimeout")
            return 500;
        if (method_name(method) == "getDoubleTapTimeout")
            return 300;
        return 0;
    }

    jfloat JNICALL call_static_float_v(JNIEnv*, jclass, const jmethodID method, va_list args)
    {
        if (step())
            return 0.0f;
        if (method_name(method) == "applyDimension")
        {
            // 글자 크기 표는 (단위, 값, 지표) 순으로 넘어온다. 값은 float가 double로 넓어져 온다.
            static_cast<void>(va_arg(args, jint));
            return static_cast<jfloat>(va_arg(args, double));
        }
        return current->animator_scale;
    }

    [[nodiscard]] JNINativeInterface make_table()
    {
        JNINativeInterface table {};
        table.PushLocalFrame = push_local_frame;
        table.PopLocalFrame = pop_local_frame;
        table.ExceptionCheck = exception_check;
        table.ExceptionClear = exception_clear;
        table.FindClass = find_class;
        table.GetObjectClass = get_object_class;
        table.GetMethodID = get_method_id;
        table.GetStaticMethodID = get_static_method_id;
        table.NewStringUTF = new_string_utf;
        table.CallObjectMethodV = call_object_v;
        table.CallIntMethodV = call_int_v;
        table.CallStaticObjectMethodV = call_static_object_v;
        table.CallStaticIntMethodV = call_static_int_v;
        table.CallStaticFloatMethodV = call_static_float_v;
        return table;
    }

    // 읽기 하나를 치를 가짜 환경이다. 살아 있는 동안만 `current`가 이 상태를 가리킨다.
    class fake_env
    {
    public:
        explicit fake_env(const int fail_at = 0, const float animator_scale = 1.0f)
            : table_ { make_table() }
            , env_ { &table_ }
        {
            state_.fail_at = fail_at;
            state_.animator_scale = animator_scale;
            current = &state_;
        }
        fake_env(const fake_env&) = delete;
        fake_env(fake_env&&) = delete;
        fake_env& operator=(const fake_env&) = delete;
        fake_env& operator=(fake_env&&) = delete;
        ~fake_env()
        {
            current = nullptr;
        }

        [[nodiscard]] JNIEnv& env() noexcept
        {
            return env_;
        }

        [[nodiscard]] const fake_state& state() const noexcept
        {
            return state_;
        }

    private:
        JNINativeInterface table_;
        JNIEnv env_;
        fake_state state_ {};
    };
}

TEST_CASE("Reduce motion is on only when the animator scale is zero", "[android][settings]")
{
    // 0.5배 같은 배율은 움직임 줄이기가 아니다 (android_settings.h).
    {
        fake_env zero { 0, 0.0f };
        CHECK(luil::android::read_reduce_motion(zero.env(), object()));
    }
    {
        fake_env half { 0, 0.5f };
        CHECK_FALSE(luil::android::read_reduce_motion(half.env(), object()));
    }
    {
        fake_env one { 0, 1.0f };
        CHECK_FALSE(luil::android::read_reduce_motion(one.env(), object()));
    }
}

TEST_CASE("Reduce motion keeps its answer when any JNI call fails", "[android][settings]")
{
    int calls { 0 };
    {
        fake_env success { 0, 0.0f };
        CHECK(luil::android::read_reduce_motion(success.env(), object()));
        CHECK(success.state().popped == 1);
        calls = success.state().calls;
    }
    REQUIRE(calls > 1);
    for (int point { 1 }; point <= calls; ++point)
    {
        // 배율을 0으로 깔아 두면 실패를 놓쳤을 때 답이 거짓이 아니라 참으로 드러난다.
        fake_env failing { point, 0.0f };
        CHECK_FALSE(luil::android::read_reduce_motion(failing.env(), object()));
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
}

TEST_CASE("Reduce motion makes no JNI call when its local frame cannot open", "[android][settings]")
{
    fake_env failing { 1, 0.0f };
    CHECK_FALSE(luil::android::read_reduce_motion(failing.env(), object()));
    CHECK(failing.state().calls == 1);
    CHECK(failing.state().popped == 0);
    CHECK_FALSE(failing.state().pending);
}

TEST_CASE("Input settings keep their answer when any JNI call fails", "[android][settings]")
{
    int calls { 0 };
    {
        fake_env success {};
        const auto settings { luil::android::read_system_input_settings(success.env(), object(), 2.0f) };
        REQUIRE(settings.has_value());
        CHECK(settings->long_press_time.count() == 500);
        CHECK(settings->double_tap_time.count() == 300);
        CHECK(settings->touch_slop == 4.0f);
        calls = success.state().calls;
    }
    REQUIRE(calls > 1);
    for (int point { 1 }; point <= calls; ++point)
    {
        fake_env failing { point };
        CHECK_FALSE(luil::android::read_system_input_settings(failing.env(), object(), 2.0f).has_value());
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
}

TEST_CASE("Font sizes keep their answer when any JNI call fails", "[android][settings]")
{
    int calls { 0 };
    {
        fake_env success {};
        const auto sizes { luil::android::read_system_font_sizes(success.env(), object(), 2.0f) };
        REQUIRE(sizes.has_value());
        CHECK(sizes->size() == 257u);
        CHECK(sizes->back() == 128.0f);
        calls = success.state().calls;
    }
    REQUIRE(calls > 1);
    for (int point { 1 }; point <= calls; ++point)
    {
        fake_env failing { point };
        CHECK_FALSE(luil::android::read_system_font_sizes(failing.env(), object(), 2.0f).has_value());
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
}

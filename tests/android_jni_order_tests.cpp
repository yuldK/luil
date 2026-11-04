#include "android/android_system_theme.h"
#include "android/android_window_control.h"
#include "android/java_vm.h"

#include <catch2/catch_test_macros.hpp>

#include <jni.h>

#include <cstdarg>
#include <optional>
#include <string_view>

// 가짜 JNIEnv로 시스템 색·대비·막대·햅틱·권한 읽기와 쓰기의 JNI 흐름을 잠근다 (android_settings_tests.cpp와 같은 틀).
//  - 실패 지점 번호의 호출 하나가 예외를 남기고 null이나 0을 돌려준다. 지점을 바꿔 가며 모두 친다.
//  - 틀을 여는 첫 호출(지점 1)도 친다. `frame.pushed() == false`만 보고 돌아가는 자리가 많아, 틀을 얻지 못했을 때 남은 예외는
//    `jni_local_frame`의 생성자가 거둔다. 호출부마다 거두지 않아도 예외가 남지 않는지를 모든 읽기와 쓰기에서 본다.
//  - 값을 읽는 `GetIntField`·`GetStaticIntField`도 실패 지점이다. 읽은 값을 쓰기 전에 예외를 거두는지, 모든 JNI 호출이
//    앞 호출의 예외를 거둔 뒤에 불리는지(예외가 남은 채 부른 호출 수 0)를 아홉 읽기와 쓰기 모두에서 본다.
//  - 읽기의 틀은 API 수준에 따라 갈린다. 기기에서 JNI 호출이 하나도 없으면 그 읽기는 SKIP한다.
//  - 앱 context 잡기처럼 thread를 JVM에 붙이는 길은 가짜 JavaVM으로 같은 가짜 JNIEnv를 내준다.
namespace {
    struct fake_state
    {
        int calls { 0 };
        int fail_at { 0 };
        // 둘째 실패 지점이다. 예외를 거둔 뒤의 호출도 실패하는 자리를 본다.
        int also_fail_at { 0 };
        bool pending { false };
        int pushed { 0 };
        int popped { 0 };
        int calls_while_pending { 0 };
        int void_calls { 0 };
        // `getColor`가 돌려주는 색과 `getContrast`가 돌려주는 값이다. 고대비는 1이 높음이다.
        jint color { static_cast<jint>(0xFF3A7BD5U) };
        jfloat contrast { 1.0f };
        // `targetSdkVersion`과 `Configuration.orientation`(가로는 2)이다.
        jint target_sdk { 34 };
        jint orientation { 2 };
        // `PackageManager.PERMISSION_GRANTED`는 0이다.
        jint permission { 0 };
        jboolean performed { JNI_TRUE };
    };

    fake_state* current { nullptr };
    JNIEnv* current_env { nullptr };
    int marker { 0 };

    [[nodiscard]] jobject object() noexcept
    {
        return reinterpret_cast<jobject>(&marker);
    }

    // 메서드와 필드 번호는 이름 글의 주소를 그대로 쓴다. 이름은 코드에 든 글자라 수명이 길다.
    [[nodiscard]] std::string_view method_name(const jmethodID method) noexcept
    {
        return reinterpret_cast<const char*>(method);
    }

    [[nodiscard]] std::string_view field_name(const jfieldID field) noexcept
    {
        return reinterpret_cast<const char*>(field);
    }

    // 예외가 남은 채 부른 JNI 호출을 센다.
    void note() noexcept
    {
        if (current->pending)
            ++current->calls_while_pending;
    }

    // 실패 지점을 세는 호출 하나다. 실패를 주입하지 않는 호출은 `note`만 부른다.
    [[nodiscard]] bool step() noexcept
    {
        note();
        ++current->calls;
        if (current->calls != current->fail_at
            && current->calls != current->also_fail_at)
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
        note();
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

    jobject JNICALL new_global_ref(JNIEnv*, const jobject reference)
    {
        note();
        return reference;
    }

    // 지역 참조 지우기는 예외가 남아도 불러도 되는 JNI다. 세지 않는다.
    void JNICALL delete_local_ref(JNIEnv*, jobject)
    {
    }

    jclass JNICALL find_class(JNIEnv*, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jclass>(&marker);
    }

    jclass JNICALL get_object_class(JNIEnv*, jobject)
    {
        note();
        return reinterpret_cast<jclass>(&marker);
    }

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

    jfieldID JNICALL get_field_id(JNIEnv*, jclass, const char* name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jfieldID>(const_cast<char*>(name));
    }

    jfieldID JNICALL get_static_field_id(JNIEnv*, jclass, const char* name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jfieldID>(const_cast<char*>(name));
    }

    jint JNICALL get_int_field(JNIEnv*, jobject, const jfieldID field)
    {
        if (step())
            return 0;
        if (field_name(field) == "targetSdkVersion")
            return current->target_sdk;
        return current->orientation;
    }

    jint JNICALL get_static_int_field(JNIEnv*, jclass, const jfieldID)
    {
        if (step())
            return 0;
        return 0x0106001d;
    }

    jstring JNICALL new_string_utf(JNIEnv*, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jstring>(&marker);
    }

    jobjectArray JNICALL new_object_array(JNIEnv*, const jsize, jclass, jobject)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jobjectArray>(&marker);
    }

    jobject JNICALL call_object_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return nullptr;
        return object();
    }

    jint JNICALL call_int_v(JNIEnv*, jobject, const jmethodID method, va_list)
    {
        if (step())
            return 0;
        const std::string_view name { method_name(method) };
        if (name == "getColor")
            return current->color;
        if (name == "checkSelfPermission")
            return current->permission;
        // `getRotation`과 `getSystemUiVisibility`는 0이다.
        return 0;
    }

    jint JNICALL call_static_int_v(JNIEnv*, jclass, jmethodID, va_list)
    {
        if (step())
            return 0;
        // `WindowInsets.Type.systemBars()`의 값은 어느 것이든 된다. 읽은 값이 쓰기로 넘어가는지만 본다.
        return 1;
    }

    jfloat JNICALL call_float_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return 0.0f;
        return current->contrast;
    }

    jboolean JNICALL call_boolean_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return JNI_FALSE;
        return current->performed;
    }

    void JNICALL call_void_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return;
        ++current->void_calls;
    }

    [[nodiscard]] JNINativeInterface make_table()
    {
        JNINativeInterface table {};
        table.PushLocalFrame = push_local_frame;
        table.PopLocalFrame = pop_local_frame;
        table.ExceptionCheck = exception_check;
        table.ExceptionClear = exception_clear;
        table.NewGlobalRef = new_global_ref;
        table.DeleteLocalRef = delete_local_ref;
        table.FindClass = find_class;
        table.GetObjectClass = get_object_class;
        table.GetMethodID = get_method_id;
        table.GetStaticMethodID = get_static_method_id;
        table.GetFieldID = get_field_id;
        table.GetStaticFieldID = get_static_field_id;
        table.GetIntField = get_int_field;
        table.GetStaticIntField = get_static_int_field;
        table.NewStringUTF = new_string_utf;
        table.NewObjectArray = new_object_array;
        table.CallObjectMethodV = call_object_v;
        table.CallIntMethodV = call_int_v;
        table.CallStaticIntMethodV = call_static_int_v;
        table.CallFloatMethodV = call_float_v;
        table.CallBooleanMethodV = call_boolean_v;
        table.CallVoidMethodV = call_void_v;
        return table;
    }

    // 호출 하나를 치를 가짜 환경이다. 살아 있는 동안만 `current`가 이 상태를 가리킨다.
    class fake_env
    {
    public:
        explicit fake_env(const int fail_at = 0)
            : table_ { make_table() }
            , env_ { &table_ }
        {
            state_.fail_at = fail_at;
            current = &state_;
            current_env = &env_;
        }
        fake_env(const fake_env&) = delete;
        fake_env(fake_env&&) = delete;
        fake_env& operator=(const fake_env&) = delete;
        fake_env& operator=(fake_env&&) = delete;
        ~fake_env()
        {
            current = nullptr;
            current_env = nullptr;
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

    // 한 번의 호출이 남긴 상태와 답이다.
    struct outcome
    {
        fake_state state;
        int answer;
    };

    template <typename Run>
    [[nodiscard]] outcome run_once(Run&& run, const int fail_at = 0)
    {
        fake_env scope { fail_at };
        const int answer { run(scope.env()) };
        return outcome { scope.state(), answer };
    }

    // 성공한 한 번을 기준으로 실패 지점을 모두 친다. 답은 `failure_answer`이고, 예외는 남지 않고, 틀은 닫혀야 한다.
    //  - 지점 1은 틀을 여는 호출이다. 틀을 얻지 못한 호출은 다른 JNI 호출을 하지 않고 돌아가야 한다.
    //  - 어느 지점에서도 예외가 남은 채 다음 JNI 호출을 부르면 안 된다.
    template <typename Run>
    void check_failure_points(Run&& run, const outcome& success, const int failure_answer)
    {
        for (int point { 1 }; point <= success.state.calls; ++point)
        {
            INFO("failure point " << point);
            const outcome failing { run_once(run, point) };
            CHECK(failing.answer == failure_answer);
            CHECK_FALSE(failing.state.pending);
            if (point == 1)
                CHECK(failing.state.calls == 1);
            CHECK(failing.state.calls_while_pending == 0);
            CHECK(failing.state.popped == failing.state.pushed);
        }
    }
}

// 프로세스에 하나인 context는 한 번 적으면 지울 수 없어, 이 파일에서 context를 쓰는 어느 test보다 앞에 둔다.
TEST_CASE("App context capture leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    if (luil::android::application_context() != nullptr)
    {
        SKIP("The app context was captured earlier in this process.");
    }
    const auto run = [](JNIEnv& env)
    {
        luil::android::set_application_context(env, object());
        return luil::android::application_context() != nullptr ? 1 : 0;
    };
    // `FindClass`, `GetMethodID`, `getApplicationContext` 셋이 예외를 낼 수 있다. 성공 뒤에는 다시 불러도 호출이 없어, 지점을 먼저 친다.
    constexpr int failure_points { 3 };
    for (int point { 1 }; point <= failure_points; ++point)
    {
        INFO("failure point " << point);
        const outcome failing { run_once(run, point) };
        CHECK(failing.answer == 0);
        CHECK_FALSE(failing.state.pending);
        CHECK(failing.state.calls == point);
        CHECK(failing.state.calls_while_pending == 0);
    }
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.calls == failure_points);
    CHECK(success.state.calls_while_pending == 0);
}

TEST_CASE("System bar icons leave no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        luil::android::apply_system_bar_icons(env, object(), true);
        return 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.state.void_calls == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Dynamic accent keeps its answer when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::read_dynamic_accent(env, object()).has_value() ? 1 : 0;
    };
    const outcome success { run_once(run) };
    if (success.state.calls == 0)
    {
        SKIP("Dynamic accent needs API 31 or later.");
    }
    REQUIRE(success.state.calls > 1);
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("High contrast keeps its answer when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::read_high_contrast(env, object()) ? 1 : 0;
    };
    const outcome success { run_once(run) };
    if (success.state.calls == 0)
    {
        SKIP("High contrast needs API 34 or later.");
    }
    REQUIRE(success.state.calls > 1);
    CHECK(success.answer == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Target SDK keeps its answer when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::read_target_sdk(env, object());
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.answer == 34);
    check_failure_points(run, success, 0);
}

TEST_CASE("Permission check keeps its answer when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::has_permission(env, object(), "android.permission.POST_NOTIFICATIONS") ? 1 : 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.answer == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Haptic feedback leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        luil::android::perform_haptic_feedback(env, object(), 0);
        return 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("System bars leave no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        luil::android::apply_system_bars(env, object(), luil::system_bars_mode::immersive);
        return 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.state.void_calls >= 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Permission request leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        luil::android::request_permission(env, object(), "android.permission.POST_NOTIFICATIONS");
        return 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.state.void_calls == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Screen orientation leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        std::optional<int> original {};
        luil::android::apply_screen_orientation(env, object(), luil::screen_orientation::current, original);
        return 0;
    };
    const outcome success { run_once(run) };
    REQUIRE(success.state.calls > 1);
    CHECK(success.state.void_calls == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("A local frame that cannot open clears the exception it left", "[android][jni_order]")
{
    fake_env failing { 1 };
    {
        const luil::android::jni_local_frame frame { failing.env() };
        CHECK_FALSE(frame.pushed());
        // 틀을 얻지 못하면 `OutOfMemoryError`가 남는다. 호출부가 `pushed()`만 보고 돌아가도 예외가 남지 않아야 한다.
        CHECK_FALSE(failing.state().pending);
    }
    CHECK(failing.state().calls == 1);
    CHECK(failing.state().pushed == 0);
    CHECK(failing.state().popped == 0);
    CHECK(failing.state().calls_while_pending == 0);
}

TEST_CASE("A local frame that opens is popped once and leaves no exception", "[android][jni_order]")
{
    fake_env opening {};
    {
        const luil::android::jni_local_frame frame { opening.env() };
        CHECK(frame.pushed());
        CHECK_FALSE(opening.state().pending);
        CHECK(opening.state().popped == 0);
    }
    CHECK(opening.state().pushed == 1);
    CHECK(opening.state().popped == 1);
}

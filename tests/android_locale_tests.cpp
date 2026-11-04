#include "android/android_locale.h"

#include <catch2/catch_test_macros.hpp>

#include <jni.h>

#include <chrono>
#include <cstdarg>
#include <optional>

// 가짜 JNIEnv로 첫 요일 읽기의 JNI 흐름을 잠근다. JVM이 없어도 기기 셸에서 돈다 (android_settings_tests.cpp와 같은 길).
//  - 실패 지점 번호의 호출 하나가 예외를 남기고 null이나 0을 돌려준다. 지점을 바꿔 가며 모두 친다.
//  - 읽기가 돌아온 뒤에는 예외가 남지 않고, 연 틀은 닫히고, 예외가 남은 채 부른 JNI 호출이 없어야 한다.
namespace {
    struct fake_state
    {
        int calls { 0 };
        int fail_at { 0 };
        // 이 번호의 호출이 예외 없이 null을 돌려준다.
        int null_at { 0 };
        bool pending { false };
        int pushed { 0 };
        int popped { 0 };
        int calls_while_pending { 0 };
        jint first_day { 2 };
    };

    fake_state* current { nullptr };
    int marker { 0 };

    enum class step_result
    {
        ok,
        failed,
        null,
    };

    // 실패 지점을 세는 호출 하나다. 예외가 남은 채 부른 호출은 따로 센다.
    [[nodiscard]] step_result step() noexcept
    {
        if (current->pending)
            ++current->calls_while_pending;
        ++current->calls;
        if (current->calls == current->null_at)
            return step_result::null;
        if (current->calls != current->fail_at)
            return step_result::ok;
        current->pending = true;
        return step_result::failed;
    }

    jint JNICALL push_local_frame(JNIEnv*, const jint)
    {
        if (step() == step_result::failed)
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
        return step() == step_result::ok ? reinterpret_cast<jclass>(&marker) : nullptr;
    }

    jmethodID JNICALL get_method_id(JNIEnv*, jclass, const char* name, const char*)
    {
        return step() == step_result::ok ? reinterpret_cast<jmethodID>(const_cast<char*>(name)) : nullptr;
    }

    jmethodID JNICALL get_static_method_id(JNIEnv*, jclass, const char* name, const char*)
    {
        return step() == step_result::ok ? reinterpret_cast<jmethodID>(const_cast<char*>(name)) : nullptr;
    }

    jobject JNICALL call_static_object_v(JNIEnv*, jclass, jmethodID, va_list)
    {
        return step() == step_result::ok ? reinterpret_cast<jobject>(&marker) : nullptr;
    }

    jint JNICALL call_int_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        return step() == step_result::ok ? current->first_day : 0;
    }

    [[nodiscard]] JNINativeInterface make_table()
    {
        JNINativeInterface table {};
        table.PushLocalFrame = push_local_frame;
        table.PopLocalFrame = pop_local_frame;
        table.ExceptionCheck = exception_check;
        table.ExceptionClear = exception_clear;
        table.FindClass = find_class;
        table.GetMethodID = get_method_id;
        table.GetStaticMethodID = get_static_method_id;
        table.CallStaticObjectMethodV = call_static_object_v;
        table.CallIntMethodV = call_int_v;
        return table;
    }

    // 읽기 하나를 치를 가짜 환경이다. 살아 있는 동안만 `current`가 이 상태를 가리킨다.
    class fake_env
    {
    public:
        explicit fake_env(const fake_state state = {})
            : table_ { make_table() }
            , env_ { &table_ }
            , state_ { state }
        {
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
        fake_state state_;
    };

    constexpr int calls_of_one_read { 6 };
}

TEST_CASE("Calendar's first day maps 1 to Sunday and 7 to Saturday and nothing else", "[android][locale]")
{
    CHECK(luil::android::weekday_from_calendar_first_day(1) == std::chrono::Sunday);
    CHECK(luil::android::weekday_from_calendar_first_day(2) == std::chrono::Monday);
    CHECK(luil::android::weekday_from_calendar_first_day(7) == std::chrono::Saturday);
    CHECK_FALSE(luil::android::weekday_from_calendar_first_day(0).has_value());
    CHECK_FALSE(luil::android::weekday_from_calendar_first_day(8).has_value());
    CHECK_FALSE(luil::android::weekday_from_calendar_first_day(-1).has_value());
}

TEST_CASE("The first weekday is what the Calendar answers", "[android][locale]")
{
    {
        fake_env monday {};
        const auto answer { luil::android::read_first_weekday(monday.env()) };
        REQUIRE(answer.has_value());
        CHECK(*answer == std::chrono::Monday);
        CHECK(monday.state().calls == calls_of_one_read);
        CHECK(monday.state().popped == 1);
    }
    {
        fake_env sunday { { .first_day = 1 } };
        CHECK(luil::android::read_first_weekday(sunday.env()) == std::chrono::Sunday);
    }
    {
        // 범위 밖의 답은 값으로 삼지 않는다.
        fake_env strange { { .first_day = 9 } };
        CHECK_FALSE(luil::android::read_first_weekday(strange.env()).has_value());
        CHECK(strange.state().popped == 1);
    }
}

TEST_CASE("The first weekday is empty when any JNI call fails and leaves no exception", "[android][locale]")
{
    for (int point { 1 }; point <= calls_of_one_read; ++point)
    {
        CAPTURE(point);
        fake_env failing { { .fail_at = point } };
        CHECK_FALSE(luil::android::read_first_weekday(failing.env()).has_value());
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
}

TEST_CASE("The first weekday is empty when a JNI call answers null without an exception", "[android][locale]")
{
    // 2번부터 5번까지가 null을 돌려줄 수 있는 호출이다 (클래스, 두 메서드 번호, 객체).
    for (int point { 2 }; point < calls_of_one_read; ++point)
    {
        CAPTURE(point);
        fake_env empty { { .null_at = point } };
        CHECK_FALSE(luil::android::read_first_weekday(empty.env()).has_value());
        CHECK(empty.state().calls == point);
        CHECK(empty.state().popped == 1);
    }
}

TEST_CASE("The first weekday makes no JNI call when its local frame cannot open", "[android][locale]")
{
    fake_env failing { { .fail_at = 1 } };
    CHECK_FALSE(luil::android::read_first_weekday(failing.env()).has_value());
    CHECK(failing.state().calls == 1);
    CHECK(failing.state().popped == 0);
    CHECK_FALSE(failing.state().pending);
}

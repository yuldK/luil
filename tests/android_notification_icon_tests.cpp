#include "android/notification_icon.h"

#include <catch2/catch_test_macros.hpp>

#include <jni.h>

#include <cstdarg>
#include <string>

// 가짜 JNIEnv로 알림 작은 아이콘 고르기를 잠근다. JVM이 없어도 기기 셸에서 돈다 (android_settings_tests.cpp와 같은 틀).
//  - 앱 아이콘이 0인 앱(매니페스트에 `android:icon`이 없음)이 framework 자원으로 갈음되는지가 이 test의 몫이다.
//  - 실패 지점 번호의 호출 하나가 예외를 남기고 null이나 0을 돌려준다. 지점을 바꿔 가며 모두 친다.
namespace {
    // 실제 번호와 겹치지 않는 표식 값이다. 고르기가 어느 쪽 값을 돌려줬는지만 가린다.
    constexpr jint app_icon_value { 0x7f010001 };
    constexpr jint framework_icon_value { 0x01080027 };

    struct fake_state
    {
        int calls { 0 };
        int fail_at { 0 };
        bool pending { false };
        int pushed { 0 };
        int popped { 0 };
        int calls_while_pending { 0 };
        jint app_icon { app_icon_value };
        jint framework_icon { framework_icon_value };
        int framework_lookups { 0 };
        std::string static_field_name {};
    };

    fake_state* current { nullptr };
    int marker { 0 };

    [[nodiscard]] jobject object() noexcept
    {
        return reinterpret_cast<jobject>(&marker);
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

    jclass JNICALL find_class(JNIEnv*, const char* const name)
    {
        if (std::string { name } == "android/R$drawable")
            ++current->framework_lookups;
        if (step())
            return nullptr;
        return reinterpret_cast<jclass>(&marker);
    }

    jclass JNICALL get_object_class(JNIEnv*, jobject)
    {
        if (current->pending)
            ++current->calls_while_pending;
        return reinterpret_cast<jclass>(&marker);
    }

    // 번호는 이름 글의 주소를 그대로 쓴다. 이름은 코드에 든 글자라 수명이 길다.
    jmethodID JNICALL get_method_id(JNIEnv*, jclass, const char* const name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jmethodID>(const_cast<char*>(name));
    }

    jobject JNICALL call_object_v(JNIEnv*, jobject, jmethodID, va_list)
    {
        if (step())
            return nullptr;
        return object();
    }

    jfieldID JNICALL get_field_id(JNIEnv*, jclass, const char* const name, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jfieldID>(const_cast<char*>(name));
    }

    jint JNICALL get_int_field(JNIEnv*, jobject, jfieldID)
    {
        if (step())
            return 0;
        return current->app_icon;
    }

    jfieldID JNICALL get_static_field_id(JNIEnv*, jclass, const char* const name, const char*)
    {
        current->static_field_name = name;
        if (step())
            return nullptr;
        return reinterpret_cast<jfieldID>(const_cast<char*>(name));
    }

    jint JNICALL get_static_int_field(JNIEnv*, jclass, jfieldID)
    {
        if (step())
            return 0;
        return current->framework_icon;
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
        table.CallObjectMethodV = call_object_v;
        table.GetFieldID = get_field_id;
        table.GetIntField = get_int_field;
        table.GetStaticFieldID = get_static_field_id;
        table.GetStaticIntField = get_static_int_field;
        return table;
    }

    // 고르기 하나를 치를 가짜 환경이다. 살아 있는 동안만 `current`가 이 상태를 가리킨다.
    class fake_env
    {
    public:
        explicit fake_env(const jint app_icon, const int fail_at = 0, const jint framework_icon = framework_icon_value)
            : table_ { make_table() }
            , env_ { &table_ }
        {
            state_.app_icon = app_icon;
            state_.fail_at = fail_at;
            state_.framework_icon = framework_icon;
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

    using luil::android::read_notification_small_icon;
    using luil::android::small_icon_source;
}

TEST_CASE("Notification small icon is the app icon when the app has one", "[android][notification]")
{
    fake_env app { app_icon_value };
    const auto icon { read_notification_small_icon(app.env(), object()) };
    CHECK(icon.source == small_icon_source::app);
    CHECK(icon.id == app_icon_value);
    // 앱 아이콘이 있으면 동작이 고치기 전과 같아야 해서 framework를 들여다보지도 않는다.
    CHECK(app.state().framework_lookups == 0);
    CHECK(app.state().pushed == 1);
    CHECK(app.state().popped == 1);
}

TEST_CASE("Notification small icon falls back to ic_dialog_info when the app has no icon", "[android][notification]")
{
    fake_env bare { 0 };
    const auto icon { read_notification_small_icon(bare.env(), object()) };
    CHECK(icon.source == small_icon_source::framework);
    CHECK(icon.id == framework_icon_value);
    CHECK(bare.state().framework_lookups == 1);
    CHECK(bare.state().static_field_name == "ic_dialog_info");
    CHECK(bare.state().popped == bare.state().pushed);
}

TEST_CASE("Notification small icon is missing when the framework icon reads zero", "[android][notification]")
{
    // 0을 그대로 넘기면 `setSmallIcon(0)`이 아이콘을 null로 두어 `notify`가 거절한다.
    fake_env bare { 0, 0, 0 };
    const auto icon { read_notification_small_icon(bare.env(), object()) };
    CHECK(icon.source == small_icon_source::missing);
    CHECK_FALSE(bare.state().pending);
}

TEST_CASE("Notification small icon gives no icon when any JNI call fails", "[android][notification]")
{
    int app_calls { 0 };
    {
        fake_env app { app_icon_value };
        CHECK(read_notification_small_icon(app.env(), object()).source == small_icon_source::app);
        app_calls = app.state().calls;
    }
    int bare_calls { 0 };
    {
        fake_env bare { 0 };
        CHECK(read_notification_small_icon(bare.env(), object()).source == small_icon_source::framework);
        bare_calls = bare.state().calls;
    }
    REQUIRE(app_calls > 1);
    REQUIRE(bare_calls > app_calls);
    for (int point { 1 }; point <= app_calls; ++point)
    {
        fake_env failing { app_icon_value, point };
        CHECK(read_notification_small_icon(failing.env(), object()).source == small_icon_source::unreadable);
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
    for (int point { 1 }; point <= bare_calls; ++point)
    {
        // 앱 정보를 읽는 동안의 실패는 앱에 아이콘이 없다는 뜻이 아니라 진단 글이 다르다.
        fake_env failing { 0, point };
        const small_icon_source expected { point <= app_calls ? small_icon_source::unreadable : small_icon_source::missing };
        CHECK(read_notification_small_icon(failing.env(), object()).source == expected);
        CHECK_FALSE(failing.state().pending);
        CHECK(failing.state().calls_while_pending == 0);
        CHECK(failing.state().popped == failing.state().pushed);
    }
}

TEST_CASE("Notification small icon makes no JNI call when its local frame cannot open", "[android][notification]")
{
    fake_env failing { app_icon_value, 1 };
    CHECK(read_notification_small_icon(failing.env(), object()).source == small_icon_source::unreadable);
    CHECK(failing.state().calls == 1);
    CHECK(failing.state().popped == 0);
    CHECK_FALSE(failing.state().pending);
}

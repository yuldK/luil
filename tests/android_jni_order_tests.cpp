#include "android/android_clipboard.h"
#include "android/android_system_theme.h"
#include "android/android_window_control.h"
#include "android/java_vm.h"
#include "luil/app/platform_services.h"
#include "luil/app/secret_store.h"

#include <catch2/catch_test_macros.hpp>

#include <jni.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

// 가짜 JNIEnv로 시스템 색·대비·막대·햅틱·권한 읽기와 쓰기의 JNI 흐름을 잠근다 (android_settings_tests.cpp와 같은 틀).
//  - 실패 지점 번호의 호출 하나가 예외를 남기고 null이나 0을 돌려준다. 지점을 바꿔 가며 모두 친다.
//  - 틀을 여는 첫 호출(지점 1)도 친다. `frame.pushed() == false`만 보고 돌아가는 자리가 많아, 틀을 얻지 못했을 때 남은 예외는
//    `jni_local_frame`의 생성자가 거둔다. 호출부마다 거두지 않아도 예외가 남지 않는지를 모든 읽기와 쓰기에서 본다.
//  - 값을 읽는 `GetIntField`·`GetStaticIntField`도 실패 지점이다. 읽은 값을 쓰기 전에 예외를 거두는지, 모든 JNI 호출이
//    앞 호출의 예외를 거둔 뒤에 불리는지(예외가 남은 채 부른 호출 수 0)를 아홉 읽기와 쓰기 모두에서 본다.
//  - 읽기의 틀은 API 수준에 따라 갈린다. 기기에서 JNI 호출이 하나도 없으면 그 읽기는 SKIP한다.
//  - 주소 열기와 비밀 저장소처럼 thread를 JVM에 붙이는 길은 가짜 JavaVM으로 같은 가짜 JNIEnv를 내준다.
//  - 클립보드는 글·HTML·그림 URI를 다 든 클립과 글 없는 URI 항목으로 읽기의 모든 갈래를 지난다.
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
        // 클립 항목의 `getText`가 null이다 (글 없는 URI 항목).
        bool clip_text_missing { false };
        // `ContentResolver.getType`의 답이다.
        const char* mime { "image/png" };
        // `InputStream.read`를 부른 수다. 첫 번은 네 바이트, 그 뒤는 끝(-1)이다.
        int reads { 0 };
        // `KeyStore.getKey`가 null이다 (아직 키가 없다).
        bool key_missing { false };
    };

    fake_state* current { nullptr };
    JNIEnv* current_env { nullptr };
    int marker { 0 };
    // `getType`의 답과 그 `toString`이다. 어느 글을 읽는지 가른다.
    int mime_marker { 0 };
    int mime_text_marker { 0 };
    // `Cipher.getIV`의 답이다. 길이가 12바이트다.
    int iv_marker { 0 };

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

    // 예외 꺼내기는 예외가 남아도 불러도 되는 JNI다. 세지 않는다.
    jthrowable JNICALL exception_occurred(JNIEnv*)
    {
        return reinterpret_cast<jthrowable>(&marker);
    }

    jboolean JNICALL is_instance_of(JNIEnv*, jobject, jclass)
    {
        note();
        return JNI_TRUE;
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

    jobject JNICALL new_object_v(JNIEnv*, jclass, jmethodID, va_list)
    {
        if (step())
            return nullptr;
        return object();
    }

    jstring JNICALL new_string_utf(JNIEnv*, const char*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jstring>(&marker);
    }

    const char* JNICALL get_string_utf_chars(JNIEnv*, const jstring text, jboolean*)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<const int*>(text) == &mime_text_marker ? current->mime : "hello";
    }

    // 글 놓기는 예외가 남아도 불러도 되는 JNI다. 세지 않는다.
    void JNICALL release_string_utf_chars(JNIEnv*, jstring, const char*)
    {
    }

    jbyteArray JNICALL new_byte_array(JNIEnv*, const jsize)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jbyteArray>(&marker);
    }

    void JNICALL get_byte_array_region(JNIEnv*, jbyteArray, const jsize, const jsize, jbyte*)
    {
        static_cast<void>(step());
    }

    void JNICALL set_byte_array_region(JNIEnv*, jbyteArray, const jsize, const jsize, const jbyte*)
    {
        static_cast<void>(step());
    }

    // 배열 길이는 예외를 내지 않는다. 암호문은 태그만큼인 16바이트로 둔다.
    jsize JNICALL get_array_length(JNIEnv*, const jarray array)
    {
        note();
        return reinterpret_cast<const int*>(array) == &iv_marker ? 12 : 16;
    }

    jobjectArray JNICALL new_object_array(JNIEnv*, const jsize, jclass, jobject)
    {
        if (step())
            return nullptr;
        return reinterpret_cast<jobjectArray>(&marker);
    }

    jobject JNICALL call_object_v(JNIEnv*, const jobject receiver, const jmethodID method, va_list)
    {
        if (step())
            return nullptr;
        const std::string_view name { method_name(method) };
        if (name == "getText" && current->clip_text_missing)
            return nullptr;
        if (name == "getType")
            return reinterpret_cast<jobject>(&mime_marker);
        if (name == "toString" && reinterpret_cast<const int*>(receiver) == &mime_marker)
            return reinterpret_cast<jobject>(&mime_text_marker);
        if (name == "getKey" && current->key_missing)
            return nullptr;
        if (name == "getIV")
            return reinterpret_cast<jobject>(&iv_marker);
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
        const std::string_view name { method_name(method) };
        if (name == "getColor")
            return current->color;
        if (name == "checkSelfPermission")
            return current->permission;
        if (name == "getItemCount")
            return 1;
        if (name == "read")
            return ++current->reads == 1 ? 4 : -1;
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
        table.ExceptionOccurred = exception_occurred;
        table.IsInstanceOf = is_instance_of;
        table.NewObjectV = new_object_v;
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
        table.GetStringUTFChars = get_string_utf_chars;
        table.ReleaseStringUTFChars = release_string_utf_chars;
        table.NewByteArray = new_byte_array;
        table.GetByteArrayRegion = get_byte_array_region;
        table.SetByteArrayRegion = set_byte_array_region;
        table.GetArrayLength = get_array_length;
        table.NewObjectArray = new_object_array;
        table.CallObjectMethodV = call_object_v;
        table.CallStaticObjectMethodV = call_static_object_v;
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
    [[nodiscard]] outcome run_once(Run&& run, const int fail_at = 0, const int also_fail_at = 0)
    {
        fake_env scope { fail_at };
        current->also_fail_at = also_fail_at;
        const int answer { run(scope.env()) };
        return outcome { scope.state(), answer };
    }

    // 지금 살아 있는 가짜 환경의 JNIEnv를 내주는 JavaVM이다. thread를 붙이지 않고 그 env를 쓰게 한다.
    jint JNICALL vm_get_env(JavaVM*, void** const env, jint)
    {
        *env = current_env;
        return JNI_OK;
    }

    [[nodiscard]] JNIInvokeInterface make_vm_table()
    {
        JNIInvokeInterface table {};
        table.GetEnv = vm_get_env;
        return table;
    }

    // 살아 있는 동안만 프로세스의 JavaVM이 가짜다. `open_url`과 비밀 저장소는 JNIEnv를 인자로 받지 않고 여기서 얻는다.
    class fake_vm_scope
    {
    public:
        fake_vm_scope()
            : table_ { make_vm_table() }
            , vm_ { &table_ }
        {
            luil::android::set_java_vm(&vm_);
        }
        fake_vm_scope(const fake_vm_scope&) = delete;
        fake_vm_scope(fake_vm_scope&&) = delete;
        fake_vm_scope& operator=(const fake_vm_scope&) = delete;
        fake_vm_scope& operator=(fake_vm_scope&&) = delete;
        ~fake_vm_scope()
        {
            luil::android::set_java_vm(nullptr);
        }

    private:
        JNIInvokeInterface table_;
        JavaVM vm_;
    };

    // 주소 열기가 쓸 앱 context를 적어 둔다. 이미 있으면 먼저 적힌 것을 쓴다.
    void capture_app_context()
    {
        fake_env scope {};
        luil::android::set_application_context(scope.env(), object());
    }

    // 실패 지점 하나를 친 결과다. 예외는 남지 않고, 틀은 닫혀야 한다.
    //  - 지점 1은 틀을 여는 호출이다. 틀을 얻지 못한 호출은 다른 JNI 호출을 하지 않고 돌아가야 한다.
    //  - 어느 지점에서도 예외가 남은 채 다음 JNI 호출을 부르면 안 된다.
    void check_clean(const outcome& failing, const int point)
    {
        CHECK_FALSE(failing.state.pending);
        if (point == 1)
            CHECK(failing.state.calls == 1);
        CHECK(failing.state.calls_while_pending == 0);
        CHECK(failing.state.popped == failing.state.pushed);
    }

    // 성공한 한 번을 기준으로 실패 지점을 모두 친다. 항목마다 따로 읽는 읽기처럼 지점마다 답이 다른 자리에 쓴다.
    template <typename Run>
    void check_clean_failure_points(Run&& run, const outcome& success)
    {
        for (int point { 1 }; point <= success.state.calls; ++point)
        {
            INFO("failure point " << point);
            check_clean(run_once(run, point), point);
        }
    }

    // 성공한 한 번을 기준으로 실패 지점을 모두 친다. 답은 `failure_answer`다.
    template <typename Run>
    void check_failure_points(Run&& run, const outcome& success, const int failure_answer)
    {
        for (int point { 1 }; point <= success.state.calls; ++point)
        {
            INFO("failure point " << point);
            const outcome failing { run_once(run, point) };
            CHECK(failing.answer == failure_answer);
            check_clean(failing, point);
        }
    }
}

// 프로세스에 하나인 context는 한 번 적으면 지울 수 없다. CTest는 test마다 프로세스를 따로 띄워 여기서 늘 비어 있고,
// 한 프로세스에 몰아 돌릴 때 먼저 적혔으면 건너뛴다.
namespace {
    // 비밀 저장소가 쓸 앱 폴더다. 기기 셸에는 정해진 임시 폴더가 없어 실행 파일이 도는 자리 아래에 둔다.
    class secret_folder
    {
    public:
        secret_folder()
            : path_ { std::filesystem::current_path() / "luil_jni_secret_store" }
        {
            std::filesystem::remove_all(path_);
            std::filesystem::create_directories(path_);
            luil::android::set_files_directory(path_.string().c_str());
        }
        secret_folder(const secret_folder&) = delete;
        secret_folder(secret_folder&&) = delete;
        secret_folder& operator=(const secret_folder&) = delete;
        secret_folder& operator=(secret_folder&&) = delete;
        ~secret_folder()
        {
            luil::android::set_files_directory(nullptr);
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] std::ptrdiff_t stored_files() const
        {
            return std::distance(std::filesystem::directory_iterator { path_ / "luil-secrets" }, std::filesystem::directory_iterator {});
        }

    private:
        std::filesystem::path path_;
    };

    constexpr std::uint8_t sample_secret[] { 1, 2, 3, 4 };

    [[nodiscard]] int store_sample()
    {
        return luil::store_secret(u8"service", u8"name", sample_secret).status == luil::secret_store_status::ok ? 1 : 0;
    }

    // 예외를 글로 옮기는 동안 부르는 JNI 수다 (클래스, 메서드, `toString`, 글 읽기).
    constexpr int exception_text_calls { 4 };

    // 비밀 저장소의 실패 지점을 모두 친다.
    //  - 지점 1은 틀을 여는 호출이다. 저장소는 틀 없이도 일을 이어 성공한다.
    //  - 예외를 글로 옮기는 호출이 또 실패하는 자리까지 둘째 지점으로 친다.
    template <typename Run>
    void check_secret_failure_points(Run&& run, const outcome& success)
    {
        for (int point { 1 }; point <= success.state.calls; ++point)
        {
            INFO("failure point " << point);
            const outcome failing { run_once(run, point) };
            CHECK(failing.answer == (point == 1 ? success.answer : 0));
            CHECK_FALSE(failing.state.pending);
            CHECK(failing.state.calls_while_pending == 0);
            CHECK(failing.state.popped == failing.state.pushed);
            for (int second { point + 1 }; second <= point + exception_text_calls; ++second)
            {
                INFO("second failure point " << second);
                const outcome twice { run_once(run, point, second) };
                CHECK_FALSE(twice.state.pending);
                CHECK(twice.state.calls_while_pending == 0);
                CHECK(twice.state.popped == twice.state.pushed);
            }
        }
    }
}

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

TEST_CASE("Copying text leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::copy_text_to_clipboard(env, object(), u8"hello") ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.void_calls == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Copying rich text leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::copy_rich_text_to_clipboard(env, object(), u8"hello", u8"<b>hello</b>") ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.void_calls == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Reading clipboard text leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        return luil::android::read_text_from_clipboard(env, object()).has_value() ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Reading a text URI item leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        current->clip_text_missing = true;
        current->mime = "text/plain";
        return luil::android::read_text_from_clipboard(env, object()).has_value() ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("An image URI item reads as no text and leaves no pending exception", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        current->clip_text_missing = true;
        return luil::android::read_text_from_clipboard(env, object()).has_value() ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 0);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("Reading clipboard data leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const auto run = [](JNIEnv& env)
    {
        const luil::clipboard_data data { luil::android::read_clipboard_data(env, object()) };
        const bool has_image { data.image != nullptr && data.image->size() == 4 };
        return (data.text.empty() ? 0 : 1) + (data.html.empty() ? 0 : 2) + (has_image ? 4 : 0);
    };
    const outcome success { run_once(run) };
    // 글, 그 항목의 HTML, 그림 URI의 네 바이트를 다 읽는 길이다.
    CHECK(success.answer == 7);
    CHECK(success.state.void_calls == 1);
    CHECK(success.state.popped == success.state.pushed);
    check_clean_failure_points(run, success);
}

TEST_CASE("Opening a URL leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    capture_app_context();
    const auto run = [](JNIEnv&)
    {
        return luil::open_url(u8"https://example.com/").ok() ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.void_calls == 1);
    CHECK(success.state.popped == 1);
    check_failure_points(run, success, 0);
}

TEST_CASE("A URL no app can open answers not found without a pending exception", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    capture_app_context();
    const auto run = [](JNIEnv&)
    {
        return static_cast<int>(luil::open_url(u8"https://example.com/").status);
    };
    const outcome success { run_once(run) };
    REQUIRE(success.answer == static_cast<int>(luil::platform_status::ok));
    // 마지막 호출이 `startActivity`다. 그 예외를 거둔 뒤에 `ActivityNotFoundException`을 찾는다.
    const int start_activity { success.state.calls };
    const outcome not_found { run_once(run, start_activity) };
    CHECK(not_found.answer == static_cast<int>(luil::platform_status::not_found));
    check_clean(not_found, start_activity);
    // 예외 종류를 찾는 호출마저 실패해도 예외가 남지 않는다.
    const outcome unknown { run_once(run, start_activity, start_activity + 1) };
    CHECK(unknown.answer == static_cast<int>(luil::platform_status::failed));
    check_clean(unknown, start_activity);
}

TEST_CASE("Storing a secret with an existing key leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    const secret_folder folder {};
    const auto run = [](JNIEnv&)
    {
        return store_sample();
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    CHECK(folder.stored_files() == 1);
    check_secret_failure_points(run, success);
}

TEST_CASE("Storing a secret that creates its key leaves no pending exception when any JNI call fails", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    const secret_folder folder {};
    const auto run = [](JNIEnv&)
    {
        current->key_missing = true;
        return store_sample();
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    CHECK(folder.stored_files() == 1);
    check_secret_failure_points(run, success);
}

TEST_CASE("Loading a secret leaves no pending exception and keeps the file when any JNI call fails", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    const secret_folder folder {};
    const auto store = [](JNIEnv&)
    {
        return store_sample();
    };
    REQUIRE(run_once(store).answer == 1);
    const auto run = [&folder](JNIEnv&)
    {
        std::vector<std::uint8_t> secret {};
        const bool opened { luil::load_secret(u8"service", u8"name", secret).status == luil::secret_store_status::ok };
        // 실패를 "키가 없다"로 읽으면 풀 수 있는 파일을 지운다. 파일이 남았는지까지 답에 넣는다.
        if (folder.stored_files() != 1)
            return -1;
        return opened && secret.size() == 16 ? 1 : 0;
    };
    const outcome success { run_once(run) };
    CHECK(success.answer == 1);
    CHECK(success.state.popped == 1);
    check_secret_failure_points(run, success);
}

TEST_CASE("Loading a secret whose key is gone removes the file and answers not found", "[android][jni_order]")
{
    const fake_vm_scope vm {};
    const secret_folder folder {};
    const auto store = [](JNIEnv&)
    {
        return store_sample();
    };
    REQUIRE(run_once(store).answer == 1);
    const auto run = [](JNIEnv&)
    {
        current->key_missing = true;
        std::vector<std::uint8_t> secret {};
        return luil::load_secret(u8"service", u8"name", secret).status == luil::secret_store_status::not_found ? 1 : 0;
    };
    const outcome gone { run_once(run) };
    CHECK(gone.answer == 1);
    CHECK(gone.state.calls_while_pending == 0);
    CHECK(folder.stored_files() == 0);
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

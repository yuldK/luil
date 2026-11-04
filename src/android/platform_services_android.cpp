#include "luil/app/platform_services.h"

#include "android/android_locale.h"
#include "android/android_text_input.h"
#include "android/errno_text.h"
#include "android/java_vm.h"
#include "host/font_registry.h"

#include <jni.h>

#include <cerrno>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>

// 플랫폼 도구의 Android 쪽이다 (platform-services-design.md). 뜻이 있는 것은 POSIX 호출로 같은 일을 하고,
// 주소 열기는 JNI로 Intent를 띄운다. 파일 관리자·프로그램 실행과 막힌 I/O 취소처럼 뜻이 없는 것은 `unsupported`로 답한다.
namespace luil {
    namespace {
        using android::jni_failed;
        using android::jni_local_frame;

        [[nodiscard]] platform_result success() noexcept
        {
            return { platform_status::ok, {}, std::nullopt };
        }

        [[nodiscard]] platform_result unsupported(const std::u8string_view why)
        {
            return { platform_status::unsupported, std::u8string { why }, std::nullopt };
        }

        [[nodiscard]] platform_result errno_failure(const std::u8string_view what)
        {
            return { platform_status::failed, android::make_errno_error(what), std::nullopt };
        }

        [[nodiscard]] std::string narrow(const std::u8string_view text)
        {
            return { text.begin(), text.end() };
        }

        // `ACTION_VIEW`로 주소를 여는 Intent다. 실패하면 nullptr이고 예외는 거뒀다.
        [[nodiscard]] jobject view_intent(JNIEnv& env, const std::u8string_view url)
        {
            const jclass uri_class { env.FindClass("android/net/Uri") };
            if (jni_failed(env) || uri_class == nullptr)
                return nullptr;
            const jmethodID parse { env.GetStaticMethodID(uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;") };
            if (jni_failed(env) || parse == nullptr)
                return nullptr;
            // JNI의 글은 modified UTF-8이다. BMP 밖 글자(이모지 도메인 등)는 그대로 넘기면 깨진다.
            const jstring text { env.NewStringUTF(android::modified_utf8_from_utf8(url).c_str()) };
            if (jni_failed(env) || text == nullptr)
                return nullptr;
            const jobject uri { env.CallStaticObjectMethod(uri_class, parse, text) };
            if (jni_failed(env) || uri == nullptr)
                return nullptr;

            const jclass intent_class { env.FindClass("android/content/Intent") };
            if (jni_failed(env) || intent_class == nullptr)
                return nullptr;
            const jmethodID construct { env.GetMethodID(intent_class, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V") };
            if (jni_failed(env) || construct == nullptr)
                return nullptr;
            const jmethodID add_flags { env.GetMethodID(intent_class, "addFlags", "(I)Landroid/content/Intent;") };
            if (jni_failed(env) || add_flags == nullptr)
                return nullptr;
            const jmethodID add_category { env.GetMethodID(intent_class, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;") };
            if (jni_failed(env) || add_category == nullptr)
                return nullptr;
            const jstring action { env.NewStringUTF("android.intent.action.VIEW") };
            if (jni_failed(env) || action == nullptr)
                return nullptr;
            const jstring browsable { env.NewStringUTF("android.intent.category.BROWSABLE") };
            if (jni_failed(env) || browsable == nullptr)
                return nullptr;
            const jobject intent { env.NewObject(intent_class, construct, action, uri) };
            if (jni_failed(env) || intent == nullptr)
                return nullptr;
            // Application context에서 띄우므로 새 task가 반드시 있어야 한다 (FLAG_ACTIVITY_NEW_TASK).
            constexpr jint flag_activity_new_task { 0x10000000 };
            static_cast<void>(env.CallObjectMethod(intent, add_flags, flag_activity_new_task));
            if (jni_failed(env))
                return nullptr;
            // 믿을 수 없는 링크라 브라우저처럼 BROWSABLE을 단다. 링크로 불려도 안전하다고 밝힌 앱만 받는다.
            static_cast<void>(env.CallObjectMethod(intent, add_category, browsable));
            if (jni_failed(env))
                return nullptr;
            return intent;
        }

        // 쓰기가 신호로 끊겨도 남은 만큼 잇는다.
        [[nodiscard]] bool write_all(const int file, std::span<const std::uint8_t> bytes)
        {
            while (bytes.empty() == false)
            {
                const ssize_t written { ::write(file, bytes.data(), bytes.size()) };
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0)
                    return false;
                bytes = bytes.subspan(static_cast<std::size_t>(written));
            }
            return true;
        }
    }

    platform_result replace_file_atomically(const std::u8string_view path, const std::span<const std::uint8_t> contents)
    {
        if (path.empty())
            return { platform_status::failed, u8"The file path is empty.", std::nullopt };
        const std::string target { narrow(path) };
        const std::string temporary { target + "." + std::to_string(::getpid()) + ".tmp" };
        const int file { ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) };
        if (file < 0)
            return errno == ENOENT ? platform_result { platform_status::failed, u8"The folder of the file does not exist.", std::nullopt } : errno_failure(u8"Failed to create the temporary file");
        // 다 쓰고 디스크까지 내린 뒤에만 이름을 바꾼다. 그 앞에서 끊기면 옛 파일이 그대로다.
        const bool written { write_all(file, contents) && ::fsync(file) == 0 };
        const platform_result failed_write { written ? success() : errno_failure(u8"Failed to write the temporary file") };
        ::close(file);
        if (written == false)
        {
            ::unlink(temporary.c_str());
            return failed_write;
        }
        if (::rename(temporary.c_str(), target.c_str()) != 0)
        {
            const platform_result result { errno_failure(u8"Failed to replace the file") };
            ::unlink(temporary.c_str());
            return result;
        }
        // 이름 바꾸기 자체를 디스크에 남기려면 폴더도 내린다.
        const std::size_t slash { target.find_last_of('/') };
        const std::string folder { slash == std::string::npos ? std::string { "." } : target.substr(0, slash == 0 ? 1 : slash) };
        if (const int directory { ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC) }; directory >= 0)
        {
            static_cast<void>(::fsync(directory));
            ::close(directory);
        }
        return success();
    }

    struct single_instance::state
    {
        std::u8string name {};
        platform_result status {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        state_->name = name;
        // OS가 앱(패키지)마다 프로세스와 task를 하나로 묶는다 — 이 프로세스가 곧 첫째다.
        state_->status = unsupported(u8"Android keeps one process per app; every instance is the primary one.");
    }

    single_instance::single_instance(single_instance&&) noexcept = default;
    single_instance& single_instance::operator=(single_instance&&) noexcept = default;
    single_instance::~single_instance() = default;

    bool single_instance::primary() const noexcept
    {
        return state_ != nullptr;
    }

    const platform_result& single_instance::status() const noexcept
    {
        static const platform_result moved { platform_status::failed, u8"The instance guard was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    std::u8string_view single_instance::name() const noexcept
    {
        return state_ != nullptr ? std::u8string_view { state_->name } : std::u8string_view {};
    }

    platform_result single_instance::send_to_primary(const std::span<const std::u8string> arguments, const instance_send_options& options) const
    {
        static_cast<void>(arguments);
        static_cast<void>(options);
        // 둘째 프로세스가 생기지 않는다. 앱을 다시 여는 요청은 Activity의 Intent로 오고 이 길이 아니다.
        // `primary()`가 늘 참이어도 `not_found`보다 `unsupported`가 먼저다.
        return unsupported(u8"Android keeps one process per app; there is no second instance to send arguments from.");
    }

    struct file_lock::state
    {
        int file { -1 };
        bool locked { false };
        platform_result status {};

        state() = default;
        state(const state&) = delete;
        state& operator=(const state&) = delete;

        ~state()
        {
            // 닫으면 flock 잠금도 함께 놓인다.
            if (file >= 0)
                ::close(file);
        }
    };

    file_lock::file_lock(const std::u8string_view path)
        : state_ { std::make_unique<state>() }
    {
        if (path.empty())
        {
            state_->status = { platform_status::failed, u8"The lock path is empty.", std::nullopt };
            return;
        }
        state_->file = ::open(narrow(path).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (state_->file < 0)
        {
            state_->status = errno_failure(u8"Failed to open the lock file");
            return;
        }
        // flock은 열린 파일 기술마다라 같은 프로세스의 두 객체도 서로 막는다 (Windows의 LockFileEx와 같다).
        if (::flock(state_->file, LOCK_EX | LOCK_NB) != 0)
        {
            state_->status = errno == EWOULDBLOCK ? success() : errno_failure(u8"Failed to lock the file");
            return;
        }
        state_->locked = true;
        state_->status = success();
    }

    file_lock::file_lock(file_lock&&) noexcept = default;
    file_lock& file_lock::operator=(file_lock&&) noexcept = default;
    file_lock::~file_lock() = default;

    bool file_lock::locked() const noexcept
    {
        return state_ != nullptr && state_->locked;
    }

    const platform_result& file_lock::status() const noexcept
    {
        static const platform_result moved { platform_status::failed, u8"The file lock was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    platform_result reveal_in_file_manager(const std::u8string_view path)
    {
        static_cast<void>(path);
        return unsupported(u8"Android has no common file manager that selects a given file.");
    }

    platform_result launch_program(const std::u8string_view program, const std::span<const std::u8string> arguments, const launch_options& options)
    {
        static_cast<void>(program);
        static_cast<void>(arguments);
        static_cast<void>(options);
        return unsupported(u8"Android opens other apps through intents, not processes; open_url opens addresses.");
    }

    platform_result open_url(const std::u8string_view url, const open_url_options& options)
    {
        if (url_scheme_allowed(url, options.schemes) == false)
            return { platform_status::failed, u8"The URL has no scheme or its scheme is not allowed.", std::nullopt };
        const jobject context { android::application_context() };
        // 부른 thread에서 바로 띄운다. 메인 thread로 넘기면 답(처리할 앱이 없음)을 돌려줄 수 없고,
        // UI thread가 메인 thread를 기다리면 glue가 UI thread를 기다리는 자리에서 서로 막힌다.
        const android::jni_thread_scope scope { "luil-open-url" };
        JNIEnv* const env { scope.env() };
        if (context == nullptr || env == nullptr)
            return { platform_status::failed, u8"The app context is not available; the app must link luil::luil, which captures it in GameActivity_onCreate.", std::nullopt };
        const jni_local_frame frame { *env };
        if (frame.pushed() == false)
        {
            env->ExceptionClear();
            return { platform_status::failed, u8"Failed to reserve JNI local references.", std::nullopt };
        }
        const jobject intent { view_intent(*env, url) };
        if (intent == nullptr)
            return { platform_status::failed, u8"Failed to build the intent for the URL.", std::nullopt };
        const jclass context_class { env->FindClass("android/content/Context") };
        const jmethodID start_activity { context_class != nullptr ? env->GetMethodID(context_class, "startActivity", "(Landroid/content/Intent;)V") : nullptr };
        if (jni_failed(*env) || start_activity == nullptr)
            return { platform_status::failed, u8"Failed to find Context.startActivity.", std::nullopt };
        env->CallVoidMethod(context, start_activity, intent);
        if (env->ExceptionCheck() == JNI_FALSE)
            return success();

        // 예외를 거둔 뒤에야 다른 JNI를 부를 수 있다.
        const jthrowable thrown { env->ExceptionOccurred() };
        env->ExceptionClear();
        const jclass not_found_class { env->FindClass("android/content/ActivityNotFoundException") };
        const bool no_handler { jni_failed(*env) == false && not_found_class != nullptr && env->IsInstanceOf(thrown, not_found_class) };
        if (no_handler)
            return { platform_status::not_found, u8"No app can open the URL.", std::nullopt };
        return { platform_status::failed, u8"Failed to start the activity for the URL.", std::nullopt };
    }

    platform_result set_background_priority(const bool background)
    {
        // Android의 배경 thread 우선순위(THREAD_PRIORITY_BACKGROUND)가 nice 10이다.
        if (::setpriority(PRIO_PROCESS, static_cast<id_t>(::gettid()), background ? 10 : 0) != 0)
            return errno_failure(u8"Failed to change the thread priority");
        return success();
    }

    struct blocking_io_canceller::state
    {
    };

    blocking_io_canceller::blocking_io_canceller()
        : state_ { std::make_unique<state>() }
    {
    }

    blocking_io_canceller::blocking_io_canceller(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller& blocking_io_canceller::operator=(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller::~blocking_io_canceller() = default;

    platform_result blocking_io_canceller::cancel() const
    {
        return unsupported(u8"Android has no call that cancels another thread's blocking I/O.");
    }

    std::optional<std::chrono::weekday> user_first_weekday()
    {
        // 앱 thread는 JVM에 붙어 있지 않을 수 있어 범위 동안 붙인다.
        const android::jni_thread_scope scope { "luil-locale" };
        JNIEnv* const env { scope.env() };
        if (env == nullptr)
            return std::nullopt;
        return android::read_first_weekday(*env);
    }

    std::vector<std::u8string> user_ui_languages()
    {
        // 호스트가 `AConfiguration`에서 읽어 둔 태그다. JNI가 없고 Activity가 다시 설 때 새 값이 된다.
        const std::string language { user_ui_language() };
        if (language.empty())
            return {};
        return { std::u8string { language.begin(), language.end() } };
    }
}

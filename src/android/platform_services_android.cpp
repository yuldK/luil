#include "luil/app/platform_services.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>

// 플랫폼 도구의 Android 쪽이다 (platform-services-design.md). 뜻이 있는 것은 POSIX 호출로 같은 일을 하고,
// 다른 앱을 여는 일과 막힌 I/O 취소처럼 뜻이 없는 것은 `unsupported`로 답한다.
namespace luil {
    namespace {
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
            std::u8string message { what };
            message += u8" (";
            for (const char* at { std::strerror(errno) }; *at != '\0'; ++at)
                message.push_back(static_cast<char8_t>(*at));
            message += u8").";
            return { platform_status::failed, std::move(message), std::nullopt };
        }

        [[nodiscard]] std::string narrow(const std::u8string_view text)
        {
            return { text.begin(), text.end() };
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
    } // namespace

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
        platform_result status {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        static_cast<void>(name);
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
        return unsupported(u8"Opening another app is an Activity intent on Android, which needs Java.");
    }

    platform_result launch_program(const std::u8string_view program, const std::span<const std::u8string> arguments, const launch_options& options)
    {
        static_cast<void>(program);
        static_cast<void>(arguments);
        static_cast<void>(options);
        return unsupported(u8"Opening another app is an Activity intent on Android, which needs Java.");
    }

    platform_result set_background_priority(const bool background)
    {
        // Android의 배경 thread 우선순위(THREAD_PRIORITY_BACKGROUND)가 nice 10이다.
        if (::setpriority(PRIO_PROCESS, static_cast<id_t>(::gettid()), background ? 10 : 0) != 0)
            return errno_failure(u8"Failed to change the thread priority");
        return success();
    }

    struct blocking_io_canceller::state
    {};

    blocking_io_canceller::blocking_io_canceller()
        : state_ { std::make_unique<state>() }
    {}

    blocking_io_canceller::blocking_io_canceller(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller& blocking_io_canceller::operator=(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller::~blocking_io_canceller() = default;

    platform_result blocking_io_canceller::cancel() const
    {
        return unsupported(u8"Android has no call that cancels another thread's blocking I/O.");
    }
} // namespace luil

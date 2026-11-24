#include "luil/app/platform_services.h"

#include "macos/macos_text.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// 플랫폼 도구의 macOS 쪽이다 (platform-services-design.md).
//  - 파일 바꾸기·잠금·우선순위는 Android와 같은 POSIX 호출이다.
//  - 주소 열기와 Finder에서 보이기는 NSWorkspace, 언어와 주의 첫날은 NSLocale·NSCalendar다.
//  - 단일 인스턴스는 아직 없다. 같은 앱을 두 번 띄우면 둘 다 첫째다 (Android와 같은 답).
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
            return { platform_status::failed, macos::errno_error(what), std::nullopt };
        }

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
        const std::string target { macos::narrow(path) };
        const std::string temporary { target + "." + std::to_string(::getpid()) + ".tmp" };
        const int file { ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) };
        if (file < 0)
            return errno == ENOENT ? platform_result { platform_status::failed, u8"The folder of the file does not exist.", std::nullopt } : errno_failure(u8"Failed to create the temporary file");
        // macOS의 fsync는 장치 캐시까지 내리지 않는다. 이름을 바꾸기 전에 F_FULLFSYNC로 내린다.
        const bool written { write_all(file, contents) && (::fcntl(file, F_FULLFSYNC) == 0 || ::fsync(file) == 0) };
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
        state_->status = unsupported(u8"Single instance arguments are not implemented on macOS yet; every instance is the primary one.");
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
        return unsupported(u8"Single instance arguments are not implemented on macOS yet.");
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
        state_->file = ::open(macos::narrow(path).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (state_->file < 0)
        {
            state_->status = errno_failure(u8"Failed to open the lock file");
            return;
        }
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
        if (path.empty())
            return { platform_status::failed, u8"The path is empty.", std::nullopt };
        @autoreleasepool
        {
            NSString* const text { macos::make_ns_string(path) };
            if (text == nil)
                return { platform_status::failed, u8"The path is not valid UTF-8.", std::nullopt };
            if ([[NSFileManager defaultManager] fileExistsAtPath:text] == NO)
                return { platform_status::not_found, u8"The file does not exist.", std::nullopt };
            NSURL* const url { [NSURL fileURLWithPath:text] };
            [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ url ]];
        }
        return success();
    }

    platform_result launch_program(const std::u8string_view program, const std::span<const std::u8string> arguments, const launch_options& options)
    {
        if (program.empty())
            return { platform_status::failed, u8"The program path is empty.", std::nullopt };
        std::vector<std::string> owned {};
        owned.reserve(arguments.size() + 1);
        owned.push_back(macos::narrow(program));
        for (const std::u8string& argument : arguments)
            owned.push_back(macos::narrow(argument));
        std::vector<char*> argv {};
        argv.reserve(owned.size() + 1);
        for (std::string& argument : owned)
            argv.push_back(argument.data());
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions {};
        posix_spawn_file_actions_init(&actions);
        const std::string directory { macos::narrow(options.working_directory) };
        if (directory.empty() == false)
            posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
        pid_t child { 0 };
        const int spawned { ::posix_spawnp(&child, owned.front().c_str(), &actions, nullptr, argv.data(), environ) };
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0)
        {
            errno = spawned;
            return spawned == ENOENT ? platform_result { platform_status::not_found, u8"The program was not found.", std::nullopt } : errno_failure(u8"Failed to start the program");
        }
        if (options.wait == false)
            return success();
        int status { 0 };
        while (::waitpid(child, &status, 0) < 0)
            if (errno != EINTR)
                return errno_failure(u8"Failed to wait for the program");
        platform_result result { success() };
        if (WIFEXITED(status))
            result.exit_code = WEXITSTATUS(status);
        return result;
    }

    platform_result open_url(const std::u8string_view url, const open_url_options& options)
    {
        if (url_scheme_allowed(url, options.schemes) == false)
            return { platform_status::failed, u8"The URL has no scheme or its scheme is not allowed.", std::nullopt };
        @autoreleasepool
        {
            NSString* const text { macos::make_ns_string(url) };
            NSURL* const value { text != nil ? [NSURL URLWithString:text] : nil };
            if (value == nil)
                return { platform_status::failed, u8"The URL is not valid.", std::nullopt };
            if ([[NSWorkspace sharedWorkspace] openURL:value] == NO)
                return { platform_status::not_found, u8"No app can open the URL.", std::nullopt };
        }
        return success();
    }

    platform_result set_background_priority(const bool background)
    {
        // QoS가 macOS의 thread 우선순위다. 배경은 utility, 되돌리면 기본이다.
        if (::pthread_set_qos_class_self_np(background ? QOS_CLASS_UTILITY : QOS_CLASS_DEFAULT, 0) != 0)
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
        return unsupported(u8"macOS has no call that cancels another thread's blocking I/O.");
    }

    std::optional<std::chrono::weekday> user_first_weekday()
    {
        @autoreleasepool
        {
            // NSCalendar는 일요일이 1이다. chrono의 weekday는 일요일이 0이다.
            const NSUInteger first { [[NSCalendar currentCalendar] firstWeekday] };
            if (first < 1 || first > 7)
                return std::nullopt;
            return std::chrono::weekday { static_cast<unsigned>(first - 1) };
        }
    }

    std::vector<std::u8string> user_ui_languages()
    {
        std::vector<std::u8string> languages {};
        @autoreleasepool
        {
            for (NSString* const language in [NSLocale preferredLanguages])
                if (std::u8string tag { macos::from_ns_string(language) }; tag.empty() == false)
                    languages.push_back(std::move(tag));
        }
        return languages;
    }
}

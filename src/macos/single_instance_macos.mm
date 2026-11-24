#include "luil/app/platform_services.h"

#include "luil/text/utf8_text.h"
#include "macos/instance_message.h"
#include "macos/macos_text.h"
#include "macos/single_instance_receiver.h"

#import <AppKit/AppKit.h>

#include <CommonCrypto/CommonDigest.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

// 단일 인스턴스와 인자 넘기기의 macOS 쪽이다 (single-instance-args-design.md).
//  - 첫째 여부는 사용자 임시 폴더(`_CS_DARWIN_USER_TEMP_DIR`)의 `luil-instance-<이름의 해시>.lock`에 건 `flock`이다.
//    끝나는 첫째가 쥔 채로 지워 파일이 쌓이지 않는다.
//    같은 사용자의 프로세스끼리만 보이고, 프로세스가 죽으면 OS가 놓는다. 같은 프로세스의 두 객체도 서로 막는다.
//  - 인자는 그 옆의 Unix 소켓으로 간다. 첫째가 받는 자리를 열면(`set_instance_receiver`) 배경 thread가 받아
//    메인 thread에서 앱에 건넨다. 형식은 macos/instance_message.h다.
namespace luil {
    namespace {
        using clock = std::chrono::steady_clock;

        // 첫째가 아직 받는 자리를 열지 않았으면 이만큼 쉬고 다시 찾는다 (Windows와 같다).
        constexpr std::chrono::milliseconds retry_pause { 50 };
        // 받는 쪽이 보내기 하나의 바이트를 기다리는 상한이다. 멈춘 보내는 쪽이 다음 둘째를 막지 않게 한다.
        constexpr std::chrono::milliseconds read_budget { 2000 };
        constexpr int listen_backlog { 16 };

        [[nodiscard]] platform_result success() noexcept
        {
            return { platform_status::ok, {}, std::nullopt };
        }

        [[nodiscard]] platform_result failure(std::u8string message)
        {
            return { platform_status::failed, std::move(message), std::nullopt };
        }

        [[nodiscard]] platform_result errno_failure(const std::u8string_view what)
        {
            return failure(macos::errno_error(what));
        }

        [[nodiscard]] platform_result not_found() noexcept
        {
            return { platform_status::not_found, {}, std::nullopt };
        }

        void close_file(int& file) noexcept
        {
            if (file >= 0)
                ::close(file);
            file = -1;
        }

        // 이 사용자의 임시 폴더다(`/`로 끝난다). 환경 변수(`TMPDIR`)가 아니라 OS에 물어, 환경이 다른 둘째도 같은 자리를 본다.
        [[nodiscard]] std::optional<std::string> user_folder()
        {
            std::array<char, PATH_MAX> buffer {};
            const size_t length { ::confstr(_CS_DARWIN_USER_TEMP_DIR, buffer.data(), buffer.size()) };
            if (length <= 1 || length > buffer.size())
                return std::nullopt;
            std::string folder { buffer.data() };
            if (folder.back() != '/')
                folder.push_back('/');
            return folder;
        }

        // 파일 이름의 몸이다. 이름의 UTF-8 바이트를 그대로 해시해 대소문자·NUL·길이와 관계없이 이름마다 다르다.
        //  - 소켓 경로는 `sun_path`의 104바이트에 들어야 한다. 해시 앞 12바이트(96비트)로 줄여 임시 폴더(약 50바이트) 뒤에 넉넉히 둔다.
        [[nodiscard]] std::string file_stem(const std::u8string_view name)
        {
            std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest {};
            CC_SHA256(name.data(), static_cast<CC_LONG>(name.size()), digest.data());
            constexpr char digits[] { "0123456789abcdef" };
            std::string stem { "luil-instance-" };
            for (std::size_t index { 0 }; index < 12; ++index)
            {
                stem.push_back(digits[digest[index] >> 4u]);
                stem.push_back(digits[digest[index] & 0x0Fu]);
            }
            return stem;
        }

        // 소켓 주소다. 경로가 `sun_path`에 들지 않으면 빈 값이다.
        [[nodiscard]] std::optional<sockaddr_un> socket_address(const std::string& path) noexcept
        {
            sockaddr_un address {};
            if (path.size() >= sizeof(address.sun_path))
                return std::nullopt;
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size());
            address.sun_len = static_cast<std::uint8_t>(SUN_LEN(&address));
            return address;
        }

        [[nodiscard]] platform_result path_too_long()
        {
            return failure(u8"The path of the instance socket is longer than a Unix socket address allows.");
        }

        // 쓰다 끊긴 상대 때문에 SIGPIPE로 죽지 않게 하고, 자식 프로세스에 물려주지 않는다.
        void prepare_socket(const int socket) noexcept
        {
            const int one { 1 };
            static_cast<void>(::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)));
            static_cast<void>(::fcntl(socket, F_SETFD, FD_CLOEXEC));
        }

        void set_nonblocking(const int socket) noexcept
        {
            const int flags { ::fcntl(socket, F_GETFL) };
            if (flags >= 0)
                static_cast<void>(::fcntl(socket, F_SETFL, flags | O_NONBLOCK));
        }

        [[nodiscard]] int remaining_milliseconds(const clock::time_point deadline) noexcept
        {
            const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()).count() };
            return static_cast<int>(std::clamp<long long>(remaining, 0, INT_MAX));
        }

        enum class transfer
        {
            done,
            // 시한이 지났다.
            timed_out,
            // 상대가 닫았다.
            closed,
            // 멈추라는 신호가 왔다.
            woken,
            failed,
        };

        // 막지 않는 소켓에 다 쓴다. `wake`가 읽을 수 있게 되면 멈춘다 (-1이면 보지 않는다).
        [[nodiscard]] transfer write_all(const int socket, std::span<const std::byte> bytes, const clock::time_point deadline, const int wake)
        {
            while (bytes.empty() == false)
            {
                const ssize_t written { ::send(socket, bytes.data(), bytes.size(), 0) };
                if (written > 0)
                {
                    bytes = bytes.subspan(static_cast<std::size_t>(written));
                    continue;
                }
                if (written < 0 && errno == EINTR)
                    continue;
                if (written < 0 && errno == EPIPE)
                    return transfer::closed;
                if (written < 0 && errno != EAGAIN)
                    return transfer::failed;
                pollfd descriptors[2] { { socket, POLLOUT, 0 }, { wake, POLLIN, 0 } };
                const int ready { ::poll(descriptors, wake >= 0 ? 2 : 1, remaining_milliseconds(deadline)) };
                if (ready < 0 && errno != EINTR)
                    return transfer::failed;
                if (wake >= 0 && (descriptors[1].revents & POLLIN) != 0)
                    return transfer::woken;
                if (ready == 0)
                    return transfer::timed_out;
            }
            return transfer::done;
        }

        // 막지 않는 소켓에서 꼭 그만큼 읽는다.
        [[nodiscard]] transfer read_exact(const int socket, std::span<std::byte> bytes, const clock::time_point deadline, const int wake)
        {
            while (bytes.empty() == false)
            {
                const ssize_t read { ::recv(socket, bytes.data(), bytes.size(), 0) };
                if (read > 0)
                {
                    bytes = bytes.subspan(static_cast<std::size_t>(read));
                    continue;
                }
                if (read == 0)
                    return transfer::closed;
                if (errno == EINTR)
                    continue;
                if (errno == ECONNRESET)
                    return transfer::closed;
                if (errno != EAGAIN)
                    return transfer::failed;
                pollfd descriptors[2] { { socket, POLLIN, 0 }, { wake, POLLIN, 0 } };
                const int ready { ::poll(descriptors, wake >= 0 ? 2 : 1, remaining_milliseconds(deadline)) };
                if (ready < 0 && errno != EINTR)
                    return transfer::failed;
                if (wake >= 0 && (descriptors[1].revents & POLLIN) != 0)
                    return transfer::woken;
                if (ready == 0)
                    return transfer::timed_out;
            }
            return transfer::done;
        }

        // 받는 함수의 자리다. 메인 queue에서 기다리는 건넴도 쥐어, 받는 자리가 닫힌 뒤에 와도 거절로 답한다.
        struct receiver_slot
        {
            std::mutex mutex {};
            macos::instance_receiver receiver {};
        };

        // 배경 thread가 읽은 보내기 하나다. 메인 thread에서 답하고 닫는다.
        struct pending_delivery
        {
            pending_delivery(std::shared_ptr<receiver_slot> owner, const int connection, instance_arguments value, const bool front) noexcept
                : slot { std::move(owner) }
                , socket { connection }
                , arguments { std::move(value) }
                , activate { front }
            {
            }

            pending_delivery(const pending_delivery&) = delete;
            pending_delivery& operator=(const pending_delivery&) = delete;

            ~pending_delivery()
            {
                close_file(socket);
            }

            std::shared_ptr<receiver_slot> slot {};
            int socket { -1 };
            instance_arguments arguments {};
            bool activate { false };
        };

        void answer(const int socket, const bool accepted) noexcept
        {
            const std::uint8_t value { accepted ? macos::instance_answer_accepted : macos::instance_answer_rejected };
            static_cast<void>(::send(socket, &value, 1, 0));
        }

        // 메인 queue에서 돈다. 받는 함수의 답을 보낸 쪽에 돌려준다.
        void deliver_on_main(void* const context) noexcept
        {
            const std::unique_ptr<pending_delivery> delivery { static_cast<pending_delivery*>(context) };
            bool accepted { false };
            try
            {
                macos::instance_receiver receiver {};
                {
                    const std::lock_guard lock { delivery->slot->mutex };
                    receiver = delivery->slot->receiver;
                }
                if (receiver)
                    accepted = receiver(delivery->arguments, delivery->activate);
            }
            catch (...)
            {
                accepted = false;
            }
            answer(delivery->socket, accepted);
        }

        // 첫째의 받는 자리다: 듣는 소켓, 멈춤 신호, 배경 thread.
        class instance_listener
        {
        public:
            instance_listener() = default;
            instance_listener(const instance_listener&) = delete;
            instance_listener& operator=(const instance_listener&) = delete;

            ~instance_listener()
            {
                stop();
            }

            [[nodiscard]] platform_result start(std::string path, std::shared_ptr<receiver_slot> owner)
            {
                path_ = std::move(path);
                slot_ = std::move(owner);
                const std::optional<sockaddr_un> address { socket_address(path_) };
                if (address.has_value() == false)
                    return path_too_long();
                if (::pipe(wake_.data()) != 0)
                    return errno_failure(u8"Failed to create the instance wake pipe");
                for (const int end : wake_)
                    static_cast<void>(::fcntl(end, F_SETFD, FD_CLOEXEC));
                socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (socket_ < 0)
                    return errno_failure(u8"Failed to create the instance socket");
                prepare_socket(socket_);
                // 잠금을 쥔 첫째만 여기에 온다. 남은 소켓 파일은 죽은 첫째의 것이다.
                static_cast<void>(::unlink(path_.c_str()));
                if (::bind(socket_, reinterpret_cast<const sockaddr*>(&*address), address->sun_len) != 0)
                    return errno_failure(u8"Failed to bind the instance socket");
                bound_ = true;
                // 같은 사용자만 연다. 임시 폴더도 그 사용자만 들어갈 수 있다.
                static_cast<void>(::chmod(path_.c_str(), S_IRUSR | S_IWUSR));
                if (::listen(socket_, listen_backlog) != 0)
                    return errno_failure(u8"Failed to listen on the instance socket");
                set_nonblocking(socket_);
                try
                {
                    thread_ = std::thread { [this] { run(); } };
                }
                catch (...)
                {
                    return failure(u8"Failed to start the instance receiver thread.");
                }
                return success();
            }

            void stop() noexcept
            {
                if (wake_[1] >= 0)
                {
                    const char byte { 1 };
                    static_cast<void>(::write(wake_[1], &byte, 1));
                }
                if (thread_.joinable())
                    thread_.join();
                close_file(socket_);
                if (bound_)
                    static_cast<void>(::unlink(path_.c_str()));
                bound_ = false;
                for (int& end : wake_)
                    close_file(end);
            }

        private:
            void run() noexcept
            {
                for (;;)
                {
                    pollfd descriptors[2] { { socket_, POLLIN, 0 }, { wake_[0], POLLIN, 0 } };
                    if (::poll(descriptors, 2, -1) < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        return;
                    }
                    if ((descriptors[1].revents & POLLIN) != 0)
                        return;
                    if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                        return;
                    if ((descriptors[0].revents & POLLIN) == 0)
                        continue;
                    const int connection { ::accept(socket_, nullptr, nullptr) };
                    if (connection < 0)
                        continue;
                    prepare_socket(connection);
                    set_nonblocking(connection);
                    if (serve(connection) == false)
                        return;
                }
            }

            // 보내기 하나를 읽어 메인 queue에 넘긴다. 멈추라는 신호가 오면 거짓이다.
            [[nodiscard]] bool serve(int connection) noexcept
            {
                uid_t user { 0 };
                gid_t group { 0 };
                if (::getpeereid(connection, &user, &group) != 0 || user != ::geteuid())
                {
                    close_file(connection);
                    return true;
                }
                const clock::time_point deadline { clock::now() + read_budget };
                std::array<std::byte, macos::instance_header_bytes> header {};
                const transfer header_read { read_exact(connection, header, deadline, wake_[0]) };
                if (header_read != transfer::done)
                {
                    close_file(connection);
                    return header_read != transfer::woken;
                }
                const macos::instance_header_result head { macos::decode_instance_header(header) };
                if (head.ok() == false)
                {
                    answer(connection, false);
                    close_file(connection);
                    return true;
                }
                try
                {
                    std::vector<std::byte> body(head.body_bytes);
                    const transfer body_read { read_exact(connection, body, deadline, wake_[0]) };
                    if (body_read != transfer::done)
                    {
                        close_file(connection);
                        return body_read != transfer::woken;
                    }
                    macos::instance_decode_result message { macos::decode_instance_body(body) };
                    if (message.ok() == false)
                    {
                        answer(connection, false);
                        close_file(connection);
                        return true;
                    }
                    auto delivery { std::make_unique<pending_delivery>(slot_, connection, std::move(message.value), message.activate) };
                    connection = -1;
                    dispatch_async_f(dispatch_get_main_queue(), delivery.release(), deliver_on_main);
                }
                catch (...)
                {
                    close_file(connection);
                }
                return true;
            }

            std::string path_ {};
            std::shared_ptr<receiver_slot> slot_ {};
            std::array<int, 2> wake_ { -1, -1 };
            int socket_ { -1 };
            bool bound_ { false };
            std::thread thread_ {};
        };

        // 첫째 객체 하나의 몫이다. 이름으로 찾는다 — 한 이름의 첫째는 한 프로세스 안에 하나뿐이다 (잠금이 막는다).
        struct primary_endpoint
        {
            primary_endpoint(const primary_endpoint&) = delete;
            primary_endpoint& operator=(const primary_endpoint&) = delete;

            explicit primary_endpoint(std::string socket) noexcept
                : socket_path { std::move(socket) }
            {
            }

            std::string socket_path {};
            std::mutex mutex {};
            std::shared_ptr<receiver_slot> slot {};
            std::unique_ptr<instance_listener> listener {};
        };

        std::mutex registry_mutex {};

        [[nodiscard]] std::unordered_map<std::u8string, std::weak_ptr<primary_endpoint>>& registry()
        {
            static std::unordered_map<std::u8string, std::weak_ptr<primary_endpoint>> endpoints {};
            return endpoints;
        }

        [[nodiscard]] std::shared_ptr<primary_endpoint> find_endpoint(const single_instance& instance)
        {
            if (instance.primary() == false)
                return nullptr;
            const std::lock_guard lock { registry_mutex };
            const auto found { registry().find(std::u8string { instance.name() }) };
            return found != registry().end() ? found->second.lock() : nullptr;
        }

        // 첫째의 프로세스에 앞 창 자리를 넘긴다 (macOS 14의 협조 활성화). 이 프로세스가 앞 앱이 아니면 OS가 무시한다.
        void yield_activation(const int socket)
        {
            pid_t process { 0 };
            socklen_t length { sizeof(process) };
            if (::getsockopt(socket, SOL_LOCAL, LOCAL_PEERPID, &process, &length) != 0 || process <= 0)
                return;
            if (@available(macOS 14.0, *))
            {
                @autoreleasepool
                {
                    NSRunningApplication* const primary { [NSRunningApplication runningApplicationWithProcessIdentifier:process] };
                    if (NSApp != nil && primary != nil)
                        [NSApp yieldActivationToApplication:primary];
                }
            }
        }
    }

    struct single_instance::state
    {
        state() = default;
        state(const state&) = delete;
        state& operator=(const state&) = delete;

        ~state()
        {
            if (endpoint != nullptr)
            {
                {
                    const std::lock_guard lock { registry_mutex };
                    const auto found { registry().find(name) };
                    if (found != registry().end() && found->second.lock() == endpoint)
                        registry().erase(found);
                }
                endpoint.reset();
            }
            // 쥔 채로 잠금 파일을 지우고 닫는다. 그 사이에 옛 파일을 연 쪽은 잠근 뒤 경로가 다른 파일임을 보고 다시 연다.
            if (lock_file >= 0)
                static_cast<void>(::unlink(lock_path.c_str()));
            close_file(lock_file);
        }

        std::u8string name {};
        // 첫째의 소켓 경로다. 보내는 쪽이 이것으로 찾는다.
        std::string socket_path {};
        std::string lock_path {};
        // 첫째만 쥔다.
        int lock_file { -1 };
        bool primary { false };
        platform_result status {};
        std::shared_ptr<primary_endpoint> endpoint {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        state& self { *state_ };
        self.name = name;
        if (name.empty() || text::utf8_is_valid(name) == false)
        {
            self.status = failure(u8"The instance name is empty or not valid UTF-8.");
            return;
        }
        const std::optional<std::string> folder { user_folder() };
        if (folder.has_value() == false)
        {
            self.status = errno_failure(u8"Failed to find the user's temporary folder");
            return;
        }
        const std::string stem { *folder + file_stem(name) };
        self.socket_path = stem + ".sock";
        self.lock_path = stem + ".lock";
        // 끝나는 첫째는 잠금 파일을 지운다. 잠근 파일이 아직 그 경로의 파일인지 보고, 아니면 새 파일로 다시 한다.
        for (int attempt { 0 };; ++attempt)
        {
            int file { ::open(self.lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR) };
            if (file < 0)
            {
                self.status = errno_failure(u8"Failed to open the instance lock file");
                return;
            }
            if (::flock(file, LOCK_EX | LOCK_NB) != 0)
            {
                // 다른 인스턴스가 먼저 섰다. 첫째가 끝나도 이 객체는 둘째로 남는다 (Windows와 같다).
                const int code { errno };
                close_file(file);
                errno = code;
                self.status = code == EWOULDBLOCK ? success() : errno_failure(u8"Failed to lock the instance file");
                return;
            }
            struct stat locked {};
            struct stat named {};
            if (::fstat(file, &locked) == 0 && ::stat(self.lock_path.c_str(), &named) == 0 && locked.st_dev == named.st_dev && locked.st_ino == named.st_ino)
            {
                self.lock_file = file;
                break;
            }
            close_file(file);
            if (attempt >= 8)
            {
                self.status = failure(u8"The instance lock file kept being replaced.");
                return;
            }
        }
        self.endpoint = std::make_shared<primary_endpoint>(self.socket_path);
        {
            const std::lock_guard lock { registry_mutex };
            registry()[self.name] = self.endpoint;
        }
        self.primary = true;
        self.status = success();
    }

    single_instance::single_instance(single_instance&&) noexcept = default;
    single_instance& single_instance::operator=(single_instance&&) noexcept = default;
    single_instance::~single_instance() = default;

    bool single_instance::primary() const noexcept
    {
        return state_ != nullptr && state_->primary;
    }

    const platform_result& single_instance::status() const noexcept
    {
        // 옮겨 간 뒤의 빈 객체도 답한다.
        static const platform_result moved { platform_status::failed, u8"The instance guard was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    std::u8string_view single_instance::name() const noexcept
    {
        return state_ != nullptr ? std::u8string_view { state_->name } : std::u8string_view {};
    }

    platform_result single_instance::send_to_primary(const std::span<const std::u8string> arguments, const instance_send_options& options) const
    {
        if (state_ == nullptr)
            return failure(u8"The instance guard was moved from.");
        // 서지 못한 객체(빈 이름이나 잠금 오류)는 보낼 곳도 모른다.
        if (state_->status.ok() == false)
            return state_->status;
        if (macos::instance_name_valid(state_->name) == false)
            return failure(u8"The instance name holds a NUL character or is longer than 241 UTF-16 code units.");
        std::array<char, PATH_MAX> directory {};
        if (::getcwd(directory.data(), directory.size()) == nullptr)
            return errno_failure(u8"Failed to read the working directory");
        const macos::instance_encode_result message { macos::encode_instance_message(arguments, macos::widen(directory.data()), options.activate) };
        if (message.ok() == false)
            return failure(std::u8string { macos::instance_message_error_text(message.error) });
        if (state_->primary)
            return not_found();

        const clock::time_point deadline { clock::now() + std::max(options.timeout, std::chrono::milliseconds::zero()) };
        // 다음 시도까지 쉰다. 시한이 지났으면 거짓이다. 첫째가 아직 받는 자리를 열지 않았을 수 있어 50 ms마다 다시 찾는다.
        const auto pause = [&deadline]() {
            const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()) };
            if (remaining <= std::chrono::milliseconds::zero())
                return false;
            std::this_thread::sleep_for(std::min(remaining, retry_pause));
            return true;
        };
        const std::optional<sockaddr_un> address { socket_address(state_->socket_path) };
        if (address.has_value() == false)
            return path_too_long();
        for (;;)
        {
            int connection { ::socket(AF_UNIX, SOCK_STREAM, 0) };
            if (connection < 0)
                return errno_failure(u8"Failed to create the instance socket");
            prepare_socket(connection);
            if (::connect(connection, reinterpret_cast<const sockaddr*>(&*address), address->sun_len) != 0)
            {
                const int code { errno };
                close_file(connection);
                // 받는 자리가 아직 없거나(ENOENT) 죽은 첫째의 소켓(ECONNREFUSED)이다. 새 첫째가 설 수 있어 다시 찾는다.
                if (code == ENOENT || code == ECONNREFUSED || code == EAGAIN || code == EINTR)
                {
                    if (pause() == false)
                        return not_found();
                    continue;
                }
                errno = code;
                return errno_failure(u8"Failed to connect to the first instance");
            }
            set_nonblocking(connection);
            if (options.activate)
                yield_activation(connection);

            // 첫째가 응답하지 않으면 시한까지 기다린다. 그동안 부른 thread는 다른 일을 하지 않는다.
            //  - 다 읽기 전에는 첫째가 답할 수 없으니 쓰기와 답 기다리기는 시한 하나를 나눠 쓴다. 적어도 1 ms는 기다린다.
            const clock::time_point wait_until { std::max(deadline, clock::now() + std::chrono::milliseconds { 1 }) };
            transfer step { write_all(connection, message.bytes, wait_until, -1) };
            std::array<std::byte, 1> reply {};
            if (step == transfer::done)
                step = read_exact(connection, reply, wait_until, -1);
            close_file(connection);
            switch (step)
            {
            case transfer::done:
                return std::to_integer<std::uint8_t>(reply.front()) == macos::instance_answer_accepted ? success() : failure(u8"The first instance did not accept the arguments.");
            case transfer::timed_out:
                return failure(u8"The first instance did not answer in time. It may still receive the arguments later.");
            case transfer::closed:
                // 그 사이 첫째가 받는 자리를 닫았다. 새 첫째가 설 수 있어 다시 찾는다.
                if (pause() == false)
                    return not_found();
                continue;
            case transfer::woken:
            case transfer::failed:
            default:
                return errno_failure(u8"Failed to send the arguments to the first instance");
            }
        }
    }

    platform_result macos::set_instance_receiver(const single_instance& instance, instance_receiver receiver)
    {
        const std::shared_ptr<primary_endpoint> endpoint { find_endpoint(instance) };
        if (endpoint == nullptr)
            return { platform_status::not_found, u8"This instance is not the first one of its name.", std::nullopt };
        const std::lock_guard lock { endpoint->mutex };
        if (endpoint->slot == nullptr)
            endpoint->slot = std::make_shared<receiver_slot>();
        {
            const std::lock_guard slot_lock { endpoint->slot->mutex };
            endpoint->slot->receiver = std::move(receiver);
        }
        if (endpoint->listener != nullptr)
            return success();
        auto listener { std::make_unique<instance_listener>() };
        platform_result started { listener->start(endpoint->socket_path, endpoint->slot) };
        if (started.ok() == false)
            return started;
        endpoint->listener = std::move(listener);
        return success();
    }

    void macos::clear_instance_receiver(const single_instance& instance) noexcept
    {
        try
        {
            const std::shared_ptr<primary_endpoint> endpoint { find_endpoint(instance) };
            if (endpoint == nullptr)
                return;
            const std::lock_guard lock { endpoint->mutex };
            endpoint->listener.reset();
            if (endpoint->slot != nullptr)
            {
                const std::lock_guard slot_lock { endpoint->slot->mutex };
                endpoint->slot->receiver = {};
            }
            // 메인 queue에 남은 건넴은 옛 자리를 쥔다. 새 자리를 세워, 다시 열어도 그것들이 새 함수에 닿지 않는다.
            endpoint->slot.reset();
        }
        catch (...)
        {
        }
    }
}

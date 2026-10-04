#include "luil/net/smb_client.h"

#include "net/smb_pool.h"
#include "net/smb_rules.h"

// libsmb2.h는 smb2.h의 정의(GUID 크기, lease 키)를 먼저 요구한다. 정렬이 순서를 바꾸지 않게 묶음을 나눈다.
#include <smb2/smb2.h>

#include <smb2/libsmb2.h>

#include <fcntl.h>
#include <poll.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// libsmb2 위의 SMB client다 (docs/smb-client-design.md).
//
// 요청마다 worker thread 하나가 연결 하나를 빌려 요청을 하고 돌려준다. 연결(로그온·공유 연결까지 마친 libsmb2 문맥)은
// 장부(`smb_connection_pool`)가 들고, 잠시 쉬게 두었다 같은 열쇠의 요청에 다시 빌려준다. libsmb2의 비동기 API를 우리가
// poll로 돌려 100 ms마다 취소·정지·시간 상한을 본다 — 막히는 동기 API는 끊을 길이 없다. 답은 배달 thread 하나가 차례로
// `deliver`에 넘기고, 그 thread가 쉬는 시간이 지난 연결도 닫는다.
namespace luil::net {
    namespace {
        // poll 한 번이 기다리는 상한이다. 취소·정지가 이만큼 안에 닿는다.
        constexpr int poll_slice_ms { 100 };
        // 한 번에 poll하는 연결 시도의 상한이다 (주소가 여럿인 서버의 동시 연결).
        constexpr std::size_t max_connecting_fds { 8 };
        // 다시 쓰지 않는 연결을 닫기 전에 공유를 놓아 보는 상한이다.
        constexpr std::chrono::milliseconds polite_disconnect { 500 };

        // libsmb2 호출 하나의 결과를 받는 자리다.
        struct call_state
        {
            bool done { false };
            int status { 0 };
            void* data { nullptr };
        };

        void on_done(smb2_context* const context, const int status, void* const command_data, void* const callback_data)
        {
            static_cast<void>(context);
            auto& state { *static_cast<call_state*>(callback_data) };
            state.status = status;
            state.data = command_data;
            state.done = true;
        }

        [[nodiscard]] std::u8string from_c(const char* const text)
        {
            if (text == nullptr)
                return {};
            return std::u8string { reinterpret_cast<const char8_t*>(text) };
        }

        [[nodiscard]] std::string to_c(const std::u8string_view text)
        {
            return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
        }

        [[nodiscard]] std::chrono::system_clock::time_point to_time(const std::uint64_t seconds, const std::uint64_t nanoseconds)
        {
            const auto since_epoch { std::chrono::seconds { static_cast<std::int64_t>(seconds) } + std::chrono::nanoseconds { static_cast<std::int64_t>(nanoseconds) } };
            return std::chrono::system_clock::time_point { std::chrono::duration_cast<std::chrono::system_clock::duration>(since_epoch) };
        }

        [[nodiscard]] smb_entry to_entry(std::u8string name, const smb2_stat_64& status)
        {
            smb_entry entry {};
            entry.name = std::move(name);
            entry.directory = status.smb2_type == SMB2_TYPE_DIRECTORY;
            entry.size = entry.directory ? 0u : status.smb2_size;
            entry.modified = to_time(status.smb2_mtime, status.smb2_mtime_nsec);
            return entry;
        }

        // libsmb2는 만든 문맥을 모두 잠금 없는 전역 목록(서버 쪽이 도는 `active_contexts`)에 넣고 빼고, 만들 때
        // `srandom`의 씨앗 셈도 전역이다. worker들이 문맥을 함께 만들고 거두면 그 목록이 깨지므로 둘을 한 잠금으로
        // 줄 세운다. 거둘 때 부르는 콜백(`on_done`)은 잠금을 잡지 않는다.
        std::mutex context_list_mutex {};

        [[nodiscard]] smb2_context* create_context() noexcept
        {
            const std::lock_guard lock { context_list_mutex };
            return smb2_init_context();
        }

        // 문맥을 거둔다. 남은 호출의 콜백이 `SMB2_STATUS_SHUTDOWN`으로 불리므로, 콜백이 쓰는 자리(`call_state`)가 살아 있는
        // 동안에만 부른다 — 요청을 하던 worker의 틀 안이거나, 남은 호출이 없는 쉬는 연결이다.
        void destroy_context(smb2_context* const context) noexcept
        {
            if (context == nullptr)
                return;
            const std::lock_guard lock { context_list_mutex };
            smb2_destroy_context(context);
        }

        void destroy_all(const std::vector<void*>& handles) noexcept
        {
            for (void* const handle : handles)
                destroy_context(static_cast<smb2_context*>(handle));
        }

        // 걸음 하나가 어떻게 끝났는가다.
        enum class pump_outcome
        {
            // 콜백이 성공으로 왔다.
            done,
            // 서버가 실패로 답했다. 전송은 살아 있을 수 있다.
            refused,
            // libsmb2가 문맥을 더 쓸 수 없다 (소켓이 끊겼다). 연결을 버린다.
            broken,
            // 취소나 정지다.
            aborted,
            timed_out,
        };

        // 쉬던 연결이 아직 살아 있는가. 쉬는 동안 읽을 것이 왔으면 서버가 닫았거나(EOF) 먼저 보낸 알림이다 — libsmb2에
        // 한 번 맡겨, 실패하면 끊긴 것으로 본다.
        [[nodiscard]] bool still_open(smb2_context* const context)
        {
            const t_socket socket { smb2_get_fd(context) };
            if (socket < 0)
                return false;
            pollfd probe { socket, POLLIN, 0 };
            const int ready { poll(&probe, 1, 0) };
            if (ready <= 0)
                return ready == 0 || errno == EINTR;
            if ((probe.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                return false;
            return smb2_service_fd(context, socket, probe.revents) >= 0 && smb2_get_fd(context) >= 0;
        }
    } // namespace

    struct smb_client::engine
    {
        // 받은 요청 하나다. 취소·정지는 `abort`에 갈래를 적고, worker가 poll 사이에 본다.
        struct job
        {
            smb_request request {};
            smb_ticket ticket {};
            std::atomic<smb_error_kind> abort { smb_error_kind::none };
        };

        // worker와 배달 thread가 함께 드는 상태다. worker는 이것을 shared_ptr로 들어, `stop`이 상한에서 돌아온 뒤에도
        // 남은 worker가 안전하게 끝난다.
        struct state
        {
            smb_client_config config {};
            mutable std::mutex mutex {};
            std::condition_variable changed {};
            bool stopping { false };
            std::uint64_t next_ticket { 0 };
            // 받았지만 답의 배달이 끝나지 않은 요청이다.
            std::map<std::uint64_t, std::shared_ptr<job>> jobs {};
            // 배달을 기다리는 답이다.
            std::deque<smb_response> ready {};
            // 아직 도는 worker의 수다.
            std::size_t workers { 0 };
            // 연결의 장부다. `mutex` 안에서만 만진다.
            smb_connection_pool pool {};

            state() = default;
            state(const state&) = delete;
            state& operator=(const state&) = delete;

            // 마지막 worker까지 끝난 뒤다. 장부에 남은 쉬는 연결을 닫는다 (쓰는 중인 연결은 없다).
            ~state()
            {
                std::vector<void*> closed {};
                pool.close_all(closed);
                destroy_all(closed);
            }
        };

        std::shared_ptr<state> shared { std::make_shared<state>() };
        std::thread delivery {};
        bool stopped { false };

        // 배달 thread다. 답을 하나씩 꺼내 잠금 밖에서 넘기고, 넘긴 뒤에야 그 요청을 셈에서 뺀다. 그 사이에 쉬는 시간이 지난
        // 연결을 닫는다 — 다음으로 지날 때까지만 잠든다.
        static void deliver_loop(const std::shared_ptr<state> shared)
        {
            std::unique_lock lock { shared->mutex };
            while (true)
            {
                std::vector<void*> expired {};
                const std::optional<std::chrono::steady_clock::time_point> next_expiry { shared->pool.expire(std::chrono::steady_clock::now(), expired) };
                if (expired.empty() == false)
                {
                    lock.unlock();
                    destroy_all(expired);
                    lock.lock();
                    continue;
                }
                const auto wake = [&shared] { return shared->stopping || shared->ready.empty() == false; };
                if (next_expiry.has_value())
                    static_cast<void>(shared->changed.wait_until(lock, *next_expiry, wake));
                else
                    shared->changed.wait(lock, wake);
                if (shared->stopping)
                    return;
                if (shared->ready.empty())
                    continue;
                smb_response response { std::move(shared->ready.front()) };
                shared->ready.pop_front();
                const std::uint64_t ticket { response.ticket.value };
                lock.unlock();
                if (shared->config.deliver)
                {
                    try
                    {
                        shared->config.deliver(std::move(response));
                    }
                    catch (...)
                    {}
                }
                lock.lock();
                shared->jobs.erase(ticket);
                shared->changed.notify_all();
            }
        }

        // 걸음 하나가 끝날 때까지 libsmb2를 돌린다. `done`이 아니면 `failure`가 이유다.
        [[nodiscard]] static pump_outcome pump(
            smb2_context* const context, call_state& call, const job& work, const std::optional<std::chrono::steady_clock::time_point> deadline, const bool connecting, smb_error& failure)
        {
            auto last_attempt { std::chrono::steady_clock::now() };
            while (call.done == false)
            {
                if (const smb_error_kind abort { work.abort.load() }; abort != smb_error_kind::none)
                {
                    failure.kind = abort;
                    failure.message = abort == smb_error_kind::cancelled ? u8"The request was cancelled." : u8"The client stopped.";
                    return pump_outcome::aborted;
                }
                const auto now { std::chrono::steady_clock::now() };
                if (deadline.has_value() && now >= *deadline)
                {
                    failure.kind = smb_error_kind::timed_out;
                    failure.message = u8"The request exceeded its timeout.";
                    return pump_outcome::timed_out;
                }

                std::size_t count { 0 };
                int connect_timeout { -1 };
                const t_socket* const sockets { smb2_get_fds(context, &count, &connect_timeout) };
                count = std::min(count, max_connecting_fds);
                pollfd polls[max_connecting_fds] {};
                const short events { static_cast<short>(smb2_which_events(context)) };
                for (std::size_t index { 0 }; index < count; ++index)
                    polls[index] = pollfd { sockets[index], events, 0 };

                int wait { poll_slice_ms };
                if (deadline.has_value())
                    wait = std::min<int>(wait, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count()) + 1);
                if (connect_timeout >= 0)
                {
                    const auto since { std::chrono::duration_cast<std::chrono::milliseconds>(now - last_attempt).count() };
                    wait = std::min<int>(wait, std::max<int>(0, connect_timeout - static_cast<int>(since)));
                }
                wait = std::max(wait, 0);

                if (count == 0)
                {
                    // 기다릴 소켓이 없다 (다음 주소로 넘어가는 중이거나 끊겼다). libsmb2에 한 번 맡긴다.
                    if (smb2_service(context, 0) < 0)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds { std::max(wait, 1) });
                    continue;
                }
                const int ready { poll(polls, static_cast<nfds_t>(count), wait) };
                if (ready < 0)
                {
                    if (errno == EINTR)
                        continue;
                    failure.kind = smb_error_kind::failed;
                    failure.native_error = -errno;
                    failure.message = u8"poll failed.";
                    return pump_outcome::broken;
                }
                if (ready == 0)
                {
                    // 주소가 여럿인 서버에서 한 주소의 연결이 늦으면 다음 주소를 함께 시도한다.
                    if (connect_timeout >= 0 && std::chrono::steady_clock::now() - last_attempt >= std::chrono::milliseconds { connect_timeout })
                    {
                        last_attempt = std::chrono::steady_clock::now();
                        if (smb2_service_fd(context, -1, 0) < 0)
                            break;
                    }
                    continue;
                }
                // 소켓 하나만 처리한다. 처리가 다른 연결 시도의 소켓을 닫을 수 있어 목록은 다시 받는다.
                bool failed { false };
                for (std::size_t index { 0 }; index < count; ++index)
                    if (polls[index].revents != 0)
                    {
                        failed = smb2_service_fd(context, polls[index].fd, polls[index].revents) < 0;
                        break;
                    }
                if (failed)
                    break;
            }
            if (call.done && call.status >= 0)
                return pump_outcome::done;
            // 콜백이 실패를 알렸거나 libsmb2가 문맥을 더 쓸 수 없다고 했다.
            const int status { call.done ? call.status : -ECONNRESET };
            const auto nt_status { static_cast<std::uint32_t>(smb2_get_nterror(context)) };
            failure.kind = smb_error_kind_for(status, nt_status, connecting);
            failure.native_error = status;
            failure.message = from_c(smb2_get_error(context));
            if (failure.message.empty())
                failure.message = u8"The SMB request failed.";
            return call.done ? pump_outcome::refused : pump_outcome::broken;
        }

        // 걸음이 실패했을 때 연결이 끊겨 있었다고 보는가 (다시 쓴 연결이면 한 번 다시 한다).
        [[nodiscard]] static bool lost(const pump_outcome outcome, smb2_context* const context, const smb_error& failure) noexcept
        {
            if (outcome == pump_outcome::broken)
                return true;
            return outcome == pump_outcome::refused && smb_connection_lost(failure.native_error, static_cast<std::uint32_t>(smb2_get_nterror(context)));
        }

        // 걸음 하나의 시작 실패다 (libsmb2가 호출을 받지 않았다).
        static void refuse_start(smb2_context* const context, const int started, const bool connecting, smb_error& failure)
        {
            failure = smb_error { smb_error_kind_for(started, 0, connecting), from_c(smb2_get_error(context)), started };
        }

        // 연결을 연다. 실패하면 nullptr이고 `failure`가 이유다 — 연 문맥은 이 틀 안에서 거둔다.
        [[nodiscard]] static smb2_context* open(
            const smb_location& location, const std::u8string_view share, const job& work, const std::optional<std::chrono::steady_clock::time_point> deadline, call_state& call, smb_error& failure)
        {
            smb2_context* const context { create_context() };
            if (context == nullptr)
            {
                failure = smb_error { smb_error_kind::failed, u8"Could not create an SMB context.", -ENOMEM };
                return nullptr;
            }
            // 서명은 서버가 요구할 때 따르고, 암호화는 앱이 켰을 때만 요구한다.
            smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED);
            smb2_set_seal(context, location.encrypt ? 1 : 0);
            const std::string domain { to_c(location.domain) };
            std::string password { to_c(location.password) };
            if (domain.empty() == false)
                smb2_set_domain(context, domain.c_str());
            if (password.empty() == false)
                smb2_set_password(context, password.c_str());
            // libsmb2가 제 사본을 가졌다. 이 사본은 지운다.
            std::fill(password.begin(), password.end(), '\0');

            const std::string server { to_c(location.server) };
            const std::string share_name { to_c(share) };
            const std::string user { location.user.empty() ? std::string { "guest" } : to_c(location.user) };
            call = {};
            if (const int started { smb2_connect_share_async(context, server.c_str(), share_name.c_str(), user.c_str(), on_done, &call) }; started != 0)
            {
                refuse_start(context, started, true, failure);
                destroy_context(context);
                return nullptr;
            }
            if (pump(context, call, work, deadline, true, failure) != pump_outcome::done)
            {
                destroy_context(context);
                call = {};
                return nullptr;
            }
            return context;
        }

        // 걸음 하나의 결과다.
        struct attempt
        {
            pump_outcome outcome { pump_outcome::done };
            // 연결을 쉬게 두어도 되는가 (깨끗하게 끝났다).
            bool reusable { false };
        };

        // 연결 위에서 요청 하나를 한다. 답의 내용과 오류를 `response`에 채운다.
        [[nodiscard]] static attempt run(
            smb2_context* const context, const job& work, const std::optional<std::chrono::steady_clock::time_point> deadline, call_state& call, smb2_stat_64& status, smb_response& response)
        {
            const smb_request& request { work.request };
            const std::string path { smb_share_path(request.path) };
            // 서버가 요청에 답한 실패(없음·거부)면 연결은 그대로 쓸 수 있다.
            const auto answered = [&response](const pump_outcome outcome) {
                return attempt { outcome, outcome == pump_outcome::refused && (response.error.kind == smb_error_kind::not_found || response.error.kind == smb_error_kind::access_denied) };
            };
            switch (request.operation)
            {
            case smb_operation::list: {
                call = {};
                if (const int started { smb2_opendir_async(context, path.c_str(), on_done, &call) }; started != 0)
                {
                    refuse_start(context, started, false, response.error);
                    return attempt { pump_outcome::refused, false };
                }
                if (const pump_outcome outcome { pump(context, call, work, deadline, false, response.error) }; outcome != pump_outcome::done)
                    return answered(outcome);
                auto* const directory { static_cast<smb2dir*>(call.data) };
                while (const smb2dirent* const item { smb2_readdir(context, directory) })
                {
                    const std::u8string name { from_c(item->name) };
                    if (name == u8"." || name == u8"..")
                        continue;
                    response.entries.push_back(to_entry(name, item->st));
                }
                smb2_closedir(context, directory);
                return attempt { pump_outcome::done, true };
            }
            case smb_operation::stat: {
                call = {};
                if (const int started { smb2_stat_async(context, path.c_str(), &status, on_done, &call) }; started != 0)
                {
                    refuse_start(context, started, false, response.error);
                    return attempt { pump_outcome::refused, false };
                }
                if (const pump_outcome outcome { pump(context, call, work, deadline, false, response.error) }; outcome != pump_outcome::done)
                    return answered(outcome);
                response.entry = to_entry(smb_last_component(request.path), status);
                return attempt { pump_outcome::done, true };
            }
            case smb_operation::read: {
                call = {};
                if (const int started { smb2_open_async(context, path.c_str(), O_RDONLY, on_done, &call) }; started != 0)
                {
                    refuse_start(context, started, false, response.error);
                    return attempt { pump_outcome::refused, false };
                }
                if (const pump_outcome outcome { pump(context, call, work, deadline, false, response.error) }; outcome != pump_outcome::done)
                    return answered(outcome);
                auto* const file { static_cast<smb2fh*>(call.data) };
                response.data.resize(request.length);
                std::size_t received { 0 };
                const std::size_t chunk_limit { std::max<std::size_t>(smb2_get_max_read_size(context), 4096u) };
                pump_outcome outcome { pump_outcome::done };
                while (received < request.length)
                {
                    const std::size_t chunk { std::min(request.length - received, chunk_limit) };
                    call = {};
                    if (const int started { smb2_pread_async(context, file, response.data.data() + received, static_cast<uint32_t>(chunk), request.offset + received, on_done, &call) }; started != 0)
                    {
                        refuse_start(context, started, false, response.error);
                        outcome = pump_outcome::refused;
                        break;
                    }
                    outcome = pump(context, call, work, deadline, false, response.error);
                    if (outcome != pump_outcome::done)
                        break;
                    // 0은 파일 끝이다.
                    if (call.status == 0)
                        break;
                    received += static_cast<std::size_t>(call.status);
                }
                response.data.resize(received);
                if (outcome != pump_outcome::done)
                {
                    // 읽기가 끊겼다. 연 파일은 연결을 버리며 서버가 닫는다.
                    response.data.clear();
                    return attempt { outcome, false };
                }
                // 닫기는 답을 바꾸지 않는다. 닫지 못했으면 연결의 상태를 알 수 없어 다시 쓰지 않는다.
                call = {};
                smb_error ignored {};
                const bool closed { smb2_close_async(context, file, on_done, &call) == 0 && pump(context, call, work, deadline, false, ignored) == pump_outcome::done };
                return attempt { pump_outcome::done, closed };
            }
            }
            response.error = smb_error { smb_error_kind::invalid_request, u8"The operation is not known.", 0 };
            return attempt { pump_outcome::refused, true };
        }

        // 연결을 빌린다. 서버의 상한이 차 모두 쓰는 중이면 하나가 돌아올 때까지 기다린다. 취소·정지·시간 상한이면 nullopt다.
        [[nodiscard]] static std::optional<smb_lease> borrow(
            state& shared, const smb_connection_key& key, const bool fresh, const job& work, const std::optional<std::chrono::steady_clock::time_point> deadline, smb_error& failure)
        {
            std::vector<void*> evicted {};
            std::optional<smb_lease> lease {};
            {
                std::unique_lock lock { shared.mutex };
                while (true)
                {
                    if (const smb_error_kind abort { work.abort.load() }; abort != smb_error_kind::none || shared.pool.closed())
                    {
                        failure.kind = abort != smb_error_kind::none ? abort : smb_error_kind::stopped;
                        failure.message = failure.kind == smb_error_kind::cancelled ? u8"The request was cancelled." : u8"The client stopped.";
                        return std::nullopt;
                    }
                    const auto now { std::chrono::steady_clock::now() };
                    if (deadline.has_value() && now >= *deadline)
                    {
                        failure.kind = smb_error_kind::timed_out;
                        failure.message = u8"The request exceeded its timeout while waiting for a connection.";
                        return std::nullopt;
                    }
                    lease = shared.pool.acquire(key, now, fresh, evicted);
                    if (lease.has_value())
                        break;
                    // 그 서버의 연결이 모두 쓰는 중이다. 돌아오면 깨우고, 그렇지 않아도 취소를 보러 깬다.
                    static_cast<void>(shared.changed.wait_for(lock, std::chrono::milliseconds { poll_slice_ms }));
                }
            }
            // 비운 연결은 쉬던 것이라 남은 호출이 없다.
            destroy_all(evicted);
            return lease;
        }

        // 빌린 연결을 돌려준다. 다시 쓰지 않을 깨끗한 연결은 공유를 놓아 보고 닫는다. 그 밖에는 바로 닫는다 — 이 틀 안이라
        // 남은 호출의 콜백이 `call`에 닿아도 된다.
        static void give_back(state& shared, const smb_lease& lease, smb2_context* const context, const bool keep, const job& work, call_state& call)
        {
            std::vector<void*> closed {};
            {
                const std::lock_guard lock { shared.mutex };
                shared.pool.attach(lease.id, context);
                shared.pool.release(lease.id, keep, std::chrono::steady_clock::now(), closed);
                shared.changed.notify_all();
            }
            for (void* const handle : closed)
            {
                auto* const closing { static_cast<smb2_context*>(handle) };
                if (keep && work.abort.load() == smb_error_kind::none)
                {
                    call = {};
                    smb_error ignored {};
                    if (smb2_disconnect_share_async(closing, on_done, &call) == 0)
                        static_cast<void>(pump(closing, call, work, std::chrono::steady_clock::now() + polite_disconnect, false, ignored));
                }
                destroy_context(closing);
                call = {};
            }
        }

        // 요청 하나를 처음부터 끝까지 몬다.
        static void perform(state& shared, const job& work, smb_response& response)
        {
            const smb_request& request { work.request };
            const std::optional<std::chrono::steady_clock::time_point> deadline { request.timeout.count() > 0 ? std::optional { std::chrono::steady_clock::now() + request.timeout } : std::nullopt };
            const smb_connection_key key { make_smb_connection_key(request.location, request.location.share) };
            // 콜백이 쓰는 자리는 어느 문맥보다 먼저 세운다. 문맥을 거두면 libsmb2가 남은 호출의 콜백을
            // `SMB2_STATUS_SHUTDOWN`으로 부르므로, 문맥을 거두는 모든 자리(이 함수와 그 아래)보다 오래 산다.
            call_state call {};
            smb2_stat_64 status {};
            bool reopened { false };
            // 다시 쓴 연결이 끊겨 있었으면 새 연결로 한 번만 다시 한다.
            for (int round { 0 }; round < 2; ++round)
            {
                const std::optional<smb_lease> lease { borrow(shared, key, reopened, work, deadline, response.error) };
                if (lease.has_value() == false)
                    return;
                auto* context { static_cast<smb2_context*>(lease->handle) };
                bool reused { lease->reused };
                if (reused && still_open(context) == false)
                {
                    // 쉬는 동안 끊겼다 (서버가 닫았다). 같은 자리에 새로 연다.
                    destroy_context(context);
                    context = nullptr;
                    reused = false;
                    reopened = true;
                }
                if (context == nullptr)
                {
                    context = open(request.location, request.location.share, work, deadline, call, response.error);
                    response.connection = reopened ? smb_connection_use::reopened : smb_connection_use::opened;
                    if (context == nullptr)
                    {
                        give_back(shared, *lease, nullptr, false, work, call);
                        return;
                    }
                }
                else
                    response.connection = smb_connection_use::reused;

                const attempt result { run(context, work, deadline, call, status, response) };
                // 취소·정지·시간 상한은 다시 하지 않는다. 서버가 답했거나 전송이 끊긴 실패만 본다.
                const bool failed { result.outcome == pump_outcome::refused || result.outcome == pump_outcome::broken };
                const bool retry { reused && failed && lost(result.outcome, context, response.error) };
                give_back(shared, *lease, context, result.reusable && retry == false, work, call);
                if (retry == false)
                    return;
                // 쓰던 답을 비우고 새 연결로 다시 한다.
                response.error = {};
                response.entries.clear();
                response.data.clear();
                response.entry = {};
                reopened = true;
            }
        }

        // worker thread다.
        static void run_job(const std::shared_ptr<state> shared, const std::shared_ptr<job> job_ptr)
        {
            smb_response response {};
            response.ticket = job_ptr->ticket;
            response.operation = job_ptr->request.operation;
            const auto started { std::chrono::steady_clock::now() };
            try
            {
                perform(*shared, *job_ptr, response);
            }
            catch (const std::bad_alloc&)
            {
                response = {};
                response.ticket = job_ptr->ticket;
                response.operation = job_ptr->request.operation;
                response.error = smb_error { smb_error_kind::failed, u8"Out of memory.", -ENOMEM };
            }
            response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);

            const std::lock_guard lock { shared->mutex };
            --shared->workers;
            if (shared->stopping)
                shared->jobs.erase(job_ptr->ticket.value);
            else
                shared->ready.push_back(std::move(response));
            shared->changed.notify_all();
        }
    };

    std::unique_ptr<smb_client> smb_client::create(smb_client_config config, std::u8string& error)
    {
        if (config.max_in_flight == 0 || config.max_read_bytes == 0 || config.max_connections_per_server == 0)
        {
            error = u8"max_in_flight, max_read_bytes and max_connections_per_server must be positive.";
            return nullptr;
        }
        if (config.idle_timeout.count() < 0)
        {
            error = u8"idle_timeout must not be negative.";
            return nullptr;
        }
        auto implementation { std::make_unique<engine>() };
        implementation->shared->pool = smb_connection_pool { smb_pool_config { config.idle_timeout, config.max_connections_per_server } };
        implementation->shared->config = std::move(config);
        try
        {
            implementation->delivery = std::thread { &engine::deliver_loop, implementation->shared };
        }
        catch (const std::system_error&)
        {
            error = u8"Could not start the SMB delivery thread.";
            return nullptr;
        }
        return std::unique_ptr<smb_client> { new smb_client { std::move(implementation) } };
    }

    bool smb_client::supported() noexcept
    {
        return true;
    }

    smb_client::smb_client(std::unique_ptr<engine> implementation) noexcept
        : engine_ { std::move(implementation) }
    {}

    smb_client::~smb_client()
    {
        stop();
    }

    smb_ticket smb_client::send(smb_request request, std::u8string& error)
    {
        engine::state& shared { *engine_->shared };
        smb_error failure {};
        if (check_smb_request(request, shared.config.max_read_bytes, failure) == false)
        {
            error = failure.message;
            return {};
        }
        auto work { std::make_shared<engine::job>() };
        work->request = std::move(request);
        {
            const std::lock_guard lock { shared.mutex };
            if (shared.stopping)
            {
                error = u8"client stopped";
                return {};
            }
            if (shared.jobs.size() >= shared.config.max_in_flight)
            {
                error = u8"Too many SMB requests in flight.";
                return {};
            }
            work->ticket = smb_ticket { ++shared.next_ticket };
            shared.jobs.emplace(work->ticket.value, work);
            ++shared.workers;
        }
        try
        {
            std::thread { &engine::run_job, engine_->shared, work }.detach();
        }
        catch (const std::system_error&)
        {
            const std::lock_guard lock { shared.mutex };
            shared.jobs.erase(work->ticket.value);
            --shared.workers;
            error = u8"Could not start an SMB worker thread.";
            return {};
        }
        return work->ticket;
    }

    void smb_client::cancel(const smb_ticket ticket) noexcept
    {
        engine::state& shared { *engine_->shared };
        const std::lock_guard lock { shared.mutex };
        if (const auto found { shared.jobs.find(ticket.value) }; found != shared.jobs.end())
        {
            smb_error_kind expected { smb_error_kind::none };
            static_cast<void>(found->second->abort.compare_exchange_strong(expected, smb_error_kind::cancelled));
        }
    }

    void smb_client::cancel_all() noexcept
    {
        engine::state& shared { *engine_->shared };
        const std::lock_guard lock { shared.mutex };
        for (auto& [ticket, work] : shared.jobs)
        {
            smb_error_kind expected { smb_error_kind::none };
            static_cast<void>(work->abort.compare_exchange_strong(expected, smb_error_kind::cancelled));
        }
    }

    void smb_client::stop() noexcept
    {
        if (engine_ == nullptr || engine_->stopped)
            return;
        engine_->stopped = true;
        engine::state& shared { *engine_->shared };
        {
            const std::lock_guard lock { shared.mutex };
            shared.stopping = true;
            for (auto& [ticket, work] : shared.jobs)
                work->abort.store(smb_error_kind::stopped);
            shared.ready.clear();
            shared.changed.notify_all();
        }
        if (engine_->delivery.joinable())
            engine_->delivery.join();
        // worker는 poll 한 번(100 ms) 안에 끊김을 본다. 상한을 넘기면 기다리지 않는다 — 남은 worker는 상태를 쥐고
        // 있어 안전하게 끝나고, 그 답은 배달하지 않고 그 연결은 닫는다 (닫힌 장부).
        std::vector<void*> closed {};
        {
            std::unique_lock lock { shared.mutex };
            static_cast<void>(shared.changed.wait_for(lock, shared.config.stop_budget, [&shared] { return shared.workers == 0; }));
            shared.pool.close_all(closed);
        }
        destroy_all(closed);
    }

    std::size_t smb_client::in_flight() const noexcept
    {
        const engine::state& shared { *engine_->shared };
        const std::lock_guard lock { shared.mutex };
        return shared.jobs.size();
    }

    void smb_client::close_idle() noexcept
    {
        engine::state& shared { *engine_->shared };
        std::vector<void*> closed {};
        {
            const std::lock_guard lock { shared.mutex };
            shared.pool.close_idle(closed);
            shared.changed.notify_all();
        }
        destroy_all(closed);
    }
} // namespace luil::net

#include "luil/net/smb_client.h"

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
// 요청마다 worker thread 하나가 libsmb2 문맥 하나를 열고 닫는다. libsmb2의 비동기 API를 우리가 poll로 돌려,
// 100 ms마다 취소·정지·시간 상한을 본다 — 막히는 동기 API는 끊을 길이 없다. 답은 배달 thread 하나가 차례로
// `deliver`에 넘긴다.
namespace luil::net {
    namespace {
        // poll 한 번이 기다리는 상한이다. 취소·정지가 이만큼 안에 닿는다.
        constexpr int poll_slice_ms { 100 };
        // 한 번에 poll하는 연결 시도의 상한이다 (주소가 여럿인 서버의 동시 연결).
        constexpr std::size_t max_connecting_fds { 8 };

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

        // 문맥 하나를 쥐고 끝에 거둔다.
        class context_guard
        {
        public:
            context_guard() noexcept
                : context_ { smb2_init_context() }
            {}

            context_guard(const context_guard&) = delete;
            context_guard(context_guard&&) = delete;
            context_guard& operator=(const context_guard&) = delete;
            context_guard& operator=(context_guard&&) = delete;

            ~context_guard()
            {
                if (context_ != nullptr)
                    smb2_destroy_context(context_);
            }

            [[nodiscard]] smb2_context* get() const noexcept
            {
                return context_;
            }

        private:
            smb2_context* context_ { nullptr };
        };
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
        };

        std::shared_ptr<state> shared { std::make_shared<state>() };
        std::thread delivery {};
        bool stopped { false };

        // 배달 thread다. 답을 하나씩 꺼내 잠금 밖에서 넘기고, 넘긴 뒤에야 그 요청을 셈에서 뺀다.
        static void deliver_loop(const std::shared_ptr<state> shared)
        {
            std::unique_lock lock { shared->mutex };
            while (true)
            {
                shared->changed.wait(lock, [&shared] { return shared->stopping || shared->ready.empty() == false; });
                if (shared->stopping)
                    return;
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

        // 걸음 하나가 끝날 때까지 libsmb2를 돌린다. 끊겼거나 실패했으면 거짓이고 `failure`가 이유다.
        [[nodiscard]] static bool pump(
            smb2_context* const context, call_state& call, const job& work, const std::optional<std::chrono::steady_clock::time_point> deadline, const bool connecting, smb_error& failure)
        {
            auto last_attempt { std::chrono::steady_clock::now() };
            while (call.done == false)
            {
                if (const smb_error_kind abort { work.abort.load() }; abort != smb_error_kind::none)
                {
                    failure.kind = abort;
                    failure.message = abort == smb_error_kind::cancelled ? u8"The request was cancelled." : u8"The client stopped.";
                    return false;
                }
                const auto now { std::chrono::steady_clock::now() };
                if (deadline.has_value() && now >= *deadline)
                {
                    failure.kind = smb_error_kind::timed_out;
                    failure.message = u8"The request exceeded its timeout.";
                    return false;
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
                    // 기다릴 소켓이 없다 (다음 주소로 넘어가는 중 등). libsmb2에 한 번 맡긴다.
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
                    return false;
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
                return true;
            // 콜백이 실패를 알렸거나 libsmb2가 문맥을 더 쓸 수 없다고 했다.
            const int status { call.done ? call.status : -ECONNRESET };
            const auto nt_status { static_cast<std::uint32_t>(smb2_get_nterror(context)) };
            failure.kind = smb_error_kind_for(status, nt_status, connecting);
            failure.native_error = status;
            failure.message = from_c(smb2_get_error(context));
            if (failure.message.empty())
                failure.message = u8"The SMB request failed.";
            return false;
        }

        // 요청 하나를 처음부터 끝까지 몬다.
        static void perform(const job& work, smb_response& response)
        {
            const smb_request& request { work.request };
            const std::optional<std::chrono::steady_clock::time_point> deadline { request.timeout.count() > 0 ? std::optional { std::chrono::steady_clock::now() + request.timeout } : std::nullopt };
            // 콜백이 쓰는 자리는 문맥보다 먼저 세운다. 문맥을 거두면 libsmb2가 남은 호출의 콜백을
            // `SMB2_STATUS_SHUTDOWN`으로 부르므로, 그때까지 살아 있어야 한다.
            call_state call {};
            smb2_stat_64 status {};
            context_guard guard {};
            smb2_context* const context { guard.get() };
            if (context == nullptr)
            {
                response.error = smb_error { smb_error_kind::failed, u8"Could not create an SMB context.", -ENOMEM };
                return;
            }

            // 서명은 서버가 요구할 때 따르고, 암호화는 앱이 켰을 때만 요구한다.
            smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED);
            smb2_set_seal(context, request.location.encrypt ? 1 : 0);
            const std::string domain { to_c(request.location.domain) };
            const std::string password { to_c(request.location.password) };
            if (domain.empty() == false)
                smb2_set_domain(context, domain.c_str());
            if (password.empty() == false)
                smb2_set_password(context, password.c_str());

            const std::string server { to_c(request.location.server) };
            const std::string share { to_c(request.location.share) };
            const std::string user { request.location.user.empty() ? std::string { "guest" } : to_c(request.location.user) };
            if (const int started { smb2_connect_share_async(context, server.c_str(), share.c_str(), user.c_str(), on_done, &call) }; started != 0)
            {
                response.error = smb_error { smb_error_kind_for(started, 0, true), from_c(smb2_get_error(context)), started };
                return;
            }
            if (pump(context, call, work, deadline, true, response.error) == false)
                return;

            const std::string path { smb_share_path(request.path) };
            switch (request.operation)
            {
            case smb_operation::list: {
                call = {};
                if (const int started { smb2_opendir_async(context, path.c_str(), on_done, &call) }; started != 0)
                {
                    response.error = smb_error { smb_error_kind_for(started, 0, false), from_c(smb2_get_error(context)), started };
                    return;
                }
                if (pump(context, call, work, deadline, false, response.error) == false)
                    return;
                auto* const directory { static_cast<smb2dir*>(call.data) };
                while (const smb2dirent* const item { smb2_readdir(context, directory) })
                {
                    const std::u8string name { from_c(item->name) };
                    if (name == u8"." || name == u8"..")
                        continue;
                    response.entries.push_back(to_entry(name, item->st));
                }
                smb2_closedir(context, directory);
                break;
            }
            case smb_operation::stat: {
                call = {};
                if (const int started { smb2_stat_async(context, path.c_str(), &status, on_done, &call) }; started != 0)
                {
                    response.error = smb_error { smb_error_kind_for(started, 0, false), from_c(smb2_get_error(context)), started };
                    return;
                }
                if (pump(context, call, work, deadline, false, response.error) == false)
                    return;
                response.entry = to_entry(smb_last_component(request.path), status);
                break;
            }
            case smb_operation::read: {
                call = {};
                if (const int started { smb2_open_async(context, path.c_str(), O_RDONLY, on_done, &call) }; started != 0)
                {
                    response.error = smb_error { smb_error_kind_for(started, 0, false), from_c(smb2_get_error(context)), started };
                    return;
                }
                if (pump(context, call, work, deadline, false, response.error) == false)
                    return;
                auto* const file { static_cast<smb2fh*>(call.data) };
                response.data.resize(request.length);
                std::size_t received { 0 };
                const std::size_t chunk_limit { std::max<std::size_t>(smb2_get_max_read_size(context), 4096u) };
                while (received < request.length)
                {
                    const std::size_t chunk { std::min(request.length - received, chunk_limit) };
                    call = {};
                    if (const int started { smb2_pread_async(context, file, response.data.data() + received, static_cast<uint32_t>(chunk), request.offset + received, on_done, &call) }; started != 0)
                    {
                        response.error = smb_error { smb_error_kind_for(started, 0, false), from_c(smb2_get_error(context)), started };
                        break;
                    }
                    if (pump(context, call, work, deadline, false, response.error) == false)
                        break;
                    // 0은 파일 끝이다.
                    if (call.status == 0)
                        break;
                    received += static_cast<std::size_t>(call.status);
                }
                response.data.resize(received);
                // 닫기는 답을 바꾸지 않는다. 실패해도 문맥을 거두면 서버가 닫는다.
                call = {};
                smb_error ignored {};
                if (smb2_close_async(context, file, on_done, &call) == 0)
                    static_cast<void>(pump(context, call, work, deadline, false, ignored));
                if (response.error.empty() == false)
                    response.data.clear();
                break;
            }
            }

            // 예의 바르게 공유를 놓는다. 끊긴 요청이나 실패한 요청은 문맥을 거두는 것으로 끝낸다.
            if (response.error.empty() && work.abort.load() == smb_error_kind::none)
            {
                call = {};
                smb_error ignored {};
                const auto quick { std::chrono::steady_clock::now() + std::chrono::milliseconds { 500 } };
                if (smb2_disconnect_share_async(context, on_done, &call) == 0)
                    static_cast<void>(pump(context, call, work, deadline.has_value() ? std::min(*deadline, quick) : quick, false, ignored));
            }
        }

        // worker thread다.
        static void run(const std::shared_ptr<state> shared, const std::shared_ptr<job> job_ptr)
        {
            smb_response response {};
            response.ticket = job_ptr->ticket;
            response.operation = job_ptr->request.operation;
            const auto started { std::chrono::steady_clock::now() };
            try
            {
                perform(*job_ptr, response);
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
        if (config.max_in_flight == 0 || config.max_read_bytes == 0)
        {
            error = u8"max_in_flight and max_read_bytes must be positive.";
            return nullptr;
        }
        auto implementation { std::make_unique<engine>() };
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
            std::thread { &engine::run, engine_->shared, work }.detach();
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
        // 있어 안전하게 끝나고, 그 답은 배달하지 않는다.
        std::unique_lock lock { shared.mutex };
        static_cast<void>(shared.changed.wait_for(lock, shared.config.stop_budget, [&shared] { return shared.workers == 0; }));
    }

    std::size_t smb_client::in_flight() const noexcept
    {
        const engine::state& shared { *engine_->shared };
        const std::lock_guard lock { shared.mutex };
        return shared.jobs.size();
    }
} // namespace luil::net

#include "luil/net/smb_client.h"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

// libsmb2 위의 SMB client를 기기에서 돌린다. 서버 없이 볼 수 있는 것과, 시험 서버(`smb_test_server.cpp`)와의
// 왕복을 본다. 실제 NAS와의 왕복은 사람 손으로 확인한다 (docs/smb-client-design.md).
namespace {
    using namespace std::chrono_literals;
    using luil::net::smb_client;
    using luil::net::smb_error_kind;
    using luil::net::smb_request;
    using luil::net::smb_response;

    class collector
    {
    public:
        void add(smb_response response)
        {
            const std::lock_guard lock { mutex_ };
            responses_.push_back(std::move(response));
            arrived_.notify_all();
        }

        [[nodiscard]] bool wait_for(const std::size_t count, const std::chrono::milliseconds limit)
        {
            std::unique_lock lock { mutex_ };
            return arrived_.wait_for(lock, limit, [this, count] { return responses_.size() >= count; });
        }

        [[nodiscard]] std::vector<smb_response> snapshot() const
        {
            const std::lock_guard lock { mutex_ };
            return responses_;
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable arrived_ {};
        std::vector<smb_response> responses_ {};
    };

    [[nodiscard]] std::unique_ptr<smb_client> make_client(collector& sink, const std::size_t max_in_flight = 4, const std::function<void(luil::net::smb_client_config&)>& adjust = {})
    {
        luil::net::smb_client_config config {};
        config.deliver = [&sink](smb_response response) { sink.add(std::move(response)); };
        config.max_in_flight = max_in_flight;
        if (adjust)
            adjust(config);
        std::u8string error {};
        auto client { smb_client::create(std::move(config), error) };
        REQUIRE(client != nullptr);
        return client;
    }

    // 연결은 받지만(커널의 대기열) 한 바이트도 답하지 않는 서버다. 협상 응답을 영영 기다리게 한다.
    class silent_server
    {
    public:
        silent_server()
        {
            socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length { sizeof(address) };
            if (socket_ >= 0 && ::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && ::listen(socket_, 8) == 0
                && ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0)
                port_ = ntohs(address.sin_port);
        }

        silent_server(const silent_server&) = delete;
        silent_server& operator=(const silent_server&) = delete;

        ~silent_server()
        {
            if (socket_ >= 0)
                ::close(socket_);
        }

        [[nodiscard]] std::u8string server() const
        {
            const std::string text { "127.0.0.1:" + std::to_string(port_) };
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] std::uint16_t port() const noexcept
        {
            return port_;
        }

    private:
        int socket_ { -1 };
        std::uint16_t port_ { 0 };
    };

    // 방금 닫은 포트다. 연결하면 거절된다.
    [[nodiscard]] std::u8string closed_server()
    {
        std::uint16_t port { 0 };
        {
            const silent_server temporary {};
            port = temporary.port();
        }
        const std::string text { "127.0.0.1:" + std::to_string(port) };
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    [[nodiscard]] std::u8string loopback(const std::uint16_t port)
    {
        const std::string text { "127.0.0.1:" + std::to_string(port) };
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    // 실행 파일 옆의 시험 서버를 따로 된 프로세스로 띄운다. thread가 여럿인 프로세스라 자식은 fork 바로 뒤에 exec만
    // 한다 (`posix_spawn`은 API 28부터라 test의 최소 판에 없다). 포트는 비어 있는 것을 하나 골라 두고, 다시 띄워도
    // 같은 포트를 쓴다. 거둘 때 SIGKILL로 끝낸다.
    class test_server
    {
    public:
        test_server()
            : port_ { silent_server {}.port() }
        {
            start();
        }

        test_server(const test_server&) = delete;
        test_server& operator=(const test_server&) = delete;

        ~test_server()
        {
            halt();
        }

        // 들을 준비가 될 때까지 기다린다. 못 띄웠으면 거짓이다.
        bool start()
        {
            char self[512] {};
            const ssize_t length { ::readlink("/proc/self/exe", self, sizeof(self) - 1) };
            std::string program { length > 0 ? std::string { self, static_cast<std::size_t>(length) } : std::string {} };
            program = program.substr(0, program.rfind('/') + 1) + "luil_smb_test_server";
            static_cast<void>(::chmod(program.c_str(), 0755));
            std::string port { std::to_string(port_) };
            char* const arguments[] { program.data(), port.data(), nullptr };
            process_ = ::fork();
            if (process_ == 0)
            {
                // test 프로세스가 거두지 못하고 죽어도(중단, 파이프 끊김) 서버가 남지 않게 함께 끝낸다.
                ::prctl(PR_SET_PDEATHSIG, SIGKILL);
                ::execve(program.c_str(), arguments, environ);
                ::_exit(127);
            }
            if (process_ < 0)
                return false;
            const auto deadline { std::chrono::steady_clock::now() + 5s };
            while (std::chrono::steady_clock::now() < deadline)
            {
                const int probe { ::socket(AF_INET, SOCK_STREAM, 0) };
                sockaddr_in address {};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = htons(port_);
                const bool connected { ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 };
                ::close(probe);
                if (connected)
                    return true;
                std::this_thread::sleep_for(20ms);
            }
            return false;
        }

        // 서버 프로세스가 아직 살아 있는가 (죽었으면 거둔다).
        [[nodiscard]] bool alive()
        {
            if (process_ <= 0)
                return false;
            int status { 0 };
            if (::waitpid(process_, &status, WNOHANG) == 0)
                return true;
            process_ = -1;
            return false;
        }

        void halt()
        {
            if (process_ <= 0)
                return;
            ::kill(process_, SIGKILL);
            ::waitpid(process_, nullptr, 0);
            process_ = -1;
        }

        [[nodiscard]] std::u8string server() const
        {
            return loopback(port_);
        }

    private:
        std::uint16_t port_ { 0 };
        pid_t process_ { -1 };
    };

    // 실패한 답을 test 출력에 적는다.
    [[nodiscard]] std::string describe(const smb_response& response)
    {
        return "kind " + std::to_string(static_cast<int>(response.error.kind)) + " (" + std::to_string(response.error.native_error) + ") connection "
            + std::to_string(static_cast<int>(response.connection)) + " " + std::string { reinterpret_cast<const char*>(response.error.message.data()), response.error.message.size() };
    }

    [[nodiscard]] smb_request list_on(std::u8string server, const std::chrono::milliseconds timeout)
    {
        smb_request request {};
        request.location.server = std::move(server);
        request.location.share = u8"photos";
        request.location.user = u8"reader";
        request.location.password = u8"secret";
        request.timeout = timeout;
        return request;
    }
} // namespace

TEST_CASE("A test server exchange lists a folder and reads ranges of a file", "[net][smb][android][server]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    REQUIRE(sink.wait_for(1, 15s));
    const smb_response listed { sink.snapshot().at(0) };
    INFO(describe(listed));
    REQUIRE(listed.ok());
    CHECK(listed.entries.size() == 5u);

    smb_request read { list_on(server.server(), 10s) };
    read.operation = luil::net::smb_operation::read;
    read.path = u8"big.bin";
    read.offset = 1000;
    read.length = 5000;
    REQUIRE(static_cast<bool>(client->send(read, error)));
    REQUIRE(sink.wait_for(2, 15s));
    const smb_response chunk { sink.snapshot().at(1) };
    INFO(describe(chunk));
    REQUIRE(chunk.ok());
    REQUIRE(chunk.data.size() == 5000u);
    CHECK(chunk.data.front() == static_cast<std::uint8_t>(1000 & 0xff));
    CHECK(chunk.data.back() == static_cast<std::uint8_t>(5999 & 0xff));
}

TEST_CASE("A wrong password answers logon_failure", "[net][smb][android][server]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    smb_request request { list_on(server.server(), 10s) };
    request.location.password = u8"wrong";
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(request, error)));
    REQUIRE(sink.wait_for(1, 15s));
    const smb_response response { sink.snapshot().at(0) };
    CHECK(response.error.kind == smb_error_kind::logon_failure);
    CHECK(response.error.message.find(u8"wrong") == std::u8string::npos);
}

namespace {
    using luil::net::smb_connection_use;

    // 요청 하나를 보내고 답을 기다린다. 답은 `sink`에 쌓이므로 몇 번째인지(`index`)로 꺼낸다.
    [[nodiscard]] smb_response exchange(smb_client& client, collector& sink, const smb_request& request, const std::size_t index)
    {
        std::u8string error {};
        REQUIRE(static_cast<bool>(client.send(request, error)));
        REQUIRE(sink.wait_for(index + 1, 15s));
        return sink.snapshot().at(index);
    }

    [[nodiscard]] smb_request in_folder(std::u8string server, std::u8string path)
    {
        smb_request request { list_on(std::move(server), 10s) };
        request.path = std::move(path);
        return request;
    }
} // namespace

TEST_CASE("The same account reuses its connection and another account opens its own", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    const smb_response first { exchange(*client, sink, list_on(server.server(), 10s), 0) };
    INFO(describe(first));
    REQUIRE(first.ok());
    CHECK(first.connection == smb_connection_use::opened);
    const smb_response second { exchange(*client, sink, list_on(server.server(), 10s), 1) };
    REQUIRE(second.ok());
    CHECK(second.connection == smb_connection_use::reused);

    // 구간 읽기도 같은 연결을 쓴다.
    smb_request read { list_on(server.server(), 10s) };
    read.operation = luil::net::smb_operation::read;
    read.path = u8"big.bin";
    read.length = 64u * 1024u;
    const smb_response chunk { exchange(*client, sink, read, 2) };
    REQUIRE(chunk.ok());
    CHECK(chunk.connection == smb_connection_use::reused);
    read.offset = read.length;
    const smb_response next { exchange(*client, sink, read, 3) };
    REQUIRE(next.ok());
    CHECK(next.connection == smb_connection_use::reused);
    CHECK(next.data.front() == static_cast<std::uint8_t>((64u * 1024u) & 0xffu));

    // 도메인이 다르면 다른 연결이다.
    smb_request other { list_on(server.server(), 10s) };
    other.location.domain = u8"WORKGROUP";
    const smb_response separate { exchange(*client, sink, other, 4) };
    INFO(describe(separate));
    REQUIRE(separate.ok());
    CHECK(separate.connection == smb_connection_use::opened);
}

TEST_CASE("A resting connection closes after its idle time", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink, 4, [](luil::net::smb_client_config& config) { config.idle_timeout = 300ms; }) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 1).connection == smb_connection_use::reused);
    std::this_thread::sleep_for(700ms);
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 2).connection == smb_connection_use::opened);
}

TEST_CASE("Without idle time every request opens its own connection", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink, 4, [](luil::net::smb_client_config& config) { config.idle_timeout = 0ms; }) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 1).connection == smb_connection_use::opened);
}

TEST_CASE("A full server makes the next request wait for a connection", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink, 4, [](luil::net::smb_client_config& config) { config.max_connections_per_server = 1; }) };
    std::u8string error {};
    // 시험 서버는 `slow` 폴더를 여는 데 2초를 쓴다. 그동안 그 서버의 연결 하나가 쓰는 중이다.
    const luil::net::smb_ticket slow { client->send(in_folder(server.server(), u8"slow"), error) };
    REQUIRE(static_cast<bool>(slow));
    std::this_thread::sleep_for(200ms);
    const luil::net::smb_ticket waiting { client->send(list_on(server.server(), 10s), error) };
    REQUIRE(static_cast<bool>(waiting));
    REQUIRE(sink.wait_for(2, 15s));
    const std::vector<smb_response> responses { sink.snapshot() };
    REQUIRE(responses.at(0).ticket == slow);
    REQUIRE(responses.at(1).ticket == waiting);
    CHECK(responses.at(0).ok());
    CHECK(responses.at(1).ok());
    // 기다린 요청은 돌아온 그 연결을 다시 쓴다.
    CHECK(responses.at(1).connection == smb_connection_use::reused);
}

TEST_CASE("A connection dropped during a request is retried once on a new one", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    // 시험 서버는 `drop-once`를 처음 열 때 답하지 않고 연결을 끊는다.
    const smb_response retried { exchange(*client, sink, in_folder(server.server(), u8"drop-once"), 1) };
    INFO(describe(retried));
    CHECK(server.alive());
    CHECK(retried.ok());
    CHECK(retried.connection == smb_connection_use::reopened);
    CHECK(retried.entries.empty());
}

TEST_CASE("A connection closed while resting is replaced before the request", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    // 서버를 끄고 같은 포트에 다시 띄운다. 쉬던 연결은 끊겼다.
    server.halt();
    REQUIRE(server.start());
    const smb_response replaced { exchange(*client, sink, list_on(server.server(), 10s), 1) };
    INFO(describe(replaced));
    CHECK(replaced.ok());
    CHECK(replaced.connection == smb_connection_use::reopened);
}

TEST_CASE("A logon failure is answered without a retry", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    smb_request wrong { list_on(server.server(), 10s) };
    wrong.location.password = u8"wrong";
    const smb_response refused { exchange(*client, sink, wrong, 1) };
    CHECK(refused.error.kind == smb_error_kind::logon_failure);
    CHECK(refused.connection == smb_connection_use::opened);
    // 맞는 계정의 쉬던 연결은 그대로다.
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 2).connection == smb_connection_use::reused);
}

TEST_CASE("A cancelled request leaves no connection to reuse", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    const luil::net::smb_ticket slow { client->send(in_folder(server.server(), u8"slow"), error) };
    REQUIRE(static_cast<bool>(slow));
    std::this_thread::sleep_for(500ms);
    client->cancel(slow);
    REQUIRE(sink.wait_for(1, 5s));
    CHECK(sink.snapshot().at(0).error.kind == smb_error_kind::cancelled);
    // 끊은 요청의 연결은 상태를 알 수 없어 버렸다. 다음 요청은 새로 연다.
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 1).connection == smb_connection_use::opened);
}

TEST_CASE("Closing idle connections makes the next request open a new one", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    client->close_idle();
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 1).connection == smb_connection_use::opened);
    CHECK(server.alive());
    const smb_response third { exchange(*client, sink, list_on(server.server(), 10s), 2) };
    INFO(describe(third));
    CHECK(server.alive());
    CHECK(third.connection == smb_connection_use::reused);
}

TEST_CASE("Stopping with resting connections returns within its budget", "[net][smb][android][server][reuse]")
{
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    CHECK(exchange(*client, sink, list_on(server.server(), 10s), 0).connection == smb_connection_use::opened);
    const auto started { std::chrono::steady_clock::now() };
    client->stop();
    CHECK(std::chrono::steady_clock::now() - started < 1s);
    CHECK(client->in_flight() == 0u);
}

TEST_CASE("A share list on a server without srvsvc fails cleanly and leaves the server usable", "[net][smb][android][server][shares]")
{
    // 시험 서버는 srvsvc 파이프를 내주지 않는다 (공유 목록의 실제 왕복은 사람이 NAS로 확인한다). 실패가 값으로 오고,
    // IPC$ 연결은 디스크 공유의 연결과 따로이며, 서버는 그대로 쓸 수 있어야 한다.
    test_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    smb_request shares { list_on(server.server(), 10s) };
    shares.operation = luil::net::smb_operation::shares;
    shares.location.share.clear();
    const smb_response refused { exchange(*client, sink, shares, 0) };
    INFO(describe(refused));
    CHECK(refused.ok() == false);
    CHECK(refused.error.kind != smb_error_kind::invalid_request);
    CHECK(refused.connection == smb_connection_use::opened);
    CHECK(refused.shares.empty());
    CHECK(server.alive());
    const smb_response listed { exchange(*client, sink, list_on(server.server(), 10s), 1) };
    INFO(describe(listed));
    CHECK(listed.ok());
    CHECK(listed.connection == smb_connection_use::opened);
}

TEST_CASE("The Android SMB build is supported and refuses a bad request before the wire", "[net][smb][android]")
{
    REQUIRE(smb_client::supported());
    collector sink {};
    const auto client { make_client(sink) };
    smb_request request { list_on(u8"nas box", 1s) };
    std::u8string error {};
    CHECK_FALSE(static_cast<bool>(client->send(request, error)));
    CHECK(error.empty() == false);
    CHECK(client->in_flight() == 0u);
}

TEST_CASE("A closed port answers cannot_connect", "[net][smb][android]")
{
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    const luil::net::smb_ticket ticket { client->send(list_on(closed_server(), 5s), error) };
    REQUIRE(static_cast<bool>(ticket));
    REQUIRE(sink.wait_for(1, 10s));
    const smb_response response { sink.snapshot().at(0) };
    CHECK(response.ticket == ticket);
    CHECK(response.error.kind == smb_error_kind::cannot_connect);
    // 열지 못한 시도도 새로 연 것으로 센다.
    CHECK(response.connection == luil::net::smb_connection_use::opened);
    // 오류 글에 암호가 들어가지 않는다.
    CHECK(response.error.message.find(u8"secret") == std::u8string::npos);
}

TEST_CASE("A server that never answers runs into the timeout", "[net][smb][android]")
{
    const silent_server server {};
    REQUIRE(server.port() != 0);
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    const auto started { std::chrono::steady_clock::now() };
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 300ms), error)));
    REQUIRE(sink.wait_for(1, 10s));
    CHECK(sink.snapshot().at(0).error.kind == smb_error_kind::timed_out);
    CHECK(std::chrono::steady_clock::now() - started < 3s);
}

TEST_CASE("Cancelling a waiting request answers it once as cancelled", "[net][smb][android]")
{
    const silent_server server {};
    collector sink {};
    const auto client { make_client(sink, 1) };
    std::u8string error {};
    const luil::net::smb_ticket ticket { client->send(list_on(server.server(), 10s), error) };
    REQUIRE(static_cast<bool>(ticket));
    // 상한이 1이라 두 번째는 숨은 줄 없이 거절된다.
    CHECK_FALSE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    CHECK(client->in_flight() == 1u);
    client->cancel(ticket);
    REQUIRE(sink.wait_for(1, 5s));
    CHECK(sink.snapshot().at(0).error.kind == smb_error_kind::cancelled);
    // 모르는 표의 취소는 아무 일도 없다.
    client->cancel(luil::net::smb_ticket { 999 });
    CHECK_FALSE(sink.wait_for(2, 300ms));
}

TEST_CASE("Overlapping requests create and tear down their contexts safely", "[net][smb][android]")
{
    // 닫힌 포트는 연결이 바로 실패해, worker들이 libsmb2 문맥을 겹쳐 만들고 거둔다 (전역 문맥 목록의 경합 자리).
    constexpr std::size_t requests { 200 };
    constexpr std::size_t width { 8 };
    collector sink {};
    const auto client { make_client(sink, width) };
    const std::u8string server { closed_server() };
    std::size_t sent { 0 };
    const auto deadline { std::chrono::steady_clock::now() + 60s };
    while (sent < requests && std::chrono::steady_clock::now() < deadline)
    {
        std::u8string error {};
        if (static_cast<bool>(client->send(list_on(server, 5s), error)))
        {
            ++sent;
            continue;
        }
        // 포화다. 숨은 줄이 없으므로 자리가 날 때까지 기다린다.
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(sent == requests);
    REQUIRE(sink.wait_for(requests, 60s));
    for (const smb_response& response : sink.snapshot())
        CHECK(response.error.kind == smb_error_kind::cannot_connect);
}

TEST_CASE("Stopping answers nothing more and returns within its budget", "[net][smb][android]")
{
    const silent_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    const auto started { std::chrono::steady_clock::now() };
    client->stop();
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    CHECK_FALSE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    CHECK_FALSE(sink.wait_for(1, 300ms));
    client->stop();
}

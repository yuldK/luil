#include "luil/net/http_client.h"

#include "loopback_http_server.h"
#include "luil/net/http_body.h"
#include "luil/net/http_media_type.h"
#include "luil/net/http_message.h"
#include "sample_image_bytes.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    using luil::net::http_body_kind;
    using luil::net::http_client;
    using luil::net::http_client_config;
    using luil::net::http_error_kind;
    using luil::net::http_header_crosses_origins;
    using luil::net::http_heartbeat;
    using luil::net::http_method;
    using luil::net::http_redirect_policy;
    using luil::net::http_request;
    using luil::net::http_response;
    using luil::net::http_ticket;
    using luil::testing::loopback_bytes;
    using luil::testing::loopback_dead_port;
    using luil::testing::loopback_framing;
    using luil::testing::loopback_http_server;
    using luil::testing::loopback_request;
    using luil::testing::loopback_response;
    // 2×2 png. 견본은 `tests/sample_image_bytes.h`가 함께 든다 — 이 축이 묻는 것은
    // "몸이 그림으로 풀렸는가"뿐이라 픽셀 값까지 볼 일이 없다.
    using luil::testing::sample_quadrant_png;

    [[nodiscard]] std::vector<std::uint8_t> request_bytes(const std::string_view text)
    {
        std::vector<std::uint8_t> bytes {};
        bytes.reserve(text.size());
        for (const char character : text)
            bytes.push_back(static_cast<std::uint8_t>(character));
        return bytes;
    }

    [[nodiscard]] loopback_response canned(const std::string_view content_type, const std::string_view body)
    {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", std::string { content_type } });
        response.body = loopback_bytes(body);
        return response;
    }

    // 답을 모아 개수로 기다린다.
    //
    // 시각이 아니라 **개수**로 기다리는 것이 요점이다 — CI 러너에서 시각은 흔들리고,
    // 이 축이 묻는 것은 "몇 개가 왔는가"이지 "언제 왔는가"가 아니다.
    class collector
    {
    public:
        void add(http_response response)
        {
            {
                std::lock_guard<std::mutex> lock { mutex_ };
                if (sealed_.load())
                    ++late_;
                delivery_thread_ = std::this_thread::get_id();
                responses_.push_back(std::move(response));
            }
            signal_.notify_all();
        }

        [[nodiscard]] bool wait_for(const std::size_t count, const std::chrono::milliseconds limit)
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            return signal_.wait_for(lock, limit, [this, count] { return responses_.size() >= count; });
        }

        [[nodiscard]] std::size_t size() const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return responses_.size();
        }

        [[nodiscard]] http_response at(const std::size_t index) const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return responses_.at(index);
        }

        [[nodiscard]] std::vector<http_response> snapshot() const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return responses_;
        }

        [[nodiscard]] std::thread::id delivery_thread() const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return delivery_thread_;
        }

        // `stop()`이 돌아온 뒤로 `deliver`가 불리면 안 된다는 계약의 자리다.
        void seal() noexcept
        {
            sealed_.store(true);
        }

        [[nodiscard]] std::size_t late() const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return late_;
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable signal_ {};
        std::vector<http_response> responses_ {};
        std::thread::id delivery_thread_ {};
        std::atomic<bool> sealed_ { false };
        std::size_t late_ { 0 };
    };

    // 되돌이 주소를 쓰므로 proxy를 끈다 — CI 기계의 proxy가 127.0.0.1을 가로채면
    // 이 파일 전체가 소음이 된다.
    [[nodiscard]] std::unique_ptr<http_client> make_client(
        collector& sink, const std::chrono::milliseconds stop_budget = 1000ms, const std::size_t max_in_flight = 32, const std::size_t max_pending = 128)
    {
        http_client_config configuration {};
        configuration.use_system_proxy = false;
        configuration.stop_budget = stop_budget;
        configuration.max_in_flight = max_in_flight;
        configuration.max_pending_responses = max_pending;
        configuration.deliver = [&sink](http_response response) { sink.add(std::move(response)); };

        std::u8string error {};
        std::unique_ptr<http_client> client { http_client::create(std::move(configuration), error) };
        REQUIRE(client != nullptr);
        REQUIRE(error.empty());
        return client;
    }

    [[nodiscard]] http_ticket send_to(http_client& client, const std::u8string& url)
    {
        http_request request {};
        request.url = url;
        std::u8string error {};
        const http_ticket ticket { client.send(std::move(request), error) };
        CHECK(error.empty());
        return ticket;
    }

    // 표 하나의 답만 온 차례대로 골라 낸다. 되풀이를 여러 번 세우는 축은 답이
    // 뒤섞여 쌓이므로 "이 표의 마지막 답"을 물으려면 이 체가 필요하다.
    [[nodiscard]] std::vector<http_response> responses_for(const collector& sink, const http_ticket ticket)
    {
        std::vector<http_response> mine {};
        for (const http_response& response : sink.snapshot())
            if (response.ticket == ticket)
                mine.push_back(response);
        return mine;
    }

    // 그 표의 답이 `count`개가 될 때까지 기다린다. 상한 안에서만 돈다 — 끝없이
    // 기다리는 자리는 CTest 시한을 통째로 태운다.
    [[nodiscard]] bool wait_for_ticket(const collector& sink, const http_ticket ticket, const std::size_t count, const std::chrono::milliseconds limit)
    {
        const auto deadline { std::chrono::steady_clock::now() + limit };
        while (responses_for(sink, ticket).size() < count)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(5ms);
        }
        return true;
    }

    // "/hop/3"의 3이다. 그 꼴이 아니면 -1이다.
    [[nodiscard]] int hop_number(const std::string& target) noexcept
    {
        constexpr std::string_view prefix { "/hop/" };
        if (target.size() <= prefix.size() || target.compare(0, prefix.size(), prefix) != 0)
            return -1;

        int value { 0 };
        const char* const first { target.data() + prefix.size() };
        const char* const last { target.data() + target.size() };
        if (std::from_chars(first, last, value).ec != std::errc {})
            return -1;
        return value;
    }

    [[nodiscard]] std::size_t header_count(const loopback_request& request, const std::string_view lower_case_name)
    {
        std::size_t count { 0 };
        for (const std::pair<std::string, std::string>& header : request.headers)
            if (header.first == lower_case_name)
                ++count;
        return count;
    }

    [[nodiscard]] std::string url_text(const std::u8string& url)
    {
        return std::string { reinterpret_cast<const char*>(url.data()), url.size() };
    }

    // 각각 "ok"와 'A' 64개의 gzip 바이트다. 푼 크기와 선 위 크기를 다르게
    // 두어 Content-Length가 몸 상한의 기준으로 잘못 쓰이는지 본다.
    constexpr std::array<std::uint8_t, 22> gzip_ok { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0xcb, 0xcf, 0x06, 0x00, 0x47, 0xdd, 0xdc, 0x79, 0x02, 0x00, 0x00, 0x00 };
    constexpr std::array<std::uint8_t, 24> gzip_repeated_a {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x73, 0x74, 0xa4, 0x0c, 0x00, 0x00, 0x3c, 0x62, 0x4c, 0x41, 0x40, 0x00, 0x00,
        0x00 // gzip 꼬리의 원래 길이는 64바이트다.
    };
} // namespace

TEST_CASE("http client delivers a parsed json body", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("application/json", R"({"a":1})"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    const http_ticket ticket { send_to(*client, server.url("/json")) };
    REQUIRE(static_cast<bool>(ticket));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    CHECK(response.ok());
    CHECK(response.status_code == 200);
    CHECK(response.ticket == ticket);
    CHECK(response.sequence == 0);
    CHECK(response.last);
    REQUIRE(response.body.kind == http_body_kind::json);
    REQUIRE(response.body.json != nullptr);
    CHECK(response.body.json->at("a") == 1);
}

TEST_CASE("http client decodes an image body", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", "image/png" });
        response.body.assign(sample_quadrant_png.begin(), sample_quadrant_png.end());
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/image.png"))));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    REQUIRE(response.body.kind == http_body_kind::image);
    CHECK(response.body.image.valid());
    CHECK(response.body.still_image().width() == 2);
}

TEST_CASE("http client decodes a text body", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain; charset=utf-8", "hello world"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/text"))));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    CHECK(response.body.kind == http_body_kind::text);
    CHECK(response.body.as_text() == u8"hello world");
}

TEST_CASE("http client leaves html unparsed", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/html; charset=utf-8", "<html></html>"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/page"))));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.body.kind == http_body_kind::html);
    CHECK(response.body.as_text().empty());
    CHECK(response.body.data().empty() == false);
    CHECK(response.content_type.charset == u8"utf-8");
}

TEST_CASE("http client reports 404 as a response and not an error", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("application/problem+json", R"({"title":"missing"})") };
        response.status = 404;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/missing"))));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    CHECK(response.status_code == 404);
    CHECK(response.ok() == false);
    REQUIRE(response.body.kind == http_body_kind::json);
    REQUIRE(response.body.json != nullptr);
    CHECK(response.body.json->at("title") == "missing");
}

TEST_CASE("http client handles a head response with no body", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("application/json", ""); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/head");
    request.method = http_method::head;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    CHECK(response.body.kind == http_body_kind::empty);
    CHECK(response.body.data().empty());
    CHECK(response.content_type.essence == u8"application/json");
    // 선에 실제로 나간 동사를 본다 — 몸이 비었다는 것만으로는 GET을 보내고 빈 답을
    // 받은 것과 구별되지 않는다.
    const std::vector<loopback_request> seen { server.requests() };
    REQUIRE(seen.size() == 1);
    CHECK(seen.front().method == "HEAD");
}

TEST_CASE("a HEAD response with a large Content-Length is not too large", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", "application/json" });
        // HEAD의 답이 적는 길이는 **오지 않을 몸의 크기**다. 서버가 붙이는 길이와
        // 겹치지 않도록 끊음으로 끝내고 몸통은 비운다.
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Length", "999999999" });
        response.framing = loopback_framing::close_delimited;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/head");
    request.method = http_method::head;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    // 그 길이로 상한을 재면 어떤 HEAD도 `body_too_large`가 된다.
    CHECK(response.error.empty());
    CHECK(response.body.kind == http_body_kind::empty);
    const std::vector<loopback_request> seen { server.requests() };
    REQUIRE(seen.size() == 1);
    CHECK(seen.front().method == "HEAD");
}

TEST_CASE("http client sends a post body and its content type", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("application/json", R"({"ok":true})"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    const std::vector<std::uint8_t> body { request_bytes(R"({"name":"x"})") };
    http_request request {};
    request.url = server.url("/submit");
    request.method = http_method::post;
    request.content_type = u8"application/json";
    request.body = body;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    const std::vector<loopback_request> seen { server.requests() };
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].method == "POST");
    CHECK(seen[0].header("content-type").value_or("") == "application/json");
    CHECK(seen[0].body == body);
    CHECK(sink.at(0).error.empty());
}

TEST_CASE("a Content-Type given as a header survives", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    SECTION("the app's own header is not overwritten by the default")
    {
        http_request request {};
        request.url = server.url("/typed");
        request.method = http_method::post;
        request.body = request_bytes("x");
        request.headers.push_back({ u8"Content-Type", u8"application/x-demo" });
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
        REQUIRE(sink.wait_for(1, 10s));

        const std::vector<loopback_request> seen { server.requests() };
        REQUIRE(seen.size() == 1);
        // 앱이 적은 줄을 기본값(`application/octet-stream`)으로 덮으면 앱은 그
        // 사실을 알 길이 없다 — 덧붙이기만 해도 두 줄이 나가 서버가 고른다.
        CHECK(header_count(seen.front(), "content-type") == 1u);
        CHECK(seen.front().header("content-type").value_or("") == "application/x-demo");
    }

    SECTION("the content type field beats a header of the same name")
    {
        http_request request {};
        request.url = server.url("/typed");
        request.method = http_method::post;
        request.body = request_bytes("x");
        request.content_type = u8"text/plain";
        request.headers.push_back({ u8"Content-Type", u8"x" });
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
        REQUIRE(sink.wait_for(1, 10s));

        const std::vector<loopback_request> seen { server.requests() };
        REQUIRE(seen.size() == 1);
        // 두 자리가 같은 것을 말하면 이기는 쪽이 한 곳에 적혀 있어야 한다.
        CHECK(header_count(seen.front(), "content-type") == 1u);
        CHECK(seen.front().header("content-type").value_or("") == "text/plain");
    }
}

TEST_CASE("http client writes every method verb on the wire", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    const std::vector<std::pair<http_method, std::string>> verbs {
        { http_method::put, "PUT" },
        { http_method::patch, "PATCH" },
        { http_method::remove, "DELETE" },
        { http_method::options, "OPTIONS" },
    };
    for (const auto& verb : verbs)
    {
        http_request request {};
        request.url = server.url("/verb");
        request.method = verb.first;
        request.body = request_bytes("x");
        request.content_type = u8"text/plain";
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    }
    REQUIRE(sink.wait_for(verbs.size(), 10s));

    std::set<std::string> seen {};
    for (const loopback_request& request : server.requests())
        seen.insert(request.method);
    for (const auto& verb : verbs)
        CHECK(seen.count(verb.second) == 1);
}

TEST_CASE("http client carries request headers and reads response headers", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "ok") };
        response.headers.push_back(std::pair<std::string, std::string> { "X-Test", "value" });
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/headers");
    request.headers.push_back({ u8"X-Sent", u8"here" });
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    const std::vector<loopback_request> seen { server.requests() };
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].header("x-sent").value_or("") == "here");
    // 답의 헤더는 대소문자를 가리지 않고 답한다.
    CHECK(sink.at(0).header(u8"x-test") == u8"value");
    CHECK(sink.at(0).header(u8"X-TEST") == u8"value");
}

TEST_CASE("http client refuses a response longer than the body limit", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", "application/octet-stream" });
        // 길이를 크게 적고 끊음으로 끝낸다 — 서버가 붙이는 헤더와 겹치지 않는 조합이다.
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Length", "1000000" });
        response.framing = loopback_framing::close_delimited;
        response.body = loopback_bytes("small");
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/huge");
    request.max_body_bytes = 1024;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).error.kind == http_error_kind::body_too_large);
    CHECK(server.request_count() == 1);
}

TEST_CASE("http client applies the body limit to decompressed bytes", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request& request) {
        loopback_response response {};
        response.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
        response.headers.emplace_back("Content-Encoding", "gzip");
        if (request.target == "/small")
            response.body.assign(gzip_ok.begin(), gzip_ok.end());
        else
            response.body.assign(gzip_repeated_a.begin(), gzip_repeated_a.end());
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request small {};
    small.url = server.url("/small");
    small.max_body_bytes = 2;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(small), error)));
    REQUIRE(sink.wait_for(1, 10s));
    CHECK(sink.at(0).error.empty());
    CHECK(sink.at(0).body.as_text() == u8"ok");

    http_request expanded {};
    expanded.url = server.url("/expanded");
    expanded.max_body_bytes = 32;
    REQUIRE(static_cast<bool>(client->send(std::move(expanded), error)));
    REQUIRE(sink.wait_for(2, 10s));
    CHECK(sink.at(1).error.kind == http_error_kind::body_too_large);

    http_request encoded {};
    encoded.url = server.url("/small");
    encoded.decompress = false;
    encoded.max_body_bytes = 2;
    REQUIRE(static_cast<bool>(client->send(std::move(encoded), error)));
    REQUIRE(sink.wait_for(3, 10s));
    CHECK(sink.at(2).error.kind == http_error_kind::body_too_large);
}

TEST_CASE("http client refuses a streamed body that crosses the limit", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", "application/octet-stream" });
        response.framing = loopback_framing::chunked;
        response.chunk_size = 1024;
        response.body.assign(64u * 1024u, std::uint8_t { 0x41 });
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/stream");
    request.max_body_bytes = 2048;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).error.kind == http_error_kind::body_too_large);
    CHECK(sink.at(0).body.data().empty());
}

TEST_CASE("http client gives up on a hanging server at the total timeout", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.hang_forever = true;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/hang");
    request.total_timeout = 300ms;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).error.kind == http_error_kind::timed_out);
}

TEST_CASE("http client reports a truncated body as a lost connection", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.headers.push_back(std::pair<std::string, std::string> { "Content-Type", "text/plain" });
        response.body.assign(4096u, std::uint8_t { 0x42 });
        response.truncate_body_after = 16u;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/cut"))));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    // 되돌이 서버가 RST로 끊으면 WinHTTP가 `ERROR_WINHTTP_CONNECTION_ERROR`
    // (0x2EFE)를 답하고, 그 코드의 뜻이 곧 이 갈래다 — "덜 왔다"를 "다 왔다"와
    // 가르는 자리라 갈래까지 못 박는다.
    CHECK(response.error.kind == http_error_kind::connection_lost);
    CHECK(response.body.data().empty());
}

TEST_CASE("http client reports a closed port as a failed connection", "[net][client]")
{
    // 서버를 세웠다 멈추는 흉내가 아니라 **묶어만 둔 포트**다. 멈춘 포트는 곧바로
    // 남의 것이 될 수 있어 거절이 답이라는 보장이 없다.
    const loopback_dead_port dead {};
    REQUIRE(dead.port() != 0);

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, dead.url("/gone"))));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).error.kind == http_error_kind::cannot_connect);
}

TEST_CASE("http client refuses a malformed url before touching the wire", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    const std::vector<std::u8string> broken { u8"", u8"ftp://example.invalid/x", u8"not a url" };
    for (const std::u8string& url : broken)
    {
        http_request request {};
        request.url = url;
        std::u8string error {};
        const http_ticket ticket { client->send(std::move(request), error) };
        CHECK(static_cast<bool>(ticket) == false);
        CHECK(error.empty() == false);
    }

    std::this_thread::sleep_for(300ms);
    CHECK(server.request_count() == 0);
    CHECK(sink.size() == 0);
}

TEST_CASE("http client refuses a header that carries a line break", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/inject");
    request.headers.push_back({ u8"X-Bad", u8"value\r\nX-Injected: yes" });
    std::u8string error {};
    const http_ticket ticket { client->send(std::move(request), error) };
    CHECK(static_cast<bool>(ticket) == false);
    CHECK(error.empty() == false);

    std::this_thread::sleep_for(300ms);
    CHECK(server.request_count() == 0);
    CHECK(sink.size() == 0);
}

TEST_CASE("http client follows a redirect unless the policy says otherwise", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([&server](const loopback_request& request) {
        if (request.target == "/start")
        {
            loopback_response response {};
            response.status = 302;
            const std::u8string target { server.url("/final") };
            response.headers.push_back(std::pair<std::string, std::string> { "Location", std::string { reinterpret_cast<const char*>(target.c_str()) } });
            return response;
        }
        return canned("text/plain", "arrived");
    });

    SECTION("the default policy follows it")
    {
        collector sink {};
        const std::unique_ptr<http_client> client { make_client(sink) };
        REQUIRE(static_cast<bool>(send_to(*client, server.url("/start"))));
        REQUIRE(sink.wait_for(1, 10s));

        const http_response response { sink.at(0) };
        CHECK(response.status_code == 200);
        CHECK(response.final_url.ends_with(u8"/final"));
        CHECK(server.request_count() == 2);
    }

    SECTION("the none policy hands the 3xx back")
    {
        collector sink {};
        const std::unique_ptr<http_client> client { make_client(sink) };

        http_request request {};
        request.url = server.url("/start");
        request.redirects = http_redirect_policy::none;
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
        REQUIRE(sink.wait_for(1, 10s));

        const http_response response { sink.at(0) };
        CHECK(response.status_code == 302);
        CHECK(response.header(u8"Location").empty() == false);
        CHECK(server.request_count() == 1);
    }
}

TEST_CASE("http client clears app headers before a cross origin redirect", "[net][client]")
{
    loopback_http_server origin {};
    loopback_http_server destination {};
    REQUIRE(origin.port() != 0);
    REQUIRE(destination.port() != 0);
    REQUIRE(origin.port() != destination.port());
    origin.set_handler([&](const loopback_request& request) {
        loopback_response response {};
        response.status = 302;
        response.headers.emplace_back("Location", url_text(request.target == "/start" ? origin.url("/same") : destination.url("/final")));
        return response;
    });
    destination.set_handler([](const loopback_request&) { return canned("text/plain", "arrived"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = origin.url("/start");
    request.headers.push_back({ u8"Authorization", u8"Bearer secret" });
    request.headers.push_back({ u8"Cookie", u8"session=secret" });
    request.headers.push_back({ u8"X-Api-Key", u8"secret" });
    request.headers.push_back({ u8"x-api-key", u8"secret" });
    // 요청의 뜻을 정하는 표준 헤더는 다른 출처에도 간다 — 이어받기가 전체 받기로
    // 바뀌거나 형식 협상이 풀리지 않는다.
    request.headers.push_back({ u8"Range", u8"bytes=4-" });
    request.headers.push_back({ u8"accept", u8"text/plain" });
    request.headers.push_back({ u8"If-None-Match", u8"\"v1\"" });
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    const http_response response { sink.at(0) };
    CHECK(response.error.empty());
    CHECK(response.status_code == 200);
    CHECK(response.final_url == destination.url("/final"));

    const std::vector<loopback_request> origin_requests { origin.requests() };
    REQUIRE(origin_requests.size() == 2);
    for (const loopback_request& seen : origin_requests)
    {
        CHECK(seen.header("authorization").value_or("") == "Bearer secret");
        CHECK(seen.header("cookie").value_or("") == "session=secret");
        CHECK(seen.header("x-api-key").value_or("") == "secret");
    }

    const std::vector<loopback_request> destination_requests { destination.requests() };
    REQUIRE(destination_requests.size() == 1);
    CHECK(destination_requests[0].header("authorization").has_value() == false);
    CHECK(destination_requests[0].header("cookie").has_value() == false);
    CHECK(header_count(destination_requests[0], "x-api-key") == 0);
    CHECK(destination_requests[0].header("range").value_or("") == "bytes=4-");
    CHECK(destination_requests[0].header("accept").value_or("") == "text/plain");
    CHECK(destination_requests[0].header("if-none-match").value_or("") == "\"v1\"");
}

TEST_CASE("Only headers that carry no secret cross origins on a redirect", "[net][message]")
{
    CHECK(http_header_crosses_origins(u8"Range"));
    CHECK(http_header_crosses_origins(u8"IF-NONE-MATCH"));
    CHECK(http_header_crosses_origins(u8"accept"));
    CHECK_FALSE(http_header_crosses_origins(u8"Authorization"));
    CHECK_FALSE(http_header_crosses_origins(u8"Cookie"));
    CHECK_FALSE(http_header_crosses_origins(u8"X-Api-Key"));
    CHECK_FALSE(http_header_crosses_origins(u8"Accept-Charset-Token"));
}

TEST_CASE("http client never follows a redirect for a request with a body", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([&server](const loopback_request&) {
        loopback_response response {};
        response.status = 302;
        const std::u8string target { server.url("/final") };
        response.headers.push_back(std::pair<std::string, std::string> { "Location", std::string { reinterpret_cast<const char*>(target.c_str()) } });
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/start");
    request.method = http_method::post;
    request.content_type = u8"text/plain";
    request.body = request_bytes("payload");
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).status_code == 302);
    CHECK(server.request_count() == 1);
}

TEST_CASE("redirect chains longer than ten are followed up to max_redirects", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([&server](const loopback_request& request) {
        const int hop { hop_number(request.target) };
        if (hop < 0 || hop >= 12)
            return canned("text/plain", "arrived");

        loopback_response response {};
        response.status = 302;
        response.headers.push_back(std::pair<std::string, std::string> { "Location", url_text(server.url("/hop/" + std::to_string(hop + 1))) });
        return response;
    });

    SECTION("a budget above ten is honoured")
    {
        collector sink {};
        const std::unique_ptr<http_client> client { make_client(sink) };

        http_request request {};
        request.url = server.url("/hop/0");
        request.max_redirects = 15;
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
        REQUIRE(sink.wait_for(1, 10s));

        const http_response response { sink.at(0) };
        // WinHTTP 자신의 상한이 기본 10이다. 그것을 요청마다 올려 두지 않으면
        // `max_redirects`가 무엇이라 적혀 있든 열한 번째 걸음에서 끊긴다.
        CHECK(response.error.empty());
        CHECK(response.status_code == 200);
        CHECK(response.final_url.ends_with(u8"/hop/12"));
    }

    SECTION("a smaller budget stops the chain")
    {
        collector sink {};
        const std::unique_ptr<http_client> client { make_client(sink) };

        http_request request {};
        request.url = server.url("/hop/0");
        request.max_redirects = 3;
        std::u8string error {};
        REQUIRE(static_cast<bool>(client->send(std::move(request), error)));
        REQUIRE(sink.wait_for(1, 10s));

        CHECK(sink.at(0).error.kind == http_error_kind::too_many_redirects);
    }
}

TEST_CASE("http client refuses a request over the in flight limit", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 400ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink, 1000ms, 2) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/one"))));
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/two"))));

    http_request third {};
    third.url = server.url("/three");
    std::u8string error {};
    CHECK(static_cast<bool>(client->send(std::move(third), error)) == false);
    CHECK(error.empty() == false);

    REQUIRE(sink.wait_for(2, 10s));
    CHECK(sink.size() == 2);
}

TEST_CASE("http client answers a cancelled request exactly once", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "late") };
        response.delay_before_headers = 1500ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    const http_ticket ticket { send_to(*client, server.url("/slow")) };
    REQUIRE(static_cast<bool>(ticket));

    client->cancel(ticket);
    // 모르는 표는 아무 일도 하지 않는다.
    client->cancel(http_ticket { ticket.value + 1000u });
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(sink.at(0).error.kind == http_error_kind::cancelled);
    CHECK(sink.at(0).ticket == ticket);
    std::this_thread::sleep_for(2s);
    CHECK(sink.size() == 1);
}

TEST_CASE("cancelling a heartbeat mid round yields exactly one final response", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "beat") };
        // 회차가 120 ms를 사는 동안 취소가 떨어지게 한다 — 그 겹치는 창이 이
        // 축이 노리는 자리다 (회차가 없는 취소는 합성 답, 있는 취소는 그 회차의
        // 완료가 마지막이 되어야 하고 **둘이 함께 오면 안 된다**).
        response.delay_before_headers = 120ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    // 경합은 한 번에 걸리지 않는다 — 되풀이가 창을 넓힌다.
    for (int round { 0 }; round < 30; ++round)
    {
        http_request request {};
        request.url = server.url("/beat");
        http_heartbeat schedule {};
        schedule.interval = 100ms;
        schedule.immediate = true;
        std::u8string error {};
        const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
        REQUIRE(static_cast<bool>(ticket));
        REQUIRE(wait_for_ticket(sink, ticket, 1, 10s));

        client->cancel(ticket);
        std::this_thread::sleep_for(400ms);

        const std::vector<http_response> mine { responses_for(sink, ticket) };
        REQUIRE(mine.empty() == false);
        std::size_t finals { 0 };
        for (const http_response& response : mine)
            if (response.last)
                ++finals;
        // 마지막이 둘이면 앱은 표를 두 번 잊고, 마지막 뒤에 답이 더 오면 잊은
        // 표로 답이 온다.
        CHECK(finals == 1);
        CHECK(mine.back().last);
    }
}

TEST_CASE("cancelling many idle heartbeats answers every one", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "beat"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink, 1000ms, 32, 200) };

    // 수락 한도를 200으로 올리고 모두 취소한다. 전송 한도보다 많은 합성
    // 응답도 이미 수락한 만큼은 하나도 버리지 않아야 한다.
    constexpr std::size_t beats { 200 };
    std::set<std::uint64_t> tickets {};
    for (std::size_t index { 0 }; index < beats; ++index)
    {
        http_request request {};
        request.url = server.url("/beat");
        http_heartbeat schedule {};
        schedule.interval = 5s;
        schedule.immediate = false;
        std::u8string error {};
        const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
        REQUIRE(static_cast<bool>(ticket));
        REQUIRE(tickets.insert(ticket.value).second);
    }

    client->cancel_all();
    REQUIRE(sink.wait_for(beats, 10s));

    const std::vector<http_response> seen { sink.snapshot() };
    CHECK(seen.size() == beats);
    std::set<std::uint64_t> answered {};
    for (const http_response& response : seen)
    {
        CHECK(response.error.kind == http_error_kind::cancelled);
        CHECK(response.last);
        // 첫 회차가 나가기 전에 취소된 되풀이의 마지막 답만 0이다.
        CHECK(response.sequence == 0u);
        CHECK(tickets.count(response.ticket.value) == 1);
        CHECK(answered.insert(response.ticket.value).second);
    }
    CHECK(server.request_count() == 0);
}

TEST_CASE("http client repeats a heartbeat under one ticket", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "beat"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/beat");
    http_heartbeat schedule {};
    schedule.interval = 100ms;
    schedule.immediate = true;
    std::u8string error {};
    const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
    REQUIRE(static_cast<bool>(ticket));
    REQUIRE(error.empty());
    REQUIRE(sink.wait_for(3, 10s));

    const std::vector<http_response> rounds { sink.snapshot() };
    REQUIRE(rounds.size() >= 3);
    for (std::size_t index { 0 }; index < 3; ++index)
    {
        CHECK(rounds[index].ticket == ticket);
        CHECK(rounds[index].sequence == index + 1);
        CHECK(rounds[index].error.empty());
    }
    CHECK(server.request_count() >= 3);

    client->cancel(ticket);
    std::this_thread::sleep_for(700ms);
    const std::size_t settled { sink.size() };
    const std::size_t served { server.request_count() };
    std::this_thread::sleep_for(700ms);
    CHECK(sink.size() == settled);
    CHECK(server.request_count() == served);
    REQUIRE(settled >= 3);
    CHECK(sink.at(settled - 1).last);
}

TEST_CASE("http client never overlaps heartbeat rounds", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "beat") };
        response.delay_before_headers = 300ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/beat");
    http_heartbeat schedule {};
    schedule.interval = 300ms;
    schedule.immediate = true;
    std::u8string error {};
    const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
    REQUIRE(static_cast<bool>(ticket));

    std::size_t peak { 0 };
    for (int sample { 0 }; sample < 100; ++sample)
    {
        peak = std::max(peak, server.open_connections());
        std::this_thread::sleep_for(20ms);
    }
    client->cancel(ticket);

    // 답을 받고 나서 쉬므로 회차가 겹칠 수가 없다 — 겹침 정책이라는 물음이 없어지는 자리다.
    CHECK(peak <= 1);
    CHECK(sink.size() >= 2);
    // `open_connections()`가 늘 0을 답해도 위의 `peak <= 1`은 통과한다 — 서버가
    // 실제로 두 번 이상 두드려졌다는 것을 따로 본다.
    CHECK(server.request_count() >= 2);
}

TEST_CASE("http client stops a heartbeat after max rounds", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "beat"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/beat");
    http_heartbeat schedule {};
    schedule.interval = 100ms;
    schedule.immediate = true;
    schedule.max_rounds = 2;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->start_heartbeat(std::move(request), schedule, error)));
    REQUIRE(sink.wait_for(2, 10s));

    std::this_thread::sleep_for(700ms);
    REQUIRE(sink.size() == 2);
    CHECK(sink.at(0).last == false);
    CHECK(sink.at(1).last);
}

TEST_CASE("http client keeps a heartbeat alive across a transport failure", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    std::atomic<int> rounds { 0 };
    server.set_handler([&rounds](const loopback_request&) {
        loopback_response response { canned("text/plain", "beat") };
        if (rounds.fetch_add(1) == 0)
            response.hang_forever = true;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/beat");
    request.total_timeout = 400ms;
    http_heartbeat schedule {};
    schedule.interval = 100ms;
    schedule.immediate = true;
    std::u8string error {};
    const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
    REQUIRE(static_cast<bool>(ticket));
    REQUIRE(sink.wait_for(3, 10s));
    client->cancel(ticket);

    const std::vector<http_response> seen { sink.snapshot() };
    REQUIRE(seen.size() >= 2);
    CHECK(seen[0].error.empty() == false);
    CHECK(seen[0].last == false);
    CHECK(seen[1].error.empty());
    CHECK(seen[1].sequence == 2);
}

TEST_CASE("http client floors the heartbeat interval", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "beat"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    http_request request {};
    request.url = server.url("/beat");
    http_heartbeat schedule {};
    schedule.interval = 0ms;
    schedule.immediate = true;
    std::u8string error {};
    const http_ticket ticket { client->start_heartbeat(std::move(request), schedule, error) };
    REQUIRE(static_cast<bool>(ticket));

    std::this_thread::sleep_for(300ms);
    const std::size_t rounds { sink.size() };
    client->cancel(ticket);
    // 0을 넘기는 것이 서버를 두드리는 고리가 되지 않는다 (바닥은 100 ms다).
    //  - 상한이 3이 아니라 5인 것은 이 축이 재는 것이 **바닥이 있는가**이지 박자의
    //    정확도가 아니기 때문이다. 300 ms 창은 sleep_for의 늦잠만큼 길어지고,
    //    바닥이 없으면 회차는 수백 개가 되므로 5로도 그 둘은 갈린다.
    CHECK(rounds <= 5);
}

TEST_CASE("http client accepts a new request from inside deliver", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "again"); });

    collector sink {};
    std::unique_ptr<http_client> client {};
    std::atomic<bool> chained { false };

    http_client_config configuration {};
    configuration.use_system_proxy = false;
    const std::u8string next { server.url("/second") };
    configuration.deliver = [&sink, &client, &chained, next](http_response response) {
        if (chained.exchange(true) == false)
        {
            http_request follow {};
            follow.url = next;
            std::u8string error {};
            static_cast<void>(client->send(std::move(follow), error));
        }
        sink.add(std::move(response));
    };

    std::u8string error {};
    client = http_client::create(std::move(configuration), error);
    REQUIRE(client != nullptr);
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/first"))));
    REQUIRE(sink.wait_for(2, 10s));
    CHECK(sink.at(1).error.empty());
}

TEST_CASE("http client delivers on a thread of its own", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/thread"))));
    REQUIRE(sink.wait_for(1, 10s));

    // 파싱이 부르는 쪽 thread에서 돌지 않는다는 계약의 자리다.
    CHECK(sink.delivery_thread() != std::this_thread::get_id());
}

TEST_CASE("in_flight counts a request until its response is delivered", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    std::unique_ptr<http_client> client {};
    std::atomic<std::size_t> counted { 0 };

    http_client_config configuration {};
    configuration.use_system_proxy = false;
    configuration.deliver = [&sink, &client, &counted](http_response response) {
        // 배달하는 **동안**에도 그 요청은 아직 답이 나가지 않은 것이다. 선을 탄
        // 것만 세면 여기서 0이 나오고, 화면의 "보내는 중" 표시가 답보다 먼저 꺼진다.
        counted.store(client->in_flight());
        sink.add(std::move(response));
    };

    std::u8string error {};
    client = http_client::create(std::move(configuration), error);
    REQUIRE(client != nullptr);
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/count"))));
    REQUIRE(sink.wait_for(1, 10s));

    CHECK(counted.load() >= 1u);
    // 빠지는 것은 배달이 끝난 **뒤**다.
    std::this_thread::sleep_for(50ms);
    CHECK(client->in_flight() == 0u);
}

TEST_CASE("http client answers every concurrent request under its own ticket", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };

    SECTION("eight at once from one thread")
    {
        std::set<std::uint64_t> tickets {};
        for (int index { 0 }; index < 8; ++index)
        {
            const http_ticket ticket { send_to(*client, server.url("/many")) };
            REQUIRE(static_cast<bool>(ticket));
            CHECK(tickets.insert(ticket.value).second);
        }
        REQUIRE(sink.wait_for(8, 10s));

        for (const http_response& response : sink.snapshot())
            CHECK(tickets.count(response.ticket.value) == 1);
    }

    SECTION("five each from four threads")
    {
        // **worker는 표만 모은다.** Catch2의 단언은 여러 thread에서 함께 부르라고
        // 지은 것이 아니라, 여기서 CHECK를 하면 이 파일이 재는 것이 client가 아니라
        // 시험틀의 내부 상태가 된다 — 판정은 전부 합류한 뒤 이 thread의 몫이다.
        std::mutex guard {};
        std::vector<http_ticket> collected {};
        std::vector<std::thread> senders {};
        const std::u8string url { server.url("/many") };
        for (int worker { 0 }; worker < 4; ++worker)
        {
            senders.emplace_back([&client, &guard, &collected, &url] {
                for (int index { 0 }; index < 5; ++index)
                {
                    http_request request {};
                    request.url = url;
                    std::u8string error {};
                    const http_ticket ticket { client->send(std::move(request), error) };
                    std::lock_guard<std::mutex> lock { guard };
                    collected.push_back(ticket);
                }
            });
        }
        for (std::thread& sender : senders)
            sender.join();

        std::set<std::uint64_t> tickets {};
        for (const http_ticket ticket : collected)
        {
            CHECK(static_cast<bool>(ticket));
            CHECK(tickets.insert(ticket.value).second);
        }
        REQUIRE(sink.wait_for(20, 10s));
        CHECK(tickets.size() == 20);
        for (const http_response& response : sink.snapshot())
            CHECK(tickets.count(response.ticket.value) == 1);
    }
}

TEST_CASE("http client survives stop with requests in flight", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 300ms;
        return response;
    });

    // use-after-free는 한 번에 걸리지 않는다 — 되풀이가 창을 넓힌다.
    for (int round { 0 }; round < 50; ++round)
    {
        collector sink {};
        std::unique_ptr<http_client> client { make_client(sink) };
        for (int index { 0 }; index < 8; ++index)
            static_cast<void>(send_to(*client, server.url("/stop")));

        CHECK(client->in_flight() > 0);
        const auto started { std::chrono::steady_clock::now() };
        client->stop();
        sink.seal();
        const auto elapsed { std::chrono::steady_clock::now() - started };

        CHECK(elapsed < 3s);
        std::this_thread::sleep_for(20ms);
        CHECK(sink.late() == 0);
        client.reset();
    }
}

TEST_CASE("http client survives stop right after send without a proxy", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 300ms;
        return response;
    });

    // 요청이 아직 WinHTTP의 proxy 해석 RPC 안에 있을 때 닫는 자리다. 직접 연결을
    // 요청마다 못박지 않으면 WinHTTP가 그 RPC를 취소하다 RPCRT4 안에서 프로세스가
    // 끝난다. 확률로 걸리는 축이라 한 번의 통과가 증명은 아니다 — 고치기 전에는
    // 이 1000회가 병렬 부하 아래서 여덟 번에 한 번꼴로 죽었다. 의심되면
    // `ctest -R "survives stop right after send" --repeat until-fail:200`으로 되풀이한다.
    for (int round { 0 }; round < 1000; ++round)
    {
        collector sink {};
        std::unique_ptr<http_client> client { make_client(sink) };
        for (int index { 0 }; index < 8; ++index)
            static_cast<void>(send_to(*client, server.url("/stop")));

        client->stop();
        client.reset();
    }
}

TEST_CASE("stop answers in-flight requests as stopped", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 300ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    for (int index { 0 }; index < 3; ++index)
        static_cast<void>(send_to(*client, server.url("/stop")));

    client->stop();
    sink.seal();
    std::this_thread::sleep_for(100ms);

    // `stop()`은 아직 넘기지 않은 완료를 버리므로 **0개도 옳다** (닫히는 앱에게
    // 초 단위 디코딩을 시킬 이유가 없다). 못 박는 것은 "나간 답이 있다면 그 답이
    // 무엇이라 말하는가"다 — 취소도 시간 초과도 아닌 `stopped`여야 앱이 종료로
    // 끝난 일을 사고와 가른다.
    for (const http_response& response : sink.snapshot())
        CHECK(response.error.kind == http_error_kind::stopped);
    CHECK(sink.late() == 0);
}

TEST_CASE("concurrent stop calls both return with no late delivery", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 300ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    for (int index { 0 }; index < 8; ++index)
        static_cast<void>(send_to(*client, server.url("/stop")));

    // 둘이 함께 불러도 **둘 모두에게** "돌아온 뒤에는 deliver가 없다"가 참이어야
    // 한다. 뒤엣것이 앞엣것을 기다리지 않고 곧바로 돌아가면 그쪽 호출자는 아직
    // 살아 있는 배출구를 부수기 시작한다.
    std::atomic<int> returned { 0 };
    const auto stopper = [&client, &returned] {
        client->stop();
        returned.fetch_add(1);
    };
    std::thread first { stopper };
    std::thread second { stopper };
    first.join();
    second.join();
    sink.seal();

    std::this_thread::sleep_for(100ms);
    CHECK(returned.load() == 2);
    CHECK(sink.late() == 0);
}

TEST_CASE("http client stop is idempotent and closes the door", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) { return canned("text/plain", "ok"); });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    REQUIRE(static_cast<bool>(send_to(*client, server.url("/once"))));

    client->stop();
    client->stop();

    http_request request {};
    request.url = server.url("/after");
    std::u8string error {};
    CHECK(static_cast<bool>(client->send(std::move(request), error)) == false);
    CHECK(error == u8"client stopped");
    CHECK(client->in_flight() == 0);
}

TEST_CASE("http client cleans up from the destructor alone", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 300ms;
        return response;
    });

    collector sink {};
    std::unique_ptr<http_client> client { make_client(sink) };
    for (int index { 0 }; index < 4; ++index)
        static_cast<void>(send_to(*client, server.url("/drop")));

    const auto started { std::chrono::steady_clock::now() };
    client.reset();
    CHECK(std::chrono::steady_clock::now() - started < 3s);
}

TEST_CASE("http client shuts down quickly after cancel all", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response { canned("text/plain", "slow") };
        response.delay_before_headers = 500ms;
        return response;
    });

    collector sink {};
    const std::unique_ptr<http_client> client { make_client(sink) };
    for (int index { 0 }; index < 4; ++index)
        static_cast<void>(send_to(*client, server.url("/bye")));

    http_request beat {};
    beat.url = server.url("/beat");
    http_heartbeat schedule {};
    schedule.interval = 100ms;
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->start_heartbeat(std::move(beat), schedule, error)));

    // 종료 0단계 + 3단계의 실제 조합이다.
    const auto started { std::chrono::steady_clock::now() };
    client->cancel_all();
    client->stop();
    CHECK(std::chrono::steady_clock::now() - started < 3s);
    CHECK(client->in_flight() == 0);
}

TEST_CASE("Concurrent sends respect the transport limit", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.hang_forever = true;
        return response;
    });
    collector sink {};
    const auto client { make_client(sink, 1000ms, 1) };
    std::barrier start { 16 };
    std::atomic<int> accepted { 0 };
    std::vector<std::thread> senders {};
    for (int index = 0; index < 16; ++index)
        senders.emplace_back([&] {
            http_request request {};
            request.url = server.url("/limit");
            std::u8string error {};
            start.arrive_and_wait();
            if (client->send(std::move(request), error))
                accepted.fetch_add(1);
        });
    for (auto& sender : senders)
        sender.join();
    CHECK(accepted.load() == 1);
    client->cancel_all();
    REQUIRE(sink.wait_for(1, 10s));
}

TEST_CASE("Undelivered responses retain admission slots", "[net][client]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    collector sink {};
    std::mutex mutex {};
    std::condition_variable signal {};
    bool entered { false };
    bool release { false };
    http_client_config config {};
    config.use_system_proxy = false;
    config.max_pending_responses = 1;
    config.deliver = [&](http_response response) {
        {
            std::unique_lock lock { mutex };
            entered = true;
            signal.notify_all();
            signal.wait_for(lock, 10s, [&] { return release; });
        }
        sink.add(std::move(response));
    };
    std::u8string error {};
    const auto client { http_client::create(std::move(config), error) };
    REQUIRE(client != nullptr);
    REQUIRE(send_to(*client, server.url("/first")));
    bool callback_entered {};
    {
        std::unique_lock lock { mutex };
        callback_entered = signal.wait_for(lock, 10s, [&] { return entered; });
    }
    http_request second {};
    second.url = server.url("/second");
    const auto rejected { client->send(second, error) };
    {
        std::lock_guard lock { mutex };
        release = true;
    }
    signal.notify_all();
    REQUIRE(callback_entered);
    CHECK_FALSE(static_cast<bool>(rejected));
    REQUIRE(sink.wait_for(1, 10s));
    http_ticket next {};
    const auto deadline { std::chrono::steady_clock::now() + 5s };
    while (!next && std::chrono::steady_clock::now() < deadline)
    {
        next = client->send(second, error);
        std::this_thread::yield();
    }
    REQUIRE(next);
    REQUIRE(sink.wait_for(2, 10s));
}

TEST_CASE("Idle heartbeats share the pending response limit", "[net][client]")
{
    collector sink {};
    http_client_config config {};
    config.use_system_proxy = false;
    config.max_pending_responses = 2;
    config.deliver = [&](http_response response) { sink.add(std::move(response)); };
    std::u8string error {};
    const auto client { http_client::create(std::move(config), error) };
    REQUIRE(client != nullptr);
    http_request request {};
    request.url = u8"http://127.0.0.1:1/idle";
    http_heartbeat schedule {};
    schedule.immediate = false;
    schedule.interval = 1h;
    REQUIRE(client->start_heartbeat(request, schedule, error));
    REQUIRE(client->start_heartbeat(request, schedule, error));
    CHECK_FALSE(static_cast<bool>(client->start_heartbeat(request, schedule, error)));
    client->cancel_all();
    REQUIRE(sink.wait_for(2, 10s));
    for (const auto& response : sink.snapshot())
    {
        CHECK(response.last);
        CHECK(response.error.kind == http_error_kind::cancelled);
    }
}

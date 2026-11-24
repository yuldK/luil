#include "luil/net/http_client.h"

#include "luil/messaging/channel.h"
#include "luil/net/http_body.h"
#include "luil/net/http_media_type.h"
#include "luil/text/utf8_text.h"
#include "macos/macos_text.h"
#include "net/http_request_rules.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pthread.h>

// NSURLSession 위에 선 `http_client`다. 엔진(되풀이표·마감·pump thread·배달)은 Android 백엔드와 같은 모양이고,
// 전송만 NSURLSession의 data task다. 완료는 세션의 직렬 delegate 큐에서 오고, 몸을 풀고 `deliver`를 부르는 것은
// pump thread의 일이다 (http_client.h의 계약).
//
// NSURLSession이 막아 다른 백엔드와 다르게 답하는 자리다.
//  - **gzip·deflate·br은 언제나 시스템이 푼다.** 끄는 길이 없어 `decompress == false`여도 앱이 받는 바이트는 푼
//    것이다. 그 대신 Accept-Encoding을 `identity`로 적어 서버가 압축하지 않게 청하고, 풀어서 건넨 답에서는
//    Content-Encoding·Content-Length를 지운다 (앱이 두 번 풀지 않게, Android와 같다). deflate는 zlib 머리가
//    있든 없든 풀린다.
//  - 상태 줄의 이유 글(`http_response::reason`)을 내주지 않아 늘 비어 있다.
//  - 답의 헤더는 사전으로 온다. 온 차례가 남지 않고, 같은 이름의 줄들은 쉼표로 이어 붙은 한 줄이다.
//  - 구간 시한이 하나(`timeoutInterval`, 조각 사이의 쉼)뿐이라 연결·보내기·받기 상한 가운데 가장 긴 것을 건다.
//    `total_timeout`은 다른 백엔드처럼 pump가 잰다.
//  - 몸 있는 GET·HEAD는 선을 건드리지 않고 실패한다 (`-1103`). 그 뜻이 드러나지 않아 Android처럼 `send`에서
//    거절한다.
namespace luil::net {
    namespace {
        using namespace std::chrono_literals;

        // pump가 할 일이 없을 때 자는 상한이다 (Windows·Android 백엔드와 같은 값이다).
        constexpr std::chrono::milliseconds pump_idle_wait { 250 };
        // 완료가 끊이지 않을 때도 마감·회차·끊기 재시도를 이만큼마다는 훑는다.
        constexpr std::chrono::milliseconds pump_sweep_interval { 50 };
        // `stop()`이 기다리는 동안 끊기를 다시 거는 간격이다.
        constexpr std::chrono::milliseconds stop_retry_interval { 50 };
        // 미리 잡는 몸 크기의 상한이다. `Content-Length: 4 GB`가 메모리를 예약하게 두지 않는다.
        constexpr std::size_t body_reserve_limit_bytes { 1024u * 1024u };
        // 구간 시한을 걸지 않을 때의 값이다. NSURLSession의 0은 "기본값(60초)"이라 0으로 끌 수 없다.
        constexpr NSTimeInterval unlimited_seconds { 365.0 * 24.0 * 60.0 * 60.0 };

        std::atomic<std::uint64_t> request_counter { 0 };

        // 표와 요청 번호를 함께 낸다. 0은 "받아들여지지 않았다"는 뜻이라 1부터 준다.
        [[nodiscard]] std::uint64_t issue_request_id() noexcept
        {
            return request_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        // delegate 큐가 지어 pump로 건너가는 완료 하나다. 몸을 푸는 것은 pump의 일이다.
        struct completion
        {
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            int status_code { 0 };
            std::u8string reason {};
            std::u8string final_url {};
            std::vector<http_header> headers {};
            std::vector<std::uint8_t> body {};
            http_error error {};
            http_body_parse_options parse {};
            std::chrono::milliseconds elapsed { 0 };
        };

        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        [[nodiscard]] bool same_ascii_ci(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
                if (lower_ascii(left[index]) != lower_ascii(right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool is_scheme_character(const char8_t byte) noexcept
        {
            return (byte >= u8'a' && byte <= u8'z') || (byte >= u8'A' && byte <= u8'Z') || (byte >= u8'0' && byte <= u8'9') || byte == u8'+' || byte == u8'-' || byte == u8'.';
        }

        // URL의 출처다 (scheme·host·port). 재지정이 다른 출처로 가는지 견주는 데 쓴다.
        struct url_origin
        {
            bool valid { false };
            bool secure { false };
            std::u8string host {};
            unsigned int port { 0 };
        };

        // 절대 http·https URL을 출처까지만 가른다. 경로와 질의는 NSURL이 읽는다 (Android 백엔드와 같은 규칙이다).
        //
        // 선을 건드리기 **전**에 거절할 것을 가르는 자리다 (`send`가 동기 실패로 답한다).
        [[nodiscard]] url_origin parse_origin(const std::u8string_view url, http_error& failure)
        {
            url_origin origin {};
            const std::size_t scheme_end { url.find(u8':') };
            const bool has_scheme { scheme_end != std::u8string_view::npos && scheme_end > 0 && std::all_of(url.begin(), url.begin() + static_cast<std::ptrdiff_t>(scheme_end), is_scheme_character) };
            if (has_scheme == false)
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL is not an absolute http or https URL.");
                return origin;
            }

            const std::u8string_view scheme { url.substr(0, scheme_end) };
            if (same_ascii_ci(scheme, u8"https"))
                origin.secure = true;
            else if (same_ascii_ci(scheme, u8"http") == false)
            {
                failure = make_http_error(http_error_kind::unsupported_scheme, u8"Only http and https URLs are supported.");
                return origin;
            }

            const std::u8string_view rest { url.substr(scheme_end + 1u) };
            if (rest.starts_with(u8"//") == false)
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL has no authority.");
                return origin;
            }
            for (const char8_t byte : url)
            {
                if (byte <= 0x20u || byte == 0x7Fu)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL contains a space or a control character.");
                    return origin;
                }
            }

            std::u8string_view authority { rest.substr(2) };
            authority = authority.substr(0, authority.find_first_of(u8"/?#"));
            if (const std::size_t at { authority.rfind(u8'@') }; at != std::u8string_view::npos)
                authority.remove_prefix(at + 1u);

            std::u8string_view host { authority };
            std::u8string_view port_text {};
            if (host.starts_with(u8'['))
            {
                const std::size_t close { host.find(u8']') };
                if (close == std::u8string_view::npos)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL host is not valid.");
                    return origin;
                }
                if (close + 1u < host.size())
                {
                    if (host[close + 1u] != u8':')
                    {
                        failure = make_http_error(http_error_kind::invalid_url, u8"The URL host is not valid.");
                        return origin;
                    }
                    port_text = host.substr(close + 2u);
                }
                host = host.substr(0, close + 1u);
            }
            else if (const std::size_t colon { host.rfind(u8':') }; colon != std::u8string_view::npos)
            {
                port_text = host.substr(colon + 1u);
                host = host.substr(0, colon);
            }
            if (host.empty())
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL has no host.");
                return origin;
            }

            origin.port = origin.secure ? 443u : 80u;
            if (port_text.empty() == false)
            {
                unsigned int port { 0 };
                const auto* const first { reinterpret_cast<const char*>(port_text.data()) };
                const auto [end, error] { std::from_chars(first, first + port_text.size(), port) };
                if (error != std::errc {} || end != first + port_text.size() || port == 0 || port > 65535u)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL port is not valid.");
                    return origin;
                }
                origin.port = port;
            }
            origin.host = host;
            origin.valid = true;
            return origin;
        }

        [[nodiscard]] bool same_origin(const url_origin& left, const url_origin& right) noexcept
        {
            return left.valid && right.valid && left.secure == right.secure && left.port == right.port && same_ascii_ci(left.host, right.host);
        }

        [[nodiscard]] std::size_t parse_content_length(const std::u8string_view text) noexcept
        {
            std::size_t value { 0 };
            const auto* const first { reinterpret_cast<const char*>(text.data()) };
            const auto [end, error] { std::from_chars(first, first + text.size(), value) };
            return error == std::errc {} && end == first + text.size() ? value : 0u;
        }

        // NSURLSession이 스스로 푸는 부호화인가. 이런 답의 바이트는 이미 풀려 온다.
        [[nodiscard]] bool system_decoded(const std::u8string_view encoding) noexcept
        {
            return same_ascii_ci(encoding, u8"gzip") || same_ascii_ci(encoding, u8"x-gzip") || same_ascii_ci(encoding, u8"deflate") || same_ascii_ci(encoding, u8"br");
        }

        // 구간 시한이다. 셋 가운데 가장 긴 것을 건다 — 짧은 것을 걸면 긴 상한을 허락한 구간이 그보다 먼저 끊긴다.
        [[nodiscard]] NSTimeInterval idle_timeout(const http_request& request) noexcept
        {
            const std::chrono::milliseconds longest { std::max({ request.connect_timeout, request.send_timeout, request.receive_timeout }) };
            if (longest <= 0ms)
                return unlimited_seconds;
            return std::min(unlimited_seconds, static_cast<NSTimeInterval>(longest.count()) / 1000.0);
        }

        // 원본 코드를 십진으로 붙인다. NSURLError는 음수라 16진보다 문서의 표와 견주기 쉽다.
        [[nodiscard]] http_error make_native_error(const http_error_kind kind, const std::u8string_view message, NSError* const error)
        {
            http_error failure { make_http_error(kind, message) };
            const NSInteger code { error.code };
            failure.native_error = static_cast<unsigned long>(code);
            failure.message += u8" (";
            failure.message += macos::from_ns_string(error.domain);
            failure.message += u8"=";
            failure.message += macos::widen(std::to_string(static_cast<long long>(code)));
            failure.message += u8")";
            return failure;
        }

        // NSURLSession의 실패를 오류 값으로 옮긴다. 같은 일에 Windows·Android와 같은 갈래가 나오게 한다.
        [[nodiscard]] http_error translate_error(NSError* const error)
        {
            if ([error.domain isEqualToString:NSURLErrorDomain])
            {
                switch (error.code)
                {
                case NSURLErrorCannotFindHost:
                case NSURLErrorDNSLookupFailed:
                    return make_native_error(http_error_kind::name_not_resolved, u8"The server name could not be resolved.", error);
                case NSURLErrorCannotConnectToHost:
                case NSURLErrorNotConnectedToInternet:
                case NSURLErrorInternationalRoamingOff:
                case NSURLErrorCallIsActive:
                case NSURLErrorDataNotAllowed:
                    return make_native_error(http_error_kind::cannot_connect, u8"Failed to connect to the server.", error);
                case NSURLErrorNetworkConnectionLost:
                case NSURLErrorZeroByteResource:
                    return make_native_error(http_error_kind::connection_lost, u8"The connection closed before the response was complete.", error);
                case NSURLErrorBadServerResponse:
                    return make_native_error(http_error_kind::connection_lost, u8"The server sent a malformed response.", error);
                case NSURLErrorTimedOut:
                    return make_native_error(http_error_kind::timed_out, u8"The request timed out.", error);
                case NSURLErrorHTTPTooManyRedirects:
                case NSURLErrorRedirectToNonExistentLocation:
                    return make_native_error(http_error_kind::too_many_redirects, u8"The redirect could not be followed.", error);
                case NSURLErrorUnsupportedURL:
                    return make_native_error(http_error_kind::unsupported_scheme, u8"The URL scheme is not http or https.", error);
                case NSURLErrorBadURL:
                    return make_native_error(http_error_kind::invalid_url, u8"The request URL could not be parsed.", error);
                case NSURLErrorCancelled:
                    return make_native_error(http_error_kind::cancelled, u8"The request was cancelled.", error);
                case NSURLErrorDataLengthExceedsMaximum:
                    return make_native_error(http_error_kind::body_too_large, u8"The response body exceeds the system limit.", error);
                case NSURLErrorAppTransportSecurityRequiresSecureConnection:
                    return make_native_error(http_error_kind::secure_failure, u8"App Transport Security does not permit cleartext HTTP to this host.", error);
                case NSURLErrorSecureConnectionFailed:
                case NSURLErrorServerCertificateHasBadDate:
                case NSURLErrorServerCertificateUntrusted:
                case NSURLErrorServerCertificateHasUnknownRoot:
                case NSURLErrorServerCertificateNotYetValid:
                case NSURLErrorClientCertificateRejected:
                case NSURLErrorClientCertificateRequired:
                    return make_native_error(http_error_kind::secure_failure, u8"The secure connection could not be established.", error);
                default:
                    break;
                }
            }
            else if ([error.domain isEqualToString:NSPOSIXErrorDomain])
            {
                switch (error.code)
                {
                case ECONNREFUSED:
                case EHOSTUNREACH:
                case ENETUNREACH:
                    return make_native_error(http_error_kind::cannot_connect, u8"Failed to connect to the server.", error);
                case ECONNRESET:
                case ENOTCONN:
                case EPIPE:
                    return make_native_error(http_error_kind::connection_lost, u8"The connection closed before the response was complete.", error);
                case ETIMEDOUT:
                    return make_native_error(http_error_kind::timed_out, u8"The request timed out.", error);
                default:
                    break;
                }
            }
            return make_native_error(http_error_kind::system_error, u8"The request failed.", error);
        }

        // 요청 하나의 상태다. 소유자는 engine의 두 지도(요청 번호·task 번호)다.
        //
        // 결말 칸(`guard` 아래)만 여러 thread가 만진다. 나머지 진행 칸은 delegate 큐(직렬)에서만 만진다.
        struct request_state
        {
            std::uint64_t id { 0 };
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            http_request request {};
            // 검사를 마친 요청 헤더다 (Content-Type 기본값까지 들었다). 다른 출처로 옮기면 줄어든다.
            std::vector<http_header> headers {};
            url_origin origin {};
            std::chrono::steady_clock::time_point started {};
            std::chrono::steady_clock::time_point deadline {};
            bool has_deadline { false };
            // 등록 전에 한 번 적고 그 뒤로는 읽기만 한다. `cancel`은 어느 thread에서 불러도 된다.
            NSURLSessionDataTask* task { nil };

            // delegate 큐의 칸이다.
            http_method method { http_method::get };
            bool current_secure { false };
            bool headers_cleared { false };
            int redirects { 0 };
            completion result {};

            std::mutex guard {};
            bool outcome_latched { false };
            http_error outcome {};
            bool finished { false };
        };

        // 결말을 걸쇠에 적는다. **먼저 적은 것이 이긴다.** 적었으면 참이다.
        bool latch(request_state& state, const http_error& outcome) noexcept
        {
            std::lock_guard lock { state.guard };
            if (state.outcome_latched || state.finished)
                return false;
            state.outcome_latched = true;
            state.outcome = outcome;
            return true;
        }

        [[nodiscard]] bool is_latched(request_state& state) noexcept
        {
            std::lock_guard lock { state.guard };
            return state.outcome_latched;
        }

        // 걸쇠가 적혀 있고 아직 끝나지 않았으면 task를 접는다. 접힌 task는 `cancelled`로 끝나고, 답은 걸쇠의 사유다.
        void cancel_if_latched(request_state& state) noexcept
        {
            {
                std::lock_guard lock { state.guard };
                if (state.outcome_latched == false || state.finished)
                    return;
            }
            @autoreleasepool
            {
                [state.task cancel];
            }
        }

        // NSURLSession은 몸을 실으면 GET·HEAD를 보내지 않고 `-1103`(자원이 상한을 넘었다)으로 실패한다.
        // 뜻이 어긋난 오류가 답으로 가지 않게 선을 건드리기 전에 거절한다 (Android와 같은 자리다).
        [[nodiscard]] bool body_fits_method(const http_request& request, http_error& failure)
        {
            if (request.body.empty() || (request.method != http_method::get && request.method != http_method::head))
                return true;
            failure = make_http_error(http_error_kind::system_error, u8"NSURLSession cannot send a body with GET or HEAD.");
            return false;
        }

        // delegate 큐가 부르는 자리다. engine이 이것을 구현한다 (engine의 이름은 http_client 밖에서 쓸 수 없다).
        class session_events
        {
        public:
            [[nodiscard]] virtual NSURLSessionResponseDisposition on_response(NSURLSessionTask* task, NSURLResponse* response) noexcept = 0;
            virtual void on_data(NSURLSessionTask* task, NSData* data) noexcept = 0;
            [[nodiscard]] virtual NSURLRequest* on_redirect(NSURLSessionTask* task, NSHTTPURLResponse* response, NSURLRequest* proposed) noexcept = 0;
            virtual void on_complete(NSURLSessionTask* task, NSError* error) noexcept = 0;

        protected:
            session_events() = default;
            session_events(const session_events&) = default;
            session_events& operator=(const session_events&) = default;
            ~session_events() = default;
        };

        // delegate와 engine 사이의 다리다. delegate는 세션이 무효가 될 때까지 살아 있으므로 engine을 직접 들지 않는다.
        // `stop()`이 `owner`를 지운 뒤로는 어느 callback도 engine에 닿지 않는다 — 그래서 `stop_budget`을 넘겨도
        // engine을 부술 수 있다 (Android처럼 일부러 흘려보낼 까닭이 없다).
        struct session_bridge
        {
            std::mutex mutex {};
            session_events* owner { nullptr };
        };
    }
}

// NSURLSession의 data delegate다. 부를 것은 모두 다리 너머로 넘긴다.
@interface LuilHttpSessionDelegate : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithBridge:(std::shared_ptr<luil::net::session_bridge>)bridge;
@end

@implementation LuilHttpSessionDelegate {
    std::shared_ptr<luil::net::session_bridge> _bridge;
}

- (instancetype)initWithBridge:(std::shared_ptr<luil::net::session_bridge>)bridge
{
    self = [super init];
    if (self != nil)
        _bridge = std::move(bridge);
    return self;
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
    static_cast<void>(session);
    NSURLSessionResponseDisposition disposition { NSURLSessionResponseCancel };
    {
        const std::lock_guard lock { _bridge->mutex };
        if (_bridge->owner != nullptr)
            disposition = _bridge->owner->on_response(task, response);
    }
    completionHandler(disposition);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data
{
    static_cast<void>(session);
    const std::lock_guard lock { _bridge->mutex };
    if (_bridge->owner != nullptr)
        _bridge->owner->on_data(task, data);
}

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler
{
    static_cast<void>(session);
    NSURLRequest* next { nil };
    {
        const std::lock_guard lock { _bridge->mutex };
        if (_bridge->owner != nullptr)
            next = _bridge->owner->on_redirect(task, response, request);
    }
    completionHandler(next);
}

- (void)URLSession:(NSURLSession*)session
                 dataTask:(NSURLSessionDataTask*)task
        willCacheResponse:(NSCachedURLResponse*)proposedResponse
        completionHandler:(void (^)(NSCachedURLResponse*))completionHandler
{
    static_cast<void>(session);
    static_cast<void>(task);
    static_cast<void>(proposedResponse);
    // 답을 담아 두지 않는다. 같은 URL의 다음 요청도 선을 탄다 (다른 백엔드와 같다).
    completionHandler(nil);
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    static_cast<void>(session);
    const std::lock_guard lock { _bridge->mutex };
    if (_bridge->owner != nullptr)
        _bridge->owner->on_complete(task, error);
}

@end

namespace luil::net {
    // NSURLSession 위에 선 실체다.
    //
    // 받아들인 요청마다 data task 하나를 띄우고, 동시 수는 `max_in_flight`가 받기 전에 막는다. pump thread 하나가
    // 다른 백엔드와 같은 일을 한다: 몸을 풀고, `deliver`를 부르고, 시계를 돈다 (전체 마감과 되풀이 회차, 끊기 재시도).
    struct http_client::engine final : session_events
    {
        struct schedule_record
        {
            http_ticket ticket {};
            http_request request {};
            http_heartbeat schedule {};
            std::chrono::steady_clock::time_point next {};
            std::uint64_t rounds { 0 };
            // 지금 날아가 있는 회차의 요청 번호다 (없으면 0).
            std::uint64_t in_flight_id { 0 };
        };

        struct pending_round
        {
            http_request request {};
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            std::uint64_t id { 0 };
        };

        explicit engine(http_client_config configuration)
            : configuration_ { std::move(configuration) }
            , completions_ { messaging::channel_options { std::numeric_limits<std::size_t>::max(), messaging::overflow_policy::reject_newest, {} } }
        {
        }

        engine(const engine&) = delete;
        engine(engine&&) = delete;
        engine& operator=(const engine&) = delete;
        engine& operator=(engine&&) = delete;

        ~engine()
        {
            static_cast<void>(stop());
        }

        [[nodiscard]] bool open(std::u8string& error)
        {
            if (text::utf8_is_valid(configuration_.user_agent) == false)
            {
                error = u8"The user agent is not valid UTF-8.";
                return false;
            }
            user_agent_ = macos::make_ns_string(configuration_.user_agent);
            if (user_agent_ == nil)
            {
                error = u8"The user agent could not be converted.";
                return false;
            }

            @autoreleasepool
            {
                // 저장소 셋(cache·cookie·자격 증명)을 모두 끈다. 앱이 적은 헤더가 선에 나가는 전부다 — 다른 출처로 옮길
                // 때 지운 Cookie를 세션의 저장소가 다시 붙이면 그 규칙이 뜻을 잃는다.
                NSURLSessionConfiguration* const settings { [NSURLSessionConfiguration ephemeralSessionConfiguration] };
                settings.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
                settings.URLCache = nil;
                settings.HTTPCookieStorage = nil;
                settings.HTTPShouldSetCookies = NO;
                settings.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
                settings.URLCredentialStorage = nil;
                settings.waitsForConnectivity = NO;
                // 전체 시한은 우리가 잰다 (`total_timeout`). 시스템의 자원 시한(기본 7일)에 먼저 걸리지 않게 한다.
                settings.timeoutIntervalForResource = unlimited_seconds;
                // 숨은 큐를 두지 않는다. host마다의 연결 상한이 `max_in_flight`보다 작으면 받은 요청이 세션 안에서
                // 줄을 서고, 그만큼 `in_flight()`와 시한의 뜻이 어긋난다.
                settings.HTTPMaximumConnectionsPerHost = static_cast<NSInteger>(std::clamp<std::size_t>(configuration_.max_in_flight, 1u, 1024u));
                // 빈 사전은 "proxy 없음"이다.
                if (configuration_.use_system_proxy == false)
                    settings.connectionProxyDictionary = @{};

                bridge_ = std::make_shared<session_bridge>();
                bridge_->owner = this;
                NSOperationQueue* const queue { [[NSOperationQueue alloc] init] };
                queue.maxConcurrentOperationCount = 1;
                queue.name = @"luil-http-delegate";
                LuilHttpSessionDelegate* const delegate { [[LuilHttpSessionDelegate alloc] initWithBridge:bridge_] };
                session_ = [NSURLSession sessionWithConfiguration:settings delegate:delegate delegateQueue:queue];
            }
            if (session_ == nil)
            {
                error = u8"The URL session could not be created.";
                return false;
            }

            transcode_ = configuration_.transcode ? configuration_.transcode : codepage_text_transcoder();
            try
            {
                pump_ = std::thread { [this] { run_pump(); } };
            }
            catch (const std::system_error&)
            {
                error = u8"The client thread could not start.";
                return false;
            }
            pump_id_ = pump_.get_id();
            return true;
        }

        [[nodiscard]] http_ticket send(http_request request, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            if (reserve_ticket(error) == false)
                return {};
            const std::uint64_t id { issue_request_id() };
            const http_ticket ticket { id };
            http_error failure {};
            if (start_request(std::move(request), ticket, 0, id, failure) == false)
            {
                pending_tickets_.fetch_sub(1);
                error = failure.message;
                return {};
            }
            return ticket;
        }

        [[nodiscard]] http_ticket start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            // URL·헤더는 **먼저** 본다 — 잘못된 요청은 표를 받지 못한다.
            http_error failure {};
            std::vector<http_header> headers {};
            if (parse_origin(request.url, failure).valid == false || check_request_method(request, failure) == false || collect_request_headers(request, headers, failure) == false
                || body_fits_method(request, failure) == false)
            {
                error = failure.message;
                return {};
            }

            if (schedule.interval < http_minimum_heartbeat_interval)
                schedule.interval = http_minimum_heartbeat_interval;

            if (reserve_ticket(error) == false)
                return {};

            const http_ticket ticket { issue_request_id() };
            schedule_record record {};
            record.ticket = ticket;
            record.request = std::move(request);
            record.schedule = schedule;
            record.next = std::chrono::steady_clock::now() + (schedule.immediate ? 0ms : schedule.interval);
            {
                std::lock_guard lock { schedules_mutex_ };
                if (stopping_.load())
                {
                    pending_tickets_.fetch_sub(1);
                    error = u8"client stopped";
                    return {};
                }
                schedules_.emplace(ticket.value, std::move(record));
            }
            return ticket;
        }

        void cancel(const http_ticket ticket) noexcept
        {
            if (ticket.value == 0)
                return;

            bool heartbeat { false };
            schedule_record dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(ticket.value) };
                if (entry != schedules_.end())
                {
                    heartbeat = true;
                    dropped = std::move(entry->second);
                    schedules_.erase(entry);
                }
            }

            // 회차가 하나도 나가 있지 않은 되풀이는 답할 것이 없으니 마지막 답을 짓는다.
            // 회차 번호가 적혀 있으면 그 회차의 답이 오고, 되풀이표가 비었으므로 그것이
            // 마지막으로 찍힌다 (Windows·Android 백엔드와 같은 규칙이다).
            if (heartbeat && dropped.in_flight_id == 0)
            {
                post_synthetic(dropped.ticket, dropped.rounds, dropped.request, cancelled_error());
                return;
            }

            const std::uint64_t target { heartbeat ? dropped.in_flight_id : ticket.value };
            if (const std::shared_ptr<request_state> live { borrow_request(target) }; live != nullptr)
                abort(*live, cancelled_error());
        }

        void cancel_all() noexcept
        {
            std::vector<schedule_record> dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                    dropped.push_back(std::move(entry.second));
                schedules_.clear();
            }
            for (const schedule_record& record : dropped)
                if (record.in_flight_id == 0)
                    post_synthetic(record.ticket, record.rounds, record.request, cancelled_error());
            abort_all(cancelled_error());
        }

        [[nodiscard]] std::size_t in_flight() const noexcept
        {
            return outstanding_.load();
        }

        // 거짓이면 예산을 넘긴 것이다. 그래도 다리를 끊으므로 engine은 부술 수 있다.
        [[nodiscard]] bool stop() noexcept
        {
            // `deliver` 안에서 부르면 자기 자신을 join하게 된다 (http_client.h의 계약).
            assert(std::this_thread::get_id() != pump_id_ && "http_client::stop must not be called from deliver");
            std::lock_guard serialize { stop_mutex_ };
            if (stopped_.load())
                return true;

            // 1. 새 요청을 거절한다.
            stopping_.store(true);
            // 2. 되풀이표를 비우고 pump를 **먼저** 멈춘다. 배달도 여기서 끝난다.
            {
                std::lock_guard lock { schedules_mutex_ };
                schedules_.clear();
            }
            delivering_.store(false);
            completions_.close();
            if (pump_.joinable())
                pump_.join();
            outstanding_.store(0);

            // 3. 날아가 있는 요청을 전부 접고 task가 다 끝나기를 기다린다. 등록 직후의 접기는 빗나갈 수 있어
            //    기다리는 동안 다시 건다.
            const http_error reason { make_http_error(http_error_kind::stopped, u8"The client was stopped.") };
            abort_all(reason);
            const auto limit { std::chrono::steady_clock::now() + configuration_.stop_budget };
            bool finished_in_time { false };
            while (true)
            {
                {
                    std::unique_lock lock { in_flight_mutex_ };
                    const auto now { std::chrono::steady_clock::now() };
                    const auto slice { std::min(limit, now + stop_retry_interval) };
                    finished_in_time = in_flight_empty_.wait_until(lock, slice, [this] { return in_flight_.empty(); });
                }
                if (finished_in_time || std::chrono::steady_clock::now() >= limit)
                    break;
                abort_all(reason);
            }

            // 4. 다리를 끊고 세션을 닫는다. 이 뒤로 delegate가 불려도 engine에 닿지 않는다.
            if (bridge_ != nullptr)
            {
                std::lock_guard lock { bridge_->mutex };
                bridge_->owner = nullptr;
            }
            @autoreleasepool
            {
                [session_ invalidateAndCancel];
                session_ = nil;
            }
            stopped_.store(true);
            return finished_in_time;
        }

        // ---- delegate 큐의 자리다 (다리의 잠금 안에서 불린다) ----

        NSURLSessionResponseDisposition on_response(NSURLSessionTask* const task, NSURLResponse* const response) noexcept override
        {
            const std::shared_ptr<request_state> state { find_task(task) };
            if (state == nullptr || is_latched(*state))
                return NSURLSessionResponseCancel;
            if ([response isKindOfClass:[NSHTTPURLResponse class]] == NO)
            {
                static_cast<void>(latch(*state, make_http_error(http_error_kind::system_error, u8"The response is not an HTTP response.")));
                return NSURLSessionResponseCancel;
            }

            try
            {
                NSHTTPURLResponse* const http { static_cast<NSHTTPURLResponse*>(response) };
                completion& result { state->result };
                result.status_code = static_cast<int>(http.statusCode);
                result.headers.clear();
                [http.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL* stop) {
                    static_cast<void>(stop);
                    if ([key isKindOfClass:[NSString class]] && [value isKindOfClass:[NSString class]])
                        result.headers.push_back(http_header { macos::from_ns_string(key), macos::from_ns_string(value) });
                }];

                // 몸이 오지 않는 답이다. HEAD와 1xx·204·304의 `Content-Length`는 오지 않을 몸의 크기다.
                const int status { result.status_code };
                const bool informational { status >= 100 && status < 200 };
                if (state->method == http_method::head || status == 204 || status == 304 || informational)
                    return NSURLSessionResponseAllow;

                // 압축되지 않은 답의 길이가 상한을 넘으면 한 바이트도 받지 않는다. 시스템이 푸는 답의 길이는 푼
                // 크기가 아니라 여기서 견주지 않고, 받는 동안 푼 바이트로 잰다.
                const bool decoded { system_decoded(find_header(result.headers, u8"content-encoding")) };
                const std::size_t content_length { parse_content_length(find_header(result.headers, u8"content-length")) };
                if (decoded == false && content_length > state->request.max_body_bytes)
                {
                    static_cast<void>(latch(*state, make_http_error(http_error_kind::body_too_large, u8"The response Content-Length exceeds max_body_bytes.")));
                    return NSURLSessionResponseCancel;
                }
                if (decoded)
                {
                    // 앱이 받는 바이트는 푼 것이다. 압축된 길이와 부호화 표시는 남기지 않는다.
                    std::erase_if(result.headers, [](const http_header& header) { return same_ascii_ci(header.name, u8"content-encoding") || same_ascii_ci(header.name, u8"content-length"); });
                }
                else
                {
                    result.body.reserve(std::min(content_length, body_reserve_limit_bytes));
                }
            }
            catch (...)
            {
                static_cast<void>(latch(*state, make_http_error(http_error_kind::system_error, u8"The response could not be stored.")));
                return NSURLSessionResponseCancel;
            }
            return NSURLSessionResponseAllow;
        }

        void on_data(NSURLSessionTask* const task, NSData* const data) noexcept override
        {
            const std::shared_ptr<request_state> state { find_task(task) };
            if (state == nullptr || is_latched(*state))
                return;
            std::vector<std::uint8_t>& body { state->result.body };
            const std::size_t limit { state->request.max_body_bytes };
            // 시스템이 푼 뒤의 크기라 이쪽이 진짜 관문이다.
            if (static_cast<std::size_t>(data.length) > limit - std::min(body.size(), limit))
            {
                static_cast<void>(latch(*state, make_http_error(http_error_kind::body_too_large, u8"The response body exceeds max_body_bytes.")));
                cancel_if_latched(*state);
                return;
            }
            try
            {
                [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL* stop) {
                    static_cast<void>(stop);
                    const auto* const first { static_cast<const std::uint8_t*>(bytes) };
                    body.insert(body.end(), first, first + range.length);
                }];
            }
            catch (...)
            {
                static_cast<void>(latch(*state, make_http_error(http_error_kind::system_error, u8"The response body could not be stored.")));
                cancel_if_latched(*state);
            }
        }

        // 재지정을 직접 고른다. 시스템의 자동 재지정은 몸 있는 요청을 따라가지 않는 규칙·다른 출처로 갈 때 헤더를
        // 지우는 규칙·동사를 바꾸는 규칙을 모른다 (Android 백엔드와 같은 규칙이다). nil이면 이 3xx가 답이다.
        NSURLRequest* on_redirect(NSURLSessionTask* const task, NSHTTPURLResponse* const response, NSURLRequest* const proposed) noexcept override
        {
            const std::shared_ptr<request_state> state { find_task(task) };
            if (state == nullptr || is_latched(*state))
                return nil;
            if (state->request.redirects != http_redirect_policy::follow || state->request.body.empty() == false || proposed.URL == nil)
                return nil;

            try
            {
                std::u8string next_url { macos::from_ns_string(proposed.URL.absoluteString) };
                http_error ignored {};
                const url_origin next_origin { parse_origin(next_url, ignored) };
                // https에서 http로 내려가는 걸음과 http·https가 아닌 곳은 따라가지 않는다. 그 3xx가 답이다.
                if (next_origin.valid == false || (state->current_secure && next_origin.secure == false))
                    return nil;

                ++state->redirects;
                if (state->redirects > state->request.max_redirects)
                {
                    static_cast<void>(latch(*state, make_http_error(http_error_kind::too_many_redirects, u8"The response redirected more times than max_redirects allows.")));
                    cancel_if_latched(*state);
                    return nil;
                }

                // 303은 GET으로 바꾼다 (RFC 9110 15.4.4). 301·302의 POST도 브라우저처럼 GET으로 바꾼다. 확장 메서드는
                // 바꾸지 않고, 303이면 따라가지 않는다 (`redirected_method`).
                const std::optional<http_method> next_method { redirected_method(state->method, static_cast<int>(response.statusCode)) };
                if (next_method.has_value() == false)
                    return nil;

                // 처음 출처를 벗어나는 첫 걸음에서 앱 헤더를 지운다 (http_redirect_policy::follow).
                if (state->headers_cleared == false && state->headers.empty() == false && same_origin(state->origin, next_origin) == false)
                {
                    std::erase_if(state->headers, [](const http_header& header) { return http_header_crosses_origins(header.name) == false; });
                    state->headers_cleared = true;
                }

                http_error failure {};
                NSURLRequest* const next { make_request(proposed.URL, *state, *next_method, failure) };
                if (next == nil)
                    return nil;
                state->method = *next_method;
                state->current_secure = next_origin.secure;
                state->result.final_url = std::move(next_url);
                state->result.status_code = 0;
                state->result.headers.clear();
                state->result.body.clear();
                return next;
            }
            catch (...)
            {
                return nil;
            }
        }

        void on_complete(NSURLSessionTask* const task, NSError* const error) noexcept override
        {
            const std::shared_ptr<request_state> state { take_task(task) };
            if (state == nullptr)
                return;

            http_error failure {};
            if (error != nil)
            {
                try
                {
                    failure = translate_error(error);
                }
                catch (...)
                {
                    failure.kind = http_error_kind::system_error;
                }
            }
            {
                // 걸쇠에 먼저 적힌 사유가 이긴다 — 우리가 접은 task는 "취소됐다"로 돌아오지만 답은 접은
                // 이유(취소·마감·정지·몸 상한)여야 한다.
                std::lock_guard lock { state->guard };
                if (state->outcome_latched)
                    failure = state->outcome;
                state->outcome_latched = true;
                state->outcome = failure;
                state->finished = true;
            }
            completion result { std::move(state->result) };
            result.error = std::move(failure);
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state->started);
            // 반쪽 몸은 "원래 그런 답"과 구별되지 않아 넘기지 않는다.
            if (result.error.empty() == false)
                result.body.clear();
            finish_request(state->id, std::move(result));
        }

    private:
        [[nodiscard]] static http_error cancelled_error()
        {
            return make_http_error(http_error_kind::cancelled, u8"The request was cancelled.");
        }

        [[nodiscard]] std::shared_ptr<request_state> borrow_request(const std::uint64_t id) const
        {
            std::lock_guard lock { in_flight_mutex_ };
            const auto entry { in_flight_.find(id) };
            return entry == in_flight_.end() ? nullptr : entry->second;
        }

        [[nodiscard]] std::shared_ptr<request_state> find_task(NSURLSessionTask* const task) const noexcept
        {
            std::lock_guard lock { in_flight_mutex_ };
            const auto entry { tasks_.find(task.taskIdentifier) };
            return entry == tasks_.end() ? nullptr : entry->second;
        }

        [[nodiscard]] std::shared_ptr<request_state> take_task(NSURLSessionTask* const task) noexcept
        {
            std::lock_guard lock { in_flight_mutex_ };
            const auto entry { tasks_.find(task.taskIdentifier) };
            if (entry == tasks_.end())
                return nullptr;
            std::shared_ptr<request_state> state { std::move(entry->second) };
            tasks_.erase(entry);
            return state;
        }

        // 결말을 적고 task를 접는다.
        void abort(request_state& state, const http_error& reason) noexcept
        {
            static_cast<void>(latch(state, reason));
            cancel_if_latched(state);
        }

        [[nodiscard]] std::vector<std::shared_ptr<request_state>> live_requests() const
        {
            std::vector<std::shared_ptr<request_state>> live {};
            std::lock_guard lock { in_flight_mutex_ };
            live.reserve(in_flight_.size());
            for (const auto& entry : in_flight_)
                live.push_back(entry.second);
            return live;
        }

        // 살아 있는 요청 전부에 사유를 걸고 접는다. 이미 걸린 것도 다시 접는다.
        void abort_all(const http_error& reason) noexcept
        {
            std::vector<std::shared_ptr<request_state>> live {};
            try
            {
                live = live_requests();
            }
            catch (...)
            {
                return;
            }
            for (const std::shared_ptr<request_state>& state : live)
                abort(*state, reason);
        }

        // 걸쇠는 적혔는데 아직 끝나지 않은 요청을 다시 접는다 (pump의 훑기).
        void retry_aborts()
        {
            for (const std::shared_ptr<request_state>& state : live_requests())
                cancel_if_latched(*state);
        }

        [[nodiscard]] bool reserve_ticket(std::u8string& error)
        {
            std::size_t pending { pending_tickets_.load() };
            while (pending < configuration_.max_pending_responses)
                if (pending_tickets_.compare_exchange_weak(pending, pending + 1))
                    return true;
            error = u8"too many responses awaiting delivery";
            return false;
        }

        void post_synthetic(const http_ticket ticket, const std::uint64_t sequence, const http_request& request, const http_error& error) noexcept
        {
            completion synthetic {};
            synthetic.ticket = ticket;
            synthetic.sequence = sequence;
            synthetic.final_url = request.url;
            synthetic.parse = request.parse;
            synthetic.error = error;
            outstanding_.fetch_add(1);
            if (completions_.post(std::move(synthetic)) != messaging::post_result::posted)
                outstanding_.fetch_sub(1);
        }

        void remember_deadline(const request_state& state)
        {
            if (state.has_deadline == false)
                return;
            std::lock_guard lock { deadlines_mutex_ };
            static_cast<void>(deadlines_.emplace(state.deadline, state.id));
        }

        // 선에 실을 요청 하나를 짓는다. 실패하면 nil이고 `failure`가 찬다.
        [[nodiscard]] NSURLRequest* make_request(NSURL* const url, const request_state& state, const http_method method, http_error& failure) const
        {
            NSMutableURLRequest* const request { [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:idle_timeout(state.request)] };
            const std::u8string_view verb { method == http_method::extension ? std::u8string_view { state.request.method_name } : http_method_name(method) };
            NSString* const verb_text { macos::make_ns_string(verb) };
            if (verb_text == nil)
            {
                failure = make_http_error(http_error_kind::invalid_method, u8"The method name is not valid UTF-8.");
                return nil;
            }
            request.HTTPMethod = verb_text;
            request.HTTPShouldHandleCookies = NO;
            [request setValue:user_agent_ forHTTPHeaderField:@"User-Agent"];
            for (const http_header& header : state.headers)
            {
                NSString* const name { macos::make_ns_string(header.name) };
                NSString* const value { macos::make_ns_string(header.value) };
                if (name == nil || value == nil)
                {
                    failure = make_http_error(http_error_kind::invalid_header, u8"A request header is not valid UTF-8.");
                    return nil;
                }
                [request setValue:value forHTTPHeaderField:name];
            }
            // 압축을 청하는 일은 시스템이 한다 (gzip·deflate, 받으면 스스로 푼다). 풀지 않을 답이면 압축하지 말라고
            // 적는다 — 시스템은 압축된 답을 끄는 길 없이 풀므로, 앱이 선의 바이트를 받을 길은 서버가 그대로
            // 보내는 것뿐이다.
            if (state.request.decompress == false && find_header(state.headers, u8"accept-encoding").empty())
                [request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
            // 몸 있는 요청은 재지정을 따라가지 않으므로 몸은 첫 걸음에만 실린다.
            if (state.request.body.empty() == false && method == state.request.method)
                request.HTTPBody = [NSData dataWithBytes:state.request.body.data() length:state.request.body.size()];
            return request;
        }

        // 요청 하나를 받아 task를 띄운다. 거짓이면 띄우기 전에 실패한 것이라 답이 오지 않는다.
        [[nodiscard]] bool start_request(http_request request, const http_ticket ticket, const std::uint64_t sequence, const std::uint64_t id, http_error& failure)
        {
            @autoreleasepool
            {
                url_origin origin { parse_origin(request.url, failure) };
                if (origin.valid == false || check_request_method(request, failure) == false)
                    return false;

                auto state { std::make_shared<request_state>() };
                if (collect_request_headers(request, state->headers, failure) == false)
                    return false;
                if (body_fits_method(request, failure) == false)
                    return false;

                NSString* const url_text { macos::make_ns_string(request.url) };
                NSURL* const url { url_text == nil ? nil : [NSURL URLWithString:url_text] };
                if (url == nil || url.host == nil)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"NSURL could not read the URL.");
                    return false;
                }

                state->id = id;
                state->ticket = ticket;
                state->sequence = sequence;
                state->method = request.method;
                state->current_secure = origin.secure;
                state->origin = std::move(origin);
                state->result.ticket = ticket;
                state->result.sequence = sequence;
                state->result.parse = request.parse;
                state->result.final_url = request.url;
                state->request = std::move(request);

                NSURLRequest* const wire { make_request(url, *state, state->method, failure) };
                if (wire == nil)
                    return false;
                state->task = [session_ dataTaskWithRequest:wire];
                if (state->task == nil)
                {
                    failure = make_http_error(http_error_kind::system_error, u8"The URL session could not create a task.");
                    return false;
                }
                state->started = std::chrono::steady_clock::now();
                if (state->request.total_timeout > 0ms)
                {
                    state->has_deadline = true;
                    state->deadline = state->started + state->request.total_timeout;
                }

                {
                    // 회차 예약의 취소와 등록을 같은 관문으로 묶는다 (Windows·Android 백엔드와 같다).
                    std::unique_lock schedule_lock { schedules_mutex_, std::defer_lock };
                    if (sequence != 0)
                    {
                        schedule_lock.lock();
                        if (schedules_.contains(ticket.value) == false)
                        {
                            failure = cancelled_error();
                            return false;
                        }
                    }
                    std::lock_guard lock { in_flight_mutex_ };
                    if (stopping_.load())
                    {
                        failure = make_http_error(http_error_kind::stopped, u8"client stopped");
                        return false;
                    }
                    if (in_flight_.size() >= configuration_.max_in_flight)
                    {
                        failure = make_http_error(http_error_kind::system_error, u8"too many requests in flight (" + count_text(configuration_.max_in_flight) + u8")");
                        return false;
                    }
                    in_flight_.emplace(id, state);
                    tasks_.emplace(state->task.taskIdentifier, state);
                    outstanding_.fetch_add(1);
                }

                remember_deadline(*state);
                [state->task resume];
                // 등록과 띄우기 사이에 걸린 취소·정지는 여기서 접는다 (접기는 멱등이다).
                cancel_if_latched(*state);
                return true;
            }
        }

        void finish_request(const std::uint64_t id, completion result) noexcept
        {
            std::lock_guard lock { in_flight_mutex_ };
            if (in_flight_.erase(id) == 0)
                return;
            static_cast<void>(completions_.post(std::move(result)));
            // 알림도 **잠금 안**이다. 밖으로 나가는 순간 `stop()`이 지도가 빈 것을 보고 engine을 부술 수 있다.
            in_flight_empty_.notify_all();
        }

        void run_pump()
        {
            static_cast<void>(pthread_setname_np("luil-http-client"));
            auto swept { std::chrono::steady_clock::now() };
            while (stopping_.load() == false)
            {
                @autoreleasepool
                {
                    messaging::envelope<completion> entry {};
                    const messaging::receive_status status { completions_.receive_wait(entry, next_wait()) };
                    if (status == messaging::receive_status::closed)
                        break;
                    if (status == messaging::receive_status::received)
                    {
                        handle_completion(std::move(entry.payload));
                        if (std::chrono::steady_clock::now() - swept < pump_sweep_interval)
                            continue;
                    }
                    swept = std::chrono::steady_clock::now();
                    expire_deadlines();
                    fire_schedules();
                    retry_aborts();
                }
            }
        }

        [[nodiscard]] std::chrono::milliseconds next_wait()
        {
            const auto now { std::chrono::steady_clock::now() };
            auto limit { now + pump_idle_wait };
            {
                std::lock_guard lock { deadlines_mutex_ };
                if (deadlines_.empty() == false)
                    limit = std::min(limit, deadlines_.begin()->first);
            }
            {
                std::lock_guard lock { schedules_mutex_ };
                for (const auto& entry : schedules_)
                    if (entry.second.in_flight_id == 0)
                        limit = std::min(limit, entry.second.next);
            }
            // 걸쇠만 적히고 끝나지 않은 요청이 있으면 접기를 다시 걸 때까지만 잔다.
            for (const std::shared_ptr<request_state>& state : live_requests())
            {
                std::lock_guard lock { state->guard };
                if (state->outcome_latched && state->finished == false)
                    limit = std::min(limit, now + pump_sweep_interval);
            }
            if (limit <= now)
                return 0ms;
            return std::chrono::duration_cast<std::chrono::milliseconds>(limit - now) + 1ms;
        }

        void handle_completion(completion finished)
        {
            bool last { true };
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(finished.ticket.value) };
                if (entry != schedules_.end())
                {
                    schedule_record& record { entry->second };
                    record.in_flight_id = 0;
                    const bool exhausted { record.schedule.max_rounds != 0 && record.rounds >= record.schedule.max_rounds };
                    last = exhausted || failure_is_permanent(finished.error.kind);
                    if (last)
                        schedules_.erase(entry);
                }
            }

            const http_ticket ticket { finished.ticket };
            deliver_safely(make_response(std::move(finished), last));
            outstanding_.fetch_sub(1);
            // **재무장은 배달한 뒤다.** 그래서 회차가 겹칠 수 없다.
            if (last == false)
                rearm_schedule(ticket);
            else
                pending_tickets_.fetch_sub(1);
        }

        [[nodiscard]] http_response make_response(completion finished, const bool last)
        {
            http_response response {};
            response.ticket = finished.ticket;
            response.sequence = finished.sequence;
            response.last = last;
            response.status_code = finished.status_code;
            response.reason = std::move(finished.reason);
            response.final_url = std::move(finished.final_url);
            response.headers = std::move(finished.headers);
            response.content_type = parse_media_type(find_header(response.headers, u8"content-type"));
            response.error = std::move(finished.error);
            response.elapsed = finished.elapsed;
            if (response.error.empty())
                response.body = decode_http_body(std::move(finished.body), response.content_type, finished.parse, transcode_);
            return response;
        }

        void deliver_safely(http_response response) noexcept
        {
            if (delivering_.load() == false || configuration_.deliver == nullptr)
                return;
            try
            {
                configuration_.deliver(std::move(response));
            }
            catch (...)
            {
                // 계약은 던지지 않는 것이다. 던지면 삼키고 그 답만 버린다.
            }
        }

        void rearm_schedule(const http_ticket ticket)
        {
            std::lock_guard lock { schedules_mutex_ };
            const auto entry { schedules_.find(ticket.value) };
            if (entry == schedules_.end())
                return;
            entry->second.next = std::chrono::steady_clock::now() + entry->second.schedule.interval;
        }

        void expire_deadlines()
        {
            const auto now { std::chrono::steady_clock::now() };
            std::vector<std::uint64_t> expired {};
            {
                std::lock_guard lock { deadlines_mutex_ };
                auto entry { deadlines_.begin() };
                while (entry != deadlines_.end() && entry->first <= now)
                {
                    expired.push_back(entry->second);
                    entry = deadlines_.erase(entry);
                }
            }
            for (const std::uint64_t id : expired)
                if (const std::shared_ptr<request_state> live { borrow_request(id) }; live != nullptr)
                    abort(*live, make_http_error(http_error_kind::timed_out, u8"The request exceeded total_timeout."));
        }

        void fire_schedules()
        {
            std::vector<pending_round> ready {};
            const auto now { std::chrono::steady_clock::now() };
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                {
                    schedule_record& record { entry.second };
                    if (record.in_flight_id != 0 || record.next > now)
                        continue;

                    // 번호를 **잠금 안에서** 먼저 발번한다 — 다른 thread의 `cancel`이 날아가는
                    // 중인 회차를 놓치지 않는다.
                    pending_round round {};
                    round.id = issue_request_id();
                    round.ticket = record.ticket;
                    round.request = record.request;
                    ++record.rounds;
                    round.sequence = record.rounds;
                    record.in_flight_id = round.id;
                    ready.push_back(std::move(round));
                }
            }

            for (pending_round& round : ready)
            {
                http_error failure {};
                if (start_request(round.request, round.ticket, round.sequence, round.id, failure) == false)
                    post_synthetic(round.ticket, round.sequence, round.request, failure);
            }
        }

        http_client_config configuration_ {};
        http_text_transcoder transcode_ {};
        NSString* user_agent_ { nil };
        NSURLSession* session_ { nil };
        std::shared_ptr<session_bridge> bridge_ {};

        std::atomic<bool> stopping_ { false };
        std::atomic<bool> stopped_ { false };
        std::atomic<bool> delivering_ { true };
        std::atomic<std::size_t> outstanding_ { 0 };
        std::atomic<std::size_t> pending_tickets_ { 0 };
        std::mutex stop_mutex_ {};

        mutable std::mutex in_flight_mutex_ {};
        std::condition_variable in_flight_empty_ {};
        std::unordered_map<std::uint64_t, std::shared_ptr<request_state>> in_flight_ {};
        // delegate가 task로 상태를 찾는 지도다. `in_flight_mutex_`가 함께 지킨다.
        std::unordered_map<NSUInteger, std::shared_ptr<request_state>> tasks_ {};

        std::mutex schedules_mutex_ {};
        std::map<std::uint64_t, schedule_record> schedules_ {};

        std::mutex deadlines_mutex_ {};
        std::multimap<std::chrono::steady_clock::time_point, std::uint64_t> deadlines_ {};

        messaging::channel<completion> completions_;
        std::thread pump_ {};
        std::thread::id pump_id_ {};
    };

    http_client::http_client(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    http_client::~http_client()
    {
        stop();
    }

    std::unique_ptr<http_client> http_client::create(http_client_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>(std::move(configuration)) };
        if (owned->open(error) == false)
            return nullptr;
        return std::unique_ptr<http_client> { new http_client { std::move(owned) } };
    }

    http_ticket http_client::send(http_request request, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->send(std::move(request), error);
    }

    http_ticket http_client::start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->start_heartbeat(std::move(request), schedule, error);
    }

    void http_client::cancel(const http_ticket ticket) noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel(ticket);
    }

    void http_client::cancel_all() noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel_all();
    }

    std::size_t http_client::in_flight() const noexcept
    {
        return engine_ == nullptr ? 0u : engine_->in_flight();
    }

    void http_client::stop() noexcept
    {
        if (engine_ != nullptr)
            static_cast<void>(engine_->stop());
    }
}

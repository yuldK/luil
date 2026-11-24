#include "net/websocket_transport.h"

#include "macos/macos_text.h"

#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <sys/errno.h>

// NSURLSessionWebSocketTask 위의 전송이다. 전송의 단위가 메시지라(`websocket_packet`) 메시지를 주고받는 이 API가
// 맞는다 — 조각 재조립·ping/pong·Close 응답·mask는 시스템이 한다.
//  - 조각은 시스템이 모아 온다. 그래서 받는 packet은 늘 `final`이다.
//  - 상대의 Close에는 시스템이 같은 코드로 곧 답한다. 우리가 뒤이어 부르는 `close`는 이미 접힌 task에 닿아
//    아무 일도 하지 않는다.
//  - **우리가 먼저 닫으면 상대의 답을 기다리지 않는다.** `cancelWithCloseCode:`는 Close를 보내고 곧 task를
//    접으며, 그 뒤의 받기는 우리가 보낸 코드로 끝난다. 그래서 macOS의 로컬 닫기는 상대가 답하지 않아도 깨끗한
//    닫기(보낸 코드, 오류 없음)로 끝나고 `close_timeout`에 걸리지 않는다 (Windows는 답을 기다린다).
//  - 상한을 넘는 메시지는 시스템이 1009로 닫고 받기가 `EMSGSIZE`로 실패한다 (`body_too_large`). 잘못된
//    UTF-8과 frame은 시스템이 1002로 닫는다.
namespace luil::net {
    namespace {
        using namespace std::chrono_literals;

        // 상대가 닫았는데 코드가 아직 task에 적히지 않았을 때 task가 끝나기를 기다리는 상한이다.
        constexpr std::chrono::milliseconds close_settle_wait { 500 };
        // 연결 뒤의 쉼에 시스템 시한이 걸리지 않게 하는 값이다. 연결 상한은 `open`이 직접 잰다.
        constexpr NSTimeInterval unlimited_seconds { 365.0 * 24.0 * 60.0 * 60.0 };

        // task와 delegate가 함께 보는 상태다. 완료 callback이 전송보다 오래 살 수 있어 공유로 든다.
        struct socket_state
        {
            std::mutex mutex {};
            std::condition_variable changed {};
            bool opened { false };
            bool completed { false };
            bool stopped { false };
            NSError* failure { nil };
        };

        // 받기·보내기 하나의 결말이다.
        struct operation
        {
            bool done { false };
            NSURLSessionWebSocketMessage* message { nil };
            NSError* error { nil };
        };
    }
}

// 열림과 끝남을 상태에 적는다. 그 밖의 사건은 task가 직접 답한다.
@interface LuilWebSocketDelegate : NSObject <NSURLSessionWebSocketDelegate>
- (instancetype)initWithState:(std::shared_ptr<luil::net::socket_state>)state;
@end

@implementation LuilWebSocketDelegate {
    std::shared_ptr<luil::net::socket_state> _state;
}

- (instancetype)initWithState:(std::shared_ptr<luil::net::socket_state>)state
{
    self = [super init];
    if (self != nil)
        _state = std::move(state);
    return self;
}

- (void)URLSession:(NSURLSession*)session webSocketTask:(NSURLSessionWebSocketTask*)task didOpenWithProtocol:(NSString*)protocol
{
    static_cast<void>(session);
    static_cast<void>(task);
    static_cast<void>(protocol);
    {
        const std::scoped_lock lock { _state->mutex };
        _state->opened = true;
    }
    _state->changed.notify_all();
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    static_cast<void>(session);
    static_cast<void>(task);
    {
        const std::scoped_lock lock { _state->mutex };
        _state->completed = true;
        _state->failure = error;
    }
    _state->changed.notify_all();
}

@end

namespace luil::net {
    namespace {
        [[nodiscard]] http_error native_failure(const std::u8string_view message, NSError* const error)
        {
            http_error failure { .kind = http_error_kind::system_error, .message = std::u8string { message } };
            if (error == nil)
                return failure;
            failure.native_error = static_cast<unsigned long>(error.code);
            failure.message += u8" (";
            failure.message += macos::from_ns_string(error.domain);
            failure.message += u8"=";
            failure.message += macos::widen(std::to_string(static_cast<long long>(error.code)));
            failure.message += u8")";
            return failure;
        }

        class macos_websocket final : public websocket_transport
        {
        public:
            macos_websocket()
                : state_ { std::make_shared<socket_state>() }
            {
            }

            macos_websocket(const macos_websocket&) = delete;
            macos_websocket& operator=(const macos_websocket&) = delete;

            ~macos_websocket() override
            {
                shutdown();
                @autoreleasepool
                {
                    // 세션은 무효가 될 때까지 delegate를 붙잡는다. 닫아야 둘 다 풀린다.
                    [session_ invalidateAndCancel];
                    session_ = nil;
                    task_ = nil;
                }
            }

            bool open(const websocket_request& request, const std::size_t limit, http_error& error) override
            {
                @autoreleasepool
                {
                    // URL은 websocket_client가 이미 ASCII로 걸렀다.
                    NSString* const text { macos::make_ns_string(request.url) };
                    NSURL* const url { text == nil ? nil : [NSURL URLWithString:text] };
                    if (url == nil || url.host == nil)
                    {
                        error = { .kind = http_error_kind::invalid_url, .message = u8"NSURL could not read the WebSocket URL." };
                        return false;
                    }

                    NSURLSessionConfiguration* const settings { [NSURLSessionConfiguration ephemeralSessionConfiguration] };
                    settings.URLCache = nil;
                    settings.HTTPCookieStorage = nil;
                    settings.HTTPShouldSetCookies = NO;
                    settings.URLCredentialStorage = nil;
                    settings.waitsForConnectivity = NO;
                    settings.timeoutIntervalForResource = unlimited_seconds;
                    NSOperationQueue* const queue { [[NSOperationQueue alloc] init] };
                    queue.maxConcurrentOperationCount = 1;
                    queue.name = @"luil-websocket-delegate";
                    LuilWebSocketDelegate* const delegate { [[LuilWebSocketDelegate alloc] initWithState:state_] };

                    NSMutableURLRequest* const upgrade { [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:unlimited_seconds] };
                    [upgrade setValue:@"luil" forHTTPHeaderField:@"User-Agent"];

                    NSURLSessionWebSocketTask* task { nil };
                    {
                        const std::scoped_lock lock { state_->mutex };
                        if (state_->stopped)
                            return stopped(error);
                        session_ = [NSURLSession sessionWithConfiguration:settings delegate:delegate delegateQueue:queue];
                        task_ = [session_ webSocketTaskWithRequest:upgrade];
                        task = task_;
                    }
                    if (task == nil)
                        return native(error, u8"NSURLSession could not create a WebSocket task.", nil);
                    // 시스템의 기본 상한은 1 MiB다. 앱의 상한을 그대로 건다 (`websocket_client`가 64 MiB 아래로 막았다).
                    task.maximumMessageSize = static_cast<NSInteger>(limit);
                    [task resume];

                    // 연결 상한은 여기서 잰다. 요청의 시한은 연결 뒤의 쉼에도 걸리므로 걸지 않는다.
                    std::unique_lock lock { state_->mutex };
                    const bool settled { state_->changed.wait_for(lock, request.connect_timeout, [this] { return state_->opened || state_->completed || state_->stopped; }) };
                    if (state_->stopped)
                        return stopped(error);
                    if (state_->opened)
                        return true;
                    if (settled == false)
                    {
                        lock.unlock();
                        [task cancel];
                        error = { .kind = http_error_kind::timed_out, .message = u8"The WebSocket connection timed out." };
                        return false;
                    }
                    NSError* const failure { state_->failure };
                    lock.unlock();
                    // 101이 아닌 답이 왔으면 서버가 upgrade를 거절한 것이다 (Windows와 같은 갈래다).
                    if ([task.response isKindOfClass:[NSHTTPURLResponse class]] && static_cast<NSHTTPURLResponse*>(task.response).statusCode != 101)
                    {
                        error = { .kind = http_error_kind::cannot_connect, .message = u8"WebSocket upgrade was rejected." };
                        return false;
                    }
                    return native(error, u8"NSURLSession WebSocket connection failed.", failure);
                }
            }

            bool receive(websocket_packet& packet, const std::size_t limit, http_error& error) override
            {
                @autoreleasepool
                {
                    NSURLSessionWebSocketTask* const task { live_task() };
                    if (task == nil)
                        return stopped(error);
                    const auto pending { std::make_shared<operation>() };
                    const std::shared_ptr<socket_state> state { state_ };
                    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message, NSError* failure) {
                        {
                            const std::scoped_lock lock { state->mutex };
                            pending->done = true;
                            pending->message = message;
                            pending->error = failure;
                        }
                        state->changed.notify_all();
                    }];
                    if (wait(*pending) == false)
                        return stopped(error);

                    if (pending->message != nil)
                        return unpack(pending->message, packet, limit, error);
                    return closed_or_failed(task, pending->error, packet, error);
                }
            }

            bool send(const std::span<const std::uint8_t> bytes, const bool text, http_error& error) override
            {
                @autoreleasepool
                {
                    NSURLSessionWebSocketTask* const task { live_task() };
                    if (task == nil)
                        return stopped(error);
                    NSURLSessionWebSocketMessage* message { nil };
                    if (text)
                    {
                        // websocket_client가 이미 UTF-8을 확인했다.
                        NSString* const string { [[NSString alloc] initWithBytes:bytes.data() length:bytes.size() encoding:NSUTF8StringEncoding] };
                        if (string == nil)
                        {
                            error = { .kind = http_error_kind::system_error, .message = u8"WebSocket text is not valid UTF-8." };
                            return false;
                        }
                        message = [[NSURLSessionWebSocketMessage alloc] initWithString:string];
                    }
                    else
                    {
                        message = [[NSURLSessionWebSocketMessage alloc] initWithData:[NSData dataWithBytes:bytes.data() length:bytes.size()]];
                    }

                    const auto pending { std::make_shared<operation>() };
                    const std::shared_ptr<socket_state> state { state_ };
                    [task sendMessage:message completionHandler:^(NSError* failure) {
                        {
                            const std::scoped_lock lock { state->mutex };
                            pending->done = true;
                            pending->error = failure;
                        }
                        state->changed.notify_all();
                    }];
                    if (wait(*pending) == false)
                        return stopped(error);
                    if (pending->error != nil)
                        return native(error, u8"NSURLSession WebSocket send failed.", pending->error);
                    return true;
                }
            }

            bool close(const std::uint16_t code, const std::u8string_view reason, http_error& error) override
            {
                @autoreleasepool
                {
                    NSURLSessionWebSocketTask* const task { live_task() };
                    if (task == nil)
                        return stopped(error);
                    NSData* const payload { reason.empty() ? nil : [NSData dataWithBytes:reason.data() length:reason.size()] };
                    [task cancelWithCloseCode:static_cast<NSURLSessionWebSocketCloseCode>(code) reason:payload];
                    return true;
                }
            }

            void shutdown() noexcept override
            {
                NSURLSessionWebSocketTask* task { nil };
                {
                    const std::scoped_lock lock { state_->mutex };
                    state_->stopped = true;
                    task = task_;
                }
                state_->changed.notify_all();
                @autoreleasepool
                {
                    [task cancel];
                }
            }

        private:
            [[nodiscard]] NSURLSessionWebSocketTask* live_task()
            {
                const std::scoped_lock lock { state_->mutex };
                return state_->stopped ? nil : task_;
            }

            // 결말이 오거나 `shutdown`이 깨울 때까지 기다린다. 깨워졌으면 거짓이다.
            [[nodiscard]] bool wait(const operation& pending)
            {
                std::unique_lock lock { state_->mutex };
                state_->changed.wait(lock, [&] { return pending.done || state_->stopped; });
                return pending.done && state_->stopped == false;
            }

            [[nodiscard]] static bool unpack(NSURLSessionWebSocketMessage* const message, websocket_packet& packet, const std::size_t limit, http_error& error)
            {
                NSData* const data { message.type == NSURLSessionWebSocketMessageTypeString ? [message.string dataUsingEncoding:NSUTF8StringEncoding] : message.data };
                const std::size_t size { data == nil ? 0u : static_cast<std::size_t>(data.length) };
                if (size > limit)
                {
                    error = { .kind = http_error_kind::body_too_large, .message = u8"WebSocket message exceeds its limit." };
                    return false;
                }
                packet.text = message.type == NSURLSessionWebSocketMessageTypeString;
                packet.final = true;
                packet.bytes.resize(size);
                if (size != 0)
                    [data getBytes:packet.bytes.data() length:size];
                return true;
            }

            // 받기가 실패했다. task에 닫기 코드가 적혀 있으면 닫힌 것이고(상대가 닫았거나 우리가 닫았다), 아니면 오류다.
            [[nodiscard]] bool closed_or_failed(NSURLSessionWebSocketTask* const task, NSError* const failure, websocket_packet& packet, http_error& error)
            {
                NSURLSessionWebSocketCloseCode code { task.closeCode };
                if (code == NSURLSessionWebSocketCloseCodeInvalid)
                {
                    // 상대의 Close가 받기 실패보다 늦게 적히는 순서에 대비한다. task가 끝나면 코드는 적혀 있다.
                    std::unique_lock lock { state_->mutex };
                    state_->changed.wait_for(lock, close_settle_wait, [this] { return state_->completed || state_->stopped; });
                    lock.unlock();
                    code = task.closeCode;
                }
                if (code != NSURLSessionWebSocketCloseCodeInvalid)
                {
                    NSData* const reason { task.closeReason };
                    packet.closed = true;
                    packet.code = static_cast<std::uint16_t>(code);
                    packet.bytes.resize(reason == nil ? 0u : static_cast<std::size_t>(reason.length));
                    if (packet.bytes.empty() == false)
                        [reason getBytes:packet.bytes.data() length:packet.bytes.size()];
                    return true;
                }
                if ([failure.domain isEqualToString:NSPOSIXErrorDomain] && failure.code == EMSGSIZE)
                {
                    error = { .kind = http_error_kind::body_too_large, .message = u8"WebSocket message exceeds its limit.", .native_error = static_cast<unsigned long>(failure.code) };
                    return false;
                }
                if ([failure.domain isEqualToString:NSPOSIXErrorDomain] && failure.code == EBADMSG)
                    return native(error, u8"The WebSocket peer sent an invalid frame or invalid UTF-8.", failure);
                return native(error, u8"NSURLSession WebSocket receive failed.", failure);
            }

            static bool native(http_error& error, const std::u8string_view message, NSError* const failure)
            {
                error = native_failure(message, failure);
                return false;
            }

            static bool stopped(http_error& error)
            {
                error = { .kind = http_error_kind::stopped, .message = u8"WebSocket stopped." };
                return false;
            }

            std::shared_ptr<socket_state> state_ {};
            NSURLSession* session_ { nil };
            NSURLSessionWebSocketTask* task_ { nil };
        };
    }

    std::unique_ptr<websocket_transport> make_websocket_transport()
    {
        return std::make_unique<macos_websocket>();
    }
}

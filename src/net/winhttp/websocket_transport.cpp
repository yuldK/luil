#include "net/websocket_transport.h"

#include "net/winhttp/http_url.h"
#include "win32/utf8.h"
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <atomic>
#include <mutex>

namespace luil::net {
    namespace {
        class winhttp_websocket final : public websocket_transport
        {
        public:
            ~winhttp_websocket() override
            {
                shutdown();
            }

            bool open(const websocket_request& request, std::size_t, http_error& error) override
            {
                auto url { request.url };
                url.replace(0, url.starts_with(u8"wss:") ? 3 : 2, url.starts_with(u8"wss:") ? u8"https" : u8"http");
                const auto parsed { parse_http_url(url, error) };
                if (parsed.has_value() == false)
                    return false;
                if (store(session_, WinHttpOpen(L"luil", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)) == false)
                    return fail(error);
                const auto timeout { static_cast<int>(request.connect_timeout.count()) };
                (void)WinHttpSetTimeouts(session_, timeout, timeout, timeout, timeout);
                if (store(connection_, WinHttpConnect(session_, parsed->host.c_str(), parsed->port, 0)) == false)
                    return fail(error);
                if (store(request_, WinHttpOpenRequest(connection_, L"GET", parsed->target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parsed->secure ? WINHTTP_FLAG_SECURE : 0)) == false)
                    return fail(error);
                DWORD redirects { WINHTTP_OPTION_REDIRECT_POLICY_NEVER };
                if (WinHttpSetOption(request_, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) == FALSE
                    || WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) == FALSE
                    || WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE
                    || WinHttpReceiveResponse(request_, nullptr) == FALSE)
                    return fail(error);
                DWORD status {};
                DWORD size { sizeof(status) };
                if (WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) == FALSE || status != 101)
                {
                    error = { .kind = http_error_kind::cannot_connect, .message = u8"WebSocket upgrade was rejected." };
                    return false;
                }
                // 열린 뒤의 수신은 다음 메시지까지 기다려야 하므로 받기 시한을 없앤다. WebSocket 핸들은 이 옵션을
                // 받지 않아(ERROR_WINHTTP_INCORRECT_HANDLE_TYPE) upgrade 전의 request 핸들에 걸고 그 값을 물려받게 한다.
                DWORD idle_timeout { 0 };
                if (WinHttpSetOption(request_, WINHTTP_OPTION_RECEIVE_TIMEOUT, &idle_timeout, sizeof(idle_timeout)) == FALSE)
                    return fail(error);
                if (store(socket_, WinHttpWebSocketCompleteUpgrade(request_, 0)) == false)
                    return fail(error);
                return true;
            }

            bool receive(websocket_packet& packet, const std::size_t limit, http_error& error) override
            {
                std::array<std::uint8_t, 16384> buffer {};
                DWORD read {};
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type {};
                const auto code { WinHttpWebSocketReceive(socket_, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type) };
                if (code != NO_ERROR)
                    return fail(error, code);
                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                {
                    USHORT status {};
                    DWORD length {};
                    const auto queried { WinHttpWebSocketQueryCloseStatus(socket_, &status, buffer.data(), 123, &length) };
                    if (queried != NO_ERROR)
                        return fail(error, queried);
                    packet.closed = true;
                    packet.code = status;
                    packet.bytes.assign(buffer.begin(), buffer.begin() + length);
                    return true;
                }
                if (read > limit)
                {
                    error = { .kind = http_error_kind::body_too_large, .message = u8"WebSocket message exceeds its limit." };
                    return false;
                }
                packet.text = type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE;
                packet.final = type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
                packet.bytes.assign(buffer.begin(), buffer.begin() + read);
                return true;
            }

            bool send(const std::span<const std::uint8_t> bytes, const bool text, http_error& error) override
            {
                const std::scoped_lock lock { send_mutex_ };
                const auto code { WinHttpWebSocketSend(socket_, text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, const_cast<std::uint8_t*>(bytes.data()), static_cast<DWORD>(bytes.size())) };
                return code == NO_ERROR || fail(error, code);
            }

            bool close(const std::uint16_t code, const std::u8string_view reason, http_error& error) override
            {
                const std::scoped_lock lock { send_mutex_ };
                const auto result { WinHttpWebSocketShutdown(socket_, code, const_cast<char8_t*>(reason.data()), static_cast<DWORD>(reason.size())) };
                return result == NO_ERROR || fail(error, result);
            }

            void shutdown() noexcept override
            {
                const std::scoped_lock lock { handles_mutex_ };
                stopped_ = true;
                for (auto* handle : { &socket_, &request_, &connection_, &session_ })
                    if (const auto owned { handle->exchange(nullptr) }; owned != nullptr)
                        (void)WinHttpCloseHandle(owned);
            }

        private:
            bool store(std::atomic<HINTERNET>& slot, const HINTERNET value)
            {
                const std::scoped_lock lock { handles_mutex_ };
                if (stopped_)
                {
                    if (value != nullptr)
                        (void)WinHttpCloseHandle(value);
                    return false;
                }
                slot = value;
                return value != nullptr;
            }

            static bool fail(http_error& error, const DWORD code = GetLastError())
            {
                error = { .kind = http_error_kind::system_error, .message = u8"WinHTTP WebSocket operation failed.", .native_error = code };
                return false;
            }
            std::atomic<HINTERNET> session_ { nullptr };
            std::atomic<HINTERNET> connection_ { nullptr };
            std::atomic<HINTERNET> request_ { nullptr };
            std::atomic<HINTERNET> socket_ { nullptr };
            std::mutex handles_mutex_ {};
            std::mutex send_mutex_ {};
            bool stopped_ { false };
        };
    }

    std::unique_ptr<websocket_transport> make_websocket_transport()
    {
        return std::make_unique<winhttp_websocket>();
    }
}

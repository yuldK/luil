#include "net/winhttp/http_request_context.h"

#include "net/winhttp/winhttp_error.h"
#include "win32/utf8.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace luil::net {
    namespace {
        // 번호 표다. 함수 안 static이라 첫 요청이 나갈 때 선다.
        std::mutex& request_table_mutex() noexcept
        {
            static std::mutex value {};
            return value;
        }

        std::unordered_map<std::uint64_t, std::weak_ptr<request_context>>& request_table()
        {
            static std::unordered_map<std::uint64_t, std::weak_ptr<request_context>> value {};
            return value;
        }

        // WinHTTP의 글 질의는 크기를 모르는 채 시작한다 — 한 번 불러 크기를 얻고 다시 부른다.
        // 크기는 **바이트**로 오고 첫 번에는 끝의 NUL이 끼어 있다.
        [[nodiscard]] bool query_header_text(const HINTERNET handle, const DWORD query, const std::size_t limit, std::wstring& out, unsigned long& code)
        {
            DWORD size { 0 };
            if (WinHttpQueryHeaders(handle, query, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX) != FALSE)
            {
                out.clear();
                return true;
            }

            code = GetLastError();
            if (code != static_cast<unsigned long>(ERROR_INSUFFICIENT_BUFFER))
                return false;
            if (static_cast<std::size_t>(size) > limit)
            {
                code = static_cast<unsigned long>(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }

            out.resize(static_cast<std::size_t>(size) / sizeof(wchar_t));
            if (WinHttpQueryHeaders(handle, query, WINHTTP_HEADER_NAME_BY_INDEX, out.data(), &size, WINHTTP_NO_HEADER_INDEX) == FALSE)
            {
                code = GetLastError();
                return false;
            }
            out.resize(static_cast<std::size_t>(size) / sizeof(wchar_t));
            return true;
        }

        [[nodiscard]] std::u8string to_utf8(const std::wstring_view text)
        {
            const auto converted { win32::utf16_to_utf8(text) };
            return converted.value.has_value() ? *converted.value : std::u8string {};
        }

        // `Content-Length` 하나를 읽는다. 숫자가 아니거나 넘치면 상한 검사에 걸리도록
        // 가장 큰 값을 답한다 — 밖에서 오는 글이라 "못 읽었으니 통과"가 되면 안 된다.
        [[nodiscard]] std::size_t parse_content_length(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return 0;

            std::size_t value { 0 };
            for (const char8_t character : text)
            {
                if (character < u8'0' || character > u8'9')
                    return std::numeric_limits<std::size_t>::max();
                const std::size_t digit { static_cast<std::size_t>(character - u8'0') };
                if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10u)
                    return std::numeric_limits<std::size_t>::max();
                value = value * 10u + digit;
            }
            return value;
        }

        // `RAW_HEADERS_CRLF`는 상태 줄로 시작해 줄마다 "이름: 값"이 이어진다.
        void parse_raw_headers(const std::wstring_view raw, std::vector<http_header>& out)
        {
            std::size_t line_start { 0 };
            bool first_line { true };
            while (line_start <= raw.size())
            {
                std::size_t line_end { raw.find(L"\r\n", line_start) };
                if (line_end == std::wstring_view::npos)
                    line_end = raw.size();

                const std::wstring_view line { raw.substr(line_start, line_end - line_start) };
                line_start = line_end + 2;
                if (first_line)
                {
                    // 상태 줄이다. 헤더가 아니므로 버린다.
                    first_line = false;
                    continue;
                }
                if (line.empty())
                    continue;

                const std::size_t separator { line.find(L':') };
                if (separator == std::wstring_view::npos)
                    continue;

                std::wstring_view value { line.substr(separator + 1) };
                while (value.empty() == false && (value.front() == L' ' || value.front() == L'\t'))
                    value.remove_prefix(1);
                while (value.empty() == false && (value.back() == L' ' || value.back() == L'\t'))
                    value.remove_suffix(1);

                http_header header {};
                header.name = to_utf8(line.substr(0, separator));
                header.value = to_utf8(value);
                if (header.name.empty() == false)
                    out.push_back(std::move(header));
            }
        }
    } // namespace

    void finish_and_close(request_context& context, const http_error& outcome) noexcept
    {
        HINTERNET taken { nullptr };
        {
            std::lock_guard lock { context.guard };
            if (context.outcome_latched == false)
            {
                context.outcome_latched = true;
                context.outcome = outcome;
            }
            // 손잡이를 아직 **꾸미는 중**이면 걸쇠만 적고 손잡이는 두고 간다.
            // 그 thread가 `WinHttpSendRequest`까지 마친 뒤 걸쇠를 보고 스스로 닫는다 —
            // 꾸미는 중인 손잡이를 옆에서 닫으면 그 thread가 닫힌(그 사이 남에게
            // 재사용됐을 수도 있는) 손잡이 값으로 옵션을 계속 건다.
            if (context.phase != request_phase::preparing)
            {
                taken = context.handle;
                context.handle = nullptr;
            }
        }

        // 잠금 밖이다. `WinHttpCloseHandle`이 같은 thread에서 곧바로 `HANDLE_CLOSING`을
        // 부를 수 있고, 그 콜백이 같은 `guard`를 잡으면 자기 자신에 막힌다.
        if (taken != nullptr)
            static_cast<void>(WinHttpCloseHandle(taken));
    }

    HINTERNET borrow_handle(request_context& context) noexcept
    {
        std::lock_guard lock { context.guard };
        return context.handle;
    }

    void set_phase(request_context& context, const request_phase phase) noexcept
    {
        std::lock_guard lock { context.guard };
        context.phase = phase;
    }

    bool append_body(request_context& context, const std::span<const std::uint8_t> bytes)
    {
        const std::size_t room { context.request.max_body_bytes > context.body.size() ? context.request.max_body_bytes - context.body.size() : 0u };
        if (bytes.size() > room)
            return false;

        context.body.insert(context.body.end(), bytes.begin(), bytes.end());
        return true;
    }

    bool capture_response_head(request_context& context, http_error& error)
    {
        const HINTERNET handle { borrow_handle(context) };
        if (handle == nullptr)
        {
            error = make_http_error(http_error_kind::cancelled, u8"The request was cancelled.", 0);
            return false;
        }

        DWORD status_code { 0 };
        DWORD status_size { static_cast<DWORD>(sizeof(status_code)) };
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX) == FALSE)
        {
            error = make_winhttp_error(GetLastError(), context.secure_flags);
            return false;
        }
        context.status_code = static_cast<int>(status_code);

        unsigned long code { 0 };
        std::wstring text {};
        if (query_header_text(handle, WINHTTP_QUERY_STATUS_TEXT, http_header_limit_bytes, text, code))
            context.reason = to_utf8(text);

        if (query_header_text(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, http_header_limit_bytes, text, code) == false)
        {
            error = make_winhttp_error(code, context.secure_flags);
            return false;
        }
        context.headers.clear();
        parse_raw_headers(text, context.headers);
        context.content_length_hint = parse_content_length(find_header(context.headers, u8"content-length"));

        // 최종 URL은 재지정을 따라간 뒤의 주소다. 못 얻으면 요청한 주소를 그대로 둔다.
        DWORD url_size { 0 };
        if (WinHttpQueryOption(handle, WINHTTP_OPTION_URL, nullptr, &url_size) == FALSE && GetLastError() == static_cast<unsigned long>(ERROR_INSUFFICIENT_BUFFER)
            && static_cast<std::size_t>(url_size) <= http_header_limit_bytes)
        {
            std::wstring url {};
            url.resize(static_cast<std::size_t>(url_size) / sizeof(wchar_t));
            if (WinHttpQueryOption(handle, WINHTTP_OPTION_URL, url.data(), &url_size) != FALSE)
            {
                url.resize(static_cast<std::size_t>(url_size) / sizeof(wchar_t));
                if (url.empty() == false)
                    context.final_url = to_utf8(url);
            }
        }
        return true;
    }

    void capture_redirect_url(request_context& context, const wchar_t* url, const std::size_t length)
    {
        if (url == nullptr || length == 0 || length > http_header_limit_bytes)
            return;

        const std::u8string moved { to_utf8(std::wstring_view { url, length }) };
        if (moved.empty() == false)
            context.final_url = moved;
    }

    http_completion make_completion(request_context& context)
    {
        http_completion completion {};
        completion.id = context.id;
        completion.ticket = context.ticket;
        completion.sequence = context.sequence;
        completion.parse = context.request.parse;
        completion.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - context.started);
        {
            std::lock_guard lock { context.guard };
            completion.error = context.outcome;
            context.phase = request_phase::finished;
        }

        completion.status_code = context.status_code;
        completion.reason = std::move(context.reason);
        completion.final_url = std::move(context.final_url);
        completion.headers = std::move(context.headers);
        // 전송이 실패했으면 몸은 넘기지 않는다 — 반쪽 몸은 "원래 그런 답"과 구별되지
        // 않는다 (`decode_animated_image_bytes`가 장 하나 실패에 통째로 거절하는 그
        // 판단과 같다).
        if (completion.error.empty())
            completion.body = std::move(context.body);
        return completion;
    }

    void register_request(const std::uint64_t id, const std::shared_ptr<request_context>& context)
    {
        std::lock_guard lock { request_table_mutex() };
        request_table()[id] = context;
    }

    void forget_request(const std::uint64_t id) noexcept
    {
        std::lock_guard lock { request_table_mutex() };
        static_cast<void>(request_table().erase(id));
    }

    std::shared_ptr<request_context> find_request(const std::uint64_t id) noexcept
    {
        std::lock_guard lock { request_table_mutex() };
        const auto entry { request_table().find(id) };
        if (entry == request_table().end())
            return nullptr;
        return entry->second.lock();
    }

    void close_request(const std::uint64_t id) noexcept
    {
        std::lock_guard lock { request_table_mutex() };
        const auto entry { request_table().find(id) };
        if (entry == request_table().end())
            return;

        const std::shared_ptr<request_context> closing { entry->second.lock() };
        static_cast<void>(request_table().erase(entry));
        if (closing == nullptr || closing->owner == nullptr)
            return;

        // 표를 쥔 채로 부른다 — 임자가 이 줄 도중에 부서지지 않게 하는 것이 이 잠금의
        // 두 번째 일이다 (`wait_for_request_lookups`가 그 반쪽이다).
        closing->owner->on_request_closed(id);
    }

    void wait_for_request_lookups() noexcept
    {
        std::lock_guard lock { request_table_mutex() };
    }
} // namespace luil::net

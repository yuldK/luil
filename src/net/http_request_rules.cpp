#include "net/http_request_rules.h"

namespace luil::net {
    namespace {
        [[nodiscard]] bool header_text_is_safe(const std::u8string_view text) noexcept
        {
            for (const char8_t character : text)
                if (character == u8'\r' || character == u8'\n' || character == 0)
                    return false;
            return true;
        }

    } // namespace

    http_error make_http_error(const http_error_kind kind, const std::u8string_view message)
    {
        http_error error {};
        error.kind = kind;
        error.message = message;
        return error;
    }

    bool collect_request_headers(const http_request& request, std::vector<http_header>& headers, http_error& failure)
    {
        headers.clear();
        for (const http_header& header : request.headers)
        {
            // 헤더 이름은 토큰 문자만이다 (RFC 9110). 밖에서 온 값이 그대로 실리는 앱이
            // 부르는 사고가 헤더 주입이라, 선을 건드리기 전에 여기서 막는다.
            if (is_http_token(header.name) == false || header_text_is_safe(header.value) == false)
            {
                failure = make_http_error(http_error_kind::invalid_header, u8"A request header name or value is not allowed.");
                return false;
            }
            headers.push_back(header);
        }

        // 앱이 적은 줄을 기본값으로 덮으면 앱은 그 사실을 알 길이 없다.
        if (request.body.empty() == false && (request.content_type.empty() == false || find_header(request.headers, u8"content-type").empty()))
        {
            std::u8string_view content_type { request.content_type };
            if (content_type.empty())
                content_type = u8"application/octet-stream";
            if (header_text_is_safe(content_type) == false)
            {
                failure = make_http_error(http_error_kind::invalid_header, u8"The request content type is not allowed.");
                return false;
            }
            headers.push_back(http_header { std::u8string { u8"Content-Type" }, std::u8string { content_type } });
        }
        return true;
    }

    bool check_request_method(const http_request& request, http_error& failure)
    {
        if (request.method != http_method::extension)
        {
            if (request.method_name.empty())
                return true;
            failure = make_http_error(http_error_kind::invalid_method, u8"A method name is given but the method is not http_method::extension.");
            return false;
        }
        if (is_http_token(request.method_name) == false)
        {
            failure = make_http_error(http_error_kind::invalid_method, u8"The extension method name is not an RFC 9110 token.");
            return false;
        }
        // 표준 동사는 값으로 적는다 — 이름으로도 적을 수 있으면 두 길의 규칙(몸·재지정)이 갈린다.
        constexpr std::u8string_view reserved[] { u8"GET", u8"HEAD", u8"POST", u8"PUT", u8"PATCH", u8"DELETE", u8"OPTIONS", u8"CONNECT" };
        for (const std::u8string_view name : reserved)
            if (request.method_name == name)
            {
                failure = make_http_error(http_error_kind::invalid_method, u8"The extension method name is a standard method or CONNECT.");
                return false;
            }
        return true;
    }

    std::optional<http_method> redirected_method(const http_method method, const int status) noexcept
    {
        if (status == 303)
        {
            if (method == http_method::extension)
                return std::nullopt;
            return method == http_method::head ? http_method::head : http_method::get;
        }
        if ((status == 301 || status == 302) && method == http_method::post)
            return http_method::get;
        return method;
    }

    bool failure_is_permanent(const http_error_kind kind) noexcept
    {
        return kind == http_error_kind::invalid_url || kind == http_error_kind::unsupported_scheme || kind == http_error_kind::invalid_header || kind == http_error_kind::invalid_method;
    }

    std::u8string count_text(const std::size_t value)
    {
        if (value == 0)
            return std::u8string { u8"0" };

        std::u8string text {};
        std::size_t rest { value };
        while (rest != 0)
        {
            text.insert(text.begin(), static_cast<char8_t>(static_cast<std::size_t>(u8'0') + rest % 10u));
            rest /= 10u;
        }
        return text;
    }
} // namespace luil::net

#include "net/winhttp/http_url.h"

#include "net/winhttp/winhttp_error.h"
#include "win32/utf8.h"

#include <windows.h>

#include <winhttp.h>

#include <cstddef>
#include <limits>

namespace luil::net {
    std::optional<parsed_url> parse_http_url(const std::u8string_view url, http_error& error)
    {
        if (url.empty())
        {
            error = make_http_error(http_error_kind::invalid_url, u8"The request URL is empty.", 0);
            return std::nullopt;
        }
        // 헤더와 같은 규칙이다 — 밖에서 온 글이 그대로 주소에 실리는 앱에서 CR·LF는
        // 요청 줄 뒤에 헤더를 끼워 넣는 길이 된다 (`build_request_headers`의 짝).
        for (const char8_t character : url)
        {
            if (character == u8'\r' || character == u8'\n' || character == 0)
            {
                error = make_http_error(http_error_kind::invalid_url, u8"The request URL contains a control character.", 0);
                return std::nullopt;
            }
        }

        const auto wide { win32::utf8_to_utf16(url) };
        if (wide.value.has_value() == false)
        {
            error = make_http_error(http_error_kind::invalid_url, u8"The request URL is not valid UTF-8.", wide.error.has_value() ? wide.error->native_error : 0ul);
            return std::nullopt;
        }
        if (wide.value->size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
        {
            error = make_http_error(http_error_kind::invalid_url, u8"The request URL is too long.", 0);
            return std::nullopt;
        }

        // 길이 칸을 -1로 두면 WinHTTP가 **원본 글 안을 가리키는** 포인터로 답한다.
        // 버퍼를 따로 잡지 않는 갈래라 실패 경로에서 풀 것이 없다.
        URL_COMPONENTS components {};
        components.dwStructSize = static_cast<DWORD>(sizeof(components));
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);

        if (WinHttpCrackUrl(wide.value->c_str(), static_cast<DWORD>(wide.value->size()), 0, &components) == FALSE)
        {
            const unsigned long code { GetLastError() };
            if (code == static_cast<unsigned long>(ERROR_WINHTTP_UNRECOGNIZED_SCHEME))
                error = make_http_error(http_error_kind::unsupported_scheme, u8"The URL scheme is not http or https.", code);
            else
                error = make_http_error(http_error_kind::invalid_url, u8"The request URL could not be parsed.", code);
            return std::nullopt;
        }

        if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
        {
            error = make_http_error(http_error_kind::unsupported_scheme, u8"The URL scheme is not http or https.", 0);
            return std::nullopt;
        }
        if (components.lpszHostName == nullptr || components.dwHostNameLength == 0)
        {
            error = make_http_error(http_error_kind::invalid_url, u8"The request URL has no host.", 0);
            return std::nullopt;
        }

        parsed_url parsed {};
        parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
        parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
        parsed.port = components.nPort;
        if (components.lpszUrlPath != nullptr && components.dwUrlPathLength != 0)
            parsed.target.assign(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength != 0)
            parsed.target.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        // 빈 경로는 `/`다. 전송에 빈 글을 넘기면 요청 줄이 `GET  HTTP/1.1`이 된다.
        if (parsed.target.empty())
            parsed.target = L"/";
        return parsed;
    }
} // namespace luil::net

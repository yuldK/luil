#include "net/winhttp/winhttp_error.h"

#include <windows.h>

#include <winhttp.h>

#include <cstddef>

namespace luil::net {
    namespace {
        void append_hex_code(std::u8string& text, const unsigned long code)
        {
            constexpr std::u8string_view digits { u8"0123456789ABCDEF" };
            text += u8" (WinHTTP=0x";
            for (int shift { 28 }; shift >= 0; shift -= 4)
                text += digits[static_cast<std::size_t>((code >> shift) & 0xFul)];
            text += u8")";
        }

        // 깃발 하나를 글로 옮긴다. 표가 아니라 목록인 것은 여러 개가 함께 서기 때문이다.
        void append_secure_reason(std::u8string& text, const std::u8string_view reason)
        {
            if (text.empty() == false)
                text += u8", ";
            text += reason;
        }
    } // namespace

    http_error make_http_error(const http_error_kind kind, const std::u8string_view message, const unsigned long code)
    {
        http_error error {};
        error.kind = kind;
        error.message = message;
        error.native_error = code;
        if (code != 0)
            append_hex_code(error.message, code);
        return error;
    }

    std::u8string describe_secure_failure(const unsigned long secure_flags)
    {
        std::u8string reasons {};
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED) != 0)
            append_secure_reason(reasons, u8"certificate revocation check failed");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT) != 0)
            append_secure_reason(reasons, u8"invalid certificate");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED) != 0)
            append_secure_reason(reasons, u8"certificate revoked");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA) != 0)
            append_secure_reason(reasons, u8"untrusted certificate authority");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID) != 0)
            append_secure_reason(reasons, u8"certificate name mismatch");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID) != 0)
            append_secure_reason(reasons, u8"certificate expired");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE) != 0)
            append_secure_reason(reasons, u8"certificate used for the wrong purpose");
        if ((secure_flags & WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR) != 0)
            append_secure_reason(reasons, u8"secure channel error");
        return reasons;
    }

    http_error make_winhttp_error(const unsigned long code, const unsigned long secure_flags)
    {
        switch (code)
        {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return make_http_error(http_error_kind::name_not_resolved, u8"The server name could not be resolved.", code);
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return make_http_error(http_error_kind::cannot_connect, u8"Failed to connect to the server.", code);
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return make_http_error(http_error_kind::connection_lost, u8"The connection closed before the response was complete.", code);
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
            return make_http_error(http_error_kind::connection_lost, u8"The server sent a malformed response.", code);
        case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
            return make_http_error(http_error_kind::connection_lost, u8"The request handle was no longer usable.", code);
        case ERROR_WINHTTP_TIMEOUT:
            return make_http_error(http_error_kind::timed_out, u8"The request timed out.", code);
        case ERROR_WINHTTP_REDIRECT_FAILED:
            return make_http_error(http_error_kind::too_many_redirects, u8"The redirect could not be followed.", code);
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
            return make_http_error(http_error_kind::unsupported_scheme, u8"The URL scheme is not http or https.", code);
        case ERROR_WINHTTP_INVALID_URL:
            return make_http_error(http_error_kind::invalid_url, u8"The request URL could not be parsed.", code);
        case ERROR_WINHTTP_OPERATION_CANCELLED:
            return make_http_error(http_error_kind::cancelled, u8"The request was cancelled.", code);
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: {
            std::u8string message { u8"The secure connection could not be established." };
            const std::u8string reasons { describe_secure_failure(secure_flags) };
            if (reasons.empty() == false)
            {
                message += u8" (";
                message += reasons;
                message += u8")";
            }
            return make_http_error(http_error_kind::secure_failure, message, code);
        }
        default:
            break;
        }
        return make_http_error(http_error_kind::system_error, u8"The request failed.", code);
    }
} // namespace luil::net

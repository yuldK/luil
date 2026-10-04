#include "net/smb_rules.h"

#include <cerrno>
#include <limits>

namespace luil::net {
    namespace {
        [[nodiscard]] bool has_control(const std::u8string_view text) noexcept
        {
            for (const char8_t character : text)
                if (character < 0x20 || character == 0x7f)
                    return true;
            return false;
        }

        // 서버·공유 이름의 글자 규칙이다. 경로 구분자와 공백이 있으면 다른 것을 가리키게 된다.
        [[nodiscard]] bool plain_name(const std::u8string_view text) noexcept
        {
            if (text.empty() || has_control(text))
                return false;
            for (const char8_t character : text)
                if (character == u8' ' || character == u8'/' || character == u8'\\')
                    return false;
            return true;
        }

        [[nodiscard]] smb_error invalid(const std::u8string_view message)
        {
            smb_error error {};
            error.kind = smb_error_kind::invalid_request;
            error.message = message;
            return error;
        }
    } // namespace

    bool check_smb_request(const smb_request& request, const std::size_t max_read_bytes, smb_error& failure)
    {
        const smb_location& location { request.location };
        if (plain_name(location.server) == false)
        {
            failure = invalid(u8"The server is empty or has spaces, slashes or control characters.");
            return false;
        }
        // 공유 이름의 상한은 SMB가 정한 80자다.
        if (plain_name(location.share) == false || location.share.size() > 80u)
        {
            failure = invalid(u8"The share name is empty, too long, or has spaces, slashes or control characters.");
            return false;
        }
        if (has_control(location.domain) || has_control(location.user))
        {
            failure = invalid(u8"The domain or user name has control characters.");
            return false;
        }
        if (has_control(request.path) || request.path.find(u8'\\') != std::u8string::npos)
        {
            failure = invalid(u8"The path has control characters or backslashes. Separate it with '/'.");
            return false;
        }
        if (request.timeout.count() < 0)
        {
            failure = invalid(u8"The timeout is negative.");
            return false;
        }
        switch (request.operation)
        {
        case smb_operation::list:
        case smb_operation::stat:
            return true;
        case smb_operation::read:
            if (request.length == 0 || request.length > max_read_bytes)
            {
                failure = invalid(u8"The read length must be between 1 and max_read_bytes.");
                return false;
            }
            if (request.offset > std::numeric_limits<std::uint64_t>::max() - request.length)
            {
                failure = invalid(u8"The read range overflows.");
                return false;
            }
            return true;
        }
        failure = invalid(u8"The operation is not known.");
        return false;
    }

    std::string smb_share_path(const std::u8string_view path)
    {
        std::string result {};
        result.reserve(path.size());
        bool separator { false };
        for (const char8_t character : path)
        {
            if (character == u8'/')
            {
                separator = result.empty() == false;
                continue;
            }
            if (separator)
                result.push_back('/');
            separator = false;
            result.push_back(static_cast<char>(character));
        }
        return result;
    }

    std::u8string smb_last_component(const std::u8string_view path)
    {
        const std::string trimmed { smb_share_path(path) };
        const std::size_t slash { trimmed.rfind('/') };
        const std::string_view last { slash == std::string::npos ? std::string_view { trimmed } : std::string_view { trimmed }.substr(slash + 1) };
        return std::u8string { reinterpret_cast<const char8_t*>(last.data()), last.size() };
    }

    smb_error_kind smb_error_kind_for(const int status, const std::uint32_t nt_status, const bool connecting) noexcept
    {
        switch (nt_status)
        {
        case smb_status_wrong_password:
        case smb_status_logon_failure:
        case smb_status_account_restriction:
        case smb_status_password_expired:
        case smb_status_account_disabled:
            return smb_error_kind::logon_failure;
        case smb_status_access_denied:
            return smb_error_kind::access_denied;
        case smb_status_no_such_file:
        case smb_status_object_name_not_found:
        case smb_status_object_path_not_found:
        case smb_status_bad_network_name:
            return smb_error_kind::not_found;
        default:
            break;
        }
        switch (-status)
        {
        case ECONNREFUSED:
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ETIMEDOUT:
        case ECONNRESET:
            return smb_error_kind::cannot_connect;
        case EACCES:
        case EPERM:
            return smb_error_kind::access_denied;
        case ENOENT:
            return smb_error_kind::not_found;
        default:
            break;
        }
        return connecting ? smb_error_kind::cannot_connect : smb_error_kind::failed;
    }
} // namespace luil::net

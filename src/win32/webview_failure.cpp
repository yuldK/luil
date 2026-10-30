#include "win32/webview_failure.h"

#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <string>
#include <string_view>

namespace luil::win32 {
    unsigned webview_runtime_build_number(const wchar_t* const version) noexcept
    {
        if (version == nullptr)
            return 0;
        unsigned component { 0 };
        unsigned value { 0 };
        for (const wchar_t* cursor { version };; ++cursor)
        {
            if (*cursor >= L'0' && *cursor <= L'9')
            {
                value = value * 10 + static_cast<unsigned>(*cursor - L'0');
                continue;
            }
            if (component == 2)
                return value;
            if (*cursor != L'.')
                return 0;
            ++component;
            value = 0;
        }
    }

    webview_creation_failure webview_environment_failure_for(const HRESULT result) noexcept
    {
        if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            return webview_creation_failure::runtime_missing;
        return webview_creation_failure::environment_failed;
    }

    std::u8string webview_environment_failure_message(const HRESULT result)
    {
        if (webview_environment_failure_for(result) == webview_creation_failure::runtime_missing)
            return make_hresult_error(u8"The WebView2 runtime is not installed", result);
        return make_hresult_error(u8"Failed to create the WebView2 environment", result);
    }

    std::u8string webview_runtime_too_old_message(const wchar_t* const version)
    {
        std::u8string message { u8"The WebView2 runtime " };
        const auto found { version != nullptr ? utf16_to_utf8(std::wstring_view { version }) : utf_conversion_result<std::u8string> {} };
        if (found.value.has_value() && found.value->empty() == false)
            message += *found.value;
        else
            message += u8"(unknown version)";
        message += u8" is too old to enforce the webview policy; build ";
        for (const char digit : std::to_string(minimum_webview_runtime_build))
            message.push_back(static_cast<char8_t>(digit));
        message += u8" or newer is required.";
        return message;
    }
}

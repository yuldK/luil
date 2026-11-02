#include "win32/app_identity.h"

#include "luil/text/utf8_text.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <utility>

namespace luil::win32 {
    namespace {
        constexpr std::wstring_view identity_root { L"Software\\Classes\\AppUserModelId\\" };

        // 열린 레지스트리 키 하나를 쥐고 없어질 때 닫는다.
        class owned_key
        {
        public:
            owned_key() = default;
            owned_key(const owned_key&) = delete;
            owned_key& operator=(const owned_key&) = delete;
            ~owned_key()
            {
                if (key_ != nullptr)
                    static_cast<void>(RegCloseKey(key_));
            }

            [[nodiscard]] HKEY* receive() noexcept
            {
                return &key_;
            }

            [[nodiscard]] HKEY get() const noexcept
            {
                return key_;
            }

        private:
            HKEY key_ { nullptr };
        };

        [[nodiscard]] platform_result registry_failure(const std::u8string_view message, const LSTATUS status)
        {
            return { platform_status::failed, make_hresult_error(message, HRESULT_FROM_WIN32(static_cast<DWORD>(status))), std::nullopt };
        }

        // 그 값이 있고 같은 REG_SZ인가. 없거나 다른 형식이면 거짓이다.
        [[nodiscard]] bool value_matches(const HKEY key, const registry_value& value)
        {
            DWORD size { 0 };
            if (RegGetValueW(key, nullptr, value.name.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
                return false;
            std::wstring stored(size / sizeof(wchar_t), L'\0');
            if (RegGetValueW(key, nullptr, value.name.c_str(), RRF_RT_REG_SZ, nullptr, stored.data(), &size) != ERROR_SUCCESS)
                return false;
            // 받은 크기는 끝 NUL을 셈에 넣는다.
            stored.resize(size / sizeof(wchar_t));
            while (stored.empty() == false && stored.back() == L'\0')
                stored.pop_back();
            return stored == value.data;
        }

        [[nodiscard]] std::optional<std::wstring> wide(const std::u8string_view text)
        {
            return utf8_to_utf16(text).value;
        }
    }

    bool app_identity_id_valid(const std::u8string_view id) noexcept
    {
        if (id.empty() || text::utf8_is_valid(id) == false)
            return false;
        std::size_t units { 0 };
        std::size_t offset { 0 };
        while (offset < id.size())
        {
            const text::utf8_decoded decoded { text::utf8_decode(id, offset) };
            offset += decoded.length;
            const char32_t codepoint { decoded.codepoint };
            // 공백과 제어 문자(C0·DEL·C1, 줄바꿈 없는 공백, 전각 공백)다.
            const bool blank { codepoint <= 0x20 || (codepoint >= 0x7F && codepoint <= 0xA0) || codepoint == 0x3000 };
            if (blank || codepoint == U'\\')
                return false;
            units += codepoint > 0xFFFF ? 2 : 1;
            if (units > app_identity_id_limit)
                return false;
        }
        return true;
    }

    std::optional<identity_registry> make_identity_registry(const app_identity& identity, const std::u8string_view fallback_display_name)
    {
        if (app_identity_id_valid(identity.id) == false)
            return std::nullopt;
        const std::optional<std::wstring> id { wide(identity.id) };
        const std::u8string_view display { identity.display_name.empty() == false ? std::u8string_view { identity.display_name } : fallback_display_name.empty() == false ? fallback_display_name : std::u8string_view { identity.id } };
        const std::optional<std::wstring> display_name { wide(display) };
        const std::optional<std::wstring> icon { wide(identity.icon_path) };
        if (id.has_value() == false || display_name.has_value() == false || icon.has_value() == false)
            return std::nullopt;

        identity_registry registry {};
        registry.key = std::wstring { identity_root } + *id;
        registry.values.push_back({ L"DisplayName", *display_name });
        if (icon->empty() == false)
            registry.values.push_back({ L"IconUri", *icon });
        else
            registry.removed.push_back(L"IconUri");
        return registry;
    }

    identity_write_result write_identity_registry(const identity_registry& registry)
    {
        identity_write_result written {};
        owned_key key {};
        if (const LSTATUS status { RegCreateKeyExW(HKEY_CURRENT_USER, registry.key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, key.receive(), nullptr) };
            status != ERROR_SUCCESS)
        {
            written.result = registry_failure(u8"Failed to open the app identity registry key", status);
            return written;
        }
        for (const registry_value& value : registry.values)
        {
            if (value_matches(key.get(), value))
                continue;
            const DWORD bytes { static_cast<DWORD>((value.data.size() + 1) * sizeof(wchar_t)) };
            if (const LSTATUS status { RegSetValueExW(key.get(), value.name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.data.c_str()), bytes) }; status != ERROR_SUCCESS)
            {
                written.result = registry_failure(u8"Failed to write an app identity registry value", status);
                return written;
            }
            written.changed = true;
        }
        for (const std::wstring& name : registry.removed)
        {
            const LSTATUS status { RegDeleteValueW(key.get(), name.c_str()) };
            if (status == ERROR_SUCCESS)
                written.changed = true;
            else if (status != ERROR_FILE_NOT_FOUND)
            {
                written.result = registry_failure(u8"Failed to remove a stale app identity registry value", status);
                return written;
            }
        }
        written.result = { platform_status::ok, {}, std::nullopt };
        return written;
    }

    platform_result unregister_app_identity(const std::u8string_view id)
    {
        if (app_identity_id_valid(id) == false)
            return { platform_status::failed, u8"The app identity id is empty, too long, or contains spaces, control characters or backslashes.", std::nullopt };
        const std::optional<std::wstring> wide_id { wide(id) };
        if (wide_id.has_value() == false)
            return { platform_status::failed, u8"The app identity id cannot be converted to UTF-16.", std::nullopt };
        const std::wstring key { std::wstring { identity_root } + *wide_id };
        const LSTATUS status { RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) };
        if (status == ERROR_FILE_NOT_FOUND)
            return { platform_status::not_found, {}, std::nullopt };
        if (status != ERROR_SUCCESS)
            return registry_failure(u8"Failed to remove the app identity registry key", status);
        return { platform_status::ok, {}, std::nullopt };
    }
}

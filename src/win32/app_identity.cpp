#include "win32/app_identity.h"

#include "luil/text/utf8_text.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>
#include <wincrypt.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <utility>

namespace luil::win32 {
    namespace {
        constexpr std::wstring_view identity_root { L"Software\\Classes\\AppUserModelId\\" };
        constexpr std::wstring_view class_root { L"Software\\Classes\\CLSID\\" };
        constexpr std::wstring_view server_subkey { L"\\LocalServer32" };

        // CryptoAPI 손잡이를 쥐고 없어질 때 놓는다.
        class crypto_provider
        {
        public:
            crypto_provider() noexcept
            {
                if (CryptAcquireContextW(&provider_, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) == FALSE)
                    provider_ = 0;
            }
            crypto_provider(const crypto_provider&) = delete;
            crypto_provider& operator=(const crypto_provider&) = delete;
            ~crypto_provider()
            {
                if (hash_ != 0)
                    static_cast<void>(CryptDestroyHash(hash_));
                if (provider_ != 0)
                    static_cast<void>(CryptReleaseContext(provider_, 0));
            }

            // SHA-1 요약이다. 실패하면 빈 값이다.
            [[nodiscard]] std::optional<std::array<std::byte, 20>> sha1(const std::span<const std::byte> first, const std::span<const std::byte> second) noexcept
            {
                if (provider_ == 0 || CryptCreateHash(provider_, CALG_SHA1, 0, 0, &hash_) == FALSE)
                    return std::nullopt;
                if (CryptHashData(hash_, reinterpret_cast<const BYTE*>(first.data()), static_cast<DWORD>(first.size()), 0) == FALSE)
                    return std::nullopt;
                if (CryptHashData(hash_, reinterpret_cast<const BYTE*>(second.data()), static_cast<DWORD>(second.size()), 0) == FALSE)
                    return std::nullopt;
                std::array<std::byte, 20> digest {};
                DWORD size { static_cast<DWORD>(digest.size()) };
                if (CryptGetHashParam(hash_, HP_HASHVAL, reinterpret_cast<BYTE*>(digest.data()), &size, 0) == FALSE || size != digest.size())
                    return std::nullopt;
                return digest;
            }

        private:
            HCRYPTPROV provider_ { 0 };
            HCRYPTHASH hash_ { 0 };
        };

        // GUID를 RFC 4122의 바이트 순서(앞 세 칸은 큰 끝)로 편다.
        [[nodiscard]] std::array<std::byte, 16> network_bytes(const GUID& value) noexcept
        {
            std::array<std::byte, 16> bytes {};
            for (int index = 0; index < 4; ++index)
                bytes[static_cast<std::size_t>(index)] = static_cast<std::byte>((value.Data1 >> (24 - 8 * index)) & 0xFFu);
            bytes[4] = static_cast<std::byte>((value.Data2 >> 8) & 0xFFu);
            bytes[5] = static_cast<std::byte>(value.Data2 & 0xFFu);
            bytes[6] = static_cast<std::byte>((value.Data3 >> 8) & 0xFFu);
            bytes[7] = static_cast<std::byte>(value.Data3 & 0xFFu);
            for (std::size_t index = 0; index < 8; ++index)
                bytes[8 + index] = static_cast<std::byte>(value.Data4[index]);
            return bytes;
        }

        [[nodiscard]] std::uint32_t byte_value(const std::byte value) noexcept
        {
            return std::to_integer<std::uint32_t>(value);
        }

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

    std::optional<GUID> name_based_uuid(const GUID& name_space, const std::span<const std::byte> name)
    {
        const std::array<std::byte, 16> prefix { network_bytes(name_space) };
        crypto_provider provider {};
        std::optional<std::array<std::byte, 20>> digest { provider.sha1(prefix, name) };
        if (digest.has_value() == false)
            return std::nullopt;
        std::array<std::byte, 20>& bytes { *digest };
        // 판(5)과 변형(RFC 4122) 비트를 건다.
        bytes[6] = (bytes[6] & std::byte { 0x0F }) | std::byte { 0x50 };
        bytes[8] = (bytes[8] & std::byte { 0x3F }) | std::byte { 0x80 };
        GUID value {};
        value.Data1 = (byte_value(bytes[0]) << 24) | (byte_value(bytes[1]) << 16) | (byte_value(bytes[2]) << 8) | byte_value(bytes[3]);
        value.Data2 = static_cast<unsigned short>((byte_value(bytes[4]) << 8) | byte_value(bytes[5]));
        value.Data3 = static_cast<unsigned short>((byte_value(bytes[6]) << 8) | byte_value(bytes[7]));
        for (std::size_t index = 0; index < 8; ++index)
            value.Data4[index] = static_cast<unsigned char>(byte_value(bytes[8 + index]));
        return value;
    }

    std::optional<GUID> activator_clsid(const std::u8string_view id)
    {
        return name_based_uuid(activator_namespace, std::as_bytes(std::span<const char8_t> { id.data(), id.size() }));
    }

    std::wstring guid_text(const GUID& value)
    {
        wchar_t text[39] {};
        std::swprintf(text, std::size(text), L"{%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}"
            , static_cast<unsigned long>(value.Data1)
            , value.Data2
            , value.Data3
            , value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3]
            , value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]
        );
        return text;
    }

    std::optional<identity_registry> make_identity_registry(const app_identity& identity, const std::u8string_view fallback_display_name, const std::wstring_view executable)
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
        if (identity.activator == false)
        {
            registry.removed.push_back(L"CustomActivator");
            return registry;
        }
        const std::optional<GUID> clsid { activator_clsid(identity.id) };
        if (clsid.has_value() == false)
            return std::nullopt;
        const std::wstring clsid_text { guid_text(*clsid) };
        registry.values.push_back({ L"CustomActivator", clsid_text });
        registry.server_key = std::wstring { class_root } + clsid_text + std::wstring { server_subkey };
        registry.server_command = L"\"" + std::wstring { executable } + L"\" " + std::wstring { activator_flag };
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
        if (registry.server_key.empty() == false)
        {
            // 실행 파일을 옮기면 다음 실행이 경로를 고쳐 쓴다.
            owned_key server {};
            if (const LSTATUS status { RegCreateKeyExW(HKEY_CURRENT_USER, registry.server_key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, server.receive(), nullptr) };
                status != ERROR_SUCCESS)
            {
                written.result = registry_failure(u8"Failed to open the notification activator registry key", status);
                return written;
            }
            const registry_value command { L"", registry.server_command };
            if (value_matches(server.get(), command) == false)
            {
                const DWORD bytes { static_cast<DWORD>((command.data.size() + 1) * sizeof(wchar_t)) };
                if (const LSTATUS status { RegSetValueExW(server.get(), nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.data.c_str()), bytes) }; status != ERROR_SUCCESS)
                {
                    written.result = registry_failure(u8"Failed to write the notification activator command", status);
                    return written;
                }
                written.changed = true;
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
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
            return registry_failure(u8"Failed to remove the app identity registry key", status);
        // 활성화기를 끈 채 쓴 식별에도 앞 실행의 CLSID가 남았을 수 있어 늘 본다.
        LSTATUS server_status { ERROR_FILE_NOT_FOUND };
        if (const std::optional<GUID> clsid { activator_clsid(id) }; clsid.has_value())
        {
            const std::wstring server { std::wstring { class_root } + guid_text(*clsid) };
            server_status = RegDeleteTreeW(HKEY_CURRENT_USER, server.c_str());
            if (server_status != ERROR_SUCCESS && server_status != ERROR_FILE_NOT_FOUND)
                return registry_failure(u8"Failed to remove the notification activator registry key", server_status);
        }
        if (status == ERROR_FILE_NOT_FOUND && server_status == ERROR_FILE_NOT_FOUND)
            return { platform_status::not_found, {}, std::nullopt };
        return { platform_status::ok, {}, std::nullopt };
    }
}

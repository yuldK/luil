#include "luil/app/secret_store.h"

#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <wincred.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>

// 비밀 저장소의 Windows 쪽이다 — 자격 증명 관리자의 일반 자격 증명 (secret-store-design.md).
namespace luil {
    namespace {
        // 옛 접두사와 겹치지 않아 옛 이름의 내용이 새 키처럼 보일 수 없다.
        constexpr std::wstring_view target_prefix { L"luil-v2:" };
        // CredWriteW가 새 키의 존재 확인과 쓰기를 함께 하지 않는다.
        std::mutex mutation_lock {};

        [[nodiscard]] secret_store_result success() noexcept
        {
            return { secret_store_status::ok, {} };
        }

        [[nodiscard]] secret_store_result failure(std::u8string message)
        {
            return { secret_store_status::failed, std::move(message) };
        }

        void append_hex(std::wstring& target, const std::u8string_view text)
        {
            constexpr wchar_t digits[] { L"0123456789abcdef" };
            for (const char8_t byte : text)
            {
                target.push_back(digits[byte >> 4u]);
                target.push_back(digits[byte & 0x0fu]);
            }
        }

        [[nodiscard]] secret_store_result make_target(const std::u8string_view service, const std::u8string_view name, std::wstring& target)
        {
            constexpr std::size_t max_input_bytes { (CRED_MAX_GENERIC_TARGET_NAME_LENGTH - target_prefix.size() - 1u) / 2u };
            if (service.size() > max_input_bytes || name.size() > max_input_bytes - service.size())
                return failure(u8"The encoded secret target is too long for the credential store.");
            if (service.empty() || name.empty()
                || win32::utf8_to_utf16(service).value.has_value() == false
                || win32::utf8_to_utf16(name).value.has_value() == false)
                return failure(u8"The secret needs a service and a name in valid UTF-8.");

            // 소문자 ASCII 16진만 쓰면 OS가 대소문자를 무시해도 바이트 구분을 잃지 않는다.
            target.reserve(target_prefix.size() + 2u * (service.size() + name.size()) + 1u);
            target = target_prefix;
            append_hex(target, service);
            target.push_back(L'-');
            append_hex(target, name);
            return success();
        }

        [[nodiscard]] secret_store_result last_error(const std::u8string_view what)
        {
            return failure(win32::make_last_error(what));
        }

        [[nodiscard]] secret_store_result write_target(const std::wstring& target, const std::span<const std::uint8_t> secret)
        {
            if (secret.size() > secret_store_maximum_bytes)
                return failure(u8"The secret is larger than the 2560 byte store limit.");
            CREDENTIALW credential {};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = const_cast<wchar_t*>(target.c_str());
            credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
            credential.CredentialBlob = const_cast<std::uint8_t*>(secret.data());
            // 이 기기의 이 사용자에게만 남는다. 도메인 로밍 프로필로 옮겨 가지 않는다.
            credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
            if (CredWriteW(&credential, 0) == FALSE)
                return last_error(u8"Failed to write the secret to the credential store");
            return success();
        }

        using owned_credential = std::unique_ptr<CREDENTIALW, decltype(&CredFree)>;

        [[nodiscard]] secret_store_result read_target(const std::wstring& target, owned_credential& credential)
        {
            PCREDENTIALW value { nullptr };
            if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &value) == FALSE)
            {
                if (GetLastError() == ERROR_NOT_FOUND)
                    return { secret_store_status::not_found, {} };
                return last_error(u8"Failed to read the secret from the credential store");
            }
            credential.reset(value);
            return success();
        }
    }

    secret_store_result store_secret(const std::u8string_view service, const std::u8string_view name, const std::span<const std::uint8_t> secret)
    {
        std::wstring target {};
        secret_store_result result { make_target(service, name, target) };
        if (result.status != secret_store_status::ok)
            return result;
        const std::lock_guard lock { mutation_lock };
        return write_target(target, secret);
    }

    secret_store_result load_secret(const std::u8string_view service, const std::u8string_view name, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        std::wstring target {};
        secret_store_result result { make_target(service, name, target) };
        if (result.status != secret_store_status::ok)
            return result;
        owned_credential credential { nullptr, &CredFree };
        result = read_target(target, credential);
        if (result.status == secret_store_status::ok && credential->CredentialBlobSize != 0)
            secret.assign(credential->CredentialBlob, credential->CredentialBlob + credential->CredentialBlobSize);
        return result;
    }

    secret_store_result erase_secret(const std::u8string_view service, const std::u8string_view name)
    {
        std::wstring target {};
        secret_store_result result { make_target(service, name, target) };
        if (result.status != secret_store_status::ok)
            return result;
        const std::lock_guard lock { mutation_lock };
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == FALSE)
        {
            if (GetLastError() == ERROR_NOT_FOUND)
                return { secret_store_status::not_found, {} };
            return last_error(u8"Failed to erase the secret from the credential store");
        }
        return success();
    }

    secret_store_result migrate_legacy_win32_secret(const std::u8string_view service, const std::u8string_view name)
    {
        std::wstring target {};
        secret_store_result result { make_target(service, name, target) };
        if (result.status != secret_store_status::ok)
            return result;
        const std::lock_guard lock { mutation_lock };
        owned_credential credential { nullptr, &CredFree };
        result = read_target(target, credential);
        if (result.status == secret_store_status::ok)
            return { secret_store_status::already_exists, {} };
        if (result.status != secret_store_status::not_found)
            return result;

        // 옛 구현의 이름과 Win32에 넘기던 방식을 재현하므로 소유권은 앱이 확인해야 한다.
        std::u8string legacy_name { u8"luil:" };
        legacy_name += service;
        legacy_name += u8'/';
        legacy_name += name;
        const auto legacy_target { win32::utf8_to_utf16(legacy_name) };
        if (legacy_target.value.has_value() == false)
            return failure(u8"Failed to prepare the legacy credential target.");
        result = read_target(*legacy_target.value, credential);
        if (result.status != secret_store_status::ok)
            return result;
        const std::span<const std::uint8_t> secret { credential->CredentialBlob, credential->CredentialBlobSize };
        return write_target(target, secret);
    }
}

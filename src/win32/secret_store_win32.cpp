#include "luil/app/secret_store.h"

#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <wincred.h>

#include <optional>
#include <string>

// 비밀 저장소의 Windows 쪽이다 — 자격 증명 관리자의 일반 자격 증명 (secret-store-design.md).
namespace luil {
    namespace {
        [[nodiscard]] secret_store_result success() noexcept
        {
            return { secret_store_status::ok, {} };
        }

        [[nodiscard]] secret_store_result failure(std::u8string message)
        {
            return { secret_store_status::failed, std::move(message) };
        }

        // 대상 이름이다. 사용자가 "자격 증명 관리자"에서 이 이름으로 본다.
        [[nodiscard]] std::optional<std::wstring> target_of(const std::u8string_view service, const std::u8string_view name)
        {
            if (service.empty() || name.empty())
                return std::nullopt;
            std::u8string target { u8"luil:" };
            target += service;
            target += u8'/';
            target += name;
            return win32::utf8_to_utf16(target).value;
        }

        [[nodiscard]] secret_store_result last_error(const std::u8string_view what)
        {
            return failure(win32::make_hresult_error(what, HRESULT_FROM_WIN32(GetLastError())));
        }
    } // namespace

    secret_store_result store_secret(const std::u8string_view service, const std::u8string_view name, const std::span<const std::uint8_t> secret)
    {
        const std::optional<std::wstring> target { target_of(service, name) };
        if (target.has_value() == false)
            return failure(u8"The secret needs a service and a name in valid UTF-8.");
        if (secret.size() > secret_store_maximum_bytes)
            return failure(u8"The secret is larger than the 2560 byte store limit.");
        CREDENTIALW credential {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t*>(target->c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob = const_cast<std::uint8_t*>(secret.data());
        // 이 기기의 이 사용자에게만 남는다. 도메인 로밍 프로필로 옮겨 가지 않는다.
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (CredWriteW(&credential, 0) == FALSE)
            return last_error(u8"Failed to write the secret to the credential store");
        return success();
    }

    secret_store_result load_secret(const std::u8string_view service, const std::u8string_view name, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        const std::optional<std::wstring> target { target_of(service, name) };
        if (target.has_value() == false)
            return failure(u8"The secret needs a service and a name in valid UTF-8.");
        PCREDENTIALW credential { nullptr };
        if (CredReadW(target->c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE)
        {
            if (GetLastError() == ERROR_NOT_FOUND)
                return { secret_store_status::not_found, {} };
            return last_error(u8"Failed to read the secret from the credential store");
        }
        secret.assign(credential->CredentialBlob, credential->CredentialBlob + credential->CredentialBlobSize);
        CredFree(credential);
        return success();
    }

    secret_store_result erase_secret(const std::u8string_view service, const std::u8string_view name)
    {
        const std::optional<std::wstring> target { target_of(service, name) };
        if (target.has_value() == false)
            return failure(u8"The secret needs a service and a name in valid UTF-8.");
        if (CredDeleteW(target->c_str(), CRED_TYPE_GENERIC, 0) == FALSE)
        {
            if (GetLastError() == ERROR_NOT_FOUND)
                return { secret_store_status::not_found, {} };
            return last_error(u8"Failed to erase the secret from the credential store");
        }
        return success();
    }
} // namespace luil

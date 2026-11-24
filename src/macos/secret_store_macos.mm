#include "luil/app/secret_store.h"

#include "macos/macos_text.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <mutex>
#include <string>
#include <utility>

// 비밀 저장소의 macOS 쪽이다 — Keychain의 일반 암호 항목 (secret-store-design.md).
//  - `service`는 `kSecAttrService`, `name`은 `kSecAttrAccount`다. 사용자가 Keychain Access에서 그 이름으로 보고 지울 수 있다.
//  - 파일 기반 로그인 keychain이다. 데이터 보호 keychain(`kSecUseDataProtectionKeychain`)은 서명 권한(keychain-access-groups)이
//    있어야 해서 서명하지 않은 개발 빌드와 test 실행 파일에서 `errSecMissingEntitlement`로 실패한다.
//  - 항목은 기본 접근 목록으로 만든다. 만든 프로그램이 믿는 앱으로 들어가 그 프로그램은 묻는 창 없이 읽고 고치고 지운다.
//    접근 목록이나 `kSecAttrAccess`를 직접 주면 그 약속이 깨지므로 주지 않는다.
namespace luil {
    namespace {
        // 같은 짝을 두 thread가 함께 쓰면 "없음 → 더하기"와 "있음 → 고치기"가 엇갈린다.
        std::mutex mutation_lock {};

        [[nodiscard]] secret_store_result success() noexcept
        {
            return { secret_store_status::ok, {} };
        }

        [[nodiscard]] secret_store_result failure(std::u8string message)
        {
            return { secret_store_status::failed, std::move(message) };
        }

        // OS의 오류 글과 번호를 붙인 진단 영문이다.
        [[nodiscard]] secret_store_result os_failure(const std::u8string_view what, const OSStatus status)
        {
            std::u8string message { what };
            message += u8" (";
            NSString* const text { CFBridgingRelease(SecCopyErrorMessageString(status, nullptr)) };
            if (text != nil)
            {
                message += macos::from_ns_string(text);
                message += u8", ";
            }
            const std::string number { "OSStatus " + std::to_string(status) };
            message.append(number.begin(), number.end());
            message += u8").";
            return failure(std::move(message));
        }

        // 항목 하나를 가리키는 물음이다. 이름이 비었거나 UTF-8이 아니거나 NUL을 품었으면 nil이다.
        //  - Keychain은 속성을 NUL에서 자른다. `name\0a`와 `name\0b`가 같은 항목이 되어 남의 값을 덮으므로 받지 않는다.
        [[nodiscard]] NSMutableDictionary* item_query(const std::u8string_view service, const std::u8string_view name)
        {
            if (service.empty() || name.empty() || service.find(u8'\0') != std::u8string_view::npos || name.find(u8'\0') != std::u8string_view::npos)
                return nil;
            NSString* const service_text { macos::make_ns_string(service) };
            NSString* const name_text { macos::make_ns_string(name) };
            if (service_text == nil || name_text == nil)
                return nil;
            return [@{
                (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
                (__bridge id)kSecAttrService : service_text,
                (__bridge id)kSecAttrAccount : name_text,
            } mutableCopy];
        }

        [[nodiscard]] secret_store_result invalid_names()
        {
            return failure(u8"The secret needs a service and a name in valid UTF-8 without NUL characters.");
        }
    }

    secret_store_result store_secret(const std::u8string_view service, const std::u8string_view name, const std::span<const std::uint8_t> secret)
    {
        @autoreleasepool
        {
            NSMutableDictionary* const query { item_query(service, name) };
            if (query == nil)
                return invalid_names();
            if (secret.size() > secret_store_maximum_bytes)
                return failure(u8"The secret is larger than the 2560 byte store limit.");
            NSData* const value { [NSData dataWithBytes:secret.data() length:secret.size()] };
            const std::lock_guard lock { mutation_lock };
            NSMutableDictionary* const added { [query mutableCopy] };
            added[(__bridge id)kSecValueData] = value;
            OSStatus status { SecItemAdd((__bridge CFDictionaryRef)added, nullptr) };
            if (status == errSecDuplicateItem)
            {
                // 같은 짝에 다시 쓰면 덮는다. 항목을 지우고 새로 만들지 않아 접근 목록과 만든 때가 그대로 남는다.
                status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef) @{ (__bridge id)kSecValueData : value });
                // 그 사이 다른 프로세스가 지웠다. 한 번 더 더한다.
                if (status == errSecItemNotFound)
                    status = SecItemAdd((__bridge CFDictionaryRef)added, nullptr);
            }
            if (status != errSecSuccess)
                return os_failure(u8"Failed to write the secret to the keychain", status);
            return success();
        }
    }

    secret_store_result load_secret(const std::u8string_view service, const std::u8string_view name, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        @autoreleasepool
        {
            NSMutableDictionary* const query { item_query(service, name) };
            if (query == nil)
                return invalid_names();
            query[(__bridge id)kSecReturnData] = @YES;
            query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
            CFTypeRef found { nullptr };
            const OSStatus status { SecItemCopyMatching((__bridge CFDictionaryRef)query, &found) };
            NSData* const value { CFBridgingRelease(found) };
            if (status == errSecItemNotFound)
                return { secret_store_status::not_found, {} };
            if (status != errSecSuccess)
                return os_failure(u8"Failed to read the secret from the keychain", status);
            // 빈 값도 값이다. OS가 데이터 없이 답하면 빈 값으로 본다.
            if ([value isKindOfClass:[NSData class]] && value.length != 0)
            {
                const auto* const bytes { static_cast<const std::uint8_t*>(value.bytes) };
                secret.assign(bytes, bytes + value.length);
            }
            return success();
        }
    }

    secret_store_result erase_secret(const std::u8string_view service, const std::u8string_view name)
    {
        @autoreleasepool
        {
            NSMutableDictionary* const query { item_query(service, name) };
            if (query == nil)
                return invalid_names();
            const std::lock_guard lock { mutation_lock };
            const OSStatus status { SecItemDelete((__bridge CFDictionaryRef)query) };
            if (status == errSecItemNotFound)
                return { secret_store_status::not_found, {} };
            if (status != errSecSuccess)
                return os_failure(u8"Failed to erase the secret from the keychain", status);
            return success();
        }
    }

    secret_store_result migrate_legacy_win32_secret(std::u8string_view, std::u8string_view)
    {
        // 옮길 옛 Windows 항목이 macOS에는 없다.
        return { secret_store_status::unsupported, u8"There are no legacy Windows secrets on macOS." };
    }
}

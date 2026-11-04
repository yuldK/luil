#pragma once

#include "luil/win32/win32_window.h"

#include <windows.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 앱 식별(AUMID)의 검증과 레지스트리 항목이다 (os-notification-design.md의 "앱 식별(Windows)").
//  - 무엇을 쓸지(`make_identity_registry`)는 순수 계산이고, 쓰기·지우기만 레지스트리를 부른다.
//  - 프로세스 AUMID와 알림 개체는 notification_win32가 맡는다.
namespace luil::win32 {
    // AUMID의 UTF-16 길이 상한이다 (셸의 상한).
    inline constexpr std::size_t app_identity_id_limit { 128 };

    // 1~128 UTF-16 코드 단위, 올바른 UTF-8, 공백·제어 문자·백슬래시가 없는가.
    //  - 백슬래시는 레지스트리 키의 구분자라 id가 하위 키로 갈라진다.
    [[nodiscard]] bool app_identity_id_valid(std::u8string_view id) noexcept;

    struct registry_value
    {
        std::wstring name {};
        std::wstring data {};

        [[nodiscard]] bool operator==(const registry_value&) const = default;
    };

    // 활성화기 CLSID를 짓는 이름 공간이다 (결정 3). luil이 한 번 고른 값이라 바꾸면 모든 앱의 CLSID가 바뀐다.
    inline constexpr GUID activator_namespace { 0x6c75696cu, 0x9a1eu, 0x4b5cu, { 0x8d, 0x27, 0x3e, 0x51, 0xa0, 0x6b, 0x94, 0xc2 } };

    // RFC 4122 이름 기반 UUID 판 5다 (SHA-1). 이름 공간과 이름 바이트가 같으면 늘 같다. SHA-1을 얻지 못하면 빈 값이다.
    [[nodiscard]] std::optional<GUID> name_based_uuid(const GUID& name_space, std::span<const std::byte> name);

    // AUMID의 UTF-8 바이트로 지은 활성화기 CLSID다. 앱이 GUID를 고르지 않아 예제를 베낀 두 앱이 부딪히지 않는다.
    [[nodiscard]] std::optional<GUID> activator_clsid(std::u8string_view id);

    // `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` 꼴의 대문자 글이다.
    [[nodiscard]] std::wstring guid_text(const GUID& value);

    // 활성화기 명령 줄의 표식이다. COM이 이 줄로 앱을 띄우면 `notification_launch`가 참이다.
    inline constexpr std::wstring_view activator_flag { L"-luil-notification" };

    // `HKCU` 아래에 쓸 것이다.
    struct identity_registry
    {
        // `Software\Classes\AppUserModelId\<id>`다.
        std::wstring key {};
        // 쓸 문자열 값이다 (REG_SZ).
        std::vector<registry_value> values {};
        // 있으면 지울 값 이름이다. 앞 실행이 쓴 것을 이번 설정이 뺐을 때다.
        std::vector<std::wstring> removed {};
        // 활성화기의 `Software\Classes\CLSID\{clsid}\LocalServer32`다. 활성화기가 없으면 비었다.
        std::wstring server_key {};
        // 그 키의 기본 값이다: `"<실행 파일>" -luil-notification`.
        std::wstring server_command {};
    };

    // 식별에서 레지스트리 항목을 짓는다. id가 규칙 밖이거나 글을 UTF-16으로 옮기지 못하면 빈 값이다.
    //  - `DisplayName`은 `display_name`, 비면 `fallback_display_name`, 그것도 비면 id다.
    //  - `IconUri`는 `icon_path`가 있을 때만 쓰고 없으면 지울 값이다.
    //  - `activator`면 `CustomActivator`에 CLSID를 쓰고 `executable`(실행 파일의 절대 경로)을 띄울 서버 키를 짓는다.
    //    아니면 `CustomActivator`가 지울 값이다.
    [[nodiscard]] std::optional<identity_registry> make_identity_registry(const app_identity& identity, std::u8string_view fallback_display_name, std::wstring_view executable);

    struct identity_write_result
    {
        platform_result result {};
        // 값 하나라도 새로 썼거나 지웠다. 같은 값이면 쓰지 않는다.
        bool changed { false };
    };

    // 항목을 `HKCU`에 쓴다. 키가 없으면 만든다.
    [[nodiscard]] identity_write_result write_identity_registry(const identity_registry& registry);
}

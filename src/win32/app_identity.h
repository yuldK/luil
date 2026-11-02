#pragma once

#include "luil/win32/win32_window.h"

#include <optional>
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

    // `HKCU` 아래에 쓸 것이다.
    struct identity_registry
    {
        // `Software\Classes\AppUserModelId\<id>`다.
        std::wstring key {};
        // 쓸 문자열 값이다 (REG_SZ).
        std::vector<registry_value> values {};
        // 있으면 지울 값 이름이다. 앞 실행이 쓴 것을 이번 설정이 뺐을 때다.
        std::vector<std::wstring> removed {};
    };

    // 식별에서 레지스트리 항목을 짓는다. id가 규칙 밖이거나 글을 UTF-16으로 옮기지 못하면 빈 값이다.
    //  - `DisplayName`은 `display_name`, 비면 `fallback_display_name`, 그것도 비면 id다.
    //  - `IconUri`는 `icon_path`가 있을 때만 쓰고 없으면 지울 값이다.
    [[nodiscard]] std::optional<identity_registry> make_identity_registry(const app_identity& identity, std::u8string_view fallback_display_name);

    struct identity_write_result
    {
        platform_result result {};
        // 값 하나라도 새로 썼거나 지웠다. 같은 값이면 쓰지 않는다.
        bool changed { false };
    };

    // 항목을 `HKCU`에 쓴다. 키가 없으면 만든다.
    [[nodiscard]] identity_write_result write_identity_registry(const identity_registry& registry);
}

#include "luil/app/notification.h"
#include "luil/ui/image_decode.h"
#include "win32/app_identity.h"
#include "win32/notification_win32.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// 앱 식별(AUMID)의 검증과 레지스트리 항목을 본다 (os-notification-design.md의 "앱 식별(Windows)").
//  - 왕복 test는 `luil.test.<pid>` id로 HKCU에 쓰고 test가 지운다. 앞 실행이 남긴 `luil.test.*`는 먼저 지운다.
//  - 프로세스 AUMID는 걸지 않는다. 이 test 실행 파일의 다른 창이 작업 표시줄에서 갈리지 않게 한다.

namespace {
    constexpr const wchar_t* identity_root { L"Software\\Classes\\AppUserModelId" };

    [[nodiscard]] std::u8string ascii(const std::string& text)
    {
        return std::u8string { text.begin(), text.end() };
    }

    [[nodiscard]] std::u8string test_identity()
    {
        return ascii("luil.test." + std::to_string(GetCurrentProcessId()));
    }

    // 앞 실행이 지우지 못하고 남긴 test 식별을 지운다.
    void remove_stale_test_identities()
    {
        HKEY root { nullptr };
        if (RegOpenKeyExW(HKEY_CURRENT_USER, identity_root, 0, KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS)
            return;
        std::vector<std::wstring> stale {};
        wchar_t name[256] {};
        for (DWORD index = 0;; ++index)
        {
            DWORD length { static_cast<DWORD>(std::size(name)) };
            if (RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            const std::wstring key { name, length };
            if (key.starts_with(L"luil.test."))
                stale.push_back(key);
        }
        RegCloseKey(root);
        for (const std::wstring& key : stale)
        {
            // test id는 ASCII뿐이다.
            std::u8string narrow {};
            for (const wchar_t unit : key)
                narrow.push_back(static_cast<char8_t>(unit));
            static_cast<void>(luil::win32::unregister_app_identity(narrow));
        }
    }

    [[nodiscard]] bool key_exists(const std::wstring& key)
    {
        HKEY opened { nullptr };
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &opened) != ERROR_SUCCESS)
            return false;
        RegCloseKey(opened);
        return true;
    }

    [[nodiscard]] std::wstring read_value(const std::wstring& key, const wchar_t* const name)
    {
        wchar_t buffer[512] {};
        DWORD size { sizeof(buffer) };
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
            return {};
        return buffer;
    }
}

TEST_CASE("An app identity id is 1 to 128 UTF-16 units without blanks or backslashes", "[win32][notification][identity]")
{
    REQUIRE(luil::win32::app_identity_id_valid(u8"Lusain.Demo"));
    REQUIRE(luil::win32::app_identity_id_valid(std::u8string(128, u8'a')));
    REQUIRE(luil::win32::app_identity_id_valid(std::u8string(129, u8'a')) == false);
    REQUIRE(luil::win32::app_identity_id_valid(u8"") == false);
    REQUIRE(luil::win32::app_identity_id_valid(u8"two words") == false);
    REQUIRE(luil::win32::app_identity_id_valid(u8"tab\there") == false);
    REQUIRE(luil::win32::app_identity_id_valid(u8"a\\b") == false);
    REQUIRE(luil::win32::app_identity_id_valid(u8"\u3000wide") == false);
    // BMP 밖 글자는 두 단위로 센다.
    REQUIRE(luil::win32::app_identity_id_valid(std::u8string(126, u8'a') + u8"\U0001F600"));
    REQUIRE(luil::win32::app_identity_id_valid(std::u8string(127, u8'a') + u8"\U0001F600") == false);
    REQUIRE(luil::win32::app_identity_id_valid(std::u8string { u8"bad " } + static_cast<char8_t>(0xC3)) == false);
}

TEST_CASE("The identity registry names the key, the display name and the icon", "[win32][notification][identity]")
{
    luil::win32::app_identity identity {};
    identity.id = u8"Lusain.Demo";
    identity.icon_path = u8"C:\\app\\icon.png";
    const std::optional<luil::win32::identity_registry> registry { luil::win32::make_identity_registry(identity, u8"Caption") };
    REQUIRE(registry.has_value());
    REQUIRE(registry->key == L"Software\\Classes\\AppUserModelId\\Lusain.Demo");
    // 표시 이름이 비면 caption 제목이다.
    REQUIRE(registry->values == std::vector<luil::win32::registry_value> { { L"DisplayName", L"Caption" }, { L"IconUri", L"C:\\app\\icon.png" } });
    REQUIRE(registry->removed.empty());

    identity.display_name = u8"\uB370\uBAA8";
    identity.icon_path.clear();
    const std::optional<luil::win32::identity_registry> named { luil::win32::make_identity_registry(identity, u8"Caption") };
    REQUIRE(named->values == std::vector<luil::win32::registry_value> { { L"DisplayName", L"\uB370\uBAA8" } });
    // 아이콘을 뺀 설정은 앞 실행이 쓴 값을 지운다.
    REQUIRE(named->removed == std::vector<std::wstring> { L"IconUri" });

    // 이름이 모두 비면 id다.
    identity.display_name.clear();
    REQUIRE(luil::win32::make_identity_registry(identity, u8"")->values.front().data == L"Lusain.Demo");

    identity.id = u8"bad id";
    REQUIRE(luil::win32::make_identity_registry(identity, u8"Caption").has_value() == false);
}

TEST_CASE("Writing the identity twice changes nothing and unregistering removes the key", "[win32][notification][identity]")
{
    remove_stale_test_identities();
    luil::win32::app_identity identity {};
    identity.id = test_identity();
    identity.display_name = u8"luil test";
    identity.icon_path = u8"C:\\nowhere\\icon.png";
    const std::optional<luil::win32::identity_registry> registry { luil::win32::make_identity_registry(identity, u8"") };
    REQUIRE(registry.has_value());

    const luil::win32::identity_write_result first { luil::win32::write_identity_registry(*registry) };
    REQUIRE(first.result.ok());
    REQUIRE(first.changed);
    REQUIRE(read_value(registry->key, L"DisplayName") == L"luil test");
    REQUIRE(read_value(registry->key, L"IconUri") == L"C:\\nowhere\\icon.png");

    // 같은 값은 다시 쓰지 않는다.
    const luil::win32::identity_write_result second { luil::win32::write_identity_registry(*registry) };
    REQUIRE(second.result.ok());
    REQUIRE(second.changed == false);

    // 아이콘을 빼면 그 값만 지운다.
    identity.icon_path.clear();
    const luil::win32::identity_write_result third { luil::win32::write_identity_registry(*luil::win32::make_identity_registry(identity, u8"")) };
    REQUIRE(third.result.ok());
    REQUIRE(third.changed);
    REQUIRE(read_value(registry->key, L"IconUri").empty());
    REQUIRE(read_value(registry->key, L"DisplayName") == L"luil test");

    REQUIRE(luil::win32::unregister_app_identity(identity.id).ok());
    REQUIRE(key_exists(registry->key) == false);
    // 없는 식별을 지우면 `not_found`다.
    REQUIRE(luil::win32::unregister_app_identity(identity.id).status == luil::platform_status::not_found);
    REQUIRE(luil::win32::unregister_app_identity(u8"bad id").status == luil::platform_status::failed);
}

// 실측이다 (os-notification-design.md의 "확인"). ctest는 돌리지 않는다: 이 PC의 사용자 화면에 토스트를 올리고 HKCU에 식별을 남긴다.
//  - `luil_tests "[.notification-live]"`로 올리고, 알림 센터의 수를 PowerShell로 되읽은 뒤 `"[.notification-live-cleanup]"`로 거둔다.
namespace {
    constexpr std::u8string_view live_identity { u8"luil.test.live" };

    [[nodiscard]] luil::win32::app_identity make_live_identity()
    {
        luil::win32::app_identity identity {};
        identity.id = std::u8string { live_identity };
        identity.display_name = u8"luil live test";
        const std::filesystem::path icon { std::filesystem::path { __FILE__ }.parent_path().parent_path() / "examples" / "demo" / "assets" / "gradient.png" };
        identity.icon_path = icon.u8string();
        return identity;
    }

    void print_line(const std::string& text)
    {
        std::printf("%s\n", text.c_str());
        std::fflush(stdout);
    }
}

TEST_CASE("Live: a registry-only identity shows a toast", "[.notification-live]")
{
    const luil::win32::app_identity identity { make_live_identity() };
    const luil::platform_result applied { luil::win32::apply_app_identity(identity, u8"") };
    print_line("apply: " + std::string { applied.error.begin(), applied.error.end() } + (applied.ok() ? "ok" : ""));
    const luil::notification_availability status { luil::notification_status() };
    print_line("status: " + std::to_string(static_cast<int>(status)));

    luil::notification value {};
    value.id = u8"live-1";
    value.title = u8"luil live test";
    value.body = u8"Registry-only app identity";
    luil::image_decode_error decode_error {};
    value.image = luil::load_image_file(identity.icon_path, decode_error);
    const luil::platform_result shown { luil::show_notification(value) };
    print_line("show: " + std::to_string(static_cast<int>(shown.status)) + " " + std::string { shown.error.begin(), shown.error.end() });
    std::this_thread::sleep_for(std::chrono::milliseconds { 1500 });
    const std::optional<std::size_t> count { luil::win32::notification_history_count() };
    print_line("history: " + (count.has_value() ? std::to_string(*count) : std::string { "unreadable" }));
    REQUIRE(applied.ok());
    REQUIRE(shown.ok());
}

TEST_CASE("Live: remove the toasts and the identity the live test left", "[.notification-live-cleanup]")
{
    const luil::platform_result applied { luil::win32::apply_app_identity(make_live_identity(), u8"") };
    const luil::platform_result closed { luil::close_all_notifications() };
    const luil::platform_result removed { luil::win32::unregister_app_identity(live_identity) };
    print_line("apply: " + std::to_string(static_cast<int>(applied.status)) + " close: " + std::to_string(static_cast<int>(closed.status)) + " unregister: " + std::to_string(static_cast<int>(removed.status)));
    REQUIRE(removed.ok());
}

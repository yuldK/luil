#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 비밀 저장소의 답이다 (secret-store-design.md).
    enum class secret_store_status
    {
        ok,
        // 읽거나 지울 항목이 없다.
        not_found,
        // 이 플랫폼·구성에 저장소가 없다.
        unsupported,
        // OS가 거절했다. `error`가 그 이유를 든다.
        failed,
    };

    struct secret_store_result
    {
        secret_store_status status { secret_store_status::failed };
        // 개발자가 읽을 진단 영문이다. `ok`면 비었다.
        std::u8string error {};
    };

    // 값 하나의 상한이다 (바이트). Windows 자격 증명의 상한에 두 플랫폼을 맞춘다.
    inline constexpr std::size_t secret_store_maximum_bytes { 2560 };

    // 항목은 `service`(앱·기능의 묶음)와 `name`(계정·서버 같은 항목 이름)의 짝이다. 둘 다 비어 있으면 안 된다.
    //  - Windows: 자격 증명 관리자의 일반 자격 증명 `luil:<service>/<name>`이다.
    //  - Android: Keystore의 AES-GCM 키(`service`마다 하나)로 암호화한 앱 내부 파일이다.
    //  - 동기 호출이다. UI thread에서 부르지 않는다 (Android Keystore는 수십 ms까지 걸린다).

    // 같은 짝에 다시 쓰면 덮는다. 상한을 넘으면 `failed`다.
    [[nodiscard]] secret_store_result store_secret(std::u8string_view service, std::u8string_view name, std::span<const std::uint8_t> secret);
    // 성공하면 `secret`에 값을 담는다. 실패하면 `secret`을 비운다.
    [[nodiscard]] secret_store_result load_secret(std::u8string_view service, std::u8string_view name, std::vector<std::uint8_t>& secret);
    // 없으면 `not_found`다.
    [[nodiscard]] secret_store_result erase_secret(std::u8string_view service, std::u8string_view name);
}

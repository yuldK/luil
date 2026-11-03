#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

// 자동 업데이트가 OS에 맡기는 자리다 (docs/auto-update-design.md).
// Win32는 CNG(src/win32/update_crypto_win32.cpp), Android는 JNI의 java.security(src/android/update_crypto_android.cpp)다.
//  - 서명 꼴 바꾸기와 SPKI 머리는 core(host/update_manifest)가 하고, 여기는 그 결과를 OS에 넘기기만 한다.
namespace luil::net {
    // 덩이로 나눠 먹이는 SHA-256이다. Android는 받기가 없어 서지 않는다 (`valid()`가 거짓).
    class update_sha256
    {
    public:
        update_sha256();
        update_sha256(const update_sha256&) = delete;
        update_sha256(update_sha256&&) noexcept;
        update_sha256& operator=(const update_sha256&) = delete;
        update_sha256& operator=(update_sha256&&) noexcept;
        ~update_sha256();

        [[nodiscard]] bool valid() const noexcept;
        // 서지 못했거나 먹이다 실패한 까닭이다 (영문).
        [[nodiscard]] const std::u8string& error() const noexcept;
        [[nodiscard]] bool update(std::span<const std::uint8_t> bytes);
        // 끝낸다. 그 뒤로는 `update`·`finish`가 실패한다.
        [[nodiscard]] std::optional<std::array<std::uint8_t, 32>> finish();

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

    enum class update_signature_check
    {
        // 서명이 맞다.
        valid,
        // 서명이 맞지 않는다 (꼴이 깨진 서명을 포함한다).
        mismatch,
        // 키를 들이지 못했거나 OS가 실패했다. `error`가 까닭이다.
        failed,
    };

    struct update_signature_result
    {
        update_signature_check check { update_signature_check::failed };
        std::u8string error {};
    };

    // ECDSA P-256 + SHA-256 서명을 확인한다. `spki`는 P-256 SubjectPublicKeyInfo DER, `signature`는 r||s 64바이트다.
    //  - r이나 s가 0이면 OS에 넘기지 않고 `mismatch`다 (두 플랫폼이 같게 답하도록).
    //  - Android는 부른 thread를 JVM에 붙여 부른다.
    [[nodiscard]] update_signature_result verify_update_signature(std::span<const std::uint8_t> spki, const std::array<std::uint8_t, 64>& signature, std::span<const std::uint8_t> message);

    // 다 쓴 파일을 디스크까지 내린다. 이름을 바꾸기 전에 부른다. Win32는 `FlushFileBuffers`, Android는 `fsync`다.
    [[nodiscard]] bool flush_update_file(const std::filesystem::path& path, std::u8string& error);
}

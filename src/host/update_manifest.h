#pragma once

#include "luil/app/updater.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 자동 업데이트가 OS 없이 값만 다루는 자리다 (docs/auto-update-design.md).
// 봉투와 매니페스트 읽기, 엄격한 base64, 판 비교, 플랫폼 칸 고르기, 주소 규칙, 서명 꼴 바꾸기, SPKI 머리, 상태 파일이다.
// 받은 바이트는 모두 밖에서 온 값이라 던지지 않고 실패를 값으로 돌려준다.
namespace luil {
    // --- base64 (RFC 4648 표준 알파벳, 패딩 `=` 필수) ---

    // 공백·줄바꿈, 패딩 없는 꼴, URL 알파벳(`-`·`_`), 쓰이지 않는 끝 비트가 0이 아닌 꼴을 거절한다. 빈 글은 빈 바이트다.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> decode_update_base64(std::u8string_view text);
    [[nodiscard]] std::u8string encode_update_base64(std::span<const std::uint8_t> bytes);

    // --- 판 (결정 3 길 a) ---

    // 점으로 나눈 십진 마디 1~4개, 각 0~4294967295, 앞자리 0 없음이다. 빠진 마디는 0이라 `1.4`와 `1.4.0`은 같다.
    struct update_version
    {
        std::array<std::uint32_t, 4> parts {};

        [[nodiscard]] auto operator<=>(const update_version&) const noexcept = default;
    };

    [[nodiscard]] std::optional<update_version> parse_update_version(std::u8string_view text) noexcept;

    // --- 16진 SHA-256 ---

    using update_digest = std::array<std::uint8_t, 32>;

    // 소문자 16진 64자만 받는다.
    [[nodiscard]] std::optional<update_digest> parse_update_digest(std::u8string_view text) noexcept;
    [[nodiscard]] std::u8string format_update_digest(const update_digest& digest);

    // --- 봉투 (결정 1 길 a) ---

    inline constexpr std::u8string_view update_envelope_format { u8"luil-update-envelope/1" };
    inline constexpr std::size_t update_maximum_signatures { 8 };

    // 서명 항목의 칸 이름이 꼴을 정한다 (결정 2 길 c). 길이로 짐작하지 않는다.
    enum class update_signature_form
    {
        raw,
        der,
    };

    struct update_signature_entry
    {
        std::u8string key {};
        update_signature_form form { update_signature_form::raw };
        // base64를 푼 바이트다. raw는 늘 64바이트이고 der는 확인 때 엄격히 푼다.
        std::vector<std::uint8_t> bytes {};
    };

    struct update_envelope
    {
        // 서명이 덮는 바이트 그대로다. 서명을 지나기 전에는 읽지 않는다.
        std::vector<std::uint8_t> payload {};
        std::vector<update_signature_entry> signatures {};
    };

    // 바깥 JSON만 읽는다. `format`, 비지 않은 `payload`, 1~8개의 `signatures`(항목마다 `key`와 `raw`·`der` 가운데 하나)가 없거나 꼴이
    // 틀리면 비었고 `error`에 까닭을 적는다. 모르는 칸은 넘긴다.
    [[nodiscard]] std::optional<update_envelope> parse_update_envelope(std::span<const std::uint8_t> bytes, std::u8string& error);

    // 차례대로, `key`가 `keys`에 있는 항목과 그 키의 번호다. 모르는 키의 항목은 넘긴다.
    struct update_signature_match
    {
        std::size_t signature { 0 };
        std::size_t key { 0 };
    };

    [[nodiscard]] std::vector<update_signature_match> match_update_signatures(const update_envelope& envelope, std::span<const update_key> keys);

    // --- 서명과 키의 바이트 꼴 (결정 2 길 c) ---

    using update_raw_signature = std::array<std::uint8_t, 64>;
    using update_public_point = std::array<std::uint8_t, 64>;

    // DER `SEQUENCE { INTEGER r, INTEGER s }`를 r||s로 푼다. 엄격하다: 짧은 길이 꼴만, 최소 꼴 INTEGER, 음수 거절, 0 거절,
    // 32바이트(앞붙임 0 하나를 빼고)를 넘으면 거절, 뒤 바이트 거절이다.
    [[nodiscard]] std::optional<update_raw_signature> update_signature_from_der(std::span<const std::uint8_t> der) noexcept;
    // r||s를 DER로 짓는다. 앞의 0을 떼고 높은 비트가 서면 0을 하나 붙인다.
    [[nodiscard]] std::vector<std::uint8_t> update_signature_to_der(const update_raw_signature& raw);
    // r과 s가 둘 다 0이 아닌가. 0인 서명은 확인 API에 넘기지 않고 맞지 않는 것으로 본다.
    [[nodiscard]] bool update_signature_scalars_nonzero(const update_raw_signature& raw) noexcept;
    // P-256 SPKI DER의 고정 머리(곡선 OID부터 비압축 점 표식 `04`까지)를 맞추고 X||Y 64바이트를 돌려준다.
    [[nodiscard]] std::optional<update_public_point> update_spki_point(std::span<const std::uint8_t> spki) noexcept;

    // --- 매니페스트 (서명을 지난 payload) ---

    enum class update_platform
    {
        windows_x64,
        windows_arm64,
        android,
    };

    // 이 빌드의 플랫폼이다. 컴파일러가 정한 기호로 고른다.
    [[nodiscard]] update_platform current_update_platform() noexcept;
    // 매니페스트 `assets`의 칸 이름이다 (`windows-x64`·`windows-arm64`·`android`).
    [[nodiscard]] std::u8string_view update_platform_key(update_platform platform) noexcept;

    struct update_asset
    {
        // Win32 칸이다. 절대 주소이거나 이름 하나다 (`resolve_update_asset_url`).
        std::u8string url {};
        std::uint64_t size { 0 };
        update_digest sha256 {};
        // Android 칸이다.
        std::u8string store_url {};
    };

    struct update_manifest
    {
        std::u8string app {};
        std::u8string channel {};
        std::uint64_t sequence { 0 };
        std::u8string version_text {};
        update_version version {};
        std::u8string notes {};
        std::u8string notes_url {};
        std::optional<update_asset> windows_x64 {};
        std::optional<update_asset> windows_arm64 {};
        std::optional<update_asset> android {};

        [[nodiscard]] const std::optional<update_asset>& asset(update_platform platform) const noexcept;
    };

    // 서명을 지난 payload를 읽는다. `format`이 1이 아니거나, `app`·`channel`·`sequence`·`version`·`assets`가 없거나 꼴이 틀리거나, 아는
    // 플랫폼 칸의 꼴이 틀리면 비었고 `error`에 까닭을 적는다. 모르는 칸과 모르는 플랫폼 칸은 넘긴다.
    [[nodiscard]] std::optional<update_manifest> parse_update_manifest(std::span<const std::uint8_t> payload, std::u8string& error);

    // 지난 매니페스트를 이 PC의 사정과 견준 답이다 (규칙의 확인 4~6).
    enum class update_verdict
    {
        mismatch,
        replayed,
        up_to_date,
        no_asset,
        too_large,
        available,
    };

    struct update_judgement
    {
        std::u8string_view app {};
        std::u8string_view channel {};
        // 상태 파일이 기억하는 가장 큰 `sequence`다.
        std::uint64_t seen_sequence { 0 };
        update_version current {};
        update_platform platform { update_platform::windows_x64 };
        std::uint64_t max_asset_bytes { 0 };
    };

    [[nodiscard]] update_verdict judge_update_manifest(const update_manifest& manifest, const update_judgement& judgement) noexcept;

    // --- 주소 ---

    // https이거나, 되돌이 host(`127.0.0.1`·`[::1]`·`localhost`)의 http인가. 사용자 정보(`@`)가 든 주소는 거절한다.
    [[nodiscard]] bool update_url_allowed(std::u8string_view url) noexcept;
    // asset `url`을 푼다. 절대 주소면 그대로, 이름 하나(영문자·숫자·`-._~`, 점만으로 된 이름은 빼고)면 매니페스트 답의 최종 주소에서
    // 마지막 경로 조각을 그 이름으로 바꾼다. 결과가 `update_url_allowed`를 지나지 않으면 비었다.
    [[nodiscard]] std::optional<std::u8string> resolve_update_asset_url(std::u8string_view asset_url, std::u8string_view manifest_final_url);

    // --- 받기 ---

    // `Content-Range: bytes <first>-<last>/<total>`이다.
    struct update_content_range
    {
        std::uint64_t first { 0 };
        std::uint64_t last { 0 };
        std::uint64_t total { 0 };
    };

    [[nodiscard]] std::optional<update_content_range> parse_update_content_range(std::u8string_view value) noexcept;
    // `Range` 요청 헤더의 값이다 (`bytes=<first>-<last>`).
    [[nodiscard]] std::u8string make_update_range(std::uint64_t first, std::uint64_t last);

    // --- 상태 파일 `update-state.json` (결정 4 길 b) ---

    struct update_state
    {
        // 서명을 지난 매니페스트 가운데 가장 큰 `sequence`다.
        std::uint64_t sequence { 0 };
    };

    // 꼴이 틀리면 비었다. 모르는 칸은 넘긴다.
    [[nodiscard]] std::optional<update_state> parse_update_state(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> serialize_update_state(const update_state& state);
}

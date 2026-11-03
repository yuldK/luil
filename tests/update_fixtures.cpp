#include "update_fixtures.h"

#include <cstddef>

// 고정 자료는 Git Bash의 openssl(3.5)로 한 번 지었다. 비밀 키 파일은 저장소 밖에 두었다.
//
//     openssl ecparam -name prime256v1 -genkey -noout -out key-a.pem            키 A (B도 같은 명령)
//     openssl ec -in key-a.pem -pubout -outform DER | openssl base64 -A          update_fixture_key_spki
//     openssl ec -in key-a.pem -outform DER                                       ECPrivateKey에서 d·X·Y (update_test_key.h)
//     openssl dgst -sha256 -sign key-a.pem -out payload.sig payload.json          update_fixture_signature_der
//     openssl dgst -sha256 -verify key-a.spki.der -keyform DER -signature payload.sig payload.json    Verified OK
//
// payload.json은 `update_fixture_payload`의 바이트 그대로이고, r||s는 DER의 두 INTEGER를 32바이트씩 큰 끝으로 옮긴 것이다.
// asset의 sha256은 `update_fixture_asset()`과 같은 식으로 지은 3 MiB의 값이다.
namespace luil::testing {
    namespace {
        constexpr std::u8string_view alphabet { u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };

        [[nodiscard]] int alphabet_value(const char8_t character) noexcept
        {
            const std::size_t at { alphabet.find(character) };
            return at == std::u8string_view::npos ? -1 : static_cast<int>(at);
        }

        // 바꾼 봉투를 지으려고 base64 하나를 바이트로 푼다. 고정 자료만 받으므로 꼴 검사를 하지 않는다.
        [[nodiscard]] std::vector<std::uint8_t> decode_fixture(const std::u8string_view text)
        {
            std::vector<std::uint8_t> bytes {};
            std::uint32_t group { 0 };
            int bits { 0 };
            for (const char8_t character : text)
            {
                const int value { alphabet_value(character) };
                if (value < 0)
                    continue;
                group = (group << 6) | static_cast<std::uint32_t>(value);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    bytes.push_back(static_cast<std::uint8_t>(group >> bits));
                }
            }
            return bytes;
        }
    }

    std::vector<std::uint8_t> update_fixture_bytes(const std::u8string_view text)
    {
        std::vector<std::uint8_t> bytes {};
        bytes.reserve(text.size());
        for (const char8_t character : text)
            bytes.push_back(static_cast<std::uint8_t>(character));
        return bytes;
    }

    std::u8string update_fixture_base64(const std::span<const std::uint8_t> bytes)
    {
        std::u8string text {};
        std::size_t at { 0 };
        for (; at + 3 <= bytes.size(); at += 3)
        {
            const std::uint32_t group { (static_cast<std::uint32_t>(bytes[at]) << 16) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) | bytes[at + 2] };
            for (int shift = 18; shift >= 0; shift -= 6)
                text.push_back(alphabet[(group >> shift) & 0x3Fu]);
        }
        const std::size_t rest { bytes.size() - at };
        if (rest == 1)
        {
            const std::uint32_t group { static_cast<std::uint32_t>(bytes[at]) << 16 };
            text.push_back(alphabet[(group >> 18) & 0x3Fu]);
            text.push_back(alphabet[(group >> 12) & 0x3Fu]);
            text += u8"==";
        }
        else if (rest == 2)
        {
            const std::uint32_t group { (static_cast<std::uint32_t>(bytes[at]) << 16) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) };
            text.push_back(alphabet[(group >> 18) & 0x3Fu]);
            text.push_back(alphabet[(group >> 12) & 0x3Fu]);
            text.push_back(alphabet[(group >> 6) & 0x3Fu]);
            text.push_back(u8'=');
        }
        return text;
    }

    std::u8string make_update_envelope(const std::span<const std::uint8_t> payload, const std::span<const update_fixture_signature> signatures)
    {
        std::u8string text { u8"{\"format\":\"luil-update-envelope/1\",\"payload\":\"" };
        text += update_fixture_base64(payload);
        text += u8"\",\"signatures\":[";
        for (std::size_t index = 0; index < signatures.size(); ++index)
        {
            if (index > 0)
                text.push_back(u8',');
            text += u8"{\"key\":\"" + signatures[index].key + u8"\",\"" + signatures[index].form + u8"\":\"" + signatures[index].value + u8"\"}";
        }
        text += u8"]}";
        return text;
    }

    std::u8string update_fixture_envelope(const bool der)
    {
        const update_fixture_signature signature {
            std::u8string { update_fixture_key_id },
            der ? std::u8string { u8"der" } : std::u8string { u8"raw" },
            std::u8string { der ? update_fixture_signature_der : update_fixture_signature_raw },
        };
        return make_update_envelope(update_fixture_bytes(update_fixture_payload), std::span<const update_fixture_signature> { &signature, 1 });
    }

    std::u8string update_fixture_tampered_envelope()
    {
        std::vector<std::uint8_t> payload { update_fixture_bytes(update_fixture_payload) };
        // `"sequence":7`의 7을 8로 바꾼다. JSON은 그대로 읽히지만 서명이 덮은 바이트가 아니다.
        const std::u8string_view marker { u8"\"sequence\":7" };
        const std::size_t at { update_fixture_payload.find(marker) };
        payload[at + marker.size() - 1] = static_cast<std::uint8_t>(u8'8');
        const update_fixture_signature signature { std::u8string { update_fixture_key_id }, u8"der", std::u8string { update_fixture_signature_der } };
        return make_update_envelope(payload, std::span<const update_fixture_signature> { &signature, 1 });
    }

    std::u8string update_fixture_broken_signature_envelope()
    {
        std::vector<std::uint8_t> der { decode_fixture(update_fixture_signature_der) };
        // s의 마지막 바이트다. DER 꼴은 그대로라 확인 API까지 가서 맞지 않는다.
        der.back() ^= 0x01;
        const update_fixture_signature signature { std::u8string { update_fixture_key_id }, u8"der", update_fixture_base64(der) };
        return make_update_envelope(update_fixture_bytes(update_fixture_payload), std::span<const update_fixture_signature> { &signature, 1 });
    }

    std::vector<std::uint8_t> update_fixture_asset(const std::uint64_t size)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(index) * 2654435761u) >> 24);
        return bytes;
    }
}

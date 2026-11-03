#include "update_test_signing.h"

#include "host/update_manifest.h"
#include "net/update_crypto.h"
#include "update_test_key.h"

#include <windows.h>

#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace luil::testing {
    std::optional<std::array<std::uint8_t, 64>> sign_update_payload(const std::span<const std::uint8_t> payload)
    {
        luil::net::update_sha256 hasher {};
        if (hasher.update(payload) == false)
            return std::nullopt;
        std::optional<std::array<std::uint8_t, 32>> digest { hasher.finish() };
        if (digest.has_value() == false)
            return std::nullopt;

        BCRYPT_ALG_HANDLE algorithm { nullptr };
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) == false)
            return std::nullopt;
        // 비밀 키 blob은 머리 뒤에 X, Y, d다.
        std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + 96> blob {};
        const BCRYPT_ECCKEY_BLOB header { BCRYPT_ECDSA_PRIVATE_P256_MAGIC, 32 };
        std::memcpy(blob.data(), &header, sizeof(header));
        std::memcpy(blob.data() + sizeof(header), update_test_key_x.data(), 32);
        std::memcpy(blob.data() + sizeof(header) + 32, update_test_key_y.data(), 32);
        std::memcpy(blob.data() + sizeof(header) + 64, update_test_key_d.data(), 32);
        BCRYPT_KEY_HANDLE key { nullptr };
        std::optional<std::array<std::uint8_t, 64>> signature {};
        if (BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0)))
        {
            std::array<std::uint8_t, 64> value {};
            ULONG written { 0 };
            if (BCRYPT_SUCCESS(BCryptSignHash(key, nullptr, digest->data(), static_cast<ULONG>(digest->size()), value.data(), static_cast<ULONG>(value.size()), &written, 0))
                && written == value.size())
                signature = value;
            static_cast<void>(BCryptDestroyKey(key));
        }
        static_cast<void>(BCryptCloseAlgorithmProvider(algorithm, 0));
        return signature;
    }

    std::u8string make_signed_update_envelope(const std::u8string_view payload, const bool der)
    {
        const std::vector<std::uint8_t> bytes { update_fixture_bytes(payload) };
        const std::optional<std::array<std::uint8_t, 64>> raw { sign_update_payload(bytes) };
        if (raw.has_value() == false)
            return {};
        update_fixture_signature signature { std::u8string { update_fixture_key_id }, der ? std::u8string { u8"der" } : std::u8string { u8"raw" }, {} };
        if (der)
        {
            const std::vector<std::uint8_t> encoded { luil::update_signature_to_der(*raw) };
            signature.value = update_fixture_base64(encoded);
        }
        else
            signature.value = update_fixture_base64(*raw);
        return make_update_envelope(bytes, std::span<const update_fixture_signature> { &signature, 1 });
    }
}

#include "net/update_crypto.h"

#include "host/update_manifest.h"
#include "win32/owned_handle.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <charconv>
#include <cstring>
#include <system_error>
#include <utility>

// 자동 업데이트의 Win32 암호다 (docs/auto-update-design.md). CNG의 SHA-256과 ECDSA P-256 확인을 부른다.
//  - CryptoAPI(`app_identity.cpp`의 SHA-1)는 P-256 확인이 없어 CNG를 쓴다. 링크 목록의 `bcrypt`가 이 파일의 것이다.
namespace luil::net {
    namespace {
        // `<ntstatus.h>`는 `<windows.h>`의 정의와 부딪혀 값만 옮긴다.
        constexpr NTSTATUS invalid_signature_status { static_cast<NTSTATUS>(0xC000A000L) };

        [[nodiscard]] std::u8string ntstatus_error(const std::u8string_view message, const NTSTATUS status)
        {
            std::u8string output { message };
            output += u8" (NTSTATUS=0x";
            std::array<char, 16> buffer {};
            const auto conversion { std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<unsigned long>(status), 16) };
            if (conversion.ec == std::errc {})
                for (const char* character = buffer.data(); character != conversion.ptr; ++character)
                    output.push_back(static_cast<char8_t>(*character));
            output += u8").";
            return output;
        }

        class algorithm_handle
        {
        public:
            algorithm_handle() noexcept = default;
            algorithm_handle(const algorithm_handle&) = delete;
            algorithm_handle& operator=(const algorithm_handle&) = delete;
            ~algorithm_handle()
            {
                if (handle_ != nullptr)
                    static_cast<void>(BCryptCloseAlgorithmProvider(handle_, 0));
            }

            [[nodiscard]] NTSTATUS open(const wchar_t* const name) noexcept
            {
                return BCryptOpenAlgorithmProvider(&handle_, name, nullptr, 0);
            }

            [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept
            {
                return handle_;
            }

        private:
            BCRYPT_ALG_HANDLE handle_ { nullptr };
        };

        class key_handle
        {
        public:
            key_handle() noexcept = default;
            key_handle(const key_handle&) = delete;
            key_handle& operator=(const key_handle&) = delete;
            ~key_handle()
            {
                if (handle_ != nullptr)
                    static_cast<void>(BCryptDestroyKey(handle_));
            }

            [[nodiscard]] BCRYPT_KEY_HANDLE* out() noexcept
            {
                return &handle_;
            }

            [[nodiscard]] BCRYPT_KEY_HANDLE get() const noexcept
            {
                return handle_;
            }

        private:
            BCRYPT_KEY_HANDLE handle_ { nullptr };
        };
    }

    struct update_sha256::state
    {
        algorithm_handle algorithm {};
        BCRYPT_HASH_HANDLE hash { nullptr };
        std::u8string error {};
        bool finished { false };

        state() = default;
        state(const state&) = delete;
        state& operator=(const state&) = delete;
        ~state()
        {
            if (hash != nullptr)
                static_cast<void>(BCryptDestroyHash(hash));
        }
    };

    update_sha256::update_sha256()
        : state_ { std::make_unique<state>() }
    {
        if (const NTSTATUS status { state_->algorithm.open(BCRYPT_SHA256_ALGORITHM) }; BCRYPT_SUCCESS(status) == false)
        {
            state_->error = ntstatus_error(u8"Failed to open the SHA-256 provider", status);
            return;
        }
        // 해시 개체 버퍼를 넘기지 않으면 CNG가 스스로 잡는다.
        if (const NTSTATUS status { BCryptCreateHash(state_->algorithm.get(), &state_->hash, nullptr, 0, nullptr, 0, 0) }; BCRYPT_SUCCESS(status) == false)
        {
            state_->hash = nullptr;
            state_->error = ntstatus_error(u8"Failed to create the SHA-256 hash", status);
        }
    }

    update_sha256::update_sha256(update_sha256&&) noexcept = default;
    update_sha256& update_sha256::operator=(update_sha256&&) noexcept = default;
    update_sha256::~update_sha256() = default;

    bool update_sha256::valid() const noexcept
    {
        return state_ != nullptr && state_->hash != nullptr && state_->finished == false && state_->error.empty();
    }

    const std::u8string& update_sha256::error() const noexcept
    {
        return state_->error;
    }

    bool update_sha256::update(std::span<const std::uint8_t> bytes)
    {
        if (valid() == false)
            return false;
        // `BCryptHashData`의 길이는 ULONG이라 큰 덩이는 나눠 먹인다.
        while (bytes.empty() == false)
        {
            const std::size_t count { bytes.size() < 0x40000000u ? bytes.size() : std::size_t { 0x40000000u } };
            if (const NTSTATUS status { BCryptHashData(state_->hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(count), 0) }; BCRYPT_SUCCESS(status) == false)
            {
                state_->error = ntstatus_error(u8"Failed to hash the bytes", status);
                return false;
            }
            bytes = bytes.subspan(count);
        }
        return true;
    }

    std::optional<std::array<std::uint8_t, 32>> update_sha256::finish()
    {
        if (valid() == false)
            return std::nullopt;
        state_->finished = true;
        std::array<std::uint8_t, 32> digest {};
        if (const NTSTATUS status { BCryptFinishHash(state_->hash, digest.data(), static_cast<ULONG>(digest.size()), 0) }; BCRYPT_SUCCESS(status) == false)
        {
            state_->error = ntstatus_error(u8"Failed to finish the SHA-256 hash", status);
            return std::nullopt;
        }
        return digest;
    }

    update_signature_result verify_update_signature(const std::span<const std::uint8_t> spki, const std::array<std::uint8_t, 64>& signature, const std::span<const std::uint8_t> message)
    {
        if (update_signature_scalars_nonzero(signature) == false)
            return { update_signature_check::mismatch, u8"The signature has a zero scalar." };
        const std::optional<update_public_point> point { update_spki_point(spki) };
        if (point.has_value() == false)
            return { update_signature_check::failed, u8"The key is not a P-256 SubjectPublicKeyInfo." };

        update_sha256 hasher {};
        if (hasher.update(message) == false)
            return { update_signature_check::failed, hasher.error() };
        std::optional<std::array<std::uint8_t, 32>> digest { hasher.finish() };
        if (digest.has_value() == false)
            return { update_signature_check::failed, hasher.error() };

        algorithm_handle algorithm {};
        if (const NTSTATUS status { algorithm.open(BCRYPT_ECDSA_P256_ALGORITHM) }; BCRYPT_SUCCESS(status) == false)
            return { update_signature_check::failed, ntstatus_error(u8"Failed to open the ECDSA P-256 provider", status) };

        // 공개 키 blob은 머리(magic, 좌표 길이) 뒤에 X와 Y다.
        std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob {};
        const BCRYPT_ECCKEY_BLOB header { BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32 };
        std::memcpy(blob.data(), &header, sizeof(header));
        std::memcpy(blob.data() + sizeof(header), point->data(), point->size());
        key_handle key {};
        if (const NTSTATUS status { BCryptImportKeyPair(algorithm.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB, key.out(), blob.data(), static_cast<ULONG>(blob.size()), 0) };
            BCRYPT_SUCCESS(status) == false)
            return { update_signature_check::failed, ntstatus_error(u8"Failed to import the P-256 public key", status) };

        std::array<std::uint8_t, 64> signature_copy { signature };
        const NTSTATUS status {
            BCryptVerifySignature(key.get(), nullptr, digest->data(), static_cast<ULONG>(digest->size()), signature_copy.data(), static_cast<ULONG>(signature_copy.size()), 0),
        };
        if (BCRYPT_SUCCESS(status))
            return { update_signature_check::valid, {} };
        if (status == invalid_signature_status)
            return { update_signature_check::mismatch, u8"The signature does not match." };
        return { update_signature_check::failed, ntstatus_error(u8"Failed to verify the signature", status) };
    }

    bool flush_update_file(const std::filesystem::path& path, std::u8string& error)
    {
        // 같은 파일의 어느 쓰기 손잡이로 불러도 시스템 캐시의 그 파일 바이트가 내려간다.
        const win32::owned_handle file {
            CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
        };
        if (file.valid() == false)
        {
            error = win32::make_last_error(u8"Failed to open the downloaded file");
            return false;
        }
        if (FlushFileBuffers(file.get()) == FALSE)
        {
            error = win32::make_last_error(u8"Failed to flush the downloaded file");
            return false;
        }
        return true;
    }
}

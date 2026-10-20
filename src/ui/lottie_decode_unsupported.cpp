#include "luil/ui/image_decode.h"

#include <cstdint>
#include <span>
#include <string_view>

// Lottie를 끈 구성이다 (`LUIL_ENABLE_LOTTIE=OFF`). 공개 API는 그대로이고 언제나 "지원 안 함"으로
// 답한다 — Skottie를 링크하지 않는다.
namespace luil {
    namespace {
        [[nodiscard]] image_decode_error lottie_unsupported()
        {
            return { image_decode_error_kind::unsupported_format, u8"This build does not read Lottie (LUIL_ENABLE_LOTTIE is OFF).", 0 };
        }
    } // namespace

    ui_lottie load_lottie_file(const std::u8string_view path, image_decode_error& error)
    {
        return load_lottie_file(path, image_decode_options {}, error);
    }

    ui_lottie load_lottie_file(const std::u8string_view path, const image_decode_options& options, image_decode_error& error)
    {
        static_cast<void>(path);
        static_cast<void>(options);
        error = lottie_unsupported();
        return {};
    }

    ui_lottie decode_lottie_bytes(const std::span<const std::uint8_t> bytes, image_decode_error& error)
    {
        return decode_lottie_bytes(bytes, image_decode_options {}, error);
    }

    ui_lottie decode_lottie_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
    {
        static_cast<void>(bytes);
        static_cast<void>(options);
        error = lottie_unsupported();
        return {};
    }
} // namespace luil

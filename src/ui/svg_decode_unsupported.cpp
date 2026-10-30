#include "luil/ui/image_decode.h"

#include <cstdint>
#include <span>
#include <string_view>

// SVG를 끈 구성이다 (`LUIL_ENABLE_SVG=OFF`). 공개 API는 그대로이고 언제나 "지원 안 함"으로
// 답한다 — SVG 모듈을 링크하지 않는다.
namespace luil {
    namespace {
        [[nodiscard]] image_decode_error svg_unsupported()
        {
            return { image_decode_error_kind::unsupported_format, u8"This build does not read SVG (LUIL_ENABLE_SVG is OFF).", 0 };
        }
    }

    ui_image load_svg_file(const std::u8string_view path, image_decode_error& error)
    {
        return load_svg_file(path, image_decode_options {}, error);
    }

    ui_image load_svg_file(const std::u8string_view path, const image_decode_options& options, image_decode_error& error)
    {
        static_cast<void>(path);
        static_cast<void>(options);
        error = svg_unsupported();
        return {};
    }

    ui_image decode_svg_bytes(const std::span<const std::uint8_t> bytes, image_decode_error& error)
    {
        return decode_svg_bytes(bytes, image_decode_options {}, error);
    }

    ui_image decode_svg_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
    {
        static_cast<void>(bytes);
        static_cast<void>(options);
        error = svg_unsupported();
        return {};
    }
}

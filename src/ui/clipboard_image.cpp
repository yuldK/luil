#include "luil/ui/clipboard_image.h"

#include "ui/image_source.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <vector>

namespace luil {
    namespace {
        using image_input::make_error;

        // DIB 머리의 자리다 (wingdi.h의 `BITMAPINFOHEADER`·`BITMAPV4HEADER`·`BITMAPV5HEADER`).
        // core는 Windows 헤더를 들이지 않으므로(`luil_core_portability`) 바이트 자리로 읽는다. 셋 다 앞 40바이트가 같고,
        // V4·V5는 그 뒤에 색 마스크 넷(R·G·B·A)을 곧바로 둔다.
        constexpr std::size_t info_header_bytes { 40 };
        constexpr std::size_t header_rgb_masks_end { 52 };
        constexpr std::size_t header_alpha_mask_end { 56 };
        constexpr std::uint32_t compression_rgb { 0 };
        constexpr std::uint32_t compression_bitfields { 3 };
        constexpr std::uint32_t compression_alpha_bitfields { 6 };
        // BMP 파일 머리(`BITMAPFILEHEADER`)의 크기다. 클립보드 DIB에는 없다.
        constexpr std::size_t file_header_bytes { 14 };

        [[nodiscard]] std::uint16_t read_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
        }

        [[nodiscard]] std::uint32_t read_u32(const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        void write_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value) noexcept
        {
            bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
            bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        }

        void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) noexcept
        {
            for (std::size_t index = 0; index < 4; ++index)
                bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFFu);
        }

        // 색 마스크 하나다. 이어진 비트여야 쓸 수 있다 (0은 "그 색이 없다").
        struct channel_mask
        {
            std::uint32_t mask { 0 };
            int shift { 0 };
            int bits { 0 };
        };

        [[nodiscard]] channel_mask make_channel_mask(const std::uint32_t mask) noexcept
        {
            if (mask == 0)
                return {};
            const int shift { std::countr_zero(mask) };
            return { mask, shift, std::popcount(mask) };
        }

        [[nodiscard]] bool contiguous(const std::uint32_t mask) noexcept
        {
            if (mask == 0)
                return false;
            const std::uint64_t run { static_cast<std::uint64_t>(mask >> std::countr_zero(mask)) + 1u };
            return std::has_single_bit(run);
        }

        // 마스크가 고른 값을 8비트로 늘이거나 줄인다.
        [[nodiscard]] std::uint8_t channel_value(const std::uint32_t pixel, const channel_mask& channel) noexcept
        {
            const std::uint32_t value { (pixel & channel.mask) >> channel.shift };
            if (channel.bits == 8)
                return static_cast<std::uint8_t>(value);
            if (channel.bits > 8)
                return static_cast<std::uint8_t>(value >> (channel.bits - 8));
            const std::uint32_t maximum { (1u << channel.bits) - 1u };
            return static_cast<std::uint8_t>((value * 255u + maximum / 2u) / maximum);
        }

        // DIB 머리에서 읽은 값이다. 바이트 자리는 블록 처음에서 센다.
        struct dib_layout
        {
            std::int64_t width { 0 };
            // 양수면 아래에서 위로, 음수면 위에서 아래로 쌓았다.
            std::int64_t height { 0 };
            std::uint16_t bit_count { 0 };
            std::uint32_t compression { 0 };
            std::uint32_t image_size { 0 };
            // 픽셀이 시작하는 자리다 (머리, 머리 뒤 마스크, 팔레트를 건넌 곳).
            std::uint64_t pixel_offset { 0 };
            // 압축하지 않은 한 행의 바이트다 (4바이트 정렬).
            std::uint64_t stride { 0 };
            channel_mask red {};
            channel_mask green {};
            channel_mask blue {};
            channel_mask alpha {};
        };

        [[nodiscard]] bool bitfields(const std::uint32_t compression) noexcept
        {
            return compression == compression_bitfields || compression == compression_alpha_bitfields;
        }

        [[nodiscard]] bool read_layout(const std::span<const std::uint8_t> bytes, dib_layout& layout, image_decode_error& error)
        {
            if (bytes.size() < info_header_bytes)
            {
                error = make_error(image_decode_error_kind::incomplete_input, u8"The DIB block is shorter than its header.");
                return false;
            }
            const std::uint32_t header_size { read_u32(bytes, 0) };
            // 12바이트 `BITMAPCOREHEADER`는 클립보드에 오지 않는다 (OS가 짓는 `CF_DIB`도 40바이트 머리다).
            if (header_size < info_header_bytes)
            {
                error = make_error(image_decode_error_kind::unsupported_format, u8"The DIB header is not a BITMAPINFOHEADER, V4 or V5 header.");
                return false;
            }
            if (header_size > bytes.size())
            {
                error = make_error(image_decode_error_kind::incomplete_input, u8"The DIB block is shorter than its header.");
                return false;
            }
            layout.width = static_cast<std::int32_t>(read_u32(bytes, 4));
            layout.height = static_cast<std::int32_t>(read_u32(bytes, 8));
            layout.bit_count = read_u16(bytes, 14);
            layout.compression = read_u32(bytes, 16);
            layout.image_size = read_u32(bytes, 20);
            const std::uint32_t colors_used { read_u32(bytes, 32) };

            // 마스크는 V4·V5면 머리 안에, 40바이트 머리면 그 바로 뒤에 셋(알파까지면 넷) 붙는다.
            std::uint64_t trailing_masks { 0 };
            std::array<std::uint32_t, 4> masks {};
            if (bitfields(layout.compression))
            {
                const std::size_t count { layout.compression == compression_alpha_bitfields ? 4u : 3u };
                std::size_t origin { info_header_bytes };
                if (header_size < header_rgb_masks_end)
                {
                    trailing_masks = count * 4u;
                    if (header_size + trailing_masks > bytes.size())
                    {
                        error = make_error(image_decode_error_kind::incomplete_input, u8"The DIB block is shorter than its color masks.");
                        return false;
                    }
                    origin = header_size;
                }
                for (std::size_t index = 0; index < count; ++index)
                    masks[index] = read_u32(bytes, origin + index * 4u);
            }
            // V4·V5 머리는 압축과 관계없이 알파 마스크 자리를 둔다.
            if (header_size >= header_alpha_mask_end && masks[3] == 0)
                masks[3] = read_u32(bytes, header_rgb_masks_end);
            layout.red = make_channel_mask(masks[0]);
            layout.green = make_channel_mask(masks[1]);
            layout.blue = make_channel_mask(masks[2]);
            layout.alpha = make_channel_mask(masks[3]);

            const std::uint64_t palette { layout.bit_count >= 1 && layout.bit_count <= 8 ? (colors_used != 0 ? colors_used : (std::uint64_t { 1 } << layout.bit_count)) : colors_used };
            layout.pixel_offset = header_size + trailing_masks + palette * 4u;
            layout.stride = ((static_cast<std::uint64_t>(layout.width > 0 ? layout.width : 0) * layout.bit_count + 31u) / 32u) * 4u;
            return true;
        }

        // 코덱 없이 옮기는 꼴인가: 24·32비트 `BI_RGB`, 이어진 R·G·B 마스크의 32비트 `BI_BITFIELDS`.
        [[nodiscard]] bool direct_layout(const dib_layout& layout) noexcept
        {
            if (layout.compression == compression_rgb)
                return layout.bit_count == 24 || layout.bit_count == 32;
            return bitfields(layout.compression) && layout.bit_count == 32 && contiguous(layout.red.mask) && contiguous(layout.green.mask) && contiguous(layout.blue.mask)
                && (layout.alpha.mask == 0 || contiguous(layout.alpha.mask));
        }

        [[nodiscard]] ui_image convert_direct(const std::span<const std::uint8_t> bytes, const dib_layout& layout, image_decode_error& error)
        {
            const auto width { static_cast<std::size_t>(layout.width) };
            const auto rows { static_cast<std::size_t>(std::llabs(layout.height)) };
            std::vector<std::uint8_t> rgba {};
            try
            {
                rgba.resize(width * rows * 4u);
            }
            catch (const std::bad_alloc&)
            {
                error = make_error(image_decode_error_kind::out_of_memory, u8"Cannot allocate the pixels of the clipboard image.");
                return {};
            }

            // 알파 자리가 있는 꼴이다. 32비트 `BI_RGB`는 넷째 바이트를 알파로 본다.
            const bool alpha_source { layout.bit_count == 32 && (layout.compression == compression_rgb || layout.alpha.mask != 0) };
            const bool bottom_up { layout.height > 0 };
            bool any_alpha { false };
            for (std::size_t row = 0; row < rows; ++row)
            {
                const std::size_t source_row { bottom_up ? rows - 1u - row : row };
                const std::uint8_t* const source { bytes.data() + layout.pixel_offset + source_row * layout.stride };
                std::uint8_t* const target { rgba.data() + row * width * 4u };
                for (std::size_t column = 0; column < width; ++column)
                {
                    std::uint8_t* const pixel { target + column * 4u };
                    if (layout.bit_count == 24)
                    {
                        const std::uint8_t* const value { source + column * 3u };
                        pixel[0] = value[2];
                        pixel[1] = value[1];
                        pixel[2] = value[0];
                        pixel[3] = 0xFFu;
                        continue;
                    }
                    const std::uint8_t* const value { source + column * 4u };
                    if (layout.compression == compression_rgb)
                    {
                        pixel[0] = value[2];
                        pixel[1] = value[1];
                        pixel[2] = value[0];
                        pixel[3] = value[3];
                    }
                    else
                    {
                        const std::uint32_t packed { read_u32(std::span<const std::uint8_t> { value, 4u }, 0) };
                        pixel[0] = channel_value(packed, layout.red);
                        pixel[1] = channel_value(packed, layout.green);
                        pixel[2] = channel_value(packed, layout.blue);
                        pixel[3] = layout.alpha.mask != 0 ? channel_value(packed, layout.alpha) : std::uint8_t { 0xFFu };
                    }
                    any_alpha = any_alpha || pixel[3] != 0;
                }
            }
            // 알파를 비워 두는 앱이 흔하다. 모두 0이면 그 자리는 알파가 아니라 쓰지 않은 바이트로 본다.
            if (alpha_source && any_alpha == false)
                for (std::size_t index = 3; index < rgba.size(); index += 4u)
                    rgba[index] = 0xFFu;

            ui_image image { make_rgba_image(static_cast<int>(width), static_cast<int>(rows), rgba) };
            if (image.valid() == false)
                error = make_error(image_decode_error_kind::internal_error, u8"Cannot make an image from the clipboard pixels.");
            return image;
        }

        // 나머지 꼴(팔레트·RLE·16비트)은 BMP 파일 머리를 붙여 BMP 코덱에 넘긴다.
        [[nodiscard]] ui_image decode_as_bmp(const std::span<const std::uint8_t> bytes, const dib_layout& layout, const image_decode_options& options, image_decode_error& error)
        {
            const std::uint64_t file_size { file_header_bytes + bytes.size() };
            if (file_size > 0xFFFFFFFFu)
            {
                error = make_error(image_decode_error_kind::input_too_large, u8"The clipboard DIB is too large for a BMP file header.");
                return {};
            }
            std::vector<std::uint8_t> file {};
            try
            {
                file.resize(static_cast<std::size_t>(file_size));
            }
            catch (const std::bad_alloc&)
            {
                error = make_error(image_decode_error_kind::out_of_memory, u8"Cannot allocate the clipboard image as a BMP file.");
                return {};
            }
            file[0] = static_cast<std::uint8_t>('B');
            file[1] = static_cast<std::uint8_t>('M');
            write_u32(file, 2, static_cast<std::uint32_t>(file_size));
            write_u16(file, 6, 0);
            write_u16(file, 8, 0);
            write_u32(file, 10, static_cast<std::uint32_t>(file_header_bytes + layout.pixel_offset));
            std::copy(bytes.begin(), bytes.end(), file.begin() + static_cast<std::ptrdiff_t>(file_header_bytes));
            return decode_image_bytes(file, options, error);
        }

        [[nodiscard]] ui_image decode_dib(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
        {
            dib_layout layout {};
            if (read_layout(bytes, layout, error) == false)
                return {};
            // 픽셀 상한은 메모리를 잡기 전에 코덱과 같은 값과 같은 글로 본다.
            const std::uint64_t rows { static_cast<std::uint64_t>(std::llabs(layout.height)) };
            if (image_input::check_source_pixels(layout.width > 0 ? static_cast<std::uint64_t>(layout.width) : 0u, rows, options, error) == false)
                return {};
            if (layout.pixel_offset > bytes.size())
            {
                error = make_error(image_decode_error_kind::incomplete_input, u8"The DIB block is shorter than its header says.");
                return {};
            }
            // 압축하지 않은 꼴은 행 수만큼, 압축한 꼴은 머리가 적은 크기만큼 자료가 있어야 한다.
            const bool uncompressed { layout.compression == compression_rgb || bitfields(layout.compression) };
            const std::uint64_t data_bytes { uncompressed ? layout.stride * rows : layout.image_size };
            if (data_bytes > bytes.size() - layout.pixel_offset)
            {
                error = make_error(image_decode_error_kind::incomplete_input, u8"The DIB block is shorter than its header says.");
                return {};
            }
            if (direct_layout(layout) == false)
                return decode_as_bmp(bytes, layout, options, error);

            ui_image image { convert_direct(bytes, layout, error) };
            if (image.valid() == false || (options.max_width <= 0 && options.max_height <= 0))
                return image;
            return downscale_image(image, options, error);
        }
    }

    ui_image decode_clipboard_image(const clipboard_data& data, const image_decode_options& options, image_decode_error& error)
    {
        if (data.image_too_large)
        {
            error = make_error(image_decode_error_kind::input_too_large, u8"The clipboard image is larger than the 256 MiB read limit.");
            return {};
        }
        if (data.image_format == clipboard_image_format::none || data.image == nullptr || data.image->empty())
        {
            error = make_error(image_decode_error_kind::file_empty, u8"The clipboard holds no image.");
            return {};
        }
        if (data.image_format == clipboard_image_format::encoded)
            return decode_image_bytes(*data.image, options, error);
        return decode_dib(*data.image, options, error);
    }
}

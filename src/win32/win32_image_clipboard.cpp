#include "win32/win32_image_clipboard.h"

#include "luil/ui/clipboard_image.h"
#include "ui/image_pixels.h"
#include "win32/utf8.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkImageInfo.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace luil::win32 {
    namespace {
        // PNG 파일의 앞 8바이트다.
        constexpr std::array<std::uint8_t, 8> png_file_signature { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };

        // 읽을 그림 형식 하나와 그 꼴이다. 앞의 것이 먼저다.
        struct image_candidate
        {
            UINT format { 0 };
            clipboard_image_format kind { clipboard_image_format::none };
        };
    }

    bool png_signature(const std::span<const std::uint8_t> bytes) noexcept
    {
        return bytes.size() >= png_file_signature.size() && std::equal(png_file_signature.begin(), png_file_signature.end(), bytes.begin());
    }

    std::optional<std::vector<std::byte>> dibv5_block(const ui_image& image, std::u8string& error)
    {
        if (image.valid() == false)
        {
            error = u8"The clipboard image is empty.";
            return std::nullopt;
        }
        SkBitmap pixels {};
        if (read_image_pixels(image, pixels, error) == false)
            return std::nullopt;
        const int width { pixels.width() };
        const int height { pixels.height() };
        const std::size_t stride { static_cast<std::size_t>(width) * 4u };
        const std::uint64_t pixel_bytes { static_cast<std::uint64_t>(stride) * static_cast<std::uint64_t>(height) };
        if (width <= 0 || height <= 0 || pixel_bytes > 0xFFFFFFFFu - sizeof(BITMAPV5HEADER))
        {
            error = u8"The clipboard image has no pixels or is too large for a DIB.";
            return std::nullopt;
        }

        BITMAPV5HEADER header {};
        header.bV5Size = sizeof(BITMAPV5HEADER);
        header.bV5Width = width;
        // 양수 높이는 아래에서 위로 쌓는 DIB다. 음수(위에서 아래로)를 잘못 읽는 앱이 있다.
        header.bV5Height = height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5SizeImage = static_cast<DWORD>(pixel_bytes);
        header.bV5RedMask = 0x00FF0000u;
        header.bV5GreenMask = 0x0000FF00u;
        header.bV5BlueMask = 0x000000FFu;
        header.bV5AlphaMask = 0xFF000000u;
        header.bV5CSType = LCS_sRGB;
        header.bV5Intent = LCS_GM_IMAGES;

        std::vector<std::byte> block(sizeof(BITMAPV5HEADER) + static_cast<std::size_t>(pixel_bytes));
        std::memcpy(block.data(), &header, sizeof(BITMAPV5HEADER));
        std::byte* const bits { block.data() + sizeof(BITMAPV5HEADER) };
        // 곱하지 않은 BGRA는 리틀 엔디언 DWORD로 0xAARRGGBB라 위의 마스크와 맞는다. 곱한 색을 주면 알파가 있는 가장자리가
        // 받는 앱에서 한 번 더 곱해져 어두워진다.
        const SkImageInfo information { SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType) };
        if (pixels.readPixels(information, bits, stride, 0, 0) == false)
        {
            error = u8"Cannot copy the pixels of the clipboard image.";
            return std::nullopt;
        }
        // Skia는 위에서 아래로 쌓는다. 양수 높이에 맞게 행을 뒤집는다.
        for (int row = 0; row < height / 2; ++row)
        {
            std::byte* const top { bits + static_cast<std::size_t>(row) * stride };
            std::byte* const bottom { bits + static_cast<std::size_t>(height - 1 - row) * stride };
            std::swap_ranges(top, top + stride, bottom);
        }
        return block;
    }

    std::optional<std::vector<clipboard_block>> clipboard_data_blocks(const clipboard_data_copy_request& request, std::u8string& error)
    {
        std::vector<clipboard_block> blocks {};
        if (request.image.valid())
        {
            if (png_signature(request.png))
            {
                if (const UINT format { RegisterClipboardFormatW(png_clipboard_format) }; format != 0)
                {
                    std::vector<std::byte> bytes(request.png.size());
                    std::memcpy(bytes.data(), request.png.data(), request.png.size());
                    blocks.push_back(clipboard_block { format, std::move(bytes) });
                }
            }
            std::optional<std::vector<std::byte>> dib { dibv5_block(request.image, error) };
            if (dib.has_value() == false)
                return std::nullopt;
            blocks.push_back(clipboard_block { CF_DIBV5, std::move(*dib) });
        }
        if (request.text.empty() == false)
        {
            std::optional<std::vector<std::byte>> text { unicode_text_block(request.text) };
            if (text.has_value() == false)
            {
                error = u8"The clipboard text is not valid UTF-8.";
                return std::nullopt;
            }
            blocks.push_back(clipboard_block { CF_UNICODETEXT, std::move(*text) });
        }
        return blocks;
    }

    bool copy_data_to_clipboard(const HWND owner, const clipboard_data_copy_request& request) noexcept
    {
        try
        {
            std::u8string error {};
            const std::optional<std::vector<clipboard_block>> blocks { clipboard_data_blocks(request, error) };
            if (blocks.has_value() == false || blocks->empty())
                return false;
            return put_clipboard_blocks(owner, *blocks);
        }
        catch (...)
        {
            return false;
        }
    }

    clipboard_data read_clipboard_data(const HWND owner) noexcept
    {
        clipboard_data data {};
        data.sequence = static_cast<std::uint32_t>(GetClipboardSequenceNumber());
        try
        {
            const UINT png_format { RegisterClipboardFormatW(png_clipboard_format) };
            const scoped_clipboard clipboard { owner };
            if (clipboard.open == false)
                return data;
            // 연 동안에는 바뀌지 않으므로 읽는 내용과 짝이 맞는 번호다.
            data.sequence = static_cast<std::uint32_t>(GetClipboardSequenceNumber());

            if (IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE)
                if (const HANDLE handle { GetClipboardData(CF_UNICODETEXT) }; handle != nullptr)
                {
                    // 다른 프로세스가 만든 블록이다 — NUL 종단을 믿지 않고 `GlobalSize`의 범위 안에서만 읽는다.
                    const scoped_global_lock lock { handle };
                    if (lock.memory != nullptr)
                        if (utf_conversion_result<std::u8string> converted { text_from_utf16_block(lock.memory, GlobalSize(handle)) }; converted.value.has_value())
                            data.text = std::move(*converted.value);
                }

            const std::array<image_candidate, 3> candidates {
                image_candidate { png_format, clipboard_image_format::encoded },
                image_candidate { CF_DIBV5, clipboard_image_format::dib },
                image_candidate { CF_DIB, clipboard_image_format::dib },
            };
            for (const image_candidate& candidate : candidates)
            {
                if (candidate.format == 0 || IsClipboardFormatAvailable(candidate.format) == FALSE)
                    continue;
                const HANDLE handle { GetClipboardData(candidate.format) };
                if (handle == nullptr)
                    continue;
                const SIZE_T bytes { GlobalSize(handle) };
                if (bytes == 0)
                    continue;
                // 상한을 넘는 블록은 복사하지 않는다. 그림은 있으나 받지 않았다는 사실만 싣는다.
                if (bytes > max_clipboard_image_bytes)
                {
                    data.image_format = candidate.kind;
                    data.image_too_large = true;
                    break;
                }
                const scoped_global_lock lock { handle };
                if (lock.memory == nullptr)
                    continue;
                const auto* const begin { static_cast<const std::uint8_t*>(lock.memory) };
                data.image = std::make_shared<const std::vector<std::uint8_t>>(begin, begin + bytes);
                data.image_format = candidate.kind;
                break;
            }
            return data;
        }
        catch (...)
        {
            data.text.clear();
            data.image.reset();
            data.image_format = clipboard_image_format::none;
            data.image_too_large = false;
            return data;
        }
    }

    bool clipboard_has_image() noexcept
    {
        const UINT png_format { RegisterClipboardFormatW(png_clipboard_format) };
        return (png_format != 0 && IsClipboardFormatAvailable(png_format) != FALSE) || IsClipboardFormatAvailable(CF_DIB) != FALSE;
    }
}

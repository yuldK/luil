#include "win32/win32_image_clipboard.h"

#include "luil/text/utf8_text.h"
#include "luil/ui/clipboard_image.h"
#include "ui/image_pixels.h"
#include "win32/utf8.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkImageInfo.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <memory>
#include <string_view>
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

        // CF_HTML 머리의 offset 칸 이름이다. 값은 열 자리로 써서 머리 길이가 값과 상관없이 정해진다.
        constexpr std::string_view html_version_line { "Version:0.9\r\n" };
        constexpr std::array<std::string_view, 4> html_offset_keys { "StartHTML:", "EndHTML:", "StartFragment:", "EndFragment:" };
        constexpr std::size_t html_offset_digits { 10 };
        constexpr std::string_view html_document_begin { "<html><body>\r\n<!--StartFragment-->" };
        constexpr std::string_view html_document_end { "<!--EndFragment-->\r\n</body></html>" };

        void append_offset(std::string& header, const std::string_view key, const std::size_t value)
        {
            std::array<char, html_offset_digits> digits {};
            std::size_t remaining { value };
            for (std::size_t index = html_offset_digits; index > 0; --index)
            {
                digits[index - 1] = static_cast<char>('0' + remaining % 10);
                remaining /= 10;
            }
            header += key;
            header.append(digits.data(), digits.size());
            header += "\r\n";
        }

        // 머리 값 하나다. 음수(`-1`)도 받는다. 없거나 숫자가 아니면 nullopt다.
        [[nodiscard]] std::optional<long long> header_value(const std::string_view header, const std::string_view key) noexcept
        {
            std::size_t at { 0 };
            while (at < header.size())
            {
                std::size_t end { header.find('\n', at) };
                if (end == std::string_view::npos)
                    end = header.size();
                std::string_view line { header.substr(at, end - at) };
                at = end + 1;
                if (line.empty() == false && line.back() == '\r')
                    line.remove_suffix(1);
                if (line.starts_with(key) == false)
                    continue;
                std::string_view number { line.substr(key.size()) };
                while (number.empty() == false && number.front() == ' ')
                    number.remove_prefix(1);
                long long value { 0 };
                const auto [stop, error] { std::from_chars(number.data(), number.data() + number.size(), value) };
                if (error != std::errc {} || stop != number.data() + number.size())
                    return std::nullopt;
                return value;
            }
            return std::nullopt;
        }

        // [begin, end)가 블록 안의 바른 범위인가.
        [[nodiscard]] bool html_range(const std::optional<long long> begin, const std::optional<long long> end, const std::size_t size) noexcept
        {
            return begin.has_value() && end.has_value() && *begin >= 0 && *begin <= *end && static_cast<unsigned long long>(*end) <= size;
        }

        // 머리 한 벌의 길이다. 값의 크기와 상관없다.
        [[nodiscard]] constexpr std::size_t html_header_size() noexcept
        {
            std::size_t size { html_version_line.size() };
            for (const std::string_view key : html_offset_keys)
                size += key.size() + html_offset_digits + 2;
            return size;
        }
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

    std::optional<std::vector<std::byte>> cf_html_block(const std::u8string_view fragment)
    {
        if (text::utf8_is_valid(fragment) == false)
            return std::nullopt;
        // 머리 길이를 먼저 알아야 offset을 정한다. 값은 열 자리라 길이가 늘 같다.
        const std::size_t start_html { html_header_size() };
        const std::size_t start_fragment { start_html + html_document_begin.size() };
        const std::size_t end_fragment { start_fragment + fragment.size() };
        const std::size_t end_html { end_fragment + html_document_end.size() };

        std::string header { html_version_line };
        append_offset(header, html_offset_keys[0], start_html);
        append_offset(header, html_offset_keys[1], end_html);
        append_offset(header, html_offset_keys[2], start_fragment);
        append_offset(header, html_offset_keys[3], end_fragment);

        std::vector<std::byte> block(end_html + 1);
        std::byte* cursor { block.data() };
        const auto put = [&cursor](const void* data, const std::size_t size) {
            std::memcpy(cursor, data, size);
            cursor += size;
        };
        put(header.data(), header.size());
        put(html_document_begin.data(), html_document_begin.size());
        put(fragment.data(), fragment.size());
        put(html_document_end.data(), html_document_end.size());
        // 끝의 NUL은 `EndHTML` 밖이다. NUL 종단을 기대하는 받는 앱이 있다.
        *cursor = std::byte { 0 };
        return block;
    }

    std::optional<std::u8string> cf_html_fragment(const std::span<const std::byte> block)
    {
        if (block.empty())
            return std::nullopt;
        // 블록은 첫 NUL까지다. `GlobalSize`는 할당 단위로 올림한 크기라 뒤에 다른 바이트가 붙을 수 있다.
        const auto* const begin { reinterpret_cast<const char*>(block.data()) };
        std::size_t size { block.size() };
        if (const void* const nul { std::memchr(begin, 0, size) }; nul != nullptr)
            size = static_cast<std::size_t>(static_cast<const char*>(nul) - begin);
        const std::string_view whole { begin, size };
        // 머리는 첫 `<` 앞이다. 값을 찾는 데만 쓴다.
        const std::string_view header { whole.substr(0, std::min(whole.find('<'), whole.size())) };
        const std::optional<long long> start_html { header_value(header, html_offset_keys[0]) };
        std::optional<long long> end_html { header_value(header, html_offset_keys[1]) };
        const std::optional<long long> start_fragment { header_value(header, html_offset_keys[2]) };
        const std::optional<long long> end_fragment { header_value(header, html_offset_keys[3]) };
        // `EndHTML:-1`을 쓰는 생산자가 있다. 블록 끝이다.
        if (end_html.has_value() && *end_html < 0)
            end_html = static_cast<long long>(size);

        std::string_view selected {};
        if (html_range(start_fragment, end_fragment, size))
            selected = whole.substr(static_cast<std::size_t>(*start_fragment), static_cast<std::size_t>(*end_fragment - *start_fragment));
        else if (html_range(start_html, end_html, size))
            selected = whole.substr(static_cast<std::size_t>(*start_html), static_cast<std::size_t>(*end_html - *start_html));
        else
            return std::nullopt;
        const std::u8string_view fragment { reinterpret_cast<const char8_t*>(selected.data()), selected.size() };
        if (fragment.size() > max_clipboard_html_bytes || text::utf8_is_valid(fragment) == false)
            return std::nullopt;
        return std::u8string { fragment };
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
        if (request.html.empty() == false)
        {
            std::optional<std::vector<std::byte>> html { cf_html_block(request.html) };
            if (html.has_value() == false)
            {
                error = u8"The clipboard HTML is not valid UTF-8.";
                return std::nullopt;
            }
            if (const UINT format { RegisterClipboardFormatW(html_clipboard_format) }; format != 0)
                blocks.push_back(clipboard_block { format, std::move(*html) });
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
            const UINT html_format { RegisterClipboardFormatW(html_clipboard_format) };
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

            if (html_format != 0 && IsClipboardFormatAvailable(html_format) != FALSE)
                if (const HANDLE handle { GetClipboardData(html_format) }; handle != nullptr)
                {
                    const scoped_global_lock lock { handle };
                    if (lock.memory != nullptr)
                        if (std::optional<std::u8string> fragment { cf_html_fragment(std::span { static_cast<const std::byte*>(lock.memory), GlobalSize(handle) }) }; fragment.has_value())
                            data.html = std::move(*fragment);
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
            data.html.clear();
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

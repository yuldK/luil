#include "win32/win32_clipboard.h"
#include "win32/win32_image_clipboard.h"

#include "luil/ui/clipboard_image.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_encode.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 그림 클립보드다 (`CF_DIBV5` + "PNG" + 글, image-clipboard-design.md).
// 실제 클립보드를 쓰는 test는 이 프로세스의 메시지 전용 창을 임자로 삼고, 끝나면 클립보드를 비운다.
namespace {
    // 클립보드의 임자가 될 창이다. 창 없이 연 클립보드에는 `SetClipboardData`가 실패한다.
    class image_clipboard_window final
    {
    public:
        image_clipboard_window()
            : window_ { CreateWindowExW(0, L"STATIC", L"luil image clipboard test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
            REQUIRE(window_ != nullptr);
        }
        image_clipboard_window(const image_clipboard_window&) = delete;
        image_clipboard_window& operator=(const image_clipboard_window&) = delete;
        ~image_clipboard_window()
        {
            if (luil::win32::open_clipboard(window_))
            {
                EmptyClipboard();
                CloseClipboard();
            }
            DestroyWindow(window_);
        }

        [[nodiscard]] HWND handle() const noexcept
        {
            return window_;
        }

    private:
        HWND window_ { nullptr };
    };

    // 2×2 그림이다. 윗줄: 불투명 빨강·반투명 초록, 아랫줄: 불투명 파랑·불투명 흰색.
    [[nodiscard]] luil::ui_image make_sample_image()
    {
        const std::vector<std::uint8_t> pixels { 255, 0, 0, 255, 0, 200, 0, 128, 0, 0, 255, 255, 255, 255, 255, 255 };
        return luil::make_rgba_image(2, 2, pixels);
    }

    [[nodiscard]] BITMAPV5HEADER header_of(const std::vector<std::byte>& block)
    {
        REQUIRE(block.size() >= sizeof(BITMAPV5HEADER));
        BITMAPV5HEADER header {};
        std::memcpy(&header, block.data(), sizeof(BITMAPV5HEADER));
        return header;
    }

    // 블록의 한 픽셀(BGRA)이다. `row`는 블록에 적힌 행 순서(아래에서 위로)다.
    [[nodiscard]] std::array<std::uint8_t, 4> stored_pixel(const std::vector<std::byte>& block, const int width, const int row, const int column)
    {
        const std::size_t at { sizeof(BITMAPV5HEADER) + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 4u };
        return {
            std::to_integer<std::uint8_t>(block[at]),
            std::to_integer<std::uint8_t>(block[at + 1]),
            std::to_integer<std::uint8_t>(block[at + 2]),
            std::to_integer<std::uint8_t>(block[at + 3]),
        };
    }

    [[nodiscard]] UINT png_format()
    {
        const UINT format { RegisterClipboardFormatW(luil::win32::png_clipboard_format) };
        REQUIRE(format != 0);
        return format;
    }

    // 클립보드를 열지 않고 형식이 있는지만 본다.
    [[nodiscard]] bool format_on_clipboard(const UINT format)
    {
        return IsClipboardFormatAvailable(format) != FALSE;
    }

    // 두 번째 넣기부터 실패하는 `SetClipboardData`다.
    int set_calls { 0 };

    HANDLE WINAPI fail_after_first(const UINT format, const HANDLE memory)
    {
        if (set_calls++ == 0)
            return SetClipboardData(format, memory);
        return nullptr;
    }
}

TEST_CASE("A DIBV5 block is a bottom-up straight alpha bitfield bitmap", "[win32][clipboard][image]")
{
    std::u8string error {};
    const std::optional<std::vector<std::byte>> block { luil::win32::dibv5_block(make_sample_image(), error) };
    REQUIRE(block.has_value());
    const BITMAPV5HEADER header { header_of(*block) };
    REQUIRE(header.bV5Size == 124u);
    REQUIRE(header.bV5Width == 2);
    REQUIRE(header.bV5Height == 2);
    REQUIRE(header.bV5Planes == 1);
    REQUIRE(header.bV5BitCount == 32);
    REQUIRE(header.bV5Compression == BI_BITFIELDS);
    REQUIRE(header.bV5RedMask == 0x00FF0000u);
    REQUIRE(header.bV5GreenMask == 0x0000FF00u);
    REQUIRE(header.bV5BlueMask == 0x000000FFu);
    REQUIRE(header.bV5AlphaMask == 0xFF000000u);
    REQUIRE(header.bV5CSType == static_cast<DWORD>(LCS_sRGB));
    REQUIRE(block->size() == sizeof(BITMAPV5HEADER) + 16u);

    // 첫 행이 그림의 아랫줄이다 (파랑, 흰색).
    REQUIRE(stored_pixel(*block, 2, 0, 0) == std::array<std::uint8_t, 4> { 255, 0, 0, 255 });
    REQUIRE(stored_pixel(*block, 2, 0, 1) == std::array<std::uint8_t, 4> { 255, 255, 255, 255 });
    REQUIRE(stored_pixel(*block, 2, 1, 0) == std::array<std::uint8_t, 4> { 0, 0, 255, 255 });
    // 반투명 초록은 곱하지 않은 값이다. 곱한 값이면 초록이 100 남짓이다.
    const std::array<std::uint8_t, 4> green { stored_pixel(*block, 2, 1, 1) };
    REQUIRE(green[0] == 0);
    REQUIRE(std::abs(static_cast<int>(green[1]) - 200) <= 2);
    REQUIRE(green[2] == 0);
    REQUIRE(green[3] == 128);
}

TEST_CASE("An SVG image becomes a DIBV5 block at its own size", "[win32][clipboard][image]")
{
    constexpr std::string_view svg { R"(<svg xmlns="http://www.w3.org/2000/svg" width="6" height="4"><rect width="6" height="4" fill="#0000ff"/></svg>)" };
    luil::image_decode_error decode_error {};
    const luil::ui_image image { luil::decode_svg_bytes(std::span { reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size() }, decode_error) };
    REQUIRE(image.valid());
    std::u8string error {};
    const std::optional<std::vector<std::byte>> block { luil::win32::dibv5_block(image, error) };
    REQUIRE(block.has_value());
    const BITMAPV5HEADER header { header_of(*block) };
    REQUIRE(header.bV5Width == 6);
    REQUIRE(header.bV5Height == 4);
    REQUIRE(stored_pixel(*block, 6, 0, 0) == std::array<std::uint8_t, 4> { 255, 0, 0, 255 });
}

TEST_CASE("Clipboard data blocks put PNG first and drop bytes that are not PNG", "[win32][clipboard][image]")
{
    std::u8string error {};
    luil::clipboard_data_copy_request request {};
    request.text = u8"sample.png";
    request.image = make_sample_image();
    request.png = luil::encode_png(request.image, error);
    REQUIRE(request.png.empty() == false);

    const std::optional<std::vector<luil::win32::clipboard_block>> blocks { luil::win32::clipboard_data_blocks(request, error) };
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 3u);
    REQUIRE((*blocks)[0].format == png_format());
    REQUIRE((*blocks)[0].bytes.size() == request.png.size());
    REQUIRE((*blocks)[1].format == CF_DIBV5);
    REQUIRE((*blocks)[2].format == CF_UNICODETEXT);

    // PNG 서명이 아니면 "PNG"를 넣지 않는다.
    request.png = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const std::optional<std::vector<luil::win32::clipboard_block>> without_png { luil::win32::clipboard_data_blocks(request, error) };
    REQUIRE(without_png.has_value());
    REQUIRE(without_png->size() == 2u);
    REQUIRE(without_png->front().format == CF_DIBV5);

    // 그림이 없으면 PNG도 넣지 않는다. 모두 비면 블록이 없다.
    luil::clipboard_data_copy_request text_only {};
    text_only.text = u8"글";
    text_only.png = luil::encode_png(make_sample_image(), error);
    const std::optional<std::vector<luil::win32::clipboard_block>> text_blocks { luil::win32::clipboard_data_blocks(text_only, error) };
    REQUIRE(text_blocks.has_value());
    REQUIRE(text_blocks->size() == 1u);
    REQUIRE(text_blocks->front().format == CF_UNICODETEXT);
    const std::optional<std::vector<luil::win32::clipboard_block>> nothing { luil::win32::clipboard_data_blocks(luil::clipboard_data_copy_request {}, error) };
    REQUIRE(nothing.has_value());
    REQUIRE(nothing->empty());
}

TEST_CASE("An image put on the clipboard reads back with its text and pixels", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    std::u8string error {};
    luil::clipboard_data_copy_request request {};
    request.text = u8"첫 줄\n\t둘째 줄";
    request.image = make_sample_image();
    request.png = luil::encode_png(request.image, error);
    REQUIRE(luil::win32::copy_data_to_clipboard(owner.handle(), request));
    REQUIRE(luil::win32::clipboard_has_image());

    // "PNG"가 DIB보다 먼저 읽힌다.
    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.text == request.text);
    REQUIRE(data.image_format == luil::clipboard_image_format::encoded);
    REQUIRE(data.image != nullptr);
    REQUIRE(*data.image == request.png);
    REQUIRE(data.sequence == static_cast<std::uint32_t>(GetClipboardSequenceNumber()));

    luil::image_decode_error decode_error {};
    const luil::ui_image image { luil::decode_clipboard_image(data, {}, decode_error) };
    REQUIRE(decode_error.empty());
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);
}

TEST_CASE("An image without PNG reads back as a DIB with the same pixels", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    luil::clipboard_data_copy_request request {};
    request.image = make_sample_image();
    REQUIRE(luil::win32::copy_data_to_clipboard(owner.handle(), request));
    REQUIRE(format_on_clipboard(CF_DIBV5));
    REQUIRE(format_on_clipboard(CF_DIB));
    REQUIRE(format_on_clipboard(CF_UNICODETEXT) == false);

    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.text.empty());
    REQUIRE(data.image_format == luil::clipboard_image_format::dib);
    REQUIRE(data.image != nullptr);

    luil::image_decode_error decode_error {};
    const luil::ui_image image { luil::decode_clipboard_image(data, {}, decode_error) };
    REQUIRE(decode_error.empty());
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);
    // 넣은 블록과 다시 지은 블록이 같다 — 픽셀이 한 바퀴 돌아 그대로다.
    std::u8string error {};
    const std::optional<std::vector<std::byte>> sent { luil::win32::dibv5_block(request.image, error) };
    const std::optional<std::vector<std::byte>> received { luil::win32::dibv5_block(image, error) };
    REQUIRE(sent.has_value());
    REQUIRE(received.has_value());
    REQUIRE(*sent == *received);
}

TEST_CASE("A text-only clipboard has no image", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    REQUIRE(luil::win32::copy_text_to_clipboard(owner.handle(), u8"글만 있다"));
    REQUIRE(luil::win32::clipboard_has_image() == false);
    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.text == u8"글만 있다");
    REQUIRE(data.image_format == luil::clipboard_image_format::none);
    REQUIRE(data.image == nullptr);
    REQUIRE(data.image_too_large == false);
}

TEST_CASE("A bitmap-only clipboard counts as an image", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    const std::array<std::uint32_t, 4> bits { 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu };
    const HBITMAP bitmap { CreateBitmap(2, 2, 1, 32, bits.data()) };
    REQUIRE(bitmap != nullptr);
    REQUIRE(luil::win32::open_clipboard(owner.handle()));
    EmptyClipboard();
    // 성공하면 비트맵은 시스템의 것이다.
    const bool stored { SetClipboardData(CF_BITMAP, bitmap) != nullptr };
    CloseClipboard();
    if (stored == false)
        DeleteObject(bitmap);
    REQUIRE(stored);
    // OS가 `CF_DIB`를 지어 준다.
    REQUIRE(luil::win32::clipboard_has_image());
    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.image_format == luil::clipboard_image_format::dib);
    REQUIRE(data.image != nullptr);
}

TEST_CASE("A failed block takes the blocks before it off the clipboard", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    std::u8string error {};
    luil::clipboard_data_copy_request request {};
    request.text = u8"남지 않는다";
    request.image = make_sample_image();
    request.png = luil::encode_png(request.image, error);
    const std::optional<std::vector<luil::win32::clipboard_block>> blocks { luil::win32::clipboard_data_blocks(request, error) };
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 3u);

    set_calls = 0;
    REQUIRE(luil::win32::put_clipboard_blocks(owner.handle(), *blocks, &fail_after_first) == false);
    REQUIRE(set_calls == 2);
    // 첫 블록("PNG")은 들어갔다가 거둬졌다. 뒤의 것은 들어가지 않았다.
    REQUIRE(format_on_clipboard(png_format()) == false);
    REQUIRE(format_on_clipboard(CF_DIBV5) == false);
    REQUIRE(format_on_clipboard(CF_UNICODETEXT) == false);
    REQUIRE(luil::win32::clipboard_has_image() == false);

    // 같은 블록이 실패 없이는 모두 들어간다.
    REQUIRE(luil::win32::put_clipboard_blocks(owner.handle(), *blocks));
    REQUIRE(format_on_clipboard(png_format()));
    REQUIRE(format_on_clipboard(CF_UNICODETEXT));
}

TEST_CASE("PNG is read before DIB when both were put by hand", "[win32][clipboard][image][system]")
{
    const image_clipboard_window owner {};
    std::u8string error {};
    // 순서를 거꾸로 넣어도 읽는 순서는 "PNG"가 먼저다.
    const std::optional<std::vector<std::byte>> dib { luil::win32::dibv5_block(make_sample_image(), error) };
    REQUIRE(dib.has_value());
    const std::vector<std::uint8_t> png { luil::encode_png(make_sample_image(), error) };
    std::vector<std::byte> png_bytes(png.size());
    std::memcpy(png_bytes.data(), png.data(), png.size());
    const std::vector<luil::win32::clipboard_block> blocks { { CF_DIBV5, *dib }, { png_format(), png_bytes } };
    REQUIRE(luil::win32::put_clipboard_blocks(owner.handle(), blocks));
    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.image_format == luil::clipboard_image_format::encoded);
    REQUIRE(data.image != nullptr);
    REQUIRE(*data.image == png);
}

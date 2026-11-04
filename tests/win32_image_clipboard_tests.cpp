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

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
    {
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    [[nodiscard]] std::string string_of(const std::vector<std::byte>& bytes)
    {
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    [[nodiscard]] long long header_number(const std::string& block, const std::string& key)
    {
        const std::size_t at { block.find(key + ":") };
        REQUIRE(at != std::string::npos);
        return std::stoll(block.substr(at + key.size() + 1, 10));
    }

    [[nodiscard]] UINT html_format()
    {
        const UINT format { RegisterClipboardFormatW(luil::win32::html_clipboard_format) };
        REQUIRE(format != 0);
        return format;
    }

    // 소멸자가 클립보드를 비우지 않는 메시지 전용 창이다.
    class backup_window final
    {
    public:
        backup_window()
            : handle_ { CreateWindowExW(0, L"STATIC", L"luil clipboard backup", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
        }
        backup_window(const backup_window&) = delete;
        backup_window& operator=(const backup_window&) = delete;
        ~backup_window()
        {
            DestroyWindow(handle_);
        }

        [[nodiscard]] HWND handle() const noexcept
        {
            return handle_;
        }

    private:
        HWND handle_ { nullptr };
    };

    // 시작 전 클립보드의 글과 HTML을 쥐었다가 끝에 되돌린다. 사용자의 클립보드를 test가 지우지 않는다.
    //  - 다른 test 창이 끝에 클립보드를 비운 뒤에 되돌려야 하므로 그 창보다 먼저 짓는다.
    class clipboard_backup final
    {
    public:
        clipboard_backup()
        {
            REQUIRE(window_.handle() != nullptr);
            if (luil::win32::open_clipboard(window_.handle()) == false)
                return;
            const std::array<UINT, 2> formats { static_cast<UINT>(CF_UNICODETEXT), RegisterClipboardFormatW(luil::win32::html_clipboard_format) };
            for (const UINT format : formats)
            {
                if (format == 0 || IsClipboardFormatAvailable(format) == FALSE)
                    continue;
                const HANDLE handle { GetClipboardData(format) };
                if (handle == nullptr)
                    continue;
                const luil::win32::scoped_global_lock lock { handle };
                if (lock.memory == nullptr)
                    continue;
                const auto* const begin { static_cast<const std::byte*>(lock.memory) };
                saved_.push_back(luil::win32::clipboard_block { format, std::vector<std::byte>(begin, begin + GlobalSize(handle)) });
            }
            CloseClipboard();
        }
        clipboard_backup(const clipboard_backup&) = delete;
        clipboard_backup& operator=(const clipboard_backup&) = delete;
        ~clipboard_backup()
        {
            if (saved_.empty() == false)
                static_cast<void>(luil::win32::put_clipboard_blocks(window_.handle(), saved_));
        }

    private:
        backup_window window_ {};
        std::vector<luil::win32::clipboard_block> saved_ {};
    };
}

TEST_CASE("A CF_HTML block points its header offsets at the document and the fragment", "[win32][clipboard][rich-text]")
{
    const std::u8string fragment { u8"<b>굵게</b> 글" };
    const std::optional<std::vector<std::byte>> block { luil::win32::cf_html_block(fragment) };
    REQUIRE(block.has_value());
    const std::string text { string_of(*block) };
    REQUIRE(text.starts_with("Version:0.9\r\nStartHTML:"));
    // 값은 열 자리 0 채움이라 머리 길이가 늘 같다.
    REQUIRE(text.find("StartHTML:0000000") != std::string::npos);
    const long long start_html { header_number(text, "StartHTML") };
    const long long end_html { header_number(text, "EndHTML") };
    const long long start_fragment { header_number(text, "StartFragment") };
    const long long end_fragment { header_number(text, "EndFragment") };
    REQUIRE(text.substr(static_cast<std::size_t>(start_html)).starts_with("<html><body>"));
    REQUIRE(text.substr(static_cast<std::size_t>(start_fragment), static_cast<std::size_t>(end_fragment - start_fragment)) == std::string { fragment.begin(), fragment.end() });
    REQUIRE(text.substr(static_cast<std::size_t>(start_fragment) - 20, 20) == "<!--StartFragment-->");
    REQUIRE(text.substr(static_cast<std::size_t>(end_fragment), 18) == "<!--EndFragment-->");
    // 끝의 NUL은 `EndHTML` 밖이다.
    REQUIRE(static_cast<std::size_t>(end_html) == block->size() - 1);
    REQUIRE(block->back() == std::byte { 0 });
    REQUIRE(text.substr(0, static_cast<std::size_t>(end_html)).ends_with("</body></html>"));

    // 지은 블록에서 같은 조각이 나온다. 깨진 UTF-8은 블록을 짓지 않는다.
    REQUIRE(luil::win32::cf_html_fragment(*block) == fragment);
    REQUIRE(luil::win32::cf_html_block(std::u8string { u8"<b>" } + char8_t { 0xFF } + u8"</b>").has_value() == false);
}

TEST_CASE("A CF_HTML fragment is read only inside the block and its offsets", "[win32][clipboard][rich-text]")
{
    const std::optional<std::vector<std::byte>> block { luil::win32::cf_html_block(u8"<i>x</i>") };
    REQUIRE(block.has_value());
    // 잘린 블록은 조각도 문서도 범위 밖이라 거절한다.
    const std::vector<std::byte> truncated(block->begin(), block->begin() + static_cast<std::ptrdiff_t>(block->size() / 2 + 20));
    REQUIRE(luil::win32::cf_html_fragment(truncated).has_value() == false);
    // `GlobalSize`가 올림한 뒤의 바이트는 첫 NUL에서 끊는다.
    std::vector<std::byte> padded { *block };
    padded.insert(padded.end(), 16, std::byte { 0x7A });
    REQUIRE(luil::win32::cf_html_fragment(padded) == std::u8string { u8"<i>x</i>" });

    // LF 줄, 모르는 키, 조각 offset이 없으면 문서 범위다. `EndHTML:-1`은 블록 끝이다.
    const std::string_view document { "Version:1.0\nStartHTML:0000000075\nEndHTML:-1\nSourceURL:https://example.com/\n<b>a</b>" };
    REQUIRE(document.find("<b>") == 75u);
    REQUIRE(luil::win32::cf_html_fragment(bytes_of(document)) == std::u8string { u8"<b>a</b>" });
    // 조각 offset이 거꾸로이거나 범위 밖이면 문서 범위로 물러선다. 그것도 맞지 않으면 거절한다.
    const std::string_view reversed { "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000000113\r\nStartFragment:0000000110\r\nEndFragment:0000000108\r\n<u>b</u>" };
    REQUIRE(reversed.find("<u>") == 105u);
    REQUIRE(reversed.size() == 113u);
    REQUIRE(luil::win32::cf_html_fragment(bytes_of(reversed)) == std::u8string { u8"<u>b</u>" });
    const std::string_view outside { "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000099999\r\nStartFragment:0000000105\r\nEndFragment:0000099999\r\n<u>b</u>" };
    REQUIRE(luil::win32::cf_html_fragment(bytes_of(outside)).has_value() == false);
    REQUIRE(luil::win32::cf_html_fragment(bytes_of("<b>no header</b>")).has_value() == false);
    REQUIRE(luil::win32::cf_html_fragment({}).has_value() == false);
}

TEST_CASE("Clipboard data blocks put HTML between the image and the text", "[win32][clipboard][rich-text]")
{
    std::u8string error {};
    luil::clipboard_data_copy_request request {};
    request.text = u8"글";
    request.html = u8"<b>글</b>";
    request.image = make_sample_image();
    request.png = luil::encode_png(request.image, error);
    const std::optional<std::vector<luil::win32::clipboard_block>> blocks { luil::win32::clipboard_data_blocks(request, error) };
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 4u);
    REQUIRE((*blocks)[0].format == png_format());
    REQUIRE((*blocks)[1].format == CF_DIBV5);
    REQUIRE((*blocks)[2].format == html_format());
    REQUIRE((*blocks)[3].format == CF_UNICODETEXT);
    REQUIRE(luil::win32::cf_html_fragment((*blocks)[2].bytes) == request.html);

    // 깨진 HTML은 반쪽 클립을 짓지 않는다.
    luil::clipboard_data_copy_request broken {};
    broken.text = u8"글";
    broken.html = std::u8string { u8"<b>" } + char8_t { 0xFF } + u8"</b>";
    REQUIRE(luil::win32::clipboard_data_blocks(broken, error).has_value() == false);
    REQUIRE(error.empty() == false);
}

TEST_CASE("Text and HTML put on the clipboard read back together", "[win32][clipboard][rich-text][system]")
{
    // 되돌리기가 test 창의 비우기보다 뒤에 서도록 먼저 짓는다.
    const clipboard_backup backup {};
    const image_clipboard_window owner {};
    luil::clipboard_data_copy_request request {};
    request.text = u8"굵게 그리고 링크";
    request.html = u8"<span style=\"white-space: pre-wrap;\"><b>굵게</b> 그리고 <a href=\"https://example.com/\">링크</a></span>";
    REQUIRE(luil::win32::copy_data_to_clipboard(owner.handle(), request));
    REQUIRE(format_on_clipboard(html_format()));
    REQUIRE(format_on_clipboard(CF_UNICODETEXT));

    const luil::clipboard_data data { luil::win32::read_clipboard_data(owner.handle()) };
    REQUIRE(data.text == request.text);
    REQUIRE(data.html == request.html);
    REQUIRE(data.image_format == luil::clipboard_image_format::none);

    // 글만 넣은 클립에는 HTML이 없다.
    REQUIRE(luil::win32::copy_text_to_clipboard(owner.handle(), u8"글만"));
    REQUIRE(luil::win32::read_clipboard_data(owner.handle()).html.empty());
}

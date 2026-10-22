#include "luil/generated/codicons.h"
#include "luil/net/http_client.h"
#include "luil/ui/glyph_element.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_encode.h"
#include "luil/ui/markdown_element.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

int main()
{
    const HMODULE module { GetModuleHandleW(nullptr) };
    for (int id = 101; id <= 104; ++id)
    {
        const HRSRC resource { FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA) };
        if (resource == nullptr || SizeofResource(module, resource) == 0 || LoadResource(module, resource) == nullptr)
            return 1;
    }
    const HRSRC notice_resource { FindResourceW(module, MAKEINTRESOURCEW(104), RT_RCDATA) };
    const auto notice_data { LockResource(LoadResource(module, notice_resource)) };
    if (notice_data == nullptr)
        return 5;
    const std::string_view notice_text(static_cast<const char*>(notice_data), SizeofResource(module, notice_resource));
    if (notice_text.find("Component: luil") == std::string_view::npos || notice_text.find("Copyright (c) 2026 yuldk") == std::string_view::npos
        || notice_text.find("Permission is hereby granted, free of charge") == std::string_view::npos || notice_text.find("OUT OF OR IN CONNECTION WITH THE SOFTWARE") == std::string_view::npos)
        return 6;
    if (notice_text.find("Component: MD4C") == std::string_view::npos)
        return 10;
    const auto markdown { luil::parse_markdown(u8"**help** [link](app:help) &amp;") };
    if (markdown.size() != 1 || markdown.front().text != u8"help link &")
        return 11;
    const auto tooltip { luil::make_markdown_tooltip({ .text = u8"### Help\n\n`code`" }) };
    if (tooltip.content == nullptr || tooltip.content->root() == nullptr || tooltip.content->unarranged().empty() == false)
        return 12;
    const auto font { luil::load_codicon_typeface() };
    if (font == nullptr || font->unicharToGlyph(luil::codicons::icon_search) == 0)
        return 3;
    const luil::glyph_element icon {
        luil::ui_element_id { luil::application_element_kind(0) },
        luil::glyph_config { .glyph = luil::codicons::icon_search, .typeface = font, .description = u8"검색" },
    };
    if (icon.accessibility().name != u8"검색")
        return 4;

    // 설치본의 공개 헤더와 기능별 아카이브도 소비자가 실제로 부를 수 있어야 한다.
    constexpr std::array<std::uint8_t, 4> red { 255, 0, 0, 255 };
    const auto image { luil::make_rgba_image(1, 1, red) };
    std::u8string encode_error {};
    if (luil::encode_png(image, encode_error).empty() || luil::encode_jpeg(image, 90, encode_error).empty() || luil::encode_webp(image, 90, encode_error).empty()
        || luil::encode_webp_lossless(image, encode_error).empty())
        return 7;
    const auto bytes_of = [](const std::string_view text) { return std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }; };
    luil::image_decode_error decode_error {};
    constexpr std::string_view svg { R"(<svg xmlns="http://www.w3.org/2000/svg" width="2" height="2"><rect width="2" height="2"/></svg>)" };
    if (luil::decode_svg_bytes(bytes_of(svg), decode_error).valid() != (LUIL_CONSUMER_SVG != 0))
        return 8;
    constexpr std::string_view lottie { R"({"v":"5.7.0","fr":10,"ip":0,"op":10,"w":4,"h":4,"layers":[]})" };
    if (luil::decode_lottie_bytes(bytes_of(lottie), decode_error).valid() != (LUIL_CONSUMER_LOTTIE != 0))
        return 9;

    luil::net::http_client_config config {};
    config.deliver = [](luil::net::http_response) {};
    std::u8string error {};
    const auto client { luil::net::http_client::create(std::move(config), error) };
    return client == nullptr ? 2 : 0;
}

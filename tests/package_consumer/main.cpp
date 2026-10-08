#include "luil/generated/codicons.h"
#include "luil/net/http_client.h"
#include "luil/ui/glyph_element.h"

#include <windows.h>

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
    const auto font { luil::load_codicon_typeface() };
    if (font == nullptr || font->unicharToGlyph(luil::codicons::icon_search) == 0)
        return 3;
    const luil::glyph_element icon {
        luil::ui_element_id { luil::application_element_kind(0) },
        luil::glyph_config { .glyph = luil::codicons::icon_search, .typeface = font, .description = u8"검색" },
    };
    if (icon.accessibility().name != u8"검색")
        return 4;
    luil::net::http_client_config config {};
    config.deliver = [](luil::net::http_response) {};
    std::u8string error {};
    const auto client { luil::net::http_client::create(std::move(config), error) };
    return client == nullptr ? 2 : 0;
}

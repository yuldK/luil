#include "luil/net/http_client.h"
#include "luil/ui/glyph_element.h"
#include "luil/generated/codicons.h"

#include <windows.h>

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

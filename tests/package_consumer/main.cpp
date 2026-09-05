#include "luil/net/http_client.h"

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
    luil::net::http_client_config config {};
    config.deliver = [](luil::net::http_response) {};
    std::u8string error {};
    const auto client { luil::net::http_client::create(std::move(config), error) };
    return client == nullptr ? 2 : 0;
}

#include "win32/win32_error.h"

#include <array>
#include <charconv>
#include <system_error>

namespace luil::win32 {
    std::u8string make_hresult_error(const std::u8string_view message, const HRESULT result)
    {
        std::u8string output { message };
        output += u8" (HRESULT=0x";
        std::array<char, 16> buffer {};
        const auto conversion {
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<unsigned long>(result), 16),
        };

        if (conversion.ec == std::errc {})
            for (const char* character = buffer.data(); character != conversion.ptr; ++character)
                output.push_back(static_cast<char8_t>(*character));
        output += u8")";
        return output;
    }
} // namespace luil::win32

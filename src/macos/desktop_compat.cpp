#include "luil/win32/drop_data.h"
#include "luil/win32/win32_window.h"

#include <cstdint>
#include <limits>
#include <optional>

// 공개 헤더가 모든 플랫폼에 선언하는 데스크톱 도우미의 macOS 몸이다. Windows 앱 코드가 고치지 않고 링크되게 한다.
//  - 창 자리 읽기와 배치 범위 검사는 값만 다루는 함수라 Windows와 같은 규칙이다 (win32_window.cpp).
//  - OS 끌기의 데이터 객체(`IDataObject`)는 macOS에 없다. 표면이 그것을 넘기지 않으므로(`native_drop_data`가 비었다)
//    읽기는 늘 빈 답이다.
namespace luil::win32 {
    namespace {
        // 십진 정수 하나다. 부호와 숫자만 받고 남는 글자가 있으면 실패다. 백만을 넘는 값은 오타로 본다.
        [[nodiscard]] std::optional<int> parse_screen_coordinate(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return std::nullopt;
            const bool negative { text.front() == u8'-' };
            const std::u8string_view digits { negative || text.front() == u8'+' ? text.substr(1) : text };
            if (digits.empty())
                return std::nullopt;
            long long value { 0 };
            for (const char8_t character : digits)
            {
                if (character < u8'0' || character > u8'9')
                    return std::nullopt;
                value = value * 10 + (character - u8'0');
                if (value > 1'000'000)
                    return std::nullopt;
            }
            return static_cast<int>(negative ? -value : value);
        }
    }

    std::optional<window_position> parse_window_position(const std::u8string_view text) noexcept
    {
        const std::size_t comma { text.find(u8',') };
        if (comma == std::u8string_view::npos)
            return std::nullopt;
        const std::optional<int> x { parse_screen_coordinate(text.substr(0, comma)) };
        const std::optional<int> y { parse_screen_coordinate(text.substr(comma + 1)) };
        if (x.has_value() == false || y.has_value() == false)
            return std::nullopt;
        return window_position { *x, *y };
    }

    bool placement_in_screen_range(const window_placement& placement) noexcept
    {
        constexpr long long lowest { std::numeric_limits<std::int32_t>::min() };
        constexpr long long highest { std::numeric_limits<std::int32_t>::max() };
        const long long right { static_cast<long long>(placement.x) + placement.width };
        const long long bottom { static_cast<long long>(placement.y) + placement.height };
        return right >= lowest && right <= highest && bottom >= lowest && bottom <= highest;
    }

    std::vector<dropped_virtual_file> drop_virtual_files(void*)
    {
        return {};
    }

    std::optional<std::vector<std::byte>> drop_data_bytes(void*, std::u8string_view, int, std::size_t)
    {
        return std::nullopt;
    }

    std::optional<std::u8string> drop_data_text(void*, std::u8string_view, std::size_t)
    {
        return std::nullopt;
    }
}

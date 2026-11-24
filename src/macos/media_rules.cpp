#include "macos/media_rules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace luil::macos {
    namespace {
        using win32::media_state;

        // 열린 항목이 있고 실패하지 않은 상태다. 재생·정지 신호가 뜻을 갖는다.
        [[nodiscard]] bool loaded(const media_state state) noexcept
        {
            return state == media_state::ready || state == media_state::playing || state == media_state::paused
                || state == media_state::stopped || state == media_state::ended;
        }
    }

    std::optional<media_state> next_media_state(const media_state current, const media_signal signal) noexcept
    {
        switch (signal)
        {
        case media_signal::item_ready:
            return current == media_state::opening ? std::optional { media_state::ready } : std::nullopt;
        case media_signal::item_failed:
        case media_signal::playback_failed:
            return current != media_state::failed && current != media_state::empty ? std::optional { media_state::failed } : std::nullopt;
        case media_signal::playing:
            return loaded(current) && current != media_state::playing ? std::optional { media_state::playing } : std::nullopt;
        case media_signal::paused:
            return loaded(current) && current != media_state::ended ? std::optional { media_state::paused } : std::nullopt;
        case media_signal::stopped:
            return loaded(current) ? std::optional { media_state::stopped } : std::nullopt;
        case media_signal::ended:
            return current == media_state::playing || current == media_state::paused ? std::optional { media_state::ended } : std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::int64_t> media_time_100ns(const std::int64_t value, const std::int32_t timescale) noexcept
    {
        constexpr std::int64_t ticks { 10'000'000 };
        if (timescale <= 0)
            return std::nullopt;
        // 몫과 나머지로 나눠 곱한다. 나머지 몫은 눈금보다 작아 넘치지 않는다.
        const std::int64_t whole { value / timescale };
        const std::int64_t part { value % timescale };
        if (whole > std::numeric_limits<std::int64_t>::max() / ticks || whole < std::numeric_limits<std::int64_t>::min() / ticks)
            return std::nullopt;
        return whole * ticks + part * ticks / timescale;
    }

    std::chrono::milliseconds media_time_ms(const std::int64_t value, const std::int32_t timescale) noexcept
    {
        const std::optional<std::int64_t> ticks { media_time_100ns(value, timescale) };
        if (ticks.has_value() == false || *ticks < 0)
            return std::chrono::milliseconds { 0 };
        return std::chrono::milliseconds { *ticks / 10'000 };
    }

    media_frame_size capped_frame_size(const double width, const double height, const int limit) noexcept
    {
        if (std::isfinite(width) == false || std::isfinite(height) == false || width < 1.0 || height < 1.0 || limit <= 0)
            return {};
        const double ratio { std::min(1.0, static_cast<double>(limit) / std::max(width, height)) };
        const auto side = [ratio](const double value) { return std::max(1, static_cast<int>(value * ratio)); };
        return { side(width), side(height) };
    }

    bool media_source_is_url(const std::u8string_view source) noexcept
    {
        const std::size_t colon { source.find(u8':') };
        if (colon == std::u8string_view::npos || colon < 2)
            return false;
        const auto letter = [](const char8_t c) { return (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z'); };
        if (letter(source.front()) == false)
            return false;
        // RFC 3986의 scheme 글자다.
        return std::all_of(source.begin() + 1, source.begin() + static_cast<std::ptrdiff_t>(colon), [&](const char8_t c) {
            return letter(c) || (c >= u8'0' && c <= u8'9') || c == u8'+' || c == u8'-' || c == u8'.';
        });
    }
}

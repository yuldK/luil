#include "luil/text/srt.h"

#include <charconv>
#include <fstream>
#include <optional>
#include <string_view>

namespace luil {
    namespace {
        std::string_view trim_spacing(const std::string_view value)
        {
            const auto first { value.find_first_not_of(" \t") };
            if (first == std::string_view::npos)
                return {};
            return value.substr(first, value.find_last_not_of(" \t") - first + 1);
        }

        std::optional<int> timestamp(const std::string_view value)
        {
            if (value.size() != 12 || value[2] != ':' || value[5] != ':' || value[8] != ',')
                return std::nullopt;
            const auto digits = [&](const std::size_t offset, const std::size_t size)
            {
                unsigned int result {};
                const char* begin { value.data() + offset };
                const auto parsed { std::from_chars(begin, begin + size, result) };
                return parsed.ec == std::errc {} && parsed.ptr == begin + size ? static_cast<int>(result) : -1;
            };
            const int hours { digits(0, 2) };
            const int minutes { digits(3, 2) };
            const int seconds { digits(6, 2) };
            const int millis { digits(9, 3) };
            if (hours < 0 || minutes < 0 || minutes >= 60 || seconds < 0 || seconds >= 60 || millis < 0)
                return std::nullopt;
            return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
        }
    }

    std::vector<subtitle_cue> read_srt(const std::filesystem::path& path)
    {
        std::ifstream input { path };
        std::vector<subtitle_cue> result {};
        std::string line {};
        bool first_line { true };
        const auto next_line = [&]()
        {
            if (std::getline(input, line).fail())
                return false;
            if (first_line && line.starts_with("\xEF\xBB\xBF"))
                line.erase(0, 3);
            first_line = false;
            if (line.ends_with('\r'))
                line.pop_back();
            return true;
        };
        while (next_line())
        {
            const auto arrow { line.find("-->") };
            if (arrow == std::string::npos)
                continue;
            const std::string_view times { line };
            const auto start { timestamp(trim_spacing(times.substr(0, arrow))) };
            const auto end { timestamp(trim_spacing(times.substr(arrow + 3))) };
            std::u8string text {};
            while (next_line() && trim_spacing(line).empty() == false)
            {
                if (text.empty() == false)
                    text += u8'\n';
                text.append(line.begin(), line.end());
            }
            if (start && end && *end > *start && text.empty() == false)
                result.push_back({ *start, *end, std::move(text) });
        }
        return result;
    }

    std::u8string subtitle_at(const std::vector<subtitle_cue>& cues, const std::chrono::milliseconds position)
    {
        std::u8string result {};
        for (const auto& item : cues)
        {
            if (position.count() < item.start_ms || position.count() >= item.end_ms)
                continue;
            if (result.empty() == false)
                result += u8'\n';
            result += item.text;
        }
        return result;
    }
}

#pragma once

#include <catch2/catch_tostring.hpp>

#include <chrono>
#include <string>

// 시험이 날짜·요일을 맞대다 실패하면 Catch2가 값을 글로 옮긴다. 그 자리에 libc++의 chrono `operator<<`가 들면
// 그 안의 chrono formatter가 부동소수점 `to_chars`를 불러, 배포 하한이 macOS 13.3보다 낮은 구성에서
// 컴파일이 멈춘다. 정수로 된 값이므로 그것을 거치지 않고 직접 적는다.
namespace Catch {
    template <>
    struct StringMaker<std::chrono::year_month_day>
    {
        static std::string convert(const std::chrono::year_month_day& value)
        {
            const auto padded = [](const unsigned number, const std::size_t width) {
                std::string text { std::to_string(number) };
                if (text.size() < width)
                    text.insert(0, width - text.size(), '0');
                return text;
            };

            const int year { static_cast<int>(value.year()) };
            std::string text { year < 0 ? "-" : "" };
            text += padded(static_cast<unsigned>(year < 0 ? -year : year), 4);
            text += '-';
            text += padded(static_cast<unsigned>(value.month()), 2);
            text += '-';
            text += padded(static_cast<unsigned>(value.day()), 2);
            if (!value.ok())
                text += " (invalid)";
            return text;
        }
    };

    // 요일은 영어 약칭으로 적는다 (libc++의 `operator<<`와 같은 글이다).
    template <>
    struct StringMaker<std::chrono::weekday>
    {
        static std::string convert(const std::chrono::weekday& value)
        {
            static constexpr const char* names[] { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            const unsigned index { value.c_encoding() };
            if (index < 7)
                return names[index];
            return std::to_string(index) + " is not a valid weekday";
        }
    };
}

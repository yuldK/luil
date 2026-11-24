#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#endif

// macOS 플랫폼 층이 함께 쓰는 글 변환이다. luil의 글은 UTF-8(`char8_t`)이고 POSIX와 Foundation은 `char`·NSString이다.
namespace luil::macos {
    [[nodiscard]] inline std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] inline std::u8string widen(const std::string_view text)
    {
        return { text.begin(), text.end() };
    }

    // `errno`의 뜻을 붙인 진단 영문이다.
    [[nodiscard]] inline std::u8string errno_error(const std::u8string_view message)
    {
        const int code { errno };
        std::u8string output { message };
        output += u8" (";
        for (const char* at { std::strerror(code) }; *at != '\0'; ++at)
            output.push_back(static_cast<char8_t>(*at));
        output += u8").";
        return output;
    }

#ifdef __OBJC__
    // UTF-8이 아니면 nil이다.
    [[nodiscard]] inline NSString* make_ns_string(const std::u8string_view text)
    {
        return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    }

    [[nodiscard]] inline std::u8string from_ns_string(NSString* const text)
    {
        if (text == nil)
            return {};
        const char* const bytes { [text UTF8String] };
        return bytes != nullptr ? widen(bytes) : std::u8string {};
    }
#endif
}

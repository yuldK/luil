#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

namespace luil::android {
    // 방금 실패한 POSIX 호출의 `errno`를 붙인 오류 글이다 ("... (No such file or directory)."). 호출한 바로 뒤에 불러야 맞다.
    //  - 실패 결과의 형식(`platform_result`, 감시자의 `std::u8string& error`)이 파일마다 달라 여기서는 글만 만든다
    //    (`win32::make_last_error`와 같은 자리다).
    //  - JVM도 OS 헤더도 쓰지 않아 header 하나로 둔다. core test가 감시자 소스를 그대로 묶는다.
    [[nodiscard]] inline std::u8string make_errno_error(const std::u8string_view message)
    {
        const int code { errno };
        std::u8string output { message };
        output += u8" (";
        for (const char* at { std::strerror(code) }; *at != '\0'; ++at)
            output.push_back(static_cast<char8_t>(*at));
        output += u8").";
        return output;
    }
}

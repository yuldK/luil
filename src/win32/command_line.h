#pragma once

#include <span>
#include <string>
#include <string_view>

namespace luil::win32 {
    // 인자 하나를 `CommandLineToArgvW`·MSVC CRT가 같은 값으로 되읽는 꼴로 감싼다.
    //  - 빈칸·탭·따옴표가 없고 비지 않았으면 그대로다.
    //  - 따옴표 앞의 백슬래시는 두 배로 하고 따옴표는 `\"`로, 끝의 백슬래시도 닫는 따옴표 앞이라 두 배로 한다.
    [[nodiscard]] std::wstring quote_argument(std::wstring_view argument);
    // 프로그램과 인자를 한 명령 줄로 잇는다. 프로그램 이름도 같은 규칙으로 감싼다.
    [[nodiscard]] std::wstring make_command_line(std::wstring_view program, std::span<const std::wstring> arguments);
}

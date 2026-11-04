#pragma once

#include "luil/app/platform_services.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// 둘째 인스턴스가 첫째의 주 창에 보내는 `WM_COPYDATA`의 운반 형식과 창 속성 이름이다
// (docs/single-instance-args-design.md). OS 호출이 없는 순수 변환이라 보내는 쪽(platform_services_win32.cpp)과
// 받는 쪽(win32_window.cpp)이 같은 규칙을 쓰고, 한 번에 시험한다.
namespace luil::win32 {
    // 형식 판이다. 모양을 바꾸면 올린다. 받는 쪽은 자기 판이 아닌 값을 이 메시지로 보지 않는다.
    inline constexpr std::uint32_t instance_message_version { 1 };

    // `COPYDATASTRUCT::dwData`다. 'LIN'과 형식 판을 담는다. 다른 보내는 쪽의 WM_COPYDATA를 이 길로 읽지 않으려는 표식이다.
    inline constexpr std::uintptr_t instance_message_tag { 0x4C494E00u + instance_message_version };

    // 싣는 글의 상한이다. 보내는 쪽이 막고 받는 쪽도 거절한다.
    //  - 한 명령 줄은 32,767자를 넘을 수 없어 크기 상한 안에 든다. 항목 수 상한은 한 글자 인자 만여 개로 넘길 수 있으나
    //    파일 연결이나 점프 목록 같은 쓰임은 미치지 못한다.
    inline constexpr std::size_t instance_message_max_bytes { 256 * 1024 };
    inline constexpr std::size_t instance_message_max_arguments { 4096 };

    // 이름의 상한이다 (UTF-16 코드 단위). 창 속성 이름이 255자까지라 `luil-instance:`(14자)를 뺀 값이다.
    // 뮤텍스 이름은 이 한계가 없어, 단일 인스턴스 생성자는 그대로 두고 보내기와 표식 걸기가 이것으로 거른다.
    inline constexpr std::size_t instance_name_max_code_units { 241 };

    // 첫 코드 단위의 표지 비트다. 나머지 비트는 0이어야 한다.
    inline constexpr wchar_t instance_flag_activate { 0x0001 };

    enum class instance_message_error
    {
        none,
        // 잘못된 UTF-8이나 짝 없는 surrogate다.
        invalid_text,
        // 인자나 작업 폴더에 NUL이 있다. NUL은 구분자라 담을 수 없다.
        embedded_nul,
        too_many_arguments,
        too_large,
        // 이 판의 표식(`instance_message_tag`)이 아니다.
        wrong_tag,
        // 코드 단위로 나누어지지 않는다 (홀수 바이트).
        misaligned,
        // 표지와 작업 폴더의 끝 NUL이 들어갈 자리도 없다 (빈 바이트열 포함).
        too_short,
        unknown_flags,
        // 마지막 글이 NUL로 끝나지 않는다.
        missing_terminator,
    };

    // 개발자가 읽을 영문이다. `none`은 빈 글이다.
    [[nodiscard]] std::u8string_view instance_message_error_text(instance_message_error error) noexcept;

    struct instance_encode_result
    {
        // UTF-16 글이다. 바이트로는 `std::as_bytes(std::span { units })`가 싣는 값이다.
        std::wstring units {};
        instance_message_error error { instance_message_error::none };

        [[nodiscard]] bool ok() const noexcept
        {
            return error == instance_message_error::none;
        }
    };

    struct instance_decode_result
    {
        instance_arguments value {};
        // 보낸 쪽이 첫 창을 앞으로 세워 달라고 했다.
        bool activate { false };
        instance_message_error error { instance_message_error::none };

        [[nodiscard]] bool ok() const noexcept
        {
            return error == instance_message_error::none;
        }
    };

    // 창 속성 이름이다: `luil-instance:<이름>`. 뮤텍스 이름에서 `Local\`을 뺀 것이다.
    //  - 이름이 비었거나, 잘못된 UTF-8이거나, NUL을 품었거나, UTF-16으로 241 코드 단위를 넘으면 빈 값이다.
    //  - 속성 이름은 대소문자를 가리지 않는 원자라, 대소문자만 다른 두 이름은 뮤텍스가 달라도 표식이 같다.
    [[nodiscard]] std::optional<std::wstring> instance_property_name(std::u8string_view name);

    // 운반 형식으로 짓는다: [표지 1단위][작업 폴더 NUL][인자 NUL]... (UTF-16). 인자가 없으면 작업 폴더 뒤에서 끝난다.
    //  - 입력 위반(잘못된 UTF-8, NUL, 항목·크기 상한)이면 아무것도 싣지 않고 까닭만 돌려준다.
    [[nodiscard]] instance_encode_result encode_instance_message(std::span<const std::u8string> arguments, std::u8string_view working_directory, bool activate);

    // 운반 형식을 읽는다. `tag`는 `dwData`, `bytes`는 `lpData`의 `cbData` 바이트다.
    //  - 모양이 맞지 않으면 까닭만 돌려준다. `bytes`는 호출 안에서만 쓰고 결과는 복사본이다.
    [[nodiscard]] instance_decode_result decode_instance_message(std::uintptr_t tag, std::span<const std::byte> bytes);
}

#pragma once

#include "luil/app/platform_services.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 둘째 인스턴스가 첫째의 Unix 소켓에 보내는 글의 운반 형식이다 (docs/single-instance-args-design.md의 macOS).
// OS 호출이 없는 순수 변환이라 보내는 쪽과 받는 쪽(single_instance_macos.mm)이 같은 규칙을 쓰고 core test가 본다.
// 모양은 Windows의 WM_COPYDATA와 같고 글자 단위만 UTF-8이다: [머리 8바이트][표지 1바이트][작업 폴더 NUL][인자 NUL]...
namespace luil::macos {
    // 형식 판이다. 모양을 바꾸면 올린다. 받는 쪽은 자기 판이 아닌 머리를 이 메시지로 보지 않는다.
    inline constexpr std::uint8_t instance_message_version { 1 };

    // 머리는 표식 넷(`L` `I` `N` 판)과 몸의 바이트 수(32비트 little endian)다.
    inline constexpr std::size_t instance_header_bytes { 8 };

    // 몸(표지 + 글)의 상한이다. Windows와 같은 수다. 보내는 쪽이 막고 받는 쪽도 거절한다.
    inline constexpr std::size_t instance_message_max_bytes { 256 * 1024 };
    inline constexpr std::size_t instance_message_max_arguments { 4096 };

    // 이름의 상한이다 (UTF-16 코드 단위). Windows 창 속성 이름의 한계를 그대로 따라 한 이름이 두 플랫폼에서 같은 답을 받는다.
    inline constexpr std::size_t instance_name_max_code_units { 241 };

    // 몸 첫 바이트의 표지 비트다. 나머지 비트는 0이어야 한다.
    inline constexpr std::uint8_t instance_flag_activate { 0x01 };

    // 받는 쪽의 답 한 바이트다.
    inline constexpr std::uint8_t instance_answer_rejected { 0 };
    inline constexpr std::uint8_t instance_answer_accepted { 1 };

    enum class instance_message_error
    {
        none,
        invalid_text,
        // 인자나 작업 폴더에 NUL이 있다. NUL은 구분자라 담을 수 없다.
        embedded_nul,
        too_many_arguments,
        too_large,
        // 이 판의 표식이 아니다.
        wrong_tag,
        // 머리가 모자라거나, 몸에 표지와 작업 폴더의 끝 NUL이 들어갈 자리도 없다.
        too_short,
        unknown_flags,
        // 마지막 글이 NUL로 끝나지 않는다.
        missing_terminator,
    };

    // 개발자가 읽을 영문이다. `none`은 빈 글이다.
    [[nodiscard]] std::u8string_view instance_message_error_text(instance_message_error error) noexcept;

    // 보낼 수 있는 이름인가: 비어 있지 않고, 유효한 UTF-8이고, NUL이 없고, UTF-16으로 241 코드 단위 이하다.
    [[nodiscard]] bool instance_name_valid(std::u8string_view name) noexcept;

    struct instance_encode_result
    {
        // 머리와 몸이다. 그대로 소켓에 쓴다.
        std::vector<std::byte> bytes {};
        instance_message_error error { instance_message_error::none };

        [[nodiscard]] bool ok() const noexcept
        {
            return error == instance_message_error::none;
        }
    };

    struct instance_header_result
    {
        // 머리 뒤에 올 몸의 바이트 수다.
        std::size_t body_bytes { 0 };
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

    // 운반 형식으로 짓는다. 인자가 없으면 작업 폴더 뒤에서 끝난다.
    //  - 입력 위반(잘못된 UTF-8, NUL, 항목·크기 상한)이면 아무것도 싣지 않고 까닭만 돌려준다.
    [[nodiscard]] instance_encode_result encode_instance_message(std::span<const std::u8string> arguments, std::u8string_view working_directory, bool activate);

    // 머리 8바이트를 읽는다. 표식이 다르면 `wrong_tag`, 몸이 상한을 넘으면 `too_large`, 몸이 표지와 끝 NUL보다 짧으면 `too_short`다.
    [[nodiscard]] instance_header_result decode_instance_header(std::span<const std::byte> header) noexcept;

    // 머리가 알린 만큼의 몸을 읽는다. 모양이 맞지 않으면 까닭만 돌려준다.
    [[nodiscard]] instance_decode_result decode_instance_body(std::span<const std::byte> body);
}

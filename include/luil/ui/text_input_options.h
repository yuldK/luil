#pragma once

namespace luil {
    // 소프트 키보드에 알리는 입력 종류다. 입력값 검사는 앱의 몫이다.
    enum class text_input_kind
    {
        text,
        number,
        decimal,
        phone,
        email,
        url,
    };

    // 키보드의 확정 단추다. 기본 done은 지금까지의 Enter 동작이다.
    enum class text_input_action
    {
        done,
        search,
        go,
        send,
        next,
        previous,
        none,
    };

    struct text_input_options
    {
        text_input_kind kind { text_input_kind::text };
        text_input_action action { text_input_action::done };
        // number·decimal 키보드에 부호 입력을 허용한다.
        bool signed_number { false };

        [[nodiscard]] bool operator==(const text_input_options&) const noexcept = default;
    };
} // namespace luil

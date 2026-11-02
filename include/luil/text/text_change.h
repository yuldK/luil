#pragma once

#include <cstddef>
#include <string_view>

namespace luil::text {
    // 글이 바뀐 구간이다. `before`의 [offset, offset + removed)를 `after`의 [offset, offset + inserted)가 대신한다.
    // 그 앞과 뒤는 두 글이 같다.
    struct text_change
    {
        std::size_t offset { 0 };
        std::size_t removed { 0 };
        std::size_t inserted { 0 };

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return removed == 0 && inserted == 0;
        }

        [[nodiscard]] constexpr bool operator==(const text_change&) const noexcept = default;
    };

    // 두 글을 견주어 바뀐 구간 하나를 찾는다 (공통 접두와 공통 접미의 사이).
    //  - 같은 글이면 `empty()`다.
    //  - 접두를 먼저 가져간다. 겹치지 않는 범위에서만 접미를 센다 (`aa`에서 `aaa`는 `{2, 0, 1}`).
    //  - `offset`과 변경의 끝(`offset + removed`는 `before`에서, `offset + inserted`는 `after`에서)은 두 글 모두에서
    //    코드포인트 경계다. 코드포인트 한가운데서 갈리면(`가`와 `각`) 구간을 그 코드포인트 처음까지 넓힌다.
    //    깨진 UTF-8에서도 범위는 두 글 안에 든다.
    //  - 글자소 경계는 약속하지 않는다. 필요한 호출자가 두 글에서 경계가 될 때까지 넓힌다.
    //  - 비용은 공통 접두와 접미의 길이에 비례한다. 최악은 글 전체를 한 번 훑는다.
    // 편집이 어느 경로(키 입력, IME 확정, 붙여넣기, 되돌리기, 밖에서 글 바꾸기)로 왔든 같은 답을 낸다.
    [[nodiscard]] text_change text_change_between(std::u8string_view before, std::u8string_view after) noexcept;
}

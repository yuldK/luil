#include "luil/text/text_change.h"

#include <algorithm>
#include <cstring>

namespace luil::text {
    namespace {
        constexpr std::size_t block_size { 64 };

        [[nodiscard]] bool is_continuation(const char8_t byte) noexcept
        {
            return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
        }

        // 앞에서부터 같은 바이트의 수다. 20MB 글에서 거의 같은 두 글을 견주므로 블록 단위로 건넌다.
        [[nodiscard]] std::size_t common_prefix(const std::u8string_view first, const std::u8string_view second) noexcept
        {
            const std::size_t limit { std::min(first.size(), second.size()) };
            std::size_t length { 0 };
            while (length + block_size <= limit
                && std::memcmp(first.data() + length, second.data() + length, block_size) == 0)
                length += block_size;
            while (length < limit
                && first[length] == second[length])
                ++length;
            return length;
        }

        // 뒤에서부터 같은 바이트의 수다. `limit`을 넘지 않는다 (접두와 겹치지 않게 하는 상한이다).
        [[nodiscard]] std::size_t common_suffix(const std::u8string_view first, const std::u8string_view second, const std::size_t limit) noexcept
        {
            std::size_t length { 0 };
            while (length + block_size <= limit
                && std::memcmp(first.data() + first.size() - length - block_size, second.data() + second.size() - length - block_size, block_size) == 0)
                length += block_size;
            while (length < limit
                && first[first.size() - length - 1] == second[second.size() - length - 1])
                ++length;
            return length;
        }
    }

    text_change text_change_between(const std::u8string_view before, const std::u8string_view after) noexcept
    {
        std::size_t prefix { common_prefix(before, after) };
        // 갈린 자리가 이어짐 바이트에 서면 코드포인트 한가운데다. 두 글이 같은 접두를 가졌으므로 처음까지 물러나면
        // 두 글 모두에서 경계가 된다. 한쪽 글이 거기서 끝나면(`prefix == size`) 그 글에서는 이미 경계다.
        while (prefix > 0
            && ((prefix < before.size() && is_continuation(before[prefix]))
                || (prefix < after.size() && is_continuation(after[prefix]))))
            --prefix;

        std::size_t suffix { common_suffix(before, after, std::min(before.size(), after.size()) - prefix) };
        // 접미의 첫 바이트가 이어짐 바이트면 그 코드포인트가 접미 밖에서 시작한다. 두 글의 접미가 같으니 앞쪽
        // 판정도 같고, 이어짐 바이트가 아닐 때까지 줄이면 변경의 끝이 두 글 모두에서 경계가 된다.
        while (suffix > 0
            && is_continuation(before[before.size() - suffix]))
            --suffix;

        return { prefix, before.size() - prefix - suffix, after.size() - prefix - suffix };
    }
}

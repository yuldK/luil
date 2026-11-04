#pragma once

#include "luil/text/text_change.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace luil::text {
    // 글자 하나에 걸리는 인라인 서식이다 (docs/rich-edit-design.md). 비트 합이다.
    //  - 뒤 비트는 2단계 몫(취소선 등)으로 남긴다. 글자 크기를 바꾸는 서식은 두지 않는다 (여러 줄 칸의 줄 높이가 고정이다).
    enum class text_mark : std::uint8_t
    {
        none = 0,
        bold = 1u << 0,
        italic = 1u << 1,
        underline = 1u << 2,
        code = 1u << 3,
    };

    [[nodiscard]] constexpr text_mark operator|(const text_mark left, const text_mark right) noexcept
    {
        return static_cast<text_mark>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr text_mark operator&(const text_mark left, const text_mark right) noexcept
    {
        return static_cast<text_mark>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    // `mark`의 비트가 모두 켜졌는가. `none`을 물으면 늘 참이다.
    [[nodiscard]] constexpr bool has_mark(const text_mark marks, const text_mark mark) noexcept
    {
        return (marks & mark) == mark;
    }

    // 같은 서식이 걸린 구간이다 (UTF-8 byte offset, [begin, end)).
    struct text_mark_run
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        text_mark marks { text_mark::none };

        [[nodiscard]] bool operator==(const text_mark_run&) const = default;
    };

    // 링크가 걸린 구간과 그 주소다. 주소는 비지 않는다.
    struct text_format_link
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        std::u8string destination {};

        [[nodiscard]] bool operator==(const text_format_link&) const = default;
    };

    // 글 한 벌의 서식이다 (UTF-8 byte offset, 글자소 경계).
    //  - 글은 들지 않는다. 글은 `text_edit_state::text`가 들고 이 값은 그 곁에서 함께 바뀐다.
    //  - 아래 불변식을 모든 함수가 지킨다. 그래서 같은 서식은 `==`로 같고, test와 presenter의 견줌이 여기에 기댄다.
    struct text_format
    {
        // begin 순이고 겹치지 않는다. 빈 구간과 `none` 구간은 없고, 맞닿은 같은 서식은 합친다.
        std::vector<text_mark_run> runs {};
        // begin 순이고 겹치지 않는다. 빈 구간은 없다. 맞닿은 이웃은 주소가 같을 때만 합친다.
        std::vector<text_format_link> links {};

        [[nodiscard]] bool operator==(const text_format&) const = default;
    };

    // offset 앞 글자(offset - 1 바이트)의 서식이다. 처음(0)이면 `none`이다.
    [[nodiscard]] text_mark text_marks_at(const text_format& format, std::size_t offset) noexcept;
    // [begin, end)의 모든 글자에 `mark`가 켜졌는가. 빈 범위는 거짓이다.
    [[nodiscard]] bool text_marks_cover(const text_format& format, std::size_t begin, std::size_t end, text_mark mark) noexcept;
    // [begin, end)의 글자에 `mark` 비트를 켜거나 끈 새 서식이다. 다른 비트와 링크는 그대로다.
    [[nodiscard]] text_format text_format_with_mark(const text_format& format, std::size_t begin, std::size_t end, text_mark mark, bool on);
    // [begin, end)의 링크를 걷고, 주소가 비지 않았으면 그 범위에 그 주소의 링크를 건 새 서식이다.
    //  - 범위에 걸친 이웃 링크는 범위 밖 조각만 남는다.
    [[nodiscard]] text_format text_format_with_link(const text_format& format, std::size_t begin, std::size_t end, std::u8string_view destination);
    // 글이 `change`만큼 바뀐 뒤의 서식이다. 넣은 글은 `inserted` 서식을 받는다.
    //  - 지운 범위는 구간에서 잘라 낸다. 그 뒤의 구간은 바뀐 길이만큼 옮긴다.
    //  - 링크는 넣은 자리가 링크 한가운데(`begin < offset < end`)일 때만 늘어난다. 링크 끝에 이어 쓴 글은 링크가 아니다.
    //  - 글을 모르므로 글자소 경계로 맞추지 않는다. `text_edit.cpp`가 바뀐 자리만 맞춘다.
    [[nodiscard]] text_format text_format_shifted(const text_format& format, const text_change& change, text_mark inserted);
    // [begin, end)의 서식을 0부터 다시 센 조각이다 (복사·잘라내기가 고른 구간의 서식을 싣는다).
    [[nodiscard]] text_format text_format_slice(const text_format& format, std::size_t begin, std::size_t end);
    // 실행 취소 기록의 바이트 상한이 세는 크기다. 구간과 링크 주소의 바이트를 더한다.
    [[nodiscard]] std::size_t text_format_bytes(const text_format& format) noexcept;
}

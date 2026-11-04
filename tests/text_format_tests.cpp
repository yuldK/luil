#include "luil/text/text_format.h"

#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using luil::text::text_format;
    using luil::text::text_mark;

    constexpr text_mark bold { text_mark::bold };
    constexpr text_mark italic { text_mark::italic };
    constexpr text_mark underline { text_mark::underline };
    constexpr text_mark code { text_mark::code };

    const std::u8string address { u8"https://example.com/" };

    // 서식 있는 글을 불러오고 caret을 놓는다.
    [[nodiscard]] luil::text::text_edit_state formatted(const std::u8string_view text, text_format format, const std::size_t caret)
    {
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_formatted_text(state, text, std::make_shared<const text_format>(std::move(format)));
        luil::text::text_edit_place_caret(state, caret, false);
        return state;
    }

    [[nodiscard]] const text_format& format_of(const luil::text::text_edit_state& state)
    {
        REQUIRE(state.format != nullptr);
        return *state.format;
    }
}

TEST_CASE("Shifting a format moves runs and links past an insertion, a deletion and a replacement", "[domain][text-format]")
{
    const text_format original { { { 2, 5, bold } }, { { 6, 9, address } } };

    // 구간 한가운데에 같은 서식으로 넣으면 셋이 하나로 합쳐진다. 그 뒤의 링크는 넣은 만큼 밀린다.
    const text_format same { luil::text::text_format_shifted(original, { 3, 0, 2 }, bold) };
    REQUIRE(same.runs == std::vector<luil::text::text_mark_run> { { 2, 7, bold } });
    REQUIRE(same.links == std::vector<luil::text::text_format_link> { { 8, 11, address } });

    // 서식 없이 넣으면 구간이 둘로 나뉜다. `none` 구간은 남지 않는다.
    const text_format split { luil::text::text_format_shifted(original, { 3, 0, 2 }, text_mark::none) };
    REQUIRE(split.runs == std::vector<luil::text::text_mark_run> { { 2, 3, bold }, { 5, 7, bold } });

    // 지운 범위는 잘라 낸다. 링크의 앞 조각이 지운 자리로 당겨진다.
    const text_format erased { luil::text::text_format_shifted(original, { 3, 4, 0 }, text_mark::none) };
    REQUIRE(erased.runs == std::vector<luil::text::text_mark_run> { { 2, 3, bold } });
    REQUIRE(erased.links == std::vector<luil::text::text_format_link> { { 3, 5, address } });

    // 대체는 지우고 넣는다. 넣은 글은 받은 서식이고 지운 자리 뒤의 구간은 넣은 글 뒤로 간다.
    const text_format replaced { luil::text::text_format_shifted(original, { 1, 3, 1 }, italic) };
    REQUIRE(replaced.runs == std::vector<luil::text::text_mark_run> { { 1, 2, italic }, { 2, 3, bold } });
    REQUIRE(replaced.links == std::vector<luil::text::text_format_link> { { 4, 7, address } });

    // 링크는 한가운데에 넣을 때만 늘어난다. 처음에 넣으면 밀리고 끝에 넣으면 그대로다.
    REQUIRE(luil::text::text_format_shifted(original, { 7, 0, 1 }, text_mark::none).links == std::vector<luil::text::text_format_link> { { 6, 10, address } });
    REQUIRE(luil::text::text_format_shifted(original, { 6, 0, 1 }, text_mark::none).links == std::vector<luil::text::text_format_link> { { 7, 10, address } });
    REQUIRE(luil::text::text_format_shifted(original, { 9, 0, 1 }, text_mark::none).links == std::vector<luil::text::text_format_link> { { 6, 9, address } });

    // 빈 변경은 그대로다.
    REQUIRE(luil::text::text_format_shifted(original, {}, bold) == original);
}

TEST_CASE("Turning a mark on and off merges neighbours, leaves no empty run and round trips", "[domain][text-format]")
{
    // 사이를 굵게 켜면 맞닿은 같은 서식이 하나로 합쳐진다.
    const text_format gapped { { { 0, 2, bold }, { 4, 6, bold } }, {} };
    REQUIRE(luil::text::text_format_with_mark(gapped, 2, 4, bold, true).runs == std::vector<luil::text::text_mark_run> { { 0, 6, bold } });
    // 모두 끄면 구간이 남지 않는다.
    REQUIRE(luil::text::text_format_with_mark(gapped, 0, 6, bold, false).runs.empty());

    // 다른 비트는 그대로 두고 그 비트만 바꾼다.
    const text_format layered { luil::text::text_format_with_mark({ { { 0, 4, bold } }, {} }, 2, 6, italic, true) };
    REQUIRE(layered.runs == std::vector<luil::text::text_mark_run> { { 0, 2, bold }, { 2, 4, bold | italic }, { 4, 6, italic } });

    // 켜고 끄면 처음 값과 같다 (`==`).
    const text_format original { { { 1, 3, italic } }, { { 3, 5, address } } };
    const text_format on { luil::text::text_format_with_mark(original, 0, 4, bold, true) };
    REQUIRE(on != original);
    REQUIRE(luil::text::text_format_with_mark(on, 0, 4, bold, false) == original);

    // 링크를 걸고 걷으면 처음 값과 같다. 걸친 이웃 링크는 범위 밖 조각만 남는다.
    const text_format linked { luil::text::text_format_with_link(original, 4, 8, u8"mailto:a@example.com") };
    REQUIRE(linked.links == std::vector<luil::text::text_format_link> { { 3, 4, address }, { 4, 8, u8"mailto:a@example.com" } });
    REQUIRE(luil::text::text_format_with_link(luil::text::text_format_with_link(original, 6, 8, u8"x"), 6, 8, {}) == original);
    // 같은 주소의 맞닿은 링크는 합친다.
    REQUIRE(luil::text::text_format_with_link(original, 5, 7, address).links == std::vector<luil::text::text_format_link> { { 3, 7, address } });
}

TEST_CASE("Marks are read from the character in front of an offset and cover only a whole range", "[domain][text-format]")
{
    const text_format format { { { 2, 5, bold }, { 5, 6, bold | code } }, {} };
    REQUIRE(luil::text::text_marks_at(format, 0) == text_mark::none);
    REQUIRE(luil::text::text_marks_at(format, 2) == text_mark::none);
    REQUIRE(luil::text::text_marks_at(format, 3) == bold);
    REQUIRE(luil::text::text_marks_at(format, 6) == (bold | code));
    REQUIRE(luil::text::text_marks_at(format, 7) == text_mark::none);

    REQUIRE(luil::text::text_marks_cover(format, 2, 6, bold));
    REQUIRE(luil::text::text_marks_cover(format, 1, 6, bold) == false);
    REQUIRE(luil::text::text_marks_cover(format, 2, 6, code) == false);
    REQUIRE(luil::text::text_marks_cover(format, 5, 6, code));
    REQUIRE(luil::text::text_marks_cover(format, 3, 3, bold) == false);
    REQUIRE(luil::text::has_mark(bold | italic, bold));
    REQUIRE(luil::text::has_mark(bold, bold | italic) == false);
}

TEST_CASE("A slice counts from zero and the byte size counts runs and link addresses", "[domain][text-format]")
{
    const text_format format { { { 2, 5, bold }, { 7, 9, italic } }, { { 4, 8, u8"x" } } };
    const text_format slice { luil::text::text_format_slice(format, 3, 8) };
    REQUIRE(slice.runs == std::vector<luil::text::text_mark_run> { { 0, 2, bold }, { 4, 5, italic } });
    REQUIRE(slice.links == std::vector<luil::text::text_format_link> { { 1, 5, u8"x" } });
    REQUIRE(luil::text::text_format_slice(format, 5, 5) == text_format {});

    REQUIRE(luil::text::text_format_bytes(text_format {}) == sizeof(text_format));
    REQUIRE(luil::text::text_format_bytes(format) == sizeof(text_format) + 2 * sizeof(luil::text::text_mark_run) + sizeof(luil::text::text_format_link) + 1);
}

TEST_CASE("Inserted text takes the typing marks, the first selected character, the previous character or the next one at a line start", "[domain][text-format]")
{
    // 앞 글자를 이어받는다. 굵은 낱말 끝에 이어 쓰면 굵다.
    luil::text::text_edit_state state { formatted(u8"abcd", { { { 0, 2, bold } }, {} }, 2) };
    luil::text::text_edit_insert(state, u8"x");
    REQUIRE(format_of(state).runs == std::vector<luil::text::text_mark_run> { { 0, 3, bold } });

    // 다음 글자 서식이 있으면 그것을 받고, 넣은 뒤 비운다. 그 뒤의 글은 앞 글자를 이어받아 같은 서식이다.
    state.typing_marks = italic;
    luil::text::text_edit_insert(state, u8"y");
    REQUIRE(state.typing_marks.has_value() == false);
    luil::text::text_edit_insert(state, u8"z");
    REQUIRE(state.text == u8"abxyzcd");
    REQUIRE(format_of(state).runs == std::vector<luil::text::text_mark_run> { { 0, 3, bold }, { 3, 5, italic } });

    // 선택을 대체하면 고른 첫 글자의 서식이다.
    luil::text::text_edit_place_caret(state, 4, false);
    luil::text::text_edit_place_caret(state, 1, true);
    luil::text::text_edit_insert(state, u8"Q");
    REQUIRE(state.text == u8"aQzcd");
    REQUIRE(format_of(state).runs == std::vector<luil::text::text_mark_run> { { 0, 2, bold }, { 2, 3, italic } });

    // 줄 처음과 글 처음에서는 뒤 글자의 서식이다.
    luil::text::text_edit_state lines { formatted(u8"ab\ncd", { { { 3, 5, underline } }, {} }, 3) };
    luil::text::text_edit_insert(lines, u8"x");
    REQUIRE(format_of(lines).runs == std::vector<luil::text::text_mark_run> { { 3, 6, underline } });
    luil::text::text_edit_state start { formatted(u8"cd", { { { 0, 2, code } }, {} }, 0) };
    REQUIRE(luil::text::text_edit_typing_marks(start) == code);
    luil::text::text_edit_insert(start, u8"x");
    REQUIRE(format_of(start).runs == std::vector<luil::text::text_mark_run> { { 0, 3, code } });
}

TEST_CASE("Text typed after a link is not part of it while text typed inside it is", "[domain][text-format]")
{
    luil::text::text_edit_state state { formatted(u8"go here", { {}, { { 3, 7, address } } }, 7) };
    luil::text::text_edit_insert(state, u8"!");
    REQUIRE(format_of(state).links == std::vector<luil::text::text_format_link> { { 3, 7, address } });
    luil::text::text_edit_place_caret(state, 5, false);
    luil::text::text_edit_insert(state, u8"X");
    REQUIRE(state.text == u8"go heXre!");
    REQUIRE(format_of(state).links == std::vector<luil::text::text_format_link> { { 3, 8, address } });
    // 링크를 모두 지우면 링크가 남지 않는다.
    luil::text::text_edit_place_caret(state, 3, false);
    luil::text::text_edit_place_caret(state, 8, true);
    REQUIRE(luil::text::text_edit_erase_selection(state));
    REQUIRE(format_of(state).links.empty());
}

TEST_CASE("Run boundaries widen to grapheme boundaries around an edit", "[domain][text-format]")
{
    // 굵은 `e` 뒤에 서식 없이 친 결합 부호는 글자소 하나라 첫 바이트의 서식(굵게)을 따른다.
    luil::text::text_edit_state accent { formatted(u8"e", { { { 0, 1, bold } }, {} }, 1) };
    accent.typing_marks = text_mark::none;
    luil::text::text_edit_insert(accent, u8"́");
    REQUIRE(accent.text == u8"é");
    REQUIRE(format_of(accent).runs == std::vector<luil::text::text_mark_run> { { 0, 3, bold } });

    // 보통 `a` 뒤에 굵게 친 결합 부호는 빈 구간이 되어 버린다.
    luil::text::text_edit_state plain { formatted(u8"a", {}, 1) };
    plain.typing_marks = bold;
    luil::text::text_edit_insert(plain, u8"́");
    REQUIRE(format_of(plain).runs.empty());

    // 밖에서 불러온 서식도 글자소 경계로 다듬고 글 크기로 자른다.
    luil::text::text_edit_state loaded {};
    luil::text::text_edit_set_formatted_text(loaded, u8"éx", std::make_shared<const text_format>(text_format { { { 1, 9, italic } }, {} }));
    REQUIRE(format_of(loaded).runs == std::vector<luil::text::text_mark_run> { { 3, 4, italic } });
}

TEST_CASE("A formatted insertion keeps its own marks and cuts a surrounding link", "[domain][text-format]")
{
    luil::text::text_edit_state state { formatted(u8"abcd", { { { 0, 4, italic } }, { { 0, 4, address } } }, 2) };
    luil::text::text_edit_insert_formatted(state, u8"XY", { { { 0, 1, bold } }, { { 1, 2, u8"mailto:b@example.com" } } });
    REQUIRE(state.text == u8"abXYcd");
    REQUIRE(state.caret == 4u);
    REQUIRE(format_of(state).runs == std::vector<luil::text::text_mark_run> { { 0, 2, italic }, { 2, 3, bold }, { 4, 6, italic } });
    REQUIRE(format_of(state).links == std::vector<luil::text::text_format_link> { { 0, 2, address }, { 3, 4, u8"mailto:b@example.com" }, { 4, 6, address } });

    // 한 걸음으로 되돌아간다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"abcd");
    REQUIRE(format_of(state).runs == std::vector<luil::text::text_mark_run> { { 0, 4, italic } });

    // 서식 없는 칸에 서식 없는 조각을 넣으면 서식이 생기지 않는다.
    luil::text::text_edit_state bare {};
    luil::text::text_edit_insert_formatted(bare, u8"plain", {});
    REQUIRE(bare.text == u8"plain");
    REQUIRE(bare.format == nullptr);
}

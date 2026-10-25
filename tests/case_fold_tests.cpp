#include "luil/text/case_fold.h"

#include "luil/ui/virtual_list_element.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

TEST_CASE("ASCII and Latin letters fold to their lower case", "[text][case-fold]")
{
    REQUIRE(luil::text::fold_case(U'D') == U'd');
    REQUIRE(luil::text::fold_case(U'd') == U'd');
    REQUIRE(luil::text::fold_case(U'7') == U'7');
    REQUIRE(luil::text::fold_case(U'É') == U'é');
    REQUIRE(luil::text::fold_case(U'Ø') == U'ø');
    // ×(U+00D7)는 글자가 아니라 짝이 없다.
    REQUIRE(luil::text::fold_case(U'×') == U'×');
    // 라틴 확장 A: 짝의 차례가 범위마다 다르다.
    REQUIRE(luil::text::fold_case(U'Ā') == U'ā');
    REQUIRE(luil::text::fold_case(U'Ł') == U'ł');
    REQUIRE(luil::text::fold_case(U'Ž') == U'ž');
    REQUIRE(luil::text::fold_case(U'Ÿ') == U'ÿ');
    // 단순 접기라 ß는 그대로이고 ſ는 s다.
    REQUIRE(luil::text::fold_case(U'ß') == U'ß');
    REQUIRE(luil::text::fold_case(U'ſ') == U's');
    REQUIRE(luil::text::fold_case(U'ẞ') == U'ß');
    // 터키어 İ는 로캘에 매인 접기라 하지 않는다.
    REQUIRE(luil::text::fold_case(U'İ') == U'İ');
    // 라틴 확장 B·추가 확장이다.
    REQUIRE(luil::text::fold_case(U'Ǆ') == U'ǆ');
    REQUIRE(luil::text::fold_case(U'ǅ') == U'ǆ');
    REQUIRE(luil::text::fold_case(U'Ạ') == U'ạ');
}

TEST_CASE("Greek, Cyrillic and full-width letters fold within their script", "[text][case-fold]")
{
    REQUIRE(luil::text::fold_case(U'Σ') == U'σ');
    REQUIRE(luil::text::fold_case(U'ς') == U'σ');
    REQUIRE(luil::text::fold_case(U'Ά') == U'ά');
    REQUIRE(luil::text::fold_case(U'Д') == U'д');
    REQUIRE(luil::text::fold_case(U'Ё') == U'ё');
    REQUIRE(luil::text::fold_case(U'Ѣ') == U'ѣ');
    // 전각 대문자는 전각 소문자다 — 반각 a로 접지 않는다.
    REQUIRE(luil::text::fold_case(U'Ｄ') == U'ｄ');
    REQUIRE(luil::text::fold_case(U'ｄ') == U'ｄ');
    // 기호의 겹친 글자다.
    REQUIRE(luil::text::fold_case(U'K') == U'k');
    REQUIRE(luil::text::fold_case(U'Ω') == U'ω');
}

TEST_CASE("Hangul, Han and kana have no case and stay as they are", "[text][case-fold]")
{
    for (const char32_t codepoint : { U'한', U'글', U'ㄱ', U'漢', U'か', U'カ', char32_t { 0x1100 }, char32_t { 0x1161 }, char32_t { 0x11A8 } })
        REQUIRE(luil::text::fold_case(codepoint) == codepoint);
    // 조합형(NFD) 한글 글도 바이트 하나 바뀌지 않는다.
    const std::u8string decomposed { u8"한글" };
    REQUIRE(luil::text::fold_case(decomposed) == decomposed);
}

TEST_CASE("Folded prefix and search match regardless of case", "[text][case-fold]")
{
    REQUIRE(luil::text::starts_with_folded(u8"Documents", u8"d"));
    REQUIRE(luil::text::starts_with_folded(u8"Documents", u8"DOC"));
    REQUIRE(luil::text::starts_with_folded(u8"Éclair", u8"éc"));
    REQUIRE(luil::text::starts_with_folded(u8"Ｄｏｃ", u8"ｄ"));
    REQUIRE(luil::text::starts_with_folded(u8"한글 문서", u8"한글"));
    REQUIRE(luil::text::starts_with_folded(u8"Doc", u8"Docs") == false);
    REQUIRE(luil::text::starts_with_folded(u8"Doc", u8""));

    // 찾은 자리는 원문 자리다. ſ(2 byte)가 s로 접혀도 원문 구간을 돌려준다.
    const std::optional<luil::text::folded_match> found { luil::text::find_folded(u8"Maſter Plan", u8"MASTER") };
    REQUIRE(found.has_value());
    REQUIRE(found->begin == 0u);
    REQUIRE(found->end == 7u);
    const std::optional<luil::text::folded_match> last { luil::text::find_folded(u8"aXbxc", u8"x", true) };
    REQUIRE(last.has_value());
    REQUIRE(last->begin == 3u);
    REQUIRE(luil::text::find_folded(u8"abc", u8"z").has_value() == false);
    // 잘못된 바이트는 그 바이트끼리만 같다.
    std::u8string broken { u8"A" };
    broken.push_back(char8_t { 0xFF });
    broken += u8"B";
    REQUIRE(luil::text::find_folded(broken, u8"b").has_value());
    std::u8string folded { u8"a" };
    folded.push_back(char8_t { 0xFF });
    folded += u8"b";
    REQUIRE(luil::text::fold_case(broken) == folded);
}

TEST_CASE("Virtual list type-ahead ignores case", "[text][case-fold][virtual_list]")
{
    // "d"를 쳐서 "Documents"로 간다.
    const std::vector<luil::virtual_list_item> items {
        luil::virtual_list_item { .key = u8"a", .label = u8"apps" },
        luil::virtual_list_item { .key = u8"d", .label = u8"Documents" },
        luil::virtual_list_item { .key = u8"e", .label = u8"Éditions" },
    };
    REQUIRE(luil::virtual_list_search_target(items, 0u, u8"d", true) == 1u);
    REQUIRE(luil::virtual_list_search_target(items, 0u, u8"é", true) == 2u);
    REQUIRE(luil::virtual_list_search_target(items, 0u, u8"DOC", false) == 1u);
}

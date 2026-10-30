#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace luil::text {
    // 로캘과 상관없는 **단순** 대소문자 접기다 (Unicode CaseFolding.txt의 C·S 줄 — case-fold-design.md).
    // 글자 찾기·포함 검색에서 "d"로 "Documents"를 찾을 때 쓴다.
    //  - 한 코드포인트가 한 코드포인트로 접힌다. ß를 "ss"로 펴는 완전 접기(F)와 터키어 i(T)는 하지 않는다 —
    //    길이가 변하면 찾은 자리를 원문 자리로 되짚을 수 없다.
    //  - 다루는 범위: ASCII, 라틴-1, 라틴 확장 A, 라틴 확장 B의 짝 범위, 라틴 추가 확장, 그리스, 키릴, 아르메니아,
    //    로마 숫자, 원 문자, 전각 라틴, 켈빈·옹스트롬·옴 기호. 한글·한자·가나는 대소문자가 없어 그대로다.
    //  - 전각 대문자는 전각 소문자로 접는다. 전각과 반각을 같게 보는 것은 대소문자가 아니라 폭의 접기다.
    [[nodiscard]] char32_t fold_case(char32_t codepoint) noexcept;
    // 글 전체를 접은 사본이다. 잘못된 UTF-8은 그 자리의 바이트를 그대로 둔다.
    [[nodiscard]] std::u8string fold_case(std::u8string_view text);

    // 접어서 보면 `text`가 `prefix`로 시작하는가.
    [[nodiscard]] bool starts_with_folded(std::u8string_view text, std::u8string_view prefix) noexcept;

    // 접어서 본 `needle`이 `text`에서 처음(`backward`면 마지막) 나오는 원문 자리다 (UTF-8 byte 구간).
    //  - 자리는 원문의 코드포인트 경계다. 접기가 바이트 길이를 바꿔도(ſ → s) 원문 구간을 돌려준다.
    //  - 빈 `needle`은 맨 앞(뒤로 찾으면 맨 끝)의 빈 구간이다.
    struct folded_match
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
    };
    [[nodiscard]] std::optional<folded_match> find_folded(std::u8string_view text, std::u8string_view needle, bool backward = false) noexcept;
}

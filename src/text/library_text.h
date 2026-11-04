#pragma once

#include "luil/generated/library_text.h"
#include "luil/text/string_table.h"

namespace luil {
    // 라이브러리 기본 문구의 표(`library_text::table`, 원본 assets/strings)에서 고른 언어의 조회다 (docs/i18n-design.md).
    //  - `set_text_languages`가 고른 언어 차례(원자 값)를 읽을 뿐이라 `noexcept` 함수에서 부른다. 잠금이나 할당이 없다.
    //  - 값 하나라 쥐고 있지 않는다. 언어가 바뀌면 다음에 부를 때 새 언어다.
    [[nodiscard]] string_lookup library_strings() noexcept;

    // 언어 상태를 "정한 적 없음"으로 돌린다. 공개 함수로는 그 상태로 돌아갈 수 없어 test의 되돌림 guard가 쓴다.
    void reset_text_languages() noexcept;

    // 표 고르기와 강조색 이름 고르기가 같은 규칙을 쓰도록 태그 견주기를 여기에 둔다.
    // 같은 언어 태그인가다. 대소문자와 `_`·`-`를 가리지 않는다.
    [[nodiscard]] bool same_language_tag(std::u8string_view left, std::u8string_view right) noexcept;
    // 끝 부분을 하나 뗀 태그다 (`zh-Hant-TW`면 `zh-Hant`). 뗄 부분이 없으면 빈 글이다.
    [[nodiscard]] std::u8string_view parent_language_tag(std::u8string_view tag) noexcept;
}

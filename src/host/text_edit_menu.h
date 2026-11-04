#pragma once

#include "luil/text/string_table.h"
#include "luil/ui/ui_tree.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 플랫폼 안의 텍스트 편집 메뉴다. 동작은 host가 실행하고 텍스트 초점은 옮기지 않는다.
    //  - caret이 링크 안이면(`text_input_snapshot::link`) "링크 열기"(`open-link`)·"주소 복사"(`copy-link`) 줄이 더 선다.
    //    터치에는 Ctrl이 없어 편집 칸의 링크를 여는 길이 이것이다.
    //  - 단추 글은 `strings`(라이브러리 표의 조회)에서 찾는다.
    [[nodiscard]] bool text_edit_menu_has_link(const text_input_snapshot& value) noexcept;
    [[nodiscard]] float text_edit_menu_height(bool link = false) noexcept;
    [[nodiscard]] std::shared_ptr<const ui_tree> make_text_edit_menu(const text_input_snapshot& value, const string_lookup& strings, float width, float height, float scale);

    // 편집 메뉴가 따를 선호 목록이다 (docs/i18n-adoption-design.md 결정 1).
    //  - 앱이 `set_text_languages`로 정했으면 그 목록이다(빈 목록도 정한 것이다). 편집 메뉴와 나머지 글이 한 언어가 된다.
    //  - 정하지 않았으면 OS 태그와 그 뒤의 `en`이다. 기본 문구가 OS를 읽지 않는다는 규칙의 예외이고, 끝의 `en`은 표에 없는 언어의
    //    OS에서 한국어가 아니라 영어로 물러서게 한다.
    [[nodiscard]] std::vector<std::u8string> text_edit_menu_languages(const std::optional<std::vector<std::u8string>>& chosen, std::u8string_view os_tag);
    // 앱이 정한 목록(`text_languages`)과 호스트가 아는 OS 태그로 라이브러리 표에서 고른 조회다.
    //  - 라이브러리 언어(`library_strings`)를 그대로 쓰면 언어를 정하지 않은 앱의 영어 폰에서 편집 메뉴가 한국어가 된다.
    [[nodiscard]] string_lookup text_edit_menu_strings(std::u8string_view os_tag);
}

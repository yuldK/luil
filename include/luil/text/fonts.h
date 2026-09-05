#pragma once

#include <string>
#include <vector>

namespace luil {
    // 설치된 글꼴 가족을 UTF-8 이름 순으로 돌려준다. 중복은 없고 조회 실패 시 빈 목록이다.
    [[nodiscard]] std::vector<std::u8string> installed_font_families();

    // 기본 UI 글꼴을 앞에서부터 찾는다. 빈 목록이나 모두 없는 이름이면 Segoe UI다.
    // 앱 시작 전에 지정한다. 이미 생성된 화면과 글꼴 객체는 자동으로 갱신하지 않는다.
    void set_ui_typeface_families(std::vector<std::u8string> families);
} // namespace luil

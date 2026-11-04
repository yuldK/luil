#pragma once

// mobile demo 첫 화면의 페이지 목록이다: 좁은 화면의 첫 장이자 넓은 화면의 왼쪽 판이다.
//  - 칸 하나는 아이콘, 제목, 설명이다. 설명은 폭에 들어가면 한 줄이고 넘치면 줄을 바꾼다. 칸을 누르면 그 페이지로 간다 (`navigate_intent`).
//  - 칸은 단추로 읽히고 이름은 페이지 제목이다. 보조 기술이 이름으로 칸을 찾아 Invoke로 페이지를 연다.
//  - 앱 셸(`app.cpp`)에서 떼어 둔 것은 page test가 칸의 접근 이름과 실행을 함께 보게 하려는 것이다.

#include "mobile_demo/common.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace mobile_demo {
    struct page_entry
    {
        std::u8string_view key {};
        std::u8string_view title {};
        std::u8string_view summary {};
        char32_t icon { 0 };
    };

    // 목록에 놓이는 순서의 모든 페이지다.
    [[nodiscard]] std::span<const page_entry> page_entries() noexcept;

    // 페이지 키의 제목이다. 목록에 없는 키는 앱 이름이다.
    [[nodiscard]] std::u8string page_title(std::u8string_view page);

    // 목록 한 칸과 그 높이다: 아이콘, 제목, 설명. `selected`이면 목록의 고른 행과 같은 바탕이고, `chevron`이면 누르면 다음 장으로
    // 넘어간다는 표시를 둔다. 폭은 논리 픽셀이고 설명이 넘치면 그 줄만큼 칸이 커진다.
    [[nodiscard]] page_content make_page_entry(const page_entry& entry, bool selected, bool chevron, float width, float scale, const luil::text_measurer* measurer);

    // 칸을 모두 쌓은 목록이다. `selected`는 고른 페이지 키이고 비면 고른 칸이 없다. 폭은 논리 픽셀이다.
    //  - `measurer`는 글 폭을 재는 쪽이다. 비면 설치 글꼴로 잰다. 글꼴에 기대지 않는 test가 고정 폭 측정기를 넣는다.
    [[nodiscard]] page_content make_page_list(float width, std::u8string_view selected, bool chevron, float scale = 1.0f, const luil::text_measurer* measurer = nullptr);
}

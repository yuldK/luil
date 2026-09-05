#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace luil {
    // 한 줄 텍스트 박스의 표시 상태다.
    // caret과 선택 anchor는 UTF-8 byte offset이고 둘이 같으면 선택이 없다.
    // element는 이 값으로 선택 띠와 caret 위치를 그린다.
    // 값은 앱의 view 모델이 만들어 tree 빌드에 넘긴다.
    struct text_input_view
    {
        std::u8string text {};
        std::size_t caret { 0 };
        std::size_t anchor { 0 };

        // IME 조합 중이다.
        // 조합 글은 아직 사용자의 글이 아니므로 `text`에 넣지 않는다.
        //  - 실행 취소 기록이 조합 단계마다 더럽혀진다.
        //    대신 그 동안 **화면에 보이는 글 전체**를 따로 싣고 element가 그것을 그린다.
        bool composing { false };
        std::u8string composition_text {};
        std::size_t composition_caret { 0 };
        // `composition_text` 안에서 밑줄을 긋는 구간이다 (UTF-8 byte offset).
        std::size_t composing_begin { 0 };
        std::size_t composing_end { 0 };

        // 지금 화면에 보이는 글이다 (조합 중이면 조합 글이 섞인 쪽).
        // element가 그리는 글과 같은 것이라, 점진 검색처럼 **보이는 대로**
        // 걸러야 하는 앱이 이것을 본다 — `text`를 보면 조합이 끝나기 전까지
        // 화면의 글과 결과가 어긋난다.
        [[nodiscard]] std::u8string_view displayed_text() const noexcept
        {
            return composing ? std::u8string_view { composition_text } : std::u8string_view { text };
        }

        [[nodiscard]] bool operator==(const text_input_view&) const = default;
    };
} // namespace luil

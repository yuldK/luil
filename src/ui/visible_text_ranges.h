#pragma once

#include "luil/ui/ui_element.h"

#include <cstddef>
#include <vector>

namespace luil {
    class text_input_element;
    class text_area_element;

    // 기본 편집기가 그리는 문서의 UTF-8 바이트 구간이다. clip은 조상과 표면이 자른 트리 좌표다.
    struct visible_text_ranges
    {
        struct span
        {
            std::size_t begin {};
            std::size_t end {};
        };

        [[nodiscard]] static std::vector<span> of(const text_input_element& field, const rect_f& clip);
        [[nodiscard]] static std::vector<span> of(const text_area_element& field, const rect_f& clip);
    };
}

#pragma once

#include <vector>

namespace luil {
    // 논리 글자 크기를 시스템 글자 크기로 바꾸고 화면 배율을 곱한다.
    // Windows의 기본은 logical_size * display_scale다. 아이콘·여백에는 쓰지 않는다.
    [[nodiscard]] float scaled_font_size(float logical_size, float display_scale = 1.0f) noexcept;

    // 글과 고정 여백을 함께 담을 논리 높이다. 앱의 고정 높이 행을 만들 때 쓴다.
    [[nodiscard]] float text_line_height(float logical_size, float padding) noexcept;

    // 플랫폼이 읽은 변환 표다. sizes[n]은 n sp의 dp 크기다. 빈 표는 기본 배율이다.
    // Android의 비선형 글자 확대를 보존한다. 유한한 단조 증가 표만 받으며 어느 thread에서나 읽을 수 있다.
    [[nodiscard]] bool set_system_font_sizes(std::vector<float> sizes);
}

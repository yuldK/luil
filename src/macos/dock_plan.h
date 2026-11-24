#pragma once

#include "luil/app/taskbar.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Dock 타일과 Dock 메뉴의 AppKit을 모르는 계산이다 (dock_macos.mm이 쓴다). core test로 돈다.
namespace luil::macos {
    // 진행 막대의 색 갈래다. 실제 색은 그리는 쪽이 시스템 색으로 고른다 (강조색·빨강·노랑).
    enum class dock_bar_tint
    {
        accent,
        error,
        paused,
    };

    // 타일에 그릴 진행 막대다.
    struct dock_bar
    {
        bool visible { false };
        // 값을 모르는 진행이다. 막대 전체를 빗금으로 채운다.
        bool striped { false };
        dock_bar_tint tint { dock_bar_tint::accent };
        // 채운 몫이다 (0..1).
        float fraction { 0.0f };

        [[nodiscard]] bool operator==(const dock_bar&) const = default;
    };

    // 셸에 낸 상태와 단계 수(`taskbar_progress_total` 중)를 막대로 옮긴다. 값이 없는 상태는 단계를 보지 않는다.
    [[nodiscard]] dock_bar make_dock_bar(taskbar_progress_state state, std::uint32_t units) noexcept;

    // 타일 안의 사각형이다. 점 단위이고 원점은 왼쪽 아래다 (AppKit의 뒤집지 않은 view).
    struct dock_rect
    {
        float x { 0.0f };
        float y { 0.0f };
        float width { 0.0f };
        float height { 0.0f };

        [[nodiscard]] bool operator==(const dock_rect&) const = default;
    };

    struct dock_tile_layout
    {
        // 막대의 바탕이다. 막대가 없으면 비었다.
        dock_rect track {};
        // 오버레이를 맞춰 넣을 오른쪽 아래 정사각형이다. 막대가 있으면 그 위에 선다 (막대 끝을 가리지 않는다).
        dock_rect overlay {};
    };

    [[nodiscard]] dock_tile_layout layout_dock_tile(float width, float height, bool bar) noexcept;

    // `box` 안에 비율을 지켜 가운데에 맞춘다. 그림 크기가 0 이하면 빈 사각형이다.
    [[nodiscard]] dock_rect fit_dock_rect(const dock_rect& box, float width, float height) noexcept;

    // 작업 번호(0부터)를 붙인 진단 영문이다. Win32 점프 목록과 같은 꼴이다 ("Jump list task 2 has ...").
    [[nodiscard]] std::u8string dock_task_error(std::size_t index, std::u8string_view what);

    // 목록을 메뉴와 실행 인자로 옮길 수 없는 까닭이다. 옮길 수 있으면 비었다.
    //  - 제목이 비었거나 제목·설명·인자에 NUL이 있으면 규칙 밖이다 (argv에 NUL을 실을 수 없다).
    //  - UTF-8이 올바른지는 NSString으로 옮기는 쪽이 본다.
    [[nodiscard]] std::optional<std::u8string> dock_menu_problem(const taskbar_jump_list& list);
}

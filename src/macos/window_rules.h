#pragma once

#include "host/caption_layout.h"
#include "host/popup_reconcile.h"
#include "luil/app/app_host.h"

#include <optional>
#include <span>
#include <string_view>

// macOS 주 창·popup의 값 규칙이다. AppKit을 모르는 층이라 core test로 돈다 (macos_app.mm이 쓴다).
//  - 좌표는 macOS host의 화면 좌표 약속을 따른다: 주 화면 왼쪽 위 원점, 아래로 늘고, 점 × 주 화면 배율의 픽셀이다.
namespace luil::macos {
    // 저장해 둔 주 창 배치(`application_config::initial_placement`)를 받아도 되는가다 (Windows `resolve_initial_placement`와 같은 규칙).
    //  - 크기가 0 이하, 끝이 32-bit 화면 좌표를 넘는 값(`win32::placement_in_screen_range`), 최소 크기(픽셀)보다 작은 값, 어느 화면의
    //    사각형(`display_info::bounds`)에도 걸치지 않는 값은 거절한다. 받으면 그대로 돌려준다.
    [[nodiscard]] std::optional<window_placement> accepted_initial_placement(
        const window_placement& placement, int minimum_width, int minimum_height, std::span<const display_info> displays) noexcept;

    // 앱이 caption을 그리는 창(`application_config::borderless`)의 한 점이 무엇인가다 (Win32 `WM_NCHITTEST`의 자리).
    //  - 판정의 띠·창 단추 자리는 게시된 tree의 `caption_element`가 선 자리와 그 설정(치수·창 단추)이다 (`caption_band_in`).
    //  - tree에 `caption_element`가 없으면 어디도 caption이 아니다 (`caption_hit::client`). Windows는 맨 위 띠를 기본으로 두지만,
    //    macOS의 테두리 없는 창은 앱이 그린 머릿줄의 단추를 끌기로 빼앗지 않게 caption을 둔 창만 끈다.
    //  - 끌기 자리에 `caption_client` 표시를 단 element가 서 있으면 client다 (앱 단추·검색 칸).
    //  - `x`·`y`는 창 내용의 물리 픽셀, `scale`은 그 창의 배율이다.
    [[nodiscard]] caption_hit custom_caption_hit(const ui_tree* tree, float x, float y, int client_width, float scale) noexcept;

    // 제목 막대를 두 번 눌렀을 때 할 일이다 (시스템 설정 > 데스크탑 및 Dock > "윈도우 제목 막대를 이중 클릭하여").
    enum class title_double_click
    {
        zoom,
        minimize,
        none,
    };

    // 사용자 기본값 `AppleActionOnDoubleClick`의 값(`Maximize`·`Minimize`·`None`)이다. 비었거나 모르는 값이면 확대다
    // (설정을 한 번도 바꾸지 않은 계정은 값이 없다).
    [[nodiscard]] title_double_click parse_title_double_click(std::u8string_view value) noexcept;

    // caption 끌기 한 걸음의 창 자리다. 끌기를 시작한 창 사각형을 포인터가 움직인 만큼 옮긴다.
    [[nodiscard]] screen_rect dragged_window_bounds(const screen_rect& start, screen_point pressed, screen_point pointer) noexcept;
    // 앱이 고친 자리(`win32::window_delegate::on_window_moving`)를 창 크기 그대로 받는다. 왼쪽 위만 쓴다.
    [[nodiscard]] screen_rect moving_bounds_kept_size(const screen_rect& proposed, const screen_rect& adjusted) noexcept;

    // popup 하나가 화면에 서는 자리다 (화면 픽셀).
    struct popup_screen_frame
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        // 보이는 높이다. 내용이 작업 영역보다 길면 작업 영역 높이로 줄고 popup이 흘려 보인다.
        int height { 0 };
    };

    // popup을 앵커 기준 자리에서 화면 자리로 옮긴다 (Win32 `popup_screen_bounds`와 같은 규칙).
    //  - `origin`은 앵커 표면 내용의 왼쪽 위다. 앵커가 흘려 보이는 popup이면 부르는 쪽이 흘린 만큼 올려 준다.
    //  - `offset_x`·`offset_y`·`width`·`height`는 popup의 앵커 기준 자리와 크기다 (화면 픽셀).
    //  - `work`는 그 자리가 가장 많이 걸친 화면의 작업 영역(메뉴 막대·Dock을 뺀 곳)이다. 그 높이보다 긴 popup은 그 높이로 줄인다.
    //  - 앵커가 popup이면(하위 메뉴) 오른쪽 끝을 넘을 때 앵커의 왼쪽으로 뒤집는다. 그 뒤 작업 영역 안으로 민다.
    [[nodiscard]] popup_screen_frame place_popup_on_screen(
        screen_point origin, int offset_x, int offset_y, int width, int height, const screen_area& work, bool anchor_is_popup) noexcept;
}

#pragma once

#include "luil/ui/ui_element.h"

#include <functional>

namespace luil {
    // 닫힌 필드가 앵커 popup 하나를 여닫는 규약의 공통 부분이다 (docs/split-button-design.md의 "필드 popup 열림 규약").
    // 분할 버튼이 처음 따르고 날짜 칸·색 고르개·메뉴 막대가 이어 따른다.
    //  - popup의 자리를 정하는 순수 함수(`place_field_popup`)와, 닫혀 있을 때 popup을 여는 키 처리기다.
    //  - 열림은 앱 상태이고 popup은 `ui_frame::popups`에 실린 동안만 있다. 이 헤더는 상태를 들지 않는다.
    //  - 네 방향 자리 함수(`place_field_popup_at_side`)는 tree 안에 겹쳐 그리는 안내 풍선도 쓴다 (docs/teaching-tip-design.md).

    // popup을 필드의 어느 가장자리에 맞추는가.
    enum class field_popup_alignment
    {
        // popup의 왼쪽을 필드의 왼쪽에 맞춘다.
        start,
        // popup의 오른쪽을 필드의 오른쪽에 맞춘다.
        end,
    };

    struct field_popup_anchor
    {
        // 필드의 자리다. `ui_element::bounds()`와 같은 좌표(물리 픽셀)다.
        rect_f field {};
        // popup이 들어갈 영역이다. 보통 앵커 표면의 client 사각형(물리 픽셀)이다.
        //  - 작업 영역은 host만 안다. 그래서 이 영역 안에서만 놓고, 그 밖으로 걸치는 일은 없다.
        rect_f limit {};
        // 필드와 popup 사이의 간격이다 (논리 픽셀).
        float gap { 2.0f };
        field_popup_alignment alignment { field_popup_alignment::start };
    };

    struct field_popup_position
    {
        // `ui_popup::x`·`y`에 그대로 넣는 논리 픽셀이다.
        float x { 0.0f };
        float y { 0.0f };
        // 아래가 모자라 필드 위에 놓았는가. 앱이 뒤집혔는지 알아야 하는 쪽(꼬리가 있는 popup)이 읽는다.
        bool above { false };
    };

    // 필드 아래에 popup을 놓을 자리를 정한다. `size`는 popup의 논리 크기(`make_menu_popup`이 잰 폭·높이)다.
    //  - 기본은 필드 아래다. 아래가 `limit`을 넘고 위가 들어가면 위로 뒤집는다. 둘 다 들어가지 않으면 아래에 두고
    //    나머지는 host가 민다.
    //  - 가로는 `alignment`로 맞춘 뒤 `limit` 안으로 민다. popup이 `limit`보다 넓으면 왼쪽 가장자리에 맞춘다.
    //  - 배율이 유효하지 않으면(0 이하, 유한하지 않음) 1로 본다.
    [[nodiscard]] field_popup_position place_field_popup(const field_popup_anchor& anchor, size_f size, float scale) noexcept;

    // 필드를 여는 키 처리기다 (`ui_element::set_key_handler`). 필드의 처리기는 닫혀 있을 때만 일한다.
    //  - Alt+↓는 Alt만 눌렸을 때, F4는 수식 키 없이 `set_open(true)`를 낸다. `down_opens`가 참이면 수식 키 없는 ↓도
    //    연다. 화살표 단추처럼 ↓가 달리 쓰이지 않는 자리에 준다.
    //  - 이미 열려 있으면 그 키들을 빈 목록으로 삼켜 앱 단축키로 새지 않게 한다. 그 밖의 키는 `std::nullopt`다.
    //  - `set_open`이 비어 있으면 처리기도 비어 있다.
    [[nodiscard]] key_handler field_popup_key_handler(bool open, std::function<input_action(bool open)> set_open, bool down_opens);

    // 필드의 어느 변 바깥에 놓는가다. 왼쪽·오른쪽은 화면의 방향이고 읽는 방향과 무관하다.
    enum class field_popup_side
    {
        below,
        above,
        left,
        right,
    };

    struct field_popup_side_position
    {
        // `place_field_popup`과 같은 논리 픽셀이다.
        float x { 0.0f };
        float y { 0.0f };
        // 실제로 놓은 방향이다.
        field_popup_side side { field_popup_side::below };
        // 네 방향 모두 모자라 바라는 방향에 두고 `limit` 안으로 밀었으면 거짓이다. 그때는 필드를 덮을 수 있다.
        bool fits { true };
    };

    // 필드의 네 변 가운데 한 곳 바깥에 놓을 자리를 정한다. `size`는 논리 크기다 (docs/teaching-tip-design.md 결정 3).
    //  - 바라는 방향, 반대 방향, 남은 두 방향(아래·위면 오른쪽·왼쪽, 오른쪽·왼쪽이면 아래·위) 순으로 주 축이 `limit`에
    //    들어가는 첫 방향에 놓는다. 주 축은 아래·위면 세로, 왼쪽·오른쪽이면 가로다.
    //  - 다른 축은 필드 가운데에 맞춘 뒤 `limit` 안으로 민다. `limit`보다 크면 처음 가장자리(왼쪽·위)에 붙는다.
    //  - 어느 방향도 들어가지 않으면 바라는 방향에 두고 주 축도 `limit` 안으로 밀며 `fits`가 거짓이다.
    //  - `anchor.alignment`는 보지 않는다. 배율이 유효하지 않으면 1로 본다 (`place_field_popup`과 같다).
    [[nodiscard]] field_popup_side_position place_field_popup_at_side(const field_popup_anchor& anchor, size_f size, float scale, field_popup_side preferred) noexcept;
}

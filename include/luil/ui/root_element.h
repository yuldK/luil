#pragma once

#include "luil/ui/ui_element.h"

#include <memory>

namespace luil {
    // 창 전체를 담는 최상위 컨테이너다.
    // 배경은 렌더러가 팔레트의 창 배경색으로 이미 칠했으므로 자식만 그린다.
    // kind는 예약된 `ui_element_kind::root`이고, 표면이 여럿이면(주 창·보조 창)
    // owner로 구분한다.
    //
    // 자식은 호출자가 직접 arrange한 채로 담는다 — caption처럼 자기 높이를
    // 아는 element와 나머지를 채우는 stack을 겹치지 않게 쌓는 것은 tree를
    // 조립하는 쪽의 몫이다 (examples/demo_main.cpp가 본보기다).
    class root_element final : public ui_element
    {
    public:
        explicit root_element(std::u8string owner = {});

        void add(std::unique_ptr<ui_element> child);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    };
} // namespace luil

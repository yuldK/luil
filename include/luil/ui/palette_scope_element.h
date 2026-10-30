#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>

namespace luil {
    // 감싼 가지가 그릴 팔레트다 (palette-scope-design.md).
    struct palette_scope_config
    {
        // 바깥 팔레트와 그 테마를 받아 가지의 팔레트를 돌려준다. 비어 있으면 바깥 팔레트 그대로다.
        //  - 역할을 고쳐 쓰는 것이라 hover·눌림·초점·비활성 그림은 그대로 그 역할의 바뀐 색으로 선다.
        std::function<ui_color_palette(const ui_color_palette& palette, color_theme theme)> palette {};
        // 고대비에서도 바꾼다. 기본(거짓)은 고대비면 바꾸지 않는다 — 사용자가 OS에서 고른 시스템 색을 앱이 옅게
        // 하거나 바꿔 대비를 무너뜨리지 않는다.
        bool in_high_contrast { false };
    };

    // 감싼 가지(내용 하나)를 바뀐 팔레트로 그리는 컨테이너다.
    // 기본 컨트롤을 옅게(미리 보기·잠긴 판) 또는 다른 역할로(위험 구역) 그릴 때 컨트롤마다 색 선택자를 달지 않는다.
    //  - 내용은 slot 전체를 받는다. 자기는 아무것도 그리지 않는다.
    //  - 중첩하면 안쪽 함수가 바깥 함수가 바꾼 팔레트를 받는다.
    //  - 키보드 초점 테는 tree가 바깥 팔레트로 마지막에 긋는다. 가지 안에서도 초점이 늘 같은 색으로 보인다.
    //  - 고른 테마가 바뀌면 다음 frame에 바뀐 테마의 팔레트로 함수가 다시 불린다 (그리기마다 부른다).
    class palette_scope_element final : public ui_element
    {
    public:
        palette_scope_element(ui_element_id id, palette_scope_config config);

        void set_content(std::unique_ptr<ui_element> content);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        palette_scope_config config_ {};
        ui_element* content_ { nullptr };
    };
}

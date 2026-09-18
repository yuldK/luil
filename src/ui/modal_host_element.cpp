#include "luil/ui/modal_host_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/panel_element.h"

#include <utility>

namespace luil {
    modal_host_element::modal_host_element(modal_host_config config)
        : ui_element { ui_element_id { ui_element_kind::modal_host, config.owner } }
        , config_ { std::move(config) }
    {
        // 초점을 이 안에 가두고 Esc를 받는다.
        // 없는 dismiss는 세우지 않는다 — 빈 액션이 "Esc로는 닫지 않는다"다.
        set_focus_trap(true);
        if (config_.dismiss)
            set_dismiss_action(config_.dismiss);
        // 이름 지은 자리가 있으면 뜨는 순간 초점이 거기 서고, 사라지면 되돌아간다.
        // 빈 이름을 그대로 넘긴다 — 그것이 "자동 초점 없음"·"되돌리지 않음"이다.
        set_focus_entry(config_.focus_entry);
        set_focus_return(config_.focus_return);

        // scrim은 언제나 만든다.
        // 진하기 0은 "보이지 않게 막는다"이지 "막지 않는다"가 아니다 — 포인터를
        // 막는 것이 modal의 몫이라 여기서 그것을 뺄 수 없다.
        panel_config scrim {};
        // 구체 색이 아니라 역할이다. 알파는 그리는 쪽이 정한다는 그 역할의 규칙을 따른다.
        // 앱이 선택자를 주었으면 그것을 그대로 넘긴다 — 진하기는 이미 그 안에서 정해졌고,
        // 여기서 다시 곱하면 앱이 적은 색과 화면의 색이 갈린다.
        if (config_.scrim_background)
            scrim.background = config_.scrim_background;
        else
            scrim.background = [opacity = config_.scrim_opacity](const ui_color_palette& palette) { return with_alpha(palette.content_shadow, opacity); };
        auto panel { std::make_unique<panel_element>(ui_element_id { ui_element_kind::modal_scrim, config_.owner }, std::move(scrim)) };
        if (config_.outside)
            panel->set_action(ui_trigger::left_click, config_.outside);
        // 액션이 없어도 뒤로 새지 않게 흡수한다.
        panel->set_hit_opaque(true);
        // 누를 수 있어도 Tab의 자리는 아니다 — 화면을 덮는 판이라 테를 두를 자리가 없다.
        panel->set_tab_stop(false);
        scrim_ = panel.get();
        add_child(std::move(panel));
    }

    void modal_host_element::set_content(std::unique_ptr<ui_element> content)
    {
        // dialog의 빈 자리를 눌러도 뒤의 scrim으로 새지 않는다.
        // 앱이 손으로 흡수 액션을 다는 대신 host가 세운다 — 잊으면 바깥 클릭으로
        // 닫히는 dialog가 자기 몸을 눌러도 닫힌다.
        content->set_hit_opaque(true);
        if (config_.surface.has_value() == false)
        {
            content_ = content.get();
            add_child(std::move(content));
            return;
        }

        // 표면은 내용을 감싸기만 한다.
        // 흡수는 그대로 내용의 몫이다 — panel은 자기 slot을 내용에게 통째로 물려주어
        // 표면이 내용보다 넓어지는 자리가 없고, 흡수를 표면으로 옮기면 같은 규칙을
        // 두 자리에서 말하게 된다.
        //  - 앱이 부르는 이름이 아니라 host가 조립하는 안쪽 부품이라 자기 kind를 갖지
        //    않는다 (virtual_list가 행 안에 세우는 라벨과 같은 자리다). 이름으로 찾을
        //    일이 있으면 그것은 앱이 만든 내용이지 이 표면이 아니다.
        auto surface { std::make_unique<panel_element>(ui_element_id { ui_element_kind::none, config_.owner }, *config_.surface) };
        surface->set_content(std::move(content));
        content_ = surface.get();
        add_child(std::move(surface));
    }

    void modal_host_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        // scrim은 준 자리를 그대로 덮는다.
        scrim_->arrange(context);
        if (content_ == nullptr)
            return;

        // 가운데에 놓고 앱이 준 만큼 밀어낸다.
        // 표면이 있으면 이 자리를 표면이 받고 내용은 그 안에서 같은 자리를 물려받는다.
        const float width { config_.content_width * scale };
        const float height { config_.content_height * scale };
        const float left { context.slot.x + (context.slot.width - width) / 2.0f + config_.offset_x * scale };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f + config_.offset_y * scale };
        content_->arrange(context.for_child({ left, top, width, height }));
    }

    void modal_host_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // scrim이 먼저, 내용이 그 위다 (자식 순서 그대로).
        draw_children(context, interaction);
    }
    access_info modal_host_element::accessibility() const
    {
        // 이름은 앱이 준 말이다. 안에 담긴 캡션 글과 같은 말을 적는 자리라
        // 라이브러리가 지어낼 수 있는 것이 없다 (`modal_host_config::name`).
        return { .role = access_role::dialog, .name = config_.name };
    }
} // namespace luil

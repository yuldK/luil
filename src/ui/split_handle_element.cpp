#include "luil/ui/split_handle_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    namespace {
        // 드러났을 때 긋는 선의 두께다 (논리 픽셀).
        // 잡는 두께는 담는 쪽이 slot으로 정하므로 여기 있는 것은 보이는 두께뿐이다.
        constexpr float handle_line_width { 2.0f };
    } // namespace

    split_handle_element::split_handle_element(const ui_element_id id, split_handle_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        // 끌 수 없는 손잡이는 커서도 걸지 않는다 — 없는 것은 두지 않는다.
        if (config_.resize == nullptr)
            return;

        set_cursor(config_.axis == split_axis::horizontal ? ui_cursor::resize_horizontal : ui_cursor::resize_vertical);
        // 좌표 변화량만 메시지로 바꾸므로 tree가 다시 빌드되어도 끌기가 이어진다.
        pointer_drag_target target {};
        target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            const float moved { config_.axis == split_axis::horizontal ? current.x - previous.x : current.y - previous.y };
            const float delta { (config_.grows == split_grows::toward_end ? moved : -moved) / scale_ };
            if (delta == 0.0f)
                return {};
            return { config_.resize(delta) };
        };
        set_pointer_drag_target(std::move(target));

        set_tab_stop(true);
        key_step_target steps {};
        // 커서를 고르는 그 값이 화살표의 축도 고른다.
        steps.axis = config_.axis == split_axis::horizontal ? focus_axis::horizontal : focus_axis::vertical;
        steps.on_step = [this](const value_step step) -> std::optional<std::vector<input_action>> {
            float moved { 0.0f };
            switch (step)
            {
            case value_step::decrease:
                moved = -split_handle_key_step;
                break;
            case value_step::increase:
                moved = split_handle_key_step;
                break;
            case value_step::decrease_page:
                moved = -split_handle_key_page_step;
                break;
            case value_step::increase_page:
                moved = split_handle_key_page_step;
                break;
            case value_step::minimum:
            case value_step::maximum:
                // 손잡이는 길이를 담지 않아 "끝"이 어디인지 모른다.
                // 답할 값이 없으므로 그 키는 내 것이 아니다 — 그대로 흐른다.
                return std::nullopt;
            }
            // 끌기와 같은 부호 규약이다 — 앱은 언제나 "+ = 그 판이 넓어진다"만 안다.
            return std::vector<input_action> { config_.resize(config_.grows == split_grows::toward_end ? moved : -moved) };
        };
        set_key_step_target(std::move(steps));
    }

    void split_handle_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
    }

    void split_handle_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (interaction.hovered != id() && interaction.pressed != id())
            return;

        const rect_f box { bounds() };
        const float line { handle_line_width * scale_ };
        if (config_.axis == split_axis::horizontal)
            context.canvas.drawRect(SkRect::MakeXYWH(box.x + (box.width - line) / 2.0f, box.y, line, box.height), solid_paint(context.palette.accent));
        else
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + (box.height - line) / 2.0f, box.width, line), solid_paint(context.palette.accent));
    }
    access_info split_handle_element::accessibility() const
    {
        // 범위가 없다 — 손잡이는 자기 범위를 모른다. Home/End를 흘려보내는 것과
        // 같은 이유다 (value-step-design.md).
        return { .role = access_role::handle, .name = tooltip() };
    }
} // namespace luil

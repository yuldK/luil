#include "luil/ui/badge_element.h"

#include "ui/badge_paint.h"

#include <cmath>
#include <optional>
#include <utility>

namespace luil {
    badge_element::badge_element(ui_element_id id, badge_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void badge_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void badge_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        // 그림은 탭의 배지와 한 벌이다 (badge_paint.h).
        draw_badge_pill(context, bounds(), config_.text, config_.tone, config_.font_size);
    }
    access_info badge_element::accessibility() const
    {
        return { .role = access_role::static_text, .name = config_.text };
    }

    std::optional<size_f> badge_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        const float text_width { std::ceil(context.text_width(config_.text, config_.font_size)) };
        return size_f { badge_pill_width_for(text_width, config_.text.empty() == false, config_.tone, config_.font_size), height_for(config_) };
    }
} // namespace luil

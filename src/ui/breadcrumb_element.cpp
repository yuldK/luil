#include "luil/ui/breadcrumb_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 조각 글 양옆의 여백·꺾쇠 칸·접은 단추 칸·한 줄 높이다 (논리 픽셀).
        constexpr float item_padding { 6.0f };
        constexpr float separator_width { 16.0f };
        constexpr float overflow_width { 24.0f };
        constexpr float row_height { 24.0f };
        constexpr float glyph_size { 12.0f };

        [[nodiscard]] std::u8string child_owner(const std::u8string_view owner, const std::u8string_view key)
        {
            std::u8string result { owner };
            result += u8'/';
            result += key;
            return result;
        }

        // 조각 하나다. 글을 그리고 누름을 받는다 — 지금 자리는 누를 것이 없다.
        class breadcrumb_item final : public ui_element
        {
        public:
            breadcrumb_item(ui_element_id id, std::u8string label, std::u8string access_name, const float font_size, const bool current)
                : ui_element { std::move(id) }
                , label_ { std::move(label) }
                , access_name_ { std::move(access_name) }
                , font_size_ { font_size }
                , current_ { current }
            {
                // 지금 자리도 Tab·화살표로 서서 이름을 읽는다.
                set_tab_stop(true);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const rect_f box { bounds() };
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                if (current_ == false)
                    draw_hover_fill(context, box, id(), interaction, enabled(), 3.0f * scale);
                const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(font_size_, scale) };
                // 지금 자리는 진한 글, 지나온 조각은 흐린 글이다.
                const SkPaint paint { solid_paint(current_ ? context.palette.primary_foreground : context.palette.secondary_foreground) };
                const float padding { item_padding * scale };
                static_cast<void>(draw_text_within(context.canvas, label_, box.x + padding, box.y + centered_text_baseline(font, box.height), box.width - padding * 2.0f, font, paint));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::link, .name = access_name_ };
            }

        private:
            std::u8string label_ {};
            std::u8string access_name_ {};
            float font_size_ { 12.0f };
            bool current_ { false };
        };

        // 조각 뒤에 꺾쇠 칸을 두는가 — 아래 단계가 있거나 다음 조각이 있으면 둔다.
        [[nodiscard]] bool has_separator(const breadcrumb_config& config, const std::size_t index) noexcept
        {
            return config.segments[index].has_children || index + 1 < config.segments.size();
        }

        [[nodiscard]] float item_width(const breadcrumb_config& config, const measure_context& context, const std::size_t index)
        {
            return context.text_width(config.segments[index].label, config.font_size) + item_padding * 2.0f;
        }

        // 앞에서 `collapsed`개를 접었을 때의 폭이다 (논리 픽셀).
        [[nodiscard]] float total_width(const breadcrumb_config& config, const measure_context& context, const std::size_t collapsed)
        {
            float width { collapsed > 0 ? overflow_width + separator_width : 0.0f };
            for (std::size_t index { collapsed }; index < config.segments.size(); ++index)
                width += item_width(config, context, index) + (has_separator(config, index) ? separator_width : 0.0f);
            return width;
        }
    } // namespace

    ui_element_id breadcrumb_segment_id(const std::u8string_view owner, const std::u8string_view key)
    {
        return ui_element_id { ui_element_kind::breadcrumb_item, child_owner(owner, key) };
    }

    ui_element_id breadcrumb_chevron_id(const std::u8string_view owner, const std::u8string_view key)
    {
        return ui_element_id { ui_element_kind::breadcrumb_chevron, child_owner(owner, key) };
    }

    ui_element_id breadcrumb_collapsed_id(const std::u8string_view owner)
    {
        return ui_element_id { ui_element_kind::breadcrumb_overflow, std::u8string { owner } };
    }

    breadcrumb_element::breadcrumb_element(breadcrumb_config config)
        : ui_element { ui_element_id { ui_element_kind::breadcrumb, config.owner } }
        , config_ { std::move(config) }
    {
        // 막대 전체가 Tab의 한 자리이고 ←/→가 그 안을 돈다.
        set_focus_group(focus_axis::horizontal);

        // 접은 단추가 맨 앞이다 — 화면의 차례가 보조 기술의 차례다.
        button_config overflow_config {};
        overflow_config.glyph = codicons::icon_ellipsis;
        overflow_config.icon_size = glyph_size;
        auto overflow { std::make_unique<button_element>(breadcrumb_collapsed_id(config_.owner), std::move(overflow_config)) };
        overflow->set_tooltip(config_.collapsed_label);
        overflow->set_tab_stop(true);
        overflow->set_visible(false);
        overflow_ = overflow.get();
        add_child(std::move(overflow));

        for (std::size_t index { 0 }; index < config_.segments.size(); ++index)
        {
            const breadcrumb_segment& segment { config_.segments[index] };
            const bool current { index + 1 == config_.segments.size() };
            std::u8string name { segment.label };
            if (current && config_.current_label.empty() == false)
                name += u8" · " + config_.current_label;
            auto item { std::make_unique<breadcrumb_item>(breadcrumb_segment_id(config_.owner, segment.key), segment.label, std::move(name), config_.font_size, current) };
            if (current == false && config_.navigate != nullptr)
            {
                item->set_cursor(ui_cursor::hand);
                item->set_action(ui_trigger::left_click, [key = segment.key, navigate = config_.navigate](const ui_action_context&) -> std::vector<input_action> { return { navigate(key) }; });
            }
            if (config_.middle_click != nullptr)
                item->set_action(ui_trigger::middle_click, [key = segment.key, open = config_.middle_click](const ui_action_context&) -> std::vector<input_action> { return { open(key) }; });
            if (config_.context_menu != nullptr)
                item->set_action(
                    ui_trigger::right_click, [key = segment.key, menu = config_.context_menu](const ui_action_context& context) -> std::vector<input_action> { return { menu(key, context) }; });
            items_.push_back(item.get());
            add_child(std::move(item));

            if (segment.has_children && config_.open_children != nullptr)
            {
                button_config chevron_config {};
                chevron_config.glyph = codicons::icon_chevron_right;
                chevron_config.icon_size = glyph_size;
                auto chevron { std::make_unique<button_element>(breadcrumb_chevron_id(config_.owner, segment.key), std::move(chevron_config)) };
                chevron->set_tooltip(config_.children_label.empty() ? segment.label : segment.label + u8" · " + config_.children_label);
                chevron->set_tab_stop(true);
                chevron->set_action(
                    ui_trigger::left_click, [key = segment.key, open = config_.open_children](const ui_action_context& context) -> std::vector<input_action> { return { open(key, context) }; });
                chevrons_.push_back(chevron.get());
                add_child(std::move(chevron));
            }
            else
                chevrons_.push_back(nullptr);
        }

        if (config_.open_collapsed != nullptr)
            overflow_->set_action(ui_trigger::left_click, [this, open = config_.open_collapsed](const ui_action_context& context) -> std::vector<input_action> {
                std::vector<std::u8string> keys {};
                for (std::size_t index { 0 }; index < collapsed_ && index < config_.segments.size(); ++index)
                    keys.push_back(config_.segments[index].key);
                return { open(keys, context) };
            });
    }

    float breadcrumb_element::height_for(const breadcrumb_config&) noexcept
    {
        return row_height;
    }

    std::size_t breadcrumb_element::collapsed_count(const breadcrumb_config& config, const measure_context& context, const float available_width)
    {
        if (config.segments.size() <= 1)
            return 0;
        // 앞에서부터 하나씩 접어 들어가는 첫 수다. 지금 자리 하나만 남겨도 안 되면 그만큼 접고 그 글을 줄인다.
        for (std::size_t collapsed { 0 }; collapsed < config.segments.size() - 1; ++collapsed)
            if (total_width(config, context, collapsed) <= available_width)
                return collapsed;
        return config.segments.size() - 1;
    }

    std::size_t breadcrumb_element::collapsed() const noexcept
    {
        return collapsed_;
    }

    void breadcrumb_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const measure_context measure { context.measure() };
        collapsed_ = collapsed_count(config_, measure, context.slot.width / scale);

        const rect_f box { context.slot };
        float x { box.x };
        const float right { box.x + box.width };
        const auto place = [&](ui_element& child, const float width) {
            const float clipped { std::max(0.0f, std::min(width, right - x)) };
            child.arrange({ { x, box.y, clipped, box.height }, scale });
            x += clipped;
        };
        overflow_->set_visible(collapsed_ > 0);
        if (collapsed_ > 0)
        {
            place(*overflow_, overflow_width * scale);
            // 접은 단추 뒤의 꺾쇠는 그리기만 하는 칸이다.
            x += separator_width * scale;
        }
        for (std::size_t index { 0 }; index < items_.size(); ++index)
        {
            const bool shown { index >= collapsed_ };
            items_[index]->set_visible(shown);
            if (chevrons_[index] != nullptr)
                chevrons_[index]->set_visible(shown);
            if (shown == false)
                continue;
            place(*items_[index], item_width(config_, measure, index) * scale);
            if (has_separator(config_, index))
            {
                if (chevrons_[index] != nullptr)
                    place(*chevrons_[index], separator_width * scale);
                else
                    x += separator_width * scale;
            }
        }
    }

    void breadcrumb_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 누를 것이 없는 조각 사이의 꺾쇠는 막대가 흐린 글리프로 긋는다 (단추가 아니다).
        if (context.codicon_typeface != nullptr)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const SkFont font { sk_ref_sp(context.codicon_typeface), glyph_size * scale };
            const SkPaint paint { solid_paint(context.palette.secondary_foreground) };
            const rect_f box { bounds() };
            const auto separator_after = [&](const rect_f& before) {
                const rect_f slot { before.x + before.width, box.y, separator_width * scale, box.height };
                if (slot.x + slot.width <= box.x + box.width)
                    draw_centered_glyph(context.canvas, codicons::icon_chevron_right, slot, font, paint);
            };
            if (collapsed_ > 0)
                separator_after(overflow_->bounds());
            for (std::size_t index { collapsed_ }; index < items_.size(); ++index)
                if (chevrons_[index] == nullptr && has_separator(config_, index))
                    separator_after(items_[index]->bounds());
        }
        draw_children(context, interaction);
    }

    access_info breadcrumb_element::accessibility() const
    {
        return { .role = access_role::navigation, .name = config_.name };
    }

    std::optional<size_f> breadcrumb_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        return size_f { total_width(config_, context, 0), row_height };
    }
} // namespace luil

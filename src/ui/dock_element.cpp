#include "luil/ui/dock_element.h"

#include "luil/ui/draw_primitives.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    struct dock_element::geometry
    {
        rect_f bounds {};
        float scale { 1.0f };
    };

    namespace {
        // 배치와 메시지만 조립한다. 탭 끌기와 손잡이 입력은 기존 요소가 맡는다.
        class dock_branch final : public ui_element
        {
        public:
            dock_branch(const dock_node& node, const dock_config& config, std::unique_ptr<ui_element> first, std::unique_ptr<ui_element> second)
                : ui_element { { ui_element_kind::dock_split, config.owner + u8"/" + node.key } }
                , axis_ { node.axis }
                , ratio_ { std::clamp(node.ratio, 0.05f, 0.95f) }
                , width_ { config.handle_width }
                , minimum_ { config.minimum_pane }
            {
                add_child(std::move(first));
                split_handle_config handle {};
                handle.axis = node.axis;
                handle.resting_divider = true;
                if (config.change != nullptr)
                    handle.resize = [this, emit = config.change, key = node.key](const float delta) {
                        return emit({ .operation = dock_operation::resize, .target = key, .delta = extent_ > 0.0f ? delta / extent_ : 0.0f });
                    };
                add_child(std::make_unique<split_handle_element>(ui_element_id { ui_element_kind::dock_drop, config.owner + u8"/" + node.key }, std::move(handle)));
                add_child(std::move(second));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const bool horizontal { axis_ == split_axis::horizontal };
                const float length { std::max(0.0f, horizontal ? context.slot.width : context.slot.height) };
                const float handle { std::clamp(width_ * scale, 0.0f, length) };
                const float available { length - handle };
                const float minimum { std::clamp(minimum_ * scale, 0.0f, available / 2.0f) };
                const float first { std::clamp(available * ratio_, minimum, available - minimum) };
                extent_ = available / scale;
                float start { horizontal ? context.slot.x : context.slot.y };
                const float lengths[] { first, handle, available - first };
                for (std::size_t index { 0 }; index < 3; ++index)
                {
                    auto slot { context.slot };
                    if (horizontal)
                    {
                        slot.x = start;
                        slot.width = lengths[index];
                    }
                    else
                    {
                        slot.y = start;
                        slot.height = lengths[index];
                    }
                    children()[index]->arrange(context.for_child(slot));
                    start += lengths[index];
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

        private:
            split_axis axis_ {};
            float ratio_ { 0.5f };
            float width_ { 6.0f };
            float minimum_ { 80.0f };
            float extent_ { 0.0f };
        };

        dock_placement placement_at(const rect_f& box, const float x, const float y)
        {
            const float dx { box.width > 0.0f ? (x - box.x) / box.width : 0.5f };
            const float dy { box.height > 0.0f ? (y - box.y) / box.height : 0.5f };
            if (std::min(dx, 1.0f - dx) < std::min(dy, 1.0f - dy))
                return dx < 0.25f ? dock_placement::left : dx > 0.75f ? dock_placement::right : dock_placement::center;
            return dy < 0.25f ? dock_placement::top : dy > 0.75f ? dock_placement::bottom : dock_placement::center;
        }

        class dock_group_element final : public ui_element
        {
        public:
            dock_group_element(ui_element_id id, std::unique_ptr<ui_element> tabs, std::unique_ptr<ui_element> content, const float tab_height)
                : ui_element { std::move(id) }, tab_height_ { tab_height }
            {
                set_clip_children(true);
                set_hit_opaque(true);
                add_child(std::move(tabs));
                if (content != nullptr)
                    add_child(std::move(content));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float height { std::clamp(tab_height_ * context.scale, 0.0f, std::max(0.0f, context.slot.height)) };
                children()[0]->arrange(context.for_child({ context.slot.x, context.slot.y, context.slot.width, height }));
                if (children().size() > 1)
                    children()[1]->arrange(context.for_child({ context.slot.x, context.slot.y + height, context.slot.width, context.slot.height - height }));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const auto box { bounds() };
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
                draw_children(context, interaction);
                if (interaction.drag.has_value() && interaction.drag->hovered_drop_target == id())
                {
                    auto preview { box };
                    switch (placement_at(box, interaction.drag->x, interaction.drag->y))
                    {
                    case dock_placement::left: preview.width *= 0.5f; break;
                    case dock_placement::right: preview.x += preview.width * 0.5f; preview.width *= 0.5f; break;
                    case dock_placement::top: preview.height *= 0.5f; break;
                    case dock_placement::bottom: preview.y += preview.height * 0.5f; preview.height *= 0.5f; break;
                    case dock_placement::center: break;
                    }
                    SkPaint paint { solid_paint(context.palette.accent) };
                    paint.setStyle(SkPaint::kStroke_Style);
                    paint.setStrokeWidth(3.0f);
                    context.canvas.drawRect(SkRect::MakeXYWH(preview.x + 2.0f, preview.y + 2.0f, std::max(0.0f, preview.width - 4.0f), std::max(0.0f, preview.height - 4.0f)), paint);
                }
            }

        private:
            float tab_height_ { 32.0f };
        };
    }

    dock_element::dock_element(dock_config config)
        : ui_element { { ui_element_kind::dock, config.owner } }
        , config_ { std::move(config) }
        , geometry_ { std::make_shared<geometry>() }
    {
        set_clip_children(true);
        if (valid_dock_layout(config_.layout) == false)
            return;
        auto root { make_node(config_.layout.root) };
        root_ = root.get();
        add_child(std::move(root));
        for (const auto& item : config_.layout.floating)
        {
            auto floating { make_node(item.group) };
            floating_.push_back(floating.get());
            add_child(std::move(floating));
        }
    }

    std::unique_ptr<ui_element> dock_element::make_node(const dock_node& node)
    {
        if (node.children.size() == 2)
            return std::make_unique<dock_branch>(node, config_, make_node(node.children[0]), make_node(node.children[1]));
        tab_bar_config tabs {};
        tabs.owner = config_.owner + u8"/" + node.key;
        tabs.selected = node.selected.empty() && node.panels.empty() == false ? node.panels.front() : node.selected;
        tabs.tab_width = 144.0f;
        tabs.min_tab_width = 72.0f;
        for (const auto& key : node.panels)
        {
            const auto panel { std::ranges::find_if(config_.panels, [&](const auto& item) { return item.key == key; }) };
            tabs.items.push_back({ .key = key, .label = panel != config_.panels.end() ? panel->label : key, .icon = panel != config_.panels.end() ? panel->icon : 0 });
        }
        std::vector<ui_element_id> containers {};
        const auto collect = [&](const auto& self, const dock_node& item) -> void {
            if (item.children.empty())
                containers.push_back({ ui_element_kind::tab_bar, config_.owner + u8"/" + item.key });
            for (const auto& child : item.children)
                self(self, child);
        };
        collect(collect, config_.layout.root);
        for (const auto& floating : config_.layout.floating)
            collect(collect, floating.group);
        const auto owns = [containers = std::move(containers)](const drag_payload& payload) {
            return payload.source.kind == ui_element_kind::tab && std::ranges::find(containers, payload.container) != containers.end();
        };
        if (config_.change != nullptr)
        {
            const auto emit { config_.change };
            tabs.receive_accepts = owns;
            tabs.select = [emit, key = node.key](const std::u8string& panel) { return emit({ .operation = dock_operation::select, .panel = panel, .target = key }); };
            tabs.move = [emit, key = node.key](const std::u8string& panel, const std::u8string& target, const drop_side side) {
                return emit({ .panel = panel, .target = key, .relative_to = target, .side = side });
            };
            tabs.receive = [emit, owns, key = node.key](const drag_payload& payload, const std::u8string& target, const drop_side side) {
                // helper가 없는 panel은 원본을 지키고 거절한다. 다른 dock의 탭은 여기서도 받지 않는다.
                return emit({ .panel = owns(payload) ? payload.dragged_owner : std::u8string {}, .target = key, .relative_to = target, .side = side });
            };
            tabs.unclaimed_drop = [emit, geometry = geometry_](const drag_payload& payload, const drag_release& release) {
                const float scale { geometry->scale };
                return emit({ .operation = dock_operation::detach, .panel = payload.dragged_owner,
                    .bounds = { (release.x - geometry->bounds.x - payload.grab_offset_x) / scale, (release.y - geometry->bounds.y - payload.grab_offset_y) / scale, 360.0f, 240.0f }, .release = release
                });
            };
        }
        auto content { config_.content != nullptr && tabs.selected.empty() == false ? config_.content(tabs.selected) : nullptr };
        auto group { std::make_unique<dock_group_element>(ui_element_id { ui_element_kind::dock_group, config_.owner + u8"/" + node.key }, std::make_unique<tab_bar_element>(std::move(tabs)), std::move(content), config_.tab_height) };
        if (config_.change != nullptr)
        {
            drop_target target {};
            target.accepts = owns;
            target.custom_highlight = true;
            target.on_drop = [element = group.get(), emit = config_.change, key = node.key](const drag_payload& payload, const ui_action_context& context) {
                return std::vector<input_action> { emit({ .panel = payload.dragged_owner, .target = key, .placement = placement_at(element->bounds(), context.x, context.y) }) };
            };
            group->set_drop_target(std::move(target));
        }
        return group;
    }

    void dock_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        geometry_->bounds = context.slot;
        geometry_->scale = context.scale > 0.0f ? context.scale : 1.0f;
        if (root_ != nullptr)
            root_->arrange(context);
        for (std::size_t index { 0 }; index < floating_.size(); ++index)
        {
            const auto& box { config_.layout.floating[index].bounds };
            const float scale { geometry_->scale };
            const float width { std::min(box.width * scale, context.slot.width) };
            const float height { std::min(box.height * scale, context.slot.height) };
            const float x { std::clamp(box.x * scale, 0.0f, std::max(0.0f, context.slot.width - width)) };
            const float y { std::clamp(box.y * scale, 0.0f, std::max(0.0f, context.slot.height - height)) };
            floating_[index]->arrange(context.for_child({ context.slot.x + x, context.slot.y + y, width, height }));
        }
    }

    void dock_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
}

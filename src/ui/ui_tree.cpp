#include "luil/ui/ui_tree.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_block.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] std::chrono::milliseconds tooltip_delay_of(const ui_element& element)
        {
            const tooltip_config* const config { element.tooltip_content() };
            return config != nullptr ? config->delay : tooltip_delay;
        }

        // 포인터 자리에서 커서 그림이 차지하는 높이다 (논리 픽셀). 상자를 이만큼 내려야 화살표에 가리지 않는다.
        constexpr float tooltip_cursor_height { 20.0f };

        // 포인터 자리가 있으면 커서 그림 아래에, 없으면 대상의 보이는 영역 아래에 놓는다.
        // 아래로 넘치면 기준(포인터 또는 대상)의 위로 뒤집고, 상자는 창 안으로 민다.
        [[nodiscard]] rect_f tooltip_bounds(
            const rect_f& window, const rect_f& anchor, const std::optional<point_f>& pointer, const float width, const float height, const float gap, const float scale)
        {
            const float box_width { std::min(width, window.width) };
            const float box_height { std::min(height, window.height) };
            float x { anchor.x };
            float y { anchor.y + anchor.height + gap };
            float above { anchor.y - box_height - gap };
            if (pointer.has_value())
            {
                // 기록 뒤에 배치가 흘렀어도 대상 안의 점으로 본다. 상자와 대상이 가로로 겹쳐야
                // `tooltip_placement::bridge`가 끊기지 않는다.
                const float pointer_x { std::clamp(pointer->x, anchor.x, anchor.x + anchor.width) };
                const float pointer_y { std::clamp(pointer->y, anchor.y, anchor.y + anchor.height) };
                x = pointer_x;
                y = pointer_y + tooltip_cursor_height * scale;
                above = pointer_y - box_height - gap;
            }
            if (y + box_height > window.y + window.height)
                y = above;
            return {
                std::clamp(x, window.x, window.x + window.width - box_width),
                std::clamp(y, window.y, window.y + window.height - box_height),
                box_width,
                box_height,
            };
        }

        void draw_tooltip_content(draw_context& context, const tooltip_config& config, const rect_f& window, const tooltip_placement& placement, const interaction_snapshot& interaction)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const rect_f content { config.content->root()->bounds() };
            const rect_f& box { placement.box };
            const rect_f& content_box { placement.content };
            const float radius { config.corner_radius * scale };
            const SkRRect body { SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius) };
            const SkAutoCanvasRestore restore { &context.canvas, true };
            context.canvas.clipRect(SkRect::MakeXYWH(window.x, window.y, window.width, window.height));
            draw_surface_shadow(context, box, radius, config.shadow);
            const SkPaint background { solid_paint(config.background ? config.background(context.palette) : context.palette.tooltip_background) };
            context.canvas.drawRRect(body, background);
            {
                const SkAutoCanvasRestore restore_content { &context.canvas, true };
                context.canvas.clipRRect(body);
                context.canvas.clipRect(SkRect::MakeXYWH(content_box.x, content_box.y, content_box.width, content_box.height));
                context.canvas.translate(content_box.x, content_box.y);
                context.canvas.scale(scale, scale);
                const float scroll { interaction.tooltip.has_value() ? interaction.tooltip->scroll_offset : 0.0f };
                context.canvas.translate(-content.x, -content.y - scroll);
                draw_context inner { derive_draw_context(context, context.palette, 1.0f) };
                // 내용 tree의 id가 주 tree와 같아도 hover·눌림·초점 그림을 물려받지 않는다.
                interaction_snapshot content_interaction {};
                if (interaction.tooltip.has_value())
                {
                    content_interaction.hovered = interaction.tooltip->hovered;
                    content_interaction.pressed = interaction.tooltip->pressed;
                }
                config.content->draw(inner, content_interaction);
            }
            SkPaint border { solid_paint(config.border ? config.border(context.palette) : context.palette.tooltip_border) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(scale);
            SkRRect outline { body };
            outline.inset(scale / 2.0f, scale / 2.0f);
            context.canvas.drawRRect(outline, border);
        }
    }

    ui_tree::ui_tree(std::unique_ptr<ui_element> root)
        : root_ { std::move(root) }
    {
        if (root_ != nullptr)
        {
            index_element(*root_, nullptr, nullptr, root_->visible());
            find_focus_trap(*root_, trap_);
            // 가둠이 정해진 뒤에 거른다 — 밖의 기본 버튼은 없는 것이다.
            // 매 frame이 아니라 여기서 한 번 판정하면 그 뒤로는 물을 것이 없다.
            find_default_button(*root_, default_button_);
            if (default_button_ != nullptr && within_focus_trap(default_button_->id()) == false)
                default_button_ = nullptr;
        }
    }

    const ui_element* ui_tree::root() const noexcept
    {
        return root_.get();
    }

    const ui_element* ui_tree::hit_test(const float x, const float y) const
    {
        return root_ != nullptr ? root_->hit_test(x, y) : nullptr;
    }

    std::optional<tooltip_placement> ui_tree::place_tooltip(const ui_element_id& owner, const float requested_scale, const std::optional<point_f>& pointer) const
    {
        const ui_element* const target { find(owner) };
        const tooltip_config* const config { target != nullptr ? target->tooltip_content() : nullptr };
        if (root_ == nullptr || config == nullptr || visibly_contains(owner) == false)
            return std::nullopt;
        const auto anchor { visible_bounds(*target) };
        const rect_f window { root_->bounds() };
        if (anchor.has_value() == false || window.width <= 0.0f || window.height <= 0.0f)
            return std::nullopt;
        const float scale { requested_scale > 0.0f ? requested_scale : 1.0f };
        const rect_f content { config->content->root()->bounds() };
        const rect_f box { tooltip_bounds(window, *anchor, pointer, (content.width + config->padding * 2.0f) * scale, (content.height + config->padding * 2.0f) * scale, config->gap * scale, scale) };
        const float padding { std::min({ config->padding * scale, box.width / 2.0f, box.height / 2.0f }) };
        const float left { std::max(anchor->x, box.x) };
        const float right { std::min(anchor->x + anchor->width, box.x + box.width) };
        const float top { std::min(anchor->y + anchor->height, box.y + box.height) };
        const float bottom { std::max(anchor->y, box.y) };
        const rect_f content_box { box.x + padding, box.y + padding, box.width - padding * 2.0f, box.height - padding * 2.0f };
        const rect_f bridge { left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top) };
        return tooltip_placement { *anchor, box, content_box, bridge };
    }

    const ui_element* ui_tree::hit_tooltip(const ui_element_id& owner, const tooltip_placement& placement, const float scale, const float x, const float y, const float scroll_offset) const
    {
        const ui_element* const target { find(owner) };
        const tooltip_config* const config { target != nullptr ? target->tooltip_content() : nullptr };
        if (config == nullptr || placement.content.contains(x, y) == false)
            return nullptr;
        const rect_f origin { config->content->root()->bounds() };
        return config->content->hit_test((x - placement.content.x) / scale + origin.x, (y - placement.content.y) / scale + origin.y + scroll_offset);
    }

    const ui_element* ui_tree::find_drop_target(const float x, const float y, const drag_payload& payload) const
    {
        return drop_hit_test(x, y, payload).target;
    }

    drop_hit_result ui_tree::drop_hit_test(const float x, const float y, const drag_payload& payload) const
    {
        return root_ != nullptr ? root_->drop_hit_test(x, y, payload) : drop_hit_result {};
    }

    const ui_element* ui_tree::find(const ui_element_id& id) const noexcept
    {
        // 포인터 이동·tooltip·drag 표시가 매 frame 부르는 hot path다.
        // 선형 문자열 비교 대신 해시 색인을 쓴다.
        const auto found { lookup_.find(id) };
        return found != lookup_.end() ? found->second : nullptr;
    }

    const std::vector<ui_element_id>& ui_tree::duplicate_ids() const noexcept
    {
        return duplicates_;
    }

    const std::vector<ui_element_id>& ui_tree::unarranged() const noexcept
    {
        return unarranged_;
    }

    ui_tree make_arranged_tree(std::unique_ptr<ui_element> root, const rect_f& slot, const float scale, const rect_f& surface, const ui_metrics* const metrics, const ui_style* const style)
    {
        if (root != nullptr)
            root->arrange({ .slot = slot, .scale = scale, .surface = surface, .metrics = metrics != nullptr || style == nullptr ? metrics : &style->metrics, .style = style });
        return ui_tree { std::move(root) };
    }

    ui_cursor ui_tree::cursor_at(const float x, const float y, const interaction_snapshot& interaction) const
    {
        if (interaction.tooltip.has_value() && interaction.drag.has_value() == false)
        {
            const auto& state { *interaction.tooltip };
            if (const auto placement { place_tooltip(state.owner, state.scale, interaction.hover_point) }; placement.has_value() && placement->box.contains(x, y))
            {
                const auto* const hit { hit_tooltip(state.owner, *placement, state.scale, x, y, state.scroll_offset) };
                return hit != nullptr ? hit->cursor_at(0.0f, 0.0f, {}) : ui_cursor::arrow;
            }
        }
        // 끌고 있는 동안은 포인터가 무엇 위에 있든 잡은 쪽이 모양을 정한다.
        //  - 놓을 자리를 찾아 헤매는 동안 모양이 깜빡이지 않는다.
        if (interaction.drag.has_value())
        {
            // 끄는 쪽이 금지 모양을 청했으면 받는 대상이 없는 자리에서 그것이다 (`drag_payload::rejected_cursor`).
            const drag_payload& payload { interaction.drag->payload };
            // 이 앱의 어느 창 위도 아니면(창 밖으로 끌어 냈다) 끄는 쪽이 청한 "떼어 낸다" 모양이다.
            //  - 표면별로 거른 것의 `outside`는 "포인터가 이 표면 밖"이라 다른 창 위일 수도 있다. 그 창의 모양은
            //    플랫폼이 그 창의 tree에 묻는다 — 여기서 답하는 것은 포인터가 어느 창 위에도 없는 자리뿐이다.
            if (interaction.drag->outside && payload.outside_cursor != ui_cursor::inherit)
                return payload.outside_cursor;
            if (payload.rejected_cursor != ui_cursor::inherit && interaction.drag->hovered_drop_target == ui_element_id {})
                return payload.rejected_cursor;
            const ui_element* const source { find(payload.source) };
            return source != nullptr ? source->cursor_at(x, y, interaction) : ui_cursor::grabbing;
        }

        // 스크롤 막대나 dialog 캡션처럼 누른 채 끄는 중이면 그 element가 정한다.
        if ((interaction.pressed == ui_element_id {}) == false)
        {
            const ui_element* const pressed { find(interaction.pressed) };
            if (pressed != nullptr && pressed->pointer_drag() != nullptr)
                return pressed->cursor_at(x, y, interaction);
        }

        const ui_element* const hit { hit_test(x, y) };
        return hit != nullptr ? hit->cursor_at(x, y, interaction) : ui_cursor::inherit;
    }

    std::vector<ui_element_id> ui_tree::ids_of_kind(const ui_element_kind kind) const
    {
        // kind 색인은 그리기 순서(등록 순서)를 유지한다.
        const auto found { kinds_.find(static_cast<std::uint32_t>(kind)) };
        return found != kinds_.end() ? found->second : std::vector<ui_element_id> {};
    }

    std::vector<ui_element_id> ui_tree::focus_order() const
    {
        std::vector<ui_element_id> order {};
        // 가둠이 있으면 훑기가 **거기서 시작한다**. 밖의 자리는 목록에 없다.
        //  - 가둠 자신은 자리가 아니라 자식부터 훑는다 (묶음과 같은 규칙).
        //  - 묶음 접기는 그 안에서 그대로 일어난다. 두 규칙은 훑기의 시작점과
        //    가지치기로 갈려 있어 서로를 모른다 (modal-dialog-design.md).
        if (trap_ != nullptr)
            collect_group_members(*trap_, order);
        else if (root_ != nullptr)
            collect_focus_stops(*root_, order);
        return order;
    }

    const ui_element* ui_tree::focus_trap() const noexcept
    {
        return trap_;
    }

    std::optional<ui_element_id> ui_tree::focus_trap_entry() const
    {
        // 가두는 것이 없거나 이름 짓지 않았으면 세울 자리가 없다.
        // 빈 이름을 "첫 자리"로 읽지 않는 것이 묶음과 갈리는 유일한 지점이다.
        if (trap_ == nullptr || trap_->focus_entry() == ui_element_id {})
            return std::nullopt;
        // 그 뒤는 묶음과 같다 — 이름 지은 자리가 지금 자리가 아니면 첫 자리다.
        return group_entry(*trap_);
    }

    bool ui_tree::within_focus_trap(const ui_element_id& id) const
    {
        // 가두는 것이 없으면 어디든 안이다.
        if (trap_ == nullptr)
            return true;
        return contains_id(*trap_, id);
    }

    bool ui_tree::visibly_contains(const ui_element_id& id) const
    {
        // 접근 색인은 보이는 경로 위의 것만 담는다 — 담겨 있음이 곧 답이다.
        // 조상만 숨은 자식은 색인에 들지 않아 여기서 걸러진다.
        return access_index_.contains(id);
    }

    const std::vector<const ui_element*>& ui_tree::access_top_level() const noexcept
    {
        return access_top_;
    }

    const std::vector<const ui_element*>& ui_tree::access_children_of(const ui_element& element) const
    {
        // 자식이 없는 요소는 색인에 담기지 않는다 — 빈 줄 하나로 답한다.
        static const std::vector<const ui_element*> childless {};
        const auto found { access_children_.find(&element) };
        return found != access_children_.end() ? found->second : childless;
    }

    const ui_element* ui_tree::access_parent_of(const ui_element_id& id) const
    {
        const auto found { access_index_.find(id) };
        return found != access_index_.end() ? found->second.parent : nullptr;
    }

    const ui_element* ui_tree::access_sibling_of(const ui_element_id& id, const bool forward) const
    {
        const auto found { access_index_.find(id) };
        // 구조(`none`)는 형제 줄에 서지 않는다 — 접힌 것의 자리는 자식들이 잇는다.
        if (found == access_index_.end() || found->second.position == no_access_position)
            return nullptr;
        const std::vector<const ui_element*>* siblings { &access_top_ };
        if (found->second.parent != nullptr)
        {
            // 줄에 선 자리가 있으면 부모의 목록도 반드시 있다 — 없음은 방어다.
            const auto children { access_children_.find(found->second.parent) };
            if (children == access_children_.end())
                return nullptr;
            siblings = &children->second;
        }
        const std::size_t position { found->second.position };
        if (forward)
            return position + 1 < siblings->size() ? (*siblings)[position + 1] : nullptr;
        return position > 0 ? (*siblings)[position - 1] : nullptr;
    }

    const ui_element* ui_tree::scroll_container_of(const ui_element_id& id) const
    {
        const auto found { access_index_.find(id) };
        return found != access_index_.end() ? found->second.scroll_container : nullptr;
    }

    bool ui_tree::within(const ui_element_id& ancestor, const ui_element_id& id) const
    {
        const ui_element* const root { find(ancestor) };
        return root != nullptr && contains_id(*root, id);
    }

    std::vector<const ui_element*> ui_tree::ancestry_of(const ui_element_id& id) const
    {
        std::vector<const ui_element*> path {};
        if (root_ == nullptr || find_path(*root_, id, path) == false)
            return {};
        // 안쪽부터 묻는 길이라 자신이 맨 앞에 서게 뒤집는다.
        std::reverse(path.begin(), path.end());
        return path;
    }

    const ui_element* ui_tree::default_button() const noexcept
    {
        return default_button_;
    }

    void ui_tree::find_default_button(const ui_element& element, const ui_element*& button)
    {
        // 보이지 않는 가지의 것은 없는 것이다 (가둠 찾기와 같은 규칙).
        if (element.visible() == false)
            return;
        // 뒤에 오는 것이 이긴다 — 겹친 dialog는 나중에 그린 것이 위다.
        if (element.default_button())
            button = &element;
        for (const std::unique_ptr<ui_element>& child : element.children())
            find_default_button(*child, button);
    }

    void ui_tree::find_focus_trap(const ui_element& element, const ui_element*& trap)
    {
        // 보이지 않는 가지의 가둠은 없는 것이다 (hit test·focus_order와 같은 규칙).
        if (element.visible() == false)
            return;
        // 뒤에 오는 것이 이긴다 — 겹친 것은 나중에 그린 것이 위이고,
        // 중첩된 것은 자손이 pre-order에서 뒤에 온다.
        if (element.focus_trap())
            trap = &element;
        for (const std::unique_ptr<ui_element>& child : element.children())
            find_focus_trap(*child, trap);
    }

    bool ui_tree::contains_id(const ui_element& element, const ui_element_id& id)
    {
        if (element.visible() == false)
            return false;
        if (element.id() == id)
            return true;
        for (const std::unique_ptr<ui_element>& child : element.children())
            if (contains_id(*child, id))
                return true;
        return false;
    }

    bool ui_tree::find_path(const ui_element& element, const ui_element_id& id, std::vector<const ui_element*>& path)
    {
        // 보이지 않는 가지에는 초점이 설 수 없다 (`contains_id`와 같은 규칙).
        if (element.visible() == false)
            return false;
        path.push_back(&element);
        if (element.id() == id)
            return true;
        for (const std::unique_ptr<ui_element>& child : element.children())
            if (find_path(*child, id, path))
                return true;
        path.pop_back();
        return false;
    }

    void ui_tree::collect_focus_stops(const ui_element& element, std::vector<ui_element_id>& order)
    {
        // 보이지 않는 가지는 그 안까지 통째로 건너뛴다.
        // 색인(index_)은 보이지 않는 자식도 담으므로 여기서는 tree를 직접 걷는다.
        if (element.visible() == false)
            return;
        // 묶음은 자리 하나로 접고 그 안으로 내려가지 않는다.
        // 중첩된 묶음도 같은 규칙으로 접히므로 훑기는 이 함수 하나다.
        if (element.focus_group() != focus_axis::none)
        {
            if (const std::optional<ui_element_id> entry { group_entry(element) }; entry.has_value())
                order.push_back(*entry);
            return;
        }
        if (element.focusable())
            order.push_back(element.id());
        for (const std::unique_ptr<ui_element>& child : element.children())
            collect_focus_stops(*child, order);
    }

    focus_group_scope ui_tree::focus_group_of(const ui_element_id& id) const
    {
        if (root_ == nullptr)
            return {};
        // 부모를 담아 두지 않으므로 root에서 내려가며 가장 안쪽 묶음을 들고 간다.
        const std::optional<const ui_element*> group { find_focus_group(*root_, id, nullptr) };
        if (group.has_value() == false || *group == nullptr)
            return {};
        focus_group_scope scope { (*group)->focus_group(), {} };
        collect_group_members(**group, scope.members);
        return scope;
    }

    std::optional<const ui_element*> ui_tree::find_focus_group(const ui_element& element, const ui_element_id& id, const ui_element* const group)
    {
        // 보이지 않는 가지에는 초점이 설 수 없다.
        if (element.visible() == false)
            return std::nullopt;
        if (element.id() == id)
            return group;
        // 내려가는 동안 가장 안쪽 묶음을 바꿔 든다.
        const ui_element* const inner { element.focus_group() != focus_axis::none ? &element : group };
        for (const std::unique_ptr<ui_element>& child : element.children())
            if (const std::optional<const ui_element*> found { find_focus_group(*child, id, inner) }; found.has_value())
                return found;
        return std::nullopt;
    }

    void ui_tree::collect_group_members(const ui_element& group, std::vector<ui_element_id>& members)
    {
        // 묶음 자신은 접지 않고 자식부터 훑는다.
        for (const std::unique_ptr<ui_element>& child : group.children())
            collect_focus_stops(*child, members);
    }

    std::optional<ui_element_id> ui_tree::group_entry(const ui_element& group)
    {
        std::vector<ui_element_id> members {};
        collect_group_members(group, members);
        if (members.empty())
            return std::nullopt;
        // 앱이 이름 붙인 항목(통상 "선택된 것")이 지금 초점을 받을 수 있으면 그것이다.
        if (std::find(members.begin(), members.end(), group.focus_entry()) != members.end())
            return group.focus_entry();
        return members.front();
    }

    std::optional<std::chrono::steady_clock::time_point> ui_tree::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        std::optional<std::chrono::steady_clock::time_point> next {};
        const auto merge = [&next](const std::optional<std::chrono::steady_clock::time_point>& candidate) {
            if (candidate.has_value() && (next.has_value() == false || *candidate < *next))
                next = candidate;
        };

        // 색인은 화면에 걸친 element만 담아 작다.
        // 매 frame 훑어도 싸다.
        //  - **보이는 경로 위의 것만이다.** 자기가 숨었든 조상이 숨었든, 그 element는
        //    그려지지 않으므로 다음 장이 서도 화면에 변화가 없다 — 숨긴 탭 안의
        //    움직이는 그림과 회전 표시가 창을 주기적으로 깨우던 자리다.
        for (const ui_element* const element : visible_index_)
            merge(element->next_update(context, interaction));

        // tooltip은 element 계층 밖에서 tree가 그리므로 지연이 끝나는 시각도 tree가 답한다.
        // 끄는 동안에는 그리지 않으니 답할 것도 없다 (draw_tooltip과 같은 조건).
        if (const ui_element* const hovered { tooltip_target(interaction) }; hovered != nullptr)
        {
            const auto shown_at { *interaction.hover_started_at + tooltip_delay_of(*hovered) };
            if (context.now < shown_at)
                merge(shown_at);
            else if (const tooltip_config* const config { hovered->tooltip_content() }; config != nullptr)
            {
                interaction_snapshot content_interaction {};
                if (interaction.tooltip.has_value())
                {
                    content_interaction.hovered = interaction.tooltip->hovered;
                    content_interaction.pressed = interaction.tooltip->pressed;
                }
                merge(config->content->next_update(context, content_interaction));
            }
        }
        return next;
    }

    void ui_tree::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (root_ == nullptr)
            return;
        root_->draw(context, interaction);
        draw_focus_ring(context, interaction);
        if (interaction.drag.has_value())
            draw_drag_visual(context, *interaction.drag);
        draw_tooltip(context, interaction);
    }

    void ui_tree::index_element(const ui_element& element, const ui_element* const scroll_container, const ui_element* access_parent, const bool visible_path)
    {
        index_.push_back(&element);
        kinds_[static_cast<std::uint32_t>(element.id().kind)].push_back(element.id());
        // 같은 id는 첫 등록이 이긴다.
        // 뒤쪽 것은 중복 목록에 남아 test·진단이 잡아낸다.
        const bool owns_id { lookup_.emplace(element.id(), &element).second };
        if (owns_id == false)
            duplicates_.push_back(element.id());
        if (element.visible() && element.arranged() == false)
            unarranged_.push_back(element.id());

        // 접근 색인은 보이는 경로 위의 것만 담는다 (`contains_id`와 같은 규칙 —
        // 보이지 않는 것은 읽을 자리도 아니다). 구조(`none`)는 형제 줄에 서지
        // 않고 그 자식이 이 부모 자리로 승격된다 (`access_children`와 같은 접기).
        //  - **id의 임자만 줄에 선다** — `find`가 답하는 그 element다. 줄의 자리를
        //    id로 되묻기 때문에(`access_sibling_of`) 같은 id가 둘 서면 뒤의 것이
        //    앞의 것의 자리를 답하고, "다음"이 제자리로 돌아와 UIA 클라이언트의
        //    자식 훑기가 영영 끝나지 않는다. 임자가 아닌 것은 구조처럼 접혀 그
        //    자식이 이 부모 자리로 올라오므로 그 아래가 통째로 사라지지는 않는다.
        //    UIA에게도 같은 말이다 — runtime id가 id에서 나오므로 줄에 둘이 서면
        //    한 창에 같은 runtime id가 둘이다.
        const ui_element* next_access_parent { access_parent };
        if (visible_path)
        {
            visible_index_.push_back(&element);
            if (element.hover_region() != nullptr)
                hover_regions_.push_back(&element);
            std::size_t position { no_access_position };
            if (element.accessibility().role != access_role::none && owns_id)
            {
                std::vector<const ui_element*>& siblings { access_parent != nullptr ? access_children_[access_parent] : access_top_ };
                position = siblings.size();
                siblings.push_back(&element);
                next_access_parent = &element;
            }
            access_index_.emplace(element.id(), access_entry { access_parent, scroll_container, position });
        }
        // 자기 자신은 자기를 감싸지 않는다 — 들이려는 자리는 언제나 그 창 **안의**
        // 무엇이다. 그래서 자식에게 넘길 때에야 이 element가 창이 된다.
        const scroll_source* const source { element.scroll() };
        const ui_element* const next_scroll_container { source != nullptr && source->scroll != nullptr ? &element : scroll_container };
        for (const std::unique_ptr<ui_element>& child : element.children())
            index_element(*child, next_scroll_container, next_access_parent, visible_path && child->visible());
    }

    namespace {
        [[nodiscard]] std::optional<rect_f> intersect(const rect_f& first, const rect_f& second) noexcept
        {
            const float left { std::max(first.x, second.x) };
            const float top { std::max(first.y, second.y) };
            const float right { std::min(first.x + first.width, second.x + second.width) };
            const float bottom { std::min(first.y + first.height, second.y + second.height) };
            if (right <= left || bottom <= top)
                return std::nullopt;
            return rect_f { left, top, right - left, bottom - top };
        }

        // node 아래에서 target을 찾아, 지나온 자르는 컨테이너들과의 교집합을 돌려준다.
        // clip이 nullopt면 아직 아무도 자르지 않았다는 뜻이다.
        [[nodiscard]] std::optional<rect_f> visible_bounds_of(const ui_element& node, const ui_element& target, const std::optional<rect_f>& clip)
        {
            if (&node == &target)
                return clip.has_value() ? intersect(target.bounds(), *clip) : std::optional<rect_f> { target.bounds() };

            std::optional<rect_f> next_clip { clip };
            if (node.clip_children())
            {
                next_clip = clip.has_value() ? intersect(*clip, node.bounds()) : std::optional<rect_f> { node.bounds() };
                // 자른 결과가 비면 이 아래는 아무것도 보이지 않는다.
                if (next_clip.has_value() == false)
                    return std::nullopt;
            }
            for (const std::unique_ptr<ui_element>& child : node.children())
                if (const std::optional<rect_f> found { visible_bounds_of(*child, target, next_clip) }; found.has_value())
                    return found;
            return std::nullopt;
        }
    }

    std::optional<rect_f> ui_tree::visible_bounds(const ui_element& target) const
    {
        if (root_ == nullptr)
            return std::nullopt;
        return visible_bounds_of(*root_, target, std::nullopt);
    }

    namespace {
        [[nodiscard]] bool caption_client_under(const ui_element& node, const float x, const float y, const std::optional<rect_f>& clip)
        {
            if (node.visible() == false)
                return false;
            if (node.caption_client())
            {
                const std::optional<rect_f> shown { clip.has_value() ? intersect(node.bounds(), *clip) : std::optional<rect_f> { node.bounds() } };
                if (shown.has_value() && shown->contains(x, y))
                    return true;
            }
            std::optional<rect_f> next_clip { clip };
            if (node.clip_children())
            {
                next_clip = clip.has_value() ? intersect(*clip, node.bounds()) : std::optional<rect_f> { node.bounds() };
                if (next_clip.has_value() == false)
                    return false;
            }
            for (const std::unique_ptr<ui_element>& child : node.children())
                if (caption_client_under(*child, x, y, next_clip))
                    return true;
            return false;
        }
    }

    bool ui_tree::caption_client_at(const float x, const float y) const
    {
        return root_ != nullptr && caption_client_under(*root_, x, y, std::nullopt);
    }

    namespace {
        [[nodiscard]] bool subtree_contains(const ui_element& node, const ui_element* const target)
        {
            if (&node == target)
                return true;
            for (const std::unique_ptr<ui_element>& child : node.children())
                if (subtree_contains(*child, target))
                    return true;
            return false;
        }

        // 그리기 순서로 훑어 마지막(가장 안쪽·나중에 담은) 정책 element를 남긴다.
        void find_idle_hide(const ui_element& node, const float x, const float y, const std::optional<rect_f>& clip, const ui_element*& found)
        {
            if (node.visible() == false)
                return;
            if (node.cursor_idle_hide() > std::chrono::milliseconds { 0 })
            {
                const std::optional<rect_f> shown { clip.has_value() ? intersect(node.bounds(), *clip) : std::optional<rect_f> { node.bounds() } };
                if (shown.has_value() && shown->contains(x, y))
                    found = &node;
            }
            std::optional<rect_f> next_clip { clip };
            if (node.clip_children())
            {
                next_clip = clip.has_value() ? intersect(*clip, node.bounds()) : std::optional<rect_f> { node.bounds() };
                if (next_clip.has_value() == false)
                    return;
            }
            for (const std::unique_ptr<ui_element>& child : node.children())
                find_idle_hide(*child, x, y, next_clip, found);
        }
    }

    std::vector<const ui_element*> ui_tree::hover_regions_at(const float x, const float y) const
    {
        std::vector<const ui_element*> result {};
        for (const ui_element* const region : hover_regions_)
            if (const std::optional<rect_f> shown { visible_bounds(*region) }; shown.has_value() && shown->contains(x, y))
                result.push_back(region);
        return result;
    }

    std::chrono::milliseconds ui_tree::cursor_idle_hide_at(const float x, const float y) const
    {
        if (root_ == nullptr)
            return std::chrono::milliseconds { 0 };
        const ui_element* policy { nullptr };
        find_idle_hide(*root_, x, y, std::nullopt, policy);
        if (policy == nullptr)
            return std::chrono::milliseconds { 0 };
        // 덮은 것이 hit를 받으면 그쪽의 자리다. 아무도 hit를 받지 않는 자리(그림만 있는 보기)는 정책의 자리다.
        const ui_element* const hit { hit_test(x, y) };
        if (hit != nullptr && subtree_contains(*policy, hit) == false)
            return std::chrono::milliseconds { 0 };
        return policy->cursor_idle_hide();
    }

    void ui_tree::draw_focus_ring(draw_context& context, const interaction_snapshot& interaction) const
    {
        // **키보드로 옮긴 초점에만** 테를 그린다. 눌러서 잡은 초점까지 테가 남으면
        // 누를 때마다 화면이 시끄럽다 (keyboard-focus-design.md).
        if (interaction.focus_visible == false)
            return;
        const ui_element* const focused { find(interaction.focused) };
        if (focused == nullptr)
            return;
        // 잘라내는 컨테이너에 걸쳐 있으면 보이는 만큼만 두른다 — 오버레이는 clip
        // 밖에 그려지므로 원시 bounds를 쓰면 창 밖으로 샌다 (drag 강조와 같은 규칙).
        //  - 아래의 바깥 여백만큼(3px)은 그 잘린 상자 밖으로 나간다. 초점은
        //    `on_focus_moved`가 화면 안으로 들이므로 반쯤 잘린 초점은 지나가는
        //    상태다 — 그 한순간을 위해 테를 상자 안으로 접지 않는다.
        std::optional<rect_f> box { visible_bounds(*focused) };
        if (box.has_value() == false)
            return;
        // 몸만 두르는 element(오류 줄을 품은 텍스트 칸)는 그 사각형과 보이는 자리의 겹침이다.
        if (const std::optional<rect_f> ring { focused->focus_ring_bounds() }; ring.has_value())
        {
            const float left { std::max(box->x, ring->x) };
            const float top { std::max(box->y, ring->y) };
            const float right { std::min(box->x + box->width, ring->x + ring->width) };
            const float bottom { std::min(box->y + box->height, ring->y + ring->height) };
            if (right <= left || bottom <= top)
                return;
            box = rect_f { left, top, right - left, bottom - top };
        }

        // element마다 그리지 않고 tree가 한곳에서 얹는다 — tooltip·drag 표시와
        // 같은 자리다. 앱이 만든 element도 고칠 것 없이 테를 받는다.
        //
        // 테는 element **밖**에 두른다. 몸 안에 그리면 채워진 버튼 위에서 테가
        // 버튼의 가장자리와 겹쳐, 초점 표시가 아니라 버튼 자신의 테두리로 읽힌다.
        // 몸과 테 사이를 한 뼘 띄우면 어느 채움 위에서든 띠로 선다
        // (브라우저의 outline-offset과 같은 자리).
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float gap { 1.0f * scale };
        const float stroke { 2.0f * scale };
        // 획은 중심선 기준이라, 안쪽 가장자리가 몸에서 gap만큼 떨어지게 옮긴다.
        const float outset { gap + stroke / 2.0f };
        SkPaint ring { solid_paint(context.palette.accent) };
        ring.setStyle(SkPaint::kStroke_Style);
        ring.setStrokeWidth(stroke);
        const SkRect body { SkRect::MakeXYWH(box->x - outset, box->y - outset, box->width + outset * 2.0f, box->height + outset * 2.0f) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, 5.0f * scale, 5.0f * scale), ring);
    }

    const ui_element* ui_tree::tooltip_target(const interaction_snapshot& interaction) const
    {
        // 숨거나 완전히 잘린 대상은 이전 snapshot에 hover가 남아 있어도 설명을 세우지 않는다.
        if (root_ == nullptr || root_->bounds().width <= 0.0f || root_->bounds().height <= 0.0f || interaction.drag.has_value() || interaction.hover_started_at.has_value() == false)
            return nullptr;
        const ui_element* const hovered { find(interaction.hovered) };
        return hovered != nullptr && hovered->has_tooltip() && visibly_contains(hovered->id()) && visible_bounds(*hovered).has_value() ? hovered : nullptr;
    }

    void ui_tree::draw_tooltip(draw_context& context, const interaction_snapshot& interaction) const
    {
        const ui_element* const hovered { tooltip_target(interaction) };
        if (hovered == nullptr || context.now - *interaction.hover_started_at < tooltip_delay_of(*hovered))
            return;
        const rect_f window { root_->bounds() };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        if (const tooltip_config* const config { hovered->tooltip_content() }; config != nullptr)
        {
            // 입력 쪽(`hit_tooltip`·간격 건너기)과 같은 자리라야 보이는 링크와 눌리는 링크가 같다.
            if (const auto placement { place_tooltip(hovered->id(), scale, interaction.hover_point) }; placement.has_value())
                draw_tooltip_content(context, *config, window, *placement, interaction);
            return;
        }

        const rect_f anchor { *visible_bounds(*hovered) };
        const SkFont font { make_text_font(context, { .size = context.metrics.small_font_size, .weight = context.metrics.font_weights.small_text }) };
        const std::u8string& text { hovered->tooltip() };
        const float text_width { measure_text(text, font) };
        const float padding { 5.0f * scale };
        const float box_width { text_width + padding * 2.0f };
        const float box_height { 22.0f * scale };
        const rect_f placed { tooltip_bounds(window, anchor, interaction.hover_point, box_width, box_height, 3.0f * scale, scale) };
        const float box_x { placed.x };
        const float box_y { placed.y };
        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(SkRect::MakeXYWH(placed.x, placed.y, placed.width, placed.height));
        const SkRect box { SkRect::MakeXYWH(placed.x, placed.y, placed.width, placed.height) };
        const SkPaint background { solid_paint(context.palette.tooltip_background) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(box, 2.0f * scale, 2.0f * scale), background);
        SkPaint border { solid_paint(context.palette.tooltip_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(box, 2.0f * scale, 2.0f * scale), border);
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        draw_text(context.canvas, text, box_x + padding, box_y + centered_text_baseline(font, box_height), font, foreground);
    }

    drag_overlay_plan plan_drag_overlay(const drag_payload& payload) noexcept
    {
        // 강조는 언제나 우리 몫이다 — 놓을 자리를 아는 것은 이쪽뿐이라
        // 밖에서 온 끌기(파일)에도 서야 한다.
        // custom_visual이 누르는 것은 ghost 하나다.
        return drag_overlay_plan { payload.suppress_drop_highlight == false, payload.custom_visual == false };
    }

    drag_ghost_label drag_ghost_label_of(const drag_payload& payload) noexcept
    {
        const std::size_t count { payload.dragged_count() };
        std::u8string_view text { payload.label };
        if (text.empty())
            text = payload.dragged_owner.empty() == false ? std::u8string_view { payload.dragged_owner }
                : payload.dragged_owners.empty() == false ? std::u8string_view { payload.dragged_owners.front() }
                                                          : std::u8string_view {};
        return { text, count > 1u ? count - 1u : 0u };
    }

    namespace {
        // ghost 상자 하나다 (바탕·테두리·이름·나머지 개수). 창 안의 ghost와 창 밖에서 따라오는 그림이 같은 상자를 쓴다.
        //  - `glyph`가 0이 아니면 이름 앞에 그 글리프를 accent로 얹는다 (창 밖 그림의 "새 창" 표시).
        void draw_drag_ghost_box(draw_context& context, const drag_payload& payload, const SkRect& ghost, const char32_t glyph)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            SkPaint fill { solid_paint(context.palette.surface_background) };
            fill.setAlphaf(0.85f);
            context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, context.metrics.control_corner_radius * scale, context.metrics.control_corner_radius * scale), fill);
            SkPaint border { solid_paint(context.palette.accent) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(1.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, context.metrics.control_corner_radius * scale, context.metrics.control_corner_radius * scale), border);

            const SkFont font { make_text_font(context, { .size = context.metrics.small_font_size, .weight = context.metrics.font_weights.small_text }) };
            const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            const drag_ghost_label label { drag_ghost_label_of(payload) };
            // 여러 항목이면 오른쪽에 나머지 개수(`+N`)의 알약을 얹는다. 이름은 그 앞까지만 그린다.
            float text_right { ghost.right() - 7.0f * scale };
            if (label.more > 0u)
            {
                const std::string digits { std::to_string(label.more) };
                const std::u8string count { u8"+" + std::u8string { digits.begin(), digits.end() } };
                const float count_width { measure_text(count, font) };
                const float pill_height { ghost.height() - 8.0f * scale };
                const SkRect pill { SkRect::MakeXYWH(ghost.right() - 4.0f * scale - count_width - 10.0f * scale, ghost.top() + 4.0f * scale, count_width + 10.0f * scale, pill_height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(pill, pill_height / 2.0f, pill_height / 2.0f), solid_paint(context.palette.accent));
                draw_text(context.canvas, count, pill.left() + 5.0f * scale, ghost.top() + centered_text_baseline(font, ghost.height()), font, solid_paint(context.palette.accent_foreground));
                text_right = pill.left() - 4.0f * scale;
            }
            float text_left { ghost.left() + 7.0f * scale };
            if (glyph != 0u && context.codicon_typeface != nullptr)
            {
                const float glyph_size { 14.0f * scale };
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_size };
                draw_centered_glyph(context.canvas, glyph, { text_left, ghost.top(), glyph_size, ghost.height() }, glyph_font, solid_paint(context.palette.accent));
                text_left += glyph_size + 6.0f * scale;
            }
            if (text_right > text_left)
                static_cast<void>(draw_text_within(context.canvas, label.text, text_left, ghost.top() + centered_text_baseline(font, ghost.height()), text_right - text_left, font, foreground));
        }
    }

    void draw_detached_drag_ghost(draw_context& context, const drag_payload& payload)
    {
        // 창 안의 ghost와 같은 상자에 "새 창" 글리프를 얹는다 — 놓으면 새 창이 된다는 표시다.
        //  - 바깥 여백 없이 (0,0)부터 채운다. 둘레는 투명 창의 알파가 가른다.
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float inset { 0.5f * scale };
        draw_drag_ghost_box(
            context, payload, SkRect::MakeXYWH(inset, inset, detached_drag_ghost_width * scale - 2.0f * inset, detached_drag_ghost_height * scale - 2.0f * inset), codicons::icon_open_in_window);
    }

    void ui_tree::draw_drag_visual(draw_context& context, const drag_visual& drag) const
    {
        // 포인터가 이 표면 위에 없으면 그릴 것이 없다 — 좌표도 강조도 다른 창(또는 화면)의 것이다. 끌리는 항목의 흐린
        // 표시는 element가 payload를 보고 스스로 그린다.
        if (drag.outside)
            return;
        const drag_overlay_plan plan { plan_drag_overlay(drag.payload) };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // 수락 중인 drop 대상을 먼저 강조한다.
        // 대상이 자르는 컨테이너에 걸쳐 있으면 보이는 만큼만 테두리를 친다.
        //  - 오버레이는 clip 밖에 그려지므로 원시 bounds를 쓰면 창 밖으로 샌다.
        //  - 자기 강조를 그리는 대상(`drop_target::custom_highlight` — 틈 표시)에는 테두리를 겹치지 않는다.
        const ui_element* const target { plan.highlight_target ? find(drag.hovered_drop_target) : nullptr };
        if (target != nullptr && (target->drop() == nullptr || target->drop()->custom_highlight == false))
        {
            if (const std::optional<rect_f> target_bounds { visible_bounds(*target) }; target_bounds.has_value())
            {
                SkPaint highlight { solid_paint(context.palette.accent) };
                highlight.setStyle(SkPaint::kStroke_Style);
                highlight.setStrokeWidth(1.0f * scale);
                const float radius { context.metrics.control_corner_radius * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(target_bounds->x, target_bounds->y, target_bounds->width, target_bounds->height), radius, radius), highlight);
            }
        }

        // 출발 쪽이 자기 방식으로 그리는 끌기는 여기서 멈춘다.
        // 강조를 그린 **뒤**여야 한다 — 위에서 돌아서면 밖에서 온 파일 끌기가
        // 놓을 자리를 잃는다 (os-dragdrop-design.md).
        if (plan.ghost == false)
            return;

        // 포인터를 따라다니는 ghost다.
        // 원본 element의 축소 표시로 충분하다.
        draw_drag_ghost_box(context, drag.payload, SkRect::MakeXYWH(drag.x + 10.0f * scale, drag.y + 10.0f * scale, 112.0f * scale, 24.0f * scale), 0u);
    }
}

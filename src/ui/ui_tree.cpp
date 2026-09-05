#include "luil/ui/ui_tree.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <utility>

namespace luil {
    ui_tree::ui_tree(std::unique_ptr<ui_element> root)
        : root_ { std::move(root) }
    {
        if (root_ != nullptr)
        {
            index_element(*root_, nullptr, root_->visible());
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

    const ui_element* ui_tree::find_drop_target(const float x, const float y, const drag_payload& payload) const
    {
        return root_ != nullptr ? root_->find_drop_target(x, y, payload) : nullptr;
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

    ui_tree make_arranged_tree(std::unique_ptr<ui_element> root, const rect_f& slot, const float scale)
    {
        if (root != nullptr)
            root->arrange({ slot, scale });
        return ui_tree { std::move(root) };
    }

    ui_cursor ui_tree::cursor_at(const float x, const float y, const interaction_snapshot& interaction) const
    {
        // 끌고 있는 동안은 포인터가 무엇 위에 있든 잡은 쪽이 모양을 정한다.
        //  - 놓을 자리를 찾아 헤매는 동안 모양이 깜빡이지 않는다.
        if (interaction.drag.has_value())
        {
            const ui_element* const source { find(interaction.drag->payload.source) };
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

    bool ui_tree::within(const ui_element_id& ancestor, const ui_element_id& id) const
    {
        const ui_element* const root { find(ancestor) };
        return root != nullptr && contains_id(*root, id);
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
        if (interaction.drag.has_value() == false && interaction.hover_started_at.has_value() && context.now - *interaction.hover_started_at < tooltip_delay)
        {
            const ui_element* const hovered { find(interaction.hovered) };
            if (hovered != nullptr && hovered->tooltip().empty() == false)
                merge(*interaction.hover_started_at + tooltip_delay);
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

    void ui_tree::index_element(const ui_element& element, const ui_element* access_parent, const bool visible_path)
    {
        index_.push_back(&element);
        kinds_[static_cast<std::uint32_t>(element.id().kind)].push_back(element.id());
        // 같은 id는 첫 등록이 이긴다.
        // 뒤쪽 것은 중복 목록에 남아 test·진단이 잡아낸다.
        if (lookup_.emplace(element.id(), &element).second == false)
            duplicates_.push_back(element.id());
        if (element.visible() && element.arranged() == false)
            unarranged_.push_back(element.id());

        // 접근 색인은 보이는 경로 위의 것만 담는다 (`contains_id`와 같은 규칙 —
        // 보이지 않는 것은 읽을 자리도 아니다). 구조(`none`)는 형제 줄에 서지
        // 않고 그 자식이 이 부모 자리로 승격된다 (`access_children`와 같은 접기).
        const ui_element* next_access_parent { access_parent };
        if (visible_path)
        {
            visible_index_.push_back(&element);
            std::size_t position { no_access_position };
            if (element.accessibility().role != access_role::none)
            {
                std::vector<const ui_element*>& siblings { access_parent != nullptr ? access_children_[access_parent] : access_top_ };
                position = siblings.size();
                siblings.push_back(&element);
                next_access_parent = &element;
            }
            access_index_.emplace(element.id(), access_entry { access_parent, position });
        }
        for (const std::unique_ptr<ui_element>& child : element.children())
            index_element(*child, next_access_parent, visible_path && child->visible());
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
    } // namespace

    std::optional<rect_f> ui_tree::visible_bounds(const ui_element& target) const
    {
        if (root_ == nullptr)
            return std::nullopt;
        return visible_bounds_of(*root_, target, std::nullopt);
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
        const std::optional<rect_f> box { visible_bounds(*focused) };
        if (box.has_value() == false)
            return;

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

    void ui_tree::draw_tooltip(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 끄는 동안에는 hover가 유지되어도 tooltip을 띄우지 않는다.
        // 놓을 자리를 찾는 중에 설명이 끼어들 일이 아니다.
        if (interaction.drag.has_value())
            return;
        if (interaction.hover_started_at.has_value() == false || context.now - *interaction.hover_started_at < tooltip_delay)
            return;
        const ui_element* const hovered { find(interaction.hovered) };
        if (hovered == nullptr || hovered->tooltip().empty())
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
        const std::u8string& text { hovered->tooltip() };
        const float text_width { measure_text(text, font) };
        const float padding { 5.0f * scale };
        const float box_width { text_width + padding * 2.0f };
        const float box_height { 22.0f * scale };
        const rect_f window { root_->bounds() };
        const rect_f anchor { hovered->bounds() };

        // 기본은 대상 아래이고, 창을 벗어나면 위로 뒤집고 좌우는 창 안으로 민다.
        float box_x { anchor.x };
        float box_y { anchor.y + anchor.height + 3.0f * scale };
        if (box_y + box_height > window.y + window.height)
            box_y = anchor.y - box_height - 3.0f * scale;
        box_x = std::clamp(box_x, window.x, std::max(window.x, window.x + window.width - box_width));

        const SkRect box { SkRect::MakeXYWH(box_x, box_y, box_width, box_height) };
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

    void ui_tree::draw_drag_visual(draw_context& context, const drag_visual& drag) const
    {
        const drag_overlay_plan plan { plan_drag_overlay(drag.payload) };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // 수락 중인 drop 대상을 먼저 강조한다.
        // 대상이 자르는 컨테이너에 걸쳐 있으면 보이는 만큼만 테두리를 친다.
        //  - 오버레이는 clip 밖에 그려지므로 원시 bounds를 쓰면 창 밖으로 샌다.
        const ui_element* const target { plan.highlight_target ? find(drag.hovered_drop_target) : nullptr };
        if (target != nullptr)
        {
            if (const std::optional<rect_f> target_bounds { visible_bounds(*target) }; target_bounds.has_value())
            {
                SkPaint highlight { solid_paint(context.palette.accent) };
                highlight.setStyle(SkPaint::kStroke_Style);
                highlight.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(target_bounds->x, target_bounds->y, target_bounds->width, target_bounds->height), 3.0f * scale, 3.0f * scale), highlight);
            }
        }

        // 출발 쪽이 자기 방식으로 그리는 끌기는 여기서 멈춘다.
        // 강조를 그린 **뒤**여야 한다 — 위에서 돌아서면 밖에서 온 파일 끌기가
        // 놓을 자리를 잃는다 (os-dragdrop-design.md).
        if (plan.ghost == false)
            return;

        // 포인터를 따라다니는 ghost다.
        // 원본 element의 축소 표시로 충분하다.
        const SkRect ghost { SkRect::MakeXYWH(drag.x + 10.0f * scale, drag.y + 10.0f * scale, 112.0f * scale, 24.0f * scale) };
        SkPaint fill { solid_paint(context.palette.surface_background) };
        fill.setAlphaf(0.85f);
        context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 3.0f * scale, 3.0f * scale), fill);
        SkPaint border { solid_paint(context.palette.accent) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 3.0f * scale, 3.0f * scale), border);

        const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        const std::u8string& ghost_text { drag.payload.label.empty() ? drag.payload.dragged_owner : drag.payload.label };
        draw_text(context.canvas, ghost_text, ghost.left() + 7.0f * scale, ghost.top() + centered_text_baseline(font, ghost.height()), font, foreground);
    }
} // namespace luil

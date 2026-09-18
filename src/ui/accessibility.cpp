#include "luil/ui/accessibility.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace luil {
    namespace {
        void collect_access_children(const ui_element& element, std::vector<const ui_element*>& children)
        {
            for (const std::unique_ptr<ui_element>& child : element.children())
            {
                if (child->visible() == false)
                    continue;
                // 구조는 접히고 그 자식이 이 자리로 승격된다.
                if (child->accessibility().role == access_role::none)
                {
                    collect_access_children(*child, children);
                    continue;
                }
                children.push_back(child.get());
            }
        }

        // 좌표 질의다.
        // nullopt는 "여기에는 없다", 값이 nullptr이면 "가려졌다"다 — scrim이
        // 흡수한 좌표를 그 아래에서 다시 찾으면 보이지 않는 것이 읽힌다.
        [[nodiscard]] std::optional<const ui_element*> find_access_at(const ui_element& element, const float x, const float y)
        {
            if (element.visible() == false)
                return std::nullopt;

            const bool inside { element.bounds().contains(x, y) };
            // 자르는 컨테이너 밖 좌표는 그 안을 보지 않는다 (hit test와 같은 규칙).
            if (element.clip_children() && inside == false)
                return std::nullopt;

            // 뒤에 추가된 자식이 위에 그려지므로 역순으로 검사한다.
            const std::span<const std::unique_ptr<ui_element>> children { element.children() };
            for (std::size_t index = children.size(); index > 0; --index)
            {
                if (const std::optional<const ui_element*> found { find_access_at(*children[index - 1], x, y) }; found.has_value())
                    return found;
            }

            if (inside == false)
                return std::nullopt;
            if (element.accessibility().role != access_role::none)
                return &element;
            if (element.hit_opaque())
                return nullptr;
            return std::nullopt;
        }
    } // namespace

    std::optional<std::vector<input_action>> plan_access_request(const ui_element& element, const access_request& request)
    {
        const access_info info { element.accessibility() };
        switch (request.command)
        {
        case access_command::invoke:
            // 누름에는 "이미"가 없다 — 할 수 있는지는 요소가 답한다.
            break;
        case access_command::toggle:
            // 뒤집기라 방향이 없다.
            if (info.checked.has_value() == false)
                return std::nullopt;
            break;
        case access_command::select:
            if (info.selected.has_value() == false)
                return std::nullopt;
            // 이미 골라져 있으면 할 일이 없다. 실행은 클릭 하나라 뒤집기이므로
            // 그대로 흘리면 도리어 고름이 풀리는 자리가 있다.
            if (*info.selected)
                return std::vector<input_action> {};
            break;
        case access_command::expand:
        case access_command::collapse: {
            if (info.expanded.has_value() == false)
                return std::nullopt;
            // UIA의 Expand는 "펼쳐진 상태로 만들라"이지 "펼침을 뒤집으라"가 아니다.
            if (*info.expanded == (request.command == access_command::expand))
                return std::vector<input_action> {};
            break;
        }
        case access_command::set_value:
            if (info.range.has_value() == false)
                return std::nullopt;
            break;
        }
        return element.access_actions(request);
    }

    std::vector<const ui_element*> access_children(const ui_element& element)
    {
        std::vector<const ui_element*> children {};
        collect_access_children(element, children);
        return children;
    }

    bool access_reachable(const ui_tree& tree, const ui_element_id& id)
    {
        // root부터 대상까지의 경로 전체가 보여야 한다 — 색인(find)은 숨은 부모
        // 아래 자식도 담으므로 대상 자신의 visible()만으로는 부족하다.
        if (tree.visibly_contains(id) == false)
            return false;
        // 가둠이 없으면 어디든 안이다 (`within_focus_trap`의 규약).
        if (tree.within_focus_trap(id))
            return true;

        // **가둠 밖이어도 포인터가 닿으면 자리다.**
        //
        // modal은 자기가 받은 자리만 덮는다. 그 밖에 남는 캡션 단추는 scrim이
        // 가리지 않아 사람이 그대로 누르는데, 가둠만 보고 거절하면 보조 기술만
        // 창을 닫지 못한다 — 규약이 뒤집힌다 ("보조 기술로 할 수 있는 일은
        // 사람이 할 수 있는 일의 부분집합"). 그래서 남은 질문을 포인터에게
        // 그대로 묻는다: 이 자리를 눌렀을 때 답이 이것인가.
        //  - **묻는 술어가 포인터의 것이어야 한다** (`ui_tree::hit_test`). 좌표
        //    질의(`access_element_at`)는 역할이 있는 것에서 멈추므로, 눌러도
        //    그대로 통과하는 이름표가 위에 떠 있기만 해도 사람은 누르는데 보조
        //    기술만 거절당한다. 같은 규약을 두 술어로 물으면 답이 갈린다.
        //  - 창 전체를 덮는 modal에서는 scrim이 답이라 여전히 거절이다 —
        //    `hit_opaque`라 hit을 흡수하고, 그래서 두 답이 저절로 갈린다.
        //  - **한 점으로는 모자란다.** 덮인 자리가 절반이면 사람은 드러난 쪽을
        //    누르므로, 가운데 한 점만 보면 그 단추를 거절한다. 상자를 아홉 자리로
        //    훑어 한 자리라도 닿으면 자리로 친다 — 가둠 밖이라는 드문 길에서만
        //    묻는 질문이라 훑는 값이 싸다.
        //  - 잘려서 화면 밖에 있는 것은 여기 오지 않는다. 가둠이 서 있고 그 밖에
        //    있다는 것은 이미 다른 것이 화면을 쥐고 있다는 뜻이다.
        const ui_element* const target { tree.find(id) };
        if (target == nullptr)
            return false;
        const std::optional<rect_f> box { tree.visible_bounds(*target) };
        if (box.has_value() == false || box->width <= 0.0f || box->height <= 0.0f)
            return false;
        constexpr std::array<float, 3> samples { 0.1f, 0.5f, 0.9f };
        for (const float along : samples)
        {
            for (const float down : samples)
            {
                const ui_element* const hit { tree.hit_test(box->x + box->width * along, box->y + box->height * down) };
                if (hit != nullptr && (hit->id() == id || tree.within(id, hit->id())))
                    return true;
            }
        }
        return false;
    }

    const ui_element* access_parent(const ui_tree& tree, const ui_element_id& id)
    {
        // tree가 생성 때 지은 색인이 답한다 — 여기서 root부터 다시 찾으면
        // 부모를 묻는 자리마다 tree 전체를 걷는다.
        return tree.access_parent_of(id);
    }

    const ui_element* access_sibling(const ui_tree& tree, const ui_element_id& id, const bool forward)
    {
        return tree.access_sibling_of(id, forward);
    }

    bool access_selection_container(const access_role role) noexcept
    {
        return role == access_role::list || role == access_role::tab_list || role == access_role::radio_group;
    }

    bool access_scroll_item(const ui_tree& tree, const ui_element_id& id)
    {
        // 흘리는 창 **안**에 있을 때만 뜻이 있다. 창이 없으면 들일 자리가 없다.
        const ui_element* const container { tree.scroll_container_of(id) };
        if (container == nullptr)
            return false;
        // **그 창이 자기 안에 세운 손잡이는 내용이 아니다.** 자기를 자기 안으로
        // 들이라는 명령은 없는 일이라 거른다. 거르는 것은 **그 창의** 막대뿐이라
        // 창의 직계 자식만 묻는다 — 내용은 언제나 안쪽 창(`scroll_view_element`)
        // 아래에 살고, 짧아서 스스로 창이 되지 못한 안쪽 목록의 막대는 바깥 창이
        // 들일 수 있는 자리라 그대로 대상이다.
        for (const std::unique_ptr<ui_element>& child : container->children())
        {
            if (child->id() == id)
                return child->accessibility().role != access_role::scroll_bar;
        }
        return true;
    }

    const ui_element* access_selection_container_of(const ui_tree& tree, const ui_element_id& id)
    {
        // 감싸는 접근 요소를 바깥으로 걷는다. 항목과 container 사이에 다른 접근
        // 요소가 끼는 조립(항목을 감싼 group 등)에서도 container를 찾는다.
        for (const ui_element* ancestor { access_parent(tree, id) }; ancestor != nullptr; ancestor = access_parent(tree, ancestor->id()))
        {
            if (access_selection_container(ancestor->accessibility().role))
                return ancestor;
        }
        return nullptr;
    }

    std::vector<const ui_element*> access_selected_items(const ui_element& container)
    {
        std::vector<const ui_element*> selected {};
        for (const ui_element* const child : access_children(container))
        {
            if (child->accessibility().selected == true)
                selected.push_back(child);
        }
        return selected;
    }

    namespace {
        // `ui_tree`의 색인과 같은 섞음이다.
        struct access_id_hash
        {
            [[nodiscard]] std::size_t operator()(const ui_element_id& id) const noexcept
            {
                return std::hash<std::u8string> {}(id.owner) ^ (static_cast<std::size_t>(id.kind) * 1099511628211ull);
            }
        };

        void collect_access_snapshot(const ui_element& element, const ui_element_id& parent, access_snapshot& snapshot)
        {
            for (const ui_element* const child : access_children(element))
            {
                snapshot.entries.push_back({ child->id(), parent, child->accessibility() });
                collect_access_snapshot(*child, child->id(), snapshot);
            }
        }

        // 발행 순서를 지킨 부모별 자식 목록이다 (빈 id 부모가 표면 root다).
        [[nodiscard]] std::unordered_map<ui_element_id, std::vector<ui_element_id>, access_id_hash> children_by_parent(const access_snapshot& snapshot)
        {
            std::unordered_map<ui_element_id, std::vector<ui_element_id>, access_id_hash> children {};
            for (const access_snapshot_entry& entry : snapshot.entries)
                children[entry.parent].push_back(entry.id);
            return children;
        }
    } // namespace

    access_snapshot make_access_snapshot(const ui_tree& tree)
    {
        access_snapshot snapshot {};
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return snapshot;
        // fragment root의 자식과 같은 규칙이다 — tree root가 구조(none)면 접혀
        // 그 자식이 최상위가 된다.
        if (root->accessibility().role != access_role::none)
        {
            snapshot.entries.push_back({ root->id(), {}, root->accessibility() });
            collect_access_snapshot(*root, root->id(), snapshot);
            return snapshot;
        }
        collect_access_snapshot(*root, {}, snapshot);
        return snapshot;
    }

    std::vector<access_change> diff_access_snapshots(const access_snapshot& previous, const access_snapshot& current)
    {
        std::vector<access_change> changes {};

        std::unordered_map<ui_element_id, const access_snapshot_entry*, access_id_hash> previous_by_id {};
        previous_by_id.reserve(previous.entries.size());
        for (const access_snapshot_entry& entry : previous.entries)
            previous_by_id.emplace(entry.id, &entry);

        for (const access_snapshot_entry& entry : current.entries)
        {
            const auto found { previous_by_id.find(entry.id) };
            // 새 요소는 부모의 structure 변화가 말한다.
            if (found == previous_by_id.end())
                continue;
            const access_info& before { found->second->info };
            if (before == entry.info)
                continue;
            // 역할이 바뀐 요소는 다른 요소다 — 부모의 structure 변화로 접는다
            // (자식 목록 비교가 그것을 잡지 못하므로 여기서 잡는다).
            if (before.role != entry.info.role)
            {
                changes.push_back({ access_change_kind::structure, entry.parent, {}, {} });
                continue;
            }
            // 새로 골라진 항목은 선택 알림이다 — 단일 선택이라 풀린 쪽은 이
            // 알림이 곧 말한다.
            if (before.selected != entry.info.selected && entry.info.selected == true)
                changes.push_back({ access_change_kind::selected, entry.id, {}, {} });
            // 선택 밖의 정보가 하나라도 바뀌었으면 property 하나로 알린다.
            // 어느 낱말들이 바뀌었는지는 받는 쪽이 이 짝에서 편다.
            access_info before_rest { before };
            access_info current_rest { entry.info };
            before_rest.selected.reset();
            current_rest.selected.reset();
            if (before_rest != current_rest)
                changes.push_back({ access_change_kind::property, entry.id, before, entry.info });
        }

        // 자식 목록(순서 포함)이 달라진 부모마다 structure 하나다.
        // 현재 발행본에 있는 부모만 알린다 — 사라진 부모는 그 조상의 변화가 말한다.
        const std::unordered_map<ui_element_id, std::vector<ui_element_id>, access_id_hash> before_children { children_by_parent(previous) };
        const std::unordered_map<ui_element_id, std::vector<ui_element_id>, access_id_hash> after_children { children_by_parent(current) };
        // 후보는 표면 root(빈 id — 최상위가 통째로 바뀌는 frame이 있다)와 현재
        // 발행본의 모든 요소다. 자식을 전부 잃은 부모도 여기서 잡힌다.
        std::vector<ui_element_id> parents { ui_element_id {} };
        for (const access_snapshot_entry& entry : current.entries)
            parents.push_back(entry.id);
        const std::vector<ui_element_id> empty {};
        for (const ui_element_id& parent : parents)
        {
            const auto after { after_children.find(parent) };
            const auto before { before_children.find(parent) };
            const std::vector<ui_element_id>& new_list { after != after_children.end() ? after->second : empty };
            const std::vector<ui_element_id>& old_list { before != before_children.end() ? before->second : empty };
            // structure 기록이 중복되지 않게, 위(property 걸음)에서 이미 낸 부모는
            // 건너뛴다 — role 변화가 이미 그 부모의 변화를 말했다.
            const bool already {
                std::find_if(changes.begin(), changes.end(),
                    [&parent](const access_change& change) { return change.kind == access_change_kind::structure && change.id == parent; })
                != changes.end(),
            };
            if (already == false && old_list != new_list)
                changes.push_back({ access_change_kind::structure, parent, {}, {} });
        }
        return changes;
    }

    const ui_element* access_element_at(const ui_tree& tree, const float x, const float y)
    {
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return nullptr;
        return find_access_at(*root, x, y).value_or(nullptr);
    }
} // namespace luil

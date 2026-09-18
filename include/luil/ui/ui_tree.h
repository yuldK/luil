#pragma once

#include "luil/ui/ui_element.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace luil {
    // 초점이 든 묶음이다.
    // 묶음 안이 아니면 `axis`가 `none`이고 `members`가 비어 있다.
    struct focus_group_scope
    {
        focus_axis axis { focus_axis::none };
        // 그리기 순서의 항목들이다 (안에 든 다른 묶음은 자리 하나로 접힌다).
        std::vector<ui_element_id> members {};
    };

    // 끌기 표시를 무엇까지 그릴지 고른다.
    // custom_visual은 ghost만 누른다 — 수락 대상의 강조는 밖에서 온 끌기에도 선다
    // (os-dragdrop-design.md).
    struct drag_overlay_plan
    {
        bool highlight_target { false };
        bool ghost { false };
    };

    // 표시 판정은 canvas와 독립적인 `plan_drag_overlay()`가 계산한다.
    // 렌더러와 테스트가 같은 payload 규칙을 사용하도록 그리기 단계와 분리한다.
    [[nodiscard]] drag_overlay_plan plan_drag_overlay(const drag_payload& payload) noexcept;

    // 배치가 끝난 element tree다.
    // 게시 후 불변이며 UI thread(그리기), input thread(hit test),
    // UI thread의 동기 조회(caption NC 클릭)가 공유한다.
    class ui_tree
    {
    public:
        explicit ui_tree(std::unique_ptr<ui_element> root);
        ui_tree(ui_tree&&) noexcept = default;
        ui_tree& operator=(ui_tree&&) noexcept = default;
        ui_tree(const ui_tree&) = delete;
        ui_tree& operator=(const ui_tree&) = delete;
        ~ui_tree() = default;

        // root element다. 빈 tree(null root)면 nullptr다 — 생성자가 null을
        // 허용하므로 다른 질의(find·hit_test)와 같은 규칙으로 답한다.
        [[nodiscard]] const ui_element* root() const noexcept;
        [[nodiscard]] const ui_element* hit_test(float x, float y) const;
        // 좌표에서 payload를 수락하는 가장 위의 drop 대상을 찾는다.
        // 일반 hit test와 달리 drop 대상이 아닌 element(예: 끌 수 있는 항목 위의 버튼)를 건너뛰고,
        // hit test와 같은 규칙으로 잘려 보이지 않는 자리는 대상이 아니다.
        [[nodiscard]] const ui_element* find_drop_target(float x, float y, const drag_payload& payload) const;
        // id로 찾는다 (해시 색인이라 크기와 무관하게 싸다).
        // 같은 id가 여럿이면 그리기 순서의 첫 element가 임자다 — 중복은
        // 앱 버그이므로 duplicate_ids로 드러난다.
        [[nodiscard]] const ui_element* find(const ui_element_id& id) const noexcept;
        // 색인 중 발견한 중복 id다 (그리기 순서, 중복된 뒤쪽 것만).
        // 비어 있어야 정상이다. find·hover·초점이 전부 id로 상태를 이으므로
        // 중복은 조용한 오동작(첫 것이 늘 이김)이 된다 — test가 이 값을 확인한다.
        [[nodiscard]] const std::vector<ui_element_id>& duplicate_ids() const noexcept;
        // 색인 중 발견한, **배치되지 않은 보이는 element**들이다 (그리기 순서).
        // 비어 있어야 정상이다. 배치 사슬은 어디서든 끊길 수 있고 끊긴 element는
        // bounds가 0이라 그려지지도 맞지도 않아 화면에서 조용히 사라진다 —
        // 중복 id와 같은 종류의 조용한 오동작이라 같은 방법으로 드러낸다
        // (tree-arrange-design.md).
        //  - 보이지 않는 element는 세지 않는다. 그리지도 맞지도 않으므로 배치할
        //    이유가 없다 (넘치지 않는 탭 막대의 넘침 버튼이 그렇다).
        [[nodiscard]] const std::vector<ui_element_id>& unarranged() const noexcept;
        // element가 조상의 잘라내기를 거치고 남는 화면 영역이다.
        // tree에 없거나 완전히 잘렸으면 nullopt다.
        // drag 오버레이가 잘린 부분에 강조를 그리지 않을 때 쓴다.
        [[nodiscard]] std::optional<rect_f> visible_bounds(const ui_element& target) const;
        // 그 자리에서 그릴 포인터 모양이다.
        // 잡고 끄는 중이면 포인터가 어디에 있든 잡은 element가 정하고,
        // 아니면 좌표 아래 element가 정한다.
        // 정한 것이 없으면 `inherit`이라 창의 기본 모양을 쓴다.
        [[nodiscard]] ui_cursor cursor_at(float x, float y, const interaction_snapshot& interaction) const;
        // 그리기 순서(pre-order)대로 해당 종류의 id를 모은다.
        // 키보드 탐색이 쓴다.
        [[nodiscard]] std::vector<ui_element_id> ids_of_kind(ui_element_kind kind) const;
        // 초점을 받을 수 있는 element의 id를 그리기 순서로 모은다.
        // Tab이 이 목록을 앞으로, Shift+Tab이 뒤로 돈다
        // (keyboard-focus-design.md).
        //  - **화면에 보이는 순서가 곧 도는 순서다.** 따로 매기는 번호를 두면
        //    배치와 순서가 두 곳에 살고 반드시 어긋난다.
        //  - 보이지 않는 가지는 통째로 건너뛴다 (hit test와 같은 규칙).
        //  - **묶음은 자리 하나로 접힌다.** 탭 막대·라디오 묶음처럼 항목이 줄선
        //    것은 Tab에서 한 자리이고 그 안은 화살표가 돈다
        //    (focus-group-design.md).
        [[nodiscard]] std::vector<ui_element_id> focus_order() const;
        // 이 자리를 감싸는 가장 안쪽 묶음이다.
        // 화살표가 그 안을 돌 때 쓴다 — 방향은 묶음이 정하고 순서는 자식 순서다.
        [[nodiscard]] focus_group_scope focus_group_of(const ui_element_id& id) const;
        // 초점을 가두는 element다 (없으면 nullptr).
        // modal dialog가 떠 있으면 Tab이 그 안만 돈다
        // (modal-dialog-design.md).
        //  - 여럿이면 **그리기 순서의 마지막**이 임자다. 나중에 그린 것이 위에
        //    있고, 겹친 dialog 중 위의 것이 키보드를 갖는다. 중첩(가둠 안의
        //    가둠)도 pre-order에서 뒤에 오므로 같은 규칙으로 풀린다.
        //  - 보이지 않는 가지의 가둠은 없는 것이다 (hit test와 같은 규칙).
        [[nodiscard]] const ui_element* focus_trap() const noexcept;
        // 가둠이 서면 초점이 설 자리다 (없으면 nullopt).
        // 가두는 것이 없거나 **가둠이 이름 짓지 않았으면 nullopt다** — 빈 이름이
        // "자동 초점이 없다"라, 자동 초점을 켜는 것은 언제나 앱의 뜻이다.
        //  - 이름 지은 자리가 지금 초점을 받을 수 없으면 묶음과 같은 규칙으로
        //    **가둠 안 첫 자리로 물러선다** (focus-entry-design.md).
        [[nodiscard]] std::optional<ui_element_id> focus_trap_entry() const;
        // 그 id가 가둠 안에 있는가.
        // 가둠이 없으면 언제나 참이다 — "가두는 것이 없으면 어디든 안이다".
        [[nodiscard]] bool within_focus_trap(const ui_element_id& id) const;
        // root부터 그 id까지의 경로가 전부 보이는가 (대상 자신의 표시도 포함).
        // 색인(lookup_)은 보이지 않는 가지도 담으므로 find로는 답할 수 없는
        // 질문이다 — 조상만 숨은 자식이 여기서 걸러진다 (hit test와 같은 규칙).
        // 접근 색인이 보이는 경로 위의 것만 담으므로 답은 색인에서 O(1)이다.
        [[nodiscard]] bool visibly_contains(const ui_element_id& id) const;
        // 최상위 접근 요소들이다 (생성 때 한 번 지은 색인).
        // root가 구조(`none`)면 접혀 그 자식들이고, 접근 요소면 root 하나다 —
        // 접근 tree의 통상 규칙 그대로다. 빈 tree(null root)면 비어 있다.
        [[nodiscard]] const std::vector<const ui_element*>& access_top_level() const noexcept;
        // 그 id를 감싸는 가장 안쪽 접근 요소다 (색인 답).
        // 없으면(최상위거나, 숨은 가지 위거나, tree에 없으면) nullptr다.
        [[nodiscard]] const ui_element* access_parent_of(const ui_element_id& id) const;
        // 접근 tree에서 같은 부모 아래 앞(`forward`)·뒤 형제다 (색인 답).
        // 끝이거나 형제 줄에 서지 않는 자리(구조·숨은 가지·없는 id)면 nullptr다.
        // 색인이 자리(부모·위치)를 들고 있어 목록 길이와 무관하게 싸다 —
        // 형제마다 root에서 부모를 재귀 탐색하면 긴 목록의 UIA 순회가
        // 형제 수의 제곱이 된다.
        [[nodiscard]] const ui_element* access_sibling_of(const ui_element_id& id, bool forward) const;
        // 이 요소를 감싸는 가장 안쪽 **흘리는 창**이다 (색인 답, 없으면 nullptr).
        // 보조 기술의 "이 자리를 화면에 들여라"가 설 자리가 있는지를 묻는다
        // (`route_reveal`이 실제로 얼마나 흘릴지 답한다).
        //  - 색인이 지을 때 적어 둔 값이라 O(1)이다. 형제마다 root에서 거슬러
        //    오르면 긴 목록의 UIA 순회가 제곱이 된다 (`access_sibling_of`와 같은
        //    이유다).
        [[nodiscard]] const ui_element* scroll_container_of(const ui_element_id& id) const;
        // 초점이 받지 못한 Enter가 갈 자리다 (없으면 nullptr).
        //  - 여럿이면 **그리기 순서의 마지막**이 임자다 (가둠과 같은 규칙).
        //  - **가둠이 서 있으면 그 안에 있어야 한다.** 밖의 기본 버튼은 없는
        //    것이다 — 초점을 거두는 규칙과 같은 문장이다
        //    (enter-default-design.md).
        //  - 보이지 않는 가지의 것은 없는 것이다 (hit test와 같은 규칙).
        [[nodiscard]] const ui_element* default_button() const noexcept;
        // `id`가 `ancestor`이거나 그 자손인지다.
        // 어느 element가 **자기 안에 둔 부품**을 누른 것인지를 묻는 자리다
        // (텍스트 칸 안의 지우기 버튼). 가둠 판정과 같은 걸음을 쓴다.
        [[nodiscard]] bool within(const ui_element_id& ancestor, const ui_element_id& id) const;
        // tree 전체에서 가장 이른 "다음 update 시각"이다.
        // element들의 답에 tree가 직접 그리는 tooltip(지연 경과)의 몫을 더한다.
        // nullopt면 시간이 흘러도 그대로라 platform이 timer를 걸지 않는다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const;

        // root와 자식을 그린 뒤 초점 테·tooltip·drag 표시를 최상위에 얹는다.
        void draw(draw_context& context, const interaction_snapshot& interaction) const;

    private:
        void index_element(const ui_element& element, const ui_element* scroll_container, const ui_element* access_parent, bool visible_path);
        static void collect_focus_stops(const ui_element& element, std::vector<ui_element_id>& order);
        // 묶음의 항목들이다 (그리기 순서). 안에 든 다른 묶음은 접힌다.
        static void collect_group_members(const ui_element& group, std::vector<ui_element_id>& members);
        // 묶음에 들어올 때 서는 자리다. 항목이 하나도 없으면 nullopt다.
        [[nodiscard]] static std::optional<ui_element_id> group_entry(const ui_element& group);
        // 그 id를 감싸는 가장 안쪽 묶음이다.
        // nullopt는 "찾지 못했다", 값이 nullptr이면 "찾았으나 묶음 밖"이다.
        [[nodiscard]] static std::optional<const ui_element*> find_focus_group(const ui_element& element, const ui_element_id& id, const ui_element* group);
        // 보이는 가지에서 가장 뒤의 가둠을 찾는다 (그리기 순서).
        static void find_focus_trap(const ui_element& element, const ui_element*& trap);
        // 보이는 가지에서 가장 뒤의 기본 버튼을 찾는다 (그리기 순서).
        static void find_default_button(const ui_element& element, const ui_element*& button);
        // element 아래에 그 id가 있는가 (보이지 않는 가지는 세지 않는다).
        [[nodiscard]] static bool contains_id(const ui_element& element, const ui_element_id& id);
        void draw_focus_ring(draw_context& context, const interaction_snapshot& interaction) const;
        void draw_tooltip(draw_context& context, const interaction_snapshot& interaction) const;
        void draw_drag_visual(draw_context& context, const drag_visual& drag) const;

        struct id_hash
        {
            [[nodiscard]] std::size_t operator()(const ui_element_id& id) const noexcept
            {
                // FNV 소수로 kind를 섞는다.
                return std::hash<std::u8string> {}(id.owner) ^ (static_cast<std::size_t>(id.kind) * 1099511628211ull);
            }
        };

        // 접근 tree에서의 자리다 — 생성 때 한 번 적어 둔다.
        struct access_entry
        {
            // 감싸는 가장 안쪽 접근 요소다 (없으면 nullptr — 그 자리는 창이다).
            const ui_element* parent { nullptr };
            // 이 자리를 감싸는 가장 안쪽 흘리는 창이다 (자기 자신은 세지 않는다).
            // 색인이 지을 때 적어 두므로 되묻는 값이 O(1)이고, 같은 id를 가진
            // element가 둘 있어도 흔들리지 않는다 (포인터로 내려온 값이다).
            const ui_element* scroll_container { nullptr };
            // 부모의 접근 자식 목록에서의 자리다.
            // 구조(`none`)는 줄에 서지 않으므로 `no_access_position`이다.
            std::size_t position { 0 };
        };
        static constexpr std::size_t no_access_position { static_cast<std::size_t>(-1) };

        std::unique_ptr<ui_element> root_ {};
        // 그리기 순서로 평탄화한 색인이다.
        // 화면에 걸친 element만 담기므로 작다.
        std::vector<const ui_element*> index_ {};
        // 색인 중 **보이는 경로 위의** element만이다 (그리기 순서).
        // `next_update`가 이것만 훑는다 — 숨긴 가지의 애니메이션은 화면에 그릴
        // 변화가 없으니 창을 깨울 이유도 없다 (hit test·접근 색인과 같은 규칙).
        std::vector<const ui_element*> visible_index_ {};
        // id → element 해시 색인이다. find가 문자열 선형 비교 대신 이것을 쓴다.
        // 같은 id는 첫 등록이 이긴다.
        std::unordered_map<ui_element_id, const ui_element*, id_hash> lookup_ {};
        // kind → 그리기 순서 id 목록이다. ids_of_kind가 쓴다.
        std::unordered_map<std::uint32_t, std::vector<ui_element_id>> kinds_ {};
        std::vector<ui_element_id> duplicates_ {};
        std::vector<ui_element_id> unarranged_ {};
        // 접근 tree 색인이다 (게시 후 불변 — lookup_과 같은 계약).
        // **보이는 경로 위의 것만** 담는다 (`contains_id`와 같은 규칙) — 담겨
        // 있음이 곧 `visibly_contains`의 답이고, 같은 id는 첫 등록이 이긴다.
        std::unordered_map<ui_element_id, access_entry, id_hash> access_index_ {};
        // 접근 요소 → 접근 자식들이다 (그리기 순서, `none` 접기를 거친 목록이라
        // `access_children`와 같은 답). 자식이 없는 요소는 담기지 않는다.
        std::unordered_map<const ui_element*, std::vector<const ui_element*>> access_children_ {};
        // 최상위 접근 요소들이다 (감싸는 접근 요소가 없어 부모가 창인 것들).
        std::vector<const ui_element*> access_top_ {};
        // 초점을 가두는 element다 (게시 때 한 번 찾는다).
        // 매 frame 묻는 자리(초점 갱신)가 있어 tree가 들고 있는다.
        const ui_element* trap_ { nullptr };
        // 기본 버튼이다 (게시 때 한 번 찾는다).
        // 가둠 밖의 것은 그때 걸러 두므로 여기 남는 것은 언제나 지금 닿을 수 있는
        // 자리다.
        const ui_element* default_button_ { nullptr };
    };

    // root를 이 자리에 배치한 뒤 tree로 만든다.
    // root가 자식까지 배치하는 tree(메뉴·패널·stack이 뿌리인 popup들)의 표준
    // 입구다 — "배치하고 감싼다"는 언제나 같은 두 줄이었다.
    //  - `root_element`처럼 호출자가 caption과 내용을 **각자** 배치해 담는
    //    tree는 지금처럼 생성자를 그대로 쓴다. 조립 방식이 실제로 다르고,
    //    빠뜨린 배치는 어느 쪽이든 `unarranged()`가 드러낸다.
    [[nodiscard]] ui_tree make_arranged_tree(std::unique_ptr<ui_element> root, const rect_f& slot, float scale);
} // namespace luil

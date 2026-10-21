#pragma once

#include "luil/ui/ui_events.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    class ui_element;
    class ui_tree;
    struct ui_element_id;

    // 보조 기술이 읽는 역할이다.
    // platform 어휘(UIA ControlType)가 아니라 이 저장소의 element가 실제로 하는
    // 일의 목록이다 — platform 번역은 win32 계층의 표 하나다
    // (accessibility-design.md).
    enum class access_role
    {
        // 구조일 뿐이다 (stack·panel·lane).
        // 접근 tree에서 접히고 자식이 이 자리로 승격된다.
        none,
        button,
        link,
        check_box,
        radio_button,
        toggle_switch,
        slider,
        scroll_bar,
        progress,
        // 판 크기를 조절하는 손잡이다.
        handle,
        image,
        edit,
        combo_box,
        list,
        list_item,
        header,
        tab_list,
        tab,
        // 서로 배타적인 선택지의 묶음이다 (`choice_group`의 라디오 스타일).
        // 항목(radio_button)의 선택 container라, 구조(none)로 접히지 않고 접근
        // tree에 선다 — SelectionItem의 짝인 Selection 패턴이 설 자리다.
        radio_group,
        menu,
        menu_item,
        group,
        title_bar,
        dialog,
        alert,
        static_text,
        pane,
        // 펼치고 접는 계층이다. 항목(tree_item)의 선택 container다 (목록과 같은 Selection 패턴).
        tree,
        // 계층의 줄 하나다. 깊이와 펼침은 `access_info::expanded`가 알린다.
        tree_item,
        // 행과 열이 있는 표다. 행(data_item)의 선택 container다.
        //  - UIA에서는 DataGrid다. 읽기만 하는 정적 표(Table)와 갈리는 자리는 행을 고를 수 있는가다 —
        //    luil의 표는 행을 고르는 컨트롤이라 DataGrid 하나로 둔다.
        table,
        // 표의 행 하나다.
        data_item,
        // 머리글 줄(`header`) 안의 칸 하나다 (표의 열 머리글).
        header_item,
        // 도구 단추들의 줄이다.
        tool_bar,
        // 구분선이다. 이름이 없고 실행할 것도 없다 — 보조 기술이 묶음의 경계를 읽는 자리다.
        separator,
        // 창 아래의 상태 줄이다.
        status_bar,
    };

    // 범위 값이다 (막대·스크롤 막대·진행률).
    struct access_range
    {
        float minimum { 0.0f };
        float maximum { 0.0f };
        float value { 0.0f };

        [[nodiscard]] bool operator==(const access_range&) const = default;
    };

    // 요소 하나가 보조 기술에 내주는 정보다.
    // 비활성·초점 가능 여부는 담지 않는다 — `enabled()`·`focusable()`이 이미
    // 공개라 같은 값을 두 곳에 두지 않는다.
    struct access_info
    {
        access_role role { access_role::none };
        std::u8string name {};
        std::optional<bool> checked {};
        // 일부만 켜진 체크다 (삼상 체크박스의 가운데 상태). 참이면 `checked`의 값과 무관하게 일부 상태로 읽힌다
        // (UIA ToggleState_Indeterminate). `checked`가 없는 요소에서는 뜻이 없다.
        bool mixed { false };
        std::optional<bool> selected {};
        std::optional<bool> expanded {};
        std::optional<access_range> range {};
        // 텍스트 칸의 글, 드롭다운의 현재 선택 같은 "지금 값"이다.
        std::u8string value {};
        // 암호 칸이다. 값은 화면처럼 가린 점이고, 보조 기술에 암호 칸으로 알린다 (UIA IsPassword).
        bool secret { false };
        // 선택 container(`access_selection_container`)가 여러 항목을 함께 고른다.
        //  - 켜진 container는 골라진 항목 모두를 내주고(UIA `GetSelection`), 비워 둘 수 있다 (`IsSelectionRequired`
        //    거짓). 그 항목의 더하기·빼기·고르기가 각각 `access_command::add_to_selection`·`remove_from_selection`·
        //    `select`로 요소에 온다 (`access_request::multiple_selection`이 참).
        //  - 항목에는 뜻이 없다. 항목은 지금처럼 `selected`만 답한다.
        bool multiple_selection { false };

        [[nodiscard]] bool operator==(const access_info&) const = default;
    };

    // 보조 기술이 시키는 실행이다.
    // 읽기(`access_info`)의 짝이고, 어휘가 platform을 모르는 것도 같다 —
    // UIA 패턴으로의 번역은 win32 계층의 표 하나다
    // (accessibility-action-design.md).
    enum class access_command
    {
        // 눌러라.
        invoke,
        // 켬/끔을 뒤집어라.
        toggle,
        // 골라라.
        select,
        // 펼쳐라 / 접어라.
        expand,
        collapse,
        // 값을 이것으로 하라.
        set_value,
        // 이 항목을 고른 것에 더하라 / 고른 것에서 빼라 (다중 선택 container의 항목).
        // 목표 상태를 나르는 절대 명령이다 — 이미 그 상태면 할 일이 없다. 기본 실행(클릭)이 없어 요소가
        // 답하지 않으면 할 수 없는 일이다 (`ui_element::set_access_handler`).
        add_to_selection,
        remove_from_selection,
    };

    struct access_request
    {
        access_command command { access_command::invoke };
        // `set_value`일 때의 목표 값이다. 그 밖의 명령에는 뜻이 없다.
        float value { 0.0f };
        // 선택 명령(`select`·`add_to_selection`·`remove_from_selection`)의 항목을 감싼 container가 여러 항목을
        // 함께 고른다 (`access_info::multiple_selection`). 부르는 쪽(provider)이 container에 물어 싣는다.
        //  - 참이면 `select`는 "이것 **하나만** 골라라"라 이미 골라져 있어도 요소에 간다 — 다른 항목이 함께
        //    골라져 있을 수 있고, 그것을 아는 것은 앱이다.
        bool multiple_selection { false };

        [[nodiscard]] bool operator==(const access_request&) const = default;
    };

    // 그 실행이 이 요소에서 무슨 액션인가.
    //
    // 답의 규약은 `key_step_target::on_step`과 같다 — **없으면(nullopt) 이 자리에서
    // 할 수 없는 일이고, 빈 목록이면 내 것이지만 할 일이 없다**(이미 그 상태다).
    // 판정은 요소가 이미 내주는 `accessibility()`로 하므로 "무엇을 답했는가"와
    // "무엇을 할 수 있는가"가 어긋날 수 없다.
    //  - `select`·`expand`·`collapse`는 **방향을 본다.** 이 저장소의 실행은 전부
    //    클릭 하나라 뒤집기인데, UIA의 Expand는 "펼쳐진 상태로 만들라"이지
    //    "펼침을 뒤집으라"가 아니다.
    //  - **비활성은 보지 않는다.** `enabled()`는 info에 싣지 않고 부르는 쪽이 직접
    //    읽는 값이고(accessibility-design.md), 어느 낱말로 거절할지를 아는 것도
    //    그쪽이다 (accessibility-action-design.md).
    [[nodiscard]] std::optional<std::vector<input_action>> plan_access_request(const ui_element& element, const access_request& request);

    // 접근 tree의 자식이다.
    // `none`은 접히고 그 자식이 이 자리로 승격되며, 보이지 않는 가지는 통째로
    // 건너뛴다 — hit test와 같은 규칙이다. 보이지 않는 것은 읽을 자리도 아니다.
    [[nodiscard]] std::vector<const ui_element*> access_children(const ui_element& element);

    // 지금 사람의 손이 닿는 자리인가.
    // 보이지 않는 것은 읽을 자리도 실행할 자리도 아니고, 가둠(modal)이 서 있으면
    // 그 안이 자리다 — scrim이 포인터를 막고 가둠이 키보드를 막는 그 경계를
    // 보조 기술도 함께 지난다 (accessibility-action-design.md: "UIA로 할 수
    // 있는 일은 사람이 할 수 있는 일의 부분집합이다").
    //  - **가둠 밖이어도 포인터가 닿으면 자리다.** modal은 자기가 받은 자리만
    //    덮으므로 그 밖에 남는 캡션 단추는 사람이 그대로 누른다. 가둠만 보고
    //    거절하면 보조 기술만 창을 닫지 못해 규약이 거꾸로 선다. 창 전체를 덮는
    //    modal에서는 scrim이 좌표의 답이라 여전히 거절이다.
    //    묻는 술어는 **사람이 쓰는 그것**이다 (`ui_tree::hit_test`) — 좌표
    //    질의(`access_element_at`)는 역할이 있는 것에서 멈추므로 눌러도 통과하는
    //    이름표 하나에 답이 갈린다. 상자는 아홉 자리로 훑는다. 절반만 덮인
    //    단추는 사람이 드러난 쪽을 누르기 때문이다.
    //  - root부터 대상까지의 경로 전체가 보여야 한다 — 조상만 숨은 자식도 여기서
    //    걸러진다 (탐색 `access_children`이 거르는 것과 같은 답이다). 이 술어는
    //    **id로 곧장 묻는 길**의 몫이다 — 클라이언트가 쥔 provider는 tree 재빌드를
    //    넘어 살아남는다.
    //  - 잘려서 화면 밖에 있는 것은 막지 않는다. 스크롤로 밀려난 행은 키보드로
    //    닿을 수 있고, 닿으면 화면 안으로 들어온다 (focus-reveal-design.md).
    [[nodiscard]] bool access_reachable(const ui_tree& tree, const ui_element_id& id);

    // 그 id를 감싸는 가장 안쪽 접근 요소다.
    // 없으면(최상위거나 tree에 없으면) nullptr다 — 그 자리는 창(fragment root)이다.
    [[nodiscard]] const ui_element* access_parent(const ui_tree& tree, const ui_element_id& id);

    // 접근 tree에서 같은 부모 아래 앞(`forward`)·뒤 형제다.
    // 끝이거나 형제 줄에 서지 않는 자리(구조·숨은 가지·없는 id)면 nullptr다.
    // tree가 생성 때 지은 색인으로 답하므로 목록 길이와 무관하게 싸다 —
    // 형제마다 root에서 부모를 재귀 탐색하면 긴 목록의 UIA 순회가 형제 수의
    // 제곱이 된다.
    [[nodiscard]] const ui_element* access_sibling(const ui_tree& tree, const ui_element_id& id, bool forward);

    // 이 역할이 선택 항목들을 담는 container인가 (목록·탭 막대·라디오 묶음·계층·표).
    // SelectionItem(`selected`)의 짝인 Selection 패턴이 서는 자리다 — 항목의
    // container 질의(`access_selection_container_of`)와 패턴 광고가 같은 술어를
    // 쓴다 (accessibility-action-design.md의 `ISelectionProvider`).
    [[nodiscard]] bool access_selection_container(access_role role) noexcept;

    // 이 자리를 흘리는 창 안으로 **들일 수 있는가** (`ui_tree::scroll_container_of`).
    // 감싸는 창이 없으면 들일 자리도 없다.
    //  - **그 창의 막대는 그 창의 내용이 아니다.** 창이 자기 안에 세우는 손잡이라
    //    배치로는 창 안에 있지만, 자기를 자기 안으로 들이라는 명령은 없는 일이다.
    //  - 거르는 것은 **그 창이 세운** 막대뿐이다. 짧아서 스스로 창이 되지 못한
    //    안쪽 목록의 막대는 바깥 창이 들일 수 있는 자리라 그대로 대상이다 —
    //    역할만 보고 막대를 통째로 거르면 그 자리를 함께 잃는다.
    // ScrollItem을 내걸 때와 실행할 때가 같은 술어를 쓴다
    // (accessibility-action-design.md).
    [[nodiscard]] bool access_scroll_item(const ui_tree& tree, const ui_element_id& id);

    // 그 id를 감싸는 가장 안쪽 **선택 container**다. 없으면 nullptr다 —
    // 홀로 선 라디오(`check_element`)처럼 묶음 밖의 선택 항목이 그렇다.
    // 감싸는 접근 요소가 container가 아니면(패턴 없는 부모를 container로 내주면
    // 계약이 깨진다) 더 바깥으로 걸어 올라간다.
    [[nodiscard]] const ui_element* access_selection_container_of(const ui_tree& tree, const ui_element_id& id);

    // container의 접근 자식 중 지금 골라진 항목들이다 (발행 순서 그대로).
    // 단일 선택 container는 0개 또는 1개이고, 다중 선택 container(`access_info::multiple_selection`)는 골라진 것
    // 모두다.
    [[nodiscard]] std::vector<const ui_element*> access_selected_items(const ui_element& container);

    // 그 항목을 감싼 선택 container가 여러 항목을 함께 고르는가다. container가 없으면 거짓이다.
    [[nodiscard]] bool access_multiple_selection(const ui_tree& tree, const ui_element_id& id);

    // 접근 tree의 발행본 하나를 편 것이다 — 접기(none)와 숨김을 지난 요소들이
    // 게시 순서(pre-order)로 늘어선다. tree는 frame마다 통째로 새로 지어지므로,
    // "무엇이 바뀌었는가"는 이전 발행본의 사본과 견줘야만 알 수 있다 —
    // 값·상태·구조 변경 알림의 재료다.
    struct access_snapshot_entry
    {
        ui_element_id id {};
        // 감싸는 접근 요소다. 빈 id면 표면 root 바로 아래다.
        ui_element_id parent {};
        access_info info {};

        [[nodiscard]] bool operator==(const access_snapshot_entry&) const = default;
    };

    struct access_snapshot
    {
        std::vector<access_snapshot_entry> entries {};

        [[nodiscard]] bool operator==(const access_snapshot&) const = default;
    };

    [[nodiscard]] access_snapshot make_access_snapshot(const ui_tree& tree);

    // 두 발행본 사이에서 보조 기술에 알릴 변화 하나다.
    enum class access_change_kind
    {
        // 같은 요소의 정보(이름·값·켬끔·펼침·범위 값)가 바뀌었다.
        property,
        // 이 항목이 골라졌다 — 단일 선택 container에서는 새로 골라진 항목이다 (풀린 쪽은 이 알림이 말한다).
        // 다중 선택 container에서는 이 항목 **하나만** 골라진 상태가 됐다 (UIA ElementSelected의 뜻).
        selected,
        // 이 요소(빈 id면 표면 root)의 접근 자식 구성이 바뀌었다.
        structure,
        // 다중 선택 container의 이 항목이 고른 것에 더해졌다 / 빠졌다.
        added_to_selection,
        removed_from_selection,
        // 이 다중 선택 container의 선택이 한꺼번에 많이 바뀌었다 (`access_selection_burst`보다 많이).
        // 항목마다 알리지 않고 다시 읽으라고 한다 (UIA Selection_Invalidated).
        selection_invalidated,
    };

    // 다중 선택 container 하나의 선택 변화를 항목마다 알리는 한계다. 넘으면 `selection_invalidated` 하나다.
    // UIA가 권하는 값(MaxSelectionChanges)과 같다.
    inline constexpr std::size_t access_selection_burst { 20 };

    struct access_change
    {
        access_change_kind kind { access_change_kind::property };
        // property·selected는 그 요소, structure는 부모다 (빈 id면 표면 root).
        ui_element_id id {};
        // property일 때의 이전·현재 정보다. 어느 낱말(UIA property)로 알릴지는
        // platform 계층이 이 짝을 견줘 정한다 — 어휘는 platform을 모른다.
        access_info previous {};
        access_info current {};

        [[nodiscard]] bool operator==(const access_change&) const = default;
    };

    // 이전/현재 접근 발행본을 견줘 알릴 변화를 편다.
    // 순서는 property·selected(현재 발행본의 게시 순서) 뒤에 다중 선택(container 차례), 그 뒤에 structure다.
    //  - 다중 선택 container의 항목은 골라짐이 바뀐 것을 container마다 모은다. 바뀐 것이 `access_selection_burst`
    //    보다 많으면 container의 `selection_invalidated` 하나, 지금 골라진 것이 바뀐 항목 하나뿐이고 그것이
    //    새로 골라졌으면 그 항목의 `selected` 하나, 그 밖에는 항목마다 `added_to_selection`·`removed_from_selection`
    //    이다. 두 발행본 모두에 있는 항목만 본다 — 들고 나는 항목은 structure가 말한다.
    //  - 역할(role)이 바뀐 요소는 다른 요소가 된 것이다 — property가 아니라
    //    부모의 structure 변화로 접는다.
    //  - 새로 나타나거나 사라진 요소도 부모의 structure 변화 하나로 접는다.
    //    사라진 부모 자신의 기록은 내지 않는다 — 그 조상의 변화가 이미 말한다.
    [[nodiscard]] std::vector<access_change> diff_access_snapshots(const access_snapshot& previous, const access_snapshot& current);

    // 좌표를 덮는 가장 위의 접근 요소다.
    // `hit_test`와 달리 읽기 전용 요소(라벨·진행률)도 맞힌다 — 그쪽의 술어
    // (`interactive() || hit_opaque()`)로는 읽을 것이 빠진다.
    // hit를 흡수하는 자리(scrim)에 가리면 아무것도 아니다 (nullptr).
    [[nodiscard]] const ui_element* access_element_at(const ui_tree& tree, float x, float y);
} // namespace luil

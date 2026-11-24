#pragma once

#include "luil/ui/accessibility.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_element_id.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    class ui_tree;
}

// VoiceOver(NSAccessibility) 다리의 AppKit을 모르는 계산이다 (accessibility_macos.mm이 쓴다). core test로 돈다.
//  - 어휘(`access_info`)를 AX 낱말로 옮기는 표, 값·상태·실행의 번역, 좌표, UTF-16 offset, 발행본 대조를 알림으로 옮기기다.
//    UIA provider(win32/uia_provider.cpp)가 같은 어휘를 UIA로 옮기는 자리의 짝이다 (accessibility-design.md).
//  - AX 낱말은 AppKit 상수의 값 그대로다 (`NSAccessibilityButtonRole`은 "AXButton"). AppKit 없이 견줄 수 있게 글로 든다.
namespace luil::macos {
    // AX 역할과 하위 역할이다. 하위 역할이 비면 없다.
    struct ax_role_names
    {
        std::string_view role {};
        std::string_view subrole {};

        [[nodiscard]] bool operator==(const ax_role_names&) const = default;
    };

    // 역할만으로 갈리지 않는 것이다. 다리가 요소와 tree에서 읽어 채운다.
    struct ax_role_context
    {
        // 글을 받는 칸이다 (`ui_element::text_input`이 있다). combo_box가 AXComboBox와 AXPopUpButton으로 갈린다.
        bool text_input { false };
        // 여러 줄 칸이다 (`text_input_snapshot::multiline`). AXTextArea가 된다.
        bool multiline { false };
        // 감싸는 접근 요소의 역할이다. 메뉴 막대 안의 항목이 AXMenuBarItem이 된다.
        std::optional<access_role> parent {};
    };

    // 어휘의 역할을 AX 역할로 옮기는 표다. 여기 한 곳뿐이다 (UIA의 `control_type_for`와 같은 자리다).
    [[nodiscard]] ax_role_names ax_role_of(const access_info& info, const ax_role_context& context = {}) noexcept;

    // 이름을 값(AXValue)으로 읽는 역할인가 (AXStaticText로 서는 글).
    //  - VoiceOver는 글의 AXValue를 읽는다. 이름을 label로도 주면 두 번 읽는다 — label은 비운다.
    [[nodiscard]] bool ax_reads_name_as_value(access_role role) noexcept;

    // 행(AXRow)으로 서는 역할인가. 목록 항목·계층 항목·표의 행이다. 그 그릇(목록·계층·표)이 행 목록(AXRows)과 고른 행을 답한다.
    [[nodiscard]] bool ax_row_role(access_role role) noexcept;
    // 행 목록을 답하는 그릇인가 (AXTable·AXOutline).
    [[nodiscard]] bool ax_row_container_role(access_role role) noexcept;

    enum class ax_value_kind
    {
        none,
        number,
        text,
    };

    // AXValue다. 체크는 0·1·2(일부), 라디오·탭은 0·1, 범위는 지금 값, 글은 글이다.
    struct ax_value
    {
        ax_value_kind kind { ax_value_kind::none };
        double number { 0.0 };
        std::u8string text {};

        [[nodiscard]] bool operator==(const ax_value&) const = default;
    };

    [[nodiscard]] ax_value ax_value_of(const access_info& info);
    // AXDescription(label)이다. 이름을 값으로 읽는 역할은 비어 있다.
    [[nodiscard]] std::u8string_view ax_label_of(const access_info& info) noexcept;
    // AXHelp다. 입력 칸의 오류 글이 있으면 그것이 먼저다 (UIA HelpText와 같은 글).
    [[nodiscard]] std::u8string_view ax_help_of(const access_info& info) noexcept;
    // 한 칸 오르내림의 걸음이다. 요소가 걸음을 알리지 않으면(0) 범위의 20분의 1이다. 범위가 없으면 0이다.
    [[nodiscard]] float ax_range_step(const access_range& range) noexcept;

    // 보조 기술이 요소에 시키는 실행이다 (NSAccessibility의 perform 동작).
    enum class ax_action
    {
        press,
        increment,
        decrement,
        confirm,
        cancel,
        show_menu,
    };

    // 실행 하나가 이 요소에서 무슨 명령인가. nullopt면 이 자리에 없는 실행이다 (`isAccessibilitySelectorAllowed:`가 거짓).
    //  - press는 상태를 든 것이면 그 상태의 명령이다: 체크는 뒤집기, 선택 항목은 고르기, 펼침은 반대쪽이다. 상태가
    //    없고 누르는 역할(단추·링크·메뉴 항목·머리글 칸)이면 누름이다. UIA의 `invokable`과 같은 갈래다.
    //  - `multiple_selection`은 감싼 선택 container가 여럿을 고르는가다 (`access_multiple_selection`).
    //  - confirm은 여기서 답하지 않는다. 한 줄 칸의 확정은 Enter 키라 다리가 따로 낸다 (`ax_confirms_with_enter`).
    [[nodiscard]] std::optional<access_request> ax_action_request(const access_info& info, ax_action action, bool multiple_selection = false) noexcept;
    // AXConfirm이 Enter 키인 칸인가 (한 줄 텍스트 칸).
    [[nodiscard]] bool ax_confirms_with_enter(const access_info& info, const ax_role_context& context) noexcept;
    // AXValue 쓰기가 범위 값의 명령이 되는가. 값은 범위로 자른다 (VoiceOver는 경계 밖을 보내지 않지만 자르는 쪽이 안전하다).
    [[nodiscard]] std::optional<access_request> ax_range_request(const access_info& info, double value) noexcept;
    // AXDisclosing·AXExpanded 쓰기다. 펼침 상태가 없으면 nullopt다.
    [[nodiscard]] std::optional<access_request> ax_disclosure_request(const access_info& info, bool expand) noexcept;
    // AXSelected 쓰기다. 다중 선택 container의 항목은 더하기·빼기, 그 밖은 고르기(참일 때만)다.
    [[nodiscard]] std::optional<access_request> ax_selection_request(const access_info& info, bool select, bool multiple_selection) noexcept;

    // tree 하나가 view 안에 앉은 자리다. 주 tree는 원점 (0, 0)이고 자르지 않는다. popup은 판의 자리다.
    struct ax_placement
    {
        // tree 원점이 표면에서 앉은 자리다 (물리 픽셀). popup은 판의 왼쪽 위에서 흘린 양을 뺀 자리다.
        float origin_x { 0.0f };
        float origin_y { 0.0f };
        // 보이는 판이다 (표면 물리 픽셀). 없으면 자르지 않는다.
        std::optional<rect_f> clip {};
        // view 점 하나가 몇 물리 픽셀인가 (`backingScaleFactor`).
        float scale { 1.0f };

        [[nodiscard]] bool operator==(const ax_placement&) const = default;
    };

    // popup 판(표면 물리 픽셀)과 그 안에서 흘린 양으로 자리를 짓는다 (`overlay_entry::content_origin_y`와 같은 식).
    [[nodiscard]] ax_placement ax_overlay_placement(const rect_f& bounds, float scroll_y, float scale) noexcept;
    // tree 사각형(물리 픽셀)을 view 사각형(점, 왼쪽 위 원점)으로 옮긴다. 판 밖으로 다 잘리면 nullopt다.
    [[nodiscard]] std::optional<rect_f> ax_view_rect(const rect_f& bounds, const ax_placement& placement) noexcept;
    // view 점(점, 왼쪽 위 원점)을 tree 점(물리 픽셀)으로 옮긴다. 판 밖이면 nullopt다.
    [[nodiscard]] std::optional<point_f> ax_tree_point(const point_f& view, const ax_placement& placement) noexcept;

    // UTF-8 byte offset과 UTF-16 offset(NSString의 색인) 사이를 옮긴다. 글 밖은 끝으로 잘린다.
    //  - 문자 가운데의 byte offset은 그 문자의 앞으로 간다. 가운데(surrogate 짝 사이)의 UTF-16 offset은 그 문자의 앞으로 간다.
    [[nodiscard]] std::size_t ax_utf16_offset(std::u8string_view text, std::size_t byte_offset) noexcept;
    [[nodiscard]] std::size_t ax_utf8_offset(std::u8string_view text, std::size_t utf16_offset) noexcept;
    [[nodiscard]] std::size_t ax_utf16_length(std::u8string_view text) noexcept;
    // byte offset이 몇째 줄인가 (`ui_element::text_line_starts`). 줄 목록이 비면 0이다 (한 줄 칸).
    [[nodiscard]] std::size_t ax_line_of(const std::vector<std::size_t>& line_starts, std::size_t offset) noexcept;

    // 이 tree에서 보조 기술에 알릴 초점이다. `interaction`은 이 표면으로 거른 발행본이다 (`interaction_for_surface`).
    //  - 메뉴 막대가 키보드로 선 동안은 그 항목이다 (Win32 표면과 같은 규칙).
    //  - tree에 없으면 빈 id다.
    [[nodiscard]] ui_element_id ax_focus_of(const ui_tree& tree, const interaction_snapshot& interaction);

    // 보조 기술에 알릴 변화 하나다 (NSAccessibility 알림 하나).
    enum class ax_notification_kind
    {
        // AXValue가 바뀌었다 (글·체크·범위 값).
        value_changed,
        // 이름(label)이 바뀌었다.
        title_changed,
        // 이 선택 container의 골라진 자식이 바뀌었다.
        selected_children_changed,
        // 이 표·계층의 골라진 행이 바뀌었다.
        selected_rows_changed,
        // 이 행이 펼쳐졌다 / 접혔다.
        row_expanded,
        row_collapsed,
        // 이 요소(빈 id면 표면 root)의 자식 구성이 바뀌었다.
        layout_changed,
        // 이 글을 읽게 한다 (오류 글·live region).
        announcement,
    };

    enum class ax_priority
    {
        low,
        medium,
        high,
    };

    struct ax_notification
    {
        ax_notification_kind kind { ax_notification_kind::value_changed };
        ui_element_id id {};
        // announcement의 글이다.
        std::u8string text {};
        ax_priority priority { ax_priority::medium };

        [[nodiscard]] bool operator==(const ax_notification&) const = default;
    };

    // 이전/현재 접근 발행본의 대조(`diff_access_snapshots`)를 NSAccessibility 알림으로 옮긴다.
    //  - 같은 요소의 같은 알림은 한 번이다. 순서는 대조의 순서를 따른다 (구조 뒤에 읽을 글).
    //  - 선택은 항목이 아니라 감싼 container가 알린다 (AppKit의 SelectedChildrenChanged·SelectedRowsChanged).
    //  - 새로 생기거나 바뀐 오류 글은 높은 우선의 announcement다 (UIA의 notification과 같은 자리).
    //  - live region은 그 안의 글을 모아 announcement로 읽힌다. assertive는 높은, polite는 중간 우선이다.
    //  - 사라진 요소는 여기서 내지 않는다. 다리가 쥔 요소 객체를 지우며 알린다.
    [[nodiscard]] std::vector<ax_notification> ax_notifications(const access_snapshot& previous, const access_snapshot& current);

    // live region 하나가 읽힐 글이다. 그 요소와 안쪽 요소의 이름·값을 발행 순서로 잇는다.
    [[nodiscard]] std::u8string ax_live_text(const access_snapshot& snapshot, const ui_element_id& id);
}

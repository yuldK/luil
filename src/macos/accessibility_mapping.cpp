#include "macos/accessibility_mapping.h"

#include "luil/text/utf8_text.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luil::macos {
    namespace {
        struct id_hash
        {
            [[nodiscard]] std::size_t operator()(const ui_element_id& id) const noexcept
            {
                // `ui_tree`의 색인과 같은 섞음이다.
                return std::hash<std::u8string> {}(id.owner) ^ (static_cast<std::size_t>(id.kind) * 1099511628211ull);
            }
        };

        using snapshot_index = std::unordered_map<ui_element_id, std::size_t, id_hash>;

        [[nodiscard]] snapshot_index index_of(const access_snapshot& snapshot)
        {
            snapshot_index index {};
            index.reserve(snapshot.entries.size());
            for (std::size_t position { 0 }; position < snapshot.entries.size(); ++position)
                index.try_emplace(snapshot.entries[position].id, position);
            return index;
        }

        [[nodiscard]] const access_snapshot_entry* entry_of(const access_snapshot& snapshot, const snapshot_index& index, const ui_element_id& id)
        {
            const auto found { index.find(id) };
            return found != index.end() ? &snapshot.entries[found->second] : nullptr;
        }

        // 항목을 감싸는 가장 안쪽 선택 container다 (`access_selection_container_of`의 발행본 짝). 없으면 nullptr다.
        [[nodiscard]] const access_snapshot_entry* selection_container_of(const access_snapshot& snapshot, const snapshot_index& index, const ui_element_id& id)
        {
            const access_snapshot_entry* entry { entry_of(snapshot, index, id) };
            // 접근 tree의 깊이보다 오래 걷지 않는다 — 부모 사슬이 고리를 이루는 발행본에서도 끝난다.
            for (std::size_t depth { 0 }; entry != nullptr && depth <= snapshot.entries.size(); ++depth)
            {
                if (entry->parent == ui_element_id {})
                    return nullptr;
                entry = entry_of(snapshot, index, entry->parent);
                if (entry != nullptr && access_selection_container(entry->info))
                    return entry;
            }
            return nullptr;
        }

        // 상태 없이 누르는 역할이다. 텍스트 칸은 hit를 흡수하려고 빈 클릭을 들고 있어 역할로 거른다 (UIA의 `invokable`).
        [[nodiscard]] bool invoke_role(const access_role role) noexcept
        {
            return role == access_role::button || role == access_role::link || role == access_role::menu_item || role == access_role::header_item;
        }

        [[nodiscard]] bool row_role(const access_role role) noexcept
        {
            return role == access_role::tree_item || role == access_role::data_item;
        }

        // 선택 알림의 낱말이다. 표·계층은 행(AXSelectedRows)으로, 그 밖은 자식(AXSelectedChildren)으로 읽힌다.
        [[nodiscard]] ax_notification_kind selection_kind_of(const access_role container) noexcept
        {
            return container == access_role::tree || container == access_role::table ? ax_notification_kind::selected_rows_changed : ax_notification_kind::selected_children_changed;
        }

        void push_unique(std::vector<ax_notification>& notifications, ax_notification notification)
        {
            if (std::ranges::find(notifications, notification) == notifications.end())
                notifications.push_back(std::move(notification));
        }

        // 요소 하나가 읽히는 글이다 (이름, 이름과 다르면 값).
        void append_readable(std::u8string& text, const access_info& info)
        {
            const auto append = [&text](const std::u8string_view piece) {
                if (piece.empty())
                    return;
                if (text.empty() == false)
                    text += u8" ";
                text += piece;
            };
            append(info.name);
            if (info.value != info.name)
                append(info.value);
        }
    }

    ax_role_names ax_role_of(const access_info& info, const ax_role_context& context) noexcept
    {
        switch (info.role)
        {
        case access_role::button:
            return { "AXButton" };
        case access_role::link:
            return { "AXLink" };
        case access_role::check_box:
            return { "AXCheckBox" };
        case access_role::radio_button:
            return { "AXRadioButton" };
        // NSSwitch와 같다. 체크 상자에 스위치 하위 역할을 단다.
        case access_role::toggle_switch:
            return { "AXCheckBox", "AXSwitch" };
        case access_role::slider:
            return { "AXSlider" };
        case access_role::scroll_bar:
            return { "AXScrollBar" };
        case access_role::progress:
            return { "AXProgressIndicator" };
        // 판 크기를 조절하는 손잡이다 (NSSplitView의 분할선).
        case access_role::handle:
            return { "AXSplitter" };
        case access_role::image:
            return { "AXImage" };
        case access_role::edit:
            if (info.secret)
                return { "AXTextField", "AXSecureTextField" };
            return { context.multiline ? "AXTextArea" : "AXTextField" };
        // 글을 받으면 콤보 상자, 고르기만 하면 팝업 단추다 (NSComboBox·NSPopUpButton).
        case access_role::combo_box:
            return { context.text_input ? "AXComboBox" : "AXPopUpButton" };
        case access_role::list:
            return { "AXList" };
        // 목록 항목은 글로 서고 AXSelected로 골라짐을 알린다 (Chromium의 listbox option과 같은 관례다).
        case access_role::list_item:
            return { "AXStaticText" };
        case access_role::header:
            return { "AXGroup" };
        // 표의 열 머리글이다. 누르면 정렬하는 단추로 선다.
        case access_role::header_item:
            return { "AXButton" };
        case access_role::tab_list:
            return { "AXTabGroup" };
        // NSTabView의 탭과 같다.
        case access_role::tab:
            return { "AXRadioButton", "AXTabButton" };
        case access_role::radio_group:
            return { "AXRadioGroup" };
        case access_role::menu:
            return { "AXMenu" };
        case access_role::menu_item:
            return { context.parent == access_role::menu_bar ? "AXMenuBarItem" : "AXMenuItem" };
        // 흘리는 창(`access_info::scroll`)은 스크롤 영역이다. 그 밖의 판은 묶음이다.
        case access_role::pane:
            return { info.scroll.has_value() ? "AXScrollArea" : "AXGroup" };
        case access_role::group:
        case access_role::title_bar:
            return { "AXGroup" };
        // view 안에 그린 dialog·경고·상태 줄·길잡이는 창이 아니라 하위 역할을 단 묶음이다 (WebKit의 ARIA 관례와 같다).
        case access_role::dialog:
            return { "AXGroup", "AXApplicationDialog" };
        case access_role::alert:
            return { "AXGroup", "AXApplicationAlert" };
        case access_role::status_bar:
            return { "AXGroup", "AXApplicationStatus" };
        case access_role::navigation:
            return { "AXGroup", "AXLandmarkNavigation" };
        case access_role::static_text:
            return { "AXStaticText" };
        case access_role::tree:
            return { "AXOutline" };
        case access_role::tree_item:
            return { "AXRow", "AXOutlineRow" };
        case access_role::table:
            return { "AXTable" };
        case access_role::data_item:
            return { "AXRow", "AXTableRow" };
        case access_role::tool_bar:
            return { "AXToolbar" };
        case access_role::separator:
            return { "AXSplitter" };
        case access_role::menu_bar:
            return { "AXMenuBar" };
        case access_role::none:
            break;
        }
        return { "AXGroup" };
    }

    bool ax_reads_name_as_value(const access_role role) noexcept
    {
        return role == access_role::static_text || role == access_role::list_item;
    }

    ax_value ax_value_of(const access_info& info)
    {
        switch (info.role)
        {
        case access_role::check_box:
        case access_role::toggle_switch:
            if (info.checked.has_value() == false)
                break;
            return { ax_value_kind::number, info.mixed ? 2.0 : info.checked == true ? 1.0 : 0.0 };
        // 라디오는 켬(`checked`)이든 골라짐(`selected`)이든 하나로 읽힌다. 선택 묶음의 항목은 골라짐을 싣는다.
        case access_role::radio_button:
        case access_role::tab:
            return { ax_value_kind::number, info.checked == true || info.selected == true ? 1.0 : 0.0 };
        default:
            break;
        }
        if (info.range.has_value())
            return { ax_value_kind::number, static_cast<double>(info.range->value) };
        if (ax_reads_name_as_value(info.role))
            return { ax_value_kind::text, 0.0, info.value.empty() ? info.name : info.value };
        // 텍스트 칸과 콤보는 빈 글도 값이다.
        if (info.role == access_role::edit || info.role == access_role::combo_box || info.value.empty() == false)
            return { ax_value_kind::text, 0.0, info.value };
        return {};
    }

    std::u8string_view ax_label_of(const access_info& info) noexcept
    {
        return ax_reads_name_as_value(info.role) ? std::u8string_view {} : std::u8string_view { info.name };
    }

    std::u8string_view ax_help_of(const access_info& info) noexcept
    {
        return info.error.empty() ? std::u8string_view { info.help } : std::u8string_view { info.error };
    }

    float ax_range_step(const access_range& range) noexcept
    {
        if (range.step > 0.0f)
            return range.step;
        if (range.maximum <= range.minimum)
            return 0.0f;
        return (range.maximum - range.minimum) / 20.0f;
    }

    std::optional<access_request> ax_action_request(const access_info& info, const ax_action action, const bool multiple_selection) noexcept
    {
        switch (action)
        {
        case ax_action::press:
            // 상태를 든 것은 그 상태의 명령이다 (UIA의 `invokable`이 Invoke를 거르는 것과 같은 갈래).
            if (info.checked.has_value())
                return access_request { access_command::toggle };
            if (info.selected.has_value())
            {
                access_request request { access_command::select };
                request.multiple_selection = multiple_selection;
                return request;
            }
            if (info.expanded.has_value())
                return access_request { info.expanded == true ? access_command::collapse : access_command::expand };
            if (info.range.has_value() || invoke_role(info.role) == false)
                return std::nullopt;
            return access_request { access_command::invoke };
        case ax_action::increment:
        case ax_action::decrement: {
            if (info.range.has_value() == false)
                return std::nullopt;
            const float step { ax_range_step(*info.range) };
            if (step <= 0.0f)
                return std::nullopt;
            const float signed_step { action == ax_action::increment ? step : -step };
            return ax_range_request(info, static_cast<double>(info.range->value + signed_step));
        }
        // 메뉴를 보이라는 것은 펼침이다 (팝업 단추·하위 메뉴가 있는 항목). 이미 펼쳐졌으면 할 일이 없다.
        case ax_action::show_menu:
            if (info.expanded.has_value() == false)
                return std::nullopt;
            return access_request { access_command::expand };
        // 물리기는 펼친 것을 접는다 (Esc와 같은 자리). 접혀 있으면 이 요소가 물릴 것이 없다.
        case ax_action::cancel:
            if (info.expanded != true)
                return std::nullopt;
            return access_request { access_command::collapse };
        case ax_action::confirm:
            return std::nullopt;
        }
        return std::nullopt;
    }

    bool ax_confirms_with_enter(const access_info& info, const ax_role_context& context) noexcept
    {
        return info.role == access_role::edit && context.text_input && context.multiline == false;
    }

    std::optional<access_request> ax_range_request(const access_info& info, const double value) noexcept
    {
        if (info.range.has_value() == false || info.range->maximum < info.range->minimum)
            return std::nullopt;
        const double clamped { std::clamp(value, static_cast<double>(info.range->minimum), static_cast<double>(info.range->maximum)) };
        return access_request { access_command::set_value, static_cast<float>(clamped) };
    }

    std::optional<access_request> ax_disclosure_request(const access_info& info, const bool expand) noexcept
    {
        if (info.expanded.has_value() == false)
            return std::nullopt;
        return access_request { expand ? access_command::expand : access_command::collapse };
    }

    std::optional<access_request> ax_selection_request(const access_info& info, const bool select, const bool multiple_selection) noexcept
    {
        if (info.selected.has_value() == false)
            return std::nullopt;
        if (multiple_selection)
        {
            access_request request { select ? access_command::add_to_selection : access_command::remove_from_selection };
            request.multiple_selection = true;
            return request;
        }
        // 단일 선택은 고른 것을 풀 길이 없다 — 다른 항목을 고르는 것이 푸는 것이다.
        if (select == false)
            return std::nullopt;
        return access_request { access_command::select };
    }

    ax_placement ax_overlay_placement(const rect_f& bounds, const float scroll_y, const float scale) noexcept
    {
        return { bounds.x, bounds.y - scroll_y, bounds, scale };
    }

    std::optional<rect_f> ax_view_rect(const rect_f& bounds, const ax_placement& placement) noexcept
    {
        rect_f surface { bounds.x + placement.origin_x, bounds.y + placement.origin_y, bounds.width, bounds.height };
        if (placement.clip.has_value())
        {
            const rect_f& clip { *placement.clip };
            const float left { std::max(surface.x, clip.x) };
            const float top { std::max(surface.y, clip.y) };
            const float right { std::min(surface.x + surface.width, clip.x + clip.width) };
            const float bottom { std::min(surface.y + surface.height, clip.y + clip.height) };
            // UIA의 `accessibility_client_bounds`와 같은 경계다 — 폭 0인 caret 자리는 남는다.
            if (right < left || bottom <= top)
                return std::nullopt;
            surface = { left, top, right - left, bottom - top };
        }
        const float scale { placement.scale > 0.0f ? placement.scale : 1.0f };
        return rect_f { surface.x / scale, surface.y / scale, surface.width / scale, surface.height / scale };
    }

    std::optional<point_f> ax_tree_point(const point_f& view, const ax_placement& placement) noexcept
    {
        const float scale { placement.scale > 0.0f ? placement.scale : 1.0f };
        const point_f surface { view.x * scale, view.y * scale };
        if (placement.clip.has_value() && placement.clip->contains(surface.x, surface.y) == false)
            return std::nullopt;
        return point_f { surface.x - placement.origin_x, surface.y - placement.origin_y };
    }

    std::size_t ax_utf16_offset(const std::u8string_view text, const std::size_t byte_offset) noexcept
    {
        std::size_t offset { 0 };
        std::size_t units { 0 };
        while (offset < text.size())
        {
            const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
            const std::size_t length { std::max<std::size_t>(1, decoded.length) };
            if (offset + length > byte_offset)
                break;
            offset += length;
            units += decoded.codepoint >= 0x10000u ? 2u : 1u;
        }
        return units;
    }

    std::size_t ax_utf8_offset(const std::u8string_view text, const std::size_t utf16_offset) noexcept
    {
        std::size_t offset { 0 };
        std::size_t units { 0 };
        while (offset < text.size())
        {
            const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
            const std::size_t width { decoded.codepoint >= 0x10000u ? 2u : 1u };
            if (units + width > utf16_offset)
                break;
            offset += std::max<std::size_t>(1, decoded.length);
            units += width;
        }
        return offset;
    }

    std::size_t ax_utf16_length(const std::u8string_view text) noexcept
    {
        return ax_utf16_offset(text, text.size());
    }

    std::size_t ax_line_of(const std::vector<std::size_t>& line_starts, const std::size_t offset) noexcept
    {
        const auto after { std::ranges::upper_bound(line_starts, offset) };
        if (after == line_starts.begin())
            return 0;
        return static_cast<std::size_t>(after - line_starts.begin()) - 1u;
    }

    ui_element_id ax_focus_of(const ui_tree& tree, const interaction_snapshot& interaction)
    {
        ui_element_id announced { interaction.focused };
        if (const menu_bar_element* const bar { active_menu_bar(tree) }; bar != nullptr && bar->config().state.keyboard && bar->config().state.current.empty() == false)
            announced = ui_element_id { ui_element_kind::menu_bar_item, bar->config().state.current };
        if (announced == ui_element_id {} || tree.find(announced) == nullptr)
            return {};
        return announced;
    }

    std::vector<ax_notification> ax_notifications(const access_snapshot& previous, const access_snapshot& current)
    {
        std::vector<ax_notification> notifications {};
        const std::vector<access_change> changes { diff_access_snapshots(previous, current) };
        if (changes.empty())
            return notifications;
        const snapshot_index current_index { index_of(current) };
        const snapshot_index previous_index { index_of(previous) };

        // 항목의 선택 변화는 container가 알린다. 지금 발행본에 container가 없으면 이전 발행본의 것이다 (풀린 쪽).
        const auto announce_selection = [&](const ui_element_id& item) {
            const access_snapshot_entry* container { selection_container_of(current, current_index, item) };
            if (container == nullptr)
                container = selection_container_of(previous, previous_index, item);
            if (container == nullptr)
                return;
            push_unique(notifications, { selection_kind_of(container->info.role), container->id });
        };

        for (const access_change& change : changes)
        {
            switch (change.kind)
            {
            case access_change_kind::property: {
                const access_info& before { change.previous };
                const access_info& after { change.current };
                if (ax_label_of(before) != ax_label_of(after))
                    push_unique(notifications, { ax_notification_kind::title_changed, change.id });
                if (ax_value_of(before) != ax_value_of(after))
                    push_unique(notifications, { ax_notification_kind::value_changed, change.id });
                // 골라짐이 property로 오는 것은 단일 선택에서 풀린 쪽이거나 container 밖의 항목이다. 감싼 container가 있으면 알린다.
                if (before.selected != after.selected)
                    announce_selection(change.id);
                // 행의 펼침은 행이 알린다. 팝업 단추·메뉴 항목의 펼침은 popup이 들고 나는 구조 변화가 말한다.
                if (before.expanded != after.expanded && row_role(after.role))
                    push_unique(notifications, { after.expanded == true ? ax_notification_kind::row_expanded : ax_notification_kind::row_collapsed, change.id });
                // Narrator처럼 VoiceOver도 속성 변화만으로는 오류를 읽지 않는다. 새로 생기거나 바뀐 오류 글을 읽게 한다.
                if (before.error != after.error && after.error.empty() == false)
                    push_unique(notifications, { ax_notification_kind::announcement, change.id, after.error, ax_priority::high });
                break;
            }
            case access_change_kind::selected:
            case access_change_kind::added_to_selection:
            case access_change_kind::removed_from_selection:
                announce_selection(change.id);
                break;
            // 한꺼번의 변화는 container 자신이 id다.
            case access_change_kind::selection_invalidated:
                if (const access_snapshot_entry* const container { entry_of(current, current_index, change.id) }; container != nullptr)
                    push_unique(notifications, { selection_kind_of(container->info.role), change.id });
                break;
            case access_change_kind::structure:
                push_unique(notifications, { ax_notification_kind::layout_changed, change.id });
                break;
            case access_change_kind::live_region: {
                const access_snapshot_entry* const region { entry_of(current, current_index, change.id) };
                if (region == nullptr)
                    break;
                std::u8string text { ax_live_text(current, change.id) };
                if (text.empty())
                    break;
                const ax_priority priority { region->info.live == access_live::assertive ? ax_priority::high : ax_priority::medium };
                push_unique(notifications, { ax_notification_kind::announcement, change.id, std::move(text), priority });
                break;
            }
            }
        }
        return notifications;
    }

    std::u8string ax_live_text(const access_snapshot& snapshot, const ui_element_id& id)
    {
        const snapshot_index index { index_of(snapshot) };
        // 발행본은 pre-order라 그 요소의 안쪽은 바로 뒤에 잇달아 선다. 부모 사슬이 그 요소에 닿는 동안이 안쪽이다.
        const auto inside = [&](const access_snapshot_entry& entry) {
            const access_snapshot_entry* walk { &entry };
            for (std::size_t depth { 0 }; walk != nullptr && depth <= snapshot.entries.size(); ++depth)
            {
                if (walk->id == id)
                    return true;
                if (walk->parent == ui_element_id {})
                    return false;
                walk = entry_of(snapshot, index, walk->parent);
            }
            return false;
        };
        std::u8string text {};
        const auto found { index.find(id) };
        if (found == index.end())
            return text;
        for (std::size_t position { found->second }; position < snapshot.entries.size(); ++position)
        {
            const access_snapshot_entry& entry { snapshot.entries[position] };
            if (inside(entry) == false)
                break;
            append_readable(text, entry.info);
        }
        return text;
    }
}

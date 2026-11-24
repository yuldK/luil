#include "macos/accessibility_mapping.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VoiceOver 다리의 AppKit을 모르는 층이다 — 역할 표, 값·상태·실행의 번역, 좌표, UTF-16 offset, 발행본 대조의 알림이다.
namespace {
    using luil::access_command;
    using luil::access_info;
    using luil::access_role;
    using luil::macos::ax_action;
    using luil::macos::ax_notification;
    using luil::macos::ax_notification_kind;
    using luil::macos::ax_priority;
    using luil::macos::ax_value_kind;

    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(0) };

    [[nodiscard]] luil::ui_element_id item(const std::u8string_view owner)
    {
        return { kind_item, std::u8string { owner } };
    }

    [[nodiscard]] access_info info_of(const access_role role, std::u8string name = {})
    {
        access_info info {};
        info.role = role;
        info.name = std::move(name);
        return info;
    }

    // 정해진 정보를 답하는 최소 element다. 자식을 품고 자기 slot을 그대로 준다.
    class probe_element final : public luil::ui_element
    {
    public:
        probe_element(luil::ui_element_id id, access_info info)
            : ui_element { std::move(id) }
            , info_ { std::move(info) }
        {
        }

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }

        [[nodiscard]] access_info accessibility() const override
        {
            return info_;
        }

    private:
        access_info info_ {};
    };

    [[nodiscard]] luil::access_snapshot_entry entry(const std::u8string_view id, const std::u8string_view parent, access_info info)
    {
        return { item(id), parent.empty() ? luil::ui_element_id {} : item(parent), std::move(info) };
    }
}

TEST_CASE("macOS accessibility maps every role to an AX role")
{
    for (int role { static_cast<int>(access_role::none) }; role <= static_cast<int>(access_role::menu_bar); ++role)
    {
        const luil::macos::ax_role_names names { luil::macos::ax_role_of(info_of(static_cast<access_role>(role))) };
        CAPTURE(role);
        REQUIRE(names.role.starts_with("AX"));
        REQUIRE((names.subrole.empty() || names.subrole.starts_with("AX")));
    }
    // 구조는 묶음으로 물러선다 (접근 tree에는 서지 않는다).
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::none)).role == "AXGroup");
}

TEST_CASE("macOS accessibility roles follow AppKit controls")
{
    using names = luil::macos::ax_role_names;
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::button)) == names { "AXButton" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::toggle_switch)) == names { "AXCheckBox", "AXSwitch" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::tab)) == names { "AXRadioButton", "AXTabButton" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::tab_list)) == names { "AXTabGroup" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::radio_group)) == names { "AXRadioGroup" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::tree_item)) == names { "AXRow", "AXOutlineRow" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::data_item)) == names { "AXRow", "AXTableRow" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::progress)) == names { "AXProgressIndicator" });
    REQUIRE(luil::macos::ax_role_of(info_of(access_role::dialog)) == names { "AXGroup", "AXApplicationDialog" });

    SECTION("text fields split by secrecy and line count")
    {
        access_info edit { info_of(access_role::edit) };
        REQUIRE(luil::macos::ax_role_of(edit, { .text_input = true }) == names { "AXTextField" });
        REQUIRE(luil::macos::ax_role_of(edit, { .text_input = true, .multiline = true }) == names { "AXTextArea" });
        edit.secret = true;
        REQUIRE(luil::macos::ax_role_of(edit, { .text_input = true }) == names { "AXTextField", "AXSecureTextField" });
    }

    SECTION("a combo box that takes text is a combo box, otherwise a pop-up button")
    {
        REQUIRE(luil::macos::ax_role_of(info_of(access_role::combo_box), { .text_input = true }).role == "AXComboBox");
        REQUIRE(luil::macos::ax_role_of(info_of(access_role::combo_box)).role == "AXPopUpButton");
    }

    SECTION("menu items inside a menu bar are menu bar items")
    {
        REQUIRE(luil::macos::ax_role_of(info_of(access_role::menu_item), { .parent = access_role::menu_bar }).role == "AXMenuBarItem");
        REQUIRE(luil::macos::ax_role_of(info_of(access_role::menu_item), { .parent = access_role::menu }).role == "AXMenuItem");
    }

    SECTION("a scrolling pane is a scroll area")
    {
        access_info pane { info_of(access_role::pane) };
        REQUIRE(luil::macos::ax_role_of(pane).role == "AXGroup");
        pane.scroll = luil::access_scroll_state { -1.0f, 40.0f, 100.0f, 25.0f };
        REQUIRE(luil::macos::ax_role_of(pane).role == "AXScrollArea");
    }
}

TEST_CASE("macOS accessibility values follow the AppKit conventions")
{
    SECTION("check boxes read 0, 1, or 2 for mixed")
    {
        access_info check { info_of(access_role::check_box, u8"Bold") };
        check.checked = false;
        REQUIRE(luil::macos::ax_value_of(check).number == 0.0);
        check.checked = true;
        REQUIRE(luil::macos::ax_value_of(check).number == 1.0);
        check.mixed = true;
        REQUIRE(luil::macos::ax_value_of(check) == luil::macos::ax_value { ax_value_kind::number, 2.0 });
        REQUIRE(luil::macos::ax_label_of(check) == u8"Bold");
    }

    SECTION("radio items and tabs read their selection")
    {
        access_info radio { info_of(access_role::radio_button) };
        radio.selected = true;
        REQUIRE(luil::macos::ax_value_of(radio).number == 1.0);
        access_info tab { info_of(access_role::tab) };
        tab.selected = false;
        REQUIRE(luil::macos::ax_value_of(tab) == luil::macos::ax_value { ax_value_kind::number, 0.0 });
    }

    SECTION("ranges read the current value")
    {
        access_info slider { info_of(access_role::slider, u8"Volume") };
        slider.range = luil::access_range { 0.0f, 10.0f, 2.5f };
        REQUIRE(luil::macos::ax_value_of(slider) == luil::macos::ax_value { ax_value_kind::number, 2.5 });
    }

    SECTION("static text reads its name as the value, not the label")
    {
        const access_info text { info_of(access_role::static_text, u8"Saved") };
        REQUIRE(luil::macos::ax_value_of(text) == luil::macos::ax_value { ax_value_kind::text, 0.0, u8"Saved" });
        REQUIRE(luil::macos::ax_label_of(text).empty());
        const access_info row { info_of(access_role::list_item, u8"Inbox") };
        REQUIRE(luil::macos::ax_value_of(row).text == u8"Inbox");
    }

    SECTION("an empty text field still has a text value")
    {
        REQUIRE(luil::macos::ax_value_of(info_of(access_role::edit, u8"Search")).kind == ax_value_kind::text);
        REQUIRE(luil::macos::ax_value_of(info_of(access_role::button, u8"OK")).kind == ax_value_kind::none);
    }

    SECTION("help reads the error first")
    {
        access_info field { info_of(access_role::edit) };
        field.help = u8"Enter a port";
        REQUIRE(luil::macos::ax_help_of(field) == u8"Enter a port");
        field.error = u8"Out of range";
        REQUIRE(luil::macos::ax_help_of(field) == u8"Out of range");
    }
}

TEST_CASE("macOS accessibility actions become the same commands as UI Automation")
{
    SECTION("press follows the state the element carries")
    {
        access_info check { info_of(access_role::check_box) };
        check.checked = false;
        REQUIRE(luil::macos::ax_action_request(check, ax_action::press)->command == access_command::toggle);

        access_info row { info_of(access_role::list_item) };
        row.selected = false;
        const std::optional<luil::access_request> select { luil::macos::ax_action_request(row, ax_action::press, true) };
        REQUIRE(select->command == access_command::select);
        REQUIRE(select->multiple_selection);

        access_info combo { info_of(access_role::combo_box) };
        combo.expanded = true;
        REQUIRE(luil::macos::ax_action_request(combo, ax_action::press)->command == access_command::collapse);
        combo.expanded = false;
        REQUIRE(luil::macos::ax_action_request(combo, ax_action::press)->command == access_command::expand);

        REQUIRE(luil::macos::ax_action_request(info_of(access_role::button), ax_action::press)->command == access_command::invoke);
        REQUIRE(luil::macos::ax_action_request(info_of(access_role::header_item), ax_action::press)->command == access_command::invoke);
        // 텍스트 칸은 hit를 흡수하려고 빈 클릭을 들고 있어도 누르는 자리가 아니다.
        REQUIRE_FALSE(luil::macos::ax_action_request(info_of(access_role::edit), ax_action::press).has_value());
        REQUIRE_FALSE(luil::macos::ax_action_request(info_of(access_role::static_text), ax_action::press).has_value());
    }

    SECTION("increment and decrement step within the range")
    {
        access_info slider { info_of(access_role::slider) };
        slider.range = luil::access_range { 0.0f, 100.0f, 50.0f };
        // 걸음을 알리지 않으면 범위의 20분의 1이다.
        REQUIRE(luil::macos::ax_action_request(slider, ax_action::increment)->value == 55.0f);
        REQUIRE(luil::macos::ax_action_request(slider, ax_action::decrement)->value == 45.0f);
        slider.range->step = 1.0f;
        REQUIRE(luil::macos::ax_action_request(slider, ax_action::increment)->value == 51.0f);
        slider.range->value = 100.0f;
        // 끝을 넘지 않는다.
        const std::optional<luil::access_request> top { luil::macos::ax_action_request(slider, ax_action::increment) };
        REQUIRE(top->command == access_command::set_value);
        REQUIRE(top->value == 100.0f);
        REQUIRE_FALSE(luil::macos::ax_action_request(info_of(access_role::button), ax_action::increment).has_value());
    }

    SECTION("show menu expands and cancel collapses")
    {
        access_info popup { info_of(access_role::combo_box) };
        popup.expanded = false;
        REQUIRE(luil::macos::ax_action_request(popup, ax_action::show_menu)->command == access_command::expand);
        REQUIRE_FALSE(luil::macos::ax_action_request(popup, ax_action::cancel).has_value());
        popup.expanded = true;
        REQUIRE(luil::macos::ax_action_request(popup, ax_action::cancel)->command == access_command::collapse);
        REQUIRE_FALSE(luil::macos::ax_action_request(info_of(access_role::button), ax_action::show_menu).has_value());
    }

    SECTION("confirm on a single-line field is the Enter key")
    {
        const access_info field { info_of(access_role::edit) };
        REQUIRE_FALSE(luil::macos::ax_action_request(field, ax_action::confirm).has_value());
        REQUIRE(luil::macos::ax_confirms_with_enter(field, { .text_input = true }));
        REQUIRE_FALSE(luil::macos::ax_confirms_with_enter(field, { .text_input = true, .multiline = true }));
        REQUIRE_FALSE(luil::macos::ax_confirms_with_enter(info_of(access_role::button), { .text_input = true }));
    }

    SECTION("setting a range value clamps to the bounds")
    {
        access_info slider { info_of(access_role::slider) };
        slider.range = luil::access_range { 0.0f, 10.0f, 2.0f };
        REQUIRE(luil::macos::ax_range_request(slider, 7.5)->value == 7.5f);
        REQUIRE(luil::macos::ax_range_request(slider, 12.0)->value == 10.0f);
        REQUIRE_FALSE(luil::macos::ax_range_request(info_of(access_role::button), 1.0).has_value());
    }

    SECTION("selection and disclosure writes")
    {
        access_info row { info_of(access_role::tree_item) };
        row.selected = true;
        row.expanded = false;
        REQUIRE(luil::macos::ax_disclosure_request(row, true)->command == access_command::expand);
        REQUIRE(luil::macos::ax_disclosure_request(row, false)->command == access_command::collapse);
        REQUIRE(luil::macos::ax_selection_request(row, false, true)->command == access_command::remove_from_selection);
        REQUIRE(luil::macos::ax_selection_request(row, true, true)->command == access_command::add_to_selection);
        REQUIRE(luil::macos::ax_selection_request(row, true, false)->command == access_command::select);
        // 단일 선택은 고른 것을 따로 풀 길이 없다.
        REQUIRE_FALSE(luil::macos::ax_selection_request(row, false, false).has_value());
        REQUIRE_FALSE(luil::macos::ax_disclosure_request(info_of(access_role::button), true).has_value());
    }
}

TEST_CASE("macOS accessibility places tree rectangles in view points")
{
    SECTION("the main tree divides physical pixels by the backing scale")
    {
        const luil::macos::ax_placement main { 0.0f, 0.0f, std::nullopt, 2.0f };
        REQUIRE(luil::macos::ax_view_rect({ 20.0f, 40.0f, 100.0f, 50.0f }, main) == luil::rect_f { 10.0f, 20.0f, 50.0f, 25.0f });
        REQUIRE(luil::macos::ax_tree_point({ 10.0f, 20.0f }, main) == luil::point_f { 20.0f, 40.0f });
    }

    SECTION("a popup sits at its panel and is clipped by it")
    {
        // 판은 (100, 200)에서 300×400이고 안을 50 흘렸다.
        const luil::macos::ax_placement popup { luil::macos::ax_overlay_placement({ 100.0f, 200.0f, 300.0f, 400.0f }, 50.0f, 2.0f) };
        REQUIRE(popup.origin_y == 150.0f);
        REQUIRE(luil::macos::ax_view_rect({ 0.0f, 60.0f, 20.0f, 10.0f }, popup) == luil::rect_f { 50.0f, 105.0f, 10.0f, 5.0f });
        // 판 위로 흘러 나간 몫은 잘린다.
        REQUIRE(luil::macos::ax_view_rect({ 0.0f, 40.0f, 20.0f, 20.0f }, popup) == luil::rect_f { 50.0f, 100.0f, 10.0f, 5.0f });
        // 판 밖으로 다 흘러 나간 행은 자리가 없다.
        REQUIRE_FALSE(luil::macos::ax_view_rect({ 0.0f, 0.0f, 20.0f, 20.0f }, popup).has_value());
        // view 점이 판 안이면 popup tree 점으로, 밖이면 아무것도 아니다.
        REQUIRE(luil::macos::ax_tree_point({ 60.0f, 110.0f }, popup) == luil::point_f { 20.0f, 70.0f });
        REQUIRE_FALSE(luil::macos::ax_tree_point({ 10.0f, 10.0f }, popup).has_value());
    }
}

TEST_CASE("macOS accessibility converts UTF-8 offsets to UTF-16 indices")
{
    // a(1 byte) 한(3 byte) 😀(4 byte, surrogate 짝) b(1 byte)
    const std::u8string text { u8"a한\U0001F600b" };
    REQUIRE(luil::macos::ax_utf16_length(text) == 5u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 0u) == 0u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 1u) == 1u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 4u) == 2u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 8u) == 4u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 9u) == 5u);
    // 문자 가운데는 그 문자의 앞이다.
    REQUIRE(luil::macos::ax_utf16_offset(text, 2u) == 1u);
    REQUIRE(luil::macos::ax_utf16_offset(text, 100u) == 5u);

    REQUIRE(luil::macos::ax_utf8_offset(text, 0u) == 0u);
    REQUIRE(luil::macos::ax_utf8_offset(text, 2u) == 4u);
    REQUIRE(luil::macos::ax_utf8_offset(text, 4u) == 8u);
    // surrogate 짝 사이는 그 문자의 앞이다.
    REQUIRE(luil::macos::ax_utf8_offset(text, 3u) == 4u);
    REQUIRE(luil::macos::ax_utf8_offset(text, 100u) == 9u);

    const std::vector<std::size_t> starts { 0u, 6u, 12u };
    REQUIRE(luil::macos::ax_line_of({}, 40u) == 0u);
    REQUIRE(luil::macos::ax_line_of(starts, 0u) == 0u);
    REQUIRE(luil::macos::ax_line_of(starts, 5u) == 0u);
    REQUIRE(luil::macos::ax_line_of(starts, 6u) == 1u);
    REQUIRE(luil::macos::ax_line_of(starts, 30u) == 2u);
}

TEST_CASE("macOS accessibility announces the logical focus of the tree")
{
    auto root { std::make_unique<probe_element>(item(u8"root"), info_of(access_role::group)) };
    root->add(std::make_unique<probe_element>(item(u8"ok"), info_of(access_role::button, u8"OK")));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 100.0f, 40.0f }, 1.0f) };

    luil::interaction_snapshot interaction {};
    REQUIRE(luil::macos::ax_focus_of(tree, interaction) == luil::ui_element_id {});
    interaction.focused = item(u8"ok");
    REQUIRE(luil::macos::ax_focus_of(tree, interaction) == item(u8"ok"));
    // 지금 tree에 없는 초점은 알릴 자리가 없다.
    interaction.focused = item(u8"gone");
    REQUIRE(luil::macos::ax_focus_of(tree, interaction) == luil::ui_element_id {});
}

TEST_CASE("macOS accessibility turns snapshot changes into notifications")
{
    SECTION("names and values")
    {
        luil::access_snapshot before { { entry(u8"title", {}, info_of(access_role::button, u8"Save")), entry(u8"status", {}, info_of(access_role::static_text, u8"Idle")) } };
        luil::access_snapshot after { before };
        after.entries[0].info.name = u8"Save all";
        after.entries[1].info.name = u8"Busy";
        const std::vector<ax_notification> notifications { luil::macos::ax_notifications(before, after) };
        // 글은 이름을 값으로 읽으므로 값 변화다.
        REQUIRE(notifications
            == std::vector<ax_notification> { { ax_notification_kind::title_changed, item(u8"title") }, { ax_notification_kind::value_changed, item(u8"status") } });
    }

    SECTION("a new single selection is announced once by its container")
    {
        access_info list { info_of(access_role::list) };
        access_info first { info_of(access_role::list_item, u8"A") };
        first.selected = true;
        access_info second { info_of(access_role::list_item, u8"B") };
        second.selected = false;
        const luil::access_snapshot before { { entry(u8"list", {}, list), entry(u8"a", u8"list", first), entry(u8"b", u8"list", second) } };
        luil::access_snapshot after { before };
        after.entries[1].info.selected = false;
        after.entries[2].info.selected = true;
        REQUIRE(luil::macos::ax_notifications(before, after) == std::vector<ax_notification> { { ax_notification_kind::selected_children_changed, item(u8"list") } });
    }

    SECTION("tree rows announce expansion and the tree announces selected rows")
    {
        access_info row { info_of(access_role::tree_item, u8"src") };
        row.expanded = false;
        row.selected = false;
        const luil::access_snapshot before { { entry(u8"tree", {}, info_of(access_role::tree)), entry(u8"src", u8"tree", row) } };
        luil::access_snapshot after { before };
        after.entries[1].info.expanded = true;
        after.entries[1].info.selected = true;
        const std::vector<ax_notification> notifications { luil::macos::ax_notifications(before, after) };
        REQUIRE(notifications.size() == 2u);
        REQUIRE(std::ranges::find(notifications, ax_notification { ax_notification_kind::row_expanded, item(u8"src") }) != notifications.end());
        REQUIRE(std::ranges::find(notifications, ax_notification { ax_notification_kind::selected_rows_changed, item(u8"tree") }) != notifications.end());
    }

    SECTION("structure changes are layout changes of the parent")
    {
        const luil::access_snapshot before { { entry(u8"panel", {}, info_of(access_role::group)) } };
        luil::access_snapshot after { before };
        after.entries.push_back(entry(u8"child", u8"panel", info_of(access_role::button, u8"New")));
        after.entries.push_back(entry(u8"top", {}, info_of(access_role::button, u8"Top")));
        const std::vector<ax_notification> notifications { luil::macos::ax_notifications(before, after) };
        REQUIRE(std::ranges::find(notifications, ax_notification { ax_notification_kind::layout_changed, item(u8"panel") }) != notifications.end());
        // 빈 id는 표면 root(view)다.
        REQUIRE(std::ranges::find(notifications, ax_notification { ax_notification_kind::layout_changed, luil::ui_element_id {} }) != notifications.end());
    }

    SECTION("a new error is announced with high priority")
    {
        const luil::access_snapshot before { { entry(u8"port", {}, info_of(access_role::edit, u8"Port")) } };
        luil::access_snapshot after { before };
        after.entries[0].info.invalid = true;
        after.entries[0].info.error = u8"Out of range";
        const std::vector<ax_notification> notifications { luil::macos::ax_notifications(before, after) };
        REQUIRE(notifications == std::vector<ax_notification> { { ax_notification_kind::announcement, item(u8"port"), u8"Out of range", ax_priority::high } });
    }

    SECTION("a live region reads its text")
    {
        access_info region { info_of(access_role::status_bar) };
        region.live = luil::access_live::polite;
        const luil::access_snapshot before { { entry(u8"bar", {}, region), entry(u8"count", u8"bar", info_of(access_role::static_text, u8"3 items")) } };
        luil::access_snapshot after { before };
        after.entries[1].info.name = u8"4 items";
        const std::vector<ax_notification> notifications { luil::macos::ax_notifications(before, after) };
        REQUIRE(std::ranges::find(notifications, ax_notification { ax_notification_kind::announcement, item(u8"bar"), u8"4 items", ax_priority::medium }) != notifications.end());
        REQUIRE(luil::macos::ax_live_text(after, item(u8"bar")) == u8"4 items");
    }

    SECTION("no change, no notification")
    {
        const luil::access_snapshot same { { entry(u8"ok", {}, info_of(access_role::button, u8"OK")) } };
        REQUIRE(luil::macos::ax_notifications(same, same).empty());
    }
}

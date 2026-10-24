#include "luil/ui/combo_box_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/label_element.h"
#include "luil/ui/spinner_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/virtual_list_element.h"

#include <algorithm>
#include <utility>

namespace luil {
    namespace {
        void keep_input_focus(ui_element& element)
        {
            element.set_preserve_pointer_focus(true);
            for (const auto& child : element.children())
                keep_input_focus(*child);
        }
    } // namespace

    combo_box_element::combo_box_element(const ui_element_id id, text_input_view value, combo_box_config config)
        : ui_element { id }, value_ { std::move(value) }, config_ { std::move(config) }
    {
        const auto query { value_.displayed_text() };
        // 다 받은 후보는 그 질의에 대한 원천의 답이다. 원천은 대소문자·접두어·별칭 같은 자기 규칙으로 고르므로
        // 같은 글을 다시 걸러 그 답을 숨기지 않는다. 받는 중(앞 질의의 후보)이거나 글이 그 뒤 바뀌었을 때만 칸에서 거른다.
        const bool answered { config_.suggestions.loading == false && query == config_.suggestions.query };
        if (config_.suggestions.items != nullptr)
            for (const auto& item : *config_.suggestions.items)
                if (config_.matches ? config_.matches(item, query) : (answered || item.label.find(query) != std::u8string::npos))
                    visible_items_.push_back(item);
        auto input { config_.input };
        input.navigation_keys_to_parent = config_.open;
        if (input.clear == nullptr && config_.set_open)
        {
            input.clear_glyph = codicons::icon_chevron_down;
            input.clear = [this](const ui_action_context&) { return std::vector<input_action> { config_.set_open(config_.open == false) }; };
        }
        auto field { std::make_unique<text_input_element>(ui_element_id { id.kind, id.owner + u8":input" }, value_, std::move(input)) };
        input_ = field.get();
        add_child(std::move(field));
        if (config_.validation_message.empty() == false)
        {
            auto label { std::make_unique<label_element>(ui_element_id { ui_element_kind::none, id.owner + u8":validation" },
                label_config { .text = config_.validation_message, .color = label_color_role::error })
            };
            validation_ = label.get();
            add_child(std::move(label));
        }
        // 입력 칸이 쓰지 않은 키(↑↓·Page·Enter·Esc)가 조상 전파로 이 콤보에 온다 (key-propagation-design.md).
        set_key_handler([this](const key_pressed_event& event) { return on_key(event); });
        keys_connected_ = true;
        // 초점이 콤보를 떠나면 열린 후보를 닫는다 (Tab·Shift+Tab·보조 기술의 초점 이동). 남겨 두면 행은 여전히 눌리는데
        // ↑↓·Enter는 새 칸으로 가 popup이 키를 받지 않는 채 떠 있다. 밖 누름은 플랫폼의 popup 닫힘도 받으므로 그때는
        // `set_open(false)`가 두 번 올 수 있다 — 목표 상태를 나르는 메시지라 거듭 와도 같다.
        //  - 창의 비활성화는 떠남이 아니라 Alt+Tab으로는 닫히지 않는다 (`ui_element::focus_leave_action`).
        if (config_.open && config_.set_open)
            set_focus_leave_action([close = config_.set_open](const ui_action_context&) { return std::vector<input_action> { close(false) }; });
    }

    bool combo_box_element::ancestor_keys_connected() const noexcept
    {
        return keys_connected_;
    }

    std::optional<input_action> combo_box_element::suggestion_request(const std::uint64_t token) const
    {
        if (config_.request_suggestions == nullptr)
            return std::nullopt;
        // token의 질의는 begin_combo_query가 상태에 남긴 글이다. 칸의 글은 앞 frame의 것일 수 있다.
        const bool known { token != 0 && token == config_.suggestions.token };
        return config_.request_suggestions(token, known ? config_.suggestions.query : std::u8string { value_.displayed_text() });
    }

    std::optional<std::vector<input_action>> combo_box_element::on_key(const key_pressed_event& event) const
    {
        if (value_.composing || event.shortcut_modifier_down())
            return std::nullopt;
        if (event.key == key_code::escape)
        {
            if (config_.open && config_.set_open)
                return std::vector<input_action> { config_.set_open(false) };
            if (config_.open == false && config_.restore)
                return config_.restore({ id() });
            return std::nullopt;
        }
        // Home·End는 글 칸의 caret 몫이다 (편집형 콤보의 관례). 열린 후보는 ↑↓·Page로만 옮긴다.
        const bool navigation { event.key == key_code::arrow_down || event.key == key_code::arrow_up || event.key == key_code::page_down || event.key == key_code::page_up };
        const auto* const items { &visible_items_ };
        if (navigation)
        {
            if (config_.open == false)
                return config_.set_open ? std::optional<std::vector<input_action>> { std::vector<input_action> { config_.set_open(true) } } : std::nullopt;
            if (items == nullptr || items->empty() || config_.move == nullptr)
                return std::vector<input_action> {};
            const auto current { std::find_if(items->begin(), items->end(), [this](const auto& item) { return item.key == config_.highlighted; }) };
            int index { current == items->end() ? -1 : static_cast<int>(current - items->begin()) };
            const bool forward { event.key == key_code::arrow_down || event.key == key_code::page_down };
            const int page { std::max(1, static_cast<int>(config_.popup_height / 24.0f)) };
            const int count { event.key == key_code::page_down || event.key == key_code::page_up ? page : 1 };
            if (index == -1 && forward == false)
                index = static_cast<int>(items->size());
            int enabled_index { index >= 0 && index < static_cast<int>(items->size()) && (*items)[static_cast<std::size_t>(index)].enabled ? index : -1 };
            for (int moved { 0 }; moved < count;)
            {
                const int next { index + (forward ? 1 : -1) };
                if (next < 0 || next >= static_cast<int>(items->size()))
                    break;
                index = next;
                if ((*items)[static_cast<std::size_t>(index)].enabled)
                {
                    enabled_index = index;
                    ++moved;
                }
            }
            index = enabled_index;
            if (index >= 0 && index < static_cast<int>(items->size()) && (*items)[static_cast<std::size_t>(index)].enabled)
            {
                std::vector<input_action> actions { config_.move((*items)[static_cast<std::size_t>(index)].key) };
                const float height { config_.popup_height - (config_.suggestions.loading ? 24.0f : 0.0f) };
                const float delta { luil::scroll_delta_to_reveal(static_cast<float>(index) * 24.0f, 24.0f, config_.scroll_offset, height) };
                if (config_.scroll && delta != 0.0f)
                    actions.push_back(config_.scroll(delta));
                return actions;
            }
            return std::vector<input_action> {};
        }
        if (event.key != key_code::enter)
            return std::nullopt;
        if (config_.open && items != nullptr)
            for (const auto& item : *items)
                if (item.enabled && item.key == config_.highlighted && config_.select)
                    return std::vector<input_action> { config_.select(item.key) };
        const std::u8string query { value_.displayed_text() };
        if (config_.allow_custom == false)
        {
            if (items != nullptr)
                for (const auto& item : *items)
                    if (item.enabled && item.label == query && config_.select)
                        return std::vector<input_action> { config_.select(item.key) };
            // 받는 중의 후보는 앞 질의의 것이라 맞는 후보가 아직 없을 수 있다. 틀렸다고 단정하지 않고 Enter만 삼킨다.
            if (config_.suggestions.loading)
                return std::vector<input_action> {};
            return config_.invalid ? std::vector<input_action> { config_.invalid(query) } : std::vector<input_action> {};
        }
        return config_.commit ? std::optional<std::vector<input_action>> { std::vector<input_action> { config_.commit(query) } } : std::nullopt;
    }

    ui_popup combo_box_element::suggestion_popup(std::u8string anchor) const
    {
        ui_popup popup {};
        popup.id = id().owner + u8":suggestions";
        popup.anchor = std::move(anchor);
        popup.x = bounds().x / scale_;
        popup.y = (input_->bounds().y + input_->bounds().height) / scale_;
        popup.width = bounds().width / scale_;
        popup.height = config_.open ? config_.popup_height : 0.0f;
        popup.dismiss = [close = config_.set_open](popup_dismiss_reason) { return close ? close(false) : input_action {}; };
        if (config_.open == false)
            return popup;
        auto root { std::make_unique<stack_element>(ui_element_id { ui_element_kind::none, popup.id }, stack_config {}) };
        if (config_.suggestions.loading)
            root->add(std::make_unique<spinner_element>(ui_element_id { ui_element_kind::none, popup.id + u8":loading" }, spinner_config { u8"Loading suggestions" }), 24.0f);
        virtual_list_config list {};
        list.owner = popup.id;
        list.selected = config_.highlighted;
        list.select = config_.select;
        list.scroll_offset = config_.scroll_offset;
        list.scroll = config_.scroll;
        list.scroll_to = config_.scroll_to;
        for (const auto& item : visible_items_)
            list.items.push_back({ .key = item.key, .label = item.label, .enabled = item.enabled });
        root->add_flexible(std::make_unique<virtual_list_element>(std::move(list)));
        keep_input_focus(*root);
        popup.tree = std::make_shared<const ui_tree>(make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale_, popup.height * scale_ }, scale_));
        return popup;
    }

    void combo_box_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        const float validation { validation_ != nullptr ? 20.0f * scale_ : 0.0f };
        input_->arrange(context.for_child({ context.slot.x, context.slot.y, context.slot.width, std::max(0.0f, context.slot.height - validation) }));
        if (validation_ != nullptr)
            validation_->arrange(context.for_child({ context.slot.x, context.slot.y + context.slot.height - validation, context.slot.width, validation }));
    }

    void combo_box_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info combo_box_element::accessibility() const
    {
        auto info { input_->accessibility() };
        info.role = access_role::combo_box;
        info.expanded = config_.open;
        return info;
    }

    std::optional<text_input_snapshot> combo_box_element::text_input() const
    {
        return input_->text_input();
    }

    std::optional<std::vector<input_action>> combo_box_element::access_actions(const access_request& request) const
    {
        if ((request.command == access_command::expand || request.command == access_command::collapse) && config_.set_open)
            return std::vector<input_action> { config_.set_open(request.command == access_command::expand) };
        return ui_element::access_actions(request);
    }
} // namespace luil

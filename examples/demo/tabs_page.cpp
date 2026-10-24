#include "demo/tabs_page.h"

#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/commands.h"
#include "luil/ui/dialog_elements.h"

#include "luil/ui/menu_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"
#include <algorithm>

#include <utility>

namespace demo {
    namespace {
        constexpr float tab_bar_height { 36.0f };
        constexpr float overflow_menu_width { 200.0f };

        [[nodiscard]] std::vector<luil::app_command> tab_commands()
        {
            return {
                { .id = u8"new", .label = u8"새 탭", .shortcut = { luil::key_code::key_t, true } },
                { .id = u8"reopen", .label = u8"닫은 탭 다시 열기", .shortcut = { luil::key_code::key_t, true, true } },
                { .id = u8"close-current", .label = u8"현재 탭 닫기", .shortcut = { luil::key_code::key_w, true } },
                { .id = u8"next", .label = u8"다음 탭", .shortcut = { luil::key_code::tab, true } },
                { .id = u8"previous", .label = u8"이전 탭", .shortcut = { luil::key_code::tab, true, true } },
            };
        }
        constexpr std::u8string_view bar_owner { u8"demo-tabs" };
        // 레인 오른쪽 끝의 새 탭 단추 폭과, 견본 막대 하나의 폭이다.
        constexpr float new_tab_width { 32.0f };
        constexpr float sample_width { 260.0f };
        // 줄이기를 켰을 때의 최소 탭 폭이다.
        constexpr float narrow_tab_width { 96.0f };

        // 그 아래 가지를 이 스타일로 배치한다 — 앱이 `make_arranged_tree`에 스타일을 넘기는 것과 같은 길이다.
        // 데모는 페이지마다 다른 화면이라 페이지 뿌리에서 배치 문맥의 스타일만 바꾼다. 막대는 config에 모양을 적지 않는다.
        class style_scope final : public luil::ui_element
        {
        public:
            style_scope(const luil::ui_style* const style, std::unique_ptr<luil::ui_element> content)
                : ui_element { luil::ui_element_id { kind_layout, u8"tabs-style" } }
                , style_ { style }
                , content_ { content.get() }
            {
                add_child(std::move(content));
            }

            void arrange(const luil::arrange_context& context) override
            {
                set_bounds(context.slot);
                luil::arrange_context inner { context.for_child(context.slot) };
                inner.style = style_;
                content_->arrange(inner);
            }

            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

        private:
            const luil::ui_style* style_ { nullptr };
            luil::ui_element* content_ { nullptr };
        };

        // 설정 한 줄이다: 왼쪽에 이름, 오른쪽에 배타 선택이다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> setting_row(const std::u8string& setting, std::u8string title, std::vector<luil::choice_item> items, const std::u8string_view selected)
        {
            const luil::stack_config line { .direction = luil::stack_direction::row, .spacing = 12.0f };
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs-setting/" + setting }, line) };
            row->add(make_label({ kind_text, u8"tabs-setting-title/" + setting }, title, 12.0f, luil::label_color_role::dim), 110.0f);
            luil::choice_group_config choices {};
            choices.owner = u8"tabs-setting/" + setting;
            choices.name = std::move(title);
            choices.style = luil::choice_style::toggle;
            choices.items = std::move(items);
            choices.selected = std::u8string { selected };
            choices.select = [setting](const std::u8string& value) { return luil::make_app_action(tab_setting_intent { setting, value }); };
            row->add_flexible(std::make_unique<luil::choice_group_element>(std::move(choices)));
            return row;
        }
    } // namespace

    tabs_page::tabs_page()
    {
        for (next_tab_ = 1; next_tab_ <= 8; ++next_tab_)
            tabs_.push_back({ u8"tab-" + to_u8(next_tab_), u8"문서 " + to_u8(next_tab_) });
        selected_ = tabs_.front().key;
    }

    bool tabs_page::handle(const luil::app_message& message)
    {
        if (const auto* const metrics { message.get<tab_window_metrics_intent>() }; metrics != nullptr)
        {
            for (detached_tab& item : detached_)
                if (item.tab.key == metrics->key)
                {
                    item.width = metrics->width / std::max(0.01f, metrics->scale);
                    item.height = metrics->height / std::max(0.01f, metrics->scale);
                    item.scale = metrics->scale;
                }
            return true;
        }
        if (const auto* const attach { message.get<tab_attach_intent>() }; attach != nullptr)
        {
            const auto found { std::ranges::find_if(detached_, [&attach](const detached_tab& item) { return item.tab.key == attach->key; }) };
            if (found == detached_.end())
                return true;
            const tab_entry tab { found->tab };
            detached_.erase(found);
            auto target { std::ranges::find_if(tabs_, [&attach](const tab_entry& item) { return item.key == attach->target; }) };
            if (target != tabs_.end() && attach->side == luil::drop_side::after)
                ++target;
            tabs_.insert(target, tab);
            selected_ = tab.key;
            reveal_selected();
            return true;
        }
        if (const auto* const detach { message.get<tab_detach_intent>() }; detach != nullptr)
        {
            const auto found { std::ranges::find_if(tabs_, [&detach](const tab_entry& item) { return item.key == detach->key; }) };
            if (found == tabs_.end())
                return true;
            detached_.push_back({ .tab = *found, .anchor = detach->anchor });
            // 분리는 닫힘 기록에 넣지 않는다.
            const bool selected { found->key == selected_ };
            const auto next { tabs_.erase(found) };
            if (selected)
                selected_ = tabs_.empty() ? std::u8string {} : (next == tabs_.end() ? tabs_.back().key : next->key);
            reveal_selected();
            close_popups();
            return true;
        }
        if (const auto* const select { message.get<tab_select_intent>() }; select != nullptr)
        {
            if (std::ranges::none_of(tabs_, [&select](const tab_entry& item) { return item.key == select->key; }))
                return true;
            selected_ = select->key;
            reveal_selected();
            // 넘침 메뉴에서 골랐을 수도 있으니 함께 닫는다.
            overflow_open_ = false;
            return true;
        }
        if (const auto* const close { message.get<tab_close_intent>() }; close != nullptr)
        {
            close_tab(close->key);
            return true;
        }
        if (const auto* const menu { message.get<tab_menu_intent>() }; menu != nullptr)
        {
            menu_key_ = menu->key;
            menu_x_ = menu->x;
            menu_y_ = menu->y;
            overflow_open_ = false;
            return true;
        }
        if (const auto* const command { message.get<tab_menu_command_intent>() }; command != nullptr)
        {
            const std::u8string key { std::move(menu_key_) };
            menu_key_.clear();
            run_command(command->command, key.empty() ? selected_ : key);
            return true;
        }
        if (message.get<tab_menu_close_intent>() != nullptr)
        {
            menu_key_.clear();
            return true;
        }
        if (const auto* const reorder { message.get<tab_reorder_intent>() }; reorder != nullptr)
        {
            if (reorder->moved == reorder->target)
                return true;
            auto find = [this](const std::u8string& key) {
                for (auto position = tabs_.begin(); position != tabs_.end(); ++position)
                    if (position->key == key)
                        return position;
                return tabs_.end();
            };
            const auto moved { find(reorder->moved) };
            if (moved == tabs_.end())
                return true;
            const tab_entry entry { *moved };
            tabs_.erase(moved);
            auto target { find(reorder->target) };
            if (target != tabs_.end() && reorder->side == luil::drop_side::after)
                ++target;
            tabs_.insert(target, entry);
            reveal_selected();
            return true;
        }
        if (const auto* const scroll { message.get<tab_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ += scroll->delta;
            if (scroll_ < 0.0f)
                scroll_ = 0.0f;
            return true;
        }
        if (const auto* const overflow { message.get<tab_overflow_intent>() }; overflow != nullptr)
        {
            overflow_open_ = overflow->open;
            return true;
        }
        if (const auto* const setting { message.get<tab_setting_intent>() }; setting != nullptr)
        {
            luil::tab_bar_style& style { style_.tab_bar };
            if (setting->setting == u8"variant")
                style.variant = setting->value == u8"connected" ? luil::tab_bar_variant::connected : setting->value == u8"flat" ? luil::tab_bar_variant::flat : luil::tab_bar_variant::underline;
            else if (setting->setting == u8"close")
                style.close_visibility = setting->value == u8"hover" ? luil::tab_close_visibility::selected_or_hovered : luil::tab_close_visibility::always;
            else if (setting->setting == u8"indicator")
                style.indicator_placement = setting->value == u8"slot" ? luil::tab_indicator_placement::close_slot : luil::tab_indicator_placement::icon_corner;
            else if (setting->setting == u8"overflow")
                step_overflow_ = setting->value == u8"step";
            else if (setting->setting == u8"width")
                shrink_ = setting->value == u8"shrink";
            reveal_selected();
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> tabs_page::make_settings() const
    {
        const luil::tab_bar_style& style { style_.tab_bar };
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs-settings" }, luil::stack_config { .spacing = luil::choice_toggle_gap }) };
        column->add(setting_row(u8"variant", u8"모양", { { u8"underline", u8"기본" }, { u8"connected", u8"이어진 탭" }, { u8"flat", u8"평평한 칸" } },
                        style.variant == luil::tab_bar_variant::connected  ? u8"connected"
                            : style.variant == luil::tab_bar_variant::flat ? u8"flat"
                                                                           : u8"underline"),
            luil::choice_toggle_height);
        column->add(setting_row(u8"close", u8"닫기 단추", { { u8"always", u8"늘 보이기" }, { u8"hover", u8"선택·포인터만" } },
                        style.close_visibility == luil::tab_close_visibility::always ? u8"always" : u8"hover"),
            luil::choice_toggle_height);
        column->add(setting_row(u8"indicator", u8"상태 표시", { { u8"corner", u8"아이콘 모서리" }, { u8"slot", u8"닫기 단추 자리" } },
                        style.indicator_placement == luil::tab_indicator_placement::icon_corner ? u8"corner" : u8"slot"),
            luil::choice_toggle_height);
        column->add(setting_row(u8"overflow", u8"넘칠 때", { { u8"scroll", u8"흘리기" }, { u8"step", u8"한 탭씩" } }, step_overflow_ ? u8"step" : u8"scroll"), luil::choice_toggle_height);
        column->add(setting_row(u8"width", u8"탭 폭", { { u8"fixed", u8"고정" }, { u8"shrink", u8"모자라면 줄이기" } }, shrink_ ? u8"shrink" : u8"fixed"), luil::choice_toggle_height);
        column->add_gap(8.0f);

        // 견본이다. 모양마다 막대 하나를 그림으로만 둔다 — 누르거나 초점이 가지 않고 보조 기술에는 이름 하나로 읽힌다.
        // 둘째 탭은 포인터가 올라간 상태로 고정해 hover의 그림도 함께 보인다.
        auto samples { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs-samples" }, luil::stack_config { .direction = luil::stack_direction::row, .spacing = 16.0f }) };
        const std::pair<luil::tab_bar_variant, std::u8string_view> variants[] {
            { luil::tab_bar_variant::underline, u8"기본 모양 견본" },
            { luil::tab_bar_variant::connected, u8"이어진 탭 견본" },
            { luil::tab_bar_variant::flat, u8"평평한 칸 견본" },
        };
        for (const auto& [variant, name] : variants)
        {
            luil::tab_bar_config sample {};
            sample.owner = u8"tabs-sample/" + std::u8string { name };
            sample.tab_width = sample_width / 3.0f;
            sample.selected = u8"a";
            sample.items.push_back({ .key = u8"a", .label = u8"가", .icon = luil::codicons::icon_file, .closable = true });
            sample.items.push_back({ .key = u8"b", .label = u8"나", .icon = luil::codicons::icon_file, .closable = true, .indicator = luil::tab_indicator {} });
            sample.items.push_back({ .key = u8"c", .label = u8"다", .icon = luil::codicons::icon_file, .closable = true });
            luil::tab_bar_style look { style };
            look.variant = variant;
            sample.style = look;
            sample.preview = luil::tab_bar_preview { .name = std::u8string { name }, .hovered = u8"b" };
            samples->add(std::make_unique<luil::tab_bar_element>(std::move(sample)), sample_width);
        }
        column->add(std::move(samples), tab_bar_height);
        return column;
    }

    std::unique_ptr<luil::ui_element> tabs_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(height);
        static_cast<void>(scale);
        bar_ = nullptr;

        luil::tab_bar_config bar_config {};
        bar_config.owner = std::u8string { bar_owner };
        for (std::size_t index = 0; index < tabs_.size(); ++index)
        {
            const tab_entry& tab { tabs_[index] };
            luil::tab_item item {};
            item.key = tab.key;
            item.label = tab.label;
            item.icon = luil::codicons::icon_file;
            item.closable = true;
            // 배지와 상태 표시를 차례로 보인다 — 개수 배지, 저장하지 않음 점, 심각도 배지, 불러오는 중.
            //  - 점과 진행의 뜻은 앱만 알아 설명이 없으면 보조 기술에 읽히지 않는다. 그래서 적는다.
            switch (index % 4)
            {
            case 0:
                item.badge = luil::tab_badge { .text = u8"3", .description = u8"바뀐 곳 3개" };
                break;
            case 1:
                item.indicator = luil::tab_indicator { .kind = luil::tab_indicator_kind::dot, .tone = luil::badge_tone::neutral, .description = u8"저장하지 않음" };
                break;
            case 2:
                item.badge = luil::tab_badge { .tone = luil::badge_tone::error, .description = u8"오류 있음" };
                break;
            default:
                item.indicator = luil::tab_indicator { .kind = luil::tab_indicator_kind::busy, .description = u8"불러오는 중" };
                break;
            }
            bar_config.items.push_back(std::move(item));
        }
        bar_config.selected = selected_;
        bar_config.tab_width = 150.0f;
        bar_config.min_tab_width = shrink_ ? narrow_tab_width : 0.0f;
        bar_config.overflow_mode = step_overflow_ ? luil::tab_overflow_mode::step : luil::tab_overflow_mode::scroll;
        bar_config.wheel_steps = step_overflow_;
        bar_config.step_back_tooltip = u8"앞 탭 보기";
        bar_config.step_forward_tooltip = u8"뒤 탭 보기";
        // 막대 끝의 새 탭 단추다. 탭이 적으면 마지막 탭 뒤에 따라붙고, 넘치면 오른쪽 끝에 선다.
        bar_config.trailing = [] {
            const luil::button_config add { .glyph = luil::codicons::icon_add, .icon_size = 14.0f };
            auto button { std::make_unique<luil::button_element>(luil::ui_element_id { kind_layout, u8"tabs/new-tab" }, add) };
            button->set_tooltip(u8"새 탭 (Ctrl+T)");
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(tab_menu_command_intent { u8"new" }));
            return std::unique_ptr<luil::ui_element> { std::move(button) };
        };
        bar_config.trailing_width = new_tab_width;
        bar_config.scroll_offset = scroll_;
        bar_config.select = [](const std::u8string& key) { return luil::make_app_action(tab_select_intent { key }); };
        bar_config.close = [](const std::u8string& key) { return luil::make_app_action(tab_close_intent { key }); };
        bar_config.move = [](const std::u8string& moved, const std::u8string& target, const luil::drop_side side) { return luil::make_app_action(tab_reorder_intent { moved, target, side }); };
        bar_config.receive = [](const luil::drag_payload& payload, const std::u8string& target, const luil::drop_side side) {
            return luil::make_app_action(tab_attach_intent { payload.dragged_owner, target, side });
        };
        bar_config.unclaimed_drop = [](const luil::drag_payload& payload, const luil::drag_release& release) {
            tab_detach_intent intent { payload.dragged_owner };
            if (release.screen.has_value())
                intent.anchor = luil::window_screen_anchor { *release.screen, 24.0f + payload.grab_offset_x, 34.0f + payload.grab_offset_y };
            return luil::make_app_action(std::move(intent));
        };
        // 막대와 같은 식으로 탭 폭과 레인 폭을 미리 잰다 — 고른 탭을 레인 안으로 들이는 계산이 쓴다.
        //  - 넘치면 꼬리(새 탭)·넘침 메뉴, 한 탭씩이면 앞·뒤 단추까지 레인에서 빠진다.
        const float bar_width { std::max(0.0f, width - 48.0f) };
        const float previous_lane { lane_width_ };
        const float smallest { luil::fitted_tab_width(tabs_.size(), 150.0f, shrink_ ? narrow_tab_width : 0.0f, 0.0f) };
        const bool overflowing { static_cast<float>(tabs_.size()) * smallest + new_tab_width > bar_width };
        tab_width_ = overflowing ? smallest : luil::fitted_tab_width(tabs_.size(), 150.0f, shrink_ ? narrow_tab_width : 0.0f, bar_width - new_tab_width);
        lane_width_ = overflowing ? std::max(0.0f, bar_width - new_tab_width - luil::tab_overflow_width - (step_overflow_ ? 2.0f * luil::tab_overflow_width : 0.0f)) : bar_width;
        if (lane_width_ != previous_lane)
            reveal_selected();
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, static_cast<float>(tabs_.size()) * tab_width_ - lane_width_));
        bar_config.scroll_offset = scroll_;
        // 우클릭(터치는 길게 누르기)한 자리에 컨텍스트 메뉴를 띄운다.
        bar_config.context_menu = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::make_app_action(tab_menu_intent { key, context.x, context.y }); };
        bar_config.overflow = luil::make_message_action(tab_overflow_intent { true });
        bar_config.overflow_tooltip = u8"넘친 탭 보기";
        // 넘친 레인을 손가락으로 옆으로 쓸어 넘긴다. 휠과 같은 메시지다.
        bar_config.scroll = [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); };
        auto bar { std::make_unique<luil::tab_bar_element>(std::move(bar_config)) };
        bar_ = bar.get();

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"tabs-hint" },
                        u8"탭 사이로 끌어 순서를 바꾸고, 창 밖으로 놓아 분리하세요. 우클릭 / Shift+F10으로 탭 메뉴를 열고, 가운데 버튼으로 닫습니다.", 11.0f, luil::label_color_role::dim),
            18.0f);
        column->add_gap(8.0f);
        column->add(make_settings(), 5.0f * luil::choice_toggle_height + 4.0f * luil::choice_toggle_gap + 8.0f + tab_bar_height);
        column->add_gap(12.0f);
        auto toolbar { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs-toolbar" }, luil::stack_config { .direction = luil::stack_direction::row, .spacing = 8.0f }) };
        for (const luil::app_command& command : tab_commands())
        {
            if (command.id != u8"new" && command.id != u8"reopen")
                continue;
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"tabs/" + command.id }, luil::text_button_config { .text = command.label }) };
            button->set_enabled(command.id != u8"reopen" || closed_.empty() == false);
            button->set_tooltip(command.label + u8" (" + luil::shortcut_label(command.shortcut) + u8")");
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(tab_menu_command_intent { command.id }));
            toolbar->add_fit(std::move(button));
        }
        column->add(std::move(toolbar), luil::text_button_height);
        column->add_gap(12.0f);
        column->add(std::move(bar), tab_bar_height);
        // 이어지는 모양은 선택된 탭이 내용 판과 한 판이라 사이를 띄우지 않는다. 판의 바탕이 선택된 탭 바탕(표면)과 같다.
        const bool joined { style_.tab_bar.variant != luil::tab_bar_variant::underline };
        column->add_gap(joined ? 0.0f : 16.0f);

        // 내용 영역은 앱 몫이다: 고른 탭을 보여 주는 판이다.
        std::u8string current { u8"열린 탭이 없다" };
        for (const tab_entry& tab : tabs_)
            if (tab.key == selected_)
                current = tab.label + u8" 내용";
        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = joined ? 0.0f : 6.0f;
        auto content_panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_page_panel, u8"tab-content" }, std::move(surface_panel)) };
        luil::stack_config content_config {};
        content_config.padding = luil::edge_insets::all(16.0f);
        auto content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tab-content" }, content_config) };
        content->add(make_label(luil::ui_element_id { kind_text, u8"tab-current" }, std::move(current), 20.0f, luil::label_color_role::primary), 36.0f);
        content->add(make_label({ kind_text, u8"tab-shortcuts" }, u8"Ctrl+Tab / Ctrl+Shift+Tab 전환 · Ctrl+T 새 탭 · Ctrl+W 닫기 · Ctrl+Shift+T 복원", 12.0f, luil::label_color_role::dim), 28.0f);
        content->add(make_label({ kind_text, u8"tab-detach-help" }, u8"분리된 창의 탭을 이 탭 막대에 끌어 놓으면 다시 합쳐집니다.", 12.0f, luil::label_color_role::dim), 28.0f);
        content_panel->set_content(std::move(content));
        column->add_flexible(std::move(content_panel));
        return std::make_unique<style_scope>(&style_, std::move(column));
    }

    void tabs_page::close_tab(const std::u8string& key)
    {
        for (auto position = tabs_.begin(); position != tabs_.end(); ++position)
        {
            if (position->key != key)
                continue;
            const bool was_selected { selected_ == position->key };
            closed_.push_back(*position);
            position = tabs_.erase(position);
            if (was_selected && tabs_.empty() == false)
                selected_ = (position != tabs_.end() ? position : tabs_.end() - 1)->key;
            if (tabs_.empty())
                selected_.clear();
            reveal_selected();
            break;
        }
    }

    std::vector<luil::ui_popup> tabs_page::make_popups(const float scale) const
    {
        if (menu_key_.empty() == false)
        {
            // 우클릭한 탭의 메뉴다. 우클릭한 자리에 왼쪽 위를 맞춘다.
            luil::menu_config menu {};
            menu.owner = u8"tab-menu";
            menu.items.push_back({ u8"close", u8"닫기", luil::codicons::icon_close });
            luil::menu_item_config others {};
            others.key = u8"close-others";
            others.label = u8"다른 탭 닫기";
            others.enabled = tabs_.size() > 1;
            menu.items.push_back(std::move(others));
            const auto found { std::ranges::find_if(tabs_, [this](const tab_entry& item) { return item.key == menu_key_; }) };
            const bool exists { found != tabs_.end() };
            menu.items.push_back({ .key = u8"close-right", .label = u8"오른쪽 탭 닫기", .enabled = exists && found + 1 != tabs_.end() });
            menu.items.push_back({ .key = u8"close-all", .label = u8"모든 탭 닫기", .enabled = tabs_.empty() == false });
            menu.items.push_back({ .key = u8"move-left", .label = u8"왼쪽으로 이동", .enabled = exists && found != tabs_.begin(), .separator_above = true });
            menu.items.push_back({ .key = u8"move-right", .label = u8"오른쪽으로 이동", .enabled = exists && found + 1 != tabs_.end() });
            menu.items.push_back({ .key = u8"detach", .label = u8"새 창으로 분리", .enabled = exists });
            for (const luil::app_command& command : tab_commands())
            {
                if (command.id != u8"new" && command.id != u8"reopen")
                    continue;
                luil::menu_item_config item { luil::menu_item_from(command) };
                item.separator_above = command.id == u8"new";
                item.enabled = command.id != u8"reopen" || closed_.empty() == false;
                menu.items.push_back(std::move(item));
            }
            menu.items.front().shortcut_text = u8"Ctrl+W";
            menu.select = [](const std::u8string& command) { return luil::make_app_action(tab_menu_command_intent { command }); };

            luil::menu_popup_config popup {};
            popup.id = u8"tab-menu";
            popup.x = menu_x_ / scale;
            popup.y = menu_y_ / scale;
            popup.scale = scale;
            popup.dismiss = [](luil::popup_dismiss_reason) { return luil::make_app_action(tab_menu_close_intent {}); };
            return { luil::make_menu_popup(std::move(menu), std::move(popup)) };
        }

        if (overflow_open_ == false || bar_ == nullptr || tabs_.empty())
            return {};

        // 열린 탭 목록이다. 고른 탭에 표식을 두고 선택하면 메뉴가 함께 닫힌다.
        luil::menu_config menu {};
        menu.owner = u8"tab-overflow";
        for (const tab_entry& tab : tabs_)
        {
            luil::menu_item_config item {};
            item.key = tab.key;
            item.label = tab.label;
            if (tab.key == selected_)
                item.icon = luil::codicons::icon_check;
            menu.items.push_back(std::move(item));
        }
        menu.select = [](const std::u8string& key) { return luil::make_app_action(tab_select_intent { key }); };

        // 탭 막대의 오른쪽 아래에 붙인다.
        // 자리는 이번 frame에서 배치된 막대의 bounds를 논리 픽셀로 되돌려 얻는다.
        const luil::rect_f bar_bounds { bar_->bounds() };
        luil::menu_popup_config popup {};
        popup.id = u8"tab-overflow";
        popup.width = overflow_menu_width;
        popup.scale = scale;
        popup.x = (bar_bounds.x + bar_bounds.width) / scale - overflow_menu_width;
        popup.y = (bar_bounds.y + bar_bounds.height) / scale + 2.0f;
        popup.dismiss = [](luil::popup_dismiss_reason) { return luil::make_app_action(tab_overflow_intent { false }); };
        return { luil::make_menu_popup(std::move(menu), std::move(popup)) };
    }

    void tabs_page::reveal_selected()
    {
        if (lane_width_ <= 0.0f)
            return;
        const auto selected { std::ranges::find_if(tabs_, [this](const tab_entry& item) { return item.key == selected_; }) };
        if (selected == tabs_.end())
        {
            scroll_ = 0.0f;
            return;
        }
        const float left { static_cast<float>(selected - tabs_.begin()) * tab_width_ };
        if (left < scroll_)
            scroll_ = left;
        else if (left + tab_width_ > scroll_ + lane_width_)
            scroll_ = left + tab_width_ - lane_width_;
    }

    void tabs_page::run_command(const std::u8string& command, const std::u8string& key)
    {
        if (command == u8"new")
        {
            const std::u8string number { to_u8(next_tab_++) };
            tabs_.push_back({ u8"tab-" + number, u8"문서 " + number });
            selected_ = tabs_.back().key;
        }
        else if (command == u8"reopen" && closed_.empty() == false)
        {
            tabs_.push_back(closed_.back());
            closed_.pop_back();
            selected_ = tabs_.back().key;
        }
        else if (command == u8"close" || command == u8"close-current")
            close_tab(key);
        else if (command == u8"detach")
            static_cast<void>(handle(luil::app_message { tab_detach_intent { key } }));
        else if (tabs_.empty() == false)
        {
            const auto found { std::ranges::find_if(tabs_, [&key](const tab_entry& item) { return item.key == key; }) };
            if (found == tabs_.end())
                return;
            const auto index { static_cast<std::size_t>(found - tabs_.begin()) };
            if (command == u8"next" || command == u8"previous")
                selected_ = tabs_[(index + (command == u8"next" ? 1 : tabs_.size() - 1)) % tabs_.size()].key;
            if (command == u8"move-left" && index > 0)
                std::iter_swap(found, found - 1);
            if (command == u8"move-right" && index + 1 < tabs_.size())
                std::iter_swap(found, found + 1);
            if (command == u8"close-others" || command == u8"close-right" || command == u8"close-all")
            {
                std::vector<std::u8string> keys {};
                for (std::size_t position = 0; position < tabs_.size(); ++position)
                    if (command == u8"close-all" || (command == u8"close-others" && position != index) || (command == u8"close-right" && position > index))
                        keys.push_back(tabs_[position].key);
                for (const std::u8string& close : keys)
                    close_tab(close);
                if (command == u8"close-others")
                    selected_ = key;
            }
        }
        reveal_selected();
    }

    std::vector<luil::ui_window> tabs_page::make_windows() const
    {
        std::vector<luil::ui_window> windows {};
        for (const detached_tab& item : detached_)
        {
            const std::u8string key { item.tab.key };
            luil::ui_window window {};
            window.id = u8"tab-window/" + key;
            window.caption.title = item.tab.label + u8" — luil";
            window.caption.close_tooltip = u8"탭으로 돌려놓기";
            window.caption.minimize_tooltip = u8"최소화";
            window.caption.maximize_tooltip = u8"최대화 / 복원";
            window.width = item.width;
            window.height = item.height;
            window.minimum_width = 420.0f;
            window.minimum_height = 240.0f;
            window.x = 120.0f;
            window.y = 100.0f;
            window.independent = true;
            window.screen_anchor = item.anchor;
            window.close = [key] { return luil::make_app_action(tab_attach_intent { key }); };
            window.metrics = [key](const float width, const float height, const float scale) { return luil::app_message { tab_window_metrics_intent { key, width, height, scale } }; };
            const float caption_height { luil::caption_element::height_for(window.caption) };
            const float scale { std::max(0.01f, item.scale) };
            luil::stack_config layout {};
            auto root { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"detached-root" }, layout) };
            root->add(std::make_unique<luil::caption_element>(window.caption), caption_height);
            luil::tab_bar_config bar {};
            bar.owner = window.id;
            bar.selected = key;
            bar.items.push_back({ .key = key, .label = item.tab.label, .icon = luil::codicons::icon_file, .closable = true });
            bar.select = [](const std::u8string&) { return luil::input_action {}; };
            bar.close = [key](const std::u8string&) { return luil::make_app_action(tab_attach_intent { key }); };
            // 창으로 떼어 낸 문서의 탭도 같은 drag payload를 낸다. 원래 막대의 receive가 받는다.
            bar.reorder = [](const std::u8string&, const std::u8string&) { return luil::input_action {}; };
            root->add(std::make_unique<luil::tab_bar_element>(std::move(bar)), tab_bar_height);
            luil::stack_config body_config {};
            body_config.padding = luil::edge_insets::all(24.0f);
            auto body { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"detached-body" }, body_config) };
            body->add(make_label({ kind_text, u8"detached-title" }, item.tab.label, 22.0f, luil::label_color_role::primary), 40.0f);
            body->add(make_label({ kind_text, u8"detached-help" }, u8"탭을 원래 창의 탭 막대에 끌어 놓아 합치세요.", 12.0f, luil::label_color_role::dim), 28.0f);
            body->add_gap(16.0f);
            luil::text_button_config restore_config { .text = u8"원래 창으로 돌려놓기", .visual = luil::text_button_visual::accent };
            auto restore { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"detached-restore" }, std::move(restore_config)) };
            restore->set_cursor(luil::ui_cursor::hand);
            restore->set_action(luil::ui_trigger::left_click, luil::make_message_action(tab_attach_intent { key }));
            body->add(std::move(restore), { .length = luil::text_button_height, .cross_length = 200.0f });
            root->add_flexible(std::move(body));
            // 분리된 창의 막대도 같은 앱 스타일로 배치한다 — 고른 모양이 창마다 따로 놀지 않는다.
            window.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, item.width * scale, item.height * scale }, scale, {}, nullptr, &style_));
            windows.push_back(std::move(window));
        }
        return windows;
    }

    std::vector<luil::input_action> tabs_page::route_key(const luil::ui_tree* const tree, const luil::key_pressed_event& event)
    {
        if (tree == nullptr || tree->focus_trap() != nullptr || event.surface.empty() == false || tree->find({ luil::ui_element_kind::tab_bar, std::u8string { bar_owner } }) == nullptr)
            return {};
        if (const auto command { luil::match_command(tab_commands(), event, luil::command_scope::window) }; command.has_value())
            return { luil::make_app_action(tab_menu_command_intent { *command }) };
        return {};
    }

    std::vector<luil::input_action> tabs_page::route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float delta)
    {
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::tab_bar, std::u8string { bar_owner } }, [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); } },
        };
        return luil::route_wheel(tree, event.x, event.y, delta, routes);
    }

    std::vector<luil::input_action> tabs_page::route_reveal(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        // 휠이 대는 바깥 막대가 아니라 **안쪽 레인**을 이름 댄다.
        // 넘침 버튼이 보일 때 막대의 bounds가 실제 레인보다 그 버튼 폭만큼 넓어,
        // 바깥을 대면 마지막 탭이 버튼 밑에 남는다 (focus-reveal-design.md).
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::tab_strip, std::u8string { bar_owner } }, [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); } },
        };
        return luil::route_reveal(tree, focused, routes);
    }
} // namespace demo

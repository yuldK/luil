#include "demo/files_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace demo {
    namespace {
        constexpr float row_height { 26.0f };
        constexpr float grip_width { 22.0f };
        constexpr float panel_inset { 8.0f };
        constexpr float archive_width { 180.0f };
        constexpr float status_height { 18.0f };
        constexpr float button_height { 28.0f };
        // 목록 위아래의 고정 칸이다 (안내·상태 셋·버튼 줄과 그 사이 간격, 바깥 여백).
        constexpr float fixed_height { 2.0f * 24.0f + 18.0f + 8.0f + 3.0f * status_height + 8.0f + 8.0f + button_height };

        const luil::ui_element_id files_list_id { luil::ui_element_kind::virtual_list, u8"files" };
        const luil::ui_element_id archive_list_id { luil::ui_element_kind::virtual_list, u8"archive" };

        // 폴더 행의 키는 `folder-`로 시작한다. 놓을 수 있는 것은 폴더뿐이다.
        [[nodiscard]] bool is_folder(const std::u8string_view key)
        {
            return key.starts_with(u8"folder-");
        }

        // 데모의 파일은 이 폴더 아래에 있는 것으로 친다 (실제로 만들지 않는다).
        [[nodiscard]] std::u8string path_of(const std::u8string& label)
        {
            return u8"C:\\luil-demo\\" + label;
        }

        [[nodiscard]] std::u8string count_text(const std::size_t count)
        {
            return to_u8(static_cast<int>(count)) + u8"개";
        }

        // 목록 하나를 감싼 판이다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_surface(const std::u8string& owner, std::unique_ptr<luil::ui_element> content)
        {
            luil::stack_config inset {};
            inset.padding = luil::edge_insets::all(panel_inset);
            auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, owner + u8"-inset" }, inset) };
            column->add_flexible(std::move(content));

            luil::panel_config surface {};
            surface.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
            surface.corner_radius = 6.0f;
            auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_list_panel, owner }, std::move(surface)) };
            panel->set_content(std::move(column));
            return panel;
        }
    }

    files_page::files_page()
    {
        for (const char8_t* const name : { u8"문서", u8"사진", u8"보관" })
        {
            const std::u8string label { name };
            items_.push_back({ u8"folder-" + label, label });
        }
        for (int index = 1; index <= 120; ++index)
        {
            std::u8string number { to_u8(index) };
            while (number.size() < 3u)
                number.insert(number.begin(), u8'0');
            items_.push_back({ u8"file-" + number, u8"파일 " + number + u8".txt" });
        }
        selection_ = { u8"file-001", u8"file-002", u8"file-003" };
        cursor_ = u8"file-001";
        anchor_ = cursor_;
    }

    std::optional<std::size_t> files_page::index_of(const std::u8string& key) const
    {
        for (std::size_t index = 0; index < items_.size(); ++index)
            if (items_[index].key == key)
                return index;
        return std::nullopt;
    }

    void files_page::select_range(const std::size_t from, const std::size_t to)
    {
        selection_.clear();
        for (std::size_t index = std::min(from, to); index <= std::max(from, to) && index < items_.size(); ++index)
            selection_.push_back(items_[index].key);
    }

    float files_page::clamp(const float offset) const
    {
        return luil::clamp_scroll(luil::virtual_list_content_height(items_, row_height), list_height_, offset);
    }

    std::vector<std::u8string> files_page::selected_paths() const
    {
        std::vector<std::u8string> paths {};
        for (const luil::virtual_list_item& item : items_)
            if (std::find(selection_.begin(), selection_.end(), item.key) != selection_.end())
                paths.push_back(path_of(item.label));
        return paths;
    }

    bool files_page::handle(const luil::app_message& message)
    {
        if (const auto* const pick { message.get<files_pick_intent>() }; pick != nullptr)
        {
            // Shift는 범위의 시작부터 누른 행까지, Ctrl은 하나 더하거나 빼기, 그냥 누르면 그것 하나다.
            const std::optional<std::size_t> target { index_of(pick->key) };
            const std::optional<std::size_t> anchor { index_of(anchor_) };
            if (pick->shift && target.has_value() && anchor.has_value())
                select_range(*anchor, *target);
            else if (pick->control)
            {
                if (const auto found { std::find(selection_.begin(), selection_.end(), pick->key) }; found != selection_.end())
                    selection_.erase(found);
                else
                    selection_.push_back(pick->key);
                anchor_ = pick->key;
            }
            else
            {
                selection_ = { pick->key };
                anchor_ = pick->key;
            }
            cursor_ = pick->key;
            return true;
        }
        if (const auto* const add { message.get<files_add_intent>() }; add != nullptr)
        {
            if (std::find(selection_.begin(), selection_.end(), add->key) == selection_.end())
                selection_.push_back(add->key);
            last_action_ = u8"보조 기술이 더함: " + add->key;
            return true;
        }
        if (const auto* const remove { message.get<files_remove_intent>() }; remove != nullptr)
        {
            std::erase(selection_, remove->key);
            last_action_ = u8"보조 기술이 뺌: " + remove->key;
            return true;
        }
        if (const auto* const cursor { message.get<files_cursor_intent>() }; cursor != nullptr)
        {
            cursor_ = cursor->key;
            return true;
        }
        if (const auto* const scroll { message.get<files_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = clamp(scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll_to { message.get<files_scroll_to_intent>() }; scroll_to != nullptr)
        {
            scroll_ = clamp(scroll_to->offset);
            return true;
        }
        if (const auto* const drop { message.get<files_drop_intent>() }; drop != nullptr)
        {
            last_action_ = count_text(drop->items.size()) + u8"를 " + drop->list + u8"/" + drop->folder.substr(7) + (drop->copy ? u8"에 복사" : u8"로 옮김");
            return true;
        }
        if (const auto* const start { message.get<files_range_start_intent>() }; start != nullptr)
        {
            if (const std::optional<std::size_t> anchor { index_of(start->key) }; anchor.has_value())
            {
                range_ = range_drag { *anchor, start->y, scroll_ };
                select_range(*anchor, *anchor);
                anchor_ = start->key;
                cursor_ = start->key;
            }
            return true;
        }
        if (const auto* const moved { message.get<files_range_move_intent>() }; moved != nullptr)
        {
            // 표면 좌표의 이동과 그 사이 흐른 만큼을 더해 내용에서 몇 줄 움직였는지 센다. 가장자리 흐름의 스크롤
            // 메시지가 이 이동보다 먼저 오므로 `scroll_`에는 방금 흐른 만큼이 들어 있다.
            if (range_.has_value())
            {
                const float travel { (moved->y - range_->press_y) / scale_ + (scroll_ - range_->press_scroll) };
                const long long rows { std::llround(travel / row_height) };
                const long long last { static_cast<long long>(items_.size()) - 1 };
                const std::size_t target { static_cast<std::size_t>(std::clamp(static_cast<long long>(range_->anchor) + rows, 0LL, last)) };
                select_range(range_->anchor, target);
                cursor_ = items_[target].key;
            }
            return true;
        }
        if (message.get<files_range_end_intent>() != nullptr)
        {
            range_.reset();
            last_action_ = u8"손잡이로 " + count_text(selection_.size()) + u8" 고름";
            return true;
        }
        if (message.get<files_clear_intent>() != nullptr)
        {
            pasted_sequence_.reset();
            last_action_ = u8"옮긴 뒤 비우기를 청함 (그 사이 클립보드가 바뀌었으면 남는다)";
            return true;
        }
        if (message.get<files_cut_intent>() != nullptr)
        {
            cut_keys_ = selection_;
            cut_pending_ = true;
            cut_sequence_.reset();
            last_action_ = count_text(cut_keys_.size()) + u8" 잘라 둠";
            return true;
        }
        if (const auto* const changed { message.get<files_clipboard_changed_intent>() }; changed != nullptr)
        {
            // 잘라 둔 표시는 `owned`만으로 정하지 않는다. 같은 앱이 글을 복사하거나 비워도 임자는 이 앱이다.
            // 잘라 넣은 뒤 처음 온 번호를 적어 두고, 그 번호의 파일 목록일 때만 표시를 남긴다.
            const luil::clipboard_change& change { changed->change };
            const bool ours { change.owned && change.has_files };
            if (cut_pending_ && ours)
            {
                cut_pending_ = false;
                cut_sequence_ = change.sequence;
            }
            else if (cut_keys_.empty() == false && (ours == false || cut_sequence_ != change.sequence))
            {
                cut_keys_.clear();
                cut_pending_ = false;
                cut_sequence_.reset();
            }
            return true;
        }
        if (const auto* const read { message.get<files_clipboard_read_intent>() }; read != nullptr)
        {
            const luil::file_clipboard_content& content { read->content };
            if (content.supported == false)
                clipboard_text_ = u8"이 플랫폼은 파일 클립보드를 다루지 않는다";
            else if (content.paths.empty())
                clipboard_text_ = u8"붙일 파일이 없다";
            else
            {
                clipboard_text_ = count_text(content.paths.size()) + (content.intent == luil::file_clipboard_intent::cut ? u8" 옮기기" : u8" 복사") + u8": " + content.paths.front();
                if (content.paths.size() > 1u)
                    clipboard_text_ += u8" 외";
            }
            // 잘라 온 것을 옮겼으면 클립보드를 비울 차례다. 읽은 번호를 들고 있다가 그 번호일 때만 비운다.
            pasted_sequence_.reset();
            if (content.intent == luil::file_clipboard_intent::cut && content.paths.empty() == false)
                pasted_sequence_ = content.sequence;
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> files_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        scale_ = scale > 0.0f ? scale : 1.0f;
        list_height_ = std::max(height - fixed_height - 2.0f * panel_inset, 4.0f * row_height);
        scroll_ = clamp(scroll_);

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"files" }, config) };
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"files-hint" },
                u8"Ctrl·Shift+클릭으로 여럿을 고른다. 고른 것을 끌어 폴더에 놓고(Ctrl은 복사), 왼쪽 손잡이를 끌어 범위를 고른다 — 가장자리에 대면 목록이 흐른다.", 11.0f, luil::label_color_role::dim),
            18.0f);
        column->add_gap(8.0f);
        column->add(make_label(
                        luil::ui_element_id { kind_text, u8"files-selection" },
                        u8"고른 것 " + count_text(selection_.size()) + u8": " +
                            [this] {
                                std::u8string text {};
                                for (std::size_t index = 0; index < selection_.size() && index < 6u; ++index)
                                    text += (index == 0 ? u8"" : u8", ") + selection_[index];
                                if (selection_.size() > 6u)
                                    text += u8", …";
                                return text;
                            }(),
                        11.0f, luil::label_color_role::primary),
            status_height);
        column->add(make_label(luil::ui_element_id { kind_text, u8"files-action" }, u8"마지막 동작: " + last_action_, 11.0f, luil::label_color_role::primary), status_height);
        column->add(make_label(luil::ui_element_id { kind_text, u8"files-clipboard" },
                        u8"클립보드: " + clipboard_text_ + (cut_keys_.empty() ? u8"" : u8"  ·  잘라 둔 것 " + count_text(cut_keys_.size())), 11.0f, luil::label_color_role::primary),
            status_height);
        column->add_gap(8.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 12.0f;
        auto lists { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"files-lists" }, row_config) };
        lists->add_flexible(make_surface(u8"files", make_files_list()));
        lists->add(make_surface(u8"archive", make_archive_list()), archive_width);
        column->add(std::move(lists), list_height_ + 2.0f * panel_inset);
        column->add_gap(8.0f);
        column->add(make_buttons(), button_height);
        return column;
    }

    std::unique_ptr<luil::ui_element> files_page::make_files_list()
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.items = items_;
        config.row_height = row_height;
        config.multiple_selection = true;
        config.selection = selection_;
        config.cursor = cursor_;
        config.scroll_offset = scroll_;

        // 행 왼쪽의 손잡이가 범위를 고른다. 누르고 조금 움직여야 끌기다 — 그냥 누르면 행을 누른 것과 같다.
        // 잘라 둔 행은 글을 흐리게 그린다.
        config.build_row = [cut = cut_keys_](const luil::virtual_list_item& item, const std::size_t, const bool selected) -> std::unique_ptr<luil::ui_element> {
            luil::stack_config row_config {};
            row_config.direction = luil::stack_direction::row;
            row_config.padding = luil::edge_insets::symmetric(4.0f, 2.0f);
            row_config.spacing = 6.0f;
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"files-row-" + item.key }, row_config) };

            luil::label_config grip_config {};
            grip_config.text = u8"≡";
            grip_config.font_size = 13.0f;
            grip_config.color = luil::label_color_role::dim;
            auto grip { std::make_unique<luil::label_element>(luil::ui_element_id { kind_files_grip, item.key }, std::move(grip_config)) };
            grip->set_tooltip(u8"끌어서 범위 고르기");
            luil::pointer_drag_target range {};
            range.start_distance = 4.0f;
            range.auto_scroll = true;
            range.on_press
                = [key = item.key](const luil::ui_action_context& context) -> std::vector<luil::input_action> { return { luil::make_app_action(files_range_start_intent { key, context.y }) }; };
            range.on_move = [](const luil::ui_action_context&, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
                return { luil::make_app_action(files_range_move_intent { current.y }) };
            };
            range.on_release = [](const luil::ui_action_context&, luil::drag_velocity) -> std::vector<luil::input_action> { return { luil::make_app_action(files_range_end_intent {}) }; };
            grip->set_pointer_drag_target(std::move(range));
            row->add(std::move(grip), grip_width);

            const bool is_cut { std::find(cut.begin(), cut.end(), item.key) != cut.end() };
            const luil::label_color_role color { is_cut ? luil::label_color_role::dim : (selected || is_folder(item.key) ? luil::label_color_role::primary : luil::label_color_role::dim) };
            row->add_flexible(make_label(luil::ui_element_id { kind_text, u8"files-label-" + item.key }, is_folder(item.key) ? u8"[" + item.label + u8"]" : item.label, 12.0f, color));
            return row;
        };

        config.pick = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::make_app_action(files_pick_intent { key, context.control, context.shift }); };
        config.move = [](const std::u8string& key) { return luil::make_app_action(files_cursor_intent { key }); };
        config.add_to_selection = [](const std::u8string& key) { return luil::make_app_action(files_add_intent { key }); };
        config.remove_from_selection = [](const std::u8string& key) { return luil::make_app_action(files_remove_intent { key }); };
        config.scroll = [](const float delta) { return luil::make_app_action(files_scroll_intent { delta }); };
        config.scroll_to = [](const float offset) { return luil::make_app_action(files_scroll_to_intent { offset }); };

        // 고른 행을 끌면 고른 것 모두가, 고르지 않은 행을 끌면 그것 하나가 간다.
        config.drag = [selection = selection_, labels = items_](const std::u8string& key, const luil::ui_action_context&) {
            luil::drag_payload payload {};
            payload.dragged_owner = key;
            if (std::find(selection.begin(), selection.end(), key) != selection.end())
                payload.dragged_owners = selection;
            for (const luil::virtual_list_item& item : labels)
                if (item.key == key)
                    payload.label = item.label;
            payload.rejected_cursor = luil::ui_cursor::not_allowed;
            return payload;
        };
        // 폴더 행에만 놓는다. 끌리는 폴더 자신은 받지 않는다 — 출발 목록까지 견주므로 보관함의 같은 키 폴더는 받는다.
        config.accepts_drop = [](const std::u8string& key, const luil::drag_payload& payload) { return is_folder(key) && payload.carries(files_list_id, key) == false; };
        config.drop = [](const std::u8string& key, const luil::drag_payload& payload, const luil::ui_action_context& context) {
            std::vector<std::u8string> items { payload.dragged_owners.empty() ? std::vector<std::u8string> { payload.dragged_owner } : payload.dragged_owners };
            return luil::make_app_action(files_drop_intent { u8"파일", key, std::move(items), context.control });
        };
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        list->set_access_name(u8"파일");
        return list;
    }

    std::unique_ptr<luil::ui_element> files_page::make_archive_list()
    {
        // 다른 목록이다. 행이 모두 폴더이고 키가 파일 목록의 폴더와 겹친다 — 키만 견주면 파일 목록에서 끌어 온 `문서`
        // 폴더를 이쪽 `문서`가 자기 자신으로 알고 거절한다.
        luil::virtual_list_config config {};
        config.owner = u8"archive";
        for (const char8_t* const name : { u8"문서", u8"사진", u8"지난해" })
        {
            const std::u8string label { name };
            config.items.push_back({ u8"folder-" + label, u8"[" + label + u8"]" });
        }
        config.row_height = row_height;
        config.accepts_drop = [](const std::u8string& key, const luil::drag_payload& payload) { return payload.carries(archive_list_id, key) == false; };
        config.drop = [](const std::u8string& key, const luil::drag_payload& payload, const luil::ui_action_context& context) {
            std::vector<std::u8string> items { payload.dragged_owners.empty() ? std::vector<std::u8string> { payload.dragged_owner } : payload.dragged_owners };
            return luil::make_app_action(files_drop_intent { u8"보관함", key, std::move(items), context.control });
        };
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        list->set_access_name(u8"보관함");
        return list;
    }

    std::unique_ptr<luil::ui_element> files_page::make_buttons() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"files-buttons" }, config) };
        const auto add = [&row](const std::u8string& owner, std::u8string text, std::vector<luil::input_action> actions, const bool enabled) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_files_button, owner }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, [actions = std::move(actions)](const luil::ui_action_context&) { return actions; });
            button->set_enabled(enabled);
            row->add(std::move(button), 110.0f);
        };
        const std::vector<std::u8string> paths { selected_paths() };
        add(u8"files-copy", u8"복사", { luil::clipboard_files_copy_request { paths, luil::file_clipboard_intent::copy } }, paths.empty() == false);
        add(u8"files-cut", u8"잘라내기", { luil::clipboard_files_copy_request { paths, luil::file_clipboard_intent::cut }, luil::make_app_action(files_cut_intent {}) }, paths.empty() == false);
        add(u8"files-paste", u8"붙여넣기", { luil::clipboard_files_paste_request { 1u } }, true);
        // 잘라 온 것을 옮긴 뒤 클립보드를 비운다. 읽은 순간의 번호일 때만이라, 그 사이 다른 것을 복사했으면 남는다.
        add(u8"files-clear", u8"옮긴 뒤 비우기", { luil::clipboard_files_copy_request { {}, luil::file_clipboard_intent::copy, pasted_sequence_ }, luil::make_app_action(files_clear_intent {}) },
            pasted_sequence_.has_value());
        return row;
    }
}

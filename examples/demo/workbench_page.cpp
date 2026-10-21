#include "demo/workbench_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/badge_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/grid_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

namespace demo {
    namespace {
        // 흉내 낸 서버가 폴더 목록을 돌려주기까지의 시간이다.
        constexpr std::chrono::milliseconds load_delay { 700 };
        constexpr float workbench_menu_width { 180.0f };

        [[nodiscard]] luil::ui_element_id workbench_id(std::u8string owner)
        {
            return { kind_workbench, std::move(owner) };
        }

        [[nodiscard]] luil::tree_node folder(std::u8string key, std::u8string label, std::vector<luil::tree_node> children = {}, const luil::node_load load = luil::node_load::loaded)
        {
            luil::tree_node node {};
            node.key = std::move(key);
            node.label = std::move(label);
            node.children = std::move(children);
            node.load = load;
            return node;
        }

        [[nodiscard]] luil::tree_node file(std::u8string key, std::u8string label)
        {
            return folder(std::move(key), std::move(label));
        }

        // 저장소 계층의 키는 `repo:` 뒤에 경로다.
        //  - 가상 목록 행의 자리표는 (행 kind, 키)라 표면 안의 목록 전체에서 키가 유일해야 한다. 변경 표의 행 키가 경로라
        //    같은 파일(trunk/CMakeLists.txt)이 두 목록에 서면 자리표가 겹친다 (`ui_tree::duplicate_ids`).

        // 묻는 폴더마다 서버가 돌려줄 목록이다.
        [[nodiscard]] std::vector<luil::tree_node> children_for(const std::u8string_view key)
        {
            if (key == u8"repo:trunk/src")
                return {
                    folder(u8"repo:trunk/src/ui", u8"ui", { file(u8"repo:trunk/src/ui/table.cpp", u8"table.cpp"), file(u8"repo:trunk/src/ui/list.cpp", u8"list.cpp") }),
                    file(u8"repo:trunk/src/main.cpp", u8"main.cpp"),
                };
            if (key == u8"repo:branches")
                return { file(u8"repo:branches/release-0.1", u8"release-0.1"), file(u8"repo:branches/release-0.2", u8"release-0.2"), file(u8"repo:branches/feature-table", u8"feature-table") };
            if (key == u8"repo:tags")
                return { file(u8"repo:tags/v0.1", u8"v0.1"), file(u8"repo:tags/v0.2", u8"v0.2") };
            if (key == u8"repo:archive")
                return { file(u8"repo:archive/old-trunk", u8"old-trunk") };
            return {};
        }

        [[nodiscard]] luil::tree_node* find_in(std::vector<luil::tree_node>& nodes, const std::u8string& key)
        {
            for (luil::tree_node& node : nodes)
            {
                if (node.key == key)
                    return &node;
                if (luil::tree_node* const found { find_in(node.children, key) }; found != nullptr)
                    return found;
            }
            return nullptr;
        }

        // 실패 줄의 키에서 그 노드의 키다. 실패 줄이 아니면 빈 값이다.
        [[nodiscard]] std::u8string failed_node(const std::u8string_view key)
        {
            if (key.ends_with(luil::tree_failed_suffix) == false)
                return {};
            return std::u8string { key.substr(0, key.size() - luil::tree_failed_suffix.size()) };
        }

        [[nodiscard]] std::u8string size_text(const int size)
        {
            return to_u8(size) + u8" B";
        }
    } // namespace

    workbench_page::workbench_page()
    {
        repository_ = {
            folder(u8"repo:trunk", u8"trunk",
                {
                    folder(u8"repo:trunk/src", u8"src", {}, luil::node_load::unloaded),
                    folder(u8"repo:trunk/docs", u8"docs", { file(u8"repo:trunk/docs/README.md", u8"README.md"), file(u8"repo:trunk/docs/table.md", u8"table.md") }),
                    file(u8"repo:trunk/CMakeLists.txt", u8"CMakeLists.txt"),
                }),
            folder(u8"repo:branches", u8"branches", {}, luil::node_load::unloaded),
            folder(u8"repo:tags", u8"tags", {}, luil::node_load::unloaded),
            folder(u8"repo:archive", u8"archive", {}, luil::node_load::unloaded),
        };
        expanded_ = { u8"repo:trunk" };
        tree_cursor_ = u8"repo:trunk";

        changes_ = {
            { u8"trunk/src/ui/table.cpp", u8"수정됨", 18432, true },
            { u8"trunk/src/ui/list.cpp", u8"수정됨", 9216, false },
            { u8"trunk/docs/table.md", u8"추가됨", 2048, true },
            { u8"trunk/CMakeLists.txt", u8"수정됨", 1536, false },
            { u8"trunk/src/main.cpp", u8"지워짐", 0, false },
        };
        // 흘릴 만큼의 행이다.
        for (int index = 1; index <= 40; ++index)
        {
            std::u8string number { to_u8(index) };
            if (index < 10)
                number.insert(number.begin(), u8'0');
            changes_.push_back({ u8"trunk/tests/case_" + number + u8".cpp", u8"추가됨", 512 + index * 37, false });
        }

        columns_ = {
            { .key = u8"path", .header = u8"경로", .width = 340.0f, .min_width = 120.0f },
            { .key = u8"status", .header = u8"상태", .width = 90.0f },
            { .key = u8"size", .header = u8"크기", .width = 100.0f, .align = luil::column_align::end },
        };
        const std::vector<luil::virtual_list_item> items { change_items() };
        selection_ = luil::selection_reconcile(selection_, {}, items);
    }

    luil::tree_node* workbench_page::find_node(const std::u8string& key)
    {
        return find_in(repository_, key);
    }

    void workbench_page::start_loads(const std::chrono::steady_clock::time_point now)
    {
        // 펼쳤지만 묻지 않은 폴더마다 표를 받아 요청을 낸다. 노드는 "묻는 중"이 되고, 평탄화가 그 아래에 불러오는 중 줄을 둔다.
        for (const std::u8string& key : luil::tree_nodes_to_load(repository_, expanded_))
        {
            luil::tree_node* const node { find_node(key) };
            if (node == nullptr)
                continue;
            node->load = luil::node_load::loading;
            pending_.push_back({ key, loads_.issue(key), now + load_delay });
        }
    }

    std::optional<std::chrono::steady_clock::time_point> workbench_page::next_due() const
    {
        std::optional<std::chrono::steady_clock::time_point> next {};
        for (const pending_load& load : pending_)
            if (next.has_value() == false || load.due < *next)
                next = load.due;
        return next;
    }

    void workbench_page::tick(const std::chrono::steady_clock::time_point now)
    {
        std::vector<pending_load> waiting {};
        for (pending_load& load : pending_)
        {
            if (load.due > now)
            {
                waiting.push_back(std::move(load));
                continue;
            }
            // 거둔 요청이나 같은 폴더를 다시 물은 뒤의 옛 요청이면 결과를 버린다.
            if (loads_.settle(load.key, load.token) == false)
                continue;
            luil::tree_node* const node { find_node(load.key) };
            if (node == nullptr)
                continue;
            if (load.key == u8"repo:archive" && archive_attempts_++ == 0)
            {
                node->load = luil::node_load::failed;
                continue;
            }
            node->children = children_for(load.key);
            node->load = luil::node_load::loaded;
        }
        pending_ = std::move(waiting);
    }

    std::vector<luil::virtual_list_item> workbench_page::change_items() const
    {
        // 원본은 그대로 두고 보기의 순서만 정렬한다. 같은 값끼리는 원본 순서다.
        const bool descending { sort_direction_ == luil::sort_direction::descending };
        const std::u8string column { sort_column_ };
        const auto less = [column, descending](const change& left, const change& right) {
            const change& first { descending ? right : left };
            const change& second { descending ? left : right };
            if (column == u8"size")
                return first.size < second.size;
            if (column == u8"status")
                return first.status < second.status;
            return first.path < second.path;
        };
        const luil::view_order order { luil::sort_filter(changes_, less) };
        std::vector<luil::virtual_list_item> items {};
        items.reserve(order.source_index.size());
        for (const std::size_t index : order.source_index)
            items.push_back({ changes_[index].path, changes_[index].path });
        return items;
    }

    std::vector<workbench_page::change>::iterator workbench_page::find_change(const std::u8string& key)
    {
        return std::find_if(changes_.begin(), changes_.end(), [&key](const change& item) { return item.path == key; });
    }

    void workbench_page::reconcile(const std::vector<luil::virtual_list_item>& previous)
    {
        selection_ = luil::selection_reconcile(selection_, previous, change_items());
    }

    void workbench_page::stage_selection(const bool staged)
    {
        std::vector<std::u8string> keys { selection_.keys };
        if (keys.empty() && selection_.cursor.empty() == false)
            keys.push_back(selection_.cursor);
        for (const std::u8string& key : keys)
            if (const auto found { find_change(key) }; found != changes_.end())
                found->staged = staged;
    }

    void workbench_page::close_popups() noexcept
    {
        menu_open_ = false;
    }

    bool workbench_page::handle(const luil::app_message& message)
    {
        const auto now { std::chrono::steady_clock::now() };
        if (const auto* const expand { message.get<workbench_tree_expand_intent>() }; expand != nullptr)
        {
            if (expand->expanded)
            {
                expanded_.insert(expand->key);
                start_loads(now);
                return true;
            }
            expanded_.erase(expand->key);
            // 묻는 중에 접었으면 요청을 거둔다. 다시 펼치면 새로 묻고, 옛 요청의 결과는 버려진다.
            if (luil::tree_node* const node { find_node(expand->key) }; node != nullptr && node->load == luil::node_load::loading)
            {
                loads_.cancel(expand->key);
                node->load = luil::node_load::unloaded;
            }
            return true;
        }
        if (const auto* const cursor { message.get<workbench_tree_cursor_intent>() }; cursor != nullptr)
        {
            tree_cursor_ = cursor->key;
            return true;
        }
        const auto retry = [this, now](const std::u8string& key) {
            luil::tree_node* const node { find_node(key) };
            if (node == nullptr || node->load != luil::node_load::failed)
                return;
            node->load = luil::node_load::unloaded;
            start_loads(now);
        };
        if (const auto* const activate { message.get<workbench_tree_activate_intent>() }; activate != nullptr)
        {
            retry(failed_node(activate->key));
            return true;
        }
        if (const auto* const again { message.get<workbench_tree_retry_intent>() }; again != nullptr)
        {
            retry(again->key);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_tree_scroll_intent>() }; scroll != nullptr)
        {
            tree_scroll_ = std::max(0.0f, tree_scroll_ + scroll->delta);
            return true;
        }

        if (const auto* const pick { message.get<workbench_change_pick_intent>() }; pick != nullptr)
        {
            selection_ = luil::selection_pick(selection_, change_items(), pick->key, pick->context);
            return true;
        }
        if (const auto* const move { message.get<workbench_change_move_intent>() }; move != nullptr)
        {
            selection_ = luil::selection_move(selection_, change_items(), move->key, move->context);
            return true;
        }
        if (const auto* const toggle { message.get<workbench_change_toggle_intent>() }; toggle != nullptr)
        {
            selection_ = luil::selection_toggle(selection_, change_items(), toggle->key);
            return true;
        }
        if (message.get<workbench_change_select_all_intent>() != nullptr)
        {
            selection_ = luil::selection_select_all(selection_, change_items());
            return true;
        }
        if (const auto* const check { message.get<workbench_change_check_intent>() }; check != nullptr)
        {
            if (const auto found { find_change(check->key) }; found != changes_.end())
                found->staged = found->staged == false;
            return true;
        }
        if (const auto* const sort { message.get<workbench_sort_intent>() }; sort != nullptr)
        {
            const std::vector<luil::virtual_list_item> previous { change_items() };
            if (sort->column == sort_column_)
                sort_direction_ = sort_direction_ == luil::sort_direction::ascending ? luil::sort_direction::descending : luil::sort_direction::ascending;
            else
            {
                sort_column_ = sort->column;
                sort_direction_ = luil::sort_direction::ascending;
            }
            reconcile(previous);
            return true;
        }
        if (const auto* const resize { message.get<workbench_resize_intent>() }; resize != nullptr)
        {
            for (luil::table_column& column : columns_)
                if (column.key == resize->column)
                    column.width += luil::table_resize_delta(column, resize->delta);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_change_scroll_intent>() }; scroll != nullptr)
        {
            changes_scroll_ = std::max(0.0f, changes_scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_change_scroll_x_intent>() }; scroll != nullptr)
        {
            changes_scroll_x_ = std::max(0.0f, changes_scroll_x_ + scroll->delta);
            return true;
        }
        if (const auto* const menu { message.get<workbench_menu_intent>() }; menu != nullptr)
        {
            // 고르지 않은 행을 우클릭하면 그 행 하나를 고르고 연다 (탐색기의 규칙).
            if (luil::selection_contains(selection_, menu->key) == false)
                selection_ = luil::selection_pick(selection_, change_items(), menu->key, {});
            menu_open_ = true;
            menu_x_ = menu->x;
            menu_y_ = menu->y;
            return true;
        }
        if (message.get<workbench_menu_close_intent>() != nullptr)
        {
            menu_open_ = false;
            return true;
        }
        if (const auto* const select { message.get<workbench_menu_select_intent>() }; select != nullptr)
        {
            menu_open_ = false;
            if (select->command == u8"stage" || select->command == u8"unstage")
                stage_selection(select->command == u8"stage");
            else if (select->command == u8"discard")
            {
                const std::vector<luil::virtual_list_item> previous { change_items() };
                std::erase_if(changes_, [this](const change& item) { return luil::selection_contains(selection_, item.path); });
                reconcile(previous);
            }
            return true;
        }
        if (message.get<workbench_stage_all_intent>() != nullptr)
        {
            const bool all { std::all_of(changes_.begin(), changes_.end(), [](const change& item) { return item.staged; }) };
            for (change& item : changes_)
                item.staged = all == false;
            return true;
        }
        if (message.get<workbench_commit_intent>() != nullptr)
        {
            const auto staged { std::count_if(changes_.begin(), changes_.end(), [](const change& item) { return item.staged; }) };
            if (staged == 0 || summary_.text.empty())
                return true;
            const std::vector<luil::virtual_list_item> previous { change_items() };
            std::erase_if(changes_, [](const change& item) { return item.staged; });
            last_commit_ = summary_.text + u8" · 파일 " + to_u8(static_cast<int>(staged)) + u8"개";
            summary_ = {};
            composition_.reset();
            reconcile(previous);
            return true;
        }
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            if (edit->request.target != target_workbench_summary)
                return false;
            luil::apply_text_edit(summary_, edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_workbench_summary)
                return false;
            if (composition->event.composing)
                composition_ = composition->event;
            else
                composition_.reset();
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> workbench_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(16.0f);
        config.spacing = 12.0f;
        auto root { std::make_unique<luil::stack_element>(workbench_id(u8"root"), config) };
        // 머리 줄과 양식은 내용 크기로 담는다 — 버튼 글·라벨 폭이 언어와 글꼴에 따라 바뀌어도 손으로 고칠 길이가 없다.
        root->add_fit(make_header(scale));

        luil::stack_config body_config {};
        body_config.direction = luil::stack_direction::row;
        body_config.spacing = 12.0f;
        auto body { std::make_unique<luil::stack_element>(workbench_id(u8"body"), body_config) };
        // 저장소 판은 창을 따라 넓어지되 300을 넘지 않는다. 남는 폭은 표가 갖는다 (stack 상한의 재분배).
        body->add(make_tree_panel(), luil::stack_item { .weight = 1.0f, .minimum = 200.0f, .maximum = 300.0f });
        body->add_flexible(make_changes_table(), 3.0f);
        root->add_flexible(std::move(body));
        root->add_fit(make_form());
        return root;
    }

    std::unique_ptr<luil::ui_element> workbench_page::make_header(const float scale)
    {
        static_cast<void>(scale);
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 12.0f;
        config.cross_alignment = luil::stack_alignment::center;
        auto row { std::make_unique<luil::stack_element>(workbench_id(u8"header"), config) };
        row->add_fit(std::make_unique<luil::label_element>(workbench_id(u8"title"), luil::label_config { .text = u8"변경 작업대", .font_size = 16.0f, .color = luil::label_color_role::primary }));
        row->add_flexible_gap();

        // 전체 스테이지 체크다. 일부만 스테이지했으면 일부 상태로 그리고 읽힌다.
        std::vector<luil::check_state> states {};
        for (const change& item : changes_)
            states.push_back(item.staged ? luil::check_state::checked : luil::check_state::unchecked);
        const luil::check_state all { luil::aggregate_check(states) };
        luil::check_config all_config {};
        all_config.owner = u8"workbench-stage-all";
        all_config.label = u8"모두 스테이지";
        all_config.checked = all == luil::check_state::checked;
        all_config.mixed = all == luil::check_state::mixed;
        all_config.toggle = luil::make_message_action(workbench_stage_all_intent {});
        row->add_fit(std::make_unique<luil::check_element>(std::move(all_config)));

        // 커밋 버튼 모서리에 스테이지한 수를 겹쳐 둔다 (겹침 배치).
        const auto staged { std::count_if(changes_.begin(), changes_.end(), [](const change& item) { return item.staged; }) };
        luil::text_button_config commit_config {};
        commit_config.text = u8"커밋";
        commit_config.visual = luil::text_button_visual::accent;
        commit_config.glyph = luil::codicons::icon_check;
        auto commit { std::make_unique<luil::text_button_element>(workbench_id(u8"commit"), std::move(commit_config)) };
        commit->set_action(luil::ui_trigger::left_click, luil::make_message_action(workbench_commit_intent {}));
        commit->set_enabled(staged > 0 && summary_.text.empty() == false);
        commit->set_tooltip(u8"요약을 쓰고 스테이지한 파일을 커밋한다");
        auto layer { std::make_unique<luil::layer_element>(workbench_id(u8"commit-layer")) };
        layer->add(std::move(commit));
        if (staged > 0)
            layer->add(std::make_unique<luil::badge_element>(workbench_id(u8"commit-count"), luil::badge_config { .text = to_u8(static_cast<int>(staged)), .tone = luil::badge_tone::accent }),
                { .horizontal = luil::layout_alignment::end, .vertical = luil::layout_alignment::start, .margin = { 0.0f, -8.0f, -8.0f, 0.0f } });
        row->add_fit(std::move(layer));
        return row;
    }

    std::unique_ptr<luil::ui_element> workbench_page::make_tree_panel()
    {
        luil::tree_flatten_options options {};
        options.loading_label = u8"불러오는 중…";
        options.failed_label = u8"목록을 가져오지 못했습니다";
        const std::vector<luil::tree_row> rows { luil::flatten_tree(repository_, expanded_, options) };

        luil::virtual_list_config list {};
        list.owner = u8"workbench-tree";
        list.items = luil::tree_row_items(rows);
        list.row_height = 22.0f;
        list.selected = tree_cursor_;
        list.cursor = tree_cursor_;
        list.scroll_offset = tree_scroll_;
        list.select = [](const std::u8string& key) { return luil::make_app_action(workbench_tree_cursor_intent { key }); };
        list.move = [](const std::u8string& key) { return luil::make_app_action(workbench_tree_cursor_intent { key }); };
        list.activate = [](const std::u8string& key) { return luil::make_app_action(workbench_tree_activate_intent { key }); };
        list.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(workbench_tree_expand_intent { key, expanded }); };
        list.scroll = [](const float delta) { return luil::make_app_action(workbench_tree_scroll_intent { delta }); };
        // 실패 줄만 앱이 짓는다: 사유와 다시 시도 링크다. 나머지 줄은 목록의 기본 행(이름 한 줄)이다.
        list.build_row = [](const luil::virtual_list_item& item, std::size_t, bool) -> std::unique_ptr<luil::ui_element> {
            const std::u8string node { failed_node(item.key) };
            if (node.empty())
                return nullptr;
            luil::stack_config config {};
            config.direction = luil::stack_direction::row;
            config.spacing = 8.0f;
            config.padding = luil::edge_insets::symmetric(8.0f, 0.0f);
            config.cross_alignment = luil::stack_alignment::center;
            auto row { std::make_unique<luil::stack_element>(workbench_id(u8"failed:" + node), config) };
            row->add_fit(std::make_unique<luil::label_element>(workbench_id(u8"failed-text:" + node), luil::label_config { .text = item.label, .color = luil::label_color_role::error }));
            auto retry { std::make_unique<luil::text_button_element>(workbench_id(u8"retry:" + node), luil::text_button_config { .text = u8"다시 시도", .visual = luil::text_button_visual::link }) };
            retry->set_action(luil::ui_trigger::left_click, luil::make_message_action(workbench_tree_retry_intent { node }));
            row->add_fit(std::move(retry));
            return row;
        };

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(8.0f);
        config.spacing = 6.0f;
        auto column { std::make_unique<luil::stack_element>(workbench_id(u8"tree-column"), config) };
        column->add_fit(std::make_unique<luil::label_element>(workbench_id(u8"tree-title"), luil::label_config { .text = u8"저장소", .color = luil::label_color_role::primary }));
        column->add_flexible(std::make_unique<luil::virtual_list_element>(std::move(list)));

        luil::panel_config panel {};
        panel.corner_radius = 6.0f;
        panel.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto card { std::make_unique<luil::panel_element>(workbench_id(u8"tree-panel"), std::move(panel)) };
        card->set_content(std::move(column));
        return card;
    }

    std::unique_ptr<luil::ui_element> workbench_page::make_changes_table()
    {
        luil::table_config table {};
        table.owner = u8"workbench-changes";
        table.columns = columns_;
        for (luil::table_column& column : table.columns)
            column.sort = column.key == sort_column_ ? sort_direction_ : luil::sort_direction::none;

        luil::virtual_list_config& rows { table.rows };
        rows.items = change_items();
        rows.row_height = 24.0f;
        rows.multiple_selection = true;
        rows.selection = selection_.keys;
        rows.cursor = selection_.cursor;
        rows.scroll_offset = changes_scroll_;
        rows.scroll = [](const float delta) { return luil::make_app_action(workbench_change_scroll_intent { delta }); };
        rows.pick = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::make_app_action(workbench_change_pick_intent { key, context }); };
        rows.move_with = [](const std::u8string& key, const luil::virtual_list_move_context& context) { return luil::make_app_action(workbench_change_move_intent { key, context }); };
        rows.toggle_at_cursor = [](const std::u8string& key) { return luil::make_app_action(workbench_change_toggle_intent { key }); };
        rows.select_all = []() { return luil::make_app_action(workbench_change_select_all_intent {}); };
        rows.double_click = [](const std::u8string& key) { return luil::make_app_action(workbench_change_check_intent { key }); };
        rows.context_menu = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::make_app_action(workbench_menu_intent { key, context.x, context.y }); };

        // 칸 글은 행 키로 변경을 찾아 낸다. 표가 짓는 동안만 불리고 모델을 바꾸지 않는다.
        const std::vector<change> changes { changes_ };
        table.cell_text = [changes](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) -> std::u8string {
            const auto found { std::find_if(changes.begin(), changes.end(), [&row](const change& item) { return item.path == row.key; }) };
            if (found == changes.end())
                return {};
            if (column.key == u8"status")
                return found->status;
            if (column.key == u8"size")
                return size_text(found->size);
            return found->path;
        };
        table.check_column = u8"path";
        table.check_state_of = [changes](const luil::virtual_list_item& row, std::size_t) {
            const auto found { std::find_if(changes.begin(), changes.end(), [&row](const change& item) { return item.path == row.key; }) };
            return found != changes.end() && found->staged ? luil::check_state::checked : luil::check_state::unchecked;
        };
        table.toggle_check = [](const std::u8string& key) { return luil::make_app_action(workbench_change_check_intent { key }); };
        table.scroll_x = changes_scroll_x_;
        table.scroll_horizontal = [](const float delta) { return luil::make_app_action(workbench_change_scroll_x_intent { delta }); };
        table.sort_request = [](const std::u8string& column) { return luil::make_app_action(workbench_sort_intent { column }); };
        table.resize_column = [](const std::u8string& column, const float delta) { return luil::make_app_action(workbench_resize_intent { column, delta }); };

        luil::panel_config panel {};
        panel.corner_radius = 6.0f;
        panel.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto card { std::make_unique<luil::panel_element>(workbench_id(u8"changes-panel"), std::move(panel)) };
        card->set_content(std::make_unique<luil::table_element>(std::move(table)));
        return card;
    }

    std::unique_ptr<luil::ui_element> workbench_page::make_form()
    {
        // 라벨 열은 가장 긴 라벨 폭이다 (맞춤 열). 줄 높이는 입력 칸이 서는 28 이상이다.
        luil::grid_config config {};
        config.columns = { luil::grid_track::fit(), luil::grid_track::flexible() };
        config.rows = { luil::grid_track::fit(28.0f), luil::grid_track::fit(28.0f), luil::grid_track::fit(28.0f) };
        config.column_spacing = 12.0f;
        config.row_spacing = 6.0f;
        auto form { std::make_unique<luil::grid_element>(workbench_id(u8"form"), std::move(config)) };

        const auto caption = [](std::u8string owner, std::u8string text) {
            return std::make_unique<luil::label_element>(workbench_id(std::move(owner)), luil::label_config { .text = std::move(text), .color = luil::label_color_role::primary });
        };

        form->add(caption(u8"summary-label", u8"요약"), { .row = 0, .column = 0, .vertical = luil::layout_alignment::center });
        form->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_workbench_summary_input }, luil::make_text_input_view(summary_, composition_, target_workbench_summary),
                      luil::text_input_config { .placeholder = u8"무엇을 바꿨는지 한 줄로" }),
            { .row = 0, .column = 1 });

        // 작성자 칸은 편집형 콤보(Codex X6)가 들어오면 그 자리에 선다. 지금은 읽기만 한다.
        form->add(caption(u8"author-label", u8"작성자"), { .row = 1, .column = 0, .vertical = luil::layout_alignment::center });
        form->add(std::make_unique<luil::label_element>(workbench_id(u8"author"), luil::label_config { .text = u8"luil 예제 <demo@luil.dev>" }),
            { .row = 1, .column = 1, .vertical = luil::layout_alignment::center });

        form->add(caption(u8"last-label", u8"마지막 커밋"), { .row = 2, .column = 0, .vertical = luil::layout_alignment::center });
        form->add(std::make_unique<luil::label_element>(workbench_id(u8"last"), luil::label_config { .text = last_commit_.empty() ? std::u8string { u8"아직 없음" } : last_commit_ }),
            { .row = 2, .column = 1, .vertical = luil::layout_alignment::center });
        return form;
    }

    std::vector<luil::win32::ui_popup> workbench_page::make_popups(const float scale) const
    {
        std::vector<luil::win32::ui_popup> popups {};
        if (menu_open_ == false)
            return popups;
        luil::menu_config menu {};
        menu.owner = u8"workbench-menu";
        menu.items.push_back({ u8"stage", u8"스테이지", luil::codicons::icon_add });
        menu.items.push_back({ u8"unstage", u8"스테이지 취소", luil::codicons::icon_remove });
        luil::menu_item_config discard {};
        discard.key = u8"discard";
        discard.label = u8"변경 버리기";
        discard.icon = luil::codicons::icon_trash;
        discard.separator_above = true;
        menu.items.push_back(std::move(discard));
        menu.select = [](const std::u8string& command) { return luil::make_app_action(workbench_menu_select_intent { command }); };

        luil::win32::ui_popup popup {};
        popup.id = u8"workbench-menu";
        popup.width = workbench_menu_width;
        popup.height = luil::menu_element::height_for(menu);
        popup.x = menu_x_ / scale;
        popup.y = menu_y_ / scale;
        auto root { std::make_unique<luil::menu_element>(std::move(menu)) };
        popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        popup.dismiss = [](luil::win32::popup_dismiss_reason) { return luil::make_app_action(workbench_menu_close_intent {}); };
        popups.push_back(std::move(popup));
        return popups;
    }
} // namespace demo

#include "demo/workbench_page.h"

#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/text/grapheme.h"
#include "luil/text/utf8_text.h"
#include "luil/ui/badge_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/grid_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/spinner_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

namespace demo {
    namespace {
        // 흉내 낸 서버가 폴더 목록을 돌려주기까지의 시간이다.
        constexpr std::chrono::milliseconds load_delay { 700 };
        // 흉내 낸 서버가 작성자 후보를 돌려주기까지의 시간이다. 치는 동안 스피너가 보일 만큼 둔다.
        constexpr std::chrono::milliseconds author_delay { 250 };
        constexpr float workbench_menu_width { 180.0f };
        constexpr float author_popup_height { 168.0f };
        // 보기에 띄울 패치 파일의 상한이다. 넘으면 읽지 않고 이유를 적는다.
        constexpr std::uintmax_t patch_size_limit { 1024u * 1024u };
        // 찾은 곳을 이만큼만 칠한다. 한 글자로 큰 패치를 찾아도 frame이 무거워지지 않는다.
        constexpr std::size_t match_limit { 1000 };

        constexpr std::array<std::u8string_view, 6> author_catalog {
            u8"luil 예제 <demo@luil.dev>",
            u8"김하늘 <sky@luil.dev>",
            u8"박도윤 <doyun@luil.dev>",
            u8"이서준 <seojun@luil.dev>",
            u8"Alex Kim <alex@luil.dev>",
            u8"Jamie Park <jamie@luil.dev>",
        };

        // 행 메뉴의 하위 메뉴가 붙는 항목이다.
        constexpr std::u8string_view sort_submenu { u8"sort" };

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

        // 저장소 계층과 변경 표의 키는 둘 다 경로다. 같은 파일(trunk/CMakeLists.txt)이 두 목록에 서도 행 자리표에 목록
        // owner가 들어 겹치지 않는다 (`virtual_list_row_id`).

        // 묻는 폴더마다 서버가 돌려줄 목록이다.
        [[nodiscard]] std::vector<luil::tree_node> children_for(const std::u8string_view key)
        {
            if (key == u8"trunk/src")
                return {
                    folder(u8"trunk/src/ui", u8"ui", { file(u8"trunk/src/ui/table.cpp", u8"table.cpp"), file(u8"trunk/src/ui/list.cpp", u8"list.cpp") }),
                    file(u8"trunk/src/main.cpp", u8"main.cpp"),
                };
            if (key == u8"branches")
                return { file(u8"branches/release-0.1", u8"release-0.1"), file(u8"branches/release-0.2", u8"release-0.2"), file(u8"branches/feature-table", u8"feature-table") };
            if (key == u8"tags")
                return { file(u8"tags/v0.1", u8"v0.1"), file(u8"tags/v0.2", u8"v0.2") };
            if (key == u8"archive")
                return { file(u8"archive/old-trunk", u8"old-trunk") };
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

        // 흉내 낸 저장소가 변경 하나에 돌려줄 차이다. 수정 차이에는 가로로 흘려야 보이는 긴 줄과 여러 hunk를 둔다.
        [[nodiscard]] std::u8string diff_for(const std::u8string_view path, const std::u8string_view status)
        {
            const std::u8string name { path };
            std::u8string text { u8"diff --git a/" + name + u8" b/" + name + u8"\n" };
            if (status == u8"추가됨")
            {
                text += u8"--- /dev/null\n+++ b/" + name + u8"\n@@ -0,0 +1,7 @@\n";
                text += u8"+// " + name + u8"\n+#include \"luil/ui/table_element.h\"\n+\n+namespace demo {\n";
                text += u8"+    // 새 파일이다. 모든 줄이 더한 줄로 칠해진다.\n+    void run_case();\n+} // namespace demo\n";
                return text;
            }
            if (status == u8"지워짐")
            {
                text += u8"--- a/" + name + u8"\n+++ /dev/null\n@@ -1,5 +0,0 @@\n";
                text += u8"-#include \"demo/app.h\"\n-\n-int main()\n-{\n-    return demo::run();\n-}\n";
                return text;
            }
            text += u8"--- a/" + name + u8"\n+++ b/" + name + u8"\n";
            text += u8"@@ -12,9 +12,10 @@ namespace luil {\n";
            text += u8"     struct column_layout\n     {\n";
            text += u8"-        float width { 80.0f };\n";
            text += u8"+        float width { 120.0f };\n";
            text += u8"+        float min_width { 48.0f };\n";
            text += u8"         bool visible { true };\n     };\n \n";
            text += u8"@@ -40,7 +41,7 @@ namespace luil {\n";
            text += u8"     void layout_columns(std::span<column_layout> columns, const float available)\n     {\n";
            text += u8"-        const float total { sum_widths(columns) };\n";
            text += u8"+        const float total { std::max(sum_widths(columns), 1.0f) }; // 열이 모두 0 폭이어도 나눗셈이 무한대가 되지 않는다 — 이 줄은 일부러 길어서 가로로 흘려야 끝이 보인다.\n";
            text += u8"         for (column_layout& column : columns)\n";
            text += u8"             column.width *= available / total;\n     }\n";
            text += u8"@@ -88,6 +89,6 @@ namespace luil {\n";
            text += u8"     // 한글·이모지 👩‍💻 · 결합 문자 é 도 글자소 단위로 고르고 지운다.\n";
            text += u8"-    constexpr int header_height { 22 };\n";
            text += u8"+    constexpr int header_height { 24 };\n";
            text += u8"     constexpr int row_height { 24 };\n";
            return text;
        }

        struct diff_decorations
        {
            std::vector<luil::text_span> spans {};
            std::vector<luil::line_decoration> lines {};
        };

        // 통합 diff의 줄 머리로 구간 색과 줄 장식을 정한다. 불러온 패치에도 같은 규칙을 쓴다.
        //  - 구간은 줄 하나를 덮는다. 줄 경계는 늘 글자소 경계라 문서 칸의 계약을 지킨다 (text_document.h).
        [[nodiscard]] diff_decorations decorate_diff(const std::u8string_view text)
        {
            diff_decorations result {};
            std::size_t begin { 0 };
            for (std::size_t line { 0 }; begin <= text.size(); ++line)
            {
                const std::size_t newline { text.find(u8'\n', begin) };
                std::size_t end { newline == std::u8string_view::npos ? text.size() : newline };
                if (end > begin && text[end - 1] == u8'\r')
                    --end;
                const std::u8string_view row { text.substr(begin, end - begin) };
                if (row.starts_with(u8"@@"))
                    result.spans.push_back({ .begin = begin, .end = end, .color = luil::text_color_role::accent, .bold = true });
                else if (row.starts_with(u8"diff ") || row.starts_with(u8"--- ") || row.starts_with(u8"+++ ") || row.starts_with(u8"index "))
                    result.spans.push_back({ .begin = begin, .end = end, .color = luil::text_color_role::secondary, .bold = true });
                else if (row.starts_with(u8'+'))
                    result.lines.push_back({ .line = line, .background = luil::line_background_role::added, .marker = U'+' });
                else if (row.starts_with(u8'-'))
                    result.lines.push_back({ .line = line, .background = luil::line_background_role::removed, .marker = U'-' });
                if (newline == std::u8string_view::npos)
                    break;
                begin = newline + 1;
            }
            // 빈 줄의 구간은 두지 않는다 (비어 있지 않은 구간만 받는다).
            std::erase_if(result.spans, [](const luil::text_span& span) { return span.begin == span.end; });
            return result;
        }

        // 패치 파일을 읽는다. 깨진 UTF-8은 대체 글자로 바꿔 그린다 — 밖에서 온 글은 언제든 깨질 수 있다 (utf8_text.h).
        [[nodiscard]] bool read_patch(const std::u8string& path, std::u8string& text, std::u8string& error)
        {
            const std::filesystem::path file { path };
            std::error_code code {};
            const std::uintmax_t size { std::filesystem::file_size(file, code) };
            if (code)
            {
                error = u8"파일 크기를 읽지 못했다";
                return false;
            }
            if (size > patch_size_limit)
            {
                error = u8"1MiB가 넘는 패치는 띄우지 않는다";
                return false;
            }
            std::ifstream stream { file, std::ios::binary };
            if (stream.is_open() == false)
            {
                error = u8"파일을 열지 못했다";
                return false;
            }
            const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
            const std::u8string_view raw { reinterpret_cast<const char8_t*>(bytes.data()), bytes.size() };
            text = luil::text::utf8_is_valid(raw) ? std::u8string { raw } : luil::text::utf8_replace_invalid(raw);
            return true;
        }

        [[nodiscard]] std::size_t line_of(const std::u8string_view text, const std::size_t offset)
        {
            return static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(std::min(offset, text.size())), u8'\n'));
        }

        [[nodiscard]] std::u8string file_name(const std::u8string_view path)
        {
            const std::size_t slash { path.find_last_of(u8"/\\") };
            return std::u8string { slash == std::u8string_view::npos ? path : path.substr(slash + 1) };
        }
    } // namespace

    workbench_page::workbench_page()
    {
        repository_ = {
            folder(u8"trunk", u8"trunk",
                {
                    folder(u8"trunk/src", u8"src", {}, luil::node_load::unloaded),
                    folder(u8"trunk/docs", u8"docs", { file(u8"trunk/docs/README.md", u8"README.md"), file(u8"trunk/docs/table.md", u8"table.md") }),
                    file(u8"trunk/CMakeLists.txt", u8"CMakeLists.txt"),
                }),
            folder(u8"branches", u8"branches", {}, luil::node_load::unloaded),
            folder(u8"tags", u8"tags", {}, luil::node_load::unloaded),
            folder(u8"archive", u8"archive", {}, luil::node_load::unloaded),
        };
        expanded_ = { u8"trunk" };
        tree_cursor_ = u8"trunk";

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
        set_author(std::u8string { author_catalog.front() });
        refresh_diff();
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
        if (author_pending_.has_value() && (next.has_value() == false || author_pending_->due < *next))
            next = author_pending_->due;
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
            if (load.key == u8"archive" && archive_attempts_++ == 0)
            {
                node->load = luil::node_load::failed;
                continue;
            }
            node->children = children_for(load.key);
            node->load = luil::node_load::loaded;
        }
        pending_ = std::move(waiting);

        if (author_pending_.has_value() && author_pending_->due <= now)
        {
            // 서버는 부분 일치로 거른다. 받는 동안 콤보가 앞 후보를 보이는 글로 거르므로 둘이 같은 규칙이어야 한다 (combo-box-design.md).
            //  - 칸의 글이 확정한 작성자 그대로면 펼치기다. 그 하나만 돌려주면 고를 것이 없으므로 전부를 돌려준다.
            const bool browse { author_pending_->query == author_committed_ };
            auto items { std::make_shared<std::vector<luil::combo_suggestion>>() };
            for (const std::u8string_view author : author_catalog)
                if (browse || author.find(author_pending_->query) != std::u8string_view::npos)
                    items->push_back({ std::u8string { author }, std::u8string { author } });
            // 더 새 물음이 이미 나갔으면 이 답은 버려진다 (token이 다르다).
            if (luil::accept_combo_suggestions(author_suggestions_, author_pending_->token, std::move(items))
                && std::none_of(author_suggestions_.items->begin(), author_suggestions_.items->end(), [this](const luil::combo_suggestion& item) { return item.key == author_highlight_; }))
            {
                author_highlight_.clear();
                author_scroll_ = 0.0f;
            }
            author_pending_.reset();
        }
    }

    luil::file_dialog_request workbench_page::patch_dialog_request(const std::uint64_t id)
    {
        luil::file_dialog_request request {};
        request.id = id;
        request.title = u8"패치 불러오기";
        request.filters.push_back({ u8"패치 파일", { u8"patch", u8"diff" } });
        request.filters.push_back({ u8"모든 파일", { u8"*" } });
        return request;
    }

    void workbench_page::refresh_diff()
    {
        std::u8string source {};
        std::u8string text {};
        if (patch_path_.empty() == false)
        {
            source = u8"patch:" + patch_path_;
            text = patch_text_;
        }
        else if (const auto found { find_change(selection_.cursor) }; found != changes_.end())
        {
            source = found->path;
            text = diff_for(found->path, found->status);
        }
        if (source == diff_source_)
            return;
        diff_source_ = std::move(source);
        diff_ = {};
        diff_.text = std::move(text);
        diff_decorations decorations { decorate_diff(diff_.text) };
        diff_spans_ = std::make_shared<const std::vector<luil::text_span>>(std::move(decorations.spans));
        diff_lines_ = std::make_shared<const std::vector<luil::line_decoration>>(std::move(decorations.lines));
        // 다른 문서다. 앞 문서의 접은 자리와 스크롤을 들고 가지 않는다.
        diff_wrap_ = std::make_shared<luil::text_area_wrap_cache>();
        diff_scroll_ = 0.0f;
        diff_scroll_x_ = 0.0f;
        // 찾는 글은 그대로 두고 새 문서에서 다시 찾는다.
        matches_stale_ = true;
        static_cast<void>(refresh_matches());
    }

    bool workbench_page::refresh_matches()
    {
        // 조합 중인 글자도 보이는 대로 찾는다 — 화면의 칸과 칠한 곳이 어긋나지 않는다.
        const std::u8string query { luil::make_text_input_view(find_, find_composition_, target_workbench_find).displayed_text() };
        if (query == matched_query_ && matches_stale_ == false)
            return false;
        matched_query_ = query;
        matches_stale_ = false;
        matches_.clear();
        current_match_ = 0;
        if (query.empty())
            return true;
        // 찾은 곳의 양 끝이 글자소 경계인 것만 칠한다. "e"로 "é"의 반쪽을 칠하지 않는다.
        const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(diff_.text) };
        const auto on_boundary = [&boundaries](const std::size_t offset) { return std::binary_search(boundaries.begin(), boundaries.end(), offset); };
        for (std::size_t at { diff_.text.find(query) }; at != std::u8string::npos && matches_.size() < match_limit; at = diff_.text.find(query, at + 1))
            if (on_boundary(at) && on_boundary(at + query.size()))
                matches_.push_back({ .begin = at, .end = at + query.size() });
        return true;
    }

    void workbench_page::reveal_match()
    {
        if (current_match_ >= matches_.size())
            return;
        const luil::text_highlight& match { matches_[current_match_] };
        diff_.anchor = match.begin;
        diff_.caret = match.end;
        diff_.caret_upstream = false;
        // 접지 않는 보기라 논리 줄이 화면 줄이다. 찾은 줄 위로 세 줄을 남기고 세운다 (넘친 값은 배치가 다듬는다).
        const float line { static_cast<float>(line_of(diff_.text, match.begin)) };
        diff_scroll_ = std::max(0.0f, (line - 3.0f) * luil::text_area_line_height);
    }

    void workbench_page::apply_author_edit(const luil::text_edit_request& request)
    {
        const std::u8string before { author_.text };
        luil::apply_text_edit(author_, request);
        if (author_.text == before)
            return;
        author_error_.clear();
        author_open_ = true;
        query_authors(std::chrono::steady_clock::now());
    }

    void workbench_page::query_authors(const std::chrono::steady_clock::time_point now)
    {
        std::u8string query { luil::make_text_input_view(author_, author_composition_, target_workbench_author).displayed_text() };
        const std::uint64_t token { luil::begin_combo_query(author_suggestions_, query) };
        author_pending_ = pending_authors { token, std::move(query), now + author_delay };
    }

    void workbench_page::set_author(std::u8string text)
    {
        author_ = {};
        author_.text = std::move(text);
        author_.caret = author_.text.size();
        author_.anchor = author_.caret;
        author_committed_ = author_.text;
        author_composition_.reset();
        author_error_.clear();
        author_open_ = false;
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
        submenu_.clear();
        author_open_ = false;
        author_combo_ = nullptr;
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
            submenu_.clear();
            menu_x_ = menu->x;
            menu_y_ = menu->y;
            return true;
        }
        if (message.get<workbench_menu_close_intent>() != nullptr)
        {
            menu_open_ = false;
            submenu_.clear();
            return true;
        }
        if (const auto* const submenu { message.get<workbench_submenu_intent>() }; submenu != nullptr)
        {
            submenu_ = submenu->key;
            return true;
        }
        if (const auto* const select { message.get<workbench_menu_select_intent>() }; select != nullptr)
        {
            menu_open_ = false;
            submenu_.clear();
            if (select->command.starts_with(u8"sort:"))
            {
                const std::vector<luil::virtual_list_item> previous { change_items() };
                sort_column_ = select->command.substr(5);
                reconcile(previous);
            }
            else if (select->command == u8"sort-descending")
            {
                const std::vector<luil::virtual_list_item> previous { change_items() };
                sort_direction_ = sort_direction_ == luil::sort_direction::descending ? luil::sort_direction::ascending : luil::sort_direction::descending;
                reconcile(previous);
            }
            else if (select->command == u8"stage" || select->command == u8"unstage")
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
        if (const auto* const patch { message.get<workbench_patch_intent>() }; patch != nullptr)
        {
            std::u8string text {};
            std::u8string error {};
            if (read_patch(patch->path, text, error) == false)
            {
                patch_error_ = file_name(patch->path) + u8": " + error;
                return true;
            }
            patch_error_.clear();
            patch_path_ = patch->path;
            patch_text_ = std::move(text);
            refresh_diff();
            return true;
        }
        if (message.get<workbench_patch_close_intent>() != nullptr)
        {
            patch_path_.clear();
            patch_text_.clear();
            patch_error_.clear();
            refresh_diff();
            return true;
        }
        if (const auto* const scroll { message.get<workbench_diff_scroll_intent>() }; scroll != nullptr)
        {
            float& offset { scroll->horizontal ? diff_scroll_x_ : diff_scroll_ };
            offset = std::max(0.0f, offset + scroll->delta);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_diff_scroll_to_intent>() }; scroll != nullptr)
        {
            (scroll->horizontal ? diff_scroll_x_ : diff_scroll_) = std::max(0.0f, scroll->offset);
            return true;
        }
        if (const auto* const step { message.get<workbench_find_step_intent>() }; step != nullptr)
        {
            if (matches_.empty())
                return true;
            const std::size_t count { matches_.size() };
            current_match_ = step->direction < 0 ? (current_match_ + count - 1) % count : (current_match_ + 1) % count;
            reveal_match();
            return true;
        }

        if (const auto* const open { message.get<workbench_author_open_intent>() }; open != nullptr)
        {
            author_open_ = open->open;
            if (author_open_)
                query_authors(now);
            return true;
        }
        if (const auto* const move { message.get<workbench_author_move_intent>() }; move != nullptr)
        {
            author_highlight_ = move->key;
            return true;
        }
        if (const auto* const select { message.get<workbench_author_select_intent>() }; select != nullptr)
        {
            set_author(select->key);
            return true;
        }
        if (const auto* const commit { message.get<workbench_author_commit_intent>() }; commit != nullptr)
        {
            // 후보 밖의 글도 받는다. 형식만 앱이 본다 — 틀리면 칸 아래에 이유를 적고 칸을 그대로 둔다.
            if (commit->text.find(u8'<') == std::u8string::npos || commit->text.find(u8'@') == std::u8string::npos || commit->text.ends_with(u8'>') == false)
            {
                author_error_ = u8"\"이름 <메일>\" 꼴로 적는다";
                author_open_ = false;
                return true;
            }
            set_author(commit->text);
            return true;
        }
        if (message.get<workbench_author_restore_intent>() != nullptr)
        {
            set_author(author_committed_);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_author_scroll_intent>() }; scroll != nullptr)
        {
            author_scroll_ = std::max(0.0f, author_scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll { message.get<workbench_author_scroll_to_intent>() }; scroll != nullptr)
        {
            author_scroll_ = std::max(0.0f, scroll->offset);
            return true;
        }

        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            const luil::text_input_target target { edit->request.target };
            if (target == target_workbench_summary)
                luil::apply_text_edit(summary_, edit->request);
            else if (target == target_workbench_author)
                apply_author_edit(edit->request);
            else if (target == target_workbench_diff)
            {
                // 읽기 전용 칸이라 caret·선택만 온다. 그래도 글이 바뀌는 요청이면 글은 되돌리고 caret만 받는다 —
                // 보기의 글은 장식(줄·구간)과 짝이라 앱이 아닌 곳에서 바뀌면 둘이 어긋난다.
                const std::u8string text { diff_.text };
                luil::apply_text_edit(diff_, edit->request);
                if (diff_.text != text)
                {
                    diff_.text = text;
                    diff_.caret = std::min(diff_.caret, diff_.text.size());
                    diff_.anchor = diff_.caret;
                }
            }
            else if (target == target_workbench_find)
            {
                luil::apply_text_edit(find_, edit->request);
                if (refresh_matches())
                    reveal_match();
            }
            else
                return false;
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            const luil::text_input_target target { composition->event.target };
            std::optional<luil::text_composition_event>* slot { nullptr };
            if (target == target_workbench_summary)
                slot = &composition_;
            else if (target == target_workbench_author)
                slot = &author_composition_;
            else if (target == target_workbench_find)
                slot = &find_composition_;
            if (slot == nullptr)
                return false;
            if (composition->event.composing)
                *slot = composition->event;
            else
                slot->reset();
            // 조합 중인 글자도 보이는 대로 다시 묻고 다시 찾는다.
            if (target == target_workbench_author)
            {
                author_open_ = true;
                query_authors(now);
            }
            if (target == target_workbench_find && refresh_matches())
                reveal_match();
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> workbench_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        // 표의 커서가 옮겨 갔거나 행이 지워졌으면 보기도 따라간다.
        refresh_diff();
        author_combo_ = nullptr;
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
        // 저장소 판은 창을 따라 넓어지되 300을 넘지 않는다. 남는 폭은 표와 변경 내용 보기가 갖는다 (stack 상한의 재분배).
        body->add(make_tree_panel(), luil::stack_item { .weight = 1.0f, .minimum = 200.0f, .maximum = 300.0f });
        luil::stack_config detail_config {};
        detail_config.spacing = 12.0f;
        auto detail { std::make_unique<luil::stack_element>(workbench_id(u8"detail"), detail_config) };
        detail->add(make_changes_table(), luil::stack_item { .weight = 1.0f, .minimum = 120.0f });
        detail->add(make_diff_panel(), luil::stack_item { .weight = 1.0f, .minimum = 160.0f });
        body->add_flexible(std::move(detail), 3.0f);
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

        // 패치 파일은 공용 파일 대화상자로 고른다. 요청과 결과는 셸이 frame으로 나른다 (file-dialog-design.md).
        luil::text_button_config import_config {};
        import_config.text = u8"패치 불러오기…";
        import_config.glyph = luil::codicons::icon_folder_opened;
        auto import { std::make_unique<luil::text_button_element>(workbench_id(u8"import"), std::move(import_config)) };
        import->set_action(luil::ui_trigger::left_click, luil::make_message_action(workbench_import_intent {}));
        import->set_tooltip(u8".patch·.diff 파일을 골라 변경 내용 보기에 띄운다");
        row->add_fit(std::move(import));

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
        // 불러오는 중 줄과 실패 줄만 앱이 짓는다: 스피너와 안내, 사유와 다시 시도 링크다. 나머지 줄은 목록의 기본 행(이름 한 줄)이다.
        list.build_row = [](const luil::virtual_list_item& item, std::size_t, bool) -> std::unique_ptr<luil::ui_element> {
            if (item.key.ends_with(luil::tree_loading_suffix))
            {
                luil::stack_config config {};
                config.direction = luil::stack_direction::row;
                config.spacing = 6.0f;
                config.padding = luil::edge_insets::symmetric(8.0f, 0.0f);
                config.cross_alignment = luil::stack_alignment::center;
                auto row { std::make_unique<luil::stack_element>(workbench_id(u8"loading-row:" + item.key), config) };
                row->add(std::make_unique<luil::spinner_element>(workbench_id(u8"loading-spinner:" + item.key), luil::spinner_config { .name = item.label, .size = 12.0f }),
                    luil::stack_item { .length = 14.0f, .cross_length = 14.0f });
                row->add_fit(std::make_unique<luil::label_element>(workbench_id(u8"loading-text:" + item.key), luil::label_config { .text = item.label }));
                return row;
            }
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

    std::unique_ptr<luil::ui_element> workbench_page::make_diff_panel()
    {
        luil::stack_config head_config {};
        head_config.direction = luil::stack_direction::row;
        head_config.spacing = 6.0f;
        head_config.cross_alignment = luil::stack_alignment::center;
        auto head { std::make_unique<luil::stack_element>(workbench_id(u8"diff-head"), head_config) };
        // 긴 경로는 가운데를 줄인다 — 앞의 폴더와 뒤의 파일 이름이 둘 다 남는다.
        std::u8string title { patch_path_.empty() ? diff_source_ : u8"불러온 패치: " + patch_path_ };
        if (title.empty())
            title = u8"표에서 변경을 고르면 차이를 보인다";
        head->add_flexible(std::make_unique<luil::label_element>(
            workbench_id(u8"diff-title"), luil::label_config { .text = std::move(title), .color = luil::label_color_role::primary, .elide = luil::text_elide::middle }));
        if (patch_path_.empty() == false)
        {
            auto close { std::make_unique<luil::text_button_element>(workbench_id(u8"patch-close"), luil::text_button_config { .text = u8"패치 닫기", .visual = luil::text_button_visual::link }) };
            close->set_action(luil::ui_trigger::left_click, luil::make_message_action(workbench_patch_close_intent {}));
            head->add_fit(std::move(close));
        }

        // 찾는 칸이다. 치는 대로 칠하고 Enter·단추로 찾은 곳 사이를 옮긴다.
        luil::text_input_config find_config {};
        find_config.placeholder = u8"차이에서 찾기";
        find_config.leading_glyph = luil::codicons::icon_search;
        find_config.submit = luil::make_message_action(workbench_find_step_intent { 1 });
        head->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_workbench_find_input }, luil::make_text_input_view(find_, find_composition_, target_workbench_find),
                      std::move(find_config)),
            luil::stack_item { .length = 180.0f, .cross_length = 28.0f });
        std::u8string count {};
        if (matched_query_.empty() == false)
            count = matches_.empty() ? std::u8string { u8"없음" } : to_u8(static_cast<int>(current_match_ + 1)) + u8"/" + to_u8(static_cast<int>(matches_.size()));
        head->add(std::make_unique<luil::label_element>(workbench_id(u8"find-count"), luil::label_config { .text = count, .align = luil::text_align::end }), 48.0f);
        const auto step_button = [this](std::u8string owner, const char32_t glyph, const std::u8string& name, const int direction) {
            auto button { std::make_unique<luil::text_button_element>(workbench_id(std::move(owner)), luil::text_button_config { .glyph = glyph }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(workbench_find_step_intent { direction }));
            button->set_tooltip(name);
            button->set_access_name(name);
            button->set_enabled(matches_.empty() == false);
            return button;
        };
        // 아이콘만 있는 단추는 정사각형으로 둔다 (글자 단추의 최소 폭을 쓰지 않는다).
        head->add(step_button(u8"find-previous", luil::codicons::icon_arrow_up, u8"이전 찾은 곳", -1), luil::stack_item { .length = 28.0f, .cross_length = 28.0f });
        head->add(step_button(u8"find-next", luil::codicons::icon_arrow_down, u8"다음 찾은 곳", 1), luil::stack_item { .length = 28.0f, .cross_length = 28.0f });

        // 읽기 전용 문서 칸이다. 줄 번호와 +/- 표시, 줄 바탕, 머리 줄의 굵은 색 구간, 찾은 곳을 같은 배치 값 위에 칠한다.
        //  - 코드 글꼴로 접지 않고 가로로 흘린다. 선택과 복사는 읽기 전용에서도 된다.
        luil::text_area_config view {};
        view.placeholder = u8"변경 내용";
        view.read_only = true;
        view.wrap = false;
        view.face = luil::text_face::code;
        view.scroll_offset = diff_scroll_;
        view.scroll = [](const float delta) { return luil::make_app_action(workbench_diff_scroll_intent { false, delta }); };
        view.scroll_to = [](const float offset) { return luil::make_app_action(workbench_diff_scroll_to_intent { false, offset }); };
        view.scroll_x = diff_scroll_x_;
        view.scroll_horizontal = [](const float delta) { return luil::make_app_action(workbench_diff_scroll_intent { true, delta }); };
        view.scroll_horizontal_to = [](const float offset) { return luil::make_app_action(workbench_diff_scroll_to_intent { true, offset }); };
        view.wrap_cache = diff_wrap_;
        view.spans = diff_spans_;
        view.lines = diff_lines_;
        view.gutters.push_back({ .kind = luil::gutter_kind::line_number });
        view.highlights = matches_;
        if (current_match_ < view.highlights.size())
            view.highlights[current_match_].current = true;

        luil::stack_config column_config {};
        column_config.padding = luil::edge_insets::all(8.0f);
        column_config.spacing = 6.0f;
        auto column { std::make_unique<luil::stack_element>(workbench_id(u8"diff-column"), column_config) };
        column->add(std::move(head), 28.0f);
        if (patch_error_.empty() == false)
            column->add(std::make_unique<luil::label_element>(workbench_id(u8"patch-error"), luil::label_config { .text = patch_error_, .color = luil::label_color_role::error }), 20.0f);
        column->add_flexible(std::make_unique<luil::text_area_element>(
            luil::ui_element_id { kind_workbench_diff }, luil::make_text_input_view(diff_, std::nullopt, target_workbench_diff), std::move(view)));

        luil::panel_config panel {};
        panel.corner_radius = 6.0f;
        panel.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto card { std::make_unique<luil::panel_element>(workbench_id(u8"diff-panel"), std::move(panel)) };
        card->set_content(std::move(column));
        return card;
    }

    std::unique_ptr<luil::ui_element> workbench_page::make_form()
    {
        // 라벨 열은 가장 긴 라벨 폭이다 (맞춤 열). 줄 높이는 입력 칸이 서는 28 이상이다.
        luil::grid_config config {};
        config.columns = { luil::grid_track::fit(), luil::grid_track::flexible() };
        // 작성자 줄은 콤보가 검증 글을 칸 아래에 둘 자리만큼 높아진다.
        const float author_height { author_error_.empty() ? 28.0f : 48.0f };
        config.rows = { luil::grid_track::fit(28.0f), luil::grid_track::fixed(author_height), luil::grid_track::fit(28.0f) };
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

        // 작성자 칸은 편집형 콤보다. 치면 흉내 낸 서버에 후보를 묻고, 답이 오기 전에는 popup에 스피너가 돈다.
        //  - 후보 밖의 글도 확정할 수 있다. 형식이 틀리면 앱이 검증 글을 칸 아래에 둔다.
        //  - 닫힌 칸의 Esc는 마지막으로 확정한 작성자로 되돌린다.
        form->add(caption(u8"author-label", u8"작성자"), { .row = 1, .column = 0, .vertical = author_error_.empty() ? luil::layout_alignment::center : luil::layout_alignment::start });
        luil::combo_box_config combo {};
        combo.input.placeholder = u8"이름 <메일>";
        combo.suggestions = author_suggestions_;
        combo.highlighted = author_highlight_;
        combo.validation_message = author_error_;
        combo.open = author_open_;
        combo.popup_height = author_popup_height;
        combo.set_open = [](const bool open) { return luil::make_app_action(workbench_author_open_intent { open }); };
        combo.move = [](const std::u8string& key) { return luil::make_app_action(workbench_author_move_intent { key }); };
        combo.select = [](const std::u8string& key) { return luil::make_app_action(workbench_author_select_intent { key }); };
        combo.commit = [](const std::u8string& text) { return luil::make_app_action(workbench_author_commit_intent { text }); };
        combo.restore = luil::make_message_action(workbench_author_restore_intent {});
        combo.scroll_offset = author_scroll_;
        combo.scroll = [](const float delta) { return luil::make_app_action(workbench_author_scroll_intent { delta }); };
        combo.scroll_to = [](const float offset) { return luil::make_app_action(workbench_author_scroll_to_intent { offset }); };
        const luil::text_input_view author_view { luil::make_text_input_view(author_, author_composition_, target_workbench_author) };
        auto author { std::make_unique<luil::combo_box_element>(luil::ui_element_id { kind_workbench_author_input, u8"workbench-author" }, author_view, std::move(combo)) };
        author->set_access_name(u8"작성자");
        author_combo_ = author.get();
        form->add(std::move(author), { .row = 1, .column = 1 });

        form->add(caption(u8"last-label", u8"마지막 커밋"), { .row = 2, .column = 0, .vertical = luil::layout_alignment::center });
        form->add(std::make_unique<luil::label_element>(workbench_id(u8"last"), luil::label_config { .text = last_commit_.empty() ? std::u8string { u8"아직 없음" } : last_commit_ }),
            { .row = 2, .column = 1, .vertical = luil::layout_alignment::center });
        return form;
    }

    std::vector<luil::win32::ui_popup> workbench_page::make_popups(const float scale) const
    {
        std::vector<luil::win32::ui_popup> popups {};
        // 작성자 후보 popup은 이번 frame에 배치한 콤보의 자리를 따라 선다.
        if (author_open_ && author_combo_ != nullptr)
            popups.push_back(author_combo_->suggestion_popup());
        if (menu_open_ == false)
            return popups;

        // 정렬 하위 메뉴다. 라디오 항목이 지금 정렬 열을, 체크 항목이 방향을 보인다.
        std::vector<luil::menu_item_config> sort_items {
            { .key = u8"sort:path", .label = u8"경로", .kind = luil::menu_item_kind::radio, .checked = sort_column_ == u8"path" },
            { .key = u8"sort:status", .label = u8"상태", .kind = luil::menu_item_kind::radio, .checked = sort_column_ == u8"status" },
            { .key = u8"sort:size", .label = u8"크기", .kind = luil::menu_item_kind::radio, .checked = sort_column_ == u8"size" },
        };
        const bool descending { sort_direction_ == luil::sort_direction::descending };
        sort_items.push_back({ .key = u8"sort-descending", .label = u8"내림차순", .separator_above = true, .kind = luil::menu_item_kind::check, .checked = descending });

        luil::menu_config menu {};
        menu.owner = u8"workbench-menu";
        menu.items.push_back({ u8"stage", u8"스테이지", luil::codicons::icon_add });
        menu.items.push_back({ u8"unstage", u8"스테이지 취소", luil::codicons::icon_remove });
        menu.items.push_back({ .key = std::u8string { sort_submenu }, .label = u8"정렬", .separator_above = true, .children = sort_items });
        menu.items.push_back({ .key = u8"discard", .label = u8"변경 버리기", .icon = luil::codicons::icon_trash, .separator_above = true });
        menu.select = [](const std::u8string& command) { return luil::make_app_action(workbench_menu_select_intent { command }); };
        // 하위 메뉴의 열림 경로는 앱 상태다. 오른쪽·Enter·hover가 열고 왼쪽·Esc가 한 단계만 닫는다 (popup-anchor-design.md).
        menu.open_submenu = [](const std::u8string& key) { return luil::make_app_action(workbench_submenu_intent { key }); };
        menu.close_submenu = luil::make_message_action(workbench_submenu_intent {});
        menu.opened_key = submenu_;

        // 하위 메뉴는 부모 popup 기준 자리에 선다. 붙는 항목의 위 끝까지의 높이를 앞 항목들로 센다.
        float submenu_y { luil::menu_padding };
        for (const luil::menu_item_config& item : menu.items)
        {
            if (item.separator_above)
                submenu_y += luil::menu_separator_height;
            if (item.key == sort_submenu)
                break;
            submenu_y += luil::menu_item_height;
        }

        const auto dismiss = [](luil::win32::popup_dismiss_reason) { return luil::make_app_action(workbench_menu_close_intent {}); };
        popups.push_back(luil::make_menu_popup(
            std::move(menu), { .id = u8"workbench-menu", .x = menu_x_ / scale, .y = menu_y_ / scale, .width = workbench_menu_width, .scale = scale, .dismiss = dismiss }));
        if (submenu_ == sort_submenu)
        {
            luil::menu_config child {};
            child.owner = u8"workbench-sort-menu";
            child.items = std::move(sort_items);
            child.select = [](const std::u8string& command) { return luil::make_app_action(workbench_menu_select_intent { command }); };
            child.close_submenu = luil::make_message_action(workbench_submenu_intent {});
            child.depth = 1;
            child.parent_surface = u8"workbench-menu";
            child.anchor_key = std::u8string { sort_submenu };
            popups.push_back(luil::make_menu_popup(std::move(child),
                { .id = u8"workbench-sort-menu", .anchor = u8"workbench-menu", .x = workbench_menu_width, .y = submenu_y - luil::menu_padding, .scale = scale, .dismiss = dismiss }));
        }
        return popups;
    }
} // namespace demo

#include "luil/ui/ui_inspect.h"

#include "ui/inspect_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

namespace luil {
    namespace {
        [[nodiscard]] std::optional<rect_f> intersect_rects(const rect_f& first, const rect_f& second) noexcept
        {
            const float left { std::max(first.x, second.x) };
            const float top { std::max(first.y, second.y) };
            const float right { std::min(first.x + first.width, second.x + second.width) };
            const float bottom { std::min(first.y + first.height, second.y + second.height) };
            if (right <= left || bottom <= top)
                return std::nullopt;
            return rect_f { left, top, right - left, bottom - top };
        }

        [[nodiscard]] bool same_rect(const rect_f& first, const rect_f& second) noexcept
        {
            return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
        }

        // 조상이 자른 영역을 들고 그리기 순서로 걷는다. 규칙은 `ui_tree::visible_bounds`와 같다.
        //  - `clip`이 비어 있으면 아직 아무도 자르지 않았다는 뜻이고, 자른 결과가 빈 가지는 `clipped_out`이 따로 든다.
        void collect_nodes(
            const ui_element& element
            , const std::size_t depth
            , const std::size_t parent
            , const bool parent_visible
            , const std::optional<rect_f>& clip
            , const bool clipped_out
            , std::vector<inspect_node>& nodes
        )
        {
            const bool visible_path { parent_visible && element.visible() };
            inspect_node node {};
            node.element = &element;
            node.depth = depth;
            node.parent = parent;
            node.bounds = element.bounds();
            node.visible_path = visible_path;
            if (visible_path && clipped_out == false)
                node.visible_bounds = clip.has_value() ? intersect_rects(element.bounds(), *clip) : std::optional<rect_f> { element.bounds() };
            const std::size_t index { nodes.size() };
            nodes.push_back(node);

            std::optional<rect_f> next_clip { clip };
            bool next_clipped_out { clipped_out };
            if (element.clip_children())
            {
                next_clip = clip.has_value() ? intersect_rects(*clip, element.bounds()) : std::optional<rect_f> { element.bounds() };
                if (next_clip.has_value() == false)
                    next_clipped_out = true;
            }
            for (const std::unique_ptr<ui_element>& child : element.children())
                collect_nodes(*child, depth + 1, index, visible_path, next_clip, next_clipped_out, nodes);
        }

        // `ui_element::hit_test`와 같은 걸음에서 상호작용 조건만 뗀 것이다.
        [[nodiscard]] const ui_element* deepest_at(const ui_element& element, const float x, const float y)
        {
            if (element.visible() == false)
                return nullptr;
            if (element.clip_children() && element.bounds().contains(x, y) == false)
                return nullptr;
            // 나중에 담은 자식이 위에 그려지므로 역순이다. 스크롤로 자식이 부모 밖에 걸칠 수 있어 자식 탐색은 자기 bounds로 막지 않는다.
            const std::span<const std::unique_ptr<ui_element>> children { element.children() };
            for (std::size_t index { children.size() }; index > 0; --index)
            {
                if (const ui_element* const hit { deepest_at(*children[index - 1], x, y) }; hit != nullptr)
                    return hit;
            }
            return element.bounds().contains(x, y) ? &element : nullptr;
        }

        // 뿌리부터 `target`까지의 보이는 경로다 (뿌리가 앞). 숨은 가지에 있거나 tree에 없으면 비어 있다.
        //  - id가 아니라 포인터로 찾는다. 같은 id가 둘이어도 집은 그 요소의 조상을 따른다.
        bool collect_path(const ui_element& element, const ui_element* const target, std::vector<const ui_element*>& path)
        {
            if (element.visible() == false)
                return false;
            path.push_back(&element);
            if (&element == target)
                return true;
            for (const std::unique_ptr<ui_element>& child : element.children())
            {
                if (collect_path(*child, target, path))
                    return true;
            }
            path.pop_back();
            return false;
        }

        [[nodiscard]] const ui_element* find_by_id(const ui_tree& tree, const ui_element_id& id) noexcept
        {
            return id == ui_element_id {} ? nullptr : tree.find(id);
        }

        // --- 글 만들기 ---
        // 숫자를 글로 옮기는 도구다. 이 글은 개발자용 ASCII라 로케일을 타지 않는다.
        void append_unsigned(std::u8string& text, std::uint64_t value)
        {
            std::array<char8_t, 20> digits {};
            std::size_t count { 0 };
            do
            {
                digits[count] = static_cast<char8_t>(u8'0' + value % 10);
                ++count;
                value /= 10;
            } while (value != 0);
            while (count > 0)
            {
                --count;
                text.push_back(digits[count]);
            }
        }

        // 소수 `decimals`자리까지 반올림하고 뒤의 0은 뗀다 (12, 66.7).
        void append_number(std::u8string& text, const double value, const int decimals)
        {
            if (std::isfinite(value) == false || std::abs(value) > 1.0e9)
            {
                text += u8"?";
                return;
            }
            std::int64_t divisor { 1 };
            for (int digit { 0 }; digit < decimals; ++digit)
                divisor *= 10;
            std::int64_t scaled { std::llround(value * static_cast<double>(divisor)) };
            if (scaled < 0)
            {
                text.push_back(u8'-');
                scaled = -scaled;
            }
            append_unsigned(text, static_cast<std::uint64_t>(scaled / divisor));
            const std::int64_t fraction { scaled % divisor };
            if (fraction == 0)
                return;
            std::u8string digits {};
            for (std::int64_t place { divisor / 10 }; place > 0; place /= 10)
                digits.push_back(static_cast<char8_t>(u8'0' + (fraction / place) % 10));
            while (digits.empty() == false && digits.back() == u8'0')
                digits.pop_back();
            text.push_back(u8'.');
            text += digits;
        }

        // 배율은 늘 소수점이 있다 (2.0, 1.25).
        void append_scale(std::u8string& text, const float scale)
        {
            const std::size_t start { text.size() };
            append_number(text, scale, 2);
            if (text.find(u8'.', start) == std::u8string::npos)
                text += u8".0";
        }

        // 앱이 준 글을 그대로 쓰되 제어 문자는 공백으로 바꾼다. 줄이 갈라져 상자의 줄 수가 어긋나는 것을 막는다.
        void append_text(std::u8string& text, const std::u8string_view part)
        {
            for (const char8_t character : part)
                text.push_back(character < u8' ' || character == 0x7f ? u8' ' : character);
        }

        // 논리 픽셀 `x,y WxH`다.
        void append_rect(std::u8string& text, const rect_f& box, const float unit)
        {
            append_number(text, box.x / unit, 1);
            text += u8",";
            append_number(text, box.y / unit, 1);
            text += u8" ";
            append_number(text, box.width / unit, 1);
            text += u8"x";
            append_number(text, box.height / unit, 1);
        }

        void append_kind(std::u8string& text, const ui_element& element, const inspector_config& config)
        {
            text += kind_display_name(element.id().kind, config);
            text += u8":";
            append_text(text, element.id().owner);
        }

        void append_access(std::u8string& text, const ui_element& element)
        {
            const access_info info { element.accessibility() };
            text += u8"access ";
            const std::u8string_view role { access_role_name(info.role) };
            if (role.empty())
            {
                text += u8"role#";
                append_unsigned(text, static_cast<std::uint64_t>(info.role));
            }
            else
                text += role;
            if (info.name.empty() == false)
            {
                text += u8" \"";
                append_text(text, info.name);
                text += u8"\"";
            }
        }

        // 공백으로 잇는 낱말 줄이다.
        struct word_list
        {
            std::u8string text {};

            void add(const std::u8string_view word)
            {
                if (text.empty() == false)
                    text.push_back(u8' ');
                text += word;
            }
        };

        [[nodiscard]] std::u8string flags_of(const ui_element& element, const bool visible_path)
        {
            word_list flags {};
            flags.add(element.enabled() ? u8"enabled" : u8"disabled");
            if (visible_path == false)
                flags.add(u8"hidden");
            else if (element.arranged() == false)
                flags.add(u8"unarranged");
            if (element.hit_opaque())
                flags.add(u8"opaque");
            if (element.tab_stop())
                flags.add(u8"tab");
            if (element.clip_children())
                flags.add(u8"clip");
            if (element.focus_group() != focus_axis::none)
                flags.add(u8"group");
            if (element.focus_trap())
                flags.add(u8"trap");
            if (element.default_button())
                flags.add(u8"default");
            return flags.text;
        }

        // 액션과 역할 포인터의 유무다. 값은 읽지 않고 있는지만 본다.
        [[nodiscard]] std::u8string handles_of(const ui_element& element)
        {
            word_list handles {};
            if (element.action(ui_trigger::left_click) != nullptr)
                handles.add(u8"click");
            if (element.action(ui_trigger::right_click) != nullptr)
                handles.add(u8"right_click");
            if (element.action(ui_trigger::double_click) != nullptr)
                handles.add(u8"double_click");
            if (element.action(ui_trigger::middle_click) != nullptr)
                handles.add(u8"middle_click");
            if (element.has_tooltip())
                handles.add(u8"tooltip");
            if (element.drag() != nullptr)
                handles.add(u8"drag");
            if (element.drop() != nullptr)
                handles.add(u8"drop");
            if (element.pointer_drag() != nullptr)
                handles.add(u8"pointer_drag");
            if (element.key_step() != nullptr)
                handles.add(u8"key_step");
            if (element.scroll() != nullptr)
                handles.add(u8"scroll");
            if (element.zoom() != nullptr)
                handles.add(u8"zoom");
            if (element.ink() != nullptr)
                handles.add(u8"ink");
            if (element.pan_handoff() != nullptr)
                handles.add(u8"pan_handoff");
            if (element.key_search() != nullptr)
                handles.add(u8"key_search");
            if (element.key_capture() != nullptr)
                handles.add(u8"key_capture");
            if (element.hover_dwell() != nullptr)
                handles.add(u8"dwell");
            if (element.hover_region() != nullptr)
                handles.add(u8"region");
            if (element.key_handler_of() != nullptr)
                handles.add(u8"key_handler");
            if (element.cursor() != ui_cursor::inherit)
                handles.add(u8"cursor");
            return handles.text.empty() ? std::u8string { u8"-" } : handles.text;
        }

        // 배율이 쓸 수 없는 값이면 1이다. 배율로 나눠 논리 픽셀을 얻는 자리마다 이 값을 쓴다.
        [[nodiscard]] float unit_of(const float scale) noexcept
        {
            return scale > 0.0f && std::isfinite(scale) ? scale : 1.0f;
        }

        // tree 전체의 요약이다 (요소 수와 중복 id·미배치 수, 0이 아니면 `!`).
        void append_totals(std::u8string& text, const ui_tree& tree, const std::size_t elements)
        {
            text += u8"elements ";
            append_unsigned(text, elements);
            text += u8"  dup ";
            append_unsigned(text, tree.duplicate_ids().size());
            if (tree.duplicate_ids().empty() == false)
                text += u8"!";
            text += u8"  unarranged ";
            append_unsigned(text, tree.unarranged().size());
            if (tree.unarranged().empty() == false)
                text += u8"!";
        }

        // 가려졌으면 `visible x,y WxH`(완전히 가려지면 `visible none`)를 덧붙인다.
        void append_visible(std::u8string& text, const rect_f& bounds, const std::optional<rect_f>& visible, const float unit)
        {
            if (visible.has_value() == false)
                text += u8"  visible none";
            else if (same_rect(*visible, bounds) == false)
            {
                text += u8"  visible ";
                append_rect(text, *visible, unit);
            }
        }
    }

    std::optional<rect_f> inspect_content_bounds(const ui_element& element)
    {
        std::optional<rect_f> joined {};
        for (const std::unique_ptr<ui_element>& child : element.children())
        {
            if (child->visible() == false || child->arranged() == false)
                continue;
            const rect_f box { child->bounds() };
            if (box.width <= 0.0f || box.height <= 0.0f)
                continue;
            if (joined.has_value() == false)
            {
                joined = box;
                continue;
            }
            const float left { std::min(joined->x, box.x) };
            const float top { std::min(joined->y, box.y) };
            const float right { std::max(joined->x + joined->width, box.x + box.width) };
            const float bottom { std::max(joined->y + joined->height, box.y + box.height) };
            joined = rect_f { left, top, right - left, bottom - top };
        }
        if (joined.has_value() == false)
            return std::nullopt;
        return intersect_rects(*joined, element.bounds());
    }

    std::vector<inspect_node> inspect_nodes(const ui_tree& tree)
    {
        std::vector<inspect_node> nodes {};
        if (tree.root() != nullptr)
            collect_nodes(*tree.root(), 0, no_inspect_parent, true, std::nullopt, false, nodes);
        return nodes;
    }

    const ui_element* inspect_at(const ui_tree& tree, const float x, const float y)
    {
        return tree.root() != nullptr ? deepest_at(*tree.root(), x, y) : nullptr;
    }

    const ui_element* pick_inspected(const ui_tree& tree, const interaction_snapshot& interaction, const inspector_config& config)
    {
        const ui_element* picked { nullptr };
        switch (config.pick)
        {
        case inspector_pick::hovered:
            picked = find_by_id(tree, interaction.hovered);
            break;
        case inspector_pick::focused:
            picked = find_by_id(tree, interaction.focused);
            break;
        case inspector_pick::pressed:
            picked = find_by_id(tree, interaction.pressed);
            break;
        case inspector_pick::selected:
            picked = find_by_id(tree, config.selected);
            break;
        case inspector_pick::point:
            if (config.point.has_value())
                picked = inspect_at(tree, config.point->x, config.point->y);
            break;
        }
        if (picked == nullptr || tree.root() == nullptr)
            return nullptr;
        std::vector<const ui_element*> path {};
        if (collect_path(*tree.root(), picked, path) == false)
            return nullptr;
        // 뿌리에서 멈춘다.
        const std::size_t levels { std::min(config.ancestor_levels, path.size() - 1) };
        return path[path.size() - 1 - levels];
    }

    ui_element_id inspect_step(const ui_tree& tree, const ui_element_id& from, const inspect_move move)
    {
        const std::vector<inspect_node> nodes { inspect_nodes(tree) };
        const auto id_of = [&nodes](const std::size_t index) { return nodes[index].element->id(); };
        // 출발점은 보이는 첫 요소다. 숨은 가지의 같은 id는 집지 않는다.
        std::size_t start { no_inspect_parent };
        if (from != ui_element_id {})
        {
            for (std::size_t index { 0 }; index < nodes.size(); ++index)
            {
                if (nodes[index].visible_path && nodes[index].element->id() == from)
                {
                    start = index;
                    break;
                }
            }
        }
        if (start == no_inspect_parent)
        {
            if (move != inspect_move::next)
                return {};
            for (std::size_t index { 0 }; index < nodes.size(); ++index)
            {
                if (nodes[index].visible_path)
                    return id_of(index);
            }
            return {};
        }

        const std::size_t depth { nodes[start].depth };
        switch (move)
        {
        case inspect_move::parent:
            return nodes[start].parent == no_inspect_parent ? ui_element_id {} : id_of(nodes[start].parent);
        case inspect_move::first_child:
            // 자손은 목록에서 바로 뒤에 이어 서고 깊이가 더 깊다.
            for (std::size_t index { start + 1 }; index < nodes.size() && nodes[index].depth > depth; ++index)
            {
                if (nodes[index].parent == start && nodes[index].visible_path)
                    return id_of(index);
            }
            return {};
        case inspect_move::next_sibling:
        {
            std::size_t index { start + 1 };
            // 자기 가지를 건너뛴다.
            while (index < nodes.size() && nodes[index].depth > depth)
                ++index;
            for (; index < nodes.size() && nodes[index].depth >= depth; ++index)
            {
                if (nodes[index].depth == depth && nodes[index].visible_path)
                    return id_of(index);
            }
            return {};
        }
        case inspect_move::previous_sibling:
            for (std::size_t index { start }; index > 0; --index)
            {
                const inspect_node& before { nodes[index - 1] };
                if (before.depth < depth)
                    break;
                if (before.depth == depth && before.visible_path)
                    return id_of(index - 1);
            }
            return {};
        case inspect_move::previous:
            for (std::size_t index { start }; index > 0; --index)
            {
                if (nodes[index - 1].visible_path)
                    return id_of(index - 1);
            }
            return {};
        case inspect_move::next:
            for (std::size_t index { start + 1 }; index < nodes.size(); ++index)
            {
                if (nodes[index].visible_path)
                    return id_of(index);
            }
            return {};
        }
        return {};
    }

    std::u8string kind_display_name(const ui_element_kind kind, const inspector_config& config)
    {
        if (const std::u8string_view library { library_kind_name(kind) }; library.empty() == false)
            return std::u8string { library };
        for (const inspector_kind_name& each : config.kind_names)
        {
            if (each.kind == kind && each.name.empty() == false)
                return each.name;
        }
        std::u8string text { u8"kind#" };
        append_unsigned(text, static_cast<std::uint32_t>(kind));
        return text;
    }

    std::u8string_view access_role_name(const access_role role) noexcept
    {
        // 다른 노드가 역할을 더해도 이 표 때문에 빌드가 깨지지 않도록 `default`를 둔다. 빈 값은 글로 쓸 때 `role#<수>`다.
        switch (role)
        {
        case access_role::none:
            return u8"none";
        case access_role::button:
            return u8"button";
        case access_role::link:
            return u8"link";
        case access_role::check_box:
            return u8"check_box";
        case access_role::radio_button:
            return u8"radio_button";
        case access_role::toggle_switch:
            return u8"toggle_switch";
        case access_role::slider:
            return u8"slider";
        case access_role::scroll_bar:
            return u8"scroll_bar";
        case access_role::progress:
            return u8"progress";
        case access_role::handle:
            return u8"handle";
        case access_role::image:
            return u8"image";
        case access_role::edit:
            return u8"edit";
        case access_role::combo_box:
            return u8"combo_box";
        case access_role::list:
            return u8"list";
        case access_role::list_item:
            return u8"list_item";
        case access_role::header:
            return u8"header";
        case access_role::tab_list:
            return u8"tab_list";
        case access_role::tab:
            return u8"tab";
        case access_role::radio_group:
            return u8"radio_group";
        case access_role::menu:
            return u8"menu";
        case access_role::menu_item:
            return u8"menu_item";
        case access_role::group:
            return u8"group";
        case access_role::title_bar:
            return u8"title_bar";
        case access_role::dialog:
            return u8"dialog";
        case access_role::alert:
            return u8"alert";
        case access_role::static_text:
            return u8"static_text";
        case access_role::pane:
            return u8"pane";
        case access_role::tree:
            return u8"tree";
        case access_role::tree_item:
            return u8"tree_item";
        case access_role::table:
            return u8"table";
        case access_role::data_item:
            return u8"data_item";
        case access_role::header_item:
            return u8"header_item";
        case access_role::tool_bar:
            return u8"tool_bar";
        case access_role::separator:
            return u8"separator";
        case access_role::status_bar:
            return u8"status_bar";
        case access_role::navigation:
            return u8"navigation";
        default:
            return {};
        }
    }

    std::u8string element_class_name(const ui_element& element)
    {
        const char* const raw { typeid(element).name() };
        std::string name {};
#if defined(__GNUG__)
        int status { 0 };
        char* const demangled { abi::__cxa_demangle(raw, nullptr, nullptr, &status) };
        name = status == 0 && demangled != nullptr ? demangled : raw;
        std::free(demangled);
#else
        name = raw;
#endif
        // MSVC가 붙이는 `class `·`struct ` 접두는 읽는 데 보탬이 없다.
        for (const std::string_view prefix : { std::string_view { "class " }, std::string_view { "struct " } })
        {
            if (name.starts_with(prefix))
            {
                name.erase(0, prefix.size());
                break;
            }
        }
        return std::u8string { reinterpret_cast<const char8_t*>(name.data()), name.size() };
    }

    std::u8string describe_element(const ui_tree& tree, const ui_element& element, const float scale, const inspector_config& config)
    {
        const float unit { unit_of(scale) };
        const std::vector<inspect_node> nodes { inspect_nodes(tree) };
        // tree에 없는 요소도 풀어 쓴다. 깊이는 0이고 가려진 데가 없는 것으로 본다.
        std::size_t depth { 0 };
        bool visible_path { true };
        std::optional<rect_f> visible { element.bounds() };
        for (const inspect_node& node : nodes)
        {
            if (node.element == &element)
            {
                depth = node.depth;
                visible_path = node.visible_path;
                visible = node.visible_bounds;
                break;
            }
        }

        std::u8string text {};
        append_kind(text, element, config);
        text += u8"  ";
        text += element_class_name(element);

        text += u8"\nbounds ";
        append_rect(text, element.bounds(), unit);
        text += u8" @";
        append_scale(text, unit);
        text += u8"x";
        append_visible(text, element.bounds(), visible, unit);
        if (const std::optional<rect_f> content { inspect_content_bounds(element) }; content.has_value())
        {
            // 위, 오른쪽, 아래, 왼쪽 차례다 (CSS와 같다).
            const rect_f outer { element.bounds() };
            text += u8"  inset ";
            append_number(text, (content->y - outer.y) / unit, 1);
            text += u8" ";
            append_number(text, (outer.x + outer.width - content->x - content->width) / unit, 1);
            text += u8" ";
            append_number(text, (outer.y + outer.height - content->y - content->height) / unit, 1);
            text += u8" ";
            append_number(text, (content->x - outer.x) / unit, 1);
        }

        text += u8"\nchildren ";
        append_unsigned(text, element.children().size());
        text += u8"  depth ";
        append_unsigned(text, depth);
        text += u8"  flags ";
        text += flags_of(element, visible_path);

        text += u8"\nhandles ";
        text += handles_of(element);
        text += u8"  ";
        append_access(text, element);

        text += u8"\n";
        append_totals(text, tree, nodes.size());
        return text;
    }

    std::u8string dump_tree(const ui_tree& tree, const float scale, const inspector_config& config)
    {
        const float unit { unit_of(scale) };
        const std::vector<inspect_node> nodes { inspect_nodes(tree) };
        std::u8string text {};
        append_totals(text, tree, nodes.size());
        text += u8"  @";
        append_scale(text, unit);
        text += u8"x";
        for (const inspect_node& node : nodes)
        {
            text += u8"\n";
            text.append(node.depth * 2, u8' ');
            append_kind(text, *node.element, config);
            text += u8"  ";
            text += element_class_name(*node.element);
            text += u8"  ";
            append_rect(text, node.bounds, unit);
            if (node.visible_path == false)
                text += u8"  hidden";
            else
                append_visible(text, node.bounds, node.visible_bounds, unit);
            if (node.element->accessibility().role != access_role::none)
            {
                text += u8"  ";
                append_access(text, *node.element);
            }
        }
        return text;
    }

    std::vector<inspector_swatch> inspector_palette_swatches(const ui_color_palette& palette)
    {
        return {
            { u8"window_background", palette.window_background },
            { u8"surface_background", palette.surface_background },
            { u8"primary_foreground", palette.primary_foreground },
            { u8"secondary_foreground", palette.secondary_foreground },
            { u8"disabled_foreground", palette.disabled_foreground },
            { u8"divider", palette.divider },
            { u8"input_background", palette.input_background },
            { u8"input_border", palette.input_border },
            { u8"control_border", palette.control_border },
            { u8"accent", palette.accent },
            { u8"accent_hover", palette.accent_hover },
            { u8"accent_soft", palette.accent_soft },
            { u8"accent_foreground", palette.accent_foreground },
            { u8"selection_background", palette.selection_background },
            { u8"selection_foreground", palette.selection_foreground },
            { u8"raised_background", palette.raised_background },
            { u8"notice_background", palette.notice_background },
            { u8"warning_accent", palette.warning_accent },
            { u8"error_accent", palette.error_accent },
            { u8"tooltip_background", palette.tooltip_background },
            { u8"tooltip_border", palette.tooltip_border },
            { u8"button_hover_background", palette.button_hover_background },
            { u8"button_pressed_background", palette.button_pressed_background },
            { u8"backdrop_background", palette.backdrop_background },
        };
    }
}

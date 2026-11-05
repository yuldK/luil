#include "luil/ui/dock_element.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace luil {
    namespace {
        dock_node* find(dock_node& node, const std::u8string& key)
        {
            if (node.key == key)
                return &node;
            for (auto& child : node.children)
                if (auto* found { find(child, key) }; found != nullptr)
                    return found;
            return nullptr;
        }

        dock_node* find(dock_layout& layout, const std::u8string& key)
        {
            if (auto* found { find(layout.root, key) }; found != nullptr)
                return found;
            for (auto& item : layout.floating)
                if (auto* found { find(item.group, key) }; found != nullptr)
                    return found;
            return nullptr;
        }

        bool remove(dock_node& node, const std::u8string& panel)
        {
            const auto count { std::erase(node.panels, panel) };
            if (count != 0)
            {
                if (node.selected == panel)
                    node.selected = node.panels.empty() ? std::u8string {} : node.panels.front();
                return true;
            }
            for (auto& child : node.children)
                if (remove(child, panel))
                    return true;
            return false;
        }

        bool prune(dock_node& node)
        {
            for (auto it { node.children.begin() }; it != node.children.end();)
                if (prune(*it))
                    it = node.children.erase(it);
                else
                    ++it;
            if (node.children.size() == 1)
            {
                dock_node remaining { std::move(node.children.front()) };
                node = std::move(remaining);
            }
            return node.children.empty() && node.panels.empty();
        }

        std::u8string fresh_key(dock_layout& layout)
        {
            for (;;)
            {
                const auto number { std::to_string(layout.next_key++) };
                std::u8string key { u8"dock-" };
                for (const char digit : number)
                    key += static_cast<char8_t>(digit);
                if (find(layout, key) == nullptr)
                    return key;
            }
        }

        bool valid_node(const dock_node& node, std::set<std::u8string>& keys, std::set<std::u8string>& panels)
        {
            if (node.key.empty() || keys.insert(node.key).second == false || std::isfinite(node.ratio) == false)
                return false;
            if (node.children.empty() == false && (node.children.size() != 2 || node.panels.empty() == false))
                return false;
            if (node.selected.empty() == false && std::ranges::find(node.panels, node.selected) == node.panels.end())
                return false;
            for (const auto& panel : node.panels)
                if (panel.empty() || panels.insert(panel).second == false)
                    return false;
            for (const auto& child : node.children)
                if (valid_node(child, keys, panels) == false)
                    return false;
            return true;
        }
    }

    bool valid_dock_layout(const dock_layout& layout)
    {
        std::set<std::u8string> keys {};
        std::set<std::u8string> panels {};
        if (valid_node(layout.root, keys, panels) == false)
            return false;
        for (const auto& item : layout.floating)
        {
            const auto& box { item.bounds };
            if (std::isfinite(box.x) == false || std::isfinite(box.y) == false || std::isfinite(box.width) == false || std::isfinite(box.height) == false
                || box.width <= 0.0f || box.height <= 0.0f || valid_node(item.group, keys, panels) == false)
                return false;
        }
        return true;
    }

    bool apply_dock_command(dock_layout& layout, const dock_command& command)
    {
        if (valid_dock_layout(layout) == false)
            return false;
        dock_layout next { layout };
        auto* target { find(next, command.target) };
        if (command.operation == dock_operation::select)
        {
            if (target == nullptr || std::ranges::find(target->panels, command.panel) == target->panels.end())
                return false;
            target->selected = command.panel;
            for (auto it { next.floating.begin() }; it != next.floating.end(); ++it)
                if (find(it->group, command.target) != nullptr)
                {
                    auto item { std::move(*it) };
                    next.floating.erase(it);
                    next.floating.push_back(std::move(item));
                    break;
                }
        }
        else if (command.operation == dock_operation::resize)
        {
            if (target == nullptr || target->children.size() != 2 || std::isfinite(command.delta) == false)
                return false;
            target->ratio = std::clamp(target->ratio + command.delta, 0.05f, 0.95f);
        }
        else if (command.operation == dock_operation::raise || command.operation == dock_operation::reposition)
        {
            const auto found { std::ranges::find_if(next.floating, [&](const auto& item) { return item.group.key == command.target; }) };
            if (found == next.floating.end())
                return false;
            auto item { std::move(*found) };
            next.floating.erase(found);
            if (command.operation == dock_operation::reposition)
            {
                if (std::isfinite(command.bounds.x) == false || std::isfinite(command.bounds.y) == false)
                    return false;
                item.bounds.x += command.bounds.x;
                item.bounds.y += command.bounds.y;
            }
            next.floating.push_back(std::move(item));
        }
        else
        {
            if (command.operation == dock_operation::move && (target == nullptr || target->children.empty() == false))
                return false;
            if (command.panel == command.relative_to)
                return false;
            bool removed { remove(next.root, command.panel) };
            for (auto& item : next.floating)
                removed = remove(item.group, command.panel) || removed;
            if (removed == false)
                return false;
            if (command.operation == dock_operation::detach)
            {
                const auto key { fresh_key(next) };
                const rect_f box { command.bounds.width > 0.0f && command.bounds.height > 0.0f ? command.bounds : rect_f { 24.0f, 24.0f, 360.0f, 240.0f } };
                next.floating.push_back({ { .key = key, .panels = { command.panel }, .selected = command.panel }, box });
            }
            else if (command.placement == dock_placement::center)
            {
                auto at { std::ranges::find(target->panels, command.relative_to) };
                if (at != target->panels.end() && command.side == drop_side::after)
                    ++at;
                target->panels.insert(at, command.panel);
                target->selected = command.panel;
            for (auto it { next.floating.begin() }; it != next.floating.end(); ++it)
                if (find(it->group, command.target) != nullptr)
                {
                    auto item { std::move(*it) };
                    next.floating.erase(it);
                    next.floating.push_back(std::move(item));
                    break;
                }
            }
            else
            {
                const auto new_key { fresh_key(next) };
                const auto split_key { fresh_key(next) };
                dock_node existing { std::move(*target) };
                dock_node added { .key = new_key, .panels = { command.panel }, .selected = command.panel };
                const bool first { command.placement == dock_placement::left || command.placement == dock_placement::top };
                *target = { .key = split_key, .axis = command.placement == dock_placement::left || command.placement == dock_placement::right ? split_axis::horizontal : split_axis::vertical };
                target->children.push_back(first ? std::move(added) : std::move(existing));
                target->children.push_back(first ? std::move(existing) : std::move(added));
            }
            (void)prune(next.root);
            std::erase_if(next.floating, [](auto& item) { return prune(item.group); });
        }
        if (valid_dock_layout(next) == false)
            return false;
        layout = std::move(next);
        return true;
    }
}

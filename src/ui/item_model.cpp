#include "luil/ui/item_model.h"

#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] bool branch(const tree_node& node) noexcept
        {
            return node.children.empty() == false || node.load != node_load::loaded;
        }

        // 필터가 이 노드를 남기는가 (자기 또는 자손이 맞는다).
        [[nodiscard]] bool kept(const tree_node& node, const tree_flatten_options& options)
        {
            if (options.keep == nullptr || options.keep(node))
                return true;
            return std::any_of(node.children.begin(), node.children.end(), [&options](const tree_node& child) { return kept(child, options); });
        }

        [[nodiscard]] std::vector<const tree_node*> ordered(std::span<const tree_node> nodes, const tree_flatten_options& options)
        {
            std::vector<const tree_node*> order {};
            order.reserve(nodes.size());
            for (const tree_node& node : nodes)
                if (kept(node, options))
                    order.push_back(&node);
            if (options.less != nullptr)
                std::stable_sort(order.begin(), order.end(), [&options](const tree_node* left, const tree_node* right) { return options.less(*left, *right); });
            return order;
        }

        [[nodiscard]] tree_row status_row(const tree_node& node, const int depth, const tree_row_kind kind, const tree_flatten_options& options)
        {
            tree_row row {};
            const bool loading { kind == tree_row_kind::loading };
            row.item.key = node.key;
            row.item.key.append(loading ? tree_loading_suffix : tree_failed_suffix);
            row.item.label = loading ? options.loading_label : options.failed_label;
            row.item.depth = depth;
            // 불러오는 중 줄은 설 자리가 아니다. 실패 줄은 다시 시도를 실행하는 자리다.
            row.item.enabled = loading == false;
            row.kind = kind;
            row.node = node.key;
            return row;
        }

        void flatten(std::span<const tree_node> nodes, const int depth, const std::unordered_set<std::u8string>& expanded, const tree_flatten_options& options,
            std::vector<tree_row>& rows)
        {
            for (const tree_node* const node : ordered(nodes, options))
            {
                // 필터가 있으면 자기가 맞지 않고 자손만 맞는 노드는 길이다. 그 노드는 찾는 동안 펼친 것으로 볼 수 있다.
                const bool path_only { options.keep != nullptr && options.keep(*node) == false };
                const bool open { expanded.contains(node->key) || (options.expand_matches && path_only) };
                tree_row row {};
                row.item.key = node->key;
                row.item.label = node->label;
                row.item.enabled = node->enabled;
                row.item.depth = depth;
                if (branch(*node))
                    row.item.expansion = open ? list_expansion::expanded : list_expansion::collapsed;
                row.node = node->key;
                rows.push_back(std::move(row));
                if (open == false || branch(*node) == false)
                    continue;
                switch (node->load)
                {
                case node_load::unloaded:
                case node_load::loading:
                    rows.push_back(status_row(*node, depth + 1, tree_row_kind::loading, options));
                    break;
                case node_load::failed:
                    rows.push_back(status_row(*node, depth + 1, tree_row_kind::failed, options));
                    break;
                case node_load::loaded:
                    // 맞는 자손이 있어 남은 길은 자손도 거른다. 자기가 맞는 노드의 자손도 맞아야 남는다.
                    flatten(node->children, depth + 1, expanded, options, rows);
                    break;
                }
            }
        }

        void collect_unloaded(std::span<const tree_node> nodes, const std::unordered_set<std::u8string>& expanded, std::vector<std::u8string>& keys)
        {
            for (const tree_node& node : nodes)
            {
                if (expanded.contains(node.key) == false)
                    continue;
                if (node.load == node_load::unloaded)
                    keys.push_back(node.key);
                else if (node.load == node_load::loaded)
                    collect_unloaded(node.children, expanded, keys);
            }
        }
    } // namespace

    std::vector<tree_row> flatten_tree(const std::span<const tree_node> roots, const std::unordered_set<std::u8string>& expanded, const tree_flatten_options& options)
    {
        std::vector<tree_row> rows {};
        flatten(roots, 0, expanded, options, rows);
        return rows;
    }

    std::vector<virtual_list_item> tree_row_items(const std::span<const tree_row> rows)
    {
        std::vector<virtual_list_item> items {};
        items.reserve(rows.size());
        for (const tree_row& row : rows)
            items.push_back(row.item);
        return items;
    }

    std::vector<std::u8string> tree_nodes_to_load(const std::span<const tree_node> roots, const std::unordered_set<std::u8string>& expanded)
    {
        std::vector<std::u8string> keys {};
        collect_unloaded(roots, expanded, keys);
        return keys;
    }

    std::optional<std::size_t> view_order::view_index(const std::size_t source) const noexcept
    {
        const auto found { std::find(source_index.begin(), source_index.end(), source) };
        if (found == source_index.end())
            return std::nullopt;
        return static_cast<std::size_t>(found - source_index.begin());
    }

    request_token request_ledger::issue(const std::u8string_view key)
    {
        // 0은 "낸 적 없음"이라 1부터 낸다.
        ++next_;
        latest_[std::u8string { key }] = next_;
        return { next_ };
    }

    bool request_ledger::current(const std::u8string_view key, const request_token token) const
    {
        const auto found { latest_.find(std::u8string { key }) };
        return found != latest_.end() && found->second == token.value;
    }

    bool request_ledger::settle(const std::u8string_view key, const request_token token)
    {
        if (current(key, token) == false)
            return false;
        latest_.erase(std::u8string { key });
        return true;
    }

    void request_ledger::cancel(const std::u8string_view key)
    {
        latest_.erase(std::u8string { key });
    }

    std::size_t request_ledger::pending() const noexcept
    {
        return latest_.size();
    }
} // namespace luil

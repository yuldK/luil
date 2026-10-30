#include "luil/ui/selection_model.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_set>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] std::optional<std::size_t> index_of(const std::span<const virtual_list_item> items, const std::u8string_view key) noexcept
        {
            if (key.empty())
                return std::nullopt;
            for (std::size_t index = 0; index < items.size(); ++index)
                if (items[index].key == key)
                    return index;
            return std::nullopt;
        }

        // 고른 키들을 모델 순서로 다시 세운다. 모델에 없거나 비활성인 키는 빠진다.
        [[nodiscard]] std::vector<std::u8string> in_model_order(const std::span<const virtual_list_item> items, const std::unordered_set<std::u8string_view>& chosen)
        {
            std::vector<std::u8string> keys {};
            if (chosen.empty())
                return keys;
            for (const virtual_list_item& item : items)
                if (item.enabled && chosen.contains(item.key))
                    keys.push_back(item.key);
            return keys;
        }

        [[nodiscard]] std::unordered_set<std::u8string_view> as_set(const std::vector<std::u8string>& keys)
        {
            return { keys.begin(), keys.end() };
        }

        // 두 자리 사이(양끝 포함)의 활성 항목을 `chosen`에 더한다.
        void add_range(const std::span<const virtual_list_item> items, const std::size_t from, const std::size_t to, std::unordered_set<std::u8string_view>& chosen)
        {
            const std::size_t first { std::min(from, to) };
            const std::size_t last { std::max(from, to) };
            for (std::size_t index = first; index <= last; ++index)
                if (items[index].enabled)
                    chosen.insert(items[index].key);
        }

        // Shift 범위의 기준점 자리다. 기준점이 모델에 없으면 커서를, 그것도 없으면 `fallback`을 쓴다.
        // 기준점을 바꿔 썼으면 `selection.anchor`도 그 키로 고친다 — 다음 Shift가 같은 자리에서 시작해야 한다.
        [[nodiscard]] std::size_t anchor_index(item_selection& selection, const std::span<const virtual_list_item> items, const std::size_t fallback)
        {
            if (const std::optional<std::size_t> anchor { index_of(items, selection.anchor) }; anchor.has_value())
                return *anchor;
            const std::size_t chosen { index_of(items, selection.cursor).value_or(fallback) };
            selection.anchor = items[chosen].key;
            return chosen;
        }

        // 고른 것을 기준점부터 `index`까지로 바꾸거나(`keep` 거짓) 그 구간을 더한다(`keep` 참).
        void extend_to(item_selection& selection, const std::span<const virtual_list_item> items, const std::size_t index, const bool keep)
        {
            const std::size_t anchor { anchor_index(selection, items, index) };
            std::unordered_set<std::u8string_view> chosen { keep ? as_set(selection.keys) : std::unordered_set<std::u8string_view> {} };
            add_range(items, anchor, index, chosen);
            selection.keys = in_model_order(items, chosen);
        }

        // 옛 모델에서 `key` 뒤쪽으로 가장 가까운, 새 모델에 남은 키다. 뒤에 없으면 앞쪽이다.
        [[nodiscard]] std::optional<std::u8string> nearest_surviving(
            const std::span<const virtual_list_item> previous, const std::unordered_set<std::u8string_view>& surviving, const std::u8string_view key)
        {
            const std::optional<std::size_t> origin { index_of(previous, key) };
            if (origin.has_value() == false)
                return std::nullopt;
            for (std::size_t index = *origin + 1u; index < previous.size(); ++index)
                if (surviving.contains(previous[index].key))
                    return previous[index].key;
            for (std::size_t index = *origin; index > 0u; --index)
                if (surviving.contains(previous[index - 1u].key))
                    return previous[index - 1u].key;
            return std::nullopt;
        }
    }

    bool selection_contains(const item_selection& selection, const std::u8string_view key) noexcept
    {
        return std::find(selection.keys.begin(), selection.keys.end(), key) != selection.keys.end();
    }

    item_selection selection_pick(item_selection selection, const std::span<const virtual_list_item> items, const std::u8string_view key, const ui_action_context& context)
    {
        const std::optional<std::size_t> index { index_of(items, key) };
        if (index.has_value() == false || items[*index].enabled == false)
            return selection;
        if (context.shift)
        {
            extend_to(selection, items, *index, context.control);
            selection.cursor = items[*index].key;
            return selection;
        }
        if (context.control)
            return selection_toggle(std::move(selection), items, key);
        selection.keys = { items[*index].key };
        selection.anchor = items[*index].key;
        selection.cursor = items[*index].key;
        return selection;
    }

    item_selection selection_move(item_selection selection, const std::span<const virtual_list_item> items, const std::u8string_view key, const virtual_list_move_context& context)
    {
        const std::optional<std::size_t> index { index_of(items, key) };
        if (index.has_value() == false)
            return selection;
        if (context.extend)
        {
            extend_to(selection, items, *index, context.keep_selection);
            selection.cursor = items[*index].key;
            return selection;
        }
        selection.cursor = items[*index].key;
        if (context.keep_selection)
            return selection;
        // 비활성 항목에는 커서가 설 수는 있어도 고를 수는 없다.
        selection.keys.clear();
        if (items[*index].enabled)
            selection.keys.push_back(items[*index].key);
        selection.anchor = items[*index].key;
        return selection;
    }

    item_selection selection_toggle(item_selection selection, const std::span<const virtual_list_item> items, const std::u8string_view key)
    {
        const std::optional<std::size_t> index { index_of(items, key) };
        if (index.has_value() == false || items[*index].enabled == false)
            return selection;
        std::unordered_set<std::u8string_view> chosen { as_set(selection.keys) };
        if (chosen.erase(items[*index].key) == 0u)
            chosen.insert(items[*index].key);
        selection.keys = in_model_order(items, chosen);
        selection.anchor = items[*index].key;
        selection.cursor = items[*index].key;
        return selection;
    }

    item_selection selection_select_all(item_selection selection, const std::span<const virtual_list_item> items)
    {
        selection.keys.clear();
        for (const virtual_list_item& item : items)
            if (item.enabled)
                selection.keys.push_back(item.key);
        return selection;
    }

    item_selection selection_reconcile(item_selection selection, const std::span<const virtual_list_item> previous, const std::span<const virtual_list_item> items)
    {
        if (items.empty())
            return {};
        std::unordered_set<std::u8string_view> surviving {};
        surviving.reserve(items.size());
        for (const virtual_list_item& item : items)
            surviving.insert(item.key);

        // 고른 것에 남는 것은 새 모델에 있고 활성인 키다. 새 모델 순서로 다시 선다.
        selection.keys = in_model_order(items, as_set(selection.keys));

        if (surviving.contains(selection.cursor) == false)
        {
            if (std::optional<std::u8string> nearest { nearest_surviving(previous, surviving, selection.cursor) }; nearest.has_value())
                selection.cursor = std::move(*nearest);
            else
            {
                // 옛 모델을 모르거나 남은 것이 없으면 새 모델의 첫 활성 항목이다 (없으면 첫 항목).
                const auto first_enabled { std::find_if(items.begin(), items.end(), [](const virtual_list_item& item) { return item.enabled; }) };
                selection.cursor = first_enabled != items.end() ? first_enabled->key : items.front().key;
            }
        }
        if (surviving.contains(selection.anchor) == false)
        {
            std::optional<std::u8string> nearest { nearest_surviving(previous, surviving, selection.anchor) };
            selection.anchor = nearest.has_value() ? std::move(*nearest) : selection.cursor;
        }
        return selection;
    }
}

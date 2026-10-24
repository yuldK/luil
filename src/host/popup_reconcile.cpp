#include "host/popup_reconcile.h"

#include "luil/app/app_host.h"

#include <algorithm>

namespace luil {
    namespace {
        [[nodiscard]] const popup_placement* find_by_id(const std::span<const popup_placement> placements, const std::u8string& id)
        {
            for (const popup_placement& placement : placements)
                if (placement.id == id)
                    return &placement;
            return nullptr;
        }

        [[nodiscard]] bool visible(const popup_placement& placement) noexcept
        {
            return placement.width > 0 && placement.height > 0;
        }
    } // namespace

    bool popup_is_descendant(const std::span<const popup_placement> placements, const std::u8string& child, const std::u8string& ancestor) noexcept
    {
        const popup_placement* current { find_by_id(placements, child) };
        for (std::size_t depth { 0 }; current != nullptr && depth < placements.size(); ++depth)
        {
            if (current->anchor == ancestor)
                return true;
            current = find_by_id(placements, current->anchor);
        }
        return false;
    }

    std::vector<std::size_t> order_popup_placements(const std::span<const popup_placement> wanted, const std::span<const std::u8string> roots)
    {
        std::vector<std::size_t> result {};
        std::vector<bool> added(wanted.size());
        for (std::size_t pass { 0 }; pass < wanted.size(); ++pass)
        {
            bool changed { false };
            for (std::size_t index { 0 }; index < wanted.size(); ++index)
            {
                const auto& item { wanted[index] };
                if (added[index] || item.id.empty() || visible(item) == false
                    || std::count_if(wanted.begin(), wanted.end(), [&item](const auto& other) { return other.id == item.id; }) != 1
                    || std::find(roots.begin(), roots.end(), item.id) != roots.end())
                    continue;
                const bool root { std::find(roots.begin(), roots.end(), item.anchor) != roots.end() };
                const bool parent { std::any_of(result.begin(), result.end(), [&](const auto previous) { return wanted[previous].id == item.anchor; }) };
                if (root || parent)
                {
                    added[index] = true;
                    result.push_back(index);
                    changed = true;
                }
            }
            if (changed == false)
                break;
        }
        return result;
    }

    popup_corner resolve_popup_corner(const popup_corner requested, const bool rounded_supported) noexcept
    {
        if (rounded_supported == false)
            return popup_corner::square;
        // OS 기본은 이 OS가 popup에 쓰는 모양이다 — Windows 11의 메뉴·tooltip은 작게 둥글다.
        return requested == popup_corner::system ? popup_corner::round_small : requested;
    }

    popup_shape resolve_popup_shape(const popup_corner requested, const bool border, const bool rounded_supported) noexcept
    {
        const popup_corner corner { resolve_popup_corner(requested, rounded_supported) };
        const bool rounded { corner != popup_corner::square };
        return popup_shape {
            .corner = corner,
            .drop_shadow = rounded == false,
            .line = border && rounded == false,
            .system_border = border && rounded,
        };
    }

    float popup_corner_logical_radius(const popup_corner resolved) noexcept
    {
        switch (resolved)
        {
        case popup_corner::round:
            return 8.0f;
        case popup_corner::round_small:
            return 4.0f;
        case popup_corner::system:
        case popup_corner::square:
            break;
        }
        return 0.0f;
    }

    float popup_corner_radius(const popup_corner corner, const ui_platform& platform) noexcept
    {
        return popup_corner_logical_radius(resolve_popup_corner(corner, platform.rounded_popup_corners));
    }

    popup_reconcile_result reconcile_popups(const std::span<const popup_placement> alive, const std::span<const popup_placement> wanted)
    {
        popup_reconcile_result result {};
        for (std::size_t index = 0; index < wanted.size(); ++index)
        {
            const popup_placement& want { wanted[index] };
            // 크기 없는 popup은 만들지 않는다.
            if (visible(want) == false)
                continue;
            const popup_placement* const existing { find_by_id(alive, want.id) };
            // 앵커가 바뀌면 창의 소유자를 고칠 수 없으므로 다시 만든다. 둥근 창과 직각 창 사이를 오가도 같다 —
            // 그림자가 창 클래스의 것이다. 없애는 쪽은 아래 순회가 같은 판정으로 낸다.
            if (existing == nullptr || existing->anchor != want.anchor || existing->rounded != want.rounded)
            {
                result.create.push_back(index);
                continue;
            }
            if (*existing != want)
                result.move.push_back(index);
        }

        for (const popup_placement& current : alive)
        {
            const popup_placement* const still { find_by_id(wanted, current.id) };
            if (still == nullptr || visible(*still) == false || still->anchor != current.anchor || still->rounded != current.rounded)
                result.destroy.push_back(current.id);
        }
        // 부모 창이 다시 만들어지면 그 소유 창인 자식도 다시 만든다.
        for (std::size_t pass { 0 }; pass < alive.size(); ++pass)
            for (const auto& current : alive)
            {
                if (std::find(result.destroy.begin(), result.destroy.end(), current.anchor) == result.destroy.end()
                    || std::find(result.destroy.begin(), result.destroy.end(), current.id) != result.destroy.end())
                    continue;
                result.destroy.push_back(current.id);
                const auto* const want { find_by_id(wanted, current.id) };
                if (want != nullptr && visible(*want))
                {
                    const auto index { static_cast<std::size_t>(want - wanted.data()) };
                    result.move.erase(std::remove(result.move.begin(), result.move.end(), index), result.move.end());
                    if (std::find(result.create.begin(), result.create.end(), index) == result.create.end())
                        result.create.push_back(index);
                }
            }
        std::sort(result.create.begin(), result.create.end());
        return result;
    }

    window_reconcile_result reconcile_windows(const std::span<const std::u8string> alive, const std::span<const std::u8string> wanted)
    {
        const auto contains = [](const std::span<const std::u8string> ids, const std::u8string& id) {
            for (const std::u8string& entry : ids)
                if (entry == id)
                    return true;
            return false;
        };

        window_reconcile_result result {};
        for (std::size_t index = 0; index < wanted.size(); ++index)
            if (contains(alive, wanted[index]) == false)
                result.create.push_back(index);
        for (const std::u8string& id : alive)
            if (contains(wanted, id) == false)
                result.destroy.push_back(id);
        return result;
    }

    webview_reconcile_result reconcile_webviews(const std::span<const webview_placement> alive, const std::span<const webview_placement> wanted)
    {
        const auto find = [](const std::span<const webview_placement> list, const std::u8string& id) -> const webview_placement* {
            for (const webview_placement& entry : list)
                if (entry.id == id)
                    return &entry;
            return nullptr;
        };

        webview_reconcile_result result {};
        for (std::size_t index = 0; index < wanted.size(); ++index)
        {
            const webview_placement* const existing { find(alive, wanted[index].id) };
            // 앵커가 바뀌면 합성 visual과 parentWindow를 함께 옮겨야 하므로 다시 만든다.
            // 없애는 쪽은 아래 순회가 같은 판정으로 낸다.
            if (existing == nullptr || existing->anchor != wanted[index].anchor)
                result.create.push_back(index);
        }

        for (const webview_placement& current : alive)
        {
            const webview_placement* const still { find(wanted, current.id) };
            if (still == nullptr || still->anchor != current.anchor)
                result.destroy.push_back(current.id);
        }
        return result;
    }

    std::pair<int, int> clamp_popup_position(int x, int y, const int width, const int height, const screen_area& area) noexcept
    {
        if (x + width > area.right)
            x = area.right - width;
        if (x < area.left)
            x = area.left;
        if (y + height > area.bottom)
            y = area.bottom - height;
        if (y < area.top)
            y = area.top;
        return { x, y };
    }
} // namespace luil

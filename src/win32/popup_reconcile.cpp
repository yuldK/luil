#include "win32/popup_reconcile.h"

namespace luil::win32 {
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
            // 앵커가 바뀌면 창의 소유자를 고칠 수 없으므로 다시 만든다.
            // 없애는 쪽은 아래 순회가 같은 판정으로 낸다.
            if (existing == nullptr || existing->anchor != want.anchor)
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
            if (still == nullptr || visible(*still) == false || still->anchor != current.anchor)
                result.destroy.push_back(current.id);
        }
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
} // namespace luil::win32

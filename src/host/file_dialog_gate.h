#pragma once

#include <cstdint>
#include <unordered_set>

namespace luil {
    // OS modal loop가 재진입해도 같은 요청이나 다른 요청을 겹쳐 열지 않는다.
    class file_dialog_gate
    {
    public:
        [[nodiscard]] bool try_begin(const std::uint64_t id)
        {
            if (active_ || seen_.contains(id))
                return false;
            seen_.insert(id);
            active_ = true;
            return true;
        }
        void finish() noexcept { active_ = false; }
    private:
        std::unordered_set<std::uint64_t> seen_ {};
        bool active_ { false };
    };
} // namespace luil

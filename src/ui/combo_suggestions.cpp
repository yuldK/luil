#include "luil/ui/combo_box_element.h"

#include <utility>

namespace luil {
    std::uint64_t begin_combo_query(combo_suggestion_state& state, std::u8string query)
    {
        ++state.token;
        if (state.token == 0)
            ++state.token;
        state.query = std::move(query);
        // 앞 후보는 새 답이 올 때까지 둔다. 비우면 받는 동안 강조·↑↓·Enter의 정확한 후보 선택이 끊긴다.
        state.loading = true;
        return state.token;
    }

    bool accept_combo_suggestions(combo_suggestion_state& state, const std::uint64_t token, std::shared_ptr<const std::vector<combo_suggestion>> items)
    {
        if (token != state.token || state.loading == false)
            return false;
        state.items = std::move(items);
        state.loading = false;
        return true;
    }
}

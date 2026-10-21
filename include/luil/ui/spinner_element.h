#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    struct spinner_config
    {
        // 값이 없는 진행을 보조 기술이 알아볼 이름이다.
        std::u8string name {};
        float size { 16.0f };
    };

    class spinner_element final : public ui_element
    {
    public:
        spinner_element(ui_element_id id, spinner_config config = {});
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        spinner_config config_ {};
    };
} // namespace luil

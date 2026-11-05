#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    // offset은 페이지 단위다. 1.25면 두 번째 장에서 다음 장으로 1/4만큼 넘겼다.
    // 끌 때는 offset을, 놓을 때는 settled_pager_offset을 앱 상태에 적용한다.
    [[nodiscard]] float settled_pager_offset(float offset, float velocity, float page_width, std::size_t count) noexcept;

    enum class pager_operation { drag, step, settle, select };
    struct pager_command
    {
        pager_operation operation { pager_operation::drag };
        // 페이지 단위 변화량과 초당 페이지 수다.
        float delta { 0.0f };
        float velocity { 0.0f };
        std::size_t page { 0 };
    };
    // 앱이 가진 최신 offset에 적용한다. 같은 frame 사이에 이동이 여러 번 와도 잃지 않는다.
    [[nodiscard]] float apply_pager_command(float offset, std::size_t count, const pager_command& command) noexcept;

    struct pager_config
    {
        float offset { 0.0f };
        std::function<input_action(pager_command)> change {};
        std::u8string name {};
    };

    class pager_element final : public ui_element
    {
    public:
        pager_element(ui_element_id id, pager_config config);
        void add_page(std::unique_ptr<ui_element> page);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        pager_config config_ {};
    };
}

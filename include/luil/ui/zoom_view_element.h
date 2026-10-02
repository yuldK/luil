#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    struct zoom_view_state
    {
        float zoom { 1.0f };
        zoom_point origin {};
    };

    // 원점·기준점·이동량은 창 가운데 기준 논리 픽셀이다. 내용 경계만 내용 좌표다.
    struct zoom_view_config
    {
        std::u8string owner {};
        float zoom { 1.0f };
        zoom_point origin {};
        float minimum_zoom { 0.1f };
        float maximum_zoom { 10.0f };
        std::optional<rect_f> content_bounds {};
        std::function<input_action(float factor, zoom_point anchor)> zoom_by {};
        std::function<input_action(zoom_point delta)> pan_by {};
        std::function<input_action(float value)> zoom_to {};
        zoom_wheel_mode wheel { zoom_wheel_mode::zoom };
    };

    [[nodiscard]] zoom_view_state zoom_about(const zoom_view_config& config, zoom_point viewport, float factor, zoom_point anchor) noexcept;
    [[nodiscard]] zoom_view_state pan_by(const zoom_view_config& config, zoom_point viewport, zoom_point delta) noexcept;
    [[nodiscard]] zoom_view_state zoom_to_fit(const zoom_view_config& config, zoom_point viewport, float margin = 16.0f, float maximum_fit_zoom = 1.0f) noexcept;

    // 내용은 하나다. 배치 배율을 바꾸므로 그림과 hit test가 같은 물리 좌표를 쓴다.
    class zoom_view_element final : public ui_element
    {
    public:
        explicit zoom_view_element(zoom_view_config config);
        void set_content(std::unique_ptr<ui_element> content);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] zoom_view_state state() const noexcept;
        [[nodiscard]] access_info accessibility() const override;
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        [[nodiscard]] std::optional<std::vector<input_action>> keys(const key_pressed_event& event) const;
        zoom_view_config config_ {};
        float scale_ { 1.0f };
        ui_element* content_ { nullptr };
    };

    struct zoom_controls_config
    {
        zoom_view_config view {};
        // 보기의 논리 크기다. 맞춤 식과 본문이 같은 창을 보게 담는 쪽이 준다.
        zoom_point viewport {};
        float fit_margin { 16.0f };
        float maximum_fit_zoom { 1.0f };
    };

    // 축소·배율·확대·맞춤 단추다. 맞춤은 내용 경계를 받은 때만 선다.
    class zoom_controls_element final : public ui_element
    {
    public:
        explicit zoom_controls_element(zoom_controls_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    };
} // namespace luil

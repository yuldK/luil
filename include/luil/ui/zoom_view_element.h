#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    struct zoom_view_state
    {
        float zoom { 1.0f };
        zoom_point origin {};
    };

    // 내용 경계(`content_bounds`)로 위치를 다듬는 방식이다.
    enum class zoom_bounds
    {
        // 내용과 창의 겹침을 축마다 최소 1 논리 픽셀 남긴다. 큰 도면의 가장자리를 창 가운데로 옮길 수 있다.
        overlap,
        // 창보다 작은 축은 가운데에 고정하고, 큰 축은 빈 곳이 보이지 않게 한다. 쪽·사진 한 장 보기의 방식이다 —
        // 맞춤 상태에서는 옮길 여지가 없어 쓸기가 전부 경계에 막힌다.
        contain,
    };

    // 두 번 탭 확대다 (zoom-view-design.md의 두 번 탭). 기본은 끈다.
    //  - 내용에 두 번 탭 동작이 없는 보기(그림 한 장 등)에서 켠다. 첫 탭은 늦추지 않고 그대로 나가므로, 내용의
    //    탭 동작이 있으면 두 번 탭의 첫 탭이 그것도 실행한다.
    //  - 기준 배율(내용 경계가 있으면 맞춤, 없으면 100%)이면 탭한 점을 기준으로 `factor`배로 키우고, 아니면 기준으로
    //    돌아간다. 메시지는 `zoom_by`·`pan_by`다.
    struct zoom_double_tap
    {
        bool enabled { false };
        float factor { 2.0f };
        // 맞춤의 여백과 상한이다 (`zoom_controls_config`와 같은 뜻).
        float fit_margin { 16.0f };
        float maximum_fit_zoom { 1.0f };
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
        zoom_bounds bounds { zoom_bounds::overlap };
        // 터치·펜 접촉이 끝날 때 경계에 막힌 쓸기를 알리는 메시지다 (`zoom_edge_swipe`). 비어 있으면 끈다.
        //  - 핀치가 섞인 접촉과 마우스 끌기는 알리지 않는다.
        std::function<input_action(const zoom_edge_swipe&)> edge_swipe {};
        zoom_edge_swipe_config edge_swipe_config {};
        // 터치의 두 번 탭으로 기준 배율과 확대를 오간다. 기본은 끈다.
        zoom_double_tap double_tap {};
    };

    [[nodiscard]] zoom_view_state zoom_about(const zoom_view_config& config, zoom_point viewport, float factor, zoom_point anchor) noexcept;
    [[nodiscard]] zoom_view_state pan_by(const zoom_view_config& config, zoom_point viewport, zoom_point delta) noexcept;
    [[nodiscard]] zoom_view_state zoom_to_fit(const zoom_view_config& config, zoom_point viewport, float margin = 16.0f, float maximum_fit_zoom = 1.0f) noexcept;
    // `pan_by`가 경계 다듬기로 버리는 몫이다 (`delta` − 실제로 옮겨진 몫). 축마다 따로다.
    [[nodiscard]] zoom_point pan_blocked(const zoom_view_config& config, zoom_point viewport, zoom_point delta) noexcept;
    // 접촉 하나의 옮겨진 몫·막힌 몫·뗄 때의 속도로 경계 쓸기를 판정한다 (`zoom_edge_swipe_config`의 세 기준).
    [[nodiscard]] zoom_edge_swipe judge_edge_swipe(const zoom_edge_swipe_config& config, zoom_point moved, zoom_point blocked, zoom_point velocity) noexcept;
    // 두 번 탭의 메시지다 (`zoom_double_tap`). 기준 배율이면 `anchor`를 기준으로 키우고, 아니면 기준으로 돌아간다.
    [[nodiscard]] std::vector<input_action> double_tap_zoom(const zoom_view_config& config, zoom_point viewport, zoom_point anchor);

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
        // 배치한 상태와 창 크기로 컨트롤러에 내놓을 확대 원천이다.
        [[nodiscard]] zoom_source make_source(zoom_point viewport) const;
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

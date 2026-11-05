#pragma once

#include "luil/ui/split_handle_element.h"
#include "luil/ui/tab_bar_element.h"

namespace luil {
    // leaf는 탭 그룹, children 두 개가 있으면 분할이다. key는 layout 전체에서 유일하다.
    struct dock_node
    {
        std::u8string key {};
        std::vector<std::u8string> panels {};
        std::u8string selected {};
        split_axis axis { split_axis::horizontal };
        float ratio { 0.5f };
        std::vector<dock_node> children {};
    };

    struct floating_dock
    {
        dock_node group {};
        // dock 자리 안의 논리 픽셀이다. 같은 표면 위에 떠 있고 뒤 항목이 앞에 선다.
        rect_f bounds { 24.0f, 24.0f, 360.0f, 240.0f };
    };

    struct dock_layout
    {
        dock_node root { .key = u8"root" };
        std::vector<floating_dock> floating {};
        std::uint64_t next_key { 1 };
    };

    enum class dock_placement { center, left, right, top, bottom };
    enum class dock_operation { select, move, detach, resize, raise, reposition };

    struct dock_command
    {
        dock_operation operation { dock_operation::move };
        std::u8string panel {};
        std::u8string target {};
        dock_placement placement { dock_placement::center };
        // center에 넣을 때 이 탭 앞/뒤다. 비면 맨 뒤다.
        std::u8string relative_to {};
        drop_side side { drop_side::before };
        // resize는 비율 변화량, reposition은 bounds의 x/y 변화량이다.
        float delta { 0.0f };
        rect_f bounds {};
        // 앱이 별도 OS 창으로 떼어 낼 때 쓸 원본 좌표다. 기본 helper는 같은 표면에 띄운다.
        std::optional<drag_release> release {};
    };

    [[nodiscard]] bool valid_dock_layout(const dock_layout& layout);
    // 잘못된 명령은 false이고 원본은 그대로다. 빈 그룹·한쪽만 남은 분할은 정리한다.
    [[nodiscard]] bool apply_dock_command(dock_layout& layout, const dock_command& command);

    struct dock_panel
    {
        std::u8string key {};
        std::u8string label {};
        char32_t icon { 0 };
    };

    struct dock_config
    {
        std::u8string owner {};
        dock_layout layout {};
        std::vector<dock_panel> panels {};
        std::function<std::unique_ptr<ui_element>(const std::u8string& panel)> content {};
        std::function<input_action(dock_command)> change {};
        float tab_height { 32.0f };
        float handle_width { 6.0f };
        float minimum_pane { 80.0f };
    };

    class dock_element final : public ui_element
    {
    public:
        explicit dock_element(dock_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        dock_config config_ {};
        struct geometry;
        std::shared_ptr<geometry> geometry_ {};
        std::unique_ptr<ui_element> make_node(const dock_node& node);
        ui_element* root_ { nullptr };
        std::vector<ui_element*> floating_ {};
    };
}

#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <string>
#include <vector>

namespace luil {
    // 메뉴 항목 하나의 설정이다.
    struct menu_item_config
    {
        // 항목을 가리키는 앱 정의 키다 (ui_element_id::owner 규약).
        std::u8string key {};
        std::u8string label {};
        // 0이면 아이콘이 없다.
        char32_t icon { 0 };
        // 거짓이면 흐리게 그리고 눌리지 않으며 키보드 탐색이 건너뛴다.
        bool enabled { true };
        // 참이면 이 항목 위에 구분선을 긋는다.
        bool separator_above { false };
    };

    // 고른 항목의 키를 담은 메시지다.
    using menu_message_factory = std::function<input_action(const std::u8string& key)>;

    struct menu_config
    {
        // 같은 화면에 메뉴가 여럿일 때 구분하는 키다.
        std::u8string owner {};
        std::vector<menu_item_config> items {};
        // 없으면 항목이 눌리지 않는다.
        menu_message_factory select {};
    };

    // 항목 한 줄의 높이, 구분선 줄의 높이, 메뉴 둘레 여백이다 (논리 픽셀).
    inline constexpr float menu_item_height { 26.0f };
    inline constexpr float menu_separator_height { 9.0f };
    inline constexpr float menu_padding { 4.0f };

    // 컨텍스트 메뉴·드롭다운 목록의 본체다.
    // 통상 popup tree의 root로 두고, 정책의 menu_kinds가
    // {menu, menu_item}을 돌려주면 ↑/↓/Enter/Esc 탐색이 붙는다.
    // 어느 항목을 고를지는 메시지로만 내고 열림·닫힘은 앱 상태다.
    class menu_element final : public ui_element
    {
    public:
        explicit menu_element(menu_config config);

        // 이 설정의 전체 높이다 (논리 픽셀).
        // 담는 쪽이 popup 크기에 같은 값을 쓴다.
        [[nodiscard]] static float height_for(const menu_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        menu_config config_ {};
    };
} // namespace luil

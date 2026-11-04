#pragma once

#include "luil/ui/commands.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    class ui_tree;

    // 막대 한 줄의 높이와 항목 글 양옆의 여백이다 (논리 픽셀).
    inline constexpr float menu_bar_height { 28.0f };
    inline constexpr float menu_bar_item_padding { 10.0f };

    struct menu_bar_item_config
    {
        // 항목 owner이자 열린 메뉴의 이름이다 (`make_menu_bar_popup`이 메뉴 owner로 싣는다).
        std::u8string key {};
        std::u8string label {};
        // 접근 키다. 영문자나 숫자 하나이고 대소문자를 가리지 않는다. 0이면 없다.
        //  - 글자가 라벨에 있으면 그 글자에 밑줄을, 없으면 라벨 뒤에 "(F)"를 붙여 그린다.
        //  - 그 밖의 글자는 접근 키가 아니다 (없는 것으로 본다).
        char32_t access_key { 0 };
        bool enabled { true };
    };

    // 막대의 상태다. 앱이 들고 `set_state`의 절대 메시지로 바꾼다 (docs/menu-bar-design.md의 표).
    struct menu_bar_state
    {
        // 막대가 선 항목이다. 비면 막대가 쉰다.
        std::u8string current {};
        // current의 메뉴가 떠 있다.
        bool open { false };
        // 키보드로 들어왔다. 밑줄을 보이고 연 메뉴의 첫 항목을 강조한다.
        bool keyboard { false };
        [[nodiscard]] bool operator==(const menu_bar_state&) const = default;
    };

    struct menu_bar_config
    {
        // 막대 root의 owner다. 항목은 자기 키가 owner다.
        std::u8string owner {};
        std::vector<menu_bar_item_config> items {};
        // 앱 상태다.
        menu_bar_state state {};
        // 막대를 이 상태로 하라는 절대 메시지다. 없으면 막대가 눌리지 않고 키도 받지 않는다.
        std::function<input_action(const menu_bar_state&)> set_state {};
        bool enabled { true };
        // 거짓이면 F10은 앱 단축키로 흐르고 Alt 단독만 막대를 세운다.
        bool f10_activates { true };
        // 비면 기본 문구다 ("메뉴 막대").
        std::u8string access_name {};
    };

    // 창 위의 최상위 메뉴 줄이다 ("파일·편집·보기").
    // 항목을 누르거나 키보드로 들어오면 앱이 그 아래에 메뉴 popup을 띄운다 (`make_menu_bar_popup`). 열린 동안 ←/→와
    // hover가 이웃 메뉴로 옮긴다.
    //  - 상태는 앱의 것이다. 요소는 그 스냅숏을 그리고 바꿀 상태를 `set_state`로 낸다.
    //  - 항목(`menu_bar_item`)은 Tab 자리가 아니다. 선 자리는 초점이 아니라 상태가 그린다 — 텍스트 칸의 초점이
    //    막대에 들어갔다 나와도 그대로 남는다. 항목을 눌러도 초점을 가져가지 않는다.
    //  - 줄 바탕은 칠하지 않는다. 앱이 놓은 자리(보통 caption 아래 줄)의 바탕이 비친다.
    class menu_bar_element final : public ui_element
    {
    public:
        explicit menu_bar_element(menu_bar_config config);

        [[nodiscard]] const menu_bar_config& config() const noexcept;
        // 그 항목의 자리다 (`bounds()`와 같은 물리 픽셀). 없는 키면 nullopt다.
        [[nodiscard]] std::optional<rect_f> item_bounds(std::u8string_view key) const;
        // 막대가 쥔 키다 (controller가 부른다). 막대의 것이 아니면 nullopt다.
        //  - `menu_shown`은 지금 이 막대의 메뉴가 떠서 키를 먼저 받았는가다. 참이면 막대는 ←/→·Esc·Alt 단독·F10·
        //    Alt+접근 키만 받는다. 메뉴 안의 키는 메뉴의 것이다.
        //  - `set_state`가 없거나 막대가 꺼졌으면 언제나 nullopt다.
        [[nodiscard]] std::optional<std::vector<input_action>> handle_key(const key_pressed_event& event, bool menu_shown) const;
        // 항목 폭의 합이다 (논리 픽셀). `menu_element::width_for`와 같이 물리 글자 크기로 잰다.
        [[nodiscard]] static float width_for(const menu_bar_config& config, const text_measurer& measurer = default_text_measurer(), float font_size = 12.0f, float scale = 1.0f);

        // 항목을 왼쪽부터 세운다. 넘치면 오른쪽이 잘리고 키보드로는 닿는다.
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 항목 폭의 합과 `menu_bar_height`다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        menu_bar_config config_ {};
    };

    // tree에서 키를 받을 수 있는 막대다. 숨었거나 꺼졌거나 `set_state`가 없거나 가둠 밖이면 nullptr다.
    //  - 여럿이면 그리기 순서의 앞 것이다.
    [[nodiscard]] const menu_bar_element* active_menu_bar(const ui_tree& tree);

    // 막대 접근 키와 겹치는 Alt+글자 명령의 id다. 겹치면 막대가 이긴다 (Alt만 눌린 글자 키를 막대가 먼저 받는다).
    //  - 명령이 실제로 쓰는 단축키(`effective_shortcuts`)를 본다. Ctrl·Shift가 함께인 단축키는 겹치지 않는다.
    //  - 꺼진 막대 항목의 접근 키는 막대가 받지 않으므로 겹침이 아니다.
    [[nodiscard]] std::vector<std::u8string> menu_bar_access_conflicts(const menu_bar_config& config, std::span<const app_command> commands);
}

#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    struct dropdown_config
    {
        // 같은 화면에 드롭다운이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // 선택된 값의 라벨이다. 비어 있으면 placeholder를 흐리게 그린다.
        std::u8string text {};
        std::u8string placeholder {};
        // 목록이 떠 있는지는 앱 상태다 (popup이 frame에 실려 있는지와 같이 간다).
        // 참이면 테두리를 강조하고 화살표를 뒤집는다.
        bool open { false };
        // 누르면 낼 액션이다 (통상 목록 popup을 여닫자는 메시지).
        // 없으면 `set_open`이 대신 눌리고, 둘 다 없으면 눌리지 않는다.
        ui_action toggle {};
        // 목록을 **이 상태로** 하라는 절대 메시지다. 없으면 보조 기술이 열고
        // 닫을 수 없다 (UIA Expand·Collapse가 거절된다).
        //  - 토글로 흘리면 오래된 발행본을 본 같은 명령 둘이 열었다 도로 닫는다 —
        //    그룹의 `set_collapsed`와 같은 갈래다.
        std::function<input_action(bool open)> set_open {};
    };

    // 닫힌 칸의 높이다 (논리 픽셀).
    inline constexpr float dropdown_height { 28.0f };

    // 드롭다운(select)의 닫힌 칸이다.
    // 선택된 라벨과 펼침 화살표만 그리고, 열린 목록은 앱이 popup에
    // `menu_element`를 실어 만든다. 어느 값을 고를지는 그 메뉴의 메시지다.
    class dropdown_element final : public ui_element
    {
    public:
        explicit dropdown_element(dropdown_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 열고 닫기는 절대 메시지(`set_open`)로 답한다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        dropdown_config config_ {};
    };
} // namespace luil

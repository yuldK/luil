#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <string>

namespace luil {
    // 펼침의 절대 메시지다 (`list_config::set_expanded`와 같은 모양).
    using tree_expand_message_factory = std::function<input_action(const std::u8string& key, bool expanded)>;

    // 가지를 접었다 펴는 삼각형이다 (가상 목록의 행 앞, 표의 트리 열).
    //
    // `list_element`의 삼각형과 같은 규약이다.
    //  - 행 안의 보조 버튼이라 Tab의 자리가 아니다. 누르면 초점은 그 목록(`focus_target`)에 선다.
    //  - 클릭과 보조 기술의 펼치기·접기가 모두 절대 메시지(`set_expanded`)를 탄다. 토글로 흘리면 오래된 발행본을 본
    //    같은 명령 둘이 두 번 뒤집는다.
    //  - 펼침 상태 자체는 행이 보조 기술에 알린다 (ExpandCollapse). 이것은 누르는 자리일 뿐이다.
    class tree_expander_element final : public ui_element
    {
    public:
        tree_expander_element(ui_element_id id, std::u8string key, bool expanded, tree_expand_message_factory set_expanded, ui_element_id focus_target);

        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        std::u8string key_ {};
        bool expanded_ { false };
        tree_expand_message_factory set_expanded_ {};
    };
}

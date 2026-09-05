#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace luil {
    // 목록의 그룹 하나다.
    struct list_group
    {
        // 그룹을 가리키는 앱 정의 키다 (ui_element_id::owner 규약).
        std::u8string key {};
        std::u8string title {};
        // 머리행 아래 내용의 높이다 (논리 픽셀).
        // 측정 단계가 없으므로 담는 쪽이 알려 준다.
        float content_height { 0.0f };
        // 그룹의 행들이다 (nullptr이면 없는 것이다).
        std::unique_ptr<ui_element> content {};
    };

    // 머리행을 눌렀을 때 그룹 키를 담은 메시지다 (예: 그룹 접기).
    using list_group_message_factory = std::function<input_action(const std::u8string& key)>;

    struct grouped_list_config
    {
        // 같은 화면에 목록이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // 없으면 머리행이 눌리지 않는다.
        list_group_message_factory activate {};
    };

    // 머리행의 높이다 (논리 픽셀).
    inline constexpr float list_header_height { 24.0f };

    // 그룹으로 나뉜 목록의 내용 컨테이너다.
    // scroll view의 content로 두면 각 그룹의 머리행이 스크롤 중에도 창 위에
    // 붙어 있다가, 그룹 끝이 다가오면 다음 머리행에 밀려 올라간다.
    //  - 창 위 좌표는 받은 slot과 스크롤 값으로 계산한다 (slot.y + scroll·scale).
    class grouped_list_element final : public ui_element
    {
    public:
        grouped_list_element(grouped_list_config config, std::vector<list_group> groups);

        // 전체 내용 높이다 (논리 픽셀).
        // 담는 쪽이 scroll view의 content_height에 같은 값을 쓴다.
        [[nodiscard]] float content_height() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        struct entry
        {
            // 내용 원점에서 이 그룹이 시작하는 자리다 (논리 픽셀).
            float begin { 0.0f };
            float content_height { 0.0f };
            ui_element* header { nullptr };
            ui_element* content { nullptr };
        };

        grouped_list_config config_ {};
        std::vector<entry> entries_ {};
        float content_height_ { 0.0f };
    };
} // namespace luil

#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    struct group_config
    {
        // 같은 화면에 그룹이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        std::u8string title {};
        // 접힘은 앱 상태다.
        // element는 이 값을 받아 그리고 토글 메시지만 낸다.
        bool collapsed { false };
        // 제목 줄을 눌렀을 때의 토글이다. 없으면 `set_collapsed`가 대신 눌리고,
        // 둘 다 없으면 제목 줄이 눌리지 않는다.
        ui_action toggle {};
        // 접힘을 **이 상태로** 하라는 절대 메시지다. 없으면 보조 기술이 펼치고
        // 접을 수 없다 (UIA Expand·Collapse가 거절된다).
        //  - 토글로 흘리면 오래된 발행본을 본 같은 명령 둘이 두 번 뒤집는다 —
        //    절대 상태는 같은 메시지가 몇 번 겹쳐도 마지막 상태 그대로다
        //    (막대의 `change_to`와 같은 갈래다).
        //  - 이것만 있으면 제목 줄의 클릭도 여기서 나온다 (지금 상태의 반대를
        //    담아). `toggle`은 이미 그 뜻의 메시지를 가진 앱의 자리다.
        std::function<input_action(bool collapsed)> set_collapsed {};
        // 펼쳤을 때의 내용 높이다 (논리 픽셀).
        // 측정 단계가 없으므로 담는 쪽이 알려 준다.
        float content_height { 0.0f };
    };

    // 제목 줄의 높이다 (논리 픽셀).
    inline constexpr float group_header_height { 28.0f };

    // 제목 있는 접이식 섹션이다.
    // 테두리 안에 제목 줄과 내용을 세로로 두고, 접히면 제목 줄만 남는다.
    class group_element final : public ui_element
    {
    public:
        // 내용은 만들 때 받는다 (nullptr이면 없는 것이다).
        // 접혀 있으면 내용을 아예 tree에 넣지 않아 보이지도 눌리지도 않는다.
        explicit group_element(group_config config, std::unique_ptr<ui_element> content = nullptr);

        // 지금 상태의 전체 높이다 (논리 픽셀).
        // 담는 쪽이 배치(stack의 길이)에 같은 값을 쓴다.
        [[nodiscard]] static float height_for(const group_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 펼치기·접기는 절대 메시지(`set_collapsed`)로 답하고, 나머지는 머리행의
        // 몫이다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        group_config config_ {};
        ui_element* header_ { nullptr };
        ui_element* content_ { nullptr };
    };
} // namespace luil

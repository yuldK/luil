#pragma once

#include "luil/ui/ui_element.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    enum class toast_severity
    {
        info,
        success,
        warning,
        error,
    };

    // 토스트 한 장의 설정이다.
    // 목록 관리와 만료 판정은 앱의 몫이다.
    //  - 라이브러리는 `now`와 표시 시각으로 남은 시간을 계산해 끝날 무렵 흐려지게만 한다.
    //    사라진 토스트를 목록에서 지우는 것은 앱이다.
    struct toast_config
    {
        // 같은 화면의 토스트를 구분하는 앱 정의 키다 (ui_element_id::owner 규약).
        std::u8string id {};
        std::u8string text {};
        toast_severity severity { toast_severity::info };
        // 라벨과 액션이 모두 있어야 버튼을 만든다.
        //  - 하나만 있으면 "버튼은 있는데 아무 일도 하지 않는" 조합이라 만들지 않는다.
        std::u8string action_label {};
        ui_action action {};
        // 표시를 시작한 시각과 보여 줄 시간이다.
        // duration이 0이면 흐려지지 않고 계속 남는다 (앱이 직접 지울 때까지).
        std::chrono::steady_clock::time_point shown_at {};
        std::chrono::milliseconds duration { 0 };
    };

    // 토스트를 쌓을 모서리다.
    enum class toast_corner
    {
        top_left,
        top_right,
        bottom_left,
        bottom_right,
    };

    struct toast_stack_config
    {
        // 같은 화면에 stack이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        toast_corner corner { toast_corner::bottom_right };
        // 이 개수까지만 만든다.
        // 넘치는 설정은 element가 되지 않는다.
        std::size_t maximum_count { 3 };
        // 토스트 사이 간격과 slot 가장자리 여백, 토스트 한 장의 폭이다 (논리 픽셀).
        float spacing { 8.0f };
        float margin { 16.0f };
        float width { 320.0f };
    };

    // 토스트 한 장의 높이와 액션 버튼의 폭이다 (논리 픽셀).
    inline constexpr float toast_height { 40.0f };
    inline constexpr float toast_action_width { 64.0f };
    // 끝나기 전 흐려지는 데 쓰는 시간이다.
    inline constexpr std::chrono::milliseconds toast_fade { 300 };

    // 잠깐 나타났다 사라지는 알림 한 장이다.
    // 심각도는 팔레트의 역할색으로 그리고, 액션 버튼은 설정이 있을 때만 만든다.
    // duration이 있으면 next_update로 흐려지기 시작하는 시각을 예고해 그때까지는 다시 그리지 않는다.
    class toast_element final : public ui_element
    {
    public:
        explicit toast_element(toast_config config);

        // `now` 시점의 불투명도다 [0, 1].
        // duration이 0이면 항상 1이고, 남은 시간이 toast_fade 아래로 내려가면 비례해 줄어든다.
        // 시각만의 함수라 어느 frame에 그려도 같은 값이 나온다 (spinner와 같은 규칙).
        [[nodiscard]] float opacity_at(std::chrono::steady_clock::time_point now) const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        toast_config config_ {};
        ui_element* action_ { nullptr };
    };

    // 토스트 여러 장을 slot의 한 모서리에 쌓는 컨테이너다.
    // 목록과 순서는 앱이 정하고 여기는 자리만 잡는다.
    //  - 위 모서리는 첫 장이 가장 위에, 아래 모서리는 첫 장이 가장 아래(모서리 쪽)에 온다.
    class toast_stack_element final : public ui_element
    {
    public:
        toast_stack_element(toast_stack_config config, std::vector<toast_config> toasts);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        toast_stack_config config_ {};
        std::vector<toast_element*> toasts_ {};
    };
} // namespace luil

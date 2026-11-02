#pragma once

#include "luil/ui/severity.h"
#include "luil/ui/text_block.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <optional>
#include <string>

namespace luil {
    // 정보 막대의 치수다 (논리 픽셀).
    //  - 최소 높이는 한 줄 막대의 높이이고 `info_bar_config::control_size`가 더 크면 그것을 따라 오른다.
    inline constexpr float info_bar_minimum_height { 40.0f };
    inline constexpr float info_bar_close_size { 24.0f };
    // 동작 단추를 옆에 둘 때 글에 남아야 하는 폭이다. 모자라면 단추가 글 아래로 간다.
    inline constexpr float info_bar_side_action_minimum_text { 200.0f };

    struct info_bar_config
    {
        // 막대와 두 단추의 owner다.
        std::u8string owner {};
        ui_severity severity { ui_severity::info };
        // 한 줄이다. 길면 끝을 줄인다. 비면 줄이 서지 않는다.
        std::u8string title {};
        // 폭에서 줄을 바꾼다. 규칙은 label의 `wrap`과 같다.
        std::u8string message {};
        // 본문의 최대 줄 수다. 0이면 제한이 없다. 넘치면 마지막 줄의 끝을 줄인다.
        std::size_t max_lines { 0 };
        // 글과 액션이 모두 있어야 동작 단추를 만든다.
        //  - 하나만 있으면 눌러도 아무 일 없는 단추라 만들지 않는다.
        std::u8string action_label {};
        ui_action action {};
        // 있어야 닫기 단추를 만든다. 열려 있는지는 앱 상태다.
        ui_action close {};
        // 닫기 단추의 툴팁이자 접근 이름이다. 비면 기본 문구("닫기")다.
        std::u8string close_tooltip {};
        // 접근 이름 앞에 붙는 심각도 낱말이다. 비면 기본 문구(정보·성공·경고·오류)다.
        std::u8string severity_name {};
        // 닫기 단추의 한 변과 동작 단추의 높이다 (논리 픽셀). 0이면 `info_bar_close_size`와 `text_button_height`다.
        float control_size { 0.0f };
    };

    // 그 폭에서 막대가 차지할 크기다.
    struct info_bar_size
    {
        float height { 0.0f };  // 논리 픽셀
        bool action_below { false };  // 동작 단추가 글 아래 줄에 서는가
    };

    // 화면 안에 남아 있는 상태 알림이다. 토스트는 시간이 지나면 사라지지만 막대는 사용자가 닫거나 앱이 지울 때까지 남는다.
    // 심각도 글리프, 한 줄 제목, 줄이 바뀌는 본문, 동작 단추 하나, 닫기 단추를 담는다.
    //
    // root는 `alert` 몸통이다. 바탕·테·글리프·제목·본문은 root가 직접 그리므로 접근 tree에 글 노드가 따로 서지 않는다.
    // 자식은 최대 둘(`info_bar_action`·`info_bar_close`)이고 둘 다 손 모양 커서이며 Tab 자리다.
    //  - 몸은 심각도와 무관한 중립 바탕(`raised_background`)이다. 심각도는 1px 테와 글리프의 색이 말한다.
    //    고대비가 바탕을 창 배경으로 접어도 테가 막대를, 글리프가 뜻을 남긴다.
    //  - 닫기는 막대가 하지 않는다. `close` 액션이 앱 상태를 바꾸고, 앱은 다음 frame에 막대를 짓지 않는다.
    //  - 높이는 폭에 달려 있어(줄바꿈) 담는 쪽이 `stack_element::add_fit`이나 `measure`로 잰다. 모자라게 담으면 아래가 넘쳐
    //    그려지고 자르지 않는다.
    class info_bar_element final : public ui_element
    {
    public:
        explicit info_bar_element(info_bar_config config);

        // 그 폭(논리 픽셀)에서 막대가 차지할 높이와 단추 자리다. arrange·draw와 같은 규칙이다.
        //  - 글 폭은 폭에서 왼쪽 몫(여백·글리프 칸·간격)과 오른쪽 몫(닫기, 옆에 선 동작 단추)을 뺀 값이다.
        //  - 동작 단추를 옆에 두었을 때 글 폭이 `info_bar_side_action_minimum_text` 아래이면 단추가 글 아래 줄로 간다.
        [[nodiscard]] static info_bar_size measure(const info_bar_config& config, const measure_context& context, float width);

        // 글리프·제목·본문과 두 단추의 자리를 정한다. 폭은 `slot.width / scale`로 `measure`와 같은 식에 다시 넣는다.
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 접근 이름은 심각도 낱말, 제목, 본문을 공백으로 이은 글이다 (빈 것은 건너뛴다). 줄인 것과 상관없이 전체 글이다.
        [[nodiscard]] access_info accessibility() const override;
        // 폭이 정해지지 않았으면(가로 stack의 자식) 답하지 않는다. 폭은 받은 폭이고 높이는 `measure`의 것이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        // arrange가 정한 자리다 (논리 픽셀). 그리기가 줄을 다시 바꾸지 않는다.
        struct placement
        {
            // 첫 줄의 높이와 막대 위쪽 여백이다. 글리프·제목·단추가 이 첫 줄의 가운데에 맞춘다.
            float line_height { 0.0f };
            float top_padding { 0.0f };
            float text_width { 0.0f };
            float title_height { 0.0f };
            text_block_layout message {};
        };

        info_bar_config config_ {};
        ui_element* action_ { nullptr };
        ui_element* close_ { nullptr };
        placement placement_ {};
    };
}

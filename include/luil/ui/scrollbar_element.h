#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <vector>

namespace luil {
    // 보이는 막대의 폭과 thumb의 최소 높이다 (논리 픽셀).
    // hit 영역 폭·배치 여백은 tree를 조립하는 쪽의 몫이다.
    inline constexpr float scrollbar_visual_width { 8.0f };
    inline constexpr float scrollbar_minimum_thumb { 24.0f };

    // 화살표 한 번이 흘리는 양이다 (논리 픽셀).
    // 휠 한 눈금(`input_wheel_scroll_step`)과 같은 값이라 두 입력이 같은 걸음으로
    // 걷는다. Page는 창 높이 하나라 상수가 없다.
    inline constexpr float scrollbar_key_line_step { 48.0f };

    // 스크롤 막대의 설정이다.
    // 길이는 전부 논리 픽셀이고 배율은 arrange가 곱한다 (다른 element와 같은 규약).
    struct scrollbar_config
    {
        // 스크롤 위치를 바꾸자는 메시지를 만든다 (delta는 논리 픽셀).
        // 통상 앱 스크롤 intent를 app_message에 담아 돌려준다.
        std::function<input_action(float delta)> scroll {};
        // 스크롤 위치를 **이 자리로** 하라는 절대 메시지다 (offset은 논리 픽셀).
        // 없으면 보조 기술이 자리를 정할 수 없다 (UIA `SetValue`가 읽기 전용으로
        // 선다). 델타로 환산해 보내면 오래된 발행본 기준의 변화량이 겹쳐 쌓인다 —
        // 막대의 `change_to`와 같은 이유·같은 갈래다 (끌기와 키는 여전히 델타다).
        std::function<input_action(float offset)> scroll_to {};
        // 내용 전체 높이·보이는 창 높이·현재 스크롤 위치다.
        float content_height { 0.0f };
        float viewport_height { 0.0f };
        float scroll_offset { 0.0f };
    };

    // 세로 스크롤 막대다.
    // 표시뿐 아니라 클릭·끌기로 스크롤 위치를 바꾼다.
    // 좌표 변화량만 메시지로 바꾸므로(상대 이동) tree가 다시 빌드되어도 끌기가 이어진다.
    // 어느 대상을 스크롤할지는 id와 config.scroll이 정한다.
    class scrollbar_element final : public ui_element
    {
    public:
        using scroll_message_factory = std::function<input_action(float delta)>;

        scrollbar_element(ui_element_id id, scrollbar_config config);

        // 내용·창 높이와 스크롤 값을 배치 시점에 다시 알려 준다 (전부 논리 픽셀).
        // 창 높이는 slot이 정해져야 알 수 있어, 막대를 **안에 담는** 컨테이너가
        // 자기 `arrange`에서 채운다 — 밖에 두는 조립은 지금처럼 설정으로 준다.
        //  - `arrange` 전에 부르는 것이 계약이다. tree가 게시된 뒤에는 부르지 않는다
        //    (`tab_bar_element`가 넘침 버튼의 보임을 자기 arrange에서 정하는 것과
        //    같은 걸음이다).
        void set_metrics(float content_height, float viewport_height, float scroll_offset) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 막대와 같은 자리다 — 목표 자리를 절대 메시지(`scroll_to`)로 내보낸다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

        // arrange 뒤에 유효한 thumb의 세로 범위다 (물리 픽셀).
        // 배치 계산을 test가 고정하고, 겹쳐 그리는 앱이 읽는다.
        [[nodiscard]] float thumb_top() const noexcept;
        [[nodiscard]] float thumb_height() const noexcept;

        // 끌 수 있는 여유가 없으면(내용이 화면보다 짧거나 thumb가
        // track을 다 채우면) 클릭과 끌기가 아무 일도 하지 않는다.
        [[nodiscard]] bool draggable() const noexcept;

    private:
        // thumb를 pixels(물리)만큼 움직이는 스크롤 변화량이다 (논리 픽셀).
        [[nodiscard]] float scroll_delta_for(float pixels) const noexcept;

        scrollbar_config config_ {};
        float scale_ { 1.0f };
        // arrange가 정하는 track과 thumb의 세로 범위다 (물리 픽셀).
        float track_top_ { 0.0f };
        float track_height_ { 0.0f };
        float thumb_top_ { 0.0f };
        float thumb_height_ { 0.0f };
    };
} // namespace luil

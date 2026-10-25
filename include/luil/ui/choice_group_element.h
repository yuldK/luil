#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 선택지를 그리는 방식이다.
    enum class choice_style
    {
        // 세로 목록의 라디오 행이다.
        radio,
        // 가로로 늘어선 선택 묶음이다 (segmented control). 한 판(트랙) 위에서 고른 칸에만 밝은 판이 뜬다.
        toggle,
    };

    // 선택 묶음(`choice_style::toggle`)이 받은 폭을 쓰는 방식이다.
    enum class choice_width
    {
        // 칸을 가장 긴 글에 맞춘 같은 폭으로 놓고 왼쪽에 붙는다. 남는 폭은 비운다.
        //  - 짧은 글의 칸이 멀리 떨어지지 않아 고른 칸이 한눈에 들어온다.
        //  - 받은 폭이 모자라면 칸을 같은 비율로 줄인다.
        fit,
        // 받은 폭을 칸들이 같은 크기로 나눠 갖는다. 설정 화면에서 여러 줄의 묶음을 같은 폭으로 맞출 때 쓴다.
        fill,
    };

    struct choice_item
    {
        // 선택지를 가리키는 앱 정의 값이다 (ui_element_id::owner 규약).
        std::u8string value {};
        std::u8string label {};
    };

    // 고른 값을 담은 메시지다.
    using choice_message_factory = std::function<input_action(const std::u8string& value)>;

    struct choice_group_config
    {
        // 같은 화면에 그룹이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        choice_style style { choice_style::radio };
        std::vector<choice_item> items {};
        // 선택된 값이다 (앱 상태).
        // element는 이 값을 받아 그리고 선택 메시지만 낸다.
        std::u8string selected {};
        // 없으면 선택지가 눌리지 않는다.
        choice_message_factory select {};
        // 묶음의 이름이다 (「테마」·「유형」·「난이도」).
        //
        // **항목만으로는 반쪽이다.** 화면 읽기가 「어두움, 선택됨」까지는 말해도
        // 무엇을 고르는 중인지는 묶음이 말한다 — 화면에 세운 머리글과 같은 말을
        // 적는다. 비면 이름 없는 선택 묶음으로 선다.
        std::u8string name {};
        // 선택 묶음이 받은 폭을 쓰는 방식이다. 라디오에는 뜻이 없다.
        choice_width width { choice_width::fit };
    };

    // 라디오 행 하나의 높이, 선택 묶음 전체(트랙)의 높이다 (논리 픽셀).
    inline constexpr float choice_radio_row_height { 24.0f };
    inline constexpr float choice_toggle_height { 26.0f };
    // 선택 묶음을 여러 줄 쌓을 때 줄 사이에 두는 간격이다 (논리 픽셀).
    inline constexpr float choice_toggle_gap { 4.0f };
    // 트랙의 안쪽 여백, 칸 사이 간격, 칸 안의 좌우 여백이다 (논리 픽셀).
    inline constexpr float choice_track_padding { 2.0f };
    inline constexpr float choice_segment_gap { 2.0f };
    inline constexpr float choice_segment_padding { 14.0f };

    // 서로 배타적인 선택을 묶는다.
    // 어느 값이 선택인지는 앱 상태고, element는 값 목록을 그리고 선택 메시지만 낸다.
    class choice_group_element final : public ui_element
    {
    public:
        explicit choice_group_element(choice_group_config config);

        // 지금 설정의 전체 높이다 (논리 픽셀).
        // 담는 쪽이 배치(stack의 길이)에 같은 값을 쓴다.
        [[nodiscard]] static float height_for(const choice_group_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 두 스타일 다 선택 container(`radio_group`)로 선다 — 항목의
        // SelectionItem이 container를 물을 때 Selection 패턴이 있는 자리가 답이다.
        // 그리는 모양만 갈릴 뿐 하는 일이 같아, 선택 묶음의 칸도 라디오처럼 자기 선택을 말한다
        // (accessibility-design.md).
        [[nodiscard]] access_info accessibility() const override;
        // 선택 묶음은 `choice_width::fit`의 트랙 폭과 `height_for` 높이다. 라디오는 답하지 않는다.
        //  - 칸 글은 스타일의 본문 글자로 잰다 (`measure_context::style_metrics`). 그리기와 같은 글꼴이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        choice_group_config config_ {};
    };
} // namespace luil

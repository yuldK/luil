#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <string>
#include <vector>

namespace luil {
    // 선택지를 그리는 방식이다.
    enum class choice_style
    {
        // 세로 목록의 라디오 행이다.
        radio,
        // 가로로 늘어선 토글 버튼 묶음이다 (테마 선택 같은 것).
        toggle,
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
    };

    // 라디오 행 하나의 높이, 토글 묶음의 높이와 버튼 사이 간격이다 (논리 픽셀).
    inline constexpr float choice_radio_row_height { 24.0f };
    inline constexpr float choice_toggle_height { 26.0f };
    inline constexpr float choice_toggle_gap { 4.0f };

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
        // 그리는 모양만 갈릴 뿐 하는 일이 같아, 토글 항목도 자기 선택을 말한다
        // (`text_button_config::selected`, accessibility-design.md).
        [[nodiscard]] access_info accessibility() const override;

    private:
        choice_group_config config_ {};
    };
} // namespace luil

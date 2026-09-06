#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/text_input_state.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 캡션을 잡아 끈 만큼 dialog를 옮기는 메시지를 만든다.
    // 이동량은 논리 픽셀이고, 무엇을 얼마나 옮길지는 앱이 정한다.
    using dialog_move_message_factory = std::function<input_action(float dx, float dy)>;

    // in-app dialog의 캡션 바 설정이다.
    // 창의 caption(`caption_element`)과 달리 창 명령을 실행하지 않는다.
    //  - 무엇을 닫고 어디로 옮길지는 앱이 액션과 factory로 정한다.
    //
    // 없는 것은 두지 않는다.
    //  - 비어 있는 항목은 그 부분을 아예 만들지 않는다는 뜻이라 불가능한 조합이 생기지 않는다.
    struct dialog_caption_config
    {
        // 같은 화면에 dialog가 여럿일 때 구분하는 키다.
        // 하나뿐이면 비워 둔다.
        std::u8string owner {};
        // 캡션에 그릴 제목이다.
        // 비어 있으면 제목 없이 닫기 버튼만 둔다.
        std::u8string title {};
        // 닫기 버튼이 실행할 액션이다.
        // 비어 있으면 닫기 버튼을 두지 않는다.
        ui_action close {};
        std::u8string close_tooltip {};
        // 캡션을 잡아 끄는 동안의 이동량을 메시지로 바꾼다.
        // 비어 있으면 캡션을 잡아도 dialog가 움직이지 않는다.
        dialog_move_message_factory move {};
        // 캡션 높이와 제목 글자 크기다 (논리 픽셀).
        float height { 34.0f };
        float title_font_size { 12.0f };
    };

    // in-app dialog의 캡션 바다.
    // 제목과 닫기 버튼을 담고, 잡아 끌면 dialog를 옮기는 메시지를 낸다.
    // 아래쪽 구분선까지가 이 element의 몫이고 dialog 본문은 그 아래에 놓는다.
    class dialog_caption_element final : public ui_element
    {
    public:
        explicit dialog_caption_element(dialog_caption_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 이 설정으로 캡션이 차지할 높이다 (논리 픽셀 — 다른 height_for와 같은 단위).
        // 본문 배치를 캡션보다 먼저 계산해야 하는 호출자가 쓴다.
        [[nodiscard]] static float height_for(const dialog_caption_config& config) noexcept;

    private:
        dialog_caption_config config_ {};
        // 닫기 액션이 없으면 nullptr다.
        ui_element* close_ { nullptr };
        float scale_ { 1.0f };
    };

    // 글자 버튼이 그려지는 모양이다.
    // 계약은 하나다 — 액션을 받아 실행한다. 갈리는 것은 그리는 모양뿐이다.
    enum class text_button_visual
    {
        // 옅은 바탕이 있는 보통 버튼이다.
        normal,
        // 강조 바탕과 강조 글자다 (확인·주 동작).
        accent,
        // 바탕 없이 강조색 글자와 밑줄만이다.
        //  - 상자가 없어야 글 흐름 안에 놓을 수 있고, 그것이 link가 버튼과 갈리는
        //    전부다. 계약은 그대로라 별 element를 세우지 않는다.
        //  - 바탕이 없으니 hover를 **글자 색**이 말한다.
        //  - 커서는 여기서 걸지 않는다. 액션은 `set_action`으로 나중에 오므로
        //    만들 때는 누를 수 있는지 알 수 없고, 커서를 미리 걸면 아무 일도 하지
        //    않는 link가 hit의 임자가 된다. 담는 쪽이 `set_cursor(hand)`를 준다.
        link,
    };

    // 글자 버튼의 설정이다.
    //
    // 지정 초기화로 `text`와 `visual`을 이름 있는 필드에 전달한다.
    // 호출부에서 버튼의 표시 의도를 바로 확인할 수 있다.
    struct text_button_config
    {
        std::u8string text {};
        text_button_visual visual { text_button_visual::normal };
        // 0이 아니면 글자 왼쪽에 아이콘 칸을 잡는다 (codicon 글리프).
        // 0이면 칸을 **아예 만들지 않아** 글자 전용 배치가 그대로 남는다 —
        // 지금까지의 글자 버튼이 이 설정의 특수 경우가 된다.
        char32_t glyph { 0 };
        // 기본 Enter 동작의 대상임을 강조색 채움으로 표시한다.
        // 초점 테는 현재 키보드 초점을 나타내며 기본 버튼의 채움과 구별된다.
        // 비활성 버튼은 채우지 않는다.
        // 텍스트 칸 이외의 요소에 초점이 있으면 그 요소의 Enter 처리가 우선한다.
        // link는 채울 상자가 없으므로 기본 버튼 표시와 Enter 대상 지정에서 제외한다.
        // 입력 우선순위는 enter-default-design.md를 따른다.
        bool default_button { false };
    };

    // 글자 버튼이 자기 상자를 어떻게 칠하는가.
    enum class text_button_fill
    {
        // link다 — 상자가 없어 아무것도 칠하지 않는다.
        none,
        // 보통 버튼의 옅은 바탕이다.
        plain,
        // `accent` 모양의 옅은 강조 바탕이다.
        soft_accent,
        // 기본 버튼의 강조색 채움이다 ("Enter가 여기로 간다").
        solid_accent,
    };

    // 그리기 전에 버튼의 채움 종류를 고르는 순수 함수다.
    // 기본 버튼의 강조색 채움과 키보드 초점 테를 구별한다.
    [[nodiscard]] text_button_fill text_button_fill_for(const text_button_config& config, bool enabled) noexcept;

    // 아이콘 칸의 폭과 글자 사이의 간격이다 (논리 픽셀).
    inline constexpr float text_button_icon_size { 14.0f };
    inline constexpr float text_button_icon_gap { 6.0f };

    // in-app dialog(update overlay, switch dialog)가 공유하는 글자 버튼이다.
    // 아이콘 전용 button_element와 달리 텍스트를 중앙에 그린다.
    // hover·눌림·비활성 표시는 같은 규칙을 쓴다.
    class text_button_element final : public ui_element
    {
    public:
        text_button_element(ui_element_id id, text_button_config config);

        // 글자 왼쪽에서 아이콘이 가져가는 폭이다 (논리 픽셀).
        // 0이면 아이콘이 없다.
        //  - 버튼 폭은 담는 쪽이 정하므로(측정 단계가 없다) 그 쪽이 이 값을 더해
        //    잡을 수 있어야 한다. `check_element::indicator_width_for`와 같은 자리다.
        [[nodiscard]] static constexpr float icon_width_for(const text_button_config& config) noexcept
        {
            return config.glyph != 0 ? text_button_icon_size + text_button_icon_gap : 0.0f;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        text_button_config config_ {};
    };

    // 한 줄 텍스트 박스의 설정이다.
    //
    // 값(`text_input_view`)은 따로 받는다 — 그것은 설정이 아니라 **매 frame 바뀌는
    // 초안**이고, 설정에 섞으면 "바뀌는 것"과 "정하는 것"이 한 자리에 눕는다.
    struct text_input_config
    {
        // 비어 있고 초점이 없을 때만 그리는 흐린 안내 글이다.
        std::u8string placeholder {};
        // 0이 아니면 글 왼쪽에 아이콘 칸을 잡는다 (검색 칸의 돋보기).
        char32_t leading_glyph { 0 };
        // 비어 있으면 지우기 버튼을 만들지 않는다.
        // 무엇을 지울지는 앱이 정한다 — 라이브러리에 `text_edit_command::clear`가
        // 이미 있으므로 앱은 그 요청을 담아 돌려주면 된다.
        ui_action clear {};
        std::u8string clear_tooltip {};
        // 지우기 버튼의 글리프다. 0이면 닫기(×)다.
        // 앱이 그 자리를 다른 뜻으로 쓸 때(검색·화살표) 바꾼다 — 무엇을 하는지는
        // 위의 `clear`가 정하므로 글리프만 따라가면 된다.
        char32_t clear_glyph { 0 };
    };

    // 앞 글리프와 지우기 버튼의 크기, 그리고 글과의 간격이다 (논리 픽셀).
    inline constexpr float text_input_glyph_size { 14.0f };
    inline constexpr float text_input_clear_size { 18.0f };
    inline constexpr float text_input_icon_gap { 5.0f };

    // dialog가 공유하는 한 줄 텍스트 박스다 (환경설정의 제한 시간 칸, switch dialog의 질의 막대).
    // 상태를 담지 않는다.
    //  - 글·caret·선택은 logic의 초안이고 초점은 interaction snapshot이 가진다.
    //
    // 포인터 x를 글 안의 offset으로 옮기는 것(`offset_at`)과 가로 스크롤을
    // 이 element가 모두 계산해 그리기와 hit test가 어긋날 수 없다.
    //
    // **검색 칸은 밖에서 감쌀 수 없다.** 돋보기와 지우기 버튼을 가로 stack으로
    // 옆에 두면 글이 실제로 보이는 안쪽 폭이 두 벌이 된다 — 그리기·`offset_at`·
    // IME 질의가 모두 이 element의 `inner_width()` 하나를 쓰므로, 그 값이 밖의
    // 계산과 어긋나는 순간 caret이 글자와 어긋난다. 그래서 둘을 안으로 들인다.
    class text_input_element final : public ui_element
    {
    public:
        text_input_element(ui_element_id id, text_input_view value, text_input_config config);

        // 글의 왼쪽·오른쪽에서 아이콘이 가져가는 폭이다 (논리 픽셀).
        // 담는 쪽이 칸 폭을 정할 때 쓴다 — 측정 단계가 없으므로 그쪽이 더해 잡는다.
        //  - `constexpr`이 아닌 이유는 설정이 `std::function`을 담아 상수 평가
        //    안에서 지을 수 없어서다.
        [[nodiscard]] static float leading_width_for(const text_input_config& config) noexcept;
        [[nodiscard]] static float trailing_width_for(const text_input_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 초점을 받은 동안 caret이 다음에 뒤집히는 시각이다.
        // 위상이 초점 시각 기준이라 몇 번을 물어도 뒤집힘 시점은 같다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::size_t> offset_at(float x, const text_measurer& measurer) const override;
        [[nodiscard]] std::optional<text_input_snapshot> text_input() const override;
        [[nodiscard]] std::optional<rect_f> text_span_bounds(const text_span_query& query, const text_measurer& measurer) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 글자 크기(논리 11 × scale)다.
        // arrange가 정하고 그리기와 hit test가 같이 쓴다.
        [[nodiscard]] float font_size() const noexcept;
        // 글 왼쪽 시작 x와 글이 보이는 안쪽 폭이다.
        [[nodiscard]] float text_left() const noexcept;
        [[nodiscard]] float inner_width() const noexcept;
        // 지금 화면에 보이는 글과 caret이다.
        // IME 조합 중에는 조합 글이 섞인 쪽이다.
        [[nodiscard]] std::u8string_view displayed_text() const noexcept;
        [[nodiscard]] std::size_t displayed_caret() const noexcept;
        // caret이 보이도록 최소한만 민 가로 스크롤이다.
        // 글·caret·폭만으로 정해진다.
        // 두 인자 overload는 IME 질의(text_span_bounds)가 다른 문서로도
        // 같은 식을 쓰기 위한 것이다.
        [[nodiscard]] float scroll_for(const std::function<float(std::u8string_view)>& measure) const;
        [[nodiscard]] float scroll_for(const std::function<float(std::u8string_view)>& measure, std::u8string_view text, std::size_t caret) const;

        text_input_view value_ {};
        text_input_config config_ {};
        // 지우기 액션이 없으면 nullptr다.
        ui_element* clear_ { nullptr };
        float scale_ { 1.0f };
    };
} // namespace luil

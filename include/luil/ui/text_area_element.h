#pragma once

#include "luil/ui/dialog_elements.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 여러 줄 텍스트 칸의 설정이다.
    struct text_area_config
    {
        // 비어 있고 초점이 없을 때만 그리는 흐린 안내 글이다. 보조 기술이 읽는 칸의 이름이기도 하다.
        std::u8string placeholder {};
        // 지금 흘러간 양이다 (논리 픽셀, 앱 상태). 범위 밖 값은 `arrange`가 다듬는다.
        float scroll_offset { 0.0f };
        // 세로 스크롤을 이만큼 옮기자는 메시지다 (논리 픽셀) — 휠, 막대, caret 따라가기가 이것을 낸다.
        //  - 없으면 흘리지 않는 칸이다. 줄이 넘치면 아래가 잘린다.
        std::function<input_action(float delta)> scroll {};
        // 스크롤을 이 자리로 하라는 절대 메시지다 (보조 기술이 막대의 값을 정할 때).
        std::function<input_action(float offset)> scroll_to {};
    };

    // 글자 크기와 줄 높이, 안쪽 여백이다 (논리 픽셀).
    // 줄 높이가 고정이라 내용 높이는 화면의 줄 수(접은 줄 포함)에 줄 높이를 곱하고 위아래 여백을 더한 값이다.
    inline constexpr float text_area_font_size { 11.0f };
    inline constexpr float text_area_line_height { 18.0f };
    inline constexpr float text_area_padding_x { 7.0f };
    inline constexpr float text_area_padding_y { 5.0f };

    // 줄 바꿈·세로 스크롤·여러 줄 붙여넣기를 받는 텍스트 칸이다.
    //
    // 한 줄 칸(`text_input_element`)과 같은 편집 파이프라인을 탄다 — 글·caret·선택은 logic의 초안이고
    // (`text_input_view`), 편집 명령도 같은 어휘다. 다른 것은 키의 뜻이다 (docs/concepts/text-input.md).
    //  - Enter는 줄 바꿈을 넣는다. 기본 버튼으로 가지 않는다. Ctrl+Enter는 앱 단축키로 흐른다.
    //  - ↑·↓·Page Up·Page Down은 화면의 줄을 오가고, Home·End는 caret이 선 화면 줄의 처음과 끝이다.
    //    Ctrl+Home·End가 글의 처음과 끝이다.
    //  - Tab은 칸을 떠난다 (탭 문자를 넣지 않는다).
    //
    // **줄은 칸 폭에서 넘겨 접는다** (docs/text-area-wrap-design.md). 배치가 `arrange_context::measure_text`로
    // 줄을 접어 내용 높이를 정하고, 그리기·포인터 판정·IME 질의가 그 접은 줄을 그대로 쓴다.
    //  - 낱말 사이(공백 뒤)에서 접는다. 한 낱말이 칸보다 길면 글자 사이에서 접는다. 공백은 윗줄 끝에 매달린다.
    //  - 접는 폭은 흘리는 칸이면 막대 자리를 늘 뺀 폭이다. 막대가 서는 순간 글 전체가 다시 접히지 않는다.
    //  - 접은 자리의 offset은 아랫줄의 처음이다. 그래서 접은 줄의 End는 매달린 공백(또는 마지막 글자) 앞에 선다.
    //
    // 세로 스크롤은 앱 상태다 (`scroll_offset`·`scroll`). 휠로 caret에서 멀리 흘려도 되돌아가지 않는다.
    // caret이나 글이 바뀐 frame에서만 controller가 caret 줄을 창 안으로 들인다
    // (`interaction_controller::follow_text_caret`).
    class text_area_element final : public ui_element
    {
    public:
        text_area_element(ui_element_id id, text_input_view value, text_area_config config);

        [[nodiscard]] const text_area_config& config() const noexcept
        {
            return config_;
        }
        // 안쪽의 흘리는 창이다 (막대·휠·되살리기의 임자).
        [[nodiscard]] const scroll_area_element& area() const noexcept
        {
            return *area_;
        }

        // 화면의 줄 하나다 (UTF-8 byte offset). `end`는 줄 바꿈 앞이거나, 넘겨 접은 자리(다음 줄의 처음)다.
        struct line
        {
            std::size_t begin { 0 };
            std::size_t end { 0 };
            // 폭에서 넘겨 접은 줄이다. 거짓이면 줄 바꿈이나 글의 끝에서 끝난다.
            bool wrapped { false };

            [[nodiscard]] bool operator==(const line&) const noexcept = default;
        };
        // 배치가 접은 화면의 줄이다 (조합 글이 섞인 표시 문서 기준). `arrange` 뒤에 유효하다.
        [[nodiscard]] const std::vector<line>& lines() const noexcept
        {
            return lines_;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        // 한 줄 판정은 caret이 선 줄에서 한다 — 여러 줄 칸에서 y 없이 물으면 그 줄이 가장 그럴듯한 답이다.
        [[nodiscard]] std::optional<std::size_t> offset_at(float x, const text_measurer& measurer) const override;
        // 창 좌표 (x, y)를 글의 offset으로 옮긴다. 첫 줄 위는 글의 처음, 마지막 줄 아래는 글의 끝이다.
        [[nodiscard]] std::optional<std::size_t> offset_at_point(float x, float y, const text_measurer& measurer) const override;
        [[nodiscard]] std::optional<text_input_snapshot> text_input() const override;
        // 구간이 시작하는 줄에서의 자리다. 여러 줄에 걸치면 그 줄의 끝까지다 — IME 후보 창이 조합 글자 밑에 앉는다.
        [[nodiscard]] std::optional<rect_f> text_span_bounds(const text_span_query& query, const text_measurer& measurer) const override;
        // 배치가 접은 화면 줄(`lines()`)의 시작이다.
        [[nodiscard]] std::vector<std::size_t> text_line_starts() const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        [[nodiscard]] float scale() const noexcept;
        [[nodiscard]] float font_size() const noexcept;
        [[nodiscard]] std::u8string_view displayed_text() const noexcept;
        [[nodiscard]] std::size_t displayed_caret() const noexcept;
        // 그 글의 화면 줄이다. 배치가 접은 표시 문서면 그 결과를, 아니면(조합 중에 확정 글을 물을 때) 같은 폭으로 다시 접는다.
        [[nodiscard]] std::vector<line> lines_for(std::u8string_view text, const std::function<float(std::u8string_view)>& measure) const;
        // 글이 서는 왼쪽 끝과 첫 줄의 위다 (물리 픽셀).
        [[nodiscard]] float text_left() const noexcept;
        [[nodiscard]] float text_top() const noexcept;

        text_input_view value_ {};
        text_area_config config_ {};
        scroll_area_element* area_ { nullptr };
        // 줄을 그리는 몸통이다. 흘린 만큼 위로 밀려 배치된다.
        ui_element* body_ { nullptr };
        float scale_ { 1.0f };
        // 줄을 접는 폭이다 (물리 픽셀). 0 이하면 접지 않는다.
        float wrap_width_ { 0.0f };
        std::vector<line> lines_ {};
    };
} // namespace luil

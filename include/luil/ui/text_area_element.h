#pragma once

#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_document.h"
#include "luil/ui/text_layout.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luil {
    class text_area_element;
    class text_link_element;

    // 여러 줄 칸이 문단(줄 바꿈으로 나뉜 줄)마다 접은 자리를 frame 사이에 쥐는 곳이다.
    //
    // 칸은 tree와 함께 frame마다 새로 지어져 스스로는 아무것도 기억하지 못한다. 그래서 배치는 frame마다 글 전체를
    // 다시 접는다 — 수백 줄 메모에는 문제가 없지만 수십만 바이트 글에서는 키 하나에 한 frame을 넘긴다
    // (docs/text-area-wrap-design.md "비용"). 앱이 이것을 하나 쥐고 같은 칸의 설정(`text_area_config::wrap_cache`)에
    // frame마다 넘기면, 글이 같은 문단은 다시 재지 않는다.
    //  - 접는 폭·글자 크기·배율·글꼴(표본 글의 폭)이 바뀌면 통째로 비운다.
    //  - Shaping 구성에서는 같은 글이라도 굵기 구간이 다르면 따로 쥔다. 굵기는 advance를 바꾸므로, 글로만 찾으면
    //    코드·diff의 `}`나 빈 줄처럼 되풀이되는 문단이 서로의 자리를 덮어 frame마다 다시 shape된다.
    //  - Shaping 구성에서는 보이는 화면 줄의 배치도 쥔다. 접지 않은 칸의 줄은 문단 전체라 전역 배치 cache의
    //    한 항목 상한을 넘을 수 있고, 그러면 흘리거나 caret만 옮겨도 frame마다 다시 shape된다.
    //  - 이번 배치에서 쓰지 않은 문단과 화면 줄은 버린다. 지운 글이 쌓이지 않고, 쥔 양이 지금 글의 크기에 머문다.
    //  - 배치(logic thread)에서만 쓴다. 안의 잠금이 한 번에 배치 하나만 들인다.
    //  - 칸 하나에 하나다. 칸 둘이 나눠 쓰면 서로의 문단을 버린다.
    class text_area_wrap_cache
    {
    public:
        // 쥐고 있는 문단 수다 (test·진단용). 같은 글을 굵기 구간마다 따로 쥐면 그 수만큼 센다. 화면 줄은 세지 않는다.
        [[nodiscard]] std::size_t size() const;

    private:
        friend class text_area_element;

        // 문단 처음 기준으로 넘겨 접은 자리다 (오름차순, 문단의 끝은 넣지 않는다).
        //  - `styles`는 그 문단 기준의 굵기 구간이다. 측정기 길은 굵기를 재지 않아 늘 비어 있다.
        struct entry
        {
            std::vector<std::size_t> cuts {};
            std::uint64_t used { 0 };
            float width { 0.0f };
            std::vector<text_layout_style> styles {};
        };
        // 보이는 화면 줄 하나를 shape한 배치다. `styles`는 그 줄 기준의 굵기 구간이다.
        struct row_entry
        {
            std::vector<text_layout_style> styles {};
            text_layout layout {};
            std::uint64_t used { 0 };
        };
        struct key_hash
        {
            using is_transparent = void;
            [[nodiscard]] std::size_t operator()(std::u8string_view key) const noexcept;
        };

        mutable std::mutex mutex_ {};
        // 글로 찾고, 같은 글 안에서는 굵기 구간으로 고른다. 대개 글 하나에 하나라 찾는 값을 글 하나로 둔다.
        std::unordered_map<std::u8string, std::vector<entry>, key_hash, std::equal_to<>> entries_ {};
        std::unordered_map<std::u8string, std::vector<row_entry>, key_hash, std::equal_to<>> rows_ {};
        float width_ { 0.0f };
        float font_size_ { 0.0f };
        // 화면 줄의 배치는 줄 높이를 품는다. 글자 크기는 배율을 표로 옮길 수 있어(Android) 글자 크기만으로는 배율이
        // 바뀐 것을 놓칠 수 있다.
        float scale_ { 0.0f };
        float sample_width_ { 0.0f };
        bool wrap_ { true };
        bool native_ { false };
        text_layout_face face_ { text_layout_face::ui };
        std::uint64_t font_revision_ { 0 };
        std::uint64_t generation_ { 0 };
    };

    // 여러 줄 텍스트 칸의 설정이다.
    enum class text_face { ui, code };

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
        // 접은 자리를 frame 사이에 쥐는 곳이다 (`text_area_wrap_cache`). 큰 글을 받는 칸만 넘긴다.
        //  - 없으면 배치가 frame마다 글 전체를 접는다. 결과는 같다.
        std::shared_ptr<text_area_wrap_cache> wrap_cache {};
        // 읽기 전용에서도 선택·복사와 caret은 남는다.
        bool read_only { false };
        bool wrap { true };
        text_face face { text_face::ui };
        float font_size { 0.0f };
        float scroll_x { 0.0f };
        std::function<input_action(float delta)> scroll_horizontal {};
        std::function<input_action(float offset)> scroll_horizontal_to {};
        // 구간은 begin 순서이고 겹치지 않는다. 공유한 vector는 게시 뒤 고치지 않는다.
        //  - offset은 확정 글(`text_input_view::text`) 기준이다. 조합 중에는 칸이 조합 글만큼 옮겨 그린다.
        std::shared_ptr<const std::vector<text_span>> spans {};
        // `line` 오름차순이고 같은 줄이 둘 없다. 그리기가 줄마다 이분 탐색(`lower_bound`)으로 찾는다.
        std::shared_ptr<const std::vector<line_decoration>> lines {};
        std::vector<gutter_column> gutters {};
        std::vector<text_highlight> highlights {};
        // 글 안의 하이퍼링크다 (`text_link`, 글에서 찾으려면 `detect_text_links`). begin 순서이고 겹치지 않는다.
        // 공유한 vector는 게시 뒤 고치지 않는다.
        //  - offset은 `spans`처럼 확정 글 기준이다. 조합 중에는 칸이 조합 글만큼 옮겨 그린다.
        //  - 강조색과 밑줄로 그리고, 링크마다 보조 기술의 Hyperlink가 선다. 머무르면 주소를 tooltip으로 보인다.
        //  - 읽기 전용 칸은 클릭으로, 편집 칸은 Ctrl+클릭으로 연다. 편집 칸의 그냥 클릭은 caret을 놓는다 — 링크 안의 글도
        //    고칠 수 있어야 해서다. 터치는 탭이 caret이고, 길게 누르면 편집 메뉴에 "링크 열기·주소 복사"가 선다.
        std::shared_ptr<const std::vector<text_link>> links {};
        // 링크를 눌렀을 때 앱 메시지를 짓는다. 비어 있으면 링크는 표시만 한다.
        //  - 라이브러리는 주소를 스스로 열지 않는다. 앱이 받아 확인하고 연다 (`open_url`).
        std::function<input_action(std::u8string_view destination)> activate_link {};
        // 편집 칸의 링크 tooltip에서 주소 뒤에 붙는 안내다 (예: "Ctrl+클릭하여 열기"). 비어 있으면 주소만 보인다.
        std::u8string link_hint {};
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
    //    Android에서는 Home·End를 IME가 먼저 받아 글의 처음과 끝으로 간다 (docs/concepts/text-input.md).
    //  - Tab은 칸을 떠난다 (탭 문자를 넣지 않는다).
    //
    // **줄은 칸 폭에서 넘겨 접는다** (docs/text-area-wrap-design.md). 배치가 `arrange_context::measure_text`로
    // 줄을 접어 내용 높이를 정하고, 그리기·포인터 판정·IME 질의가 그 접은 줄을 그대로 쓴다.
    //  - 낱말 사이(공백 뒤)에서 접는다. 한 낱말이 칸보다 길면 글자 사이에서 접는다. 공백은 윗줄 끝에 매달린다.
    //  - 접는 폭은 흘리는 칸이면 막대 자리를 늘 뺀 폭이다. 막대가 서는 순간 글 전체가 다시 접히지 않는다.
    //  - 접은 자리의 offset은 윗줄의 끝이자 아랫줄의 처음이다. 기본은 아랫줄이고, 접은 줄의 End나 그 줄 끝을
    //    누르면 caret이 윗줄 끝에 붙는다 (`text_edit_state::caret_upstream`, `caret_at_point`).
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

        [[nodiscard]] float content_width() const noexcept;
        [[nodiscard]] std::optional<rect_f> line_rect(std::size_t logical_line) const;
        [[nodiscard]] std::optional<std::size_t> offset_at(zoom_point point) const;
        [[nodiscard]] std::vector<input_action> scroll_to_offset(std::size_t offset) const;
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        // 한 줄 판정은 caret이 선 줄에서 한다 — 여러 줄 칸에서 y 없이 물으면 그 줄이 가장 그럴듯한 답이다.
        [[nodiscard]] std::optional<std::size_t> offset_at(float x, const text_measurer& measurer) const override;
        // 창 좌표 (x, y)를 글의 offset으로 옮긴다. 첫 줄 위는 글의 처음, 마지막 줄 아래는 글의 끝이다.
        [[nodiscard]] std::optional<std::size_t> offset_at_point(float x, float y, const text_measurer& measurer) const override;
        // 누른 화면 줄이 접은 줄이고 답한 offset이 그 줄의 끝이면 윗줄 쪽(`upstream`)이다.
        [[nodiscard]] std::optional<text_caret_position> caret_at_point(float x, float y, const text_measurer& measurer) const override;
        [[nodiscard]] std::optional<text_input_snapshot> text_input() const override;
        // 구간이 시작하는 줄에서의 자리다. 여러 줄에 걸치면 그 줄의 끝까지다 — IME 후보 창이 조합 글자 밑에 앉는다.
        [[nodiscard]] std::optional<rect_f> text_span_bounds(const text_span_query& query, const text_measurer& measurer) const override;
        // 배치가 접은 화면 줄(`lines()`)의 시작이다.
        [[nodiscard]] std::vector<std::size_t> text_line_starts() const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        [[nodiscard]] float scale() const noexcept;
        [[nodiscard]] float font_size() const noexcept;
        [[nodiscard]] float line_height_logical() const noexcept;
        [[nodiscard]] const text_measurer& measure_with(const text_measurer& fallback) const;
        [[nodiscard]] std::u8string_view displayed_text() const noexcept;
        [[nodiscard]] std::size_t displayed_caret() const noexcept;
        // 그 글의 화면 줄이다. 배치가 접은 표시 문서면 그 결과를, 아니면(조합 중에 확정 글을 물을 때) 같은 폭으로 다시 접는다.
        // 다시 접은 줄은 `storage`에 담아 돌려준다 — 칸은 여러 thread가 읽으므로 안에 담을 수 없다.
        [[nodiscard]] const std::vector<line>& lines_for(std::u8string_view text, const std::function<float(std::u8string_view)>& measure, std::vector<line>& storage) const;
        // 표시 문서를 접는다. 설정에 캐시가 있으면 바뀐 문단만 잰다.
        [[nodiscard]] std::vector<line> wrap_displayed(const std::function<float(std::u8string_view)>& measure);
        // 글이 서는 왼쪽 끝과 첫 줄의 위다 (물리 픽셀).
        // `displayed`면 `begin`·`end`가 표시 문서(조합 글 포함)의 offset이라 확정 글 기준의 구간을 옮겨 읽는다.
        [[nodiscard]] std::vector<text_layout_style> metric_styles(std::size_t begin, std::size_t end, bool displayed) const;
        [[nodiscard]] text_layout_request layout_request(std::u8string_view text, std::size_t begin, bool wrap, bool displayed) const;
        [[nodiscard]] text_layout row_layout(std::u8string_view text, const line& row) const;
        [[nodiscard]] float text_left() const noexcept;
        [[nodiscard]] float text_top() const noexcept;
        // 창에 걸친 줄에서 링크 자식의 화면 조각을 놓는다. 배치의 끝에서 부른다.
        void place_links();
        // 그 순번의 링크 자식이다. 짓지 않은 링크(글 밖)면 nullptr다.
        [[nodiscard]] const text_link_element* link_element(std::size_t index) const noexcept;

        text_input_view value_ {};
        text_area_config config_ {};
        scroll_area_element* area_ { nullptr };
        // 줄을 그리는 몸통이다. 흘린 만큼 위로 밀려 배치된다.
        ui_element* body_ { nullptr };
        float scale_ { 1.0f };
        // 줄을 접는 폭이다 (물리 픽셀). 0 이하면 접지 않는다.
        float wrap_width_ { 0.0f };
        std::vector<line> lines_ {};
        scrollbar_element* horizontal_bar_ { nullptr };
        float content_width_ { 0.0f };
        float horizontal_offset_ { 0.0f };
        text_measurer code_measurer_ {};
        text_measurer layout_measurer_ {};
        std::vector<std::size_t> logical_lines_ {};
        std::vector<float> gutter_widths_ {};
        float gutter_width_ { 0.0f };
        bool native_layout_ { false };
        text_layout font_layout_ {};
        std::vector<std::pair<std::size_t, text_layout>> row_layouts_ {};
        // 링크의 순번과 그 자식이다 (순번 오름차순).
        std::vector<std::pair<std::size_t, text_link_element*>> link_elements_ {};

    };
}

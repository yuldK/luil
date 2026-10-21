#pragma once

#include "luil/ui/check_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/virtual_list_element.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace luil {
    // 열 머리글이 보이는 정렬 방향이다. 정렬을 **하는** 것은 앱이다 (`table_config::sort_request`).
    enum class sort_direction
    {
        none,
        ascending,
        descending,
    };

    // 칸 안에서 글이 붙는 쪽이다. 숫자·날짜 열은 끝(`end`)에 붙인다.
    enum class column_align
    {
        start,
        end,
    };

    // 표의 열 하나다 (table-design.md).
    // 폭·순서·정렬 표시는 전부 **앱 상태**다. 표는 그것을 그리고, 바꾸자는 메시지만 낸다.
    struct table_column
    {
        // 열을 가리키는 앱 정의 키다. 표 안에서 유일해야 한다 — 메시지와 자리표가 전부 이 값이다.
        std::u8string key {};
        // 머리글의 글이자 보조 기술이 읽는 열 이름이다.
        std::u8string header {};
        // 폭이다 (논리 픽셀). 끌어서 바꾸는 것은 `resize_column`이 알린다.
        float width { 120.0f };
        // 끌어도 이 아래로는 줄지 않는다 (논리 픽셀).
        float min_width { 40.0f };
        column_align align { column_align::start };
        sort_direction sort { sort_direction::none };
        // 거짓이면 오른쪽 경계를 끌 수 없다.
        bool resizable { true };
        // 거짓이면 머리글을 눌러도 정렬을 청하지 않는다.
        bool sortable { true };
    };

    // 칸에 보일 글이다. 칸을 따로 짓지 않은 열(`build_cell`이 nullptr을 돌려준 열)은 표가 이 글을 한 줄로 그린다.
    using table_cell_text_factory = std::function<std::u8string(const virtual_list_item& row, std::size_t index, const table_column& column)>;
    // 칸 하나를 앱이 짓는다 (상태 배지·아이콘·체크 같은 것).
    //  - nullptr이면 그 칸은 `cell_text`의 글 한 줄이다.
    //  - **자리표는 앱이 정하고 표 안에서 유일해야 한다.** 행 키와 열 키를 함께 담는 것이 보통이다.
    //  - tree를 짓는 동안 불린다 (`virtual_row_builder`와 같은 규약). 앱 상태를 바꾸지 않는 순수한 함수여야 한다.
    using table_cell_builder = std::function<std::unique_ptr<ui_element>(const virtual_list_item& row, std::size_t index, const table_column& column, bool selected)>;
    using table_column_message_factory = std::function<input_action(const std::u8string& column)>;
    // 열 폭 변화량이다 (논리 픽셀, + = 넓어진다). 최소 폭 아래로 가는 몫은 표가 미리 잘라 낸다.
    using table_resize_message_factory = std::function<input_action(const std::u8string& column, float delta)>;

    inline constexpr float table_header_height { 28.0f };
    // 칸 글의 좌우 여백이다 (논리 픽셀). 머리글과 본문이 같은 값을 써서 글의 끝이 맞는다.
    inline constexpr float table_cell_padding { 8.0f };
    // 열 경계를 잡는 띠의 폭이다 (논리 픽셀). 경계에 절반씩 걸친다.
    inline constexpr float table_grip_width { 8.0f };
    // ←·→ 한 번이 가로로 흘리는 거리다 (논리 픽셀).
    inline constexpr float table_key_scroll_step { 48.0f };

    struct table_config
    {
        // 같은 화면에 표가 여럿일 때 구분하는 키다. 본문 목록의 owner도 이 값이다.
        std::u8string owner {};
        // 보이는 열이다. 그리는 순서가 곧 이 순서다.
        std::vector<table_column> columns {};
        // 본문이다. 행 모델·선택·커서·세로 스크롤·키보드·행 메시지가 전부 가상 목록의 값 그대로다.
        //  - `owner`·`build_row`·`role`·`row_role`은 표가 정한다 (넣어도 덮어쓴다).
        //  - 키보드 범위 선택은 `move_with`로 선다 (keyboard-selection-design.md).
        virtual_list_config rows {};
        table_cell_text_factory cell_text {};
        table_cell_builder build_cell {};
        // 가로로 흘러간 양이다 (논리 픽셀, 앱 상태). 머리글과 본문이 이 값 하나로 함께 흐른다.
        //  - 범위 밖 값은 `arrange`가 다듬고 `scroll_x()`가 다듬은 값을 돌려준다.
        float scroll_x { 0.0f };
        // 가로로 이만큼 흘리자는 메시지다 (+ = 오른쪽 내용이 보인다). 가로 휠·←·→가 낸다.
        //  - 없으면 가로로 흐르지 않는다. 넘친 열은 잘린다.
        std::function<input_action(float delta)> scroll_horizontal {};
        // 머리글을 눌렀다. 정렬 순서를 정하고 행을 다시 늘어놓는 것은 앱이다 — 표는 `table_column::sort`를 그릴 뿐이다.
        table_column_message_factory sort_request {};
        // 열 경계를 끌었다. 끄는 동안 이어서 오고, 끝나면 `resize_column_end`가 한 번 온다.
        table_resize_message_factory resize_column {};
        table_column_message_factory resize_column_end {};
        // 계층을 그리는 열이다 (열 키). 비어 있으면 표는 평평하다.
        //  - 깊이와 펼침은 행 모델의 값(`virtual_list_item::depth`·`expansion`)이고, 펼치기는 `rows.set_expanded`다.
        //  - 그 열의 칸 앞에 깊이만큼 들여쓰고 가지에는 삼각형을 세운다. ←·→는 본문 목록이 계층으로 쓰고
        //    (접기·부모로·펴기·첫 자식으로), 갈 곳이 없을 때만 표의 가로 스크롤이 된다.
        std::u8string tree_column {};
        // 체크 칸을 둘 열이다 (열 키). 비어 있으면 체크 칸이 없다.
        //  - 칸 앞에 체크박스가 서고, 상태는 `check_state_of`가 답한다. 폴더처럼 일부만 고른 행은 `mixed`다
        //    (`aggregate_check`). 트리 열과 같은 열이면 삼각형 뒤에 선다.
        //  - 누르거나, 본문이 Space를 쓰지 않을 때(`rows.activate`가 비었을 때) Space가 `toggle_check(행 키)`를 낸다.
        //    다음 상태는 앱이 정한다.
        std::u8string check_column {};
        std::function<check_state(const virtual_list_item& row, std::size_t index)> check_state_of {};
        std::function<input_action(const std::u8string& row)> toggle_check {};
    };

    // 열 하나가 차지하는 가로 자리다 (논리 픽셀, 가로 스크롤 전).
    struct table_column_span
    {
        float begin { 0.0f };
        float width { 0.0f };

        [[nodiscard]] bool operator==(const table_column_span&) const = default;
    };

    // 열들의 가로 자리다. 폭이 음수인 열은 0으로 본다.
    [[nodiscard]] std::vector<table_column_span> table_column_spans(std::span<const table_column> columns);
    // 열 폭의 합이다 (가로로 흐를 내용의 폭).
    [[nodiscard]] float table_content_width(std::span<const table_column> columns) noexcept;
    // 끈 변화량에서 최소 폭 아래로 가는 몫을 잘라 낸 값이다.
    [[nodiscard]] float table_resize_delta(const table_column& column, float delta) noexcept;
    // 가로 스크롤을 범위 안으로 다듬는다. 내용이 창보다 좁으면 0이다.
    [[nodiscard]] float table_clamp_scroll(float scroll_x, float content_width, float viewport_width) noexcept;

    // 열 머리글이 있고 행을 고를 수 있는 표다 (Qt `QTreeView`+`QHeaderView`, WPF `DataGrid`의 1단계).
    //
    // **본문은 가상 목록 그대로다.** 행의 가상화·선택·커서·키보드·컨텍스트 메뉴·끌어 옮기기가 `rows`의 값으로
    // 그대로 선다. 표가 더하는 것은 열이다.
    //  - 머리글과 본문의 열 경계가 같은 계산(`table_column_spans`)에서 나와 어느 배율에서나 맞는다.
    //  - 가로 스크롤은 값 하나(`scroll_x`)라 머리글과 본문이 함께 흐른다. 가로 휠과 ←·→가 그것을 청한다.
    //  - 열 경계를 끌면 폭 변화량을, 머리글을 누르면 정렬 요청을 낸다. 정렬과 폭 저장은 앱의 몫이다.
    //  - 보조 기술은 본문을 표(DataGrid)로, 행을 DataItem으로, 머리글 칸을 HeaderItem으로 읽는다.
    //
    // 아직 없는 것은 table-design.md의 "범위"에 있다 (셀 커서와 편집, 열 재배치·숨김, 트리 열, 고정 열).
    class table_element final : public ui_element
    {
    public:
        explicit table_element(table_config config);

        // `arrange`가 범위 안으로 다듬은 가로 스크롤이다.
        [[nodiscard]] float scroll_x() const noexcept;
        // 열 폭의 합이다 (논리 픽셀).
        [[nodiscard]] float content_width() const noexcept;
        // 본문 목록이다.
        [[nodiscard]] const virtual_list_element& body() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 행 칸과 머리글이 배치에서 읽는다 (같은 tree 안의 부품이라 표가 먼저 배치된다).
        [[nodiscard]] const table_config& config() const noexcept;
        [[nodiscard]] const std::vector<table_column_span>& spans() const noexcept;
        // 행 모델에서 옮겨 둔 펼침 메시지다 (`rows.set_expanded`). 트리 열의 삼각형이 쓴다.
        [[nodiscard]] const std::function<input_action(const std::u8string& key, bool expanded)>& expand_message() const noexcept;

    private:
        table_config config_ {};
        std::function<input_action(const std::u8string& key, bool expanded)> expand_ {};
        std::vector<table_column_span> spans_ {};
        ui_element* header_ { nullptr };
        virtual_list_element* body_ { nullptr };
        float content_width_ { 0.0f };
        float scroll_x_ { 0.0f };
        float viewport_width_ { 0.0f };
    };
} // namespace luil

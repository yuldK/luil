#pragma once

#include "luil/ui/scroll_area_element.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 모델 안의 항목 하나다.
    //
    // **행을 짓지 않고도 아는 것만 담는다.** 화면에 걸치지 않는 항목은 element가
    // 되지 않으므로, 키보드가 알아야 하는 것(순서·이름·고를 수 있는가)은 전부
    // 여기 있어야 한다. 그리는 데 필요한 나머지는 앱이 자기 모델에서 꺼내
    // `build_row`에서 쓴다 — 라이브러리가 행의 내용을 알 필요가 없다.
    struct virtual_list_item
    {
        // 항목을 가리키는 앱 정의 키다 (`ui_element_id::owner` 규약).
        // 모델 안에서 유일해야 한다 — 선택·커서·행 자리표가 전부 이 값이다.
        std::u8string key {};
        // 글자 탐색이 읽는 이름이다.
        // 비어 있으면 그 항목은 글자로 찾을 수 없다 (`search_label`과 같은 규약).
        std::u8string label {};
        // 이 행의 높이다 (논리 픽셀).
        // 0이면 `virtual_list_config::row_height`다 — "없음은 언제나 0으로
        // 말한다"는 `stack_item`의 규칙 그대로다.
        //  - 값이 섞여 있어도 된다. 창에 걸치는 범위는 앞에서부터 더한 합으로
        //    찾으므로 항목 수에 비례한 일이 한 번 생기는데, 그것은 **더하기뿐**이라
        //    행을 짓는 것과 견줄 비용이 아니다.
        float height { 0.0f };
        // 거짓이면 흐리게 그리고 고를 수 없다 (키보드도 건너뛴다).
        bool enabled { true };
    };

    // 행 하나를 앱이 짓는다.
    //
    // 돌려주는 것은 **행의 내용**이다. 행 자신(자리표·선택 표시·누름 액션·글자
    // 탐색 이름)은 목록이 만들어 그 안에 담는다 — 그러지 않으면 앱마다 선택과
    // 키보드를 다시 짜고 그중 하나를 반드시 틀린다.
    //  - 비어 있으면(nullptr) 라이브러리가 기본 행을 짓는다 (`label` 한 줄).
    //  - nullptr을 돌려줘도 된다. 그 행은 내용 없이 배경과 선택 표시만 선다.
    //  - **tree를 짓는 동안 불린다.** 게시된 뒤에는 불리지 않으므로 앱 상태를
    //    바꾸지 않는 순수한 함수여야 한다 (게시된 tree는 여러 thread가 읽는다).
    using virtual_row_builder = std::function<std::unique_ptr<ui_element>(const virtual_list_item& item, std::size_t index, bool selected)>;

    // 항목 키 하나를 담은 메시지다 (선택·커서 이동).
    using virtual_list_message_factory = std::function<input_action(const std::u8string& key)>;

    struct virtual_list_config
    {
        // 같은 화면에 목록이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // **모델 전체**다. 화면에 걸치지 않는 항목도 전부 담는다.
        //  - 담기는 것은 값이지 element가 아니다. 십만 줄이 와도 여기까지는
        //    복사 한 번이고, tree에 서는 것은 창에 걸치는 몇 줄뿐이다.
        //  - 그 복사는 **frame마다 한 번**이다. 십만 줄을 frame마다 새로 지어 넣지
        //    말고, 앱이 든 모델을 그대로 옮겨 담는다 (`items = model_`). 모델이
        //    바뀌지 않는 frame에서는 그것이 memcpy 한 번이고, `std::u8string`의
        //    짧은 이름은 그 안에서 함께 옮겨진다.
        std::vector<virtual_list_item> items {};
        // 고른 항목의 키다 (앱 상태).
        std::u8string selected {};
        // 키보드 커서가 선 항목의 키다 (앱 상태).
        //
        // **선택과 갈라 둔다.** 화살표로 훑다가 Enter로 고르는 것과, 화살표가
        // 곧 선택인 것은 앱마다 다르다 — `move` 메시지를 받은 앱이 커서만 옮길지
        // 선택까지 옮길지 정하면 둘 다 표현된다.
        //  - 비어 있으면 `selected`가 커서다. 그것도 비어 있으면 첫 항목이다.
        //  - 커서가 선 행은 창에 걸치지 않아도 **반드시 짓는다.** 그러지 않으면
        //    초점 테를 그릴 자리가 없고, 커스텀 행 안의 컨트롤이 사라진다.
        std::u8string cursor {};
        // 줄 하나의 기본 높이다 (논리 픽셀).
        // `virtual_list_item::height`가 0인 항목이 이 값을 쓴다.
        float row_height { 24.0f };
        // 지금 흘러간 양이다 (논리 픽셀, 앱 상태).
        // 범위 밖 값은 `arrange`가 다듬고 `metrics()`가 다듬은 값을 되돌려 준다.
        float scroll_offset { 0.0f };
        // 창 위아래로 더 짓는 행 수다.
        // 0이면 걸치는 것만 짓는다 — 휠을 굴리는 동안 행이 나타나는 것이 보인다.
        int overscan { 2 };
        // 행 내용을 짓는다. 비어 있으면 기본 행(`label` 한 줄)이다.
        virtual_row_builder build_row {};
        // 행을 눌렀을 때다.
        //  - 없으면 행이 눌리지 않는다. 목록은 그래도 커서를 옮길 수 있다 —
        //    `list_element`와 갈리는 자리다. 저쪽은 행이 Tab의 자리라 누를 수
        //    없으면 키보드도 서지 않지만, 이쪽은 **목록 자신이 자리**다.
        virtual_list_message_factory select {};
        // 키보드가 커서를 이 항목으로 옮기자는 메시지다.
        //
        // **없으면 키보드 탐색을 켜지 않는다** — 커서는 앱 상태라 라이브러리가
        // 고칠 수 없고, 옮길 길이 없는 화살표는 아무 일도 하지 않는 키가 된다
        // ("없는 것은 두지 않는다").
        virtual_list_message_factory move {};
        // 커서가 선 항목을 **실행**하자는 메시지다 (Space·Enter).
        //
        // 커서와 선택을 갈라 둔 값이 여기서 나온다. 화살표로 훑고 Enter로 고르는
        // 모델은 이 factory가 있어야 실제로 표현된다 — 없으면 키보드에서 고를
        // 길이 아예 없어, 앱은 `move`를 받을 때 선택까지 함께 옮기는 수밖에 없고
        // 그러면 커서와 선택을 가른 뜻이 사라진다.
        //  - 통상 `select`와 같은 메시지를 낸다. 다르게 두는 자리도 있다 —
        //    Enter가 "열기"이고 클릭이 "고르기"인 목록이 그렇다.
        //  - **키보드만의 문이다.** 포인터는 행을 직접 누르므로 이 자리를 지나지
        //    않는다 — 목록의 빈 자리를 눌러 커서 행이 실행되는 일은 없다
        //    (`hit_test`가 목록 자신을 답하지 않는다).
        virtual_list_message_factory activate {};
        // 스크롤 위치를 옮기자는 메시지다 (delta는 논리 픽셀).
        // 휠·막대·키보드 되살리기가 전부 이 하나를 쓴다 (`scroll_area_config`와 같다).
        std::function<input_action(float delta)> scroll {};
        // 스크롤 위치를 **이 자리로** 하라는 절대 메시지다 (offset은 논리 픽셀).
        std::function<input_action(float offset)> scroll_to {};
        scrollbar_visibility bar { scrollbar_visibility::automatic };
        // 창의 위·아래 가장자리 표시다 (구분선과 흘린 쪽의 그림자).
        // 안의 영역에 그대로 이어진다 — `scroll_area_config::edges`와 같은 설정·같은 그림이다.
        scroll_edges edges {};
    };

    // 모델 안에서 키가 커서를 옮길 자리다.
    //
    // 판정이 element 안에 숨으면 test가 닿지 못해 여기로 뗀다
    // (`decoded_image_size`·`animation_frame_at`이 이미 선 자리다). 받는 것이
    // 항목 목록과 값 몇뿐이라 test에 창도 tree도 필요 없다.
    //
    // 규칙은 이렇다.
    //  - **비활성 항목은 건너뛴다.** 건너뛸 것뿐이면 제자리다.
    //  - 화살표는 끝에서 **멈춘다**. 묶음의 화살표가 도는 것과 갈리는데, 도는
    //    것은 항목이 한 화면에 다 보일 때의 어휘다 — 십만 줄에서 ↓ 한 번에 맨
    //    위로 돌아가면 그것은 이동이 아니라 사고다.
    //  - Home/End는 처음·끝의 **활성 항목**이다.
    //  - Page는 `page_rows` 칸이다. 0 이하면 한 칸으로 본다 — 창이 한 줄보다
    //    낮아도 키가 멈추지 않는다.
    //  - `from`이 모델에 없으면 첫 활성 항목에서 시작한 것으로 본다.
    // 옮길 자리가 없으면(모델이 비었거나 전부 비활성) 빈 값이다.
    [[nodiscard]] std::optional<std::size_t> virtual_list_step_target(std::span<const virtual_list_item> items, std::optional<std::size_t> from, value_step step, int page_rows) noexcept;

    // 글자 질의로 찾을 항목이다 (모델 전체에서).
    //
    // 규칙은 묶음의 글자 탐색과 같다.
    //  - `label`이 질의로 **시작하는** 첫 항목이고, 비교는 UTF-8 byte 그대로다.
    //  - `first`가 참이면 `from`의 **다음**부터 찾고 끝에서 처음으로 돈다 —
    //    같은 글자를 거듭 치면 그 글자로 시작하는 항목들을 돈다. 거짓이면
    //    `from`부터 찾는다 (글을 더 적은 것이지 다음으로 가자는 뜻이 아니다).
    //  - 비활성 항목은 맞지 않는다.
    // 맞는 것이 없으면 빈 값이다.
    [[nodiscard]] std::optional<std::size_t> virtual_list_search_target(std::span<const virtual_list_item> items, std::optional<std::size_t> from, std::u8string_view query, bool first) noexcept;

    // 항목들이 차지하는 세로 범위다 (논리 픽셀, 모델 원점 기준).
    // `height`가 0인 항목은 `row_height`를 쓴다.
    struct virtual_list_span
    {
        float begin { 0.0f };
        float length { 0.0f };

        [[nodiscard]] bool operator==(const virtual_list_span&) const noexcept = default;
    };

    // `index`번 행이 서는 자리다. 범위 밖이면 빈 값이다 (`{0, 0}`).
    [[nodiscard]] virtual_list_span virtual_list_row_span(std::span<const virtual_list_item> items, float row_height, std::size_t index) noexcept;

    // 모델 전체의 높이다 (논리 픽셀).
    [[nodiscard]] float virtual_list_content_height(std::span<const virtual_list_item> items, float row_height) noexcept;

    // 창에 걸치는 행의 반열린 구간이다 (`[begin, end)`).
    //
    // `overscan`은 위아래로 더 짓는 행 수이고 음수는 0으로 본다.
    // 창이 비었거나 항목이 없으면 빈 구간이다.
    struct virtual_list_range
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };

        [[nodiscard]] bool empty() const noexcept
        {
            return begin >= end;
        }

        [[nodiscard]] bool operator==(const virtual_list_range&) const noexcept = default;
    };

    [[nodiscard]] virtual_list_range virtual_list_visible_range(std::span<const virtual_list_item> items, float row_height, float scroll_offset, float viewport_height, int overscan) noexcept;

    // 창에 걸치는 행만 짓는 목록이다.
    //
    // `list_element`는 창 밖의 행까지 전부 tree에 담는다. 묶음의 키보드(↑↓·
    // Home/End·글자 탐색)와 접근성 순회가 tree를 읽어 답하기 때문이고, 그래서
    // 그 목록의 계약은 "모든 행이 tree에 남는다"다 (list-view-design.md). 십만
    // 줄에서는 그 계약이 곧 십만 개의 element다.
    //
    // 이 목록은 **모델을 tree 밖에 두고** 키보드를 그 모델 위에서 돈다.
    //  - **자리는 목록 자신 하나다.** 행이 아니라 목록이 Tab의 자리이고, 커서는
    //    앱 상태다. 옮길 자리가 tree에 없을 수 있으므로 초점이 행에 설 수 없고,
    //    설 수 없는 것을 억지로 세우면 초점을 쥔 element가 다음 frame에 사라진다
    //    (`update_focus`가 그것을 거둔다). 이것이 `list_element`와 갈리는 뿌리다.
    //  - 화살표·Page·Home/End는 `key_step_target`이 받는다 — 묶음보다 앞에 선
    //    경로라 감싼 묶음이 있어도 목록이 먼저 가진다. 묶음이 갖지 못하는
    //    PageUp/PageDown이 여기서는 저절로 온다.
    //  - 글자 탐색은 `key_search_target`이 받는다. 질의를 잇고 끊는 규칙은
    //    controller가 쥐고, 모델에서 무엇이 맞는지는 목록이 답한다.
    //  - 키가 내는 것은 **커서 메시지와 스크롤 메시지 둘**이다. 옮긴 자리가 창
    //    밖이면 함께 흘려 보내므로, 앱이 `on_focus_moved`에 되살리기를 적지
    //    않아도 커서가 화면에서 사라지지 않는다.
    //
    // 접근성은 목록 하나와 **지금 지어진 행들**로 읽힌다. 전체 항목 수는 목록이
    // 답하므로 "몇 개 중 몇 번째"는 옳게 들리지만, 보조 기술의 형제 순회는 창에
    // 걸치는 범위 안이다 — 그것이 가상화의 값과 맞바꾼 것이다.
    class virtual_list_element final : public ui_element
    {
    public:
        explicit virtual_list_element(virtual_list_config config);

        // 모델 전체가 차지하는 높이다 (논리 픽셀).
        [[nodiscard]] float content_height() const noexcept;
        // `arrange` 뒤에 유효한 이 목록의 스크롤 치수다.
        [[nodiscard]] const scroll_metrics& metrics() const noexcept;
        // `arrange`가 실제로 지은 행의 구간이다 (`[begin, end)`).
        // test와 진단이 읽는다 — 커서 행이 창 밖이면 그 행도 따로 지어진다.
        [[nodiscard]] const virtual_list_range& realized() const noexcept;
        // 커서가 선 항목의 색인이다. 모델이 비었으면 빈 값이다.
        [[nodiscard]] const std::optional<std::size_t>& cursor() const noexcept;

        // 목록 자신은 hit의 답이 되지 않는다 — 답은 언제나 행이거나 없음이다.
        //
        // 목록은 Space·Enter를 받으려고 `left_click` 액션을 든다 (`activate`).
        // 그 액션은 **키보드만의 것**인데, 기본 hit test는 액션이 있으면 자기
        // bounds를 답하므로 그대로 두면 행 아래 빈 자리를 누른 것이 커서 행의
        // 실행이 된다 — 누른 자리와 실행된 자리가 다른, 설명할 수 없는 클릭이다.
        [[nodiscard]] const ui_element* hit_test(float x, float y) const override;
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 커서를 `index`로 옮기고 그 행을 창 안으로 들이는 메시지들이다.
        // 옮길 자리가 없거나 `move`가 없으면 빈 목록이다.
        [[nodiscard]] std::vector<input_action> move_cursor(std::size_t index) const;
        // 한 번의 Page가 건너뛰는 행 수다.
        [[nodiscard]] int page_rows(value_step step) const noexcept;
        // 창에 걸치는 행과 커서 행을 실제로 짓는다 (`arrange`가 한 번만 부른다).
        void build_rows();

        virtual_list_config config_ {};
        scroll_area_element* area_ { nullptr };
        ui_element* lane_ { nullptr };
        float content_height_ { 0.0f };
        virtual_list_range realized_ {};
        // 행을 이미 지었는가.
        //
        // 자식은 tree 하나에 한 벌이다 — 두 번 지으면 같은 자리표가 둘 선다
        // (`ui_tree::duplicate_ids()`). 그래서 `arrange`가 여러 번 불려도 짓는 것은
        // 높이가 있는 **첫 번째**뿐이다.
        //  - **한 tree는 한 번만 배치된다** (`make_arranged_tree`). 그것이 이 목록이
        //    기대는 계약이고, 그러지 않는 컨테이너 안에 놓으면 행은 첫 배치의
        //    창 높이로 굳는데 `page_rows`와 되살리기는 마지막 배치의 높이를 본다.
        //    담는 쪽이 한 frame에 두 번 배치하지 않는 것이 조건이다 —
        //    `scroll_area_element`가 창을 한 번만 배치하는 이유가 이것이다.
        bool realized_built_ { false };
        std::optional<std::size_t> cursor_ {};
        // `arrange`가 정하는 창 높이다 (논리 픽셀). Page 걸음과 되살리기가 읽는다.
        float viewport_height_ { 0.0f };
    };
} // namespace luil

#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <memory>
#include <vector>

namespace luil {
    enum class stack_direction
    {
        // 위에서 아래로 쌓는다.
        column,
        // 왼쪽에서 오른쪽으로 쌓는다.
        row,
    };

    // 교차축(쌓는 방향과 직각)에서 자식을 어디에 둘지다.
    // 교차축 길이를 정하지 않은 자식은 남는 폭을 다 채우므로 정렬이 뜻을 갖지 않는다.
    enum class stack_alignment
    {
        start,
        center,
        end,
        stretch,
    };

    // 주축(쌓는 방향)에서 **남는 자리**를 어디에 둘지다.
    //  - 유연 항목이 하나라도 있으면 남는 자리를 그 항목들이 다 가져가므로 정렬이
    //    뜻을 갖지 않는다. 넘칠 때도 남는 자리가 0이라 셋이 모두 같다.
    //  - `stretch`에 해당하는 값은 두지 않는다. 주축에서 늘리는 일은 `weight`가
    //    이미 한다 — 같은 일에 이름이 둘이면 다음 사람이 다시 고를 수 있다.
    //  - `space_between`도 두지 않는다. 항목 **사이**에 자리를 만드는 것은
    //    `add_flexible_gap`이고, 정렬이 몰래 자리를 끼우면 그 자리를 만든 것이
    //    gap인지 정렬인지 코드에서 사라진다.
    enum class stack_main_alignment
    {
        start,
        center,
        end,
    };

    struct stack_config
    {
        stack_direction direction { stack_direction::column };
        // 자식 사이 간격이다 (논리 픽셀).
        float spacing { 0.0f };
        edge_insets padding {};
        stack_alignment cross_alignment { stack_alignment::stretch };
        // 남는 자리를 주축의 어디에 둘지다.
        // `main_alignment`은 남는 공간을 항목 앞·뒤에 둔다.
        // `add_flexible_gap()`은 항목 사이에 유연한 간격을 둔다.
        stack_main_alignment main_alignment { stack_main_alignment::start };
    };

    // 한 항목의 배치 설정이다 (길이는 전부 논리 픽셀).
    //
    // 항목 쪽 값을 여기 모으는 이유는 **이름 없는 float가 늘면 부르는 쪽에서 무슨
    // 값인지 읽을 수 없어서다.** 세 번째 인자였던 `cross_length`는 세로 stack의
    // 습관인 (높이, 폭) 순서로 가로 stack의 두 자리에 적혀 있었고, 그 버튼들이 줄
    // 밖으로 60px 넘게 삐져나간 채로 아무도 알아채지 못했다. 지정 초기화
    // `{ .length = 96.0f, .cross_length = 30.0f }`는 그 오류를 애초에 쓸 수 없게 한다.
    //
    // **없음은 언제나 0으로 말한다.** `std::optional`을 섞지 않는다 — 한 구조체
    // 안에서 없음을 말하는 법이 둘이 되면 안 된다.
    struct stack_item
    {
        // 주축 길이다. `weight`가 0보다 크면 쓰지 않는다.
        float length { 0.0f };
        // 0보다 크면 고정 길이를 뺀 나머지를 이 비율로 나눠 갖는다.
        float weight { 0.0f };
        // 0보다 크면 교차축 길이도 정하고, 아니면 남는 폭을 다 채운다.
        float cross_length { 0.0f };
        // 유연 항목이 **반드시 받는** 주축 길이다. 0이면 하한이 없다.
        // `weight`가 0보다 클 때만 뜻을 갖는다 — 고정 항목은 `length`가 이미
        // 하한이자 상한이라, 배치가 그것을 다시 흥정하면 "길이는 담을 때 정한다"가
        // 거짓이 된다.
        //  - 하한에 걸려 늘어난 만큼을 다른 유연 항목에서 **빼지 않는다.** 그만큼
        //    컨테이너를 넘치고, 넘침은 이 element가 이미 하는 일이다 —
        //    **하한은 넘침을 만든다.**
        //  - 재분배를 하지 않으므로 유연 길이의 합을 세지 않고, 그래서 부동소수
        //    잔차가 계약에 들어오지 않는다. 필요해지면 이 규칙만 바뀌고 이 필드의
        //    뜻은 그대로다.
        //  - 상한은 두지 않는다. 상한이 남긴 자리의 임자를 정할 근거가 아직 코드에
        //    없다 (stack-expressiveness-design.md).
        float minimum { 0.0f };
    };

    // 자식을 한 방향으로 쌓는 배치 컨테이너다.
    //
    // 길이는 담을 때 정한다.
    //  - 측정 단계를 두지 않으므로 element가 자기 크기를 알릴 필요가 없고,
    //    배치가 담는 쪽의 코드에 그대로 드러난다.
    // 남는 자리는 `add_flexible`이 비율대로 나눠 갖는다.
    class stack_element final : public ui_element
    {
    public:
        stack_element(ui_element_id id, stack_config config);

        // 항목 설정을 그대로 주고 담는다.
        // 새 표현력은 전부 이 자리로 들어온다.
        //  - **값을 다듬지 않는다.** 구조체가 말하는 대로 담고 `weight`가 0 이하면
        //    고정 항목이라 `length`를 쓴다. 유연 항목을 만드는 것은 `add_flexible`의
        //    계약이고 그쪽이 0 이하를 1.0으로 다듬는다 — 진입점 둘이 같은 값을 다르게
        //    다듬으면 어느 쪽이 정본인지 사라진다.
        void add(std::unique_ptr<ui_element> child, stack_item item);
        // 주축 길이만 정해 담는다 (논리 픽셀).
        //  - 위 함수의 얇은 편의 함수다. 담는 호출 대부분이 "길이 하나"뿐이라 그
        //    자리를 지정 초기화로 바꾸면 읽기만 나빠진다.
        //  - 교차축 길이는 `.cross_length =`로만 준다. 한 값을 말하는 법이 둘일 때
        //    나쁜 쪽이 실제로 두 번 틀렸다 (`stack_item` 주석).
        void add(std::unique_ptr<ui_element> child, float length);
        // 고정 길이를 뺀 나머지를 `weight` 비율로 나눠 갖는다.
        // `weight`가 0 이하면 1.0으로 본다.
        void add_flexible(std::unique_ptr<ui_element> child, float weight = 1.0f);
        // 자식 없이 자리만 비운다.
        // 빈 element를 tree에 넣지 않으므로 hit test와 그리기가 늘지 않는다.
        void add_gap(float length);
        void add_flexible_gap(float weight = 1.0f);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        struct entry
        {
            // 자리만 비우는 항목은 nullptr다.
            ui_element* child { nullptr };
            stack_item item {};
        };

        stack_config config_ {};
        std::vector<entry> entries_ {};
    };
} // namespace luil

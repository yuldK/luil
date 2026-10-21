#pragma once

#include "luil/ui/list_element.h"
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
        // 계층의 깊이다. 0이면 뿌리 줄이다 (`list_item::depth`와 같다).
        // **앱이 평탄화해서 준다** — 접힌 가지의 자식은 모델에 담지 않는다 (list-view-design.md의 평탄화).
        int depth { 0 };
        // 잎·접힘·펼침이다. 삼각형과 ←·→, 보조 기술의 펼침 상태가 이 값을 읽는다.
        list_expansion expansion { list_expansion::none };
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
    // 누른 행의 키와 그 누름의 문맥이다 (수식 키 — Ctrl+클릭 더하기, Shift+클릭 범위 고르기).
    using virtual_list_pick_factory = std::function<input_action(const std::u8string& key, const ui_action_context& context)>;
    // 커서를 옮긴 걸음과 그때 눌린 수식 키의 뜻이다 (`virtual_list_config::move_with`, `selection_move`).
    struct virtual_list_move_context
    {
        // 커서를 옮긴 키의 걸음이다. 걸음 없이 옮긴 것(행 누름·글자 탐색)은 비어 있다.
        std::optional<value_step> step {};
        // Shift다. 기준점을 둔 채 기준점부터 커서까지를 고른다.
        bool extend { false };
        // Ctrl이다. 고른 것을 두고 커서만 옮긴다. `extend`와 함께면 그 구간을 지금 고른 것에 더한다.
        bool keep_selection { false };

        [[nodiscard]] bool operator==(const virtual_list_move_context&) const = default;
    };
    using virtual_list_move_with_factory = std::function<input_action(const std::u8string& key, const virtual_list_move_context& context)>;
    // 항목에 컨텍스트 메뉴를 열자는 메시지다.
    // `tab_context_message_factory`와 같은 모양이다 — 문맥의 좌표(물리 픽셀, 창 좌표)는 메뉴를 띄울 자리이고,
    // 수식 키는 그 순간의 것이다.
    using virtual_list_context_message_factory = std::function<input_action(const std::u8string& key, const ui_action_context& context)>;

    // 창에 걸치지 않아도 짓는 고른 행의 최대 수다 (창에 가까운 것부터).
    //  - 보조 기술은 tree에 선 행만 읽는다. 창 밖의 고른 행을 짓지 않으면 고른 것을 흘려 보낸 순간 선택 읽기
    //    (UIA `GetSelection`)와 선택 알림에서 빠진다.
    //  - 상한이 있는 것은 모두 고르기 때문이다. 십만 줄을 고르면 행이 모델만큼 지어진다. 상한을 넘는 선택은 한꺼번
    //    변화라 보조 기술에 다시 읽으라고 알린다(`access_selection_burst`). 그래도 넘친 행은 선택 읽기에서 빠진다 —
    //    상한 없이 다 답하는 길은 행을 짓지 않고 답하는 UIA `ItemContainer`·`VirtualizedItem` 패턴이다.
    //  - 창 밖의 고른 행을 고름에서 빼면 그 행은 tree에서 빠질 뿐 "골라지지 않음"으로 바뀌지 않는다. 그래서
    //    그 행의 빠짐 알림(`ElementRemovedFromSelection`)은 나지 않는다.
    inline constexpr std::size_t virtual_list_offscreen_selection_limit { 64 };

    struct virtual_list_config
    {
        // 같은 화면에 목록이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // **모델 전체**다. 화면에 걸치지 않는 항목도 전부 담는다.
        //  - 담기는 것은 값이지 element가 아니다. tree에 서는 것은 창에 걸치는 몇 줄뿐이다.
        //  - 이 vector는 **frame마다 깊은 복사**다. 항목마다 키와 이름 문자열을 새로 복사하므로
        //    모델 크기에 비례한다 — 큰 목록은 `shared_items`로 넘긴다.
        std::vector<virtual_list_item> items {};
        // 공유하는 불변 모델이다. 있으면 `items` 대신 이것을 읽고, `items`는 비워 둔다.
        //  - 모델이 바뀌지 않는 frame에서는 참조 수 하나만 오른다. 큰 목록에서 frame마다 하던 복사가 사라진다.
        //  - **넘긴 뒤에는 고치지 않는다.** 게시된 tree는 여러 thread가 읽으므로, 바꿀 때는 새 vector를 만들어
        //    갈아 끼운다 (앞 frame의 tree는 옛 vector를 쥔 채 끝까지 산다).
        //  - 키·선택·커서·스크롤 같은 나머지 값은 지금처럼 frame마다 준다.
        std::shared_ptr<const std::vector<virtual_list_item>> shared_items {};
        // 고른 항목의 키다 (앱 상태).
        std::u8string selected {};
        // 여러 항목을 함께 고르는 목록이다 (앱 상태). 켜면 `selection`의 키들(과 `selected`)이 고른 행으로
        // 그려지고, 보조 기술은 다중 선택 목록으로 읽는다 (`access_info::multiple_selection`).
        //  - 어떻게 더하고 빼는지는 앱이 정한다 — 행의 누름은 `pick`이 수식 키와 함께 받는다.
        //  - 꺼져 있으면 `selection`은 읽지 않는다. 지금까지의 단일 선택 목록 그대로다.
        //  - 고른 행은 창 밖에서도 지어진다 (`virtual_list_offscreen_selection_limit`). `selected`도 같다.
        bool multiple_selection { false };
        std::vector<std::u8string> selection {};
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
        // `move`에 걸음과 수식 키의 뜻을 더한 짝이다 (keyboard-selection-design.md). 있으면 `move` 대신 쓴다.
        //
        // 다중 선택 목록의 키보드 범위 선택이 여기서 선다. `move`는 키만 받아 Shift+↓와 ↓를 가를 수 없다.
        //  - 화살표·Page·Home/End는 그 걸음과 Shift(`extend`)·Ctrl(`keep_selection`)을 싣는다. Ctrl+화살표도 이 목록의
        //    키가 된다 (선택을 두고 커서만 옮기기). `move`만 든 목록에서 그 키는 지금처럼 앱 단축키로 흐른다.
        //  - 글자 탐색은 걸음 없이 그냥 옮긴 것이다 (`step` 비어 있음, 수식 키 거짓).
        //  - 행 누름은 고르기를 `pick`·`select`가 이미 정했으므로 커서만 옮긴다 (`keep_selection` 참).
        //  - **제자리여도 낸다.** 커서는 그대로여도 고른 것이 바뀌는 걸음(첫 항목에서 Home을 누르면 선택이 그 하나로
        //    접힌다)이 있어서다. 커서만 옮기는 Ctrl 걸음만 제자리에서 내지 않는다.
        //  - 받은 앱은 `selection_move`(ui/selection_model.h)로 다음 선택을 계산하면 된다.
        virtual_list_move_with_factory move_with {};
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
        // 행을 두 번 눌렀을 때다 (마우스 더블 클릭·두 번 탭). 그 행의 키를 받는다.
        //  - 첫 누름은 평소처럼 `select`·`pick`이다. 같은 자리의 두 번째 누름이 이것이 되고, 그 누름은 다시 고르지
        //    않는다 — 이미 첫 누름이 골랐다.
        //  - 포인터만의 문이다. 키보드의 짝은 `activate`다 (Enter가 "열기"인 목록은 둘에 같은 메시지를 준다).
        //  - 비어 있으면 두 번째 누름도 평소의 누름이다.
        virtual_list_message_factory double_click {};
        // 행을 눌렀을 때 그 누름의 문맥까지 받는다. 있으면 `select` 대신 쓴다 (다중 선택 목록의 Ctrl·Shift+클릭).
        //  - 커서를 함께 옮기는 것(`move`)은 `select`와 같다.
        virtual_list_pick_factory pick {};
        // 행을 우클릭하면 낸다. 터치의 길게 누르기·펜의 배럴 버튼도 우클릭이라 이 하나로 함께 선다.
        //  - 고르지도 커서를 옮기지도 않는다. 다중 선택 목록에서 "고르지 않은 행을 우클릭하면 그 행만 고르고,
        //    고른 행이면 선택을 둔 채 메뉴를 연다" 같은 규칙은 앱마다 다르므로 이 메시지를 받은 앱이 정한다.
        //  - 비활성 행에서는 나지 않는다 (누름과 같다).
        //  - `build_row`가 지은 내용 중 **누를 수 있는 것**(단추 같은 것) 위의 우클릭은 그것의 몫이다. 우클릭은
        //    부모로 올라가지 않는다. 그 위에서도 행의 메뉴를 열려면 앱이 그 요소에 같은 우클릭 동작을 단다.
        virtual_list_context_message_factory context_menu {};
        // 보조 기술이 이 항목을 고른 것에 더하라·빼라고 했다 (UIA AddToSelection·RemoveFromSelection).
        // `multiple_selection`일 때만 쓴다. 고르기(Select)는 행의 누름과 같다.
        //  - 없으면 `pick`을 Ctrl+누름의 문맥(`control` 참, 좌표 없음)으로 부른다 — 포인터로 하나 더 고르거나 빼는 몸짓과
        //    같은 길이다. 이미 그 상태인 명령은 먼저 걸러지므로 고르지 않은 행에만 더하기가, 고른 행에만 빼기가 온다.
        //  - `pick`도 없으면 그 명령은 할 수 없다.
        virtual_list_message_factory add_to_selection {};
        virtual_list_message_factory remove_from_selection {};
        // Ctrl+Space다. 커서 항목을 고른 것에 더하거나 뺀다 (`multiple_selection`일 때만). 그 키를 받는다.
        //  - 커서가 비활성 항목이면 키는 목록이 삼키고 아무것도 내지 않는다.
        virtual_list_message_factory toggle_at_cursor {};
        // Ctrl+A다. 모든 항목을 고르자는 메시지다 (`multiple_selection`일 때만).
        //  - **목록의 키다.** 초점이 행 안의 부품에 있어도 그 부품이 쓰지 않으면 목록까지 올라온다
        //    (key-propagation-design.md). 초점이 행 안의 텍스트 칸이면 칸의 전체 선택이 먼저다.
        //  - 비어 있으면 Ctrl+A는 지금처럼 앱 정책으로 흐른다.
        std::function<input_action()> select_all {};
        // 행을 끌어 옮기는 drag & drop이다. 누른 행의 키와 끌기가 시작되는 순간의 문맥(수식 키)으로 payload를 짓는다.
        //  - 고른 항목들을 함께 끌려면 `dragged_owners`에 그 키들을 담는다 (기본 ghost가 `+N`을 보인다). 함께 끌리는 행은
        //    제자리에 흐리게 남는다.
        //  - `source`를 비워 두면 그 행이고, `container`를 비워 두면 이 목록이다. 놓을 수 없는 행 위의 금지 모양은
        //    `rejected_cursor`로 청한다.
        //  - 비어 있으면 행을 끌 수 없다. 끌기는 누른 채 `interaction_config::drag_start_distance`를 넘겨야 서므로 클릭과 함께 산다.
        std::function<drag_payload(const std::u8string& key, const ui_action_context& context)> drag {};
        // 이 행에 놓을 수 있는가다 (폴더 같은 container 행). 받는 행 위에 머물면 그 행이 놓을 자리로 강조된다.
        //  - 끌리는 항목 자신에 놓지 못하게 하는 것도 여기서 답한다. 키는 목록 안에서만 유일하므로 출발 목록까지
        //    견주는 `drag_payload::carries(목록 id, 키)`로 묻는다 — 키만 견주면 다른 목록에서 온 같은 키의 행을 거절한다.
        //  - `drop`과 함께 있어야 행이 놓을 자리가 된다.
        std::function<bool(const std::u8string& key, const drag_payload& payload)> accepts_drop {};
        // 이 행에 놓았다. 놓는 순간의 문맥을 받는다 — Ctrl(`control`)로 복사·옮기기를 가른다.
        std::function<input_action(const std::u8string& key, const drag_payload& payload, const ui_action_context& context)> drop {};
        // 스크롤 위치를 옮기자는 메시지다 (delta는 논리 픽셀).
        // 휠·막대·키보드 되살리기가 전부 이 하나를 쓴다 (`scroll_area_config`와 같다).
        std::function<input_action(float delta)> scroll {};
        // 스크롤 위치를 **이 자리로** 하라는 절대 메시지다 (offset은 논리 픽셀).
        std::function<input_action(float offset)> scroll_to {};
        scrollbar_visibility bar { scrollbar_visibility::automatic };
        // 창의 위·아래 가장자리 표시다 (구분선과 흘린 쪽의 그림자).
        // 안의 영역에 그대로 이어진다 — `scroll_area_config::edges`와 같은 설정·같은 그림이다.
        scroll_edges edges {};
        // 보조 기술이 이 목록과 그 행을 읽는 역할이다.
        //  - 목록 위에 다른 뜻을 얹는 컴포넌트가 바꾼다. 표(`table_element`)의 본문은 표(DataGrid)와 행(DataItem)이다.
        //  - 선택 container 규칙(`access_selection_container`)은 역할을 따르므로, 바꾼 역할도 선택 container여야
        //    고른 행을 읽는다 (`list`·`tree`·`table`).
        access_role role { access_role::list };
        access_role row_role { access_role::list_item };
        // 가지를 이 상태로 하라는 절대 메시지다 (`list_config::set_expanded`와 같다). 있으면 목록이 계층이 된다.
        //  - 행 앞에 깊이만큼 들여쓰고 가지에는 삼각형을 세운다. 잎도 삼각형 자리를 비워 글이 줄을 맞춘다.
        //  - ←는 펼친 가지를 접고, 접혔거나 잎이면 부모로 간다. →는 접힌 가지를 펴고, 펼친 가지면 첫 자식으로 간다.
        //    갈 곳이 없는 ←·→는 소비하지 않아 위로 흐른다 (표에서는 가로 스크롤이 된다).
        //  - 보조 기술의 펼치기·접기도 이 메시지다. 행은 펼침 상태를 ExpandCollapse로 알린다.
        std::function<input_action(const std::u8string& key, bool expanded)> set_expanded {};
        // 행 앞에 들여쓰기와 삼각형 자리를 두는가. 계층을 다른 자리에 그리는 컴포넌트(표의 트리 열)가 끈다.
        bool tree_gutter { true };
        // ←·→를 계층 키로 쓰는가. ←·→에 다른 뜻을 먼저 주는 컴포넌트(셀 커서가 있는 표)가 끄고, 계층 키가 필요한
        // 자리에서 `step_tree`를 직접 부른다.
        bool tree_keys { true };
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
        // 이 목록이 읽는 모델이다 (`shared_items`가 있으면 그것, 없으면 `items`).
        [[nodiscard]] const std::vector<virtual_list_item>& model() const noexcept;
        // 계층 목록의 ←(`forward` 거짓)·→가 할 일이다 (`set_expanded`). 갈 곳이 없으면 nullopt다.
        //  - 목록 자신의 ←·→가 이것이다. `tree_keys`를 끈 컴포넌트가 자기 규칙 안에서 같은 일을 시킬 때 부른다.
        [[nodiscard]] std::optional<std::vector<input_action>> step_tree(bool forward) const;

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
        // 옮길 자리가 없거나 `move`·`move_with`가 둘 다 없으면 빈 목록이다.
        [[nodiscard]] std::vector<input_action> move_cursor(std::size_t index, const virtual_list_move_context& context) const;
        // 커서를 옮기는 메시지가 하나라도 있는가 (`move` 또는 `move_with`).
        [[nodiscard]] bool moves_cursor() const noexcept;
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

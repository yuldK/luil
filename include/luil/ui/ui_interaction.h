#pragma once

#include "luil/messaging/channel.h"
#include "luil/messaging/latest_slot.h"
#include "luil/text/text_edit.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace luil {
    // 텍스트 편집 한 동작이다.
    // controller가 만들고 policy가 앱 메시지로 바꾼다.
    struct text_edit_request
    {
        text_input_target target { text_input_target::none };
        text::text_edit_command command { text::text_edit_command::insert };
        bool extend { false };
        std::size_t offset { 0 };
        std::u8string text {};
    };

    // IME 조합의 표시 상태다.
    // TSF session이 만들고 policy가 앱 메시지로 바꾼다.
    // `composing == false`는 조합이 끝났다는 뜻이라 표시 상태를 버린다.
    struct text_composition_event
    {
        text_input_target target { text_input_target::none };
        // 조합 중 화면에 보이는 글 전체다 (확정된 글 + 조합 글).
        std::u8string text {};
        std::size_t caret { 0 };
        // `text` 안에서 밑줄을 그을 구간이다 (UTF-8 byte offset).
        std::size_t composing_begin { 0 };
        std::size_t composing_end { 0 };
        bool composing { false };
    };

    // insert·replace 전에 글을 다듬는 앱 훅이다 (숫자만 남기기, 길이 제한 등).
    // 비어 있으면 그대로 넣는다.
    using text_insert_filter = std::function<std::u8string(std::u8string_view)>;

    // 편집 명령 하나를 초안 상태에 적용한다.
    // 명령 enum(text_edit_command)과 상태 기계 함수(text_edit_*)가 둘 다
    // 라이브러리의 것이라 이 다리도 라이브러리가 놓는다 — 앱 logic은
    // policy가 만든 요청을 자기 초안에 이 함수로 적용하기만 하면 된다.
    void apply_text_edit(text::text_edit_state& state, const text_edit_request& request, const text_insert_filter& filter = {});

    // 초안과 조합 표시 상태를 텍스트 박스가 그릴 view로 합친다.
    // 조합 이벤트가 없거나 다른 target의 것이면 확정 글만 담긴다.
    [[nodiscard]] text_input_view make_text_input_view(const text::text_edit_state& state, const std::optional<text_composition_event>& composition, text_input_target target);

    // 점진 검색(검색 칸이 목록을 좁히는 것)이 쓸 질의를 고른다.
    // 조합 중인 글자를 포함한 글로 먼저 찾고(`text_input_view::displayed_text`),
    // 그것이 **아무것도 맞히지 못하면** 조합 중인 글자를 뺀 확정된 글로 물러선다.
    //  - 한글은 한 글자가 여러 단계로 완성된다. "토스"의 중간인 "토ㅅ"은 어디에도
    //    없으므로, 물러서지 않으면 목록이 사라졌다 나타나기를 반복한다.
    //  - **확정된 글이 비어 있으면 물러서지 않는다.** 빈 질의는 좁히기가 아니라
    //    "전부"라서, 첫 글자를 조합하는 동안 목록 전체가 되살아난다.
    //  - 무엇이 맞는지는 앱이 정한다 (부분 일치·대소문자·초성 검색 등).
    //    `any_match`는 그 질의로 맞는 것이 하나라도 있는지만 답한다.
    [[nodiscard]] std::u8string_view search_query(const text_input_view& value, const std::function<bool(std::u8string_view)>& any_match);

    // 스크롤 라우팅 표의 한 줄이다.
    // "이 element를 이 메시지로 흘린다"가 전부다.
    //  - 계기가 아니라 **모양**을 이름 삼는다. 휠(`route_wheel`)과 키보드
    //    되살리기(`route_reveal`)가 같은 줄을 쓰고, 같은 모양의 struct를 둘
    //    세우면 `layout_metrics.h`가 경고한 "식이 두 벌"이 타입에서 일어난다.
    //  - 표 자체는 둘이 다를 수 있다. 넘침 버튼이 있는 탭 막대가 그렇다 —
    //    휠은 바깥 막대를, 되살리기는 안쪽 레인을 이름 대야 한다
    //    (focus-reveal-design.md).
    struct scroll_route
    {
        ui_element_id id {};
        std::function<input_action(float delta)> scroll {};
    };

    // 표에서 (x, y)를 덮는 첫 일치가 임자다.
    // policy::on_wheel의 몸통이 이 한 줄이 된다 — 어느 목록을 어떤 메시지로
    // 스크롤할지(표)는 앱 정책으로 남고, 좌표 판정은 라이브러리가 한다.
    // 덮는 것이 없으면 빈 목록이다.
    [[nodiscard]] std::vector<input_action> route_wheel(const ui_tree& tree, float x, float y, float delta, std::span<const scroll_route> routes);

    // 표에서 `target`을 **품은** 첫 일치가 임자다.
    // policy::on_focus_moved의 몸통이 이 한 줄이 된다 — 어느 창을 어떤 메시지로
    // 흘릴지(표)는 앱 정책으로 남고, 얼마나 흘릴지는 그 창이 답한다
    // (`ui_element::scroll_delta_to_reveal`).
    //  - **이미 보이면 빈 목록이다.** 이것이 방벽이다 — 없으면 화살표를 누를
    //    때마다 0짜리 스크롤 메시지가 logic을 깨워 tree를 다시 짓는다.
    [[nodiscard]] std::vector<input_action> route_reveal(const ui_tree& tree, const ui_element_id& target, std::span<const scroll_route> routes);

    // 표 없는 짝이다 — 임자를 `ui_element::scroll()`에서 찾는다.
    //
    // 앱이 표를 짓지 않는다. 흘리는 컨테이너가 자기 메시지를 들고 있으므로
    // (`scroll_source`), policy의 몸통이 `return route_wheel(tree, event.x, event.y, delta);`
    // 한 줄이 된다 — 화면에 창이 몇이든, 어느 페이지가 떠 있든 같은 줄이다.
    //  - **표를 지우지 않는다.** 위의 표 있는 짝은 그대로 남는다. `scroll_source`를
    //    세우지 않은 컨테이너(앱이 만든 것, 넘침 버튼이 있는 탭 막대처럼 휠과
    //    되살리기의 임자가 갈리는 것)는 여전히 표로 이름 댄다.
    //  - **휠은 하나가 가진다.** 그리기 순서를 거슬러 가장 안쪽·가장 위의 흘리는
    //    컨테이너가 임자이고, 거기서 끝난다. 흘리지 않지만 포인터를 막는 것
    //    (modal scrim 같은 `hit_opaque`)이 먼저 걸리면 아무 일도 하지 않는다 —
    //    포인터를 막는 것이 modal의 몫이라면 휠도 포인터다.
    //  - **되살리기는 겹겹이 이어진다.** 안쪽 창이 초점을 들인 다음에는 그 안쪽
    //    창 자체가 바깥 창의 대상이 된다. 한 겹만 보고 끝내면 행은 안쪽 목록
    //    안에서 보이는데 그 목록이 바깥 판에서 밀려 나가 있는 경우를 놓친다.
    //    **표 있는 짝과 답의 모양이 여기서 갈린다** — 그쪽은 표에서 처음 맞은
    //    한 줄로 끝난다.
    //  - 전부 이미 보이면 빈 목록이다 (delta 0의 방벽과 같은 자리).
    [[nodiscard]] std::vector<input_action> route_wheel(const ui_tree& tree, float x, float y, float delta);
    [[nodiscard]] std::vector<input_action> route_wheel(const ui_tree& tree, const mouse_wheel_event& event, float scroll_delta);
    [[nodiscard]] const ui_element* zoom_owner_at(const ui_tree& tree, float x, float y);
    [[nodiscard]] std::vector<input_action> route_reveal(const ui_tree& tree, const ui_element_id& target);

    // 터치 끌기가 흘릴 컨테이너다 (touch-pen-input-design.md).
    // 휠의 `scroll_route`와 달리 축과 배율을 함께 든다 — 손가락의 물리 이동을
    // 그 컨테이너의 논리 변화량으로 옮기려면 둘 다 있어야 하고, 모르는 값을
    // 세로·배율 1로 추측하면 가로 띠가 세로 손짓에 흐른다.
    struct pan_target
    {
        ui_element_id id {};
        scroll_axis axis { scroll_axis::vertical };
        // 물리 픽셀 / 논리 픽셀이다 (`scroll_source::scale`과 같은 값).
        float scale { 1.0f };
        std::function<input_action(float delta)> scroll {};
    };

    // 표로 끌기를 이름 대는 앱의 한 줄이다.
    // 탐색 규칙(가시성·활성·clip·`hit_opaque` 방벽)은 표 없는 짝과 같다.
    struct pan_route
    {
        ui_element_id id {};
        scroll_axis axis { scroll_axis::vertical };
        float scale { 1.0f };
        std::function<input_action(float delta)> scroll {};
    };

    // (x, y)를 덮고 `axis`로 흐르는 가장 안쪽·가장 위의 컨테이너다.
    // 축이 다른 컨테이너는 지나쳐 바깥을 본다 — 세로 화면 안의 가로 탭 막대
    // 위를 세로로 쓸면 화면이 흐른다. 흘리지 않고 포인터를 막는 것
    // (`hit_opaque`)이 먼저 걸리면 없다.
    [[nodiscard]] std::optional<pan_target> route_pan(const ui_tree& tree, float x, float y, scroll_axis axis);
    [[nodiscard]] std::optional<pan_target> route_pan(const ui_tree& tree, float x, float y, scroll_axis axis, std::span<const pan_route> routes);

    // 터치 몸짓의 설정이다. 마우스·펜·키보드에는 몸짓을 켜는 설정이 없다.
    //  - 끄는 것은 bool이다. 거리·시간 0을 "끔"으로 읽는 중의적 계약을 두지 않는다.
    //  - 거리는 논리 픽셀이다 (누른 표면의 배율로 나눠 잰다).
    struct touch_gesture_config
    {
        // 축에 맞는 빠른 쓸기가 스크롤한다.
        bool pan_enabled { true };
        // 움직이지 않고 오래 눌렀다 떼면 우클릭이다.
        bool long_press_enabled { true };
        float pan_start_distance { 12.0f };
        // 이 시간 안에 시작 거리를 넘어야 스크롤이다. 넘긴 뒤의 이동은 일반 끌기다.
        std::chrono::milliseconds pan_start_time { 400 };
        // 탭·길게 누르기의 최대 이동 허용치이자 터치 일반 끌기의 시작 거리다.
        // 연속 탭의 거리 한계로도 쓴다 — 마우스의 4px로는 두 번 탭이 서지 않는다.
        float press_move_tolerance { 12.0f };
        std::chrono::milliseconds long_press_time { 600 };
        // 같은 확대 보기의 두 접촉만 핀치로 잇는다. 원시 웹뷰 입력과는 별개다.
        bool pinch_enabled { true };
        // 이전·현재 간격이 모두 이 거리 이상일 때만 비율을 만든다.
        float minimum_pinch_distance { 24.0f };

        [[nodiscard]] bool operator==(const touch_gesture_config&) const noexcept = default;
    };

    // 거리는 유한한 양수, 시간은 양수, 그리고 스크롤 시간 창이 길게 누르기보다 짧아야 한다.
    [[nodiscard]] bool valid_touch_gesture_config(const touch_gesture_config& config) noexcept;

    // 컨텍스트 메뉴의 키보드 탐색에 필요한 kind 짝이다.
    // container가 tree에 있으면 메뉴가 열린 것으로 보고 ↑/↓/Enter/Esc를 메뉴가 가져간다.
    //  - owner는 보지 않는다. `menu_config::owner`로 구분한 메뉴도 같은 kind면 찾는다.
    struct menu_kinds
    {
        ui_element_kind container { ui_element_kind::none };
        ui_element_kind item { ui_element_kind::none };
    };

    // 상태 기계가 모르는 앱 정책이다.
    // 휠·키 라우팅, 텍스트 입력 대상 판정, 편집 명령의 앱 메시지 변환을 앱이 구현한다.
    // 모든 기본 구현은 "아무 일도 하지 않음"이라 필요한 것만 재정의한다.
    //
    // 구현은 input thread에서 불린다.
    // tree는 게시된 불변 tree다.
    class interaction_policy
    {
    public:
        interaction_policy() = default;
        interaction_policy(const interaction_policy&) = delete;
        interaction_policy(interaction_policy&&) = delete;
        interaction_policy& operator=(const interaction_policy&) = delete;
        interaction_policy& operator=(interaction_policy&&) = delete;
        virtual ~interaction_policy() = default;

        // 그 element가 문자 입력을 받는 텍스트 박스인지와 그 대상 id다.
        // nullopt면 텍스트 박스가 아니다.
        [[nodiscard]] virtual std::optional<text_input_target> text_target_of(ui_element_kind kind) const
        {
            static_cast<void>(kind);
            return std::nullopt;
        }

        // 편집 동작을 앱 메시지로 바꾼다.
        // 기본은 무시(monostate)다.
        [[nodiscard]] virtual input_action make_text_edit_action(const text_edit_request& request) const
        {
            static_cast<void>(request);
            return {};
        }

        // IME 조합 표시를 앱 메시지로 바꾼다.
        // 기본은 무시(monostate)다.
        // UI thread (TSF session)에서도 불리므로 구현은 순수 변환이어야 한다.
        [[nodiscard]] virtual input_action make_text_composition_action(const text_composition_event& event) const
        {
            static_cast<void>(event);
            return {};
        }

        // 컨텍스트 메뉴 kind다.
        // 값이 있어야 메뉴 키보드 탐색이 켜진다.
        [[nodiscard]] virtual std::optional<menu_kinds> menu() const
        {
            return std::nullopt;
        }

        // 메뉴가 열린 채 Esc다.
        // 통상 닫기 메시지를 돌려준다.
        [[nodiscard]] virtual std::vector<input_action> close_menu() const
        {
            return {};
        }

        // 휠 라우팅이다.
        // scroll_delta는 논리 픽셀로 변환된 값이다 (위로 굴리면 음수 — 내용이 위로 돌아간다).
        [[nodiscard]] virtual std::vector<input_action> on_wheel(const ui_tree& tree, const mouse_wheel_event& event, float scroll_delta)
        {
            static_cast<void>(tree);
            static_cast<void>(event);
            static_cast<void>(scroll_delta);
            return {};
        }

        // 키보드가 초점을 옮긴 직후다 (Tab·화살표·Home/End·글자 탐색).
        // 그 자리가 흘리는 창 밖이면 앱이 자기 스크롤 메시지를 돌려준다 —
        // 스크롤 값은 **앱 상태**라 라이브러리가 고칠 수 없다
        // (keyboard-focus-design.md이 미뤄 둔 그 계기다).
        //  - 몸통은 통상 `route_reveal(tree, focused, 표)` 한 줄이다.
        //  - **눌러서 잡은 초점에는 부르지 않는다.** 누른 자리는 이미 보인다.
        //  - 기본이 빈 목록인 것이 계약이다. Tab·화살표가 액션을 내지 않는 것을
        //    잠근 기존 test들이 그 위에 선다.
        [[nodiscard]] virtual std::vector<input_action> on_focus_moved(const ui_tree& tree, const ui_element_id& focused)
        {
            static_cast<void>(tree);
            static_cast<void>(focused);
            return {};
        }

        // 텍스트 박스·메뉴가 소비하지 않은 키다.
        // tree는 없을 수 있다.
        [[nodiscard]] virtual std::vector<input_action> on_key(const ui_tree* tree, const key_pressed_event& event, const interaction_snapshot& snapshot)
        {
            static_cast<void>(tree);
            static_cast<void>(event);
            static_cast<void>(snapshot);
            return {};
        }

        // Tab 순회의 순서다.
        // 기본은 라이브러리가 만든 **그리기 순서** 그대로다 (`ui_tree::focus_order`).
        // 걸러 내도 되고(그 자리에는 Tab이 서지 않는다) 다시 늘어놓아도 된다.
        //  - element마다 번호를 심지 않는 이유가 이것이다. 번호를 심으면 배치와
        //    순서가 두 곳에 살고, 배치를 옮기는 사람이 번호를 모른 채 옮기는 순간
        //    조용히 어긋난다. 앱은 여기 한곳에서 전부 본다
        //    (focus-group-design.md).
        //  - Tab을 누를 때만 불린다. 매 frame이 아니다.
        //  - **묶음 안의 순서는 여기서 정하지 않는다.** 그것은 묶음의 자식 순서다.
        [[nodiscard]] virtual std::vector<ui_element_id> order_focus(const ui_tree& tree, std::vector<ui_element_id> order)
        {
            static_cast<void>(tree);
            return order;
        }

        // 클릭이 확정된 직후다 (액션 실행 전).
        // 키보드 탐색 초점 등 앱 쪽 입력 상태를 잇는 데 쓴다.
        //  - 터치 탭·터치 길게 누르기·펜 클릭도 같은 자리에서 부른다.
        //    스크롤로 끝난 접촉에는 부르지 않는다.
        virtual void on_click(const ui_element& element)
        {
            static_cast<void>(element);
        }

        // 활성 element를 왼쪽 버튼(터치 접촉·펜촉)으로 누른 순간이다 (클릭이 확정되기 전).
        // 어느 판을 만졌는가로 화면을 바꾸는 앱이 쓴다 — 판 안의 무엇을 눌렀든 같은
        // 답이어야 해서 element마다 액션을 다는 대신 `ui_tree::within`으로 묻는다.
        //  - 돌려준 액션은 그 누름의 다른 액션(끌기 손잡이의 누름)보다 앞선다.
        //  - **관찰 hook이다.** 이 누름이 탭·스크롤·길게 누르기 중 무엇이 될지는
        //    아직 모른다. 장치는 `event.device`로 본다. 앱이 여기서 화면을 바꾸면
        //    이후의 몸짓은 다음 tree에서 임자를 다시 확인한다.
        //  - 클릭 대상이 없는 여백(흘리는 창의 빈 곳)에서는 부르지 않는다.
        //  - 기본이 빈 목록인 것이 계약이다.
        [[nodiscard]] virtual std::vector<input_action> on_press(const ui_tree& tree, const ui_element& element, const pointer_pressed_event& event)
        {
            static_cast<void>(tree);
            static_cast<void>(element);
            static_cast<void>(event);
            return {};
        }

        // 터치 끌기가 흘릴 컨테이너다.
        // 기본은 표 없는 탐색(`route_pan`)이다. 표로만 흘리는 컨테이너는
        // `route_pan(tree, x, y, axis, 표)`로 답한다.
        //  - 끄는 동안 매 이동마다 새 tree로 다시 묻는다. 같은 id와 축이 답할 때만
        //    이어 흘리고, 아니면 그 접촉을 취소한다.
        [[nodiscard]] virtual std::optional<pan_target> pan_target_at(const ui_tree& tree, float x, float y, scroll_axis axis)
        {
            return route_pan(tree, x, y, axis);
        }
    };

    struct interaction_config
    {
        // Win32에서는 GetDoubleClickTime()을 주입한다.
        // test는 고정값을 쓴다.
        std::chrono::milliseconds double_click_time { 500 };
        float double_click_distance { 4.0f };
        // 이 거리(논리 픽셀 × scale 없이 창 좌표)만큼 끌면 클릭 대신 drag다.
        float drag_start_distance { 6.0f };
        // 연달아 친 글자를 한 질의로 묶는 한계다 (묶음 안 글자 탐색).
        // 이 시간이 지나 다시 치면 앞의 글자는 잊고 새 질의가 시작된다.
        std::chrono::milliseconds typeahead_reset_time { 1000 };
        // 텍스트 박스가 포인터 x를 글 안의 offset으로 옮길 때 쓰는 측정 함수다.
        // 앱은 내장 글꼴로 만들고 test는 고정 폭 가짜를 넣는다.
        // 비어 있으면 caret이 글 끝으로 간다.
        text_measurer measure_text {};
        // 터치 몸짓이다. 실행 중 변경은 `interaction_controller::set_touch_config`가 받는다.
        touch_gesture_config touch {};
    };

    // 주 창 밖 표면(popup·보조 창)들의 tree다.
    // 순서는 frame의 순서 그대로다.
    // controller에는 "표면"만 있고 popup인지 창인지 구분이 없다 (multi-window-design.md).
    using surface_tree_list = std::vector<std::pair<std::u8string, std::shared_ptr<const ui_tree>>>;

    // raw input을 tree로 hit test해 액션과 interaction snapshot으로 바꾸는 상태 기계다.
    // input thread가 소유하는 입력 정규화 상태다.
    // 시각은 이벤트에 담긴 timestamp만 사용하므로 test가 결정적이다.
    // policy는 nullptr일 수 있고, 그 경우 텍스트 입력·휠·키 라우팅이 전부 꺼진다.
    //
    // 포인터 이벤트는 surface 표식으로 어느 tree에 hit할지 고른다.
    // 키·문자는 OS 초점을 가진 창(주 또는 보조)이 보낸다 — popup만 초점을 받지 않아
    // 앵커 창이 대신 나른다.
    // 텍스트 초점은 눌린 표면을 기억해(`focused_surface`) 그 표면의 tree에서 잇는다.
    // 그 표면이 keyboard focus를 잃으면 표면 id가 맞을 때만 텍스트 초점을 거둔다.
    // 키도 표식을 싣지만 그것은 **초점이 없을 때의 시작 표면**일 뿐이다 — 논리
    // 초점이 있으면 그것이 이긴다 (key-surface-routing-design.md).
    //
    // 이벤트가 아니라 **tree가 계기인 자리**(가둠 진입·되돌리기)는 실어 올 표식이
    // 없어 활성 표면(`active_surface_`)을 본다. 곧 라우팅은 이벤트가 답하고 진입은
    // 활성 표면이 답한다 — 근거가 둘이 아니라 질문이 둘이다
    // (active-surface-design.md).
    class interaction_controller
    {
    public:
        explicit interaction_controller(interaction_policy* policy = nullptr, interaction_config config = {}) noexcept;

        void set_tree(std::shared_ptr<const ui_tree> tree) noexcept;
        // 주 창 밖 표면의 tree들을 통째로 바꾼다.
        // 이벤트의 surface가 여기 없는 id면 그 이벤트는 아무것도 맞히지 않는다.
        void set_surface_trees(surface_tree_list surfaces) noexcept;
        [[nodiscard]] std::vector<input_action> process(const raw_input_event& event);
        // raw 입력 큐가 넘쳐 이벤트가 잘려 나간 뒤에 부른다 (envelope.sequence의 건너뜀).
        // 유실분에 뗌·이탈 같은 상태 전이가 있었을 수 있으므로 진행 중이던
        // 몸짓(누름·텍스트 끌기·drag)을 거둔다 — 남긴 채 틀리면 유령 선택
        // 확장·유령 drop이 되고, 거둬서 틀리면 다음 누름이 다시 시작할 뿐이다.
        // 초점은 건드리지 않는다 — 다음 focus 이벤트가 바로잡는다.
        void cancel_dropped_gestures() noexcept;
        // 실행 중 터치 설정을 바꾼다. 잘못된 값이면 거짓이고 직전 값이 남는다.
        //  - 진행 중 접촉의 거리·시간은 누를 때의 값 그대로다.
        //  - 끈 몸짓이 진행 중이면(스크롤을 끄면 대기·스크롤, 길게 누르기를 끄면 대기)
        //    그 접촉을 취소하고 남은 이벤트를 삼킨다. 다시 켜도 아직 닿아 있는
        //    손가락이 새 접촉이 되지 않는다.
        //  - 손잡이 조작에서 길게 누르기를 끄면 조작은 이어가고 메뉴 후보만 거둔다.
        bool set_touch_config(const touch_gesture_config& config) noexcept;
        [[nodiscard]] const interaction_snapshot& snapshot() const noexcept;

    private:
        // 터치 접촉 하나의 판정 단계다.
        enum class touch_phase
        {
            // 탭·길게 누르기·스크롤·끌기 중 무엇이 될지 아직 모른다.
            pending,
            panning,
            view_pan,
            pinching,
            // 일반 drag & drop이다 (`snapshot_.drag`가 선다).
            dragging,
            // 누르는 즉시 시작하는 전용 조작이다 (`pointer_drag_target`).
            handle,
        };

        // 보통 조작은 한 접촉이다. 같은 확대 보기의 핀치만 둘째 접촉을 품는다.
        //  - 쥐지 않은 id의 이동·뗌·취소는 무시한다. 핀치 뒤 남은 접촉은
        //    이동만 잇고 새 누름·탭으로 승격하지 않는다.
        //  - `clear_press`가 지우지 않는다. 누름이 스크롤로 바뀌어도 접촉은 남는다.
        struct touch_contact
        {
            std::uint32_t id { 0 };
            std::u8string surface {};
            // 누를 때의 값이다. 진행 중에 설정이 바뀌어도 판정 기준은 그대로다.
            touch_gesture_config config {};
            float scale { 1.0f };
            std::chrono::steady_clock::time_point pressed_at {};
            float start_x { 0.0f };
            float start_y { 0.0f };
            float last_x { 0.0f };
            float last_y { 0.0f };
            // 누른 자리에서 가장 멀리 간 거리(논리 픽셀)다.
            // 돌아와도 줄지 않는다 — 한 번 허용치를 넘은 접촉은 탭도 길게 누르기도 아니다.
            float max_distance { 0.0f };
            touch_phase phase { touch_phase::pending };
            // 아직 스크롤로 바뀔 수 있는가. 시간 창이 지났거나 축에 맞는 후보가
            // 없다고 판정되면 닫힌다.
            bool pan_open { false };
            // 전용 조작이 실제 이동 액션을 냈다. 그 뒤로는 길게 누르기가 아니다.
            bool moved_action { false };
            std::optional<pan_target> pan {};
            ui_element_id view {};
            struct second_contact
            {
                std::uint32_t id { 0 };
                float x { 0.0f };
                float y { 0.0f };
            };
            std::optional<second_contact> second {};
            // 누른 자리의 활성 element다. 없으면 빈 곳(또는 비활성)을 누른 것이다.
            ui_element_id target {};
        };

        [[nodiscard]] std::vector<input_action> process_touch_press(const pointer_pressed_event& event);
        [[nodiscard]] std::vector<input_action> process_touch_move(const pointer_moved_event& event);
        [[nodiscard]] std::vector<input_action> process_touch_release(const pointer_released_event& event);
        [[nodiscard]] std::vector<input_action> process_cancel(const pointer_cancelled_event& event);
        // 탭으로 확정된 뗌이다 (클릭·더블 탭·caret·길게 누르기).
        [[nodiscard]] std::vector<input_action> finish_touch_tap(const touch_contact& contact, const pointer_released_event& event);
        [[nodiscard]] std::optional<pan_target> resolve_pan(const ui_tree& tree, float x, float y, scroll_axis axis);
        // 진행 중인 터치 접촉을 액션 없이 거둔다.
        void cancel_touch() noexcept;
        // 마우스·펜 조작을 액션 없이 거둔다. OS 파일 끌기는 별도 소유권이다.
        void cancel_pointer_gesture() noexcept;
        // 마우스·펜의 진행 중 몸짓이 있는가 (누름·텍스트 끌기·전용 조작·내부 끌기).
        [[nodiscard]] bool pointer_gesture_active() const noexcept;
        // 누른 element로 초점을 옮기거나 거둔다 (마우스는 누를 때, 터치는 탭이 확정될 때).
        void apply_press_focus(const ui_tree& tree, const ui_element* hit, const std::u8string& surface, std::chrono::steady_clock::time_point time);
        // 끌리는 그림의 자리와 수락 중인 drop 대상을 갱신한다.
        void update_drag(const ui_tree& tree, float x, float y);
        // 끌기를 놓는다. 수락하는 대상 위에서만 drop 액션이 나간다.
        [[nodiscard]] std::vector<input_action> finish_drag(const ui_tree* tree, float x, float y);
        [[nodiscard]] std::optional<text_input_target> text_target(ui_element_kind kind) const;
        [[nodiscard]] input_action text_edit_action(text_input_target target, text::text_edit_command command, bool extend = false) const;
        [[nodiscard]] std::vector<input_action> process_move(const pointer_moved_event& event);
        [[nodiscard]] std::vector<input_action> process_press(const pointer_pressed_event& event);
        [[nodiscard]] std::vector<input_action> process_release(const pointer_released_event& event);
        void process_file_drag_entered(const file_drag_entered_event& event);
        void process_file_drag_moved(const file_drag_moved_event& event);
        [[nodiscard]] std::vector<input_action> process_key(const key_pressed_event& event);
        // 보조 기술이 청한 초점이다 (UIA의 SetFocus).
        // 그 표면의 tree에서 요소를 찾아 **자리인 것만** 세운다 — Tab이 서지 않는
        // 곳에는 보조 기술도 서지 않는다.
        //  - 가둠(modal) 밖인지는 여기서 묻지 않는다. 그 판정은
        //    `update_focus`의 거둠 한 곳에 있고, 경로마다 흩어 두지 않는 것이
        //    그쪽의 계약이다 (modal-dialog-design.md). 청하는 쪽(provider)이
        //    닿을 수 없는 자리를 미리 거절한다 (accessibility-action-design.md).
        //  - 되살리기는 저절로 따라온다. `process`가 초점의 전후를 한 곳에서
        //    견줘 `on_focus_moved`를 부른다 (focus-reveal-design.md).
        [[nodiscard]] std::vector<input_action> process_access_focus(const access_focus_event& event);
        // 메뉴 container가 있는 표면과 그 tree다.
        // 메뉴가 닫혀 있으면 `tree`가 nullptr다.
        struct menu_location
        {
            const ui_tree* tree { nullptr };
            // 비어 있으면 주 창이다 (표면 표식의 통상 규칙).
            std::u8string surface {};
        };

        // 컨텍스트 메뉴가 열린 동안의 키 처리다.
        // ↑/↓는 활성 항목 사이를 오가고 Enter는 강조 항목의 클릭 액션을 실행하며 Esc는 닫는다.
        // 메뉴가 갖는 키(↑/↓/Enter/Esc)면 그 결과고, 아니면 nullopt라 호출자가
        // 메뉴 안의 텍스트 박스에게 넘긴다.
        //  - 강조를 세우면 그 메뉴가 사는 표면도 함께 세운다 (`menu_surface`).
        [[nodiscard]] std::optional<std::vector<input_action>> process_menu_key(const key_pressed_event& event, const menu_kinds& kinds, const menu_location& host);
        // 메뉴 container가 있는 자리다.
        // 주 tree와 popup tree를 차례로 보고 없으면 tree가 nullptr다 (메뉴가 닫혀 있다).
        [[nodiscard]] menu_location find_menu(const menu_kinds& kinds) const;
        [[nodiscard]] std::vector<input_action> run_trigger(const ui_element& element, ui_trigger trigger, float x, float y, bool control);
        // 초점을 가진 텍스트 박스의 키 처리다.
        // 그 박스가 소비한 키만 값을 돌려주고, 아니면 nullopt라 기존 키 경로가 이어진다.
        [[nodiscard]] std::optional<std::vector<input_action>> process_text_input_key(const key_pressed_event& event);
        // 초점을 가진 박스의 선택을 클립보드로 넘기는 요청이다.
        // `erase`면 지우기까지 함께 낸다 (잘라내기).
        // 선택이 없으면 빈 목록이다.
        [[nodiscard]] std::vector<input_action> copy_focused_selection(text_input_target target, bool erase) const;
        // 포인터 x를 그 텍스트 박스의 caret 자리로 옮기는 편집 동작이다.
        [[nodiscard]] std::vector<input_action> place_text_caret(const ui_element& element, float x, bool extend, bool select_word) const;
        // surface가 가리키는 tree다.
        // 비어 있으면 주 tree고, 모르는 표면 id면 nullptr다.
        [[nodiscard]] const ui_tree* surface_tree(const std::u8string& surface) const noexcept;
        void update_hover(float x, float y, std::chrono::steady_clock::time_point time);
        void clear_press() noexcept;
        // 사라진 표면에서 시작한 누름·끌기를 거둔다.
        // 표면 목록을 받은 **직후**에 부른다 — 그 표면에서 시작한 몸짓은 끝낼
        // 계기(뗌)가 그 표면 id로 오므로 아무도 거두지 못하고, 남은 끌기 대상이
        // 그 다음 포인터 이동을 통째로 삼킨다 (multi-window-design.md).
        void clear_gone_surface_gestures() noexcept;
        // 초점을 가진 컨트롤을 실행하는 키다 (Space·Enter).
        // 실행했으면 그 액션들이고, 아니면 nullopt라 기존 키 경로가 이어진다.
        //  - 초점이 텍스트 칸이면 Space는 빈 목록으로 **삼키고**(글자다) Enter는
        //    nullopt로 **흘려보낸다**(기본 버튼이 받아야 한다).
        [[nodiscard]] std::optional<std::vector<input_action>> process_activation_key(const key_pressed_event& event);
        // 지금 초점을 가진 element가 Tab을 자기 것으로 쓰는가.
        [[nodiscard]] bool focus_takes_tab() const;
        // 초점이 묶음 안일 때의 화살표다.
        // 방향이 맞으면 항목을 옮기고, 아니면 nullopt라 키가 앱 정책으로 흐른다.
        // 텍스트 박스가 초점을 갖지 않은 문자 입력이다.
        // 묶음 안이면 그 글자로 항목을 찾고, 아니면 아무 일도 하지 않는다.
        [[nodiscard]] std::vector<input_action> process_typeahead(const character_typed_event& event);
        [[nodiscard]] std::optional<std::vector<input_action>> process_group_key(const key_pressed_event& event);
        // 초점을 가진 element가 키로 **자기 값**을 바꾼다 (막대·스크롤 막대·손잡이).
        // 그 element가 가져간 키만 값이고, 아니면 nullopt라 묶음·앱 정책으로 이어진다.
        //  - 묶음보다 **앞**이다. 초점이 선 element 자신의 키 쓰임이 감싼 묶음보다
        //    앞선다 — 텍스트 박스를 묶음 앞에 둔 것과 같은 규칙이다. 뒤에 두면
        //    목록이나 선택 묶음 안에 놓인 막대가 Home/End를 통째로 빼앗긴다
        //    (묶음은 축이 서 있기만 하면 Home/End를 자기 것으로 본다).
        [[nodiscard]] std::optional<std::vector<input_action>> process_step_key(const key_pressed_event& event);
        // 가둠(modal)이 떠 있을 때의 Esc다.
        // 가둠의 dismiss 액션이 있으면 그 결과고, 없으면 nullopt라 키가 앱 정책으로 흐른다.
        [[nodiscard]] std::optional<std::vector<input_action>> process_dismiss_key(const key_pressed_event& event);
        // 초점이 받지 못한 Enter다.
        // 지금 범위의 기본 버튼이 있으면 그 클릭이고, 없으면 nullopt라 키가 앱 정책으로 흐른다.
        [[nodiscard]] std::optional<std::vector<input_action>> process_default_key(const key_pressed_event& event);
        // 키보드로 옮긴 초점을 세운다 (테를 켠다).
        // 시각이 비면 caret 깜빡임의 기준이 없다는 뜻이다 — 이벤트가 아니라 tree가
        // 계기인 자리(가둠 진입)가 그렇다. 두 소비자(caret 그리기·`next_update`)가
        // 이미 빈 값을 옳게 다룬다 (focus-entry-design.md).
        void set_keyboard_focus(const std::u8string& surface, const ui_element_id& id, std::optional<std::chrono::steady_clock::time_point> time);
        // 초점을 한 칸 옮긴다 (Tab·Shift+Tab).
        // 옮길 자리가 없으면 거짓이라 키가 앱 정책으로 흐른다.
        //  - `key_surface`는 그 키를 나른 표면이다. 초점이 **없을 때만** 쓰는
        //    시작점이고, 초점이 있으면 그 초점의 표면에서 돈다
        //    (key-surface-routing-design.md).
        [[nodiscard]] bool move_focus(bool forward, const std::u8string& key_surface, std::chrono::steady_clock::time_point time);
        void clear_focus() noexcept;
        void update_focus();

        interaction_policy* policy_ { nullptr };
        interaction_config config_ {};
        std::shared_ptr<const ui_tree> tree_ {};
        surface_tree_list surface_trees_ {};
        interaction_snapshot snapshot_ {};

        // 텍스트 박스를 누른 채 끄는 중이면 그 박스다.
        // 포인터 이동이 선택 범위를 늘린다.
        // 스크롤 막대의 끌기와 달리 element handler를 두지 않는다.
        //  - offset 계산에 측정 함수가 필요해 controller가 직접 다룬다.
        ui_element_id text_drag_id_ {};

        // 스크롤 막대처럼 누른 채 끄는 대상이다.
        // 값이 있으면 포인터 이동이 클릭· drag 대신 이 element로 간다.
        // 좌표는 마지막으로 보낸 위치다.
        ui_element_id pointer_drag_id_ {};
        float pointer_drag_x_ { 0.0f };
        float pointer_drag_y_ { 0.0f };

        // 누름이 시작된 표면이다.
        // 잡은 대상을 tree가 다시 빌드된 뒤에도 같은 표면에서 찾는다.
        std::u8string pressed_surface_ {};

        // keyboard focus를 가진 창의 표면이다 (비어 있으면 주 창).
        // `surface_focus_gained_event`가 넣고 **상실은 건드리지 않는다** — 다른
        // 앱으로 넘어가면 우리 창이 전부 초점을 잃는데, 그때 "활성 표면 없음"으로
        // 떨어뜨리면 돌아왔을 때 진입이 어느 tree도 고르지 못한다. 마지막 값이
        // 곧 돌아갈 자리다 (active-surface-design.md).
        //  - 그 창이 닫히면 값은 낡지만 `surface_tree()`가 nullptr로 답해
        //    진입이 저절로 멈춘다.
        //  - **논리 초점이 사는 표면(`focused_surface`)과는 다른 질문의 답이다.**
        //    popup에 초점이 서면 그 값은 popup id이고 활성 표면은 앵커 창이다.
        std::u8string active_surface_ {};

        // 가둠이 사라지면 초점이 돌아갈 자리다 (비어 있으면 되돌리지 않는다).
        // 가둠이 살아 있는 동안 그 값을 들고 있다가 가둠이 없어진 판정 위에서 쓴다 —
        // 사라진 tree에는 그 이름이 더 이상 없기 때문이다
        // (focus-entry-design.md).
        //  - 사용자가 **지나간** 자리를 쌓는 이력이 아니라 앱이 **적어 준** 값의
        //    사본이라 낡지 않는다. tree가 올 때마다 다시 읽는다
        //    (focus-group-design.md이 거절한 "묶음별 기억"과 갈리는 지점이다).
        //  - 같은 성격의 선례가 `context_menu_open_`과 `ui_tree::trap_`이다.
        ui_element_id focus_return_ {};
        // 그 자리가 사는 표면이다. 되돌리기는 **적어 둔 표면이 지금 활성일 때만**
        // 발화한다 — 없으면 보조 창으로 넘어간 순간 "활성 표면에 가둠이 없다"가
        // 참이 되어, 아직 서 있는 주 창 가둠의 되돌리기가 터진다
        // (active-surface-design.md).
        std::u8string focus_return_surface_ {};

        // 클릭은 같은 대상 위의 누름과 뗌이다.
        // 비활성 element는 누름부터 무시한다.
        ui_element_id pressed_id_ {};
        pointer_button pressed_button_ { pointer_button::none };
        float pressed_x_ { 0.0f };
        float pressed_y_ { 0.0f };
        bool drag_candidate_ { false };
        // 누름 표시가 내부 끌기로 바뀌어도 시퀀스 소유권은 뗌·취소까지 남는다.
        // 다른 장치의 hover나 늦은 뗌이 잡은 조작을 움직이거나 끝내면 안 된다.
        struct pointer_contact
        {
            pointer_device device { pointer_device::mouse };
            std::uint32_t id { 0 };
            std::u8string surface {};
            pointer_button button { pointer_button::none };
        };
        std::optional<pointer_contact> pointer_contact_ {};

        std::optional<touch_contact> touch_ {};
        struct view_drag
        {
            ui_element_id id {};
            float start_x { 0.0f };
            float start_y { 0.0f };
            float x { 0.0f };
            float y { 0.0f };
        };
        std::optional<view_drag> view_drag_ {};

        // 더블 클릭 판정: 직전 클릭의 대상·표면·시각·위치다.
        // `click_streak_`은 같은 자리를 연달아 누른 횟수로,
        // 텍스트 박스에서 2는 낱말·3 이상은 전체 선택이다.
        //  - **표면도 함께 본다.** 좌표는 표면마다 자기 client 기준이고 id는 tree
        //    안에서만 안정적이라, 표면을 빼면 A창을 누른 직후 B창의 같은 id를 같은
        //    자리에서 누르는 것이 연타가 된다 — 각 창은 한 번씩 눌렸는데
        //    더블 클릭이 돈다 (multi-window-design.md).
        //  - **장치도 함께 본다.** 마우스 클릭과 터치 탭, 펜 접촉은 서로 합쳐
        //    더블 클릭이 되지 않는다. 터치 포인터 id는 접촉마다 달라 보지 않는다.
        std::size_t click_streak_ { 0 };
        ui_element_id last_click_id_ {};
        pointer_device last_click_device_ { pointer_device::mouse };
        std::u8string last_click_surface_ {};
        std::chrono::steady_clock::time_point last_click_time_ {};
        float last_click_x_ { 0.0f };
        float last_click_y_ { 0.0f };

        // 묶음 안 글자 탐색의 누적 질의다.
        // 그려지지 않으므로 snapshot이 아니라 controller가 든다 —
        // snapshot에 있는 것(메뉴 강조)은 그리는 쪽이 보아야 하는 값이다.
        std::u8string typeahead_query_ {};
        std::chrono::steady_clock::time_point last_typeahead_time_ {};
        // 그 질의로 옮겨 온 자리다. 초점이 다른 길로 옮겨 가면 질의를 잊는다.
        ui_element_id typeahead_focus_ {};

        // 컨텍스트 메뉴의 열림 상태다.
        // 여닫는 edge에서 키보드 강조를 지운다.
        bool context_menu_open_ { false };

        // 마지막으로 본 포인터 위치·시각·표면이다.
        // 포인터가 머문 채 내용만 스크롤되면 (휠) 새
        // tree를 받을 때 이 자리로 hover를 다시 판정한다.
        // 창을 벗어나면 무효가 된다.
        bool pointer_inside_ { false };
        float last_pointer_x_ { 0.0f };
        float last_pointer_y_ { 0.0f };
        std::chrono::steady_clock::time_point last_pointer_time_ {};
        std::u8string last_pointer_surface_ {};
    };

    // input thread의 소비 루프다.
    // raw input 채널이 닫히면 반환한다.
    // tree는 처리 직전에 최신 것으로 갱신하고, interaction snapshot은 바뀔 때만 게시한다.
    // 앱 메시지는 app inbox로 가고,
    // `ui_command`·`app_ui_command`·클립보드 요청은 UI thread 전용이라 callback으로 넘긴다.
    // `touch_slot`이 있으면 실행 중 바뀐 터치 설정을 다음 이벤트 처리 전에 적용한다.
    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot,
        messaging::latest_slot<surface_tree_list>& surface_tree_slot, messaging::channel<app_message>& app_inbox, messaging::latest_slot<interaction_snapshot>& interaction_slot,
        const std::function<void(ui_command)>& execute_ui_command, interaction_policy* policy = nullptr, interaction_config config = {},
        const std::function<void(app_ui_command)>& execute_app_ui_command = {}, const std::function<void(clipboard_request)>& execute_clipboard = {},
        messaging::latest_slot<touch_gesture_config>* touch_slot = nullptr);
} // namespace luil

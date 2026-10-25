#pragma once

#include "luil/ui/font_scale.h"
#include "luil/ui/text_input_options.h"

#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/ui_cursor.h"
#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_events.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class SkCanvas;
class SkTypeface;

namespace luil {
    struct rect_f
    {
        float x { 0.0f };
        float y { 0.0f };
        float width { 0.0f };
        float height { 0.0f };

        [[nodiscard]] bool contains(float point_x, float point_y) const noexcept;
    };

    // 한 점이다 (단위는 쓰는 자리가 정한다).
    struct point_f
    {
        float x { 0.0f };
        float y { 0.0f };

        [[nodiscard]] bool operator==(const point_f&) const noexcept = default;
    };

    // 폭과 높이다 (단위는 쓰는 자리가 정한다 — `preferred_size`는 논리 픽셀이다).
    struct size_f
    {
        float width { 0.0f };
        float height { 0.0f };

        [[nodiscard]] bool operator==(const size_f&) const noexcept = default;
    };

    // `outer`에서 `inner`를 뺀 띠들이다 (위·아래·왼쪽·오른쪽 차례, 넓이가 없는 띠는 뺀다). 왼쪽·오른쪽 띠는 `inner`의
    // 높이만큼이라 띠끼리 겹치지 않는다. `inner`가 `outer` 밖으로 나간 몫은 잘라서 본다.
    [[nodiscard]] std::vector<rect_f> bands_outside(const rect_f& outer, const rect_f& inner);

    // 묶음 안을 도는 방향이다.
    // 묶음은 Tab 순회에서 **한 자리**이고 그 안은 화살표로 돈다
    // (focus-group-design.md).
    enum class focus_axis
    {
        // 묶음이 아니다.
        none,
        horizontal,
        vertical,
        both,
    };

    enum class ui_trigger
    {
        left_click,
        right_click,
        double_click,
        // 가운데 버튼 클릭이다. 같은 element 위에서 누르고 뗐을 때만 서고, 끌기 문턱을 넘어 움직이면 취소된다.
        //  - 선택·초점·끌기·연타 판정을 건드리지 않는다 — 가운데 버튼은 그 자리의 부가 동작(탭 닫기)이다.
        //  - 터치에는 가운데 버튼이 없다.
        middle_click,
    };

    inline constexpr std::size_t ui_trigger_count { 4 };

    // 액션 실행 시점의 문맥이다.
    // 좌표는 물리 픽셀(창 좌표)이다.
    //
    // 수식 키는 그 동작이 일어난 순간에 눌려 있던 것이다 — 앱이 Ctrl+클릭(하나 더 고르기)과
    // Shift+클릭(범위 고르기), 놓는 순간의 Ctrl(복사·이동)을 가르는 근거다.
    //  - 클릭·더블 클릭·우클릭은 **뗀** 순간이다. 터치의 길게 누르기는 시간이 찬 순간(마지막 이동)의 값이다.
    //  - `pointer_drag_target`의 `on_press`는 누른 순간, `on_move`는 그 이동의 순간(앞 문맥은 앞 이동의 순간),
    //    `on_release`는 뗀 순간이다.
    //  - `drag_source::make_payload`는 끌기가 시작되는 이동의 순간, `drop_target::on_drop`은 놓는 순간이다.
    //  - Space·Enter로 실행한 것은 그 키의 수식 키다. Ctrl·Alt가 눌린 키는 앱 단축키라 실행 키가 되지
    //    않으므로 실제로 서는 것은 Shift뿐이다 (Shift+Space의 범위 고르기).
    //  - 보조 기술이 시킨 실행과 Esc의 닫기에는 수식 키가 없다.
    struct ui_action_context
    {
        ui_element_id element {};
        float x { 0.0f };
        float y { 0.0f };
        bool control { false };
        bool shift { false };
        bool alt { false };
    };

    // 글자 폭을 재는 함수다.
    // 텍스트 박스가 포인터 x를 글 안의 offset으로 옮길 때 쓴다.
    // 앱은 내장 글꼴로 만들고 test는 고정 폭 가짜를 넣는다.
    // 글꼴이 없는 thread에서도 부를 수 있어야 하므로 element 밖에서 주입한다.
    using text_measurer = std::function<float(std::u8string_view text, float pixel_size)>;

    // 설정된 UI 글꼴(그리기가 쓰는 그 글꼴)로 재는 측정기다. 어느 thread에서나 부를 수 있다.
    [[nodiscard]] const text_measurer& default_text_measurer();
    [[nodiscard]] const text_measurer& default_code_text_measurer();

    // 텍스트 박스가 내주는 편집 상태다.
    // 초점을 가진 박스의 선택 글을 클립보드로 복사할 때 입력 thread가 읽는다.
    // offset은 UTF-8 byte offset이다.
    // IME가 "이 글의 이 구간이 화면 어디인가"를 물을 때의 질의다.
    // text는 조합 글이 섞인 문서라 element가 든 확정 글과 다를 수 있고,
    // offset은 전부 그 문서 기준 UTF-8 byte offset이다.
    // caret은 가로 스크롤 계산의 기준이다 — 그리기와 같은 값이 나온다.
    struct text_span_query
    {
        std::u8string_view text {};
        std::size_t caret { 0 };
        std::size_t begin { 0 };
        std::size_t end { 0 };
    };

    struct text_input_snapshot
    {
        std::u8string_view text {};
        std::size_t caret { 0 };
        std::size_t anchor { 0 };
        // IME 조합 중의 표시 문서다 (확정 글 + 조합 글, caret은 그 문서 기준).
        // 클립보드·TSF 동기화는 위의 확정 글만 본다 — 조합 글은 아직 사용자의
        // 글이 아니다. 보조 기술의 Text pattern만 이쪽을 본다: 화면이 보이는
        // 그대로를 읽어야 조합 중인 글자가 들리기 때문이다.
        bool composing { false };
        std::u8string_view composition_text {};
        std::size_t composition_caret { 0 };
        std::uint64_t applied_sequence { 0 };
        // 암호 칸이다 (`text_input_config::secret`). 복사와 낱말 단위 편집을 막고 플랫폼 IME·보조 기술에 암호
        // 칸으로 알리는 근거다. 위의 글은 가리지 않은 진짜 글이다 — IME가 편집하려면 있어야 한다.
        bool secret { false };
        // Android가 키보드를 고를 때 사용한다. Windows의 키·IME 동작은 바꾸지 않는다.
        text_input_options options {};
        // 여러 줄 붙여넣기를 허용한다. 거짓이면 기존 한 줄 붙여넣기 동작을 따른다.
        bool multiline { false };
        // caret이 앞 글자 쪽에 붙는다 (`text_edit_state::caret_upstream`). 넘겨 접은 자리에서만 뜻이 있다.
        bool caret_upstream { false };
        bool read_only { false };
        bool navigation_keys_to_parent { false };
    };

    // 포인터 자리에 놓을 caret이다. `upstream`이면 그 offset의 앞 글자 쪽(넘겨 접은 윗줄의 끝)에 붙는다.
    struct text_caret_position
    {
        std::size_t offset { 0 };
        bool upstream { false };

        [[nodiscard]] bool operator==(const text_caret_position&) const noexcept = default;
    };

    // 액션은 상태를 바꾸지 않고 후속 메시지를 반환한다.
    using ui_action = std::function<std::vector<input_action>(const ui_action_context&)>;

    // "실행되면 이 메시지 하나"인 액션이다.
    // set_action과 config의 액션 자리에 바로 꽂는, 가장 흔한 꼴의 도우미다.
    // 좌표가 필요한 액션은 여전히 lambda를 직접 쓴다.
    template<typename intent_type>
    [[nodiscard]] ui_action make_message_action(intent_type intent)
    {
        return [intent = std::move(intent)](const ui_action_context&) -> std::vector<input_action> { return { make_app_action(intent) }; };
    }

    // 보조 기술이 시킨 실행 하나를 이 요소의 메시지로 옮긴다 (`ui_element::set_access_handler`).
    // 답의 규약은 `ui_element::access_actions`와 같다 — 없으면(nullopt) 기본 실행에 맡기고, 빈 목록이면 할 일이 없다.
    using access_handler = std::function<std::optional<std::vector<input_action>>(const access_request& request)>;

    // 더 안쪽이 소비하지 않은 키 하나를 받는다 (`ui_element::set_key_handler`, key-propagation-design.md).
    //
    // **가장 작은 단위가 먼저 받고, 소비하지 않은 키는 위로 흐른다.** 초점이 선 element부터 root까지
    // 차례로 묻고 처음 답한 것에서 멈춘다. 아무도 답하지 않으면 앱 정책(`interaction_policy::on_key`)으로 간다.
    //  - 답의 규약은 `key_step_target`과 같다. nullopt면 소비하지 않았다(위로 흐른다), 빈 목록이면
    //    소비했고 낼 메시지가 없다.
    //  - 텍스트 칸·값 단계·묶음이 먼저 가진 키는 오지 않는다. 그것들이 가장 안쪽의 소비자다.
    using key_handler = std::function<std::optional<std::vector<input_action>>(const key_pressed_event& event)>;

    struct drag_payload
    {
        ui_element_id source {};
        // 끌리는 대상의 앱 정의 키다 (ui_element_id::owner와 같은 규약).
        // 여러 항목을 함께 끌면 대표 항목(통상 누른 항목)이다. 모두는 `dragged_owners`에 있다.
        std::u8string dragged_owner {};
        // drag ghost에 표시할 이름이다.
        // 비어 있으면 dragged_owner를 대신 쓴다.
        std::u8string label {};
        // 참이면 tree의 기본 ghost를 그리지 않는다.
        //  - 출발 element가 자기 방식으로 drag를 표시한다.
        //  - **누르는 것은 ghost 하나다.** 수락 대상의 강조는 그대로 선다 —
        //    놓을 자리를 아는 것은 이쪽뿐이라, 그림을 밖(탐색기)이 그리는 파일
        //    끌기에도 강조는 우리 몫이다 (os-dragdrop-design.md).
        bool custom_visual { false };
        // drag 시작 시 포인터와 출발 element 원점의 차이다.
        // 끌리는 그림이 잡은 지점 그대로 포인터를 따라오게 한다.
        float grab_offset_x { 0.0f };
        float grab_offset_y { 0.0f };
        // OS에서 들어온 파일 끌기의 경로 목록이다.
        // 비어 있지 않으면 밖에서 온 끌기이고 source·dragged_owner는 비어 있다
        // (os-dragdrop-design.md).
        std::vector<std::u8string> files {};
        // 순서를 바꾸는 drop은 출발 종류와 소속 컨테이너를 함께 확인한다.
        // 기존 위치 초기화를 보존하기 위해 끝에 둔다. 밖에서 온 끌기는 비어 있다.
        ui_element_id container {};

        // 기본 drop 강조(테두리와 목록·탭 배경)만 끈다. 수락과 drop 동작은 유지한다.
        bool suppress_drop_highlight { false };

        // 함께 끌리는 항목들의 앱 정의 키다 (앱이 고른 여러 항목을 한 번에 끈다).
        //  - 비어 있으면 `dragged_owner` 하나를 끄는 것이다 — 지금까지의 끌기 그대로다.
        //  - 채울 때는 `dragged_owner`도 그 안에 넣는다. 기본 ghost는 대표 항목의 이름과 나머지 개수(`+N`)를
        //    보인다 (`drag_ghost_label_of`).
        //  - 받는 쪽(`drop_target`)은 이 목록으로 무엇이 왔는지 알고, 놓는 순간의 수식 키(`ui_action_context`의
        //    `control`)로 옮기기·복사를 가른다.
        std::vector<std::u8string> dragged_owners {};
        // 놓을 자리가 없는 곳 위의 포인터 모양이다.
        //  - `inherit`(기본)이면 지금처럼 끄는 동안 잡은 쪽의 모양이 이어진다.
        //  - `not_allowed`를 주면 받는 대상이 없는 자리(놓을 수 없는 행·빈 곳) 위에서 금지 모양이다. 받는 대상 위에서는
        //    잡은 쪽의 모양이다.
        ui_cursor rejected_cursor { ui_cursor::inherit };
        // 포인터가 이 앱의 어느 창 위에도 없을 때의 모양이다 (창 밖으로 끌어 내 새 창으로 떼어 낼 자리).
        //  - `inherit`(기본)이면 잡은 쪽의 모양이 이어진다. 앱이 정의한 모양(`application_cursor`)을 주면 "떼어 낸다"를
        //    알아볼 수 있다. 실제 커서는 `window_config::resolve_cursor`가 준다.
        //  - 창 밖에서는 ghost도 화면 위를 따로 따라온다 (`custom_visual`이 아닐 때 — multi-window-design.md).
        ui_cursor outside_cursor { ui_cursor::inherit };

        // 이 끌기가 그 키의 항목을 싣고 있는가 (`dragged_owner` 또는 `dragged_owners`).
        [[nodiscard]] bool carries(std::u8string_view owner) const noexcept;
        // `from` 목록의 그 키 항목을 싣고 있는가다. 키는 목록 안에서만 유일하므로, 두 목록 사이에서 끌 때 받는
        // 목록의 같은 키 행을 끌리는 것으로 오인하지 않게 출발 목록(`container`)까지 견준다.
        //  - `container`가 비어 있으면(앱이 직접 지은 payload) 키만 견준다 — 지금까지의 `carries`다.
        //  - 목록(`list_element`·`virtual_list_element`)은 행의 payload에 자기 id를 `container`로 채운다. 받는
        //    목록의 `accepts_drop`이 자기 자신 위의 놓기를 거를 때 이것을 쓴다.
        [[nodiscard]] bool carries(const ui_element_id& from, std::u8string_view owner) const noexcept;
        // 끌리는 항목 수다. `dragged_owners`가 비어 있으면 `dragged_owner`가 있을 때 1이다.
        [[nodiscard]] std::size_t dragged_count() const noexcept;

        [[nodiscard]] bool operator==(const drag_payload&) const = default;
    };

    // 받는 대상 없이 끝난 놓기다 (`drag_source::on_unclaimed_drop`).
    // 놓은 자리를 두 가지로 준다 — 어느 표면 위였는지와 그 표면 좌표, 그리고 화면 좌표다.
    struct drag_release
    {
        // 놓은 자리의 표면이다 (표면 표식의 통상 규칙 — 비면 주 창이다).
        //  - `outside`면 끌기가 시작된 표면이다. 아래 좌표도 그 표면 기준이라 창 밖의 값일 수 있다.
        std::u8string surface {};
        // 그 표면의 client 좌표다 (물리 픽셀).
        float x { 0.0f };
        float y { 0.0f };
        // 이 앱의 어느 표면 위도 아니다 (바탕화면·다른 앱·작업 표시줄 위). 탭을 새 창으로 떼어 낼 자리다.
        //  - 거짓이면 이 앱의 창 위였지만 받는 대상이 없는 자리다 (다른 창의 빈 곳). 그 창에 합칠지는 앱이 정한다.
        bool outside { false };
        // 놓은 자리의 화면 좌표다 (가상 화면 물리 픽셀). 모르는 플랫폼은 비운다.
        //  - 새 창을 그 자리에 세우려면 `ui_window::screen_anchor`에 그대로 넘긴다.
        std::optional<screen_point> screen {};
        // 놓는 순간의 수식 키다 (`ui_action_context`와 같은 규약).
        bool control { false };
        bool shift { false };
        bool alt { false };

        [[nodiscard]] bool operator==(const drag_release&) const = default;
    };

    struct drag_source
    {
        std::function<drag_payload(const ui_action_context&)> make_payload {};
        // 받는 대상 없이 놓였다 (창 밖에 놓았다, 창 안이지만 받는 element가 없는 자리다).
        //  - **취소에는 부르지 않는다.** Esc, 캡처 상실, 시스템의 접촉 취소, 입력 큐 유실, 출발 표면의 소멸은 놓기가
        //    아니다. 불투명 표면(modal·메뉴)이 가린 자리에 놓은 것도 부르지 않는다 — 그 자리는 막힌 것이지 빈 것이
        //    아니다 (OS 파일 drop의 창 전체 물러섬과 같은 규칙, os-dragdrop-design.md).
        //  - 끌기가 **시작될 때** 쥐어 둔다. 출발 element가 끄는 동안의 새 tree에서 사라져도 이 값으로 알린다.
        //  - 비어 있으면 지금처럼 아무것도 내지 않는다.
        std::function<std::vector<input_action>(const drag_payload&, const drag_release&)> on_unclaimed_drop {};
    };

    // 순서가 있는 항목의 어느 쪽에 놓는가다 (항목의 앞 반쪽이면 앞, 뒤 반쪽이면 뒤).
    enum class drop_side
    {
        before,
        after,
    };

    struct drop_target
    {
        std::function<bool(const drag_payload&)> accepts {};
        std::function<std::vector<input_action>(const drag_payload&, const ui_action_context&)> on_drop {};
        // 자리마다 받기를 가른다 (같은 항목이라도 앞 반쪽은 받고 뒤 반쪽은 받지 않는 식).
        //  - 있으면 `accepts`가 참인 뒤에 이것도 참이어야 받는다. 좌표는 그 표면의 client 물리 픽셀이다.
        //  - 받지 않은 자리는 아래의 대상으로 내려가 다시 찾는다 (보통 element처럼 지나친다).
        //  - 끄는 동안 이동마다 불린다. `accepts`처럼 가볍게 둔다.
        std::function<bool(const drag_payload&, float x, float y)> accepts_at {};
        // 받는 쪽이 자기 강조를 그린다 (항목 사이의 틈 표시 등). 기본 강조 테두리를 그리지 않는다.
        //  - 끄는 쪽이 끈 강조(`drag_payload::suppress_drop_highlight`)와는 다른 자리의 판단이다 — 그쪽은 모든
        //    대상의 기본 강조를 끄고, 이쪽은 이 대상의 테두리만 자기 그림으로 바꾼다.
        bool custom_highlight { false };
    };

    class ui_element;

    // drop 대상을 못 찾은 자리와 위쪽 불투명 요소가 가린 자리를 구별한다.
    // OS 파일 drop은 전자에서만 창 전체 처리로 물러선다.
    struct drop_hit_result
    {
        const ui_element* target { nullptr };
        bool blocked { false };
    };

    // 끌기를 놓은 순간의 속도다 (초당 물리 픽셀, 오른쪽·아래가 양수).
    //  - 놓기 전 100ms 동안의 이동으로 잰다. 그 사이 이동이 둘보다 적으면(멈췄다 놓았다, 누르고 바로 뗐다) 0이다.
    //  - 놓은 자리는 넣지 않는다. 마우스는 capture를 잃으면 화면 밖 자리의 합성 뗌으로 끝난다.
    //  - 시스템의 접촉 취소·입력 큐 유실로 끝난 끌기는 0이다.
    struct drag_velocity
    {
        float x { 0.0f };
        float y { 0.0f };

        [[nodiscard]] bool operator==(const drag_velocity&) const noexcept = default;
    };

    // 누른 채 끄는 동안 연속으로 반응하는 element다 (스크롤 막대).
    // 일반적인 drag & drop과 달리 ghost도 drop 대상도 없고, 포인터 이동을 그때그때 메시지로 바꾼다.
    // 눌린 동안에는 포인터가 element를 벗어나도 계속 호출된다.
    //
    // 기본(`start_distance` 0)은 누르는 순간 끌기다 — 그 element에는 클릭이 오지 않는다. 시작 거리를 두면
    // 클릭과 함께 산다 (목록 행: 누르고 떼면 고르기, 누른 채 끌면 범위 정하기).
    struct pointer_drag_target
    {
        // 눌린 순간 한 번이다.
        // 누른 지점으로 즉시 이동할지 element가 정한다.
        std::function<std::vector<input_action>(const ui_action_context&)> on_press {};
        // 직전 위치와 현재 위치를 받는다.
        // 상대 변화량만 쓰므로 tree가 다시 빌드되어도 이어서 끌 수 있다.
        std::function<std::vector<input_action>(const ui_action_context& previous, const ui_action_context& current)> on_move {};
        // 끌기가 끝났다 (뗌, 시스템의 접촉 취소, 입력 큐 유실로 거둔 몸짓). 마지막 자리와 놓은 순간의 속도를 받는다.
        // 끄는 동안의 상태를 앱이 들고 있다가 여기서 멈출 자리를 정한다 (시트의 멈춤점, 튕김).
        //  - 비어 있으면 끝을 알리지 않는다 — 지금까지의 손잡이가 그 경우다.
        //  - 누른 element가 사라졌거나 가려진 뒤의 뗌에는 부르지 않는다.
        std::function<std::vector<input_action>(const ui_action_context&, drag_velocity)> on_release {};
        // 손잡이가 포인터를 따라 자리를 옮긴다 (시트의 끌기 손잡이, 판 경계의 나눔 손잡이). 참이면 끄는 동안(마우스·펜·터치)
        // 이 손잡이가 살아 있는지를 누른 자리의 hit이 아니라 id·가시성·가둠으로 본다 — 손잡이가 옮겨 가면 누른 자리는
        // 곧 다른 것(scrim, 넓어진 판)에 덮인다.
        //  - 기본은 거짓이다. 제자리의 손잡이(스크롤 막대)는 지금처럼 누른 자리를 덮은 것을 끊는다.
        bool follows_pointer { false };
        // 끌기가 시작되는 거리다 (창 좌표의 물리 픽셀, `interaction_config::drag_start_distance`와 같은 단위).
        //  - 0(기본)이면 지금처럼 누르는 순간 `on_press`이고 클릭은 없다.
        //  - 0보다 크면 마우스·펜은 누른 자리에서 이 거리를 넘기 전까지 보통 누름이다. 그 안에서 떼면 끌기는 없었던
        //    것이 되고 평소처럼 클릭·더블 클릭·우클릭 트리거가 나간다. 넘는 순간 `on_press`를 **누른 자리**의 좌표와
        //    누른 순간의 수식 키로 부르고, 이어서 같은 이동의 `on_move`(앞 문맥이 누른 자리)를 부른다. 그 뒤는 지금
        //    계약 그대로 `on_move`·`on_release`이고, 끌기가 선 뒤에는 시작 자리로 돌아와 떼도 클릭이 아니다.
        //  - 터치는 이 거리 대신 터치 몸짓의 규칙을 따른다. 축에 맞는 빠른 쓸기는 감싼 창의 스크롤이고, 허용치
        //    (`touch_gesture_config::press_move_tolerance`) 안에서 떼면 탭(클릭·두 번 탭), 움직이지 않고 오래 누르면
        //    길게 누르기(우클릭)다. 스크롤이 될 수 없게 된 뒤(시간 창이 지났거나 흘릴 창이 없다) 허용치를 넘기면
        //    그때 끌기가 선다 — 오래 잡았다 옮기는 것이 끌기라는 일반 drag와 같은 규칙이다.
        float start_distance { 0.0f };
        // 끄는 동안 이 element를 감싼 흘리는 창의 가장자리에 포인터를 대면 그 창이 스스로 흐른다
        // (`interaction_config::auto_scroll`, 끌어서 범위 정하기).
        //  - 내용이 흐르면 포인터가 멈춰 있어도 `on_move`를 다시 부른다. 앞·지금 문맥은 같은 **표면 좌표**다 —
        //    좌표는 언제나 표면(창) 좌표이고, 흐른 만큼은 앱이 든 스크롤 위치에 이미 들어가 있다. 스크롤 메시지가
        //    다시 부른 `on_move`의 메시지보다 먼저 나가므로, 표면 좌표를 자기 스크롤 위치로 내용 좌표로 옮기는
        //    앱은 흐른 만큼 늘어난 범위를 얻는다.
        //  - 기본은 끔이다. 스크롤 막대·나눔 손잡이처럼 변화량만 쓰는 손잡이에 같은 자리의 이동이 오지 않는다.
        bool auto_scroll { false };
    };

    // 키가 값을 한 칸 옮기는 방향이다.
    // 화살표는 축으로 갈리고 Page·Home·End에는 방향이 없다 (묶음의 Home/End와
    // 같은 규칙 — 방향이 없는 키라 축으로 가릴 것이 없다).
    //  - 이름이 `key_step`이 아닌 이유는 접근자와 부딪히기 때문이다. element 안에서
    //    `key_step`은 상속받은 **멤버 함수**로 먼저 보인다 (클래스 범위가 namespace
    //    범위를 가린다).
    enum class value_step
    {
        decrease,
        increase,
        decrease_page,
        increase_page,
        minimum,
        maximum,
    };

    // 단계 키에 함께 눌린 수식 키의 뜻이다 (`key_step_target::on_step_with`).
    //
    // 목록의 키보드 범위 선택이 이것으로 선다. Shift+↓와 ↓가 같은 `value_step`이라 걸음만으로는
    // "커서를 옮겨라"와 "기준점부터 커서까지 골라라"를 가를 수 없다.
    //  - `extend`는 Shift다. 기준점(anchor)을 둔 채 커서까지 고른 구간을 늘린다.
    //  - `keep_selection`은 Ctrl이다. 고른 것을 그대로 두고 커서만 옮긴다 (그 자리에서 Ctrl+Space로 더하거나 뺀다).
    //  - 둘 다 참이면 지금 고른 것에 기준점부터 커서까지를 **더한다**.
    struct key_step_context
    {
        bool extend { false };
        bool keep_selection { false };

        [[nodiscard]] bool operator==(const key_step_context&) const = default;
    };

    // 초점을 가진 채 키로 값을 바꾸는 element다 (막대·스크롤 막대·손잡이).
    //
    // **초점은 제자리에 남는다** — 묶음(`focus_axis`)이 항목 **사이로** 초점을
    // 옮기는 것과 갈리는 자리다. 끌기와 같은 규약으로 값이 아니라 변화량만 나른다.
    struct key_step_target
    {
        // 이 element가 가져가는 화살표의 축이다.
        // `none`이면 화살표는 남의 것이고 Page·Home·End만 본다.
        focus_axis axis { focus_axis::none };
        // 답이 없으면(nullopt) 그 걸음은 이 element의 것이 아니라 키가 그대로
        // 흐른다 — 자기 범위를 모르는 손잡이의 Home/End가 그렇다.
        // 빈 목록은 "내 것이고 삼켰다"이다 (controller의 `process_*`와 같은 규약).
        std::function<std::optional<std::vector<input_action>>(value_step step)> on_step {};
        // 수식 키의 뜻까지 받는 짝이다. 있으면 `on_step` 대신 부른다.
        //  - **Ctrl과 함께 누른 단계 키도 이 element의 것이 된다.** `on_step`만 든 element에서 Ctrl+화살표는
        //    앱 단축키로 흐르지만, 이것을 든 element는 그것을 "선택을 두고 커서만 옮기기"로 쓴다.
        //    Alt·Meta와 함께인 키는 여전히 앱 단축키다.
        //  - `on_step`만 든 element에는 Shift가 지금처럼 보이지 않는다 (막대의 Shift+→는 그냥 →다).
        std::function<std::optional<std::vector<input_action>>(value_step step, const key_step_context& context)> on_step_with {};
    };

    // 흘리는 컨테이너가 자기 스크롤을 청하는 메시지다.
    //
    // **자리표가 아니라 element가 든다.** 지금까지 휠과 초점 되살리기는 "어느
    // 것을 어떤 메시지로 흘리는가"를 앱이 지은 표(`scroll_route`)에서 찾았다.
    // 표는 계기마다 한 벌씩 필요했고 — 휠은 좌표가 **덮는** 것을, 되살리기는
    // 초점을 **품는** 것을 묻는다 — 두 표의 줄이 어긋나면 마지막 행이 막대 밑에
    // 남거나 초점이 화면 밖에 선다. 컨테이너가 자기 메시지를 들고 있으면 두
    // 질문이 같은 답을 본다 (`route_wheel`·`route_reveal`의 표 없는 짝).
    //  - `drag_source`·`drop_target`·`key_step_target`과 같은 자리·같은 규약이다.
    //    비어 있으면 그 역할이 없는 것이라 아무도 이 컨테이너를 흘리지 않는다.
    // 흘리는 컨테이너의 축이다.
    // 터치 끌기는 손가락이 움직인 축과 같은 컨테이너만 흘린다.
    enum class scroll_axis
    {
        vertical,
        horizontal,
    };

    struct zoom_point
    {
        float x { 0.0f };
        float y { 0.0f };
        [[nodiscard]] bool operator==(const zoom_point&) const noexcept = default;
    };

    enum class zoom_wheel_mode
    {
        zoom,
        pan,
    };

    // 확대 보기에서 터치·펜 접촉 하나가 경계에 막힌 쓸기다 (zoom-view-design.md의 경계 쓸기).
    // 접촉이 끝날 때 판정 기준을 넘은 축이 하나라도 있으면 보기가 이 값을 실은 메시지를 낸다. 실제로 옮겨진
    // 몫은 이미 `pan_by`로 나갔고, 여기는 막혀 버려진 몫만 다룬다.
    //  - 값은 보기의 논리 픽셀이고 부호는 손가락 방향이다 (`pan_by`의 이동량과 같다). 왼쪽으로 쓸다 오른쪽
    //    끝에 막혔으면 `blocked.x`가 음수이고 `horizontal`이 -1이다 — 왼쪽에서 오른쪽으로 읽는 쪽 보기라면
    //    다음 쪽이다.
    struct zoom_edge_swipe
    {
        // 그 접촉 동안 경계에 막혀 버려진 이동량이다.
        zoom_point blocked {};
        // 뗄 때의 손가락 속도다 (논리 픽셀/초).
        zoom_point velocity {};
        // 판정 기준을 넘은 축의 손가락 방향이다. -1은 왼쪽·위, 1은 오른쪽·아래, 0은 그 축의 쓸기가 아니다.
        int horizontal { 0 };
        int vertical { 0 };

        [[nodiscard]] bool operator==(const zoom_edge_swipe&) const noexcept = default;
    };

    // 경계 쓸기의 판정 기준이다. 한 축이 쓸기가 되려면 셋을 모두 넘어야 한다.
    //  - 그 축으로 `minimum_distance` 이상 막혔다 (손가락 떨림이 아니다).
    //  - 그 축으로 `minimum_distance`보다 적게 옮겨졌다 — 이미 끝에 있던 축이다. 확대한 상태에서 끝까지 끈
    //    접촉은 쓸기가 아니고, 끝에 닿은 뒤 한 번 더 쓸어야 한다.
    //  - 뗄 때 막힌 쪽으로 `minimum_velocity` 이상 빨랐다 — 끌다 멈춰 뗀 것(마음을 바꾼 것)은 쓸기가 아니다.
    struct zoom_edge_swipe_config
    {
        float minimum_distance { 32.0f };
        float minimum_velocity { 300.0f };

        [[nodiscard]] bool operator==(const zoom_edge_swipe_config&) const noexcept = default;
    };

    // 상태는 앱이 소유한다. 콜백은 최신 tree의 메시지 factory다.
    struct zoom_source
    {
        std::function<input_action(float factor, zoom_point anchor)> zoom_by {};
        std::function<input_action(zoom_point delta)> pan_by {};
        float scale { 1.0f };
        zoom_wheel_mode wheel { zoom_wheel_mode::zoom };
        std::function<std::optional<std::vector<input_action>>(const key_pressed_event&)> on_key {};
        // 발행된 상태에서 `offset`만큼 옮겨 간 뒤 `delta`를 더 옮기면 실제로 옮겨지는 몫이다 (경계 다듬기).
        // controller는 누를 때 이것을 쥐고 접촉 동안 보낸 이동을 차례로 견줘 막힌 몫을 잰다 — 발행본이 몇 frame
        // 늦어도 앱이 같은 식으로 다듬은 결과와 같다.
        std::function<zoom_point(zoom_point offset, zoom_point delta)> pan_room {};
        // 경계 쓸기의 메시지다. 비어 있으면 재지도 알리지도 않는다.
        std::function<input_action(const zoom_edge_swipe&)> edge_swipe {};
        zoom_edge_swipe_config edge_swipe_config {};
        // 두 번 탭 확대의 액션이다. 기준점은 둘째 탭의 자리다 (보기 가운데 기준 논리 픽셀). 비어 있으면 두 번 탭은
        // 확대가 아니다.
        std::function<std::vector<input_action>(zoom_point anchor)> double_tap {};
    };

    struct scroll_source
    {
        // 스크롤 위치를 이만큼 옮기자는 메시지다 (delta는 논리 픽셀).
        // 부호는 휠과 같다: 양수 = offset 증가 = 내용이 위로(가로면 왼쪽으로) 간다.
        std::function<input_action(float delta)> scroll {};
        // 논리 스크롤 변화량을 배치의 물리 좌표로 옮기는 배율이다.
        // arrange에서 실제 배율을 넣는다. 중첩 reveal이 이동 후 위치를 계산한다.
        float scale { 1.0f };
        // 기존 초기화가 세로를 뜻하도록 뒤에 둔다.
        // 세로 휠은 축을 묻지 않는다 — 가로 띠 위에서 굴린 휠이 그 띠를 흘리는 것이
        // 앱이 표로 지어 오던 동작이다. 가로 휠은 가로 창만 받는다 (`route_wheel`의 이벤트 짝).
        scroll_axis axis { scroll_axis::vertical };
        // 내용이 처음(맨 위·맨 왼쪽)에 있다. 앱이 위치를 들고 있으므로 앱이 싣는다.
        //  - 바깥의 `pan_handoff_target`(시트)이 이 값을 보고, 처음에 있는 창을 처음 쪽으로 끄는 터치를 넘겨받는다.
        //    기본은 거짓이라 넘겨받지 않는다 — 지금까지의 창이 그 경우다.
        bool at_start { false };
        // 내용이 끝(맨 아래·맨 오른쪽)에 있다. 관성 스크롤이 이 방향에서 멈춘다.
        bool at_end { false };
        // 휠을 칸 단위로 넘기는 메시지다 (`steps`는 넘길 칸 수, 양수 = 앞으로). 비어 있으면 휠은 `scroll`로 양만큼 흘린다.
        //  - 휠 한 칸(120)마다 한 칸이다. 터치패드처럼 잘게 오는 휠은 controller가 쌓아 두었다가 한 칸이 차면 낸다 —
        //    tree는 frame마다 새로 서므로 쌓는 자리는 element가 아니라 controller다.
        //  - 칸을 항목 경계에 맞춘 변화량으로 바꾸는 것은 이 메시지를 지은 쪽이다 (탭 막대는 다음 탭이 온전히 드러나는
        //    자리로 옮긴다). 그래서 `scroll`과 따로다 — 터치 끌기와 초점 되살리기는 지금처럼 양으로 흘린다.
        std::function<input_action(int steps)> step {};
        // 절대 자리(`scroll_to`)는 여기 두지 않는다. 휠도 되살리기도 변화량으로만
        // 말하고, 절대 자리를 읽는 것은 보조 기술뿐이라 그 값은 그것을 실제로
        // 내주는 자리(`scrollbar_config::scroll_to`)에 있다 — 아무도 읽지 않는
        // 칸을 두면 채운 앱이 켜졌다고 믿는다.
    };

    // 안쪽 흘리는 창이 처음에 있을 때 처음 쪽으로의 터치 끌기를 넘겨받는 컨테이너다 (아래 시트를 끌어 내리기).
    //  - 터치가 그 축으로 흐르기 시작하는 순간(축 판정) 한 번 정한다. 누른 자리에 이 컨테이너 안의 흘리는 창이
    //    없거나, 있어도 처음(`scroll_source::at_start`)에 있고 손가락이 처음 쪽(아래·오른쪽)으로 움직이면 이
    //    컨테이너가 받는다. 그 밖에는 지금처럼 안쪽 창이 흐른다. 접촉 도중에는 넘기지 않는다.
    //  - 마우스·펜에는 몸짓이 없다. 끄는 손잡이(`pointer_drag_target`)가 따로 있다.
    struct pan_handoff_target
    {
        scroll_axis axis { scroll_axis::vertical };
        // 물리 픽셀 / 논리 픽셀이다.
        float scale { 1.0f };
        // 끈 만큼이다 (논리 픽셀, 손가락 방향 — 아래·오른쪽이 양수).
        std::function<input_action(float delta)> drag {};
        // 손을 뗐거나 접촉이 취소됐다. 뗌 이벤트의 마지막 이동과 놓은 순간 축 방향의 속도를 받는다 (논리 픽셀과
        // 초당 논리 픽셀, `drag`와 같은 방향이 양수). 마지막 이동의 `drag` 메시지는 먼저 나가지만 아직 tree에
        // 반영되지 않았으므로 정착 계산에 더한다. 취소·입력 큐 유실이면 둘 다 0이다 (`drag_velocity`).
        std::function<input_action(float final_delta, float velocity)> release {};
    };

    // 초점을 가진 채 글자로 **자기 모델 안**을 찾는 element다 (가상 목록).
    //
    // 묶음의 글자 탐색은 tree에 선 항목만 본다 (`search_label`). 창에 걸치는
    // 것만 짓는 목록에서는 그것이 곧 "보이는 것만 찾는다"가 되어, 같은 글자를
    // 쳤을 때 어디로 갈지가 지금 스크롤 자리에 달리게 된다.
    //  - 질의를 잇고 끊는 규칙(글자 이어 붙이기·시간 끊김·같은 글자 되풀이)은
    //    controller가 한곳에서 쥔다. 여기는 **무엇이 맞는가**만 답한다 — 모델을
    //    아는 것은 element뿐이고, 규칙이 둘로 갈리면 묶음의 글자 탐색과 목록의
    //    글자 탐색이 서로 다르게 걷는다.
    //  - 답이 없으면(nullopt) 맞는 것이 없다는 뜻이고, 그래도 글자는 이 element가
    //    가진다 — 맞지 않는 글자가 앱으로 새지 않는 것이 묶음과 같은 규약이다.
    struct key_search_target
    {
        // 질의로 맞는 항목의 자리로 옮기자는 메시지다.
        // `query`는 이어 친 글자 전체이고 UTF-8이다.
        //
        // `first`는 **이 글자로 질의가 새로 시작하는가**다. 참이면 지금 자리의
        // **다음**부터 찾고, 거짓이면 지금 자리부터 찾는다 — 글을 더 적은 것이지
        // 다음으로 가자는 뜻이 아니기 때문이다.
        //  - 참이 되는 것은 앞의 질의가 **끊긴** 다음이다 (시간이 지났거나 초점이
        //    다른 길로 옮겨 갔다). 같은 글자를 시간 안에 거듭 치면 질의는 `aa`로
        //    이어 붙고 그 접두로 다시 찾는다 — 묶음의 글자 탐색과 같은 규칙이다.
        //  - **controller가 준다.** 질의의 길이로 element가 되짚으면 안 된다 —
        //    UTF-8에서 한글 한 글자는 세 byte라 첫 글자부터 "이어 친 글자"가 되고,
        //    질의를 잇고 끊는 것을 쥔 쪽만 이 값을 옳게 안다.
        std::function<std::optional<std::vector<input_action>>(std::u8string_view query, bool first)> on_search {};
    };

    // 내용 크기를 재는 문맥이다 (`ui_element::preferred_size`).
    //  - 배율은 글을 물리 크기로 재고 논리 픽셀로 되돌리는 데 쓴다. 글꼴의 힌팅이 크기마다 달라 논리 크기로 잰
    //    폭은 그린 폭과 어긋날 수 있다.
    //  - 측정기와 치수는 배치 문맥에서 온다 (`arrange_context::measure`). 비어 있으면 기본값이다.
    struct measure_context
    {
        float scale { 1.0f };
        const text_measurer* measurer { nullptr };
        const ui_metrics* metrics { nullptr };

        // 이 문맥이 쓸 측정기다 (비었으면 `default_text_measurer`).
        [[nodiscard]] const text_measurer& measure_text() const;
        // 이 문맥이 쓸 스타일 치수다 (비었으면 내장 스타일).
        [[nodiscard]] const ui_metrics& style_metrics() const noexcept;
        // `font_size`(논리 픽셀) 글 한 줄의 폭이다 (논리 픽셀). 그리기와 같은 물리 크기(`scaled_font_size`)로 재서
        // 배율로 나눈다.
        [[nodiscard]] float text_width(std::u8string_view text, float font_size) const;
    };

    // 배치 문맥이다.
    // slot은 부모가 준 영역이고 scroll_offset은 논리 픽셀이다.
    struct arrange_context
    {
        rect_f slot {};
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        // 그릴 수 있는 표면 전체다 (tree 좌표, 물리 픽셀). 앱의 자리가 안전 영역이면 그 밖(상태 표시줄·제스처 막대·
        // 컷아웃 자리)까지 닿는다 — root 자리가 (0, 0)에서 시작하므로 왼쪽·위 몫은 음수다 (`window_surface`).
        //  - 비어 있으면 자리 밖을 모른다는 뜻이고 지금까지처럼 자리 안에만 그린다. 앱이 넘길 때만 쓰인다.
        //  - scrim과 시트 바탕처럼 화면 끝까지 칠해야 하는 것만 쓴다. 누르기는 지금처럼 자리 안에서만 받는다.
        //  - 자식 문맥이 그대로 잇는다. 자르는 부모(흘리는 창·확대 보기) 안에서는 그 부모가 자른다.
        rect_f surface {};
        // 배치에서 글 폭을 재는 함수다 (`text_measurer`). 비어 있으면 그리기와 같은 UI 글꼴로 잰다
        // (`default_text_measurer`) — 배치를 손으로 짜는 조립도 따로 넘길 것이 없다.
        //  - 글 폭에 따라 줄 수가 바뀌는 요소(여러 줄 칸의 자동 줄 넘김)만 쓴다. 잰 값은 배치 결과에 남기고
        //    포인터를 붙잡아 두지 않는다 — 배치가 끝나면 이 문맥은 사라진다.
        //  - Test는 고정 폭 가짜를 넣는다. Core test의 글꼴 관리자는 비어 있어 기본 측정기는 폭 0을 잰다.
        const text_measurer* measurer { nullptr };

        // 내용 크기를 재는 element가 글자 크기를 읽는 스타일 치수다 (`ui_style::metrics`).
        //  - 비어 있으면 내장 스타일의 치수다 (`default_ui_style`). 그리기의 `draw_context::metrics`와 같은 값을
        //    넘겨야 잰 폭과 그린 글이 맞는다 — 스타일을 바꾼 앱은 `make_arranged_tree`에 같은 치수를 준다.
        //  - 포인터만 들고 배치가 끝나면 잊는다 (`measurer`와 같다).
        const ui_metrics* metrics { nullptr };
        // 앱의 스타일 전체다. 컴포넌트가 자기 칸(`ui_style::tab_bar` 같은 것)을 **배치에서** 읽어 그 모양으로
        // 자리를 잡고 그린다 (tab-bar-style-design.md의 공용 스타일).
        //  - 배치에서 읽는 까닭은 모양이 자리와 입력 규칙까지 정하기 때문이다 — 닫기 단추를 숨기는 모양은 터치의
        //    탭이 그 단추를 지나가게 한다. 그림만 바꾸는 값이라도 같은 자리에서 읽어 한 tree가 한 스타일을 본다.
        //  - 비어 있으면 내장 스타일이다 (`default_ui_style`, LUIL_STYLE로 바꾼 값). 읽을 때는 `app_style()`로 묻는다.
        //  - 스타일을 실행 시점에 바꾸는 앱은 `ui_frame::style`(팔레트)과 `make_arranged_tree`(이것)에 같은 값을 준다.
        //  - 포인터만 들고 배치가 끝나면 잊는다 (`metrics`와 같다).
        const ui_style* style { nullptr };

        // 이 배치가 쓸 측정기다.
        [[nodiscard]] const text_measurer& measure_text() const;
        // 이 배치가 쓸 앱 스타일이다 (비었으면 내장 스타일).
        [[nodiscard]] const ui_style& app_style() const noexcept;
        // 이 배치로 내용 크기를 재는 문맥이다 (`ui_element::preferred_size`).
        //  - 머리글에 둔다. 묻기만 하는 컨테이너가 기본 측정기(글꼴 등록부)까지 링크로 끌어오지 않는다
        //    (`text_measurer.cpp`의 경계).
        [[nodiscard]] measure_context measure() const noexcept
        {
            return { scale, measurer, metrics };
        }

        // 같은 배율·scroll_offset으로 자식에게 줄 문맥이다.
        // 자리만 새로 준다 — 자식 문맥을 손으로 지으면 뒤의 값을 빠뜨리기 쉽고,
        // 빠뜨려도 그 자리에서는 아무 일도 일어나지 않는다. 몇 계층 아래의
        // sticky 머리행이 조용히 어긋날 뿐이다 (tree-arrange-design.md).
        //  - 문맥에 새 값이 생겨도 모든 부모가 저절로 잇는다.
        [[nodiscard]] arrange_context for_child(const rect_f& child_slot) const noexcept
        {
            return { child_slot, scale, scroll_offset, surface, measurer, metrics, style };
        }

        // 표면 전체를 아는가다 (넓이가 있는 `surface`).
        [[nodiscard]] bool knows_surface() const noexcept
        {
            return surface.width > 0.0f && surface.height > 0.0f;
        }
    };

    // 그리기 문맥이다.
    // draw 호출 동안만 유효하다.
    // now는 tooltip 지연 판정용이고 maximized는 view snapshot에 없는 창 상태라 UI thread가 채운다.
    // 가족 이름을 typeface로 옮긴다.
    // 글꼴 목록을 각 행의 글꼴로 그려 미리 보기를 만들 때 쓴다.
    // 해석은 platform이 하고 presentation은 이 interface만 안다.
    struct font_resolver
    {
        font_resolver() = default;
        font_resolver(const font_resolver&) = delete;
        font_resolver(font_resolver&&) = delete;
        font_resolver& operator=(const font_resolver&) = delete;
        font_resolver& operator=(font_resolver&&) = delete;
        virtual ~font_resolver() = default;

        // 빈 이름은 내장 글꼴이다.
        // 찾지 못하면 nullptr다.
        [[nodiscard]] virtual SkTypeface* family(std::u8string_view name) const = 0;
    };

    struct draw_context
    {
        SkCanvas& canvas;
        SkTypeface* codicon_typeface { nullptr };
        SkTypeface* ui_typeface { nullptr };
        // 고정폭 본문의 글꼴이다.
        // 설정이 없으면 UI 글꼴과 같다.
        SkTypeface* code_typeface { nullptr };
        // 글꼴 미리 보기가 쓴다.
        // 없으면 미리 보기를 UI 글꼴로 그린다.
        const font_resolver* fonts { nullptr };
        const ui_color_palette& palette;
        float scale { 1.0f };
        std::chrono::steady_clock::time_point now {};
        bool maximized { false };
        // 창이 테두리 없는 전체 화면인가다 (`maximized`와 같은 규칙으로 UI thread가 채운다).
        // 둘은 **함께 참이 되지 않는다** — 전체 화면인 동안은 최대화가 아니다.
        //  - 캡션 줄을 접거나 화면 가장자리까지 그림을 넓히는 element가 이것을 본다.
        //    tree를 다시 짓는 것(캡션 줄을 아예 빼는 것)은 앱의 몫이고, 그 앱은 같은
        //    사실을 배치 메시지(`window_placement::fullscreen`)로 받는다.
        bool fullscreen { false };
        // 스타일의 치수다 (`ui_style::metrics`). 그리기 안에 박혀 있던 글자 크기·모서리가
        // 여기서 온다. 기본값은 내장 스타일과 같아 채우지 않은 context도 지금까지의 그림이다.
        ui_metrics metrics {};
        // 시스템의 움직임 줄이기 설정이다 (Win32는 클라이언트 영역 애니메이션을 끈 것).
        //  - 참이면 돌아가는 표시가 멈춘 그림으로 선다. `update_context::reduce_motion`과 같은 값이라야 한다 —
        //    멈춘 그림에 다음 frame을 청하면 헛도는 timer만 남는다.
        bool reduce_motion { false };
    };

    // update 판정의 문맥이다.
    // 그리기와 같은 시계(steady_clock)를 쓰므로 next_update가 예고한 시각에
    // 다시 그리면 draw_context::now가 그 시각을 지나 있다.
    struct update_context
    {
        std::chrono::steady_clock::time_point now {};
        // 움직임 줄이기다 (`draw_context::reduce_motion`). 참이면 돌아가는 표시는 다음 frame을 청하지 않는다.
        bool reduce_motion { false };
    };

    struct drag_visual
    {
        drag_payload payload {};
        float x { 0.0f };
        float y { 0.0f };
        // 현재 위치에서 payload를 수락하는 drop 대상이다.
        // 강조 표시에 쓴다.
        ui_element_id hovered_drop_target {};
        // 끌기가 시작된 표면이다 (비어 있으면 주 창, 값은 popup·보조 창 id).
        // 두 가지가 이 한 값을 요구한다 (multi-window-design.md).
        //  - **표시**: ghost는 좌표로만 그려져 hover·눌림과 달리 element id로
        //    갈리지 않는다. 이 표식이 없으면 열려 있는 모든 창의 같은 자리에
        //    유령 상자가 함께 그려진다.
        //  - **수명**: 그 표면이 사라지면 끌기도 죽는다. 표식이 `optional`인
        //    이 안에 있어 `reset()` 한 번에 함께 사라진다.
        //  - **꼬리에 둔다** — 위치 초기화로 만드는 자리들이 이미 있어 가운데
        //    끼우면 그 자리들의 뜻이 조용히 어긋난다.
        std::u8string surface {};
        // 포인터가 지금 놓인 표면이다. `x`·`y`와 `hovered_drop_target`은 이 표면의 것이다.
        //  - 끌기는 표면을 넘는다. 한 창에서 시작한 끌기가 다른 창 위에 오면 그 창의 tree에서 놓을 자리를 찾고,
        //    ghost와 강조도 그 창에 선다. 출발 표면에는 끌리는 항목의 흐린 표시만 남는다 (multi-window-design.md).
        //  - 비어 있으면(`std::nullopt`) 출발 표면(`surface`)이다. 처음에는 그렇다. 읽을 때는 `pointer_on()`으로 묻는다.
        std::optional<std::u8string> pointer_surface {};
        // 포인터가 `pointer_on()` 위에 없다. 그 표면은 ghost와 강조를 그리지 않는다.
        //  - 발행본에서는 포인터가 이 앱의 어느 표면 위에도 없다는 뜻이다 (바탕화면·다른 앱 위). 그때 포인터 표면은
        //    출발 표면이고 `x`·`y`는 그 표면 기준이라 창 밖의 값이다.
        //  - 표면별로 거른 것(`interaction_for_surface`)에서는 "포인터가 이 표면 밖에 있다"이다. 출발 표면은 포인터가
        //    다른 창에 가 있는 동안 이 값으로 받는다.
        bool outside { false };

        // 포인터가 놓인 표면이다 (`pointer_surface`가 비면 출발 표면).
        [[nodiscard]] const std::u8string& pointer_on() const noexcept
        {
            return pointer_surface.has_value() ? *pointer_surface : surface;
        }

        [[nodiscard]] bool operator==(const drag_visual&) const = default;
    };

    // input thread가 게시하고 UI thread가 그리기에 쓰는 상호작용 발행본이다.
    // 앱 상태가 아니라 순수한 "입력 정규화 상태"다.
    struct tooltip_interaction
    {
        ui_element_id owner {};
        std::u8string surface {};
        ui_element_id hovered {};
        ui_element_id pressed {};
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        [[nodiscard]] bool operator==(const tooltip_interaction&) const = default;
    };

    struct interaction_snapshot
    {
        ui_element_id hovered {};
        ui_element_id pressed {};
        // hover와 눌림이 난 표면이다 (비어 있으면 주 창, 값은 popup·보조 창 id).
        // **값과 짝으로 세우고 함께 비운다.**
        //  - `ui_element_id`는 **tree 안에서만** 안정적인 정체성이라 두 표면에
        //    같은 id가 사는 것이 유효하다. 라이브러리 자신이 컨테이너 owner를
        //    물려주지 않는 항목 수준 id를 만든다 — `list_row`·`menu_item`·`tab`이
        //    전부 항목 키만 쓴다. 그래서 두 창이 같은 목록을 보이면 앱의 잘못
        //    없이 id가 겹치고, 표식이 없으면 A창에서 hover한 행이 B창의 같은 키
        //    행까지 강조한다 (multi-window-design.md).
        std::u8string hovered_surface {};
        std::u8string pressed_surface {};
        // hover가 시작된 시각이다.
        // tooltip 표시 여부(지연 경과)는 그리는 쪽이 판정한다.
        std::optional<std::chrono::steady_clock::time_point> hover_started_at {};
        // tooltip을 놓을 포인터 자리다 (표면 좌표, 물리 픽셀). `hovered_surface`의 것이다.
        //  - hover가 시작될 때 세우고 지연이 끝나기 전까지 같은 element 안의 이동을 따른다. 그 뒤로는 멈춘다 —
        //    OS tooltip처럼 한 번 뜬 상자는 포인터를 따라다니지 않는다.
        //  - tooltip이 있는 element 위에서만 기록한다. 그 밖의 hover까지 세우면 이동마다 발행본이 바뀐다.
        //  - 비어 있으면(키보드·보조 기술·터치·캡션 버튼) 대상의 보이는 영역에 놓는다.
        std::optional<point_f> hover_point {};
        std::optional<drag_visual> drag {};
        // 키보드 초점이다 (element라면 무엇이든).
        // Tab이 옮기고, 자리인 element를 누르면 그리로 간다
        // (keyboard-focus-design.md).
        ui_element_id focused {};
        // 그 초점이 **텍스트 박스**일 때 같은 값이고, 아니면 비어 있다.
        // 값이 있으면 그 element가 caret을 그린다.
        // caret·문자·편집 키·TSF가 이 값을 본다 — 텍스트만의 규칙이 이 이름에 남는다.
        //  - 비어 있지 않으면 언제나 `focused`와 같다. 둘을 세우는 자리가
        //    controller 한 곳이라 어긋날 수 없다.
        ui_element_id focused_input {};
        // 초점을 가진 element가 있는 표면이다 (비어 있으면 주 창, 값은 popup·보조 창 id).
        // 그 표면의 tree에서 초점 element를 찾고, 창마다 있는 TSF host가 자기 초점인지 판정한다.
        std::u8string focused_surface {};
        // 초점을 받은 시각이다.
        // caret 깜빡임의 위상 기준이라 초점을 받는 순간 caret이 켜진 상태로 시작한다.
        std::optional<std::chrono::steady_clock::time_point> focus_started_at {};
        // 초점이 **키보드로** 옮겨졌는가.
        // 참일 때만 element가 초점 테를 그린다 — 누를 때마다 테가 남으면 시끄럽다.
        //  - caret은 이 값과 무관하다. caret은 초점 자체의 표시이지 탐색의 표시가
        //    아니라, 눌러서 잡은 초점에도 깜빡여야 한다.
        bool focus_visible { false };
        // 컨텍스트 메뉴의 키보드(↑/↓) 강조 항목이다.
        // 마우스 hover와 별개로 그려지고 Enter가 이 항목을 실행한다.
        ui_element_id menu_highlight {};
        // 그 강조가 사는 표면이다 (비어 있으면 주 창).
        //  - **초점 표면도 hover 표면도 아니다.** 메뉴 항목은 `set_tab_stop(false)`라
        //    초점을 받지 못해 초점은 메뉴를 연 자리(앵커 표면)에 남고, 포인터가
        //    메뉴 밖에 있어도 ↑/↓는 돈다. 둘 중 하나에 매면 popup에 뜬 메뉴의
        //    강조가 그냥 사라진다 — 그래서 강조는 자기 표식을 따로 든다
        //    (multi-window-design.md).
        std::u8string menu_surface {};
        // 커스텀 툴팁의 id 공간은 주 tree와 분리한다. 원래 요소의 hover는 표시 수명을 유지한다.
        std::optional<tooltip_interaction> tooltip {};

        [[nodiscard]] bool operator==(const interaction_snapshot&) const = default;
    };

    // 이 표면이 볼 상호작용 상태다.
    // 남의 표면에서 난 것은 지운다 — `ui_element_id`는 tree 안에서만 안정적이라
    // 두 표면에 같은 id가 사는 것이 유효하다 (multi-window-design.md).
    //  - **경계는 한 곳이다.** 표면 검사를 element(버튼·목록·탭·메뉴 30여 개)로
    //    퍼뜨리면 앱이 만드는 element마다 같은 실수를 되풀이한다. element는
    //    지금처럼 "이 snapshot은 내 것"이라고 믿고 id만 비교하면 된다.
    //  - 값과 표식은 짝으로 지운다. 거른 결과도 그 자체로 옳은 snapshot이라
    //    다시 걸러도 달라지지 않는다.
    //  - **값으로 받는다.** 어차피 한 벌을 만들어 지우는 일이고, 흔한 자리는
    //    `interaction_for_surface(host->acquire_interaction(), id_)`처럼 갓 집은
    //    것을 그대로 넘긴다. 그 자리에서 복사가 통째로 사라진다 — 스냅샷은
    //    문자열 넷과 끌기 payload(파일 목록)를 실은 값이고 이 함수는 표면마다·
    //    frame마다 돈다. 이미 들고 있는 것을 넘기는 자리는 예전과 같다.
    [[nodiscard]] interaction_snapshot interaction_for_surface(interaction_snapshot snapshot, const std::u8string& surface);

    inline constexpr std::chrono::milliseconds tooltip_delay { 500 };
    class ui_tree;

    // 정보 표시용 hover 콘텐츠다. 내용은 배율 1의 논리 좌표로 배치한 독립 tree이고,
    // root의 bounds가 내용 크기다. 그릴 때 소유 표면의 DPI를 적용하므로 여러 배율에서 공유할 수 있다.
    // 포인터는 내용에 먼저 전달한다. 키보드 초점·접근 tree는 분리하며 읽을 요약은 소유 요소의 set_tooltip에 둔다.
    struct tooltip_config
    {
        std::shared_ptr<const ui_tree> content {};
        std::chrono::milliseconds delay { tooltip_delay };
        float padding { 8.0f };
        float gap { 3.0f };
        float corner_radius { 4.0f };
        float shadow { 0.16f };
        // 비어 있으면 각각 tooltip_background·tooltip_border다.
        std::function<ui_color(const ui_color_palette&)> background {};
        std::function<ui_color(const ui_color_palette&)> border {};
    };

    // caret 깜빡임의 반주기다.
    // 켜짐·꺼짐이 이 간격으로 번갈아 나타난다.
    inline constexpr std::chrono::milliseconds caret_blink_interval { 530 };

    // 모든 화면 요소의 최상위 추상 클래스다.
    // 빌드 중에만 mutable이고 tree로 게시된 뒤에는 불변으로
    // 취급하므로 여러 스레드가 동시에 읽어도 안전하다.
    // "재설정"은 다음 snapshot의 tree 빌드에서 다른 값을 등록하는 것으로 달성한다.
    class ui_element
    {
    public:
        explicit ui_element(ui_element_id id) noexcept;
        virtual ~ui_element() = default;
        ui_element(const ui_element&) = delete;
        ui_element(ui_element&&) = delete;
        ui_element& operator=(const ui_element&) = delete;
        ui_element& operator=(ui_element&&) = delete;

        [[nodiscard]] const ui_element_id& id() const noexcept;
        [[nodiscard]] const rect_f& bounds() const noexcept;
        // `arrange`가 한 번이라도 불렸는지다 (`set_bounds`가 세운다).
        // **크기가 0인 것과 다르다** — 넘치지 않는 탭 막대의 넘침 버튼처럼
        // 일부러 0인 자리가 있다. 배치를 빠뜨리면 화면에서 조용히 사라지므로
        // `ui_tree`가 이 값으로 그것을 드러낸다 (tree-arrange-design.md).
        [[nodiscard]] bool arranged() const noexcept;
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] bool visible() const noexcept;
        [[nodiscard]] const std::u8string& tooltip() const noexcept;
        // 유효한 커스텀 내용이 없으면 nullptr다. 문자열은 접근성 요약과 기본 표시로 남는다.
        [[nodiscard]] const tooltip_config* tooltip_content() const noexcept;
        [[nodiscard]] bool has_tooltip() const noexcept;
        // 묶음 안 글자 탐색이 읽는 글이다 (그리기에는 쓰지 않는다).
        // 비어 있으면 그 항목은 글자로 찾을 수 없다.
        [[nodiscard]] const std::u8string& search_label() const noexcept;
        // 보조 기술이 읽을 이름이다: `set_access_name` → `search_label` → tooltip 순서.
        //
        // **툴팁으로는 이름을 줄 수 없는 자리가 있다.** 목록·탭 막대·스크롤
        // 막대처럼 자기 글을 세우지 않는 컨테이너에 이름을 주려고 툴팁을 달면
        // 화면에 글 상자가 뜬다. 그래서 화면에 뜨지 않는 이름의 자리를 따로 둔다.
        //  - 글자 탐색의 글(`search_label`)로도 물러선다. 항목이 보이는 글을
        //    거기 적어 두고 있으면 그것이 곧 사람이 부르는 이름이다.
        //  - `accessibility()`를 재정의한 element도 이 술어를 그대로 쓴다. 이름의
        //    원천이 자리마다 갈리면 앱이 어디에 무엇을 적어야 할지 알 수 없다.
        [[nodiscard]] const std::u8string& access_name() const noexcept;
        [[nodiscard]] const ui_action* action(ui_trigger trigger) const noexcept;
        [[nodiscard]] const drag_source* drag() const noexcept;
        [[nodiscard]] const drop_target* drop() const noexcept;
        [[nodiscard]] const pointer_drag_target* pointer_drag() const noexcept;
        // 초점을 가진 채 키로 값을 바꾸는 역할이다. 없으면 nullptr다.
        [[nodiscard]] const key_step_target* key_step() const noexcept;
        // 이 컨테이너를 흘리는 메시지다. 없으면 nullptr다.
        // 휠과 초점 되살리기가 표 없이 이 값으로 임자를 찾는다.
        [[nodiscard]] const scroll_source* scroll() const noexcept;
        [[nodiscard]] const zoom_source* zoom() const noexcept;
        // 안쪽 창의 끝에서 터치 끌기를 넘겨받는 역할이다. 없으면 nullptr다.
        [[nodiscard]] const pan_handoff_target* pan_handoff() const noexcept;
        // 초점을 가진 채 글자로 자기 모델을 찾는 역할이다. 없으면 nullptr다.
        [[nodiscard]] const key_search_target* key_search() const noexcept;
        // 더 안쪽이 소비하지 않은 키를 받는 처리기다. 없으면 nullptr다.
        [[nodiscard]] const key_handler* key_handler_of() const noexcept;
        // 앱이 지정한 포인터 모양이다.
        // `inherit`이면 지정하지 않았다는 뜻이라 `cursor_at`이 역할에서 고른다.
        [[nodiscard]] ui_cursor cursor() const noexcept;
        [[nodiscard]] ui_cursor active_cursor() const noexcept;
        // 액션·drag·drop·tooltip·커서 중 하나라도 있으면 hit test의 대상이 된다.
        //  - 포인터 모양도 tooltip처럼 hover에만 필요한 것이라 hit가 되어야 뜻이 생긴다.
        // 비활성 element도 tooltip 표시를 위해 hit는 되고 액션 실행만 막는다.
        [[nodiscard]] bool interactive() const noexcept;
        // Tab 순회의 자리인가.
        // 기본은 **누를 수 있으면 자리다** — left_click 액션이 있으면 참이다.
        // 마우스로 누를 수 있는데 키보드로는 갈 수 없는 자리를 만들지 않는 것이
        // 이 기본값의 뜻이다 (keyboard-focus-design.md).
        [[nodiscard]] bool tab_stop() const noexcept;
        // 포인터로 눌렀을 때 초점을 받을 대상이다. 기본은 자신이다 (`set_pointer_focus_target`으로 바꿀 수 있다).
        // 합성 컨트롤의 부품은 Tab 자리 없이도 컨트롤로 초점을 보낼 수 있다.
        [[nodiscard]] virtual const ui_element_id& pointer_focus_target() const noexcept;
        // 이 element의 자손들이 Tab 순회에서 한 자리인가 (묶음).
        // `none`이 아니면 `focus_order`가 묶음을 자리 하나로 접고, 그 안은
        // 화살표가 돈다.
        [[nodiscard]] focus_axis focus_group() const noexcept;
        // 묶음에 Tab으로 들어올 때 서는 항목이다.
        // 비어 있거나 그 항목이 초점을 받을 수 없으면 첫 항목이다.
        //  - **가둠(`focus_trap`)도 같은 값을 읽는다.** 가둠이 선 tree가 왔는데
        //    초점이 없으면 이 자리에 세운다. 다만 거기서 **빈 값은 "첫 항목"이
        //    아니라 "자동 초점이 없다"**다 — 묶음은 Tab이 이미 들어온 뒤라
        //    어딘가 서야 하지만, 가둠은 세울지 말지부터가 앱의 뜻이다
        //    (focus-entry-design.md).
        [[nodiscard]] const ui_element_id& focus_entry() const noexcept;
        // 참이면 Tab이 이 element의 것이다.
        // 초점이 여기 있는 동안 Tab·Shift+Tab은 초점을 옮기지 않고 키 경로로
        // 흘러, 탭 문자를 넣는 앱 정책까지 간다 (코드 칸·들여쓰기가 있는 메모).
        //  - **빠져나가는 길은 앱이 만든다.** 라이브러리는 탈출 키를 예약하지
        //    않는다 — Ctrl+Tab을 가로채면 그것을 이미 쓰는 앱과 다투고, Esc는
        //    dialog 닫기·popup 닫기·drag 취소와 우선순위가 얽힌다
        //    (focus-group-design.md).
        [[nodiscard]] bool takes_tab() const noexcept;
        // 참이면 초점이 이 element 안에 갇힌다 (modal dialog).
        // `focus_order`가 이 안만 훑으므로 Tab이 밖으로 나가지 않고, 밖에 있던
        // 초점은 사라진 것과 같이 거둬진다 (modal-dialog-design.md).
        //  - 포인터를 막는 것은 이 값이 아니라 `hit_opaque`인 scrim이다. 둘은
        //    각자 하나씩만 한다.
        [[nodiscard]] bool focus_trap() const noexcept;
        // 가둠이 사라지면 초점이 돌아갈 **바깥** 자리다.
        // 비어 있으면 되돌리지 않는다. 가둠이 아닌 element에서는 아무 뜻이 없다.
        //  - `focus_entry`와 나란히 선다. 자동 초점만 켜려면 두 줄 중 하나만 적게
        //    되고, 그 어색함이 "둘은 함께 온다"를 어휘로 말한다
        //    (focus-entry-design.md).
        [[nodiscard]] const ui_element_id& focus_return() const noexcept;
        // 가둠이 떠 있는 동안 Esc가 실행할 액션이다 (없으면 nullptr).
        // 가둠이 아닌 element에서는 아무 뜻이 없다.
        //  - **빈 액션이 "Esc로는 닫지 않는다"다.** popup 닫힘 계기와 같은
        //    규칙이라 저장 중 진행 표시처럼 닫히면 안 되는 dialog가
        //    따로 플래그 없이 표현된다 (modal-dialog-design.md).
        [[nodiscard]] const ui_action* dismiss_action() const noexcept;
        // 키보드 초점이 이 element의 가지를 떠날 때 실행할 액션이다 (없으면 nullptr).
        // 초점이 자신이나 자손에 있다가 그 밖의 element로, 또는 아무 데도 아닌 곳으로 옮겨 간 순간이다.
        //  - **떠날 때마다 한 번이다.** 가지 안에서 옮겨 다니는 것(입력 칸에서 그 옆 부품으로)은 떠난 것이 아니다.
        //    겹친 가지가 함께 떠나면 안쪽부터 차례로 실행된다.
        //  - 액션이 불릴 때 controller의 snapshot은 이미 새 초점이다. 옛 초점의 조상은 초점이 옮겨 간 그 순간의
        //    tree에서 찾는다.
        //  - **창의 비활성화는 떠남이 아니다** (`surface_focus_lost_event`). Alt+Tab으로 다른 창을 잠깐 보는 것은 "이
        //    검색을 그만두자"가 아니라서, 그것으로 후보 popup이 닫히면 치던 흐름이 끊긴다 (demo의 `search_popup_stays`가
        //    같은 판단이다). 돌아오면 초점도 popup도 그대로여야 한다. 창 활성화가 다시 세운 초점(가둠 진입)도 계기가 아니다.
        //  - **tree 교체가 거둔 초점도 계기가 아니다** (`set_tree`·`set_surface_trees`). 그 element를 뺀 tree를 지은 것이
        //    앱이라 이미 알고, 옛 tree는 그때 이미 없으며 그 자리에는 메시지를 돌려줄 길도 없다.
        //  - 계기는 사람의 입력이 옮긴 초점이다 — Tab·Shift+Tab, 묶음의 화살표·글자 탐색, 포인터 누름(빈 곳 포함),
        //    보조 기술의 초점 요청(`access_focus_event`).
        [[nodiscard]] const ui_action* focus_leave_action() const noexcept;
        // 참이면 이 화면의 **기본 자리**다 — 초점이 받지 못한 Enter가 여기로 온다.
        // 실행되는 것은 이 element의 `left_click`이다.
        //  - **표식이 액션이 아니라 bool인 이유**: 실행할 것이 이미 있다. 액션을
        //    또 받으면 "눌렀을 때와 Enter를 쳤을 때가 다른" 조합이 생긴다
        //    (enter-default-design.md).
        //  - 초점이 텍스트 칸이 아닌 자리에 있으면 **그 자리가 먼저다.** 기본
        //    버튼은 초점이 Enter를 받지 못할 때만 나선다.
        [[nodiscard]] bool default_button() const noexcept;
        // 지금 실제로 초점을 받을 수 있는가.
        // 배치되고 보이고 활성인 자리만이다 — 불변식을 한곳에 두어 자리 판정을
        // 뒤집어도 깨지지 않게 한다.
        [[nodiscard]] bool focusable() const noexcept;
        // 참이면 상호작용이 없어도 자기 bounds 안의 hit를 흡수한다.
        // 위에 떠 있는 표면(메뉴·토스트·scrim)의 빈 자리 클릭이 아래
        // element로 새지 않게 한다 — 흡수만 하고 아무 일도 하지 않는다.
        [[nodiscard]] bool hit_opaque() const noexcept;
        // 참이면 포인터가 올라가 있을 때만 그려지는 부품이다 (탭의 숨은 닫기 단추).
        //  - 마우스·펜은 누르기 전에 늘 올라가므로 보이는 것을 누른다. 터치에는 올림이 없어 손가락이 보이지 않는
        //    부품을 누르게 되므로, 터치의 탭은 이것을 지나 **감싼 가장 가까운 누를 수 있는 조상**이 받는다.
        //  - 접근 tree와 키보드에서는 그대로 선다 — 보이지 않을 뿐 없는 것이 아니다.
        [[nodiscard]] bool hover_only() const noexcept;
        // 참이면 창 제목 표시줄의 끌기 띠 위에 서 있어도 그 자리를 client로 판정한다
        // (caption-button-design.md). 검색 칸·명령 단추처럼 제목 표시줄 안에서 눌려야 하는 element가 단다.
        //  - 비클라이언트 판정은 게시된 tree에서 이 element의 **보이는 자리**를 본다 (`ui_tree::caption_client_at`).
        //  - 비활성이어도 client다. 눌러도 아무 일이 없는 칸을 잡고 창이 딸려 가면 안 된다.
        //  - 창 caption이 없는 플랫폼(Android)에서는 아무 뜻이 없다.
        [[nodiscard]] bool caption_client() const noexcept;
        [[nodiscard]] std::span<const std::unique_ptr<ui_element>> children() const noexcept;
        // 참이면 자식을 자기 bounds 안으로 자른다.
        // 그리기와 hit test 양쪽에 함께 걸리므로 보이지 않는 자식은 눌리지도 않는다.
        [[nodiscard]] bool clip_children() const noexcept;

        // 빌드 시 구성이다.
        // tree 게시 후에는 호출하지 않는다.
        void set_bounds(const rect_f& bounds) noexcept;
        void set_enabled(bool value) noexcept;
        void set_visible(bool value) noexcept;
        // 후보 popup처럼 입력 칸의 논리 초점을 유지하는 표면에 쓴다.
        void set_preserve_pointer_focus(bool preserve) noexcept
        {
            preserve_pointer_focus_ = preserve;
        }
        [[nodiscard]] bool preserves_pointer_focus() const noexcept
        {
            return preserve_pointer_focus_;
        }
        void set_tooltip(std::u8string text);
        // 커스텀 내용이 문자열 대신 표시된다. 빈 content로 지우면 문자열 표시로 돌아간다.
        // 게시 후에는 내용 tree도 불변이다. 화면 밖으로 넘치는 내용은 표면 안에서 잘린다.
        void set_tooltip_content(tooltip_config config);
        void set_search_label(std::u8string text);
        // 화면에 뜨지 않는, 보조 기술만 읽는 이름이다 (`access_name`).
        //
        // 글을 세우지 않는 컨테이너(목록·탭 막대·스크롤 막대)에 이름을 주는 자리다.
        // 그리기에도 글자 탐색에도 쓰이지 않으므로 화면은 그대로다.
        void set_access_name(std::u8string text);
        void set_action(ui_trigger trigger, ui_action action);
        void clear_action(ui_trigger trigger) noexcept;
        void set_drag_source(std::optional<drag_source> source);
        void set_drop_target(std::optional<drop_target> target);
        void set_pointer_drag_target(std::optional<pointer_drag_target> target);
        // 끌기와 짝이 되는 키 역할이다 (`set_pointer_drag_target`과 같은 build 시 설정).
        void set_key_step_target(std::optional<key_step_target> target);
        // 이 컨테이너가 낼 스크롤 메시지다 (`set_key_step_target`과 같은 build 시 설정).
        // 세우면 휠과 초점 되살리기가 표 없이 이 컨테이너를 찾아 쓴다 —
        // `scroll_delta_to_reveal`을 재정의한 컨테이너와 짝이 되는 값이다.
        void set_scroll_source(std::optional<scroll_source> source);
        void set_zoom_source(std::optional<zoom_source> source);
        void set_pan_handoff(std::optional<pan_handoff_target> target);
        // 글자 탐색이 이 element의 모델을 묻게 한다 (가상 목록).
        void set_key_search_target(std::optional<key_search_target> target);
        // 보조 기술이 시킨 실행을 앱 메시지로 받는다 (다중 선택 항목의 더하기·빼기·고르기 등).
        // 기본 `access_actions`가 먼저 이것에 묻고, 답이 없으면(nullopt) 지금까지의 기본(클릭)으로 물러선다.
        //  - `access_actions`를 재정의한 element도 처리하지 않는 명령을 기본 구현으로 넘기면 이것이 답한다.
        //  - 할 수 있는지는 여전히 `plan_access_request`가 먼저 거른다 — 이미 그 상태인 명령은 여기 오지 않는다.
        void set_access_handler(access_handler handler);
        // 초점이 이 element이거나 그 자손일 때, 더 안쪽이 소비하지 않은 키를 받는다 (`key_handler`).
        //  - 목록의 Ctrl+A처럼 **초점이 행 안의 부품에 있어도** 그 부품이 쓰지 않는 키를 목록이 받는 자리다.
        //  - hit 대상을 넓히지 않는다 (`set_key_step_target`과 같은 이유). 키는 포인터가 아니라 초점을 따른다.
        void set_key_handler(key_handler handler);
        // 포인터로 누르면 초점이 갈 곳을 다른 element로 정한다 (`pointer_focus_target`).
        //  - 합성 컨트롤이 **이미 있는 컨트롤**을 부품으로 담을 때 쓴다. 표의 체크 칸을 눌러도 초점은 표 본문에 남는다 —
        //    체크박스가 초점을 가져가면 그 뒤의 ↓가 표가 아니라 체크박스의 것이 된다.
        //  - 빈 id면 자신이다 (기본).
        void set_pointer_focus_target(ui_element_id target);
        void set_clip_children(bool value) noexcept;
        void set_hit_opaque(bool value) noexcept;
        // 포인터를 올려야 그려지는 부품이다 (`hover_only`).
        void set_hover_only(bool value) noexcept;
        // 제목 표시줄 안의 client 자리다 (`caption_client`).
        void set_caption_client(bool value) noexcept;
        // 자손들을 Tab의 한 자리로 묶는다 (탭 막대·라디오 묶음·목록).
        //  - 묶음 자체는 자리가 아니다. 자리는 **그 안의 항목 하나**다 —
        //    테를 두를 자리가 항목이어야 어디에 있는지 보인다.
        void set_focus_group(focus_axis axis) noexcept;
        // 묶음에 들어올 때 설 항목이다 (통상 앱 상태의 "선택된 것").
        // 가둠에 붙이면 그 가둠이 선 순간 초점이 설 자리가 된다.
        void set_focus_entry(ui_element_id id);
        // Tab을 이 element의 것으로 만든다 (코드 칸의 탭 문자).
        // 쓰는 쪽이 **빠져나갈 길을 함께 만들어야 한다.**
        void set_takes_tab(bool value) noexcept;
        // 초점을 이 element 안에 가둔다 (modal dialog host).
        //  - 가둠 자신은 자리가 아니다. 자리는 그 안의 컨트롤들이다 — 묶음과
        //    같은 규칙이라, scrim이 클릭을 흡수하느라 액션을 갖고 있어도 Tab이
        //    거기 서지 않는다.
        void set_focus_trap(bool value) noexcept;
        // 가둠이 사라졌을 때 초점이 돌아갈 바깥 자리다 (통상 가둠을 연 그 자리).
        // 앱은 추측하지 않고 손에 쥔다 — 여는 계기가 클릭이나 Space라 그때의
        // 초점이 곧 그 자리이고, 액션이 `ui_action_context::element`로 자기 id를 받는다.
        void set_focus_return(ui_element_id id);
        // 가둠이 떠 있는 동안의 Esc를 이 액션으로 받는다.
        // 무엇을 닫을지는 앱이 정한다 — element는 메시지만 만든다.
        void set_dismiss_action(ui_action action);
        // 키보드 초점이 이 가지를 떠날 때의 액션이다 (`focus_leave_action`).
        // 초점을 따라 사는 표면(편집형 콤보의 후보 popup)을 닫는 자리다 — 밖 누름은 플랫폼의 popup 닫힘이 이미 받지만
        // Tab·보조 기술로 옮긴 초점은 거기 오지 않는다.
        void set_focus_leave_action(ui_action action);
        // 초점이 받지 못한 Enter를 이 자리로 데려온다 (기본 버튼).
        // 강조색 채움으로 그것을 알리는 것은 그리는 쪽의 몫이다 —
        // `text_button_config`가 한 값으로 둘을 함께 낸다.
        //  - 테가 아니라 채움이다. 초점 테는 `ui_tree`가 얹으므로, 둘 다 테면
        //    한 화면에서 구별되지 않는다 (enter-default-design.md).
        void set_default_button(bool value) noexcept;
        // 자리 판정(`tab_stop`)의 기본값을 뒤집는다.
        // **종류가 아니라 자리로 정한다**: 같은 클래스가 두 역할을 하기 때문이다 —
        // `button_element` 하나가 보통 버튼이자 창 버튼이고, 창 버튼은 Tab의
        // 자리가 아니다. 만드는 쪽이 그 자식의 성격을 안다.
        void set_tab_stop(bool value) noexcept;
        // 평소의 포인터 모양과, 잡고 끄는 동안의 모양이다.
        // 지정하지 않으면 element의 역할에서 고른다 (`cursor_at`).
        void set_cursor(ui_cursor cursor) noexcept;
        void set_active_cursor(ui_cursor cursor) noexcept;

        // 부모가 준 slot 안에서 자기 bounds와 자식 배치를 확정한다.
        virtual void arrange(const arrange_context& context) = 0;
        // 내용이 원하는 크기다 (논리 픽셀). 기본은 없음(nullopt) — "나는 크기를 말하지 않는다".
        //
        // 배치는 여전히 부모가 정한다. 이것은 **묻는 컨테이너만** 부르는 선택적 질문이다 (`stack_element::add_fit`,
        // `grid_element`의 맞춤 열). 고정 길이로 담는 지금까지의 tree는 이것을 부르지 않아 배치가 바뀌지 않는다.
        //  - `available_width`는 줄 수가 폭에 달린 내용(여러 줄 글)이 높이를 정하는 폭이다. 제한이 없으면
        //    무한대다. 넓이는 그보다 좁게 답해도 되고, 한 줄 내용은 무시한다.
        //  - 글이 든 element(label·글자 버튼·체크·글리프)와 배치 컨테이너가 답한다. 답이 없는 자식은 맞춤
        //    컨테이너에서 0이다.
        //  - arrange 전에 불린다. 배치 결과(bounds)를 읽지 않고 config만으로 답한다.
        [[nodiscard]] virtual std::optional<size_f> preferred_size(const measure_context& context, float available_width) const;
        // 자신과 자식을 그린다.
        // hover·눌림 강조는 interaction으로 판정한다.
        virtual void draw(draw_context& context, const interaction_snapshot& interaction) const = 0;
        // 좌표를 포함하는 가장 안쪽의 상호작용 대상을 돌려준다.
        // 기본 구현은 자식을 역순(위에 그려진 것 먼저)으로 탐색한 뒤 자기 bounds를 검사한다.
        [[nodiscard]] virtual const ui_element* hit_test(float x, float y) const;
        // 좌표에서 payload를 수락하는 가장 위의 drop 대상을 돌려준다.
        // hit test와 같은 규칙으로 자르는 컨테이너 밖 좌표는 그 안을 보지 않는다.
        //  - 보이지 않는 것은 놓을 자리도 아니다.
        [[nodiscard]] const ui_element* find_drop_target(float x, float y, const drag_payload& payload) const;
        // hit_opaque에 막혔는지도 돌려준다. 보통 element는 drop 대상이 아니면 지나친다.
        [[nodiscard]] drop_hit_result drop_hit_test(float x, float y, const drag_payload& payload) const;

        // 다음에 그림이 달라지는 시각이다 (시간 update 계약).
        // 시각만의 함수로 그리는 element(회전 표시·토스트 흐려짐·caret 깜빡임)가 재정의한다.
        //  - nullopt(기본값)는 시간이 흘러도 그대로라는 뜻이다.
        //  - context.now 이하는 "계속 움직이는 중"이라 platform이 짧은 주기로 다시 그린다.
        // platform은 tree의 최솟값 시각에 맞춰 timer 하나만 걸므로 평소 비용은 0이다.
        // 자식 몫은 tree가 모으므로 자기 것만 답한다.
        [[nodiscard]] virtual std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const;

        // 텍스트 박스만 구현한다.
        // 창 좌표 x를 글 안의 byte offset으로 옮긴다.
        // 글꼴 크기와 가로 스크롤은 element가 알고 있어 그리기와 같은 값을 쓴다.
        [[nodiscard]] virtual std::optional<std::size_t> offset_at(float x, const text_measurer& measurer) const;
        // 여러 줄 텍스트 박스가 창 좌표 (x, y)를 글 안의 byte offset으로 옮긴다.
        // 기본 구현은 y를 쓰지 않고 offset_at(x, measurer)에 맡긴다.
        [[nodiscard]] virtual std::optional<std::size_t> offset_at_point(float x, float y, const text_measurer& measurer) const;
        // 창 좌표 (x, y)에 놓을 caret이다 — offset과, 넘겨 접은 자리면 어느 줄에 붙는가다.
        // 기본 구현은 offset_at_point를 감싸 언제나 아랫줄 쪽(`upstream == false`)이다.
        [[nodiscard]] virtual std::optional<text_caret_position> caret_at_point(float x, float y, const text_measurer& measurer) const;
        // 텍스트 박스만 구현한다.
        // 글과 선택 범위를 그대로 내준다.
        [[nodiscard]] virtual std::optional<text_input_snapshot> text_input() const;
        // 텍스트 박스만 구현한다.
        // 질의한 문서의 byte 구간이 창 좌표에서 차지하는 자리다.
        // IME 후보 창을 조합 글자 밑에 앉히는 근거라, 글꼴 크기·안쪽 여백·
        // 가로 스크롤을 그리기와 같은 값으로 element가 직접 계산한다.
        [[nodiscard]] virtual std::optional<rect_f> text_span_bounds(const text_span_query& query, const text_measurer& measurer) const;
        // 여러 줄 텍스트 박스만 구현한다.
        // 표시 문서(조합 중이면 조합 글이 섞인 문서)의 화면 줄이 시작하는 offset이다. 첫 값은 0이다.
        // 보조 기술의 줄 단위(UIA `TextUnit_Line`)가 쓴다 — 넘겨 접은 줄도 한 줄이라 배치의 결과를 그대로 내준다.
        //  - 비어 있으면 문서 전체가 한 줄이다 (한 줄 칸).
        [[nodiscard]] virtual std::vector<std::size_t> text_line_starts() const;

        // 흘리는 컨테이너면 `target`을 자기 창 안으로 들이는 스크롤 **변화량**이다
        // (논리 픽셀). 흘리지 않으면 0이다.
        //
        // 기본값이 "나는 흘리지 않는다"이므로 **자르기만 하는 것은 재정의하지
        // 않는다** (`tab_bar_element`). 재정의하는 것은 흘리는 컨테이너다 —
        // `scroll_view_element`·`strip_element`가 직접 재고, 그것을 안에 두는
        // `scroll_area_element`·`list_element`·`virtual_list_element`가 물려준다.
        //  - 물리 좌표(`bounds()`)를 논리 값으로 옮기는 것은 배율을 아는 이쪽뿐이라
        //    여기가 자리다. `arrange` 뒤에만 유효하다.
        //  - virtual인 이유는 **앱이 만든 흘리는 컨테이너도 답해야 해서다.**
        //    라이브러리 타입을 열거해 캐스팅하면 앱의 것이 조용히 빠진다
        //    (초점 테를 tree가 한곳에서 그리기로 한 것과 같은 판단이다).
        [[nodiscard]] virtual float scroll_delta_to_reveal(const rect_f& target) const;

        // 보조 기술이 읽는 이 요소의 정보다 (이름·역할·상태).
        // 기본 구현은 **누를 수 있고 이름이 있으면 단추로 읽힌다** — `tab_stop`의
        // "누를 수 있으면 자리다"와 같은 문장이다. 이름은 `access_name()`이 답하고
        // (`set_access_name` → `search_label` → tooltip), 셋 다 비면 구조(`none`)다 —
        // 이름 없는 단추는 스크린 리더에 소음이라(scrim) 이름 있는 것만 승격한다.
        //  - virtual인 이유는 `scroll_delta_to_reveal`과 같다. 앱이 만든 element도
        //    답해야 한다.
        [[nodiscard]] virtual access_info accessibility() const;

        // 보조 기술이 시킨 실행 하나를 이 요소의 액션으로 옮긴 것이다.
        // `accessibility()`의 짝이다 — 저쪽이 "나는 무엇인가"를, 이쪽이 "그것을
        // 어떻게 하는가"를 답한다. 답의 규약도 `plan_access_request`와 같다
        // (없으면 할 수 없고, 빈 목록이면 할 일이 없다).
        //
        // 기본 구현은 **누를 수 있으면 그 클릭이다** — `accessibility()`의 기본이
        // "누를 수 있으면 단추다"인 것과 같은 문장이라, 마우스로 누를 수 없는 것은
        // 보조 기술로도 누를 수 없다. 좌표는 요소의 한가운데다 (포인터가 관여하지
        // 않은 실행의 통상 규칙 — Space의 실행이 그렇게 한다).
        //  - `set_value`와 `expand`·`collapse`는 기본이 없음이다. 절대 명령은
        //    목표 상태를 나르는 factory를 가진 요소만 답할 수 있다 — 클릭은
        //    뒤집기라, 오래된 발행본 기준으로 흘리면 같은 명령 둘이 두 번 뒤집는다
        //    (accessibility-action-design.md).
        //  - 상태를 답하는 요소와 그 상태를 바꾸는 요소가 다를 수 있다 (그룹의
        //    펼침은 머리행이, 목록 행의 펼침은 삼각형이 쥔다). 그것을 아는 것은 그
        //    요소뿐이라 재정의가 자리다.
        [[nodiscard]] virtual std::optional<std::vector<input_action>> access_actions(const access_request& request) const;

        // 화면 밖 항목까지 답하는 모델이다 (`access_item_model`). 기본은 없다(nullptr).
        //  - 창에 걸치는 행만 짓는 container(가상 목록)가 재정의한다. 보조 기술의 항목 찾기·선택 읽기·위치가 tree가
        //    아니라 이 모델을 본다.
        //  - virtual인 이유는 `accessibility()`와 같다. 앱이 만든 가상 container도 답해야 한다.
        [[nodiscard]] virtual const access_item_model* access_items() const noexcept;

        // 이 자리의 포인터 모양이다.
        // 좌표는 창 좌표이고 `interaction`으로 지금 잡고 끄는 중인지 판단한다.
        // 기본 구현의 우선순위는 다음과 같다.
        //  1. 앱이 지정한 값 (`set_active_cursor` → `set_cursor`)
        //  2. 역할에서 고른 값 (끌 수 있으면 grab·grabbing, 텍스트 박스면 text)
        //  3. `inherit` (창의 기본 모양)
        // 자리마다 모양이 다른 element(가장자리를 잡아 크기를 바꾸는 칸 등)가 재정의한다.
        [[nodiscard]] virtual ui_cursor cursor_at(float x, float y, const interaction_snapshot& interaction) const;

    protected:
        void add_child(std::unique_ptr<ui_element> child);
        void draw_children(draw_context& context, const interaction_snapshot& interaction) const;

    private:
        ui_element_id id_ {};
        rect_f bounds_ {};
        bool enabled_ { true };
        bool visible_ { true };
        bool arranged_ { false };
        std::u8string tooltip_ {};
        std::unique_ptr<tooltip_config> tooltip_content_ {};
        bool preserve_pointer_focus_ { false };
        std::u8string search_label_ {};
        std::u8string access_name_ {};
        std::array<ui_action, ui_trigger_count> actions_ {};
        std::optional<drag_source> drag_source_ {};
        std::optional<drop_target> drop_target_ {};
        std::optional<pointer_drag_target> pointer_drag_target_ {};
        std::optional<key_step_target> key_step_target_ {};
        std::optional<scroll_source> scroll_source_ {};
        std::optional<zoom_source> zoom_source_ {};
        std::optional<pan_handoff_target> pan_handoff_ {};
        std::optional<key_search_target> key_search_target_ {};
        access_handler access_handler_ {};
        key_handler key_handler_ {};
        ui_element_id pointer_focus_target_ {};
        bool clip_children_ { false };
        bool hit_opaque_ { false };
        bool hover_only_ { false };
        bool caption_client_ { false };
        // 비어 있으면 액션 유무에서 판정한다 (`tab_stop`).
        std::optional<bool> tab_stop_ {};
        focus_axis focus_group_ { focus_axis::none };
        ui_element_id focus_entry_ {};
        bool takes_tab_ { false };
        bool focus_trap_ { false };
        ui_element_id focus_return_ {};
        ui_action dismiss_action_ {};
        ui_action focus_leave_action_ {};
        bool default_button_ { false };
        ui_cursor cursor_ { ui_cursor::inherit };
        ui_cursor active_cursor_ { ui_cursor::inherit };
        std::vector<std::unique_ptr<ui_element>> children_ {};
    };
} // namespace luil

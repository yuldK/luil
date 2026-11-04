#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_tree.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 실행 중인 화면의 요소를 들여다보는 검사기다 (inspector-design.md).
//  - 이 헤더의 함수들은 모두 순수하다. 게시된 tree는 불변이라 어느 thread에서나 부르고, 앱의 로그와 test가
//    overlay와 같은 글을 얻는다.
//  - overlay는 그리기만 한다 (`ui_frame::inspector`). 요소가 아니라서 hit test, 입력, 초점, 접근성 tree에 닿지 않는다.
namespace luil {
    // 요소를 집는 길이다. 집은 요소에서 `inspector_config::ancestor_levels`만큼 조상으로 올라간 요소가 대상이다.
    enum class inspector_pick
    {
        hovered,
        focused,
        pressed,
        // 앱이 `inspector_config::selected`로 정한 요소다. `inspect_step`으로 옮긴다.
        selected,
        // 앱이 `inspector_config::point`로 준 좌표 아래의 가장 깊은 보이는 요소다.
        point,
    };

    // 앱 kind의 이름이다. 값이라 UI thread가 앱 상태를 잡은 함수를 부르지 않는다.
    struct inspector_kind_name
    {
        ui_element_kind kind {};
        std::u8string name {};
    };

    // frame마다 앱이 싣는다 (`ui_frame::inspector`). 기본값이면 아무것도 그리지 않는다.
    //  - `selected`는 pick이 selected일 때, `point`는 point일 때 쓴다. `point`는 `ui_tree::hit_test`와 같은 tree 좌표다
    //    (물리 픽셀, 표면의 주 tree면 안전 영역 원점 기준).
    //  - 켜는 단축키는 앱이 정한다. 라이브러리는 키를 예약하지 않는다.
    struct inspector_config
    {
        bool enabled { false };
        inspector_pick pick { inspector_pick::hovered };
        std::size_t ancestor_levels { 0 };
        ui_element_id selected {};
        std::optional<point_f> point {};
        // 보이는 모든 요소의 윤곽도 옅게 긋는다.
        bool outline_all { false };
        // 정보 상자다.
        bool show_info { true };
        // 팔레트 견본이다.
        bool show_palette { false };
        std::vector<inspector_kind_name> kind_names {};
    };

    inline constexpr std::size_t no_inspect_parent { static_cast<std::size_t>(-1) };

    // tree를 그리기 순서(pre-order)로 평탄화한 줄이다. 좌표는 물리 픽셀(tree 좌표)이다.
    struct inspect_node
    {
        const ui_element* element { nullptr };
        std::size_t depth { 0 };
        // 목록 안의 자리다. 뿌리는 `no_inspect_parent`다.
        std::size_t parent { no_inspect_parent };
        rect_f bounds {};
        // 조상의 잘림을 거치고 남은 자리다. 완전히 잘렸거나 보이지 않는 가지면 비어 있다.
        std::optional<rect_f> visible_bounds {};
        // 뿌리부터 이 요소까지 모두 보인다. 숨은 가지의 요소도 목록에는 서고 이 값이 거짓이다.
        bool visible_path { true };
    };

    [[nodiscard]] std::vector<inspect_node> inspect_nodes(const ui_tree& tree);

    // 좌표 아래의 가장 깊은 보이는 요소다. `hit_test`와 달리 interactive가 아니어도 집는다.
    //  - 숨은 요소는 건너뛰고, `clip_children()` 밖의 좌표는 그 아래를 보지 않으며, 겹치면 나중에 그린 것이 먼저다.
    //  - 가상 함수 `hit_test`를 재정의한 요소(가상 목록, 링크)의 규칙은 따르지 않는다. 자식을 `children()`으로 걷는다.
    //  - 좌표가 어떤 요소에도 들지 않으면 nullptr이다.
    [[nodiscard]] const ui_element* inspect_at(const ui_tree& tree, float x, float y);

    // `config`가 가리키는 대상이다. 이 표면의 tree에 없거나 보이지 않으면 nullptr이다.
    //  - `interaction`은 이 표면의 것이어야 한다 (`interaction_for_surface`로 거른 값). 남의 표면의 hover는 이 tree의 id로
    //    잘못 맞을 수 있어서다.
    //  - `ancestor_levels`만큼 조상으로 올라가며 뿌리에서 멈춘다.
    [[nodiscard]] const ui_element* pick_inspected(const ui_tree& tree, const interaction_snapshot& interaction, const inspector_config& config);

    // 선택을 옮기는 길이다. 보이지 않는 가지는 건너뛴다.
    //  - `previous`·`next`는 그리기 순서(pre-order)의 앞뒤, `previous_sibling`·`next_sibling`은 같은 부모 아래의 앞뒤다.
    enum class inspect_move
    {
        parent,
        first_child,
        previous_sibling,
        next_sibling,
        previous,
        next,
    };

    // `from`에서 `move`만큼 옮긴 요소의 id다. 앱은 방금 지은 tree로 부른다. 옮길 곳이 없으면 빈 id다.
    //  - `from`이 비었거나 tree에 없으면 `next`만 뿌리를 돌려준다. 나머지 이동은 빈 id다.
    //  - 같은 id가 둘이면 그리기 순서의 첫 요소에서 출발한다 (`ui_tree::find`와 같은 규칙).
    [[nodiscard]] ui_element_id inspect_step(const ui_tree& tree, const ui_element_id& from, inspect_move move);

    // 열거자 이름 그대로다 (`info_bar_close`). 라이브러리 kind가 아니면 빈 값이다.
    //  - 새 라이브러리 kind는 같은 커밋에 이름을 더한다. 이름 없는 kind는 `inspect_names.cpp`의 `default` 없는 switch가
    //    경고로, 전수 test가 실패로 막는다.
    [[nodiscard]] std::u8string_view library_kind_name(ui_element_kind kind) noexcept;
    // 라이브러리 이름, `config.kind_names`, `kind#<수>` 순이다.
    [[nodiscard]] std::u8string kind_display_name(ui_element_kind kind, const inspector_config& config);
    // 열거자 이름이다 (`button`). 모르는 값은 빈 값이고, 글로 쓸 때는 `describe_element`가 `role#<수>`로 쓴다.
    [[nodiscard]] std::u8string_view access_role_name(access_role role) noexcept;
    // `typeid`에서 푼 클래스 이름이다. MSVC의 `class `·`struct ` 접두를 떼고, `__GNUG__` 계열은 demangle한다.
    [[nodiscard]] std::u8string element_class_name(const ui_element& element);

    // 요소 하나를 다섯 줄 글로 푼다. 글은 개발자용 ASCII 식별자와 숫자다. 줄은 `\n`으로 잇고 끝에는 붙이지 않는다.
    //  - `scale`은 tree를 그린 배율이다. 치수는 논리 픽셀로 쓴다.
    //  - 앱이 준 접근 이름은 그대로 쓰되 제어 문자는 공백으로 바꾼다. 값(`access_info::value`)은 쓰지 않는다. 암호 칸과
    //    긴 글이 새지 않게 하려는 것이다.
    //  - 요소가 tree에 없으면 깊이를 0으로, 보이는 영역을 bounds와 같게 쓴다.
    [[nodiscard]] std::u8string describe_element(const ui_tree& tree, const ui_element& element, float scale, const inspector_config& config);
    // tree 전체를 한 요소 한 줄로 풀어 쓴다. 첫 줄은 요소 수와 중복 id·미배치 수다.
    [[nodiscard]] std::u8string dump_tree(const ui_tree& tree, float scale, const inspector_config& config);

    // 팔레트 역할 하나의 이름과 색이다 (inspector-design.md 결정 7). 이름은 팔레트 멤버 이름 그대로이고 caption 하위 팔레트는
    // `caption.` 뒤에 멤버 이름을 쓴다.
    struct inspector_swatch
    {
        std::u8string_view name {};
        ui_color color { 0 };
    };

    // `ui_color_palette`의 전 역할을 선언 차례로 낸다. caption 하위 팔레트는 그 칸의 자리에 든다.
    [[nodiscard]] std::vector<inspector_swatch> inspector_palette_swatches(const ui_color_palette& palette);
}

#pragma once

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
    };

    inline constexpr std::size_t ui_trigger_count { 3 };

    // 액션 실행 시점의 문맥이다.
    // 좌표는 물리 픽셀(창 좌표)이다.
    struct ui_action_context
    {
        ui_element_id element {};
        float x { 0.0f };
        float y { 0.0f };
        bool control { false };
    };

    // 글자 폭을 재는 함수다.
    // 텍스트 박스가 포인터 x를 글 안의 offset으로 옮길 때 쓴다.
    // 앱은 내장 글꼴로 만들고 test는 고정 폭 가짜를 넣는다.
    // 글꼴이 없는 thread에서도 부를 수 있어야 하므로 element 밖에서 주입한다.
    using text_measurer = std::function<float(std::u8string_view text, float pixel_size)>;

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

    struct drag_payload
    {
        ui_element_id source {};
        // 끌리는 대상의 앱 정의 키다 (ui_element_id::owner와 같은 규약).
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

        [[nodiscard]] bool operator==(const drag_payload&) const = default;
    };

    struct drag_source
    {
        std::function<drag_payload(const ui_action_context&)> make_payload {};
    };

    struct drop_target
    {
        std::function<bool(const drag_payload&)> accepts {};
        std::function<std::vector<input_action>(const drag_payload&, const ui_action_context&)> on_drop {};
    };

    class ui_element;

    // drop 대상을 못 찾은 자리와 위쪽 불투명 요소가 가린 자리를 구별한다.
    // OS 파일 drop은 전자에서만 창 전체 처리로 물러선다.
    struct drop_hit_result
    {
        const ui_element* target { nullptr };
        bool blocked { false };
    };

    // 누른 채 끄는 동안 연속으로 반응하는 element다 (스크롤 막대).
    // 일반적인 drag & drop과 달리 ghost도 drop 대상도 없고, 포인터 이동을 그때그때 메시지로 바꾼다.
    // 눌린 동안에는 포인터가 element를 벗어나도 계속 호출된다.
    struct pointer_drag_target
    {
        // 눌린 순간 한 번이다.
        // 누른 지점으로 즉시 이동할지 element가 정한다.
        std::function<std::vector<input_action>(const ui_action_context&)> on_press {};
        // 직전 위치와 현재 위치를 받는다.
        // 상대 변화량만 쓰므로 tree가 다시 빌드되어도 이어서 끌 수 있다.
        std::function<std::vector<input_action>(const ui_action_context& previous, const ui_action_context& current)> on_move {};
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
    struct scroll_source
    {
        // 스크롤 위치를 이만큼 옮기자는 메시지다 (delta는 논리 픽셀).
        // 부호는 휠과 같다: 양수 = offset 증가 = 내용이 위로 올라간다.
        std::function<input_action(float delta)> scroll {};
        // 논리 스크롤 변화량을 배치의 물리 좌표로 옮기는 배율이다.
        // arrange에서 실제 배율을 넣는다. 중첩 reveal이 이동 후 위치를 계산한다.
        float scale { 1.0f };
        // 절대 자리(`scroll_to`)는 여기 두지 않는다. 휠도 되살리기도 변화량으로만
        // 말하고, 절대 자리를 읽는 것은 보조 기술뿐이라 그 값은 그것을 실제로
        // 내주는 자리(`scrollbar_config::scroll_to`)에 있다 — 아무도 읽지 않는
        // 칸을 두면 채운 앱이 켜졌다고 믿는다.
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

    // 배치 문맥이다.
    // slot은 부모가 준 영역이고 scroll_offset은 논리 픽셀이다.
    struct arrange_context
    {
        rect_f slot {};
        float scale { 1.0f };
        float scroll_offset { 0.0f };

        // 같은 배율·scroll_offset으로 자식에게 줄 문맥이다.
        // 자리만 새로 준다 — 자식 문맥을 손으로 지으면 뒤의 값을 빠뜨리기 쉽고,
        // 빠뜨려도 그 자리에서는 아무 일도 일어나지 않는다. 몇 계층 아래의
        // sticky 머리행이 조용히 어긋날 뿐이다 (tree-arrange-design.md).
        //  - 문맥에 새 값이 생겨도 모든 부모가 저절로 잇는다.
        [[nodiscard]] arrange_context for_child(const rect_f& child_slot) const noexcept
        {
            return { child_slot, scale, scroll_offset };
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
    };

    // update 판정의 문맥이다.
    // 그리기와 같은 시계(steady_clock)를 쓰므로 next_update가 예고한 시각에
    // 다시 그리면 draw_context::now가 그 시각을 지나 있다.
    struct update_context
    {
        std::chrono::steady_clock::time_point now {};
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

        [[nodiscard]] bool operator==(const drag_visual&) const = default;
    };

    // input thread가 게시하고 UI thread가 그리기에 쓰는 상호작용 발행본이다.
    // 앱 상태가 아니라 순수한 "입력 정규화 상태"다.
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
        // 초점을 가진 채 글자로 자기 모델을 찾는 역할이다. 없으면 nullptr다.
        [[nodiscard]] const key_search_target* key_search() const noexcept;
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
        // 포인터로 눌렀을 때 초점을 받을 대상이다. 기본은 자신이다.
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
        [[nodiscard]] std::span<const std::unique_ptr<ui_element>> children() const noexcept;
        // 참이면 자식을 자기 bounds 안으로 자른다.
        // 그리기와 hit test 양쪽에 함께 걸리므로 보이지 않는 자식은 눌리지도 않는다.
        [[nodiscard]] bool clip_children() const noexcept;

        // 빌드 시 구성이다.
        // tree 게시 후에는 호출하지 않는다.
        void set_bounds(const rect_f& bounds) noexcept;
        void set_enabled(bool value) noexcept;
        void set_visible(bool value) noexcept;
        void set_tooltip(std::u8string text);
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
        // 글자 탐색이 이 element의 모델을 묻게 한다 (가상 목록).
        void set_key_search_target(std::optional<key_search_target> target);
        void set_clip_children(bool value) noexcept;
        void set_hit_opaque(bool value) noexcept;
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
        // 텍스트 박스만 구현한다.
        // 글과 선택 범위를 그대로 내준다.
        [[nodiscard]] virtual std::optional<text_input_snapshot> text_input() const;
        // 텍스트 박스만 구현한다.
        // 질의한 문서의 byte 구간이 창 좌표에서 차지하는 자리다.
        // IME 후보 창을 조합 글자 밑에 앉히는 근거라, 글꼴 크기·안쪽 여백·
        // 가로 스크롤을 그리기와 같은 값으로 element가 직접 계산한다.
        [[nodiscard]] virtual std::optional<rect_f> text_span_bounds(const text_span_query& query, const text_measurer& measurer) const;

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
        std::u8string search_label_ {};
        std::u8string access_name_ {};
        std::array<ui_action, ui_trigger_count> actions_ {};
        std::optional<drag_source> drag_source_ {};
        std::optional<drop_target> drop_target_ {};
        std::optional<pointer_drag_target> pointer_drag_target_ {};
        std::optional<key_step_target> key_step_target_ {};
        std::optional<scroll_source> scroll_source_ {};
        std::optional<key_search_target> key_search_target_ {};
        bool clip_children_ { false };
        bool hit_opaque_ { false };
        // 비어 있으면 액션 유무에서 판정한다 (`tab_stop`).
        std::optional<bool> tab_stop_ {};
        focus_axis focus_group_ { focus_axis::none };
        ui_element_id focus_entry_ {};
        bool takes_tab_ { false };
        bool focus_trap_ { false };
        ui_element_id focus_return_ {};
        ui_action dismiss_action_ {};
        bool default_button_ { false };
        ui_cursor cursor_ { ui_cursor::inherit };
        ui_cursor active_cursor_ { ui_cursor::inherit };
        std::vector<std::unique_ptr<ui_element>> children_ {};
    };
} // namespace luil

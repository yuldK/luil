// 텍스트 입력 섹션이다.
//
// 다루는 element: text_input_element (한 줄 텍스트 박스).
//
// 텍스트 입력은 조각이 넷이라 처음이 가장 헷갈린다. 흐름은 이렇다.
//   1. kind 등록: policy의 text_target_of가 kind_note_input → target_note를
//      답해야 클릭 초점·caret·IME·클립보드가 이 박스로 이어진다 (app.cpp).
//   2. 입력 발생: 키·IME·붙여넣기를 라이브러리가 편집 요청(text_edit_request)
//      으로 정규화하고, policy가 그 요청을 앱 메시지(edit_intent)에 담는다.
//   3. 상태 변경: logic thread의 handle이 apply_text_edit(초안, 요청)을 부른다.
//      초안(text_edit_state)이 확정 글의 진실이고 undo 기록도 여기에 쌓인다.
//   4. 화면: make_text_input_view(초안, 조합 표시, target)가 element에 줄
//      view를 합친다. IME 조합 중에는 조합 글이 섞여 보인다.
//
// 이 파일은 4번(화면)만 맡는다. 1~3은 app.cpp의 policy·handle에 있다.

#include "widgets/app.h"

#include "luil/generated/codicons.h"

#include <utility>

namespace widgets {
    section build_inputs_section(const app_state& state)
    {
        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inputs" }, column_config) };

        luil::label_config title {};
        title.text = u8"텍스트 입력 — 클릭해 초점을 주고, 한글 조합·붙여넣기·Ctrl+Z를 써 본다";
        title.font_size = 13.0f;
        title.color = luil::label_color_role::primary;
        const float title_height { luil::label_element::height_for(title) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"inputs-title" }, title), title_height);

        // view는 확정 글(초안)과 IME 조합 표시를 합친 값이다.
        // 조합이 이 target의 것이 아니면 확정 글만 보인다.
        luil::text_input_view view { luil::make_text_input_view(state.note, state.composition, target_note) };

        // 검색 칸: 돋보기와 지우기 버튼이 **칸 안에** 선다.
        // 밖에서 가로 stack으로 감싸면 글이 보이는 안쪽 폭이 두 벌이 되어
        // caret과 글자가 어긋난다 — 그래서 element가 둘을 갖는다.
        luil::text_input_config input_config {};
        input_config.placeholder = u8"여기에 입력한다";
        input_config.leading_glyph = luil::codicons::icon_search;
        input_config.clear_tooltip = u8"글을 비운다";
        // 지우는 것은 앱이다 — element는 액션을 실행할 뿐이다.
        // clear 명령은 라이브러리에 이미 있으므로 그 요청을 봉투에 담아 돌려준다.
        input_config.clear = [](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            luil::text_edit_request request {};
            request.target = target_note;
            request.command = luil::text::text_edit_command::clear;
            return { luil::make_app_action(edit_intent { request }) };
        };
        auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_note_input }, std::move(view), std::move(input_config)) };
        column->add(std::move(input), 28.0f);

        // 입력된 글을 그대로 보여 주는 라벨.
        // 상태(초안)는 driver가 소유하고 이 섹션은 읽기만 한다.
        luil::label_config echo {};
        echo.text = state.note.text.empty() ? u8"(아직 비어 있다)" : u8"지금 글: " + state.note.text;
        echo.font_size = 11.0f;
        const float echo_height { luil::label_element::height_for(echo) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"inputs-echo" }, echo), echo_height);

        return { std::move(column), title_height + 8.0f + 28.0f + 8.0f + echo_height };
    }
} // namespace widgets

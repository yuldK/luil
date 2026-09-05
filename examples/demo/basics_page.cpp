#include "demo/basics_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/modal_host_element.h"


#include <utility>
#include <vector>

namespace demo {
    namespace {
        // 숫자 칸에 들어갈 수 있는 글자만 남긴다.
        // 붙여넣기와 IME 확정도 이 관문을 지난다.
        [[nodiscard]] std::u8string keep_digits(const std::u8string_view text)
        {
            std::u8string digits {};
            for (const char8_t character : text)
                if (character >= u8'0' && character <= u8'9')
                    digits.push_back(character);
            return digits;
        }

        [[nodiscard]] float clamp_offset(const float value, const float minimum, const float maximum)
        {
            if (maximum < minimum)
                return 0.0f;
            if (value < minimum)
                return minimum;
            if (value > maximum)
                return maximum;
            return value;
        }

        // 실행 파일이 있는 디렉터리다. 자산은 그 옆에 선다.
        // 작업 디렉터리로 찾으면 바로 가기·디버거·탐색기 중 무엇으로 띄웠느냐에
        // 따라 자리가 달라진다.

        // 가로는 색상, 세로는 밝기가 변하는 견본 이미지를 파일에서 읽는다.
        // fit 설정에 따른 여백과 잘림의 차이를 확인할 수 있다.

        // 움직이는 견본 하나를 읽는다 (실행 파일 옆 `assets/`의 webp·gif).
        //  - **정지 그림과 같은 문을 쓰지 않는다.** 정지 쪽 문은 여러 장이 든
        //    파일에서도 첫 장만 내주므로 그림이 서 버린다 (image_decode.h).
        //  - 상한은 **들고 있을** 크기를 정한다. 칸이 96 논리 픽셀 높이라 배율
        //    2까지 쳐도 200이면 넉넉하고, 움직이는 그림에서는 그 값에 장 수가
        //    곱해진다 — 상한 하나가 여기서 열둘·열여섯 번 값을 낸다.
    } // namespace

    bool basics_page::handle(const luil::app_message& message)
    {
        if (message.get<increment_intent>() != nullptr)
        {
            ++clicks_;
            return true;
        }
        if (message.get<counter_reset_intent>() != nullptr)
        {
            clicks_ = 0;
            return true;
        }
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            // 편집 메시지는 target으로 나눠 갖는다.
            // 남의 칸이면 다음 페이지(창 페이지)가 받는다.
            if (edit->request.target != target_number && edit->request.target != target_note)
                return false;
            apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_number && composition->event.target != target_note)
                return false;
            apply_composition(composition->event);
            return true;
        }
        if (const auto* const dialog { message.get<dialog_intent>() }; dialog != nullptr)
        {
            open_dialog(dialog->open);
            return true;
        }
        if (const auto* const move { message.get<move_dialog_intent>() }; move != nullptr)
        {
            dialog_x_ += move->dx;
            dialog_y_ += move->dy;
            return true;
        }
        if (const auto* const file { message.get<note_file_intent>() }; file != nullptr)
        {
            // 드롭은 칸의 글을 통째로 바꾼다.
            // 편집 명령 둘(모두 고르고 넣기)로 적어 실행 취소가 그대로 성립한다.
            luil::text_edit_request request {};
            request.target = target_note;
            request.command = luil::text::text_edit_command::select_all;
            luil::apply_text_edit(note_, request);
            request.command = luil::text::text_edit_command::insert;
            request.text = file->path;
            luil::apply_text_edit(note_, request);
            return true;
        }
        return false;
    }

    void basics_page::apply_edit(const luil::text_edit_request& request)
    {
        if (request.target == target_number)
            luil::apply_text_edit(number_, request, keep_digits);
        else if (request.target == target_note)
            luil::apply_text_edit(note_, request);
    }

    void basics_page::apply_composition(const luil::text_composition_event& event)
    {
        // 조합이 끝나면 표시 상태를 버린다.
        // 확정된 글은 별도의 편집 요청으로 이미 들어왔다.
        if (event.composing == false)
        {
            composition_.reset();
            return;
        }
        composition_ = event;
    }

    void basics_page::open_dialog(const bool open)
    {
        // 열 때마다 가운데에서 시작한다.
        if (open && dialog_open_ == false)
        {
            dialog_x_ = 0.0f;
            dialog_y_ = 0.0f;
        }
        dialog_open_ = open;
    }



    luil::text_input_view basics_page::make_input_view(const luil::text::text_edit_state& state, const luil::text_input_target target) const
    {
        return luil::make_text_input_view(state, composition_, target);
    }

    std::unique_ptr<luil::ui_element> basics_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"basics" }, config) };
        column->add(make_counter_row(), 32.0f);
        column->add_gap(16.0f);
        column->add(make_input_row(), 50.0f);
        column->add_gap(22.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"basics-hint" }, u8"확인 dialog는 캡션을 잡아 옮기고 Esc로 닫는다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(10.0f);

        auto open { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_open }, luil::text_button_config { .text = u8"카운터 초기화" }) };
        open->set_tooltip(u8"확인 dialog를 연다");
        open->set_cursor(luil::ui_cursor::hand);
        open->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { true }));
        column->add(std::move(open), { .length = 32.0f, .cross_length = 140.0f });
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_counter_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 12.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"counter" }, config) };

        luil::button_config button_config {};
        button_config.glyph = luil::codicons::icon_add;
        button_config.corner_radius = 3.0f;
        auto button { std::make_unique<luil::button_element>(luil::ui_element_id { kind_increment }, button_config) };
        button->set_tooltip(u8"하나 더한다");
        button->set_cursor(luil::ui_cursor::hand);
        button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(button), 32.0f);

        row->add_flexible(make_label(luil::ui_element_id { kind_counter_label }, u8"버튼을 누른 횟수: " + to_u8(clicks_), 14.0f, luil::label_color_role::primary));
        return row;
    }








    std::unique_ptr<luil::stack_element> basics_page::make_input_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 24.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inputs" }, config) };
        row->add(make_labeled_input(u8"number", u8"숫자만 받는 칸", kind_number_input, make_input_view(number_, target_number), u8"0-9"), 160.0f);

        // OS에서 끌어 온 파일을 받는 칸이다 (os-dragdrop-design.md).
        // 수락은 "밖에서 온 끌기인가" 한 줄이고, 놓이면 첫 경로가 칸의 글이 된다.
        luil::drop_target note_drop {};
        note_drop.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
        note_drop.on_drop = [](const luil::drag_payload& payload, const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::make_app_action(note_file_intent { payload.files.front() }) };
        };
        // 창이 좁아져도 이 칸이 사라지지 않는다.
        // 하한에 걸리면 그만큼 넘치는데, 폭 0으로 없어지는 것보다는 낫다 — 그
        // 판단은 앱의 것이고 라이브러리는 넘침을 만들 뿐이다.
        row->add(make_labeled_input(u8"note", u8"아무 글이나 받는 칸 (IME 조합·붙여넣기·파일 드롭)", kind_note_input, make_input_view(note_, target_note), u8"메모", std::move(note_drop)),
            { .weight = 1.0f, .minimum = 160.0f });
        return row;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_input(
        std::u8string owner, std::u8string label, const luil::ui_element_kind input_kind, luil::text_input_view view, std::u8string placeholder, luil::drop_target drop) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(label), 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { input_kind }, std::move(view), luil::text_input_config { .placeholder = std::move(placeholder) }) };
        // 드롭 대상은 온 칸에만 단다 — 없는 것은 두지 않는다.
        if (drop.accepts && drop.on_drop)
            input->set_drop_target(std::move(drop));
        column->add(std::move(input), 28.0f);
        return column;
    }

    void basics_page::add_overlay(luil::root_element& root, const float width, const float height, const float scale)
    {
        if (dialog_open_ == false)
            return;

        // dialog를 만들 때 캡션의 생김새를 정한다.
        // 라이브러리는 없는 것을 두지 않는다.
        luil::dialog_caption_config caption {};
        caption.title = u8"확인";
        caption.close_tooltip = u8"닫기";
        caption.close = luil::make_message_action(dialog_intent { false });
        caption.move = [](const float dx, const float dy) { return luil::make_app_action(move_dialog_intent { dx, dy }); };

        // 캡션이 있으면 그만큼 키가 커진다.
        // height_for는 논리 픽셀이다 (다른 element의 정적 사이저와 같은 단위).
        const float caption_height { luil::dialog_caption_element::height_for(caption) };
        const float dialog_width { 360.0f };
        const float dialog_height { caption_height + 148.0f };

        // 창 밖으로 나가지 않도록 다듬고, 다듬은 값을 상태에 되돌려
        // 다음 이동이 화면 밖에서부터 시작하지 않게 한다.
        //  - 가운데 배치는 host가 하므로 앱이 아는 것은 "가운데에서 얼마나
        //    밀어낼 수 있는가"뿐이고, 그 여유는 좌우가 같다.
        const float free_x { (width / scale - dialog_width) / 2.0f };
        const float free_y { (height / scale - dialog_height) / 2.0f };
        dialog_x_ = clamp_offset(dialog_x_, -free_x, free_x);
        dialog_y_ = clamp_offset(dialog_y_, -free_y, free_y);

        // 캡션은 여백 없이 위를 다 차지하고, 본문이 나머지를 갖는다.
        luil::stack_config content_config {};
        auto content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"dialog" }, content_config) };
        content->add(std::make_unique<luil::dialog_caption_element>(caption), caption_height);
        content->add_flexible(make_dialog_body());

        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 8.0f;
        auto dialog { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_dialog }, std::move(surface_panel)) };
        dialog->set_content(std::move(content));

        // scrim·초점 가둠·Esc는 host의 몫이다.
        // 앱이 정하는 것은 "무엇을 닫는가"뿐이다.
        luil::modal_host_config modal {};
        modal.outside = luil::make_message_action(dialog_intent { false });
        modal.dismiss = luil::make_message_action(dialog_intent { false });
        // 뜨면 취소에 서고, 닫히면 이 dialog를 연 버튼으로 돌아간다.
        //  - 진입 자리는 **취소**다. 되돌릴 수 없는 쪽(초기화)에 키보드를 미리
        //    올려 두면 무심코 친 Space가 카운터를 지운다. Enter를 채움으로
        //    말하는 기본 버튼과 초점이 서는 자리는 서로 다른 것이다.
        //  - 되돌아갈 자리는 앱이 추측하지 않는다. 여는 계기가 그 버튼의 클릭이라
        //    이름을 이미 손에 쥐고 있고, dialog를 짓는 이 함수 안에 그 이름이 있다.
        modal.focus_entry = luil::ui_element_id { kind_dialog_cancel };
        modal.focus_return = luil::ui_element_id { kind_dialog_open };
        modal.content_width = dialog_width;
        modal.content_height = dialog_height;
        modal.offset_x = dialog_x_;
        modal.offset_y = dialog_y_;
        auto host { std::make_unique<luil::modal_host_element>(std::move(modal)) };
        host->set_content(std::move(dialog));
        host->arrange({ { 0.0f, 0.0f, width, height }, scale });
        root.add(std::move(host));
    }

    std::unique_ptr<luil::stack_element> basics_page::make_dialog_body()
    {
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(20.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"dialog-body" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"dialog-title" }, u8"카운터를 0으로 되돌릴까요?", 13.0f, luil::label_color_role::primary), 22.0f);
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"dialog-note" }, u8"누른 횟수가 사라진다. Esc로 닫고 Enter로 확인한다.", 11.0f, luil::label_color_role::dim), 20.0f);
        column->add_flexible_gap();
        column->add(make_dialog_buttons(), 30.0f);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_dialog_buttons()
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 8.0f;
        // 버튼 둘을 오른쪽 끝에 붙인다.
        // 선두 `add_flexible_gap()`도 같은 자리를 주지만 그것은 항목 **사이**의
        // 도구다 — 바깥 자리는 정렬이 말한다.
        config.main_alignment = luil::stack_main_alignment::end;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"dialog-buttons" }, config) };

        auto cancel { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_cancel }, luil::text_button_config { .text = u8"취소" }) };
        cancel->set_cursor(luil::ui_cursor::hand);
        cancel->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { false }));
        row->add(std::move(cancel), 88.0f);

        // Enter로 실행되는 버튼임을 강조색 채움이 말한다.
        // 초점 테는 취소에 서 있으므로, 채움과 테가 한 화면에서 서로 다른 것을
        // 말하는 자리가 바로 여기다 (enter-default-design.md).
        luil::text_button_config confirm_config {};
        confirm_config.text = u8"초기화";
        confirm_config.visual = luil::text_button_visual::accent;
        confirm_config.default_button = true;
        auto confirm { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_confirm }, std::move(confirm_config)) };
        confirm->set_cursor(luil::ui_cursor::hand);
        confirm->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return {
                luil::make_app_action(counter_reset_intent {}),
                luil::make_app_action(dialog_intent { false }),
            };
        });
        row->add(std::move(confirm), 88.0f);
        return row;
    }
} // namespace demo

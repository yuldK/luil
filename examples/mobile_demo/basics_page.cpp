#include "mobile_demo/basics_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/modal_host_element.h"
#include "mobile_demo/assets.h"

#include <array>
#include <chrono>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace mobile_demo {
    namespace {
        // 그림·움직이는 그림·텍스트 칸의 높이다 (논리 픽셀).
        constexpr float picture_height { 90.0f };
        constexpr float film_height { 120.0f };
        constexpr float input_height { 40.0f };
        constexpr float input_block_height { note_line_height + 4.0f + input_height };

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

        // 가로는 색상, 세로는 밝기가 변하는 견본 이미지다 (앱에 실린 바이트, assets.h).
        // fit 설정에 따른 여백과 잘림의 차이를 확인할 수 있다.
        [[nodiscard]] luil::ui_image load_demo_picture(luil::image_decode_error& error)
        {
            // 그림 칸이 220 논리 픽셀이라 휴대폰 배율 3.5까지 쳐도 800이면 넉넉하다 — 480×240 원본이
            // 그대로 들어온다.
            return luil::decode_image_bytes(gradient_png(), { .max_width = 800, .max_height = 800 }, error);
        }

        // 움직이는 견본 하나를 읽는다 (webp·gif).
        //  - **정지 그림과 같은 문을 쓰지 않는다.** 정지 쪽 문은 여러 장이 든 바이트에서도 첫 장만
        //    내주므로 그림이 서 버린다 (image_decode.h).
        //  - 상한은 **들고 있을** 크기를 정한다. 움직이는 그림에서는 그 값에 장 수가 곱해진다.
        [[nodiscard]] luil::ui_animated_image load_demo_film(const std::span<const std::uint8_t> bytes, luil::image_decode_error& error)
        {
            return luil::decode_animated_image_bytes(bytes, { .max_height = 360 }, error);
        }
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
        if (message.get<playback_toggle_intent>() != nullptr)
        {
            toggle_playback();
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

    void basics_page::read_films()
    {
        films_read_ = true;
        sweep_ = load_demo_film(sweep_webp(), film_error_);
        spinner_ = load_demo_film(spinner_gif(), film_error_);
        // **값을 채우는 것이 곧 재생의 시작이다** (image_element.h). 읽은 것이
        // 하나도 없으면 시작하지 않는다 — 그러면 element가 다음 장을 예고하지
        // 않아 창이 그대로 잠잔다.
        if (sweep_.valid() || spinner_.valid())
            playback_.started = std::chrono::steady_clock::now();
    }

    void basics_page::toggle_playback()
    {
        const std::chrono::steady_clock::time_point now { std::chrono::steady_clock::now() };
        // 멈추는 것은 시각 하나를 적는 일이라 라이브러리에 함수가 없다. 산수가
        // 있는 쪽은 잇는 쪽뿐이다 — 멈춰 있던 만큼 시작 시각을 밀어 **선 자리에서**
        // 이어 간다 (그러지 않으면 다시 돌리는 순간 멈춰 있던 만큼 건너뛴다).
        if (playback_.paused_at.has_value())
        {
            playback_ = luil::playback_resumed(playback_, now);
            return;
        }
        playback_.paused_at = now;
    }

    luil::text_input_view basics_page::make_input_view(const luil::text::text_edit_state& state, const luil::text_input_target target) const
    {
        return luil::make_text_input_view(state, composition_, target);
    }

    page_content basics_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        // 그림은 한 번만 읽는다. frame마다 읽으면 디코딩이 매 frame 선다 (image-decode-design.md).
        //  - `valid()`로 물으면 **읽지 못한** 바이트를 frame마다 다시 푼다. 실패도 한 번이어야
        //    하므로 읽었다는 사실을 따로 기억한다.
        if (picture_read_ == false)
        {
            picture_read_ = true;
            picture_ = load_demo_picture(picture_error_);
        }
        // 움직이는 그림도 같은 규칙으로 한 번만 읽는다. 한 편이 장 수만큼의 디코딩이다.
        if (films_read_ == false)
            read_films();

        page_column column { u8"basics" };
        column.add(make_counter_row(), touch_height);
        column.gap(20.0f);

        // 텍스트 칸 둘이다. 휴대폰은 좁아 위아래로 쌓는다.
        column.add(make_labeled_input(u8"number", u8"숫자만 받는 칸", kind_number_input, make_input_view(number_, target_number), u8"0-9"), input_block_height);
        column.gap(12.0f);
        column.add(make_labeled_input(u8"note", u8"아무 글이나 받는 칸 (IME·붙여넣기)", kind_note_input, make_input_view(note_, target_note), u8"메모"), input_block_height);
        column.gap(24.0f);

        // 그림 둘을 나란히 둔다. 칸은 폭의 절반씩이다.
        column.add(make_pair_row(u8"pictures", make_labeled_picture(u8"contain", u8"contain — 남는 자리는 바탕", luil::image_fit::contain, u8"그러데이션 견본, 전부 보이게"),
                       make_labeled_picture(u8"cover", u8"cover — 넘친 만큼 잘림", luil::image_fit::cover, u8"그러데이션 견본, 칸을 덮게")),
            note_line_height + 4.0f + picture_height);
        column.gap(4.0f);
        // 읽지 못한 그림은 조용히 비지 않는다 — 이유가 화면에 남는다.
        if (picture_.valid() == false)
            column.body(u8"picture-status", u8"그림을 읽지 못했다 — " + picture_error_.message);
        else
            column.note(u8"picture-status", u8"gradient.png " + to_u8(picture_.width()) + u8"×" + to_u8(picture_.height()) + u8" 픽셀");
        column.gap(20.0f);

        // 움직이는 그림 둘이다. 형식을 둘 다 세운다 (webp, gif). gif는 배경이 비쳐 테마를 바꾸면 같은
        // 그림이 다른 바탕 위에서 돈다.
        column.add(make_pair_row(u8"films", make_labeled_film(u8"sweep", u8"webp (16장 · 80 ms)", sweep_, u8"띠 하나가 왼쪽에서 오른쪽으로 훑고 지나가는 그러데이션"),
                       make_labeled_film(u8"spinner", u8"gif (12장 · 90 ms)", spinner_, u8"점 열둘이 둥글게 놓이고 밝은 점 하나가 시계 방향으로 도는 표시")),
            note_line_height + 4.0f + film_height);
        column.gap(4.0f);
        if (sweep_.valid() == false || spinner_.valid() == false)
            column.body(u8"film-status", u8"움직이는 그림을 읽지 못했다 — " + film_error_.message);
        else
            column.note(u8"film-status", u8"한 재생 시계를 타고 장이 바뀔 때만 깬다.");
        // 멈출 것이 없으면 단추도 두지 않는다 — 눌러도 아무 일도 없는 단추는 고장으로 보인다.
        if (sweep_.valid() || spinner_.valid())
        {
            column.gap(8.0f);
            column.add(make_playback_button(), touch_height, 160.0f);
        }
        column.gap(24.0f);

        column.note(u8"basics-hint", u8"확인 dialog는 캡션을 끌어 옮긴다.");
        column.gap(8.0f);
        auto open { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_open }, luil::text_button_config { .text = u8"카운터 초기화" }) };
        open->set_tooltip(u8"확인 dialog를 연다");
        open->set_cursor(luil::ui_cursor::hand);
        open->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { true }));
        column.add(std::move(open), touch_height, std::min(width - 2.0f * page_padding, 180.0f));
        return column.finish();
    }

    std::unique_ptr<luil::stack_element> basics_page::make_counter_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 12.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"counter" }, config) };

        luil::button_config button_config {};
        button_config.glyph = luil::codicons::icon_add;
        button_config.corner_radius = 4.0f;
        auto button { std::make_unique<luil::button_element>(luil::ui_element_id { kind_increment }, button_config) };
        button->set_tooltip(u8"하나 더한다");
        button->set_cursor(luil::ui_cursor::hand);
        button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(button), touch_height);

        row->add_flexible(make_label(luil::ui_element_id { kind_counter_label }, u8"누른 횟수: " + to_u8(clicks_), body_text_size, luil::label_color_role::primary));
        return row;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_pair_row(std::u8string owner, std::unique_ptr<luil::ui_element> left, std::unique_ptr<luil::ui_element> right) const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 16.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, std::move(owner) }, config) };
        row->add_flexible(std::move(left));
        row->add_flexible(std::move(right));
        return row;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_picture(std::u8string owner, std::u8string label, const luil::image_fit fit, std::u8string description) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"picture-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"picture-" + owner }, std::move(label), note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_gap(4.0f);
        column->add(
            std::make_unique<luil::image_element>(luil::ui_element_id { kind_picture, std::move(owner) }, luil::image_config { .image = picture_, .fit = fit, .description = std::move(description) }),
            picture_height);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_film(std::u8string owner, std::u8string label, const luil::ui_animated_image& film, std::u8string description) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"film-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"film-" + owner }, std::move(label), note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_gap(4.0f);
        // element는 재생 시계를 들지 않는다. frame마다 이 config에 실리는 것이 시계이고, 어느 장인지는
        // 그리는 시각에서 매번 새로 나온다 (image_element.h). 그래서 필름 둘에 같은 시계를 실으면 둘이
        // 함께 돌고 함께 선다 — 멈춤 단추가 하나로 족한 이유다.
        column->add(std::make_unique<luil::image_element>(
                        luil::ui_element_id { kind_picture, std::move(owner) }, luil::image_config { .description = std::move(description), .animation = film, .playback = playback_ }),
            film_height);
        return column;
    }

    std::unique_ptr<luil::ui_element> basics_page::make_playback_button() const
    {
        // 단추의 글은 **누르면 무엇이 되는지**를 말한다.
        const bool paused { playback_.paused_at.has_value() };
        luil::text_button_config button_config {};
        button_config.text = paused ? u8"이어서 돌린다" : u8"잠시 멈춘다";
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_playback_toggle }, std::move(button_config)) };
        button->set_tooltip(paused ? u8"멈춘 자리에서 이어 간다" : u8"보이는 장에 멈춘다");
        button->set_cursor(luil::ui_cursor::hand);
        button->set_action(luil::ui_trigger::left_click, luil::make_message_action(playback_toggle_intent {}));
        return button;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_input(
        std::u8string owner, std::u8string label, const luil::ui_element_kind input_kind, luil::text_input_view view, std::u8string placeholder) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(label), note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_gap(4.0f);
        column->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { input_kind }, std::move(view), luil::text_input_config { .placeholder = std::move(placeholder) }), input_height);
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
        // 좁은 휴대폰에서는 양옆 여백만 남기고 화면 폭에 맞춘다.
        const float dialog_width { std::min(360.0f, width / scale - 2.0f * page_padding) };
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
        // dialog의 이름이다. 화면 읽기가 뜨는 순간 말하는 말이라 캡션과 같은 말을 적는다.
        modal.name = caption.title;
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
        column->add(make_label(luil::ui_element_id { kind_text, u8"dialog-title" }, u8"카운터를 0으로 되돌릴까요?", body_text_size, luil::label_color_role::primary), body_line_height);
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"dialog-note" }, u8"누른 횟수가 사라진다.", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_flexible_gap();
        column->add(make_dialog_buttons(), 40.0f);
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
} // namespace mobile_demo

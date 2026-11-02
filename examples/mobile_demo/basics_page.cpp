#include "mobile_demo/basics_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/message_box.h"
#include "luil/ui/number_input.h"
#include "luil/ui/text_area_element.h"
#include "mobile_demo/assets.h"

#include <algorithm>
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
        [[nodiscard]] float scaled_input_height() noexcept
        {
            return luil::text_line_height(11.0f, input_height - 11.0f);
        }

        [[nodiscard]] float input_block_height() noexcept
        {
            return luil::text_line_height(note_text_size, note_line_height - note_text_size) + 4.0f + scaled_input_height();
        }

        // 숫자 칸의 범위다. 붙여넣기와 IME 확정은 라이브러리 거르기(`number_input_filter`)가 숫자만 남긴다 — 전각 숫자도 ASCII로 옮긴다.
        [[nodiscard]] luil::number_input_range number_range()
        {
            return { .minimum = 0.0 };
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
    }

    bool basics_page::handle(const luil::app_message& message)
    {
        if (message.get<increment_intent>() != nullptr)
        {
            ++clicks_;
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
            if (edit->request.target != target_number && edit->request.target != target_note && edit->request.target != target_secret && edit->request.target != target_memo
                && edit->request.target != target_code && edit->request.target != target_line_break)
                return false;
            apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_number && composition->event.target != target_note && composition->event.target != target_secret
                && composition->event.target != target_memo && composition->event.target != target_code && composition->event.target != target_line_break)
                return false;
            apply_composition(composition->event);
            return true;
        }
        if (const auto* const policy { message.get<demo::line_break_policy_intent>() }; policy != nullptr)
        {
            line_break_policy_ = policy->policy;
            return true;
        }
        if (const auto* const dialog { message.get<dialog_intent>() }; dialog != nullptr)
        {
            open_dialog(dialog->open);
            return true;
        }
        if (const auto* const result { message.get<dialog_result_intent>() }; result != nullptr)
        {
            // 단추, Esc, 바깥 누름, 뒤로 가기가 모두 이 메시지 하나로 온다. 받아서 닫고 key로 가른다.
            dialog_result_ = result->key;
            if (dialog_ == dialog_kind::reset && result->key == u8"reset")
                clicks_ = 0;
            dialog_ = dialog_kind::none;
            return true;
        }
        if (message.get<dialog_check_intent>() != nullptr)
        {
            ask_again_ = ask_again_ == false;
            return true;
        }
        if (const auto* const reveal { message.get<secret_reveal_intent>() }; reveal != nullptr)
        {
            secret_reveal_ = reveal->reveal;
            return true;
        }
        if (const auto* const scroll { message.get<memo_scroll_intent>() }; scroll != nullptr)
        {
            // 범위 다듬기는 칸이 한다. 아래로 넘친 값만 0으로 막는다.
            memo_scroll_ = std::max(0.0f, memo_scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll_to { message.get<memo_scroll_to_intent>() }; scroll_to != nullptr)
        {
            memo_scroll_ = std::max(0.0f, scroll_to->offset);
            return true;
        }
        if (const auto* const scroll { message.get<code_scroll_intent>() }; scroll != nullptr)
        {
            code_scroll_ = std::max(0.0f, code_scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll_to { message.get<code_scroll_to_intent>() }; scroll_to != nullptr)
        {
            code_scroll_ = std::max(0.0f, scroll_to->offset);
            return true;
        }
        return false;
    }

    void basics_page::apply_edit(const luil::text_edit_request& request)
    {
        if (request.target == target_number)
            luil::apply_text_edit(number_, request, luil::number_input_filter(number_range()));
        else if (request.target == target_note)
            luil::apply_text_edit(note_, request);
        else if (request.target == target_secret)
            luil::apply_text_edit(secret_, request);
        else if (request.target == target_memo)
            luil::apply_text_edit(memo_, request);
        else if (request.target == target_line_break)
            luil::apply_text_edit(line_break_, request);
        else if (request.target == target_code)
        {
            luil::apply_text_edit(code_, request);
            // 편집을 적용한 같은 logic turn에서 구간을 다시 칠한다. 늦으면 칸이 한 frame 낡은 offset으로 그린다.
            highlighter_.update(code_.text);
        }
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

    void basics_page::open_dialog(const dialog_kind kind)
    {
        dialog_ = kind;
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
        // 코드 칸의 구간이다. 처음 한 번 칠하고, 그 뒤는 편집이 닿을 때마다 `apply_edit`가 다시 칠한다.
        if (code_painted_ == false)
        {
            code_painted_ = true;
            highlighter_.update(code_.text);
        }

        page_column column { u8"basics" };
        column.add(make_counter_row(), touch_height);
        column.gap(20.0f);

        // 텍스트 칸 셋이다. 휴대폰은 좁아 위아래로 쌓는다.
        column.add(make_labeled_input(u8"number", u8"숫자만 받는 칸", kind_number_input, make_input_view(number_, target_number),
                       { .placeholder = u8"0-9", .input = { .kind = luil::text_input_kind::number, .action = luil::text_input_action::next } }),
            input_block_height());
        column.gap(12.0f);
        column.add(make_labeled_input(u8"note", u8"아무 글이나 받는 칸 (IME·붙여넣기)", kind_note_input, make_input_view(note_, target_note),
                       { .placeholder = u8"메모", .input = { .action = luil::text_input_action::next } }),
            input_block_height());
        column.gap(12.0f);
        // 암호 칸이다. 점으로 그리고 복사하지 않으며, 키보드가 배우지 않는다. 눈 단추로 보인다.
        const luil::ui_action reveal { luil::make_message_action(secret_reveal_intent { secret_reveal_ == false }) };
        column.add(make_labeled_input(
                       u8"secret", u8"암호 칸 (가림·복사 막기·암호 키보드)", kind_secret_input, make_input_view(secret_, target_secret), make_secret_input_config(u8"암호", secret_reveal_, reveal)),
            input_block_height());
        column.gap(12.0f);
        // 여러 줄 메모다. 소프트 키보드의 Enter가 줄을 나누고, 칸 안을 쓸어 넘기면 흐른다.
        column.note(u8"memo", u8"여러 줄 메모 (Enter 줄 바꿈)");
        column.gap(4.0f);
        luil::text_area_config memo {};
        memo.placeholder = u8"여러 줄 메모";
        memo.scroll_offset = memo_scroll_;
        memo.scroll = [](const float delta) { return luil::make_app_action(memo_scroll_intent { delta }); };
        memo.scroll_to = [](const float offset) { return luil::make_app_action(memo_scroll_to_intent { offset }); };
        memo.wrap_cache = memo_wrap_;
        column.add(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo_input }, make_input_view(memo_, target_memo), std::move(memo)), 96.0f);
        column.gap(24.0f);

        // 코드 칸이다. 소프트 키보드로 치면 편집한 줄부터 다시 칠한다. 상태 줄은 마지막 편집이 렉서에 보낸 줄 수까지 그린다.
        column.note(u8"code", u8"코드 칸 (구문 강조 · 키워드는 굵게 · 주석은 기울임)");
        column.gap(4.0f);
        const auto code_spans { highlighter_.spans() };
        luil::text_area_config code {};
        code.placeholder = u8"코드 칸";
        code.face = luil::text_face::code;
        code.scroll_offset = code_scroll_;
        code.scroll = [](const float delta) { return luil::make_app_action(code_scroll_intent { delta }); };
        code.scroll_to = [](const float offset) { return luil::make_app_action(code_scroll_to_intent { offset }); };
        code.wrap_cache = code_wrap_;
        code.spans = code_spans;
        column.add(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_code_input }, make_input_view(code_, target_code), std::move(code)), 160.0f);
        column.gap(4.0f);
        // 상태 줄은 폭이 좁은 휴대폰에서 두 줄로 접는다.
        luil::label_config code_status {};
        code_status.text = demo::code_status_text(demo::count_code_spans(*code_spans), highlighter_.stats().relexed);
        code_status.font_size = note_text_size;
        code_status.color = luil::label_color_role::dim;
        code_status.wrap = true;
        column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"code-status" }, std::move(code_status)), 2.0f * note_line_height);
        column.gap(24.0f);

        // 줄 나눔 칸이다. 같은 글을 같은 폭의 여러 줄 label과 여러 줄 칸에 위아래로 놓고, 스위치로 정책을 바꾼다.
        // 상태 줄은 이 배율에서 잰 줄 수와 label 첫 줄의 끝 글자를 그린다 (line-break-design.md).
        column.note(u8"line-break", u8"줄 나눔 (한국어는 어절 · 닫는 부호는 줄 머리에 두지 않는다)");
        column.gap(4.0f);
        {
            const bool syllables { line_break_policy_ == luil::text::line_break_policy::unicode };
            luil::check_config policy {};
            policy.owner = u8"line-break-policy";
            policy.style = luil::check_style::toggle_switch;
            policy.label = u8"한글 음절 사이에서도 바꾸기 (unicode)";
            policy.checked = syllables;
            policy.toggle = luil::make_message_action(demo::line_break_policy_intent { syllables ? luil::text::line_break_policy::hangul_words : luil::text::line_break_policy::unicode });
            const float policy_height { luil::check_element::height_for(policy) };
            column.add(std::make_unique<luil::check_element>(std::move(policy)), std::max(policy_height, touch_height), std::min(width - 2.0f * page_padding, 360.0f));
            column.gap(6.0f);
            luil::label_config label { demo::line_break_label_config(line_break_.text, line_break_policy_) };
            auto area { std::make_unique<luil::text_area_element>(
                luil::ui_element_id { kind_line_break_input }, make_input_view(line_break_, target_line_break), demo::line_break_area_config(line_break_policy_, line_break_wrap_))
            };
            area->set_access_name(u8"줄 나눔 칸");
            const std::u8string status { demo::measure_line_break_status(line_break_policy_, label, *area, scale) };
            const float label_height { luil::label_element::measure_wrap(label, luil::measure_context { .scale = scale }, demo::line_break_cell_width).height };
            column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"line-break-label" }, std::move(label)), label_height, demo::line_break_cell_width);
            column.gap(6.0f);
            column.add(std::move(area), demo::line_break_area_height, demo::line_break_cell_width);
            column.gap(4.0f);
            luil::label_config line_break_status {};
            line_break_status.text = status;
            line_break_status.font_size = note_text_size;
            line_break_status.color = luil::label_color_role::dim;
            line_break_status.wrap = true;
            column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"line-break-status" }, std::move(line_break_status)), 2.0f * note_line_height);
        }
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

        column.note(u8"basics-hint", u8"확인 대화 상자는 뒤로 가기나 바깥을 눌러 닫는다. 초기화 대화는 취소에 초점이 선다.");
        column.gap(8.0f);
        auto open { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_open }, luil::text_button_config { .text = u8"카운터 초기화" }) };
        open->set_tooltip(u8"확인 대화 상자를 연다");
        open->set_cursor(luil::ui_cursor::hand);
        open->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { dialog_kind::reset }));
        column.add(std::move(open), touch_height, std::min(width - 2.0f * page_padding, 180.0f));
        column.gap(8.0f);
        auto details { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_details_open }, luil::text_button_config { .text = u8"긴 확인 대화" }) };
        details->set_tooltip(u8"긴 본문과 단추 셋과 확인 칸이 있는 대화를 연다");
        details->set_cursor(luil::ui_cursor::hand);
        details->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { dialog_kind::details }));
        column.add(std::move(details), touch_height, std::min(width - 2.0f * page_padding, 180.0f));
        // 대화 상태를 글로도 보인다. 화면과 보조 기술이 같은 말을 읽는다.
        column.gap(8.0f);
        column.note(u8"dialog-state", dialog_open() ? u8"대화: 열림" : u8"대화: 닫힘");
        column.gap(4.0f);
        column.note(u8"dialog-result", u8"마지막 결과: " + (dialog_result_.empty() ? std::u8string { u8"없음" } : dialog_result_));
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
        std::u8string owner, std::u8string label, const luil::ui_element_kind input_kind, luil::text_input_view view, luil::text_input_config input) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(label), note_text_size, luil::label_color_role::dim),
            luil::text_line_height(note_text_size, note_line_height - note_text_size));
        column->add_gap(4.0f);
        column->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { input_kind }, std::move(view), std::move(input)), scaled_input_height());
        return column;
    }

    luil::message_box_config basics_page::make_dialog_config() const
    {
        luil::message_box_config box {};
        // 닫는 모든 계기(단추, Esc, 바깥 누름, 뒤로 가기)가 이 함수 하나를 부른다.
        box.result = [](const std::u8string_view key) { return luil::make_app_action(dialog_result_intent { std::u8string { key } }); };
        // 터치 앱은 단추를 손가락 크기로 키운다.
        box.control_size = touch_height;
        if (dialog_ == dialog_kind::details)
        {
            box.owner = u8"details";
            box.severity = luil::ui_severity::info;
            box.title = u8"변경 내용을 저장할까요?";
            box.body = u8"이 문서에는 저장하지 않은 변경이 있다. 저장하면 변경이 파일에 쓰이고, 저장하지 않고 닫으면 변경이 사라진다.\n"
                       u8"나중에 다시 묻기를 고르면 대화만 닫고 문서는 그대로 둔다. 좁은 화면에서는 단추가 위에서 아래로 쌓이고, 본문이 길어도 높이는 내용에서 나온다.";
            box.buttons = {
                luil::message_box_button { .key = u8"later", .text = u8"나중에 다시 묻기", .role = luil::message_box_role::reject },
                luil::message_box_button { .key = u8"discard", .text = u8"저장하지 않고 닫기", .role = luil::message_box_role::destructive },
                luil::message_box_ok(u8"저장"),
            };
            box.check = luil::message_box_check { .label = u8"다시 묻지 않기", .checked = ask_again_, .toggle = luil::make_message_action(dialog_check_intent {}) };
            box.focus_return = luil::ui_element_id { kind_dialog_details_open };
            return box;
        }
        if (dialog_ == dialog_kind::reset)
        {
            box.owner = u8"reset";
            box.severity = luil::ui_severity::warning;
            box.title = u8"카운터를 0으로 되돌릴까요?";
            box.body = u8"누른 횟수가 사라진다.";
            // 위험한 단추는 Enter의 대상도 첫 초점도 되지 않는다. 뜨면 취소에 선다.
            box.buttons = { luil::message_box_cancel(), luil::message_box_button { .key = u8"reset", .text = u8"초기화", .role = luil::message_box_role::destructive } };
            box.focus_return = luil::ui_element_id { kind_dialog_open };
        }
        return box;
    }

    luil::input_action basics_page::back_action() const
    {
        // 뒤로 가기도 Esc와 같은 결과를 낸다. 닫는 길이 없는 설정이면 앱 상태를 바로 닫는다.
        return luil::message_box_dismiss_message(make_dialog_config()).value_or(luil::make_app_action(dialog_intent {}));
    }

    void basics_page::add_overlay(luil::root_element& root, const float width, const float height, const float scale, const luil::rect_f& surface)
    {
        if (dialog_open() == false)
            return;

        // 크기는 내용에서 나온다. 앱은 높이도 폭을 줄이는 식도 넘기지 않고 배치만 한다.
        // 좁은 휴대폰에서는 host가 폭을 자리 - 32로 줄이고 단추가 넘치면 쌓는다.
        auto host { luil::make_message_box(make_dialog_config()) };
        host->arrange({ .slot = { 0.0f, 0.0f, width, height }, .scale = scale, .surface = surface });
        root.add(std::move(host));
    }
}

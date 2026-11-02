#pragma once

// 기본 페이지다: 버튼·카운터, 별점 둘(손가락 크기의 별 다섯, 가로로 흐르는 창 안의 별 열),
// 텍스트 입력 세 칸(숫자 전용·자유 입력·암호),
// 앱에 실린 그림(정지 한 장과 움직이는 둘), 화면을 덮는 확인 대화 상자 둘
// (`luil::make_message_box` — 크기는 내용에서 나오고 닫는 모든 계기가 한 결과 메시지를 낸다).

#include "demo/code_lexer.h"
#include "demo/line_break_sample.h"
#include "demo/readonly_undo_keys.h"
#include "luil/text/text_edit.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/ui/message_box.h"
#include "luil/ui/rating_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/syntax_highlighter.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_interaction.h"
#include "mobile_demo/common.h"

#include <memory>
#include <optional>
#include <vector>

namespace mobile_demo {
    // 재생을 멈추고 다시 잇는 단추다.
    // kind 등록부는 common.h이고 이 값도 그 번호를 그대로 잇는다 — 이 페이지
    // 밖에서 쓰는 곳이 없어 여기 섰을 뿐이라, 다른 페이지가 같은 것을 원하면
    // 그때 등록부로 옮긴다.
    constexpr luil::ui_element_kind kind_playback_toggle { luil::application_element_kind(30) };

    // --- 이 페이지의 메시지 ---
    // 텍스트 편집·조합 메시지(edit_intent·composition_intent)는 창 페이지와
    // target으로 나눠 가지므로 common.h에 있다.
    struct increment_intent
    {
    };

    // 열린 확인 대화 상자다. 열림은 앱 상태이고 하나만 열린다.
    //  - reset은 카운터 초기화(취소와 위험한 초기화), details는 긴 본문과 단추 셋과 확인 칸을 가진 대화다.
    enum class dialog_kind
    {
        none,
        reset,
        details,
    };

    // 대화를 열거나 닫는다. 값이 절대 값이라 거듭 와도 결과가 같다.
    struct dialog_intent
    {
        dialog_kind open { dialog_kind::none };
    };

    // 대화 상자를 닫는 모든 계기(단추, Esc, 바깥 누름, 뒤로 가기)가 내는 결과다 (`message_box_config::result`).
    // 앱은 이 메시지 하나를 받아 닫고 key로 가른다.
    struct dialog_result_intent
    {
        std::u8string key {};
    };

    // 둘째 대화의 "다시 묻지 않기" 칸을 뒤집는다. 칸은 닫지 않는다.
    struct dialog_check_intent
    {
    };

    // 별점의 값이다. key는 어느 별점인지(`stars` 별 다섯, `strip` 가로로 흐르는 창 안의 별 열)이다.
    // 값은 절대 값이라 몇 번 겹쳐 와도 마지막 값 그대로다.
    struct rating_intent
    {
        std::u8string key {};
        float value { 0.0f };
    };

    // 별 열을 담은 가로로 흐르는 창의 스크롤이다 (논리 픽셀, 양수는 내용이 왼쪽으로). 위치는 앱 상태다.
    struct rating_scroll_intent
    {
        float delta { 0.0f };
    };

    // 움직이는 그림을 멈추거나, 멈춘 자리에서 이어 돌린다.
    // 메시지가 하나인 것은 단추가 하나여서다 — 어느 쪽인지는 재생 시계가 이미
    // 알고 있고(`image_playback::paused_at`), 단추의 글이 그것을 그대로 말한다.
    struct playback_toggle_intent
    {
    };

    // 암호 칸의 글을 보이거나 가린다.
    struct secret_reveal_intent
    {
        bool reveal { false };
    };

    // 여러 줄 메모 칸의 세로 스크롤이다 (논리 픽셀). 위치는 앱 상태다.
    struct memo_scroll_intent
    {
        float delta { 0.0f };
    };

    struct memo_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // 코드 칸의 세로 스크롤이다 (논리 픽셀). 위치는 앱 상태다.
    struct code_scroll_intent
    {
        float delta { 0.0f };
    };

    struct code_scroll_to_intent
    {
        float offset { 0.0f };
    };

    class basics_page
    {
    public:
        // 이 페이지의 메시지를 처리했으면 참이다.
        bool handle(const luil::app_message& message);

        // 페이지 내용과 그 높이다. width는 내용 영역의 논리 픽셀 폭이다.
        [[nodiscard]] page_content build(float width, float scale);

        // dialog가 열려 있으면 창 전체를 덮는 modal host를 root에 얹는다.
        // 어느 페이지가 보이든 dialog는 창 전체를 덮는다.
        // `surface`는 그릴 수 있는 표면 전체다. scrim이 안전 영역 밖까지 닿는다.
        void add_overlay(luil::root_element& root, float width, float height, float scale, const luil::rect_f& surface);

        [[nodiscard]] bool dialog_open() const noexcept
        {
            return dialog_ != dialog_kind::none;
        }

        // 열린 대화 상자의 설정이다. 닫힌 때는 빈 설정이다.
        [[nodiscard]] luil::message_box_config make_dialog_config() const;
        // 뒤로 가기가 낼 메시지다. Esc와 바깥 누름이 내는 결과와 같다 (`luil::message_box_dismiss_message`).
        [[nodiscard]] luil::input_action back_action() const;

    private:
        void apply_edit(const luil::text_edit_request& request);
        void apply_composition(const luil::text_composition_event& event);
        void open_dialog(dialog_kind kind);
        // 움직이는 견본 둘을 읽고, 읽혔으면 그 자리에서 재생을 시작한다.
        void read_films();
        // 멈춰 있으면 잇고, 돌고 있으면 멈춘다.
        void toggle_playback();
        [[nodiscard]] luil::text_input_view make_input_view(const luil::text::text_edit_state& state, luil::text_input_target target) const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_counter_row() const;
        // 별점 설정이다. 손가락 크기(44)의 칸이고, 별 수가 다섯이면 폭 220이다.
        [[nodiscard]] luil::rating_config make_stars_config() const;
        [[nodiscard]] luil::rating_config make_strip_rating_config() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_rating_strip() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_pair_row(std::u8string owner, std::unique_ptr<luil::ui_element> left, std::unique_ptr<luil::ui_element> right) const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_picture(std::u8string owner, std::u8string label, luil::image_fit fit, std::u8string description) const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_film(std::u8string owner, std::u8string label, const luil::ui_animated_image& film, std::u8string description) const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_playback_button() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_input(
            std::u8string owner, std::u8string label, luil::ui_element_kind input_kind, luil::text_input_view view, luil::text_input_config input) const;

        // 그림은 만들 때 한 번 준비되는 불변 값이라 페이지가 들고 있는다 —
        // frame마다 config에 실리는 것은 손잡이뿐이다 (image-design.md).
        // 파일에서 읽으므로 실패할 수 있다: 읽었다는 사실과 실패 이유를 함께 든다
        // (`picture_`의 유효성으로 물으면 실패한 파일을 frame마다 다시 연다).
        //  - 이유는 글이 아니라 **값**이다 (`luil::image_decode_error`). 화면에
        //    적는 것은 그 안의 `message`뿐이지만, 갈래를 보고 다르게 굴어야 하는
        //    앱은 문장을 뒤지지 않고 `kind`를 본다.
        luil::ui_image picture_ {};
        luil::image_decode_error picture_error_ {};
        bool picture_read_ { false };
        // 움직이는 견본 둘이다 (webp 하나, gif 하나). 정지 그림과 같은 규칙으로
        // 한 번만 읽고, **읽었다는 사실**을 기억한다 — 성공을 기억하면 읽지 못한
        // 파일을 frame마다 다시 연다.
        //  - 오류 글이 하나뿐인 것은 계약이 그렇게 생겨서다: 성공하면 `error`를
        //    건드리지 않으므로(image_decode.h) 실패한 쪽만 여기 적히고, 뒤에 성공한
        //    쪽이 앞선 실패를 지우지 않는다.
        luil::ui_animated_image sweep_ {};
        luil::ui_animated_image spinner_ {};
        luil::image_decode_error film_error_ {};
        bool films_read_ { false };
        // 재생 시계다. 필름이 둘이어도 시계는 하나라 둘이 같은 장단에 돌고, 멈춤
        // 단추 하나가 둘을 함께 세운다 — 재생 위치가 element가 아니라 **앱**에
        // 있다는 것이 여기서 눈에 보인다 (사이드바의 transition과 같은 자리다).
        luil::image_playback playback_ {};
        luil::text::text_edit_state number_ {};
        luil::text::text_edit_state note_ {};
        luil::text::text_edit_state secret_ {};
        // 여러 줄 메모의 글과 세로 스크롤이다.
        luil::text::text_edit_state memo_ {};
        float memo_scroll_ { 0.0f };
        // 메모 칸이 접은 자리를 frame 사이에 쥔다. 바뀐 문단만 다시 잰다 (`text_area_wrap_cache`).
        std::shared_ptr<luil::text_area_wrap_cache> memo_wrap_ { std::make_shared<luil::text_area_wrap_cache>() };
        // 코드 칸의 글과 세로 스크롤이다. 구간은 도우미가 편집에 맞춰 다시 칠한다 (데스크톱 demo의 `code_lexer.h`를 함께 쓴다).
        luil::text::text_edit_state code_ { .text = std::u8string { demo::code_sample } };
        float code_scroll_ { 0.0f };
        std::shared_ptr<luil::text_area_wrap_cache> code_wrap_ { std::make_shared<luil::text_area_wrap_cache>() };
        luil::syntax_highlighter highlighter_ { demo::code_highlighter_config() };
        bool code_painted_ { false };
        // 줄 나눔 칸의 글과 정책이다 (데스크톱 demo의 `line_break_sample.h`를 함께 쓴다). label은 칸의 확정 글을 그린다.
        luil::text::text_edit_state line_break_ { .text = std::u8string { demo::line_break_sample } };
        luil::text::line_break_policy line_break_policy_ { luil::text::line_break_policy::hangul_words };
        std::shared_ptr<luil::text_area_wrap_cache> line_break_wrap_ { std::make_shared<luil::text_area_wrap_cache>() };
        // 읽기 전용 안내 칸의 글이다. 글은 고정이고 caret·선택만 바뀐다. 그 칸에 초점이 있는 채 앱 on_key에 닿은
        // Ctrl+Z·Ctrl+Y를 센다 (데스크톱 demo의 `readonly_undo_keys.h`를 함께 쓴다).
        luil::text::text_edit_state readonly_ {
            .text = u8"이 칸은 읽기 전용이다. 선택과 복사만 된다. 하드웨어 키보드로 이 칸에 초점을 두고 Ctrl+Z나 Ctrl+Y를 누르면 칸이 삼키지 않고 앱으로 넘긴다.",
        };
        demo::readonly_undo_keys undo_keys_ {};
        bool secret_reveal_ { false };
        std::optional<luil::text_composition_event> composition_ {};
        int clicks_ { 0 };
        // 별점 둘의 값이다. 0은 아직 매기지 않음이다.
        float rating_stars_ { 0.0f };
        float rating_strip_ { 0.0f };
        // 별 열을 담은 창이 흘러간 양과, 끝까지 흘린 값이다 (논리 픽셀). 끝은 창의 폭에 달려 조립할 때 안다.
        float rating_scroll_ { 0.0f };
        float rating_scroll_limit_ { 0.0f };
        dialog_kind dialog_ { dialog_kind::none };
        // 마지막으로 닫은 결과의 key다. 아직 닫은 적이 없으면 비었다.
        std::u8string dialog_result_ {};
        // 둘째 대화의 "다시 묻지 않기" 칸이다.
        bool ask_again_ { false };
    };
}

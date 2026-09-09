#pragma once

// 기본 페이지다: 버튼·카운터, 텍스트 입력 두 칸(숫자 전용·자유 입력),
// 파일에서 읽은 그림(정지 한 장과 움직이는 둘), 화면을 덮는 확인 dialog
// (캡션을 잡아 옮긴다).

#include "demo/common.h"
#include "luil/text/text_edit.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_interaction.h"

#include <optional>

namespace demo {
    // 재생을 멈추고 다시 잇는 단추다.
    // kind 등록부는 common.h이고 이 값도 그 번호를 그대로 잇는다 — 이 페이지
    // 밖에서 쓰는 곳이 없어 여기 섰을 뿐이라, 다른 페이지가 같은 것을 원하면
    // 그때 등록부로 옮긴다.
    constexpr luil::ui_element_kind kind_playback_toggle { luil::application_element_kind(30) };

    // --- 이 페이지의 메시지 ---
    // 텍스트 편집·조합 메시지(edit_intent·composition_intent)는 창 페이지와
    // target으로 나눠 가지므로 common.h에 있다.
    struct increment_intent
    {};

    struct dialog_intent
    {
        bool open { false };
    };

    struct counter_reset_intent
    {};

    // 캡션을 잡고 끈 만큼 dialog를 옮긴다 (논리 픽셀).
    struct move_dialog_intent
    {
        float dx { 0.0f };
        float dy { 0.0f };
    };

    // OS에서 노트 칸에 끌어다 놓은 파일이다.
    // 붙여넣기의 CF_HDROP과 같은 정책 — 첫 경로 하나가 칸의 글이 된다.
    struct note_file_intent
    {
        std::u8string path {};
    };

    // 움직이는 그림을 멈추거나, 멈춘 자리에서 이어 돌린다.
    // 메시지가 하나인 것은 단추가 하나여서다 — 어느 쪽인지는 재생 시계가 이미
    // 알고 있고(`image_playback::paused_at`), 단추의 글이 그것을 그대로 말한다.
    struct playback_toggle_intent
    {};

    class basics_page
    {
    public:
        // 이 페이지의 메시지를 처리했으면 참이다.
        bool handle(const luil::app_message& message);

        // 페이지 내용이다. width·height는 내용 영역의 논리 픽셀 크기다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // dialog가 열려 있으면 창 전체를 덮는 modal host를 root에 얹는다.
        // 어느 페이지가 보이든 dialog는 창 전체를 덮는다.
        void add_overlay(luil::root_element& root, float width, float height, float scale);

        [[nodiscard]] bool dialog_open() const noexcept
        {
            return dialog_open_;
        }

    private:
        void apply_edit(const luil::text_edit_request& request);
        void apply_composition(const luil::text_composition_event& event);
        void open_dialog(bool open);
        // 움직이는 견본 둘을 읽고, 읽혔으면 그 자리에서 재생을 시작한다.
        void read_films();
        // 멈춰 있으면 잇고, 돌고 있으면 멈춘다.
        void toggle_playback();
        [[nodiscard]] luil::text_input_view make_input_view(const luil::text::text_edit_state& state, luil::text_input_target target) const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_counter_row() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_input_row() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_image_row() const;
        [[nodiscard]] std::unique_ptr<luil::label_element> make_picture_status() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_picture(std::u8string owner, std::u8string label, luil::image_fit fit, std::u8string description) const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_film_row() const;
        [[nodiscard]] std::unique_ptr<luil::label_element> make_film_status() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_film(std::u8string owner, std::u8string label, const luil::ui_animated_image& film, std::u8string description) const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_playback_button() const;
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_labeled_input(
            std::u8string owner, std::u8string label, luil::ui_element_kind input_kind, luil::text_input_view view, std::u8string placeholder, luil::drop_target drop = {}) const;
        [[nodiscard]] static std::unique_ptr<luil::stack_element> make_dialog_body();
        [[nodiscard]] static std::unique_ptr<luil::stack_element> make_dialog_buttons();

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
        std::optional<luil::text_composition_event> composition_ {};
        int clicks_ { 0 };
        bool dialog_open_ { false };
        // 가운데에서 얼마나 밀어 둔 dialog인지다 (논리 픽셀).
        float dialog_x_ { 0.0f };
        float dialog_y_ { 0.0f };
    };
} // namespace demo

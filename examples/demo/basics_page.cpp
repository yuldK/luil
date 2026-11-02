#include "demo/basics_page.h"

#include "luil/app/platform_services.h"
#include "luil/app/secret_store.h"
#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/message_box.h"
#include "luil/ui/number_input.h"
#include "luil/ui/text_area_element.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        // 비밀 저장소를 한 바퀴 돌린다: 쓰기 → 읽기 → 덮어쓰기 → 읽기 → 지우기 → 없는지 읽기 (secret-store-design.md).
        // 사람이 읽을 결과 한 줄을 돌려준다. 단계가 어긋나면 그 자리와 OS의 이유를 적는다.
        [[nodiscard]] std::u8string check_secret_store()
        {
            constexpr std::u8string_view service { u8"luil-demo" };
            constexpr std::u8string_view name { u8"sample" };
            const auto bytes = [](const std::u8string_view text) { return std::vector<std::uint8_t> { text.begin(), text.end() }; };
            const auto failed = [](const std::u8string_view step, const luil::secret_store_result& result) {
                return std::u8string { step } + u8"에서 멈췄다 — " + (result.status == luil::secret_store_status::unsupported ? std::u8string { u8"이 플랫폼에는 저장소가 없다" } : result.error);
            };
            std::vector<std::uint8_t> read {};
            if (const auto result { luil::store_secret(service, name, bytes(u8"첫 값")) }; result.status != luil::secret_store_status::ok)
                return failed(u8"쓰기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::ok || read != bytes(u8"첫 값"))
                return failed(u8"읽기", result);
            if (const auto result { luil::store_secret(service, name, bytes(u8"둘째 값")) }; result.status != luil::secret_store_status::ok)
                return failed(u8"덮어쓰기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::ok || read != bytes(u8"둘째 값"))
                return failed(u8"다시 읽기", result);
            if (const auto result { luil::erase_secret(service, name) }; result.status != luil::secret_store_status::ok)
                return failed(u8"지우기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::not_found)
                return failed(u8"지운 뒤 읽기", result);
            return u8"쓰기·읽기·덮어쓰기·지우기를 모두 확인했다 (luil-demo/sample).";
        }

        // 숫자 칸의 범위다. 검사·거르기·증감이 같은 범위를 본다.
        [[nodiscard]] luil::number_input_range number_range()
        {
            return { .minimum = 0.0, .maximum = 3600.0, .step = 5.0 };
        }

        // 오류 글은 앱의 말이다. 빈 칸은 오류로 보지 않는다 — 아직 적지 않은 것이다.
        [[nodiscard]] std::u8string number_error(const luil::number_input_check& check)
        {
            switch (check.problem)
            {
            case luil::number_input_problem::none:
            case luil::number_input_problem::empty:
                return {};
            case luil::number_input_problem::not_a_number:
                return u8"숫자로 적어 주세요";
            case luil::number_input_problem::too_precise:
                return u8"소수 없이 적어 주세요";
            case luil::number_input_problem::below_minimum:
            case luil::number_input_problem::above_maximum:
                return u8"0에서 3600 사이로 적어 주세요";
            }
            return {};
        }

        // 실행 파일이 있는 디렉터리다. 자산은 그 옆에 선다.
        // 작업 디렉터리로 찾으면 바로 가기·디버거·탐색기 중 무엇으로 띄웠느냐에
        // 따라 자리가 달라진다.
        [[nodiscard]] std::filesystem::path module_directory()
        {
            std::array<wchar_t, MAX_PATH> buffer {};
            const DWORD length { GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())) };
            if (length == 0 || length >= buffer.size())
                return {};
            return std::filesystem::path { std::wstring { buffer.data(), length } }.parent_path();
        }

        // 가로는 색상, 세로는 밝기가 변하는 견본 이미지를 파일에서 읽는다.
        // fit 설정에 따른 여백과 잘림의 차이를 확인할 수 있다.
        [[nodiscard]] luil::ui_image load_demo_picture(luil::image_decode_error& error)
        {
            const std::filesystem::path directory { module_directory() };
            if (directory.empty())
            {
                // **앱의 실패도 라이브러리의 어휘로 적는다.** 화면과 로그가 두
                // 가지 실패 값을 갈라 들면 그 자리마다 갈래가 하나 늘어난다 —
                // 경로를 짓지 못한 것은 빈 경로와 같은 갈래다.
                error = { luil::image_decode_error_kind::path_empty, u8"Failed to locate the executable directory." };
                return {};
            }

            // 그림 칸이 220 논리 픽셀이라 배율 2까지 쳐도 440이면 넉넉하다 —
            // 480×240 파일이 440×220으로 줄어 들어온다.
            //  - 배율이 바뀌면 다시 읽는 것이 더 옳지만 그 판단은 앱의 것이고,
            //    이 데모는 한 번 읽는 쪽을 보인다.
            const std::filesystem::path path { directory / L"assets" / L"gradient.png" };
            return luil::load_image_file(path.u8string(), { .max_width = 440, .max_height = 440 }, error);
        }

        // 움직이는 견본 하나를 읽는다 (실행 파일 옆 `assets/`의 webp·gif).
        //  - **정지 그림과 같은 문을 쓰지 않는다.** 정지 쪽 문은 여러 장이 든
        //    파일에서도 첫 장만 내주므로 그림이 서 버린다 (image_decode.h).
        //  - 상한은 **들고 있을** 크기를 정한다. 칸이 96 논리 픽셀 높이라 배율
        //    2까지 쳐도 200이면 넉넉하고, 움직이는 그림에서는 그 값에 장 수가
        //    곱해진다 — 상한 하나가 여기서 열둘·열여섯 번 값을 낸다.
        [[nodiscard]] luil::ui_animated_image load_demo_film(const std::filesystem::path& directory, const std::wstring_view name, luil::image_decode_error& error)
        {
            if (directory.empty())
            {
                error = { luil::image_decode_error_kind::path_empty, u8"Failed to locate the executable directory." };
                return {};
            }
            const std::filesystem::path path { directory / L"assets" / name };
            return luil::load_animated_image_file(path.u8string(), { .max_height = 200 }, error);
        }
    }

    bool basics_page::handle(const luil::app_message& message)
    {
        if (const auto* const scroll { message.get<basics_scroll_intent>() }; scroll != nullptr)
        {
            page_scroll_ = std::max(0.0f, page_scroll_ + scroll->delta);
            return true;
        }
        if (message.get<increment_intent>() != nullptr)
        {
            ++clicks_;
            return true;
        }
        if (const auto* const scroll { message.get<memo_scroll_intent>() }; scroll != nullptr)
        {
            // 범위 다듬기는 칸이 한다 — 다음 frame의 칸이 내용 높이로 다시 다듬는다. 아래로 넘친 값만 0으로 막는다.
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
        if (const auto* const policy { message.get<line_break_policy_intent>() }; policy != nullptr)
        {
            line_break_policy_ = policy->policy;
            return true;
        }
        if (const auto* const link { message.get<open_link_intent>() }; link != nullptr)
        {
            // 라이브러리는 링크를 열지 않는다 — 여는 것은 앱이고, `open_url`은 허용한 scheme(http·https·mailto)만 연다.
            const luil::platform_result result { luil::open_url(link->destination) };
            link_report_ = result.ok() ? link->destination + u8" 를 열었다." : link->destination + u8" 를 열지 못했다 — " + result.error;
            return true;
        }
        if (undo_keys_.handle(message))
            return true;
        if (message.get<secret_check_intent>() != nullptr)
        {
            secret_report_ = check_secret_store();
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
            if (edit->request.target != target_number && edit->request.target != target_note && edit->request.target != target_memo && edit->request.target != target_link_view
                && edit->request.target != target_code && edit->request.target != target_line_break)
                return false;
            apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_number && composition->event.target != target_note && composition->event.target != target_memo
                && composition->event.target != target_code && composition->event.target != target_line_break)
                return false;
            apply_composition(composition->event);
            return true;
        }
        if (const auto* const dialog { message.get<dialog_intent>() }; dialog != nullptr)
        {
            open_dialog(dialog->open);
            return true;
        }
        if (const auto* const result { message.get<dialog_result_intent>() }; result != nullptr)
        {
            // 단추, Esc, 바깥 누름이 모두 이 메시지 하나로 온다. 받아서 닫고 key로 가른다.
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
        if (const auto* const stepped { message.get<number_text_intent>() }; stepped != nullptr)
        {
            // 칸이 지은 글로 통째로 바꾼다. 실행 취소 한 덩어리이고 caret은 끝에 선다.
            luil::text_edit_request request {};
            request.target = target_number;
            request.command = luil::text::text_edit_command::replace_all;
            request.text = stepped->text;
            request.offset = stepped->text.size();
            luil::apply_text_edit(number_, request);
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
            luil::apply_text_edit(number_, request, luil::number_input_filter(number_range()));
        else if (request.target == target_note)
            luil::apply_text_edit(note_, request);
        else if (request.target == target_memo)
            luil::apply_text_edit(memo_, request);
        else if (request.target == target_link_view)
            luil::apply_text_edit(link_view_, request);
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
        const std::filesystem::path directory { module_directory() };
        sweep_ = load_demo_film(directory, L"sweep.webp", film_error_);
        spinner_ = load_demo_film(directory, L"spinner.gif", film_error_);
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

    std::unique_ptr<luil::ui_element> basics_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        // 그림은 한 번만 읽는다.
        // frame마다 읽으면 디스크와 디코딩이 매 frame 선다 (image-decode-design.md).
        //  - `valid()`로 물으면 **읽지 못한** 파일을 frame마다 다시 열어 본다.
        //    실패도 한 번이어야 하므로 읽었다는 사실을 따로 기억한다.
        if (picture_read_ == false)
        {
            picture_read_ = true;
            picture_ = load_demo_picture(picture_error_);
        }
        // 움직이는 그림도 같은 규칙으로 한 번만 읽는다. 한 편이 장 수만큼의
        // 디코딩이라, frame마다 읽으면 그 값이 장 수만큼 곱절로 든다.
        if (films_read_ == false)
            read_films();
        // 코드 칸의 구간이다. 처음 한 번 칠하고, 그 뒤는 편집이 닿을 때마다 `apply_edit`가 다시 칠한다.
        if (code_painted_ == false)
        {
            code_painted_ = true;
            highlighter_.update(code_.text);
        }
        code_spans_ = highlighter_.spans();

        page_layout page { u8"basics" };
        page.heading(u8"입력과 다이얼로그", u8"IME 조합 · 여러 줄 편집 · 기본 버튼과 확인 흐름");
        page.add(make_counter_row(), 32.0f);
        page.gap(16.0f);
        // 숫자 칸의 오류 줄 자리를 늘 잡아 둔다 — 오류가 생겨도 아래 줄이 밀리지 않는다.
        page.add(make_input_row(), 50.0f + luil::text_input_error_height);
        page.gap(16.0f);
        page.add(make_memo(), 118.0f);
        page.gap(16.0f);
        page.add(make_link_view(), 106.0f);
        page.gap(16.0f);
        page.add(make_code(), 202.0f);
        page.gap(22.0f);
        {
            float line_break_height { 0.0f };
            auto line_break { make_line_break(scale, line_break_height) };
            page.add(std::move(line_break), line_break_height);
        }
        page.gap(22.0f);
        page.add(make_image_row(), 78.0f);
        page.gap(4.0f);
        page.add(make_picture_status(), 16.0f);
        page.gap(16.0f);
        page.add(make_film_row(), 118.0f);
        page.gap(4.0f);
        page.add(make_film_status(), 16.0f);
        // 멈출 것이 없으면 단추도 두지 않는다 — 라이브러리가 없는 것을 두지 않는
        // 것과 같은 규칙이고, 눌러도 아무 일도 없는 단추가 화면에서 고장으로
        // 보이는 자리를 여기서 자른다.
        if (sweep_.valid() || spinner_.valid())
        {
            page.gap(8.0f);
            page.add(make_playback_button(), { .length = 26.0f, .cross_length = 132.0f });
        }
        // 비밀 저장소다. 눌러 보면 자격 증명 관리자에 luil:luil-demo/sample을 쓰고 읽고 지운다.
        page.gap(12.0f);
        {
            luil::stack_config secret_config {};
            secret_config.direction = luil::stack_direction::row;
            secret_config.spacing = 12.0f;
            auto secret { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"basics-secret" }, secret_config) };
            auto check { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"secret-check" }, luil::text_button_config { .text = u8"비밀 저장소 확인" }) };
            check->set_action(luil::ui_trigger::left_click, luil::make_message_action(secret_check_intent {}));
            secret->add(std::move(check), 132.0f);
            const std::u8string report { secret_report_.empty() ? u8"자격 증명 관리자에 쓰고·읽고·지워 본다." : secret_report_ };
            secret->add_flexible(make_label(luil::ui_element_id { kind_text, u8"secret-report" }, report, 11.0f, luil::label_color_role::dim));
            page.add(std::move(secret), 26.0f);
        }
        page.gap(12.0f);
        page.add(make_label(luil::ui_element_id { kind_text, u8"basics-hint" }, u8"확인 대화 상자는 Esc나 바깥을 눌러 닫는다. 초기화 대화는 취소에 초점이 서서 Enter도 취소다.", 11.0f, luil::label_color_role::dim),
            18.0f);
        page.gap(10.0f);
        page.add(make_dialog_row(), 32.0f);
        // 대화 상태를 글로도 보인다. 화면과 보조 기술이 같은 말을 읽는다.
        page.gap(8.0f);
        page.add(make_label(luil::ui_element_id { kind_text, u8"dialog-state" }, dialog_open() ? u8"대화: 열림" : u8"대화: 닫힘", 11.0f, luil::label_color_role::primary), 18.0f);
        page.gap(4.0f);
        page.add(make_label(luil::ui_element_id { kind_text, u8"dialog-result" }, u8"마지막 결과: " + (dialog_result_.empty() ? std::u8string { u8"없음" } : dialog_result_), 11.0f, luil::label_color_role::dim), 18.0f);
        return page.finish(page_scroll_, [](const float delta) { return luil::make_app_action(basics_scroll_intent { delta }); });
    }

    std::unique_ptr<luil::stack_element> basics_page::make_dialog_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 12.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"dialog-open-row" }, config) };

        auto reset { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_open }, luil::text_button_config { .text = u8"카운터 초기화" }) };
        reset->set_tooltip(u8"확인 대화 상자를 연다");
        reset->set_cursor(luil::ui_cursor::hand);
        reset->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { dialog_kind::reset }));
        row->add(std::move(reset), 140.0f);

        auto details { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_details_open }, luil::text_button_config { .text = u8"긴 확인 대화" }) };
        details->set_tooltip(u8"긴 본문과 단추 셋과 확인 칸이 있는 대화를 연다");
        details->set_cursor(luil::ui_cursor::hand);
        details->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { dialog_kind::details }));
        row->add(std::move(details), 140.0f);
        return row;
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

    std::unique_ptr<luil::stack_element> basics_page::make_image_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 24.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"pictures" }, config) };
        row->add(make_labeled_picture(u8"contain", u8"이미지 (contain — 남는 자리는 배경)", luil::image_fit::contain, u8"그러데이션 견본, 전부 보이게"), 220.0f);
        row->add(make_labeled_picture(u8"cover", u8"이미지 (cover — 넘친 만큼 잘림)", luil::image_fit::cover, u8"그러데이션 견본, 칸을 덮게"), 220.0f);
        return row;
    }

    std::unique_ptr<luil::label_element> basics_page::make_picture_status() const
    {
        // 읽지 못한 파일은 조용히 비지 않는다 — 이유가 화면에 남는다.
        // 라이브러리가 내는 글은 진단용 영문이고, 사람에게 보일 문장은 앱이 짓는다.
        if (picture_.valid() == false)
            return make_label(luil::ui_element_id { kind_text, u8"picture-status" }, u8"그림을 읽지 못했다 — " + picture_error_.message, 11.0f, luil::label_color_role::primary);
        return make_label(luil::ui_element_id { kind_text, u8"picture-status" },
            u8"assets/gradient.png을 " + to_u8(picture_.width()) + u8"×" + to_u8(picture_.height()) + u8" 픽셀로 읽었다 (원본 480×240).", 11.0f, luil::label_color_role::dim);
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_picture(std::u8string owner, std::u8string label, const luil::image_fit fit, std::u8string description) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"picture-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"picture-" + owner }, std::move(label), 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        column->add(
            std::make_unique<luil::image_element>(luil::ui_element_id { kind_picture, std::move(owner) }, luil::image_config { .image = picture_, .fit = fit, .description = std::move(description) }),
            56.0f);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_film_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 24.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"films" }, config) };
        // 형식을 둘 다 세운다 — 코덱이 우리 것이 된 자리가 한 화면에 함께 선다.
        // gif 쪽은 배경이 비쳐, 테마를 바꾸면 같은 파일이 다른 바탕 위에서 돈다.
        row->add(make_labeled_film(u8"sweep", u8"움직이는 webp (16장 · 80 ms)", sweep_, u8"띠 하나가 왼쪽에서 오른쪽으로 훑고 지나가는 그러데이션"), 220.0f);
        row->add(make_labeled_film(u8"spinner", u8"움직이는 gif (12장 · 90 ms)", spinner_, u8"점 열둘이 둥글게 놓이고 밝은 점 하나가 시계 방향으로 도는 표시 — 배경이 비친다"), 160.0f);
        row->add_flexible_gap();
        return row;
    }

    std::unique_ptr<luil::label_element> basics_page::make_film_status() const
    {
        // 정지 그림과 같은 규칙이다 — 읽지 못한 파일은 조용히 비지 않는다.
        if (sweep_.valid() == false || spinner_.valid() == false)
            return make_label(luil::ui_element_id { kind_text, u8"film-status" }, u8"움직이는 그림을 읽지 못했다 — " + film_error_.message, 11.0f, luil::label_color_role::primary);
        return make_label(luil::ui_element_id { kind_text, u8"film-status" },
            u8"assets/sweep.webp를 " + to_u8(sweep_.width()) + u8"×" + to_u8(sweep_.height()) + u8" " + to_u8(static_cast<int>(sweep_.frame_count())) + u8"장으로, assets/spinner.gif를 "
                + to_u8(spinner_.width()) + u8"×" + to_u8(spinner_.height()) + u8" " + to_u8(static_cast<int>(spinner_.frame_count()))
                + u8"장으로 읽었다. 둘이 한 재생 시계를 타고, 창은 장이 바뀌는 경계에만 깨어난다.",
            11.0f, luil::label_color_role::dim);
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_film(std::u8string owner, std::u8string label, const luil::ui_animated_image& film, std::u8string description) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"film-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"film-" + owner }, std::move(label), 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        // element는 재생 시계를 들지 않는다. frame마다 이 config에 실리는 것이
        // 시계이고, 어느 장인지는 그리는 시각에서 매번 새로 나온다
        // (image_element.h). 그래서 필름 둘에 같은 시계를 실으면 둘이 함께 돌고
        // 함께 선다 — 멈춤 단추가 하나로 족한 이유다.
        //  - 설명은 보조 기술이 읽는다. 움직인다는 것은 이름이 아니라 그림의
        //    성질이라 그 글에 담는다 (image-design.md).
        column->add(std::make_unique<luil::image_element>(
                        luil::ui_element_id { kind_picture, std::move(owner) }, luil::image_config { .description = std::move(description), .animation = film, .playback = playback_ }),
            96.0f);
        return column;
    }

    std::unique_ptr<luil::ui_element> basics_page::make_playback_button() const
    {
        // 단추의 글은 **누르면 무엇이 되는지**를 말한다. 지금 상태("멈춤")를
        // 적으면 사람이 그것을 뒤집어 읽어야 한다.
        const bool paused { playback_.paused_at.has_value() };
        luil::text_button_config button_config {};
        button_config.text = paused ? u8"이어서 돌린다" : u8"잠시 멈춘다";
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_playback_toggle }, std::move(button_config)) };
        button->set_tooltip(paused ? u8"멈춘 자리에서 이어 간다" : u8"보이는 장에 멈춘다");
        button->set_cursor(luil::ui_cursor::hand);
        button->set_action(luil::ui_trigger::left_click, luil::make_message_action(playback_toggle_intent {}));
        return button;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_input_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 24.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inputs" }, config) };
        // 숫자 칸이다 (number-input-design.md). 0–3600을 5씩, ↑↓·PageUp/PageDown·스핀 단추로 늘리고 줄인다.
        //  - 붙여넣기·IME 확정은 `number_input_filter`가 거르고(전각 숫자는 ASCII로), 범위 밖은 칸 아래 오류 줄로 보인다.
        luil::text_input_config number {};
        number.placeholder = u8"0–3600";
        number.error = number_error(luil::check_number_input(number_.text, number_range()));
        number.number = luil::text_input_number {
            .range = number_range(),
            .change_to = [](std::u8string text) { return luil::make_app_action(number_text_intent { std::move(text) }); },
            .spin_buttons = true,
            .increase_tooltip = u8"5 늘리기",
            .decrease_tooltip = u8"5 줄이기",
        };
        row->add(make_labeled_input(u8"number", u8"숫자 칸 (0–3600, ↑↓ 5씩)", kind_number_input, make_input_view(number_, target_number), std::move(number)), 160.0f);

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
        // 친 글의 주소는 링크로 보이고 Ctrl+클릭으로 연다 (한 줄 칸의 링크).
        luil::text_input_config note { .placeholder = u8"메모" };
        note.links = std::make_shared<const std::vector<luil::text_link>>(luil::detect_text_links(note_.text));
        note.activate_link = [](const std::u8string_view destination) { return luil::make_app_action(open_link_intent { std::u8string { destination } }); };
        note.link_hint = u8"Ctrl+클릭하여 열기";
        row->add(
            make_labeled_input(u8"note", u8"아무 글이나 받는 칸 (IME 조합·붙여넣기·파일 드롭·주소 링크)", kind_note_input, make_input_view(note_, target_note), std::move(note), std::move(note_drop)),
            { .weight = 1.0f, .minimum = 160.0f });
        return row;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_memo() const
    {
        // 줄 바꿈·세로 스크롤·여러 줄 붙여넣기를 받는 칸이다. 글과 스크롤은 앱 상태이고 편집은 한 줄 칸과 같은 길이다.
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-memo" }, config) };
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"memo" }, u8"여러 줄 메모 (Enter 줄 바꿈 · ↑↓ 줄 이동 · 휠 스크롤 · 주소는 Ctrl+클릭으로 연다)", 11.0f, luil::label_color_role::primary),
            18.0f);
        column->add_gap(4.0f);
        luil::text_area_config memo {};
        memo.placeholder = u8"여러 줄 메모";
        memo.scroll_offset = memo_scroll_;
        memo.scroll = [](const float delta) { return luil::make_app_action(memo_scroll_intent { delta }); };
        memo.scroll_to = [](const float offset) { return luil::make_app_action(memo_scroll_to_intent { offset }); };
        memo.wrap_cache = memo_wrap_;
        // 친 글에서 주소를 찾아 링크로 보인다. 메모 정도의 글이라 frame마다 찾아도 된다.
        memo.links = std::make_shared<const std::vector<luil::text_link>>(luil::detect_text_links(memo_.text));
        memo.activate_link = [](const std::u8string_view destination) { return luil::make_app_action(open_link_intent { std::u8string { destination } }); };
        memo.link_hint = u8"Ctrl+클릭하여 열기";
        column->add(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo_input }, make_input_view(memo_, target_memo), std::move(memo)), 96.0f);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_code() const
    {
        // 편집하는 코드 글이다. 줄을 칠하는 렉서는 앱의 것(code_lexer.h)이고, 도우미가 편집이 닿은 줄부터 다시 칠한 구간을 칸에 건넨다.
        // 상태 줄은 마지막 편집이 렉서에 보낸 줄 수까지 그려, 증분이 한 줄에서 끝나는지 화면으로 되읽는다.
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-code" }, luil::stack_config {}) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"code" }, u8"코드 칸 (구문 강조 · 키워드는 굵게 · 주석은 기울임 · 편집한 줄부터 다시 칠한다)", 11.0f, luil::label_color_role::primary), 18.0f);
        column->add_gap(4.0f);
        luil::text_area_config code {};
        code.placeholder = u8"코드 칸";
        code.face = luil::text_face::code;
        code.scroll_offset = code_scroll_;
        code.scroll = [](const float delta) { return luil::make_app_action(code_scroll_intent { delta }); };
        code.scroll_to = [](const float offset) { return luil::make_app_action(code_scroll_to_intent { offset }); };
        code.wrap_cache = code_wrap_;
        code.spans = code_spans_;
        column->add(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_code_input }, make_input_view(code_, target_code), std::move(code)), 160.0f);
        column->add_gap(4.0f);
        const code_span_counts counts { code_spans_ != nullptr ? count_code_spans(*code_spans_) : code_span_counts {} };
        column->add(make_label(luil::ui_element_id { kind_text, u8"code-status" }, code_status_text(counts, highlighter_.stats().relexed), 11.0f, luil::label_color_role::dim), 16.0f);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_line_break(const float scale, float& height) const
    {
        // 같은 글을 같은 폭의 여러 줄 label과 여러 줄 칸에 놓는다. 둘이 같은 줄 나눔 규칙으로 접는지, 정책이 접는 자리를
        // 바꾸는지를 상태 줄로 되읽는다 (line-break-design.md).
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-line-break" }, luil::stack_config {}) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"line-break" }, u8"줄 나눔 (한국어는 어절 · 하이픈·슬래시 뒤 · 닫는 부호는 줄 머리에 두지 않는다)", 11.0f, luil::label_color_role::primary),
            18.0f);
        column->add_gap(4.0f);
        // 스위치의 글은 켜면 무엇이 되는지를 말한다.
        const bool syllables { line_break_policy_ == luil::text::line_break_policy::unicode };
        luil::check_config policy {};
        policy.owner = u8"line-break-policy";
        policy.style = luil::check_style::toggle_switch;
        policy.label = u8"한글 음절 사이에서도 바꾸기 (unicode)";
        policy.checked = syllables;
        policy.toggle = luil::make_message_action(line_break_policy_intent { syllables ? luil::text::line_break_policy::hangul_words : luil::text::line_break_policy::unicode });
        const float policy_height { luil::check_element::height_for(policy) };
        column->add(std::make_unique<luil::check_element>(std::move(policy)), { .length = policy_height, .cross_length = 2.0f * line_break_cell_width + 16.0f });
        column->add_gap(4.0f);

        // 칸을 그 폭으로 배치하고 label을 같은 폭으로 재어 상태 줄을 짓는다. 칸은 배치한 채로 tree에 넣는다.
        luil::label_config label { line_break_label_config(line_break_.text, line_break_policy_) };
        auto area { std::make_unique<luil::text_area_element>(
            luil::ui_element_id { kind_line_break_input }, make_input_view(line_break_, target_line_break), line_break_area_config(line_break_policy_, line_break_wrap_))
        };
        area->set_access_name(u8"줄 나눔 칸");
        const std::u8string status { measure_line_break_status(line_break_policy_, label, *area, scale) };
        const float label_height { luil::label_element::measure_wrap(label, luil::measure_context { .scale = scale }, line_break_cell_width).height };
        const float row_height { std::max(label_height, line_break_area_height) };

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 16.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"line-break-row" }, row_config) };
        row->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"line-break-label" }, std::move(label)), line_break_cell_width);
        row->add(std::move(area), { .length = line_break_cell_width, .cross_length = line_break_area_height });
        row->add_flexible_gap();
        column->add(std::move(row), row_height);
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"line-break-status" }, status, 11.0f, luil::label_color_role::dim), 16.0f);
        height = 18.0f + 4.0f + policy_height + 4.0f + row_height + 4.0f + 16.0f;
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_link_view() const
    {
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-links" }, luil::stack_config {}) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"links" }, u8"읽기 전용 안내 (링크는 그냥 클릭으로 연다 · 머무르면 주소)", 11.0f, luil::label_color_role::primary), 18.0f);
        column->add_gap(4.0f);
        luil::text_area_config view {};
        view.read_only = true;
        view.links = std::make_shared<const std::vector<luil::text_link>>(luil::detect_text_links(link_view_.text));
        view.activate_link = [](const std::u8string_view destination) { return luil::make_app_action(open_link_intent { std::u8string { destination } }); };
        auto area { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_link_view_input }, make_input_view(link_view_, target_link_view), std::move(view)) };
        area->set_access_name(u8"읽기 전용 안내 칸");
        column->add(std::move(area), 44.0f);
        column->add_gap(4.0f);
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"link-report" }, link_report_.empty() ? std::u8string { u8"아직 연 링크가 없다." } : link_report_, 11.0f, luil::label_color_role::dim),
            16.0f);
        // 칸을 눌러 초점을 둔 채 Ctrl+Z나 Ctrl+Y를 누르면 센다. 칸은 되돌릴 글이 없어 키를 삼키지 않고 앱으로 넘긴다.
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"readonly-undo-keys" }, undo_keys_.text(), 11.0f, luil::label_color_role::dim), 16.0f);
        return column;
    }

    std::unique_ptr<luil::stack_element> basics_page::make_labeled_input(
        std::u8string owner, std::u8string label, const luil::ui_element_kind input_kind, luil::text_input_view view, luil::text_input_config input_config, luil::drop_target drop) const
    {
        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"input-" + owner }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, label, 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        // 오류 줄이 있으면 칸이 그만큼 키를 더 받는다 (측정 단계가 없어 담는 쪽이 더한다).
        const float height { 28.0f + luil::text_input_element::error_height_for(input_config) };
        auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { input_kind }, std::move(view), std::move(input_config)) };
        // 안내 글은 적으면 사라지므로 접근 이름은 위 라벨이다.
        input->set_access_name(std::move(label));
        // 드롭 대상은 온 칸에만 단다 — 없는 것은 두지 않는다.
        if (drop.accepts && drop.on_drop)
            input->set_drop_target(std::move(drop));
        column->add(std::move(input), height);
        return column;
    }

    luil::message_box_config basics_page::make_dialog_config() const
    {
        luil::message_box_config box {};
        // 닫는 모든 계기(단추, Esc, 바깥 누름)가 이 함수 하나를 부른다.
        box.result = [](const std::u8string_view key) { return luil::make_app_action(dialog_result_intent { std::u8string { key } }); };
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
            // 단추 셋이 한 행에 서는 폭이다. 창이 이보다 좁으면 host가 줄이고 단추가 쌓인다.
            box.width = 480.0f;
            return box;
        }
        if (dialog_ == dialog_kind::reset)
        {
            box.owner = u8"reset";
            box.severity = luil::ui_severity::warning;
            box.title = u8"카운터를 0으로 되돌릴까요?";
            box.body = u8"누른 횟수가 사라진다.";
            // 위험한 단추는 Enter의 대상도 첫 초점도 되지 않는다. 뜨면 취소에 서고 Enter도 취소다.
            box.buttons = { luil::message_box_cancel(), luil::message_box_button { .key = u8"reset", .text = u8"초기화", .role = luil::message_box_role::destructive } };
            box.focus_return = luil::ui_element_id { kind_dialog_open };
        }
        return box;
    }

    void basics_page::add_overlay(luil::root_element& root, const float width, const float height, const float scale)
    {
        if (dialog_open() == false)
            return;

        // 크기는 내용에서 나온다. 앱은 높이도 창 크기도 넘기지 않고 배치만 한다.
        // scrim·초점 가둠·Esc·바깥 누름은 host의 몫이고 앱이 정하는 것은 "무엇을 닫는가"를 받는 결과 메시지 하나뿐이다.
        auto host { luil::make_message_box(make_dialog_config()) };
        host->arrange({ { 0.0f, 0.0f, width, height }, scale });
        root.add(std::move(host));
    }
}

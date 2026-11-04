#pragma once

#include "luil/ui/calendar_date.h"
#include "luil/ui/ui_element.h"

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 달력의 치수다 (논리 픽셀). 그릴 때 배율을 곱한다.
    // 날 칸과 머리 줄 단추의 한 변이다. 터치 앱은 `calendar_config::cell_size`로 올린다.
    inline constexpr float calendar_cell_size { 32.0f };
    // 요일 줄의 높이다.
    inline constexpr float calendar_weekday_height { 24.0f };

    // 달력이 스스로 놓는 글이다. 칸마다 비면 기본 문구다 (docs/calendar-date-design.md).
    struct calendar_text
    {
        // root의 접근 이름이다 (달력).
        std::u8string name {};
        // 이전 달·다음 달 단추의 접근 이름이자 툴팁이다.
        std::u8string previous_month {};
        std::u8string next_month {};
        // 날 이름 뒤 꼬리의 낱말이다 (", 오늘"·", 고를 수 없음"). 잇는 차례와 쉼표는 요소가 정한다.
        std::u8string today {};
        std::u8string unavailable {};
        // 요일 줄의 이름이다. 일요일부터다 (`weekday::c_encoding` 순). 칸마다 비면 기본 문구(일·월·…·토)다.
        std::array<std::u8string, 7> weekdays {};
        // 머리 줄 제목이자 격자의 접근 이름이다 ("2026년 11월").
        std::function<std::u8string(std::chrono::year_month)> month_title {};
        // 날 칸의 접근 이름이다 ("2026년 11월 3일 화요일").
        std::function<std::u8string(std::chrono::year_month_day)> day_name {};
    };

    struct calendar_config
    {
        // root와 격자의 owner다. 단추의 owner는 이 값, 단위 구분자(U+001F), `prev`·`next`이고 날 칸은 이 값, 단위 구분자,
        // ISO 날짜(`2026-11-03`)다.
        std::u8string owner {};
        // 고른 날이다. 앱 상태다. 범위 밖이어도 그대로 두고 그 칸을 고른 날로 그린다.
        std::optional<std::chrono::year_month_day> value {};
        // 키보드 커서이자 보이는 달이다. 앱 상태다 (docs/date-picker-design.md 결정 2).
        //  - 열 때 앱은 `calendar_start_cursor(value, today, bounds)`로 둔다. `ok()`가 아니면 요소가 그 함수로 바꿔 그리고,
        //    범위 밖이면 범위로 접어 그린다. 키·단추가 내는 목표는 이 접은 커서에서 센다.
        std::chrono::year_month_day cursor {};
        // 오늘이다. 라이브러리는 시계를 읽지 않는다. 비면 오늘 표시가 없다. 자정이 지나면 앱이 다음 frame에 바꾼다.
        std::optional<std::chrono::year_month_day> today {};
        calendar_bounds bounds {};
        // 고를 수 있는 날인가다. 비면 범위 안 모두다. 거짓인 날에도 커서는 서지만 고르지 않는다 (결정 6).
        std::function<bool(std::chrono::year_month_day)> available {};
        // 주의 첫 요일이다. 기본은 일요일이다. OS를 읽지 않는다. 앱은 `user_first_weekday`의 값을 넣는다.
        std::chrono::weekday first_day { std::chrono::Sunday };
        // 이 날을 고르라는 메시지다. 날 칸의 클릭·탭, 초점 날의 Enter·Space, 보조 기술의 Select가 낸다. 없으면 고를 수 없다.
        std::function<input_action(std::chrono::year_month_day)> select {};
        // 커서를 이 날로 하라는 절대 메시지다. 키와 커서가 아닌 날의 클릭이 낸다 (클릭은 이것 다음에 `select`).
        //  - 앱은 커서를 옮기고 달력이 선 표면의 `day_id(owner, 새 커서)`로 초점 요청을 싣는다 (결정 2).
        //  - 목표가 build 때의 커서와 같으면(범위 끝) 빈 목록으로 삼킨다. 한 frame 안에 키 둘이 오면 둘째가 같은 목표를 낸다.
        //  - `move_to`와 `turn_month`가 모두 없으면 키 처리기와 이전·다음 단추가 없다.
        std::function<input_action(std::chrono::year_month_day)> move_to {};
        // 날 칸과 머리 줄 단추의 한 변이다 (논리 픽셀). 0이면 `calendar_cell_size`다. 터치 앱은 44 안팎으로 올린다.
        float cell_size { 0.0f };
        calendar_text text {};
        // 이전·다음 단추가 내는 커서 메시지다 (결정 8). 비면 `move_to`다.
        //  - 앱은 커서를 옮기되 초점 요청을 싣지 않는다. 그래야 누른 단추에 초점이 남아 같은 단추를 거듭 누를 수 있다.
        std::function<input_action(std::chrono::year_month_day)> turn_month {};
    };

    // 한 달을 6주 x 7일로 보이고 날 하나를 고르는 달력이다 (`ui_element_kind::calendar`, docs/date-picker-design.md).
    // 인라인으로 화면에 놓거나 `make_date_picker_popup`으로 날짜 칸 아래 popup에 싣는다.
    //
    // root는 머리 줄의 달 제목과 요일 줄을 그리고 자식을 그린다. 접근 역할은 group이다.
    //  - 이전·다음 단추(`calendar_nav`)는 button이다. 그 달 전체가 범위 밖이면 비활성이다. 누른 뒤 초점은 단추에 남는다.
    //  - 격자(`calendar_grid`)는 list이고 이름이 달 제목이다. 키 처리기가 여기 있다. 초점 날에는 처리기가 없어 조상 전파의
    //    첫 자리가 격자다. 묶음(focus group)과 값 단계(key step)를 쓰지 않는다. 둘 다 처리기보다 먼저 키를 가져간다.
    //  - 날 칸(`calendar_day`) 42개는 list_item이다. 고른 날이 `selected`이고 이름에 ", 오늘"·", 고를 수 없음"을 잇는다.
    //  - Tab 자리는 이전 달, 다음 달, 커서 날 순이다. 커서 날만 자리다 (고를 수 없는 날이어도 자리다). 다른 날을 누르면
    //    포인터 초점은 커서 날에 서고, 클릭이 낸 `move_to`를 받은 앱의 초점 요청이 누른 날로 옮긴다.
    //  - 비활성은 앱이 root에 거는 `set_enabled(false)`이고 자식이 arrange에서 따라간다.
    //
    // root의 bounds는 본체가 차지한 크기다 (`size_for`에 배율을 곱한 것). 자리가 더 넓으면 왼쪽 위에 붙인다.
    class calendar_element final : public ui_element
    {
    public:
        explicit calendar_element(calendar_config config);

        // 본체의 논리 크기다. 폭은 7칸이고 높이는 머리 줄(한 칸), 요일 줄, 6주다.
        //  - 글 폭을 재지 않는 논리 상수의 합이라 배율과 글자 크기에 매이지 않는다. 칸보다 큰 글은 말줄임한다.
        [[nodiscard]] static size_f size_for(const calendar_config& config) noexcept;
        // 그 날 칸의 id다. 앱이 초점 요청의 대상으로 쓴다 (열 때와 `move_to`를 처리할 때).
        [[nodiscard]] static ui_element_id day_id(std::u8string_view owner, std::chrono::year_month_day day);

        // 그린 커서다. `config.cursor`를 `ok()`·범위로 다듬은 날이다. 키와 단추의 목표가 이것에서 센다.
        [[nodiscard]] std::chrono::year_month_day cursor() const noexcept
        {
            return cursor_;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 격자의 키다. 표에 없는 키와 Ctrl·Alt·Meta를 든 키는 `std::nullopt`다.
        [[nodiscard]] std::optional<std::vector<input_action>> handle_key(const key_pressed_event& event) const;
        // 커서 메시지다. 목표가 커서와 같으면 빈 목록이다.
        [[nodiscard]] std::vector<input_action> emit_move(const std::function<input_action(std::chrono::year_month_day)>& message, std::chrono::year_month_day target) const;
        [[nodiscard]] bool selectable(std::chrono::year_month_day day) const;
        [[nodiscard]] std::u8string month_title() const;
        [[nodiscard]] std::u8string day_name(std::chrono::year_month_day day) const;

        calendar_config config_ {};
        std::chrono::year_month_day cursor_ {};
        float scale_ { 1.0f };
        rect_f title_box_ {};
        rect_f weekday_box_ {};
        // 단추가 없으면 nullptr다.
        ui_element* previous_ { nullptr };
        ui_element* next_ { nullptr };
        ui_element* grid_ { nullptr };
        // 그 달 전체가 범위 밖이라 누를 수 없는 단추인가다. arrange가 root의 활성과 함께 건다.
        bool previous_blocked_ { false };
        bool next_blocked_ { false };
    };

    struct date_field_config
    {
        std::u8string owner {};
        // 고른 날이다. 앱 상태다. 비었거나 `ok()`가 아니면 placeholder를 흐리게 그린다.
        std::optional<std::chrono::year_month_day> value {};
        // popup이 떠 있는지는 앱 상태다 (popup이 frame에 실려 있는지와 같이 간다).
        bool open { false };
        // popup을 **이 상태로** 하라는 절대 메시지다. 누름은 build 때의 `open` 반대를 담고, 보조 기술의 펼치기·접기는 목표를
        // 담는다. 없으면 칸이 눌리지 않고 보조 기술이 열지 못한다.
        std::function<input_action(bool open)> set_open {};
        // 표시 글이자 접근 값이다. 비면 기본 문구("2026년 11월 3일")다.
        std::function<std::u8string(std::chrono::year_month_day)> format {};
        // 값이 없을 때의 글이다. 비면 기본 문구(날짜 선택)다.
        std::u8string placeholder {};
        // 접근 이름이다. 비면 기본 문구(날짜)다.
        std::u8string name {};
    };

    // 달력을 popup으로 여는 닫힌 날짜 칸이다 (`ui_element_kind::date_field`). 높이는 `dropdown_height`(28)다.
    // 표시 글과 달력 아이콘을 그린다. 닫힌 필드 popup 규약(field_popup.h)을 따르고 글을 받지 않는다.
    //  - 클릭은 `set_open(!open)`, 키는 Alt+↓·F4·↓가 연다. 열린 동안 키보드 초점이 칸을 같은 표면 안에서 떠나면 닫는다.
    //  - 열 때 앱은 커서를 시작 커서로 두고 popup 표면의 `calendar_element::day_id(owner, 커서)`로 초점 요청을 싣는다.
    //    Esc나 날 고르기로 닫으면 칸으로 돌아오는 요청을 한 번 더 싣는다 (docs/date-picker-design.md).
    class date_field_element final : public ui_element
    {
    public:
        explicit date_field_element(date_field_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 펼치기·접기는 절대 메시지(`set_open`)로 답한다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        // 값의 표시 글이다. 값이 없으면 빈 글이다.
        [[nodiscard]] std::u8string value_text() const;

        date_field_config config_ {};
    };
}

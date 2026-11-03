#include "ui/default_text.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {
    constexpr std::array<luil::ui_severity, 4> severities { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error };
    constexpr std::array<luil::theme_preference, 3> preferences { luil::theme_preference::light, luil::theme_preference::system, luil::theme_preference::dark };
}

TEST_CASE("Every default text is a non-empty Korean word, the same as the ones the elements wrote themselves", "[ui][default-text]")
{
    // 요소마다 박혀 있던 낱말을 옮긴 것이라 글자가 전과 같아야 한다. 열다섯 낱말에 확인 대화 상자의 표준 단추 넷과 별점의 이름,
    // 실행 취소 명령표의 두 이름, 잉크 캔버스의 이름, 색 고르기의 이름 다섯, 서식 명령표의 여섯 이름, 메뉴 막대의 이름을 더한 서른다섯이다.
    // 분할 버튼의 이름은 문자열을 새로 짓는다. view로 들기 전에 수명을 잡아 둔다.
    const std::u8string arrow_name { luil::default_text::split_button_arrow_name(u8"저장") };
    const std::array<std::pair<std::u8string_view, std::u8string_view>, 35> words {
        std::pair { luil::default_text::info_bar_close(), std::u8string_view { u8"닫기" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::info), std::u8string_view { u8"정보" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::success), std::u8string_view { u8"성공" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::warning), std::u8string_view { u8"경고" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::error), std::u8string_view { u8"오류" } },
        std::pair { std::u8string_view { arrow_name }, std::u8string_view { u8"저장 옵션" } },
        std::pair { luil::default_text::zoom_decrease(), std::u8string_view { u8"축소" } },
        std::pair { luil::default_text::zoom_increase(), std::u8string_view { u8"확대" } },
        std::pair { luil::default_text::zoom_fit(), std::u8string_view { u8"맞춤" } },
        std::pair { luil::default_text::theme_picker_name(), std::u8string_view { u8"테마" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::light), std::u8string_view { u8"밝게" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::system), std::u8string_view { u8"시스템" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::dark), std::u8string_view { u8"어둡게" } },
        std::pair { luil::default_text::accent_picker_name(), std::u8string_view { u8"강조색" } },
        std::pair { luil::default_text::combo_box_loading(), std::u8string_view { u8"추천 항목을 불러오는 중" } },
        std::pair { luil::default_text::message_box_ok_label(), std::u8string_view { u8"확인" } },
        std::pair { luil::default_text::message_box_cancel_label(), std::u8string_view { u8"취소" } },
        std::pair { luil::default_text::message_box_yes_label(), std::u8string_view { u8"예" } },
        std::pair { luil::default_text::message_box_no_label(), std::u8string_view { u8"아니요" } },
        std::pair { luil::default_text::rating_name(), std::u8string_view { u8"별점" } },
        std::pair { luil::default_text::undo_command_undo(), std::u8string_view { u8"실행 취소" } },
        std::pair { luil::default_text::undo_command_redo(), std::u8string_view { u8"다시 실행" } },
        std::pair { luil::default_text::ink_canvas_name(), std::u8string_view { u8"잉크 캔버스" } },
        std::pair { luil::default_text::color_picker_name(), std::u8string_view { u8"색" } },
        std::pair { luil::default_text::color_picker_plane_name(), std::u8string_view { u8"채도와 명도" } },
        std::pair { luil::default_text::color_picker_hue_name(), std::u8string_view { u8"색상" } },
        std::pair { luil::default_text::color_picker_alpha_name(), std::u8string_view { u8"불투명도" } },
        std::pair { luil::default_text::color_picker_hex_name(), std::u8string_view { u8"16진 색" } },
        std::pair { luil::default_text::rich_text_bold(), std::u8string_view { u8"굵게" } },
        std::pair { luil::default_text::rich_text_italic(), std::u8string_view { u8"기울임꼴" } },
        std::pair { luil::default_text::rich_text_underline(), std::u8string_view { u8"밑줄" } },
        std::pair { luil::default_text::rich_text_code(), std::u8string_view { u8"코드" } },
        std::pair { luil::default_text::rich_text_link(), std::u8string_view { u8"링크" } },
        std::pair { luil::default_text::rich_text_clear_format(), std::u8string_view { u8"서식 지우기" } },
        std::pair { luil::default_text::menu_bar_name(), std::u8string_view { u8"메뉴 막대" } },
    };
    for (const auto& [text, expected] : words)
    {
        REQUIRE(text.empty() == false);
        REQUIRE(text == expected);
    }
}

TEST_CASE("The four severities and the three theme preferences each have their own word", "[ui][default-text]")
{
    for (std::size_t first = 0; first < severities.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < severities.size(); ++second)
            REQUIRE(luil::default_text::severity_name(severities[first]) != luil::default_text::severity_name(severities[second]));
    }

    for (std::size_t first = 0; first < preferences.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < preferences.size(); ++second)
            REQUIRE(luil::default_text::theme_picker_label(preferences[first]) != luil::default_text::theme_picker_label(preferences[second]));
    }
}

TEST_CASE("An empty configured text falls back to the default and anything else is the app's own", "[ui][default-text]")
{
    const std::u8string_view fallback { luil::default_text::zoom_fit() };
    REQUIRE(luil::default_text::text_or_default({}, fallback) == fallback);
    REQUIRE(luil::default_text::text_or_default(u8"", fallback) == fallback);
    REQUIRE(luil::default_text::text_or_default(u8"Fit", fallback) == u8"Fit");

    // 공백만 있는 글은 앱이 일부러 준 글이다. 기본 문구로 바꾸지 않는다.
    REQUIRE(luil::default_text::text_or_default(u8" ", fallback) == u8" ");
    REQUIRE(luil::default_text::text_or_default(u8"\t\n", fallback) == u8"\t\n");

    // 앱 글이 기본 문구와 같아도 앱 글이다. 돌려준 view는 인자가 가리키던 글자다.
    const std::u8string configured { u8"Zoom out" };
    const std::u8string_view picked { luil::default_text::text_or_default(configured, fallback) };
    REQUIRE(picked.data() == configured.data());
}

TEST_CASE("The split button arrow name keeps the word order in one place", "[ui][default-text]")
{
    REQUIRE(luil::default_text::split_button_arrow_name(u8"저장") == u8"저장 옵션");
    REQUIRE(luil::default_text::split_button_arrow_name(u8"Save") == u8"Save 옵션");
    // 본체 이름이 비어도 앞 공백이 든 채 그대로다 (분할 버튼이 비어 있던 때의 동작).
    REQUIRE(luil::default_text::split_button_arrow_name({}) == u8" 옵션");
}

TEST_CASE("The color plane value names both percents as whole numbers", "[ui][default-text][color-picker]")
{
    REQUIRE(luil::default_text::color_picker_plane_value(0.4f, 0.8f) == u8"채도 40%, 명도 80%");
    REQUIRE(luil::default_text::color_picker_plane_value(0.0f, 1.0f) == u8"채도 0%, 명도 100%");
    // 백분율은 반올림한 정수다. 범위 밖과 NaN은 가장자리로 읽는다.
    REQUIRE(luil::default_text::color_picker_plane_value(0.406f, 0.994f) == u8"채도 41%, 명도 99%");
    REQUIRE(luil::default_text::color_picker_plane_value(-1.0f, 3.0f) == u8"채도 0%, 명도 100%");
    REQUIRE(luil::default_text::color_picker_plane_value(std::numeric_limits<float>::quiet_NaN(), 0.5f) == u8"채도 0%, 명도 50%");
}

TEST_CASE("The rating value name builds digits itself and keeps one decimal at most", "[ui][default-text]")
{
    REQUIRE(luil::default_text::rating_value_name(3.0f, 5) == u8"3 / 5");
    REQUIRE(luil::default_text::rating_value_name(0.0f, 5) == u8"0 / 5");
    REQUIRE(luil::default_text::rating_value_name(3.5f, 5) == u8"3.5 / 5");
    REQUIRE(luil::default_text::rating_value_name(4.3f, 5) == u8"4.3 / 5");
    // 소수 둘째 자리는 반올림한다. 소수 첫째 자리가 0이 되면 정수로 읽는다.
    REQUIRE(luil::default_text::rating_value_name(4.26f, 5) == u8"4.3 / 5");
    REQUIRE(luil::default_text::rating_value_name(4.96f, 5) == u8"5 / 5");
    REQUIRE(luil::default_text::rating_value_name(4.04f, 5) == u8"4 / 5");
    // 두 자리 수 별 수도 그대로 읽는다.
    REQUIRE(luil::default_text::rating_value_name(12.0f, 20) == u8"12 / 20");
    // 범위 밖과 NaN은 가장자리로 읽는다.
    REQUIRE(luil::default_text::rating_value_name(-1.0f, 5) == u8"0 / 5");
    REQUIRE(luil::default_text::rating_value_name(9.0f, 5) == u8"5 / 5");
    REQUIRE(luil::default_text::rating_value_name(std::numeric_limits<float>::quiet_NaN(), 5) == u8"0 / 5");
}

TEST_CASE("The ink canvas summary builds the stroke count digits itself", "[ui][default-text][ink]")
{
    REQUIRE(luil::default_text::ink_canvas_summary(0) == u8"획 0개");
    REQUIRE(luil::default_text::ink_canvas_summary(1) == u8"획 1개");
    REQUIRE(luil::default_text::ink_canvas_summary(3) == u8"획 3개");
    // 여러 자리 수도 그대로 읽는다.
    REQUIRE(luil::default_text::ink_canvas_summary(1205) == u8"획 1205개");
}

TEST_CASE("The calendar words are Korean and the dates build their digits without leading zeros", "[ui][default-text][calendar]")
{
    // 날짜 칸과 달력의 열한 함수다 (calendar-date-design.md). 낱말은 한국어이고 날짜는 숫자 글자를 직접 짓는다.
    const std::array<std::pair<std::u8string_view, std::u8string_view>, 7> words {
        std::pair { luil::default_text::date_field_name(), std::u8string_view { u8"날짜" } },
        std::pair { luil::default_text::date_field_placeholder(), std::u8string_view { u8"날짜 선택" } },
        std::pair { luil::default_text::calendar_name(), std::u8string_view { u8"달력" } },
        std::pair { luil::default_text::calendar_previous_month(), std::u8string_view { u8"이전 달" } },
        std::pair { luil::default_text::calendar_next_month(), std::u8string_view { u8"다음 달" } },
        std::pair { luil::default_text::calendar_today(), std::u8string_view { u8"오늘" } },
        std::pair { luil::default_text::calendar_unavailable(), std::u8string_view { u8"고를 수 없음" } },
    };
    for (const auto& [text, expected] : words)
        REQUIRE(text == expected);

    const std::chrono::year_month_day day { std::chrono::year { 2026 }, std::chrono::November, std::chrono::day { 3 } };
    REQUIRE(luil::default_text::date_field_text(day) == u8"2026년 11월 3일");
    REQUIRE(luil::default_text::calendar_day_name(day) == u8"2026년 11월 3일 화요일");
    REQUIRE(luil::default_text::calendar_month_title({ std::chrono::year { 2026 }, std::chrono::November }) == u8"2026년 11월");
    // 달·날 앞에 0이 없다. 네 자리가 안 되는 해도 그대로다.
    const std::chrono::year_month_day early { std::chrono::year { 5 }, std::chrono::January, std::chrono::day { 9 } };
    REQUIRE(luil::default_text::date_field_text(early) == u8"5년 1월 9일");
    REQUIRE(luil::default_text::calendar_month_title({ std::chrono::year { 2026 }, std::chrono::March }) == u8"2026년 3월");
    // 틀린 날에는 요일을 붙이지 않는다.
    REQUIRE(luil::default_text::calendar_day_name({ std::chrono::year { 2026 }, std::chrono::February, std::chrono::day { 30 } }) == u8"2026년 2월 30일");

    // 요일 일곱은 일요일부터다. 틀린 요일은 빈 글이다.
    const std::array<std::u8string_view, 7> weekdays { u8"일", u8"월", u8"화", u8"수", u8"목", u8"금", u8"토" };
    for (unsigned index { 0 }; index < 7u; ++index)
        REQUIRE(luil::default_text::calendar_weekday_short(std::chrono::weekday { index }) == weekdays[index]);
    REQUIRE(luil::default_text::calendar_weekday_short(std::chrono::weekday { 9 }).empty());
}

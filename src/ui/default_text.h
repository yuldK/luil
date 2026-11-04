#pragma once

#include "luil/theme/appearance.h"
#include "luil/ui/severity.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace luil::default_text {
    // 라이브러리 요소가 스스로 화면이나 접근 tree에 놓는 글의 기본값이다 (docs/default-text-design.md).
    //
    // 기본은 한국어이고 앱이 `set_text_languages`로 언어를 정하면 그 언어다. 앱이 config로 글을 주면 그 글이고, 비어 있으면 이 함수들이
    // 돌려준다. OS 언어는 읽지 않는다.
    // 몸통은 라이브러리 표(assets/strings, docs/i18n-adoption-design.md)의 조회다. 낱말마다 함수 하나라 호출부는 언어를 모른다.
    // 공개하지 않는다. 앱은 config 칸으로 글을 준다.
    //
    // 돌려주는 view는 정적 저장소를 가리킨다. 언어를 바꿔도 살아 있다. 수명 걱정 없이 `std::u8string`으로 옮겨 담아 쓴다.

    // 닫기 단추의 툴팁이자 접근 이름이다 (닫기).
    [[nodiscard]] std::u8string_view info_bar_close() noexcept;
    // 접근 이름 앞에 붙는 심각도 낱말이다 (정보·성공·경고·오류). 정보 막대가 쓰고 msg_box도 쓴다.
    [[nodiscard]] std::u8string_view severity_name(ui_severity severity) noexcept;
    // 분할 버튼 화살표의 접근 이름이다. 어순은 표의 문장이다 (ko는 본체 이름 뒤에 " 옵션"을 잇는다).
    //  - 본체 이름이 비면 앞 공백이 든 " 옵션"이 나온다. 이 동작은 바꾸지 않는다.
    [[nodiscard]] std::u8string split_button_arrow_name(std::u8string_view body_name);
    [[nodiscard]] std::u8string_view zoom_decrease() noexcept;  // 축소
    [[nodiscard]] std::u8string_view zoom_increase() noexcept;  // 확대
    [[nodiscard]] std::u8string_view zoom_fit() noexcept;  // 맞춤
    [[nodiscard]] std::u8string_view theme_picker_name() noexcept;  // 테마
    [[nodiscard]] std::u8string_view theme_picker_label(theme_preference preference) noexcept;  // 밝게·시스템·어둡게
    [[nodiscard]] std::u8string_view accent_picker_name() noexcept;  // 강조색
    [[nodiscard]] std::u8string_view combo_box_loading() noexcept;  // 추천 항목을 불러오는 중
    // 표준 확인 대화 상자 단추의 글이다 (확인·취소·예·아니요). 공개 helper `luil::message_box_ok` 같은 함수가 글이 비었을 때 쓴다.
    //  - 이름에 `_label`을 붙이는 까닭은 공개 helper와 이름이 같아 한정 없이 부르면 둘이 헷갈리기 때문이다.
    [[nodiscard]] std::u8string_view message_box_ok_label() noexcept;  // 확인
    [[nodiscard]] std::u8string_view message_box_cancel_label() noexcept;  // 취소
    [[nodiscard]] std::u8string_view message_box_yes_label() noexcept;  // 예
    [[nodiscard]] std::u8string_view message_box_no_label() noexcept;  // 아니요
    // 별점의 기본 접근 이름이다 (별점).
    [[nodiscard]] std::u8string_view rating_name() noexcept;
    // 별점의 기본 접근 값 글이다 ("3 / 5", 소수 첫째 자리가 있으면 "3.5 / 5"). 값과 최댓값을 받는 함수라 문자열을 새로 짓는다.
    //  - 값을 10배 반올림한 정수로 정수부와 소수 첫째 자리를 지어 로케일과 부동소수 서식 함수에 달리지 않게 한다.
    //    숫자 서식(자릿점, 숫자 모양)은 넣지 않는다.
    [[nodiscard]] std::u8string rating_value_name(float value, std::size_t count);
    // 실행 취소 명령표(`make_undo_commands`)가 앱이 글을 주지 않았을 때 쓰는 명령 이름이다. 걸음 이름을 잇는 문장은 앱 몫이다.
    [[nodiscard]] std::u8string_view undo_command_undo() noexcept;  // 실행 취소
    [[nodiscard]] std::u8string_view undo_command_redo() noexcept;  // 다시 실행
    // 잉크 캔버스의 기본 접근 이름이다 (잉크 캔버스).
    [[nodiscard]] std::u8string_view ink_canvas_name() noexcept;
    // 잉크 캔버스의 기본 접근 값 글이다 ("획 3개"). 획 수가 끼는 문장이라 문자열을 새로 짓는다. 복수형은 표의 범주 객체다.
    [[nodiscard]] std::u8string ink_canvas_summary(std::size_t strokes);
    // 색 고르기와 색 칸의 기본 접근 이름이다 (색).
    [[nodiscard]] std::u8string_view color_picker_name() noexcept;
    // 색 고르기 판의 기본 접근 이름이다 (채도와 명도).
    [[nodiscard]] std::u8string_view color_picker_plane_name() noexcept;
    // 색 고르기 판의 접근 값 글이다 ("채도 40%, 명도 80%"). 백분율은 [0, 1]로 자른 값을 lround한 정수다.
    //  - NaN은 0이다. 숫자 글자를 직접 지어 로케일에 달리지 않는다.
    [[nodiscard]] std::u8string color_picker_plane_value(float saturation, float value);
    [[nodiscard]] std::u8string_view color_picker_hue_name() noexcept;  // 색상
    [[nodiscard]] std::u8string_view color_picker_alpha_name() noexcept;  // 불투명도
    [[nodiscard]] std::u8string_view color_picker_hex_name() noexcept;  // 16진 색
    // 서식 명령표(`make_rich_text_commands`)가 앱이 이름을 주지 않았을 때 쓰는 명령 이름이다.
    [[nodiscard]] std::u8string_view rich_text_bold() noexcept;  // 굵게
    [[nodiscard]] std::u8string_view rich_text_italic() noexcept;  // 기울임꼴
    [[nodiscard]] std::u8string_view rich_text_underline() noexcept;  // 밑줄
    [[nodiscard]] std::u8string_view rich_text_code() noexcept;  // 코드
    [[nodiscard]] std::u8string_view rich_text_link() noexcept;  // 링크
    [[nodiscard]] std::u8string_view rich_text_clear_format() noexcept;  // 서식 지우기
    // 메뉴 막대의 기본 접근 이름이다 (메뉴 막대).
    [[nodiscard]] std::u8string_view menu_bar_name() noexcept;
    // 날짜 칸과 달력의 기본 문구다 (docs/calendar-date-design.md). 숫자는 별점 값 글처럼 숫자 글자를 직접 짓고 달·날 앞에
    // 0을 붙이지 않는다 ("11월 3일"). 언어별 어순은 표의 문장이고 날짜 꼴의 OS 서식은 넣지 않는다.
    [[nodiscard]] std::u8string_view date_field_name() noexcept;  // 날짜
    [[nodiscard]] std::u8string_view date_field_placeholder() noexcept;  // 날짜 선택
    // 닫힌 날짜 칸의 글이자 접근 값이다 ("2026년 11월 3일").
    [[nodiscard]] std::u8string date_field_text(std::chrono::year_month_day day);
    [[nodiscard]] std::u8string_view calendar_name() noexcept;  // 달력
    [[nodiscard]] std::u8string_view calendar_previous_month() noexcept;  // 이전 달
    [[nodiscard]] std::u8string_view calendar_next_month() noexcept;  // 다음 달
    // 달력 머리 줄의 제목이자 격자의 접근 이름이다 ("2026년 11월").
    [[nodiscard]] std::u8string calendar_month_title(std::chrono::year_month month);
    // 요일 줄의 한 글자 이름이다 (일·월·화·수·목·금·토). `ok()`가 아닌 요일은 빈 글이다.
    [[nodiscard]] std::u8string_view calendar_weekday_short(std::chrono::weekday day) noexcept;
    // 날 칸의 접근 이름이다 ("2026년 11월 3일 화요일"). 요일은 온 이름이다.
    [[nodiscard]] std::u8string calendar_day_name(std::chrono::year_month_day day);
    // 날 이름 뒤 꼬리의 낱말이다 (", 오늘"·", 고를 수 없음"의 낱말). 잇는 차례와 쉼표는 요소가 정한다.
    [[nodiscard]] std::u8string_view calendar_today() noexcept;  // 오늘
    [[nodiscard]] std::u8string_view calendar_unavailable() noexcept;  // 고를 수 없음
    // 안내 풍선의 기본 접근 이름과 닫기 단추의 툴팁·이름이다 (docs/teaching-tip-design.md). 정보 막대의 닫기와 같은 낱말이지만
    // 요소마다 함수를 둔다.
    [[nodiscard]] std::u8string_view teaching_tip_name() noexcept;  // 안내
    [[nodiscard]] std::u8string_view teaching_tip_close() noexcept;  // 닫기

    // 앱이 준 글이 비어 있으면 기본 문구다. 돌려준 view는 두 인자보다 오래 살지 않는다.
    //  - 빈 값의 판정은 `empty()`뿐이다. 공백만 있는 글은 앱의 글이다.
    [[nodiscard]] std::u8string_view text_or_default(std::u8string_view configured, std::u8string_view fallback) noexcept;
}

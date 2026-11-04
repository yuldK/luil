# 달력 날짜 계산과 문구

[날짜 고르기](date-picker-design.md)가 쓰는 순수 날짜 함수와 기본 문구를 둔다. 값 형은 C++20 `std::chrono::year_month_day`·`year_month`·`weekday`이고 시간대가 없는 그레고리력 날짜다. 공개 형식은 [calendar_date.h](../include/luil/ui/calendar_date.h)의 `calendar_bounds`·`calendar_move`·`calendar_month_grid`·`add_months_clamped`·`clamp_date`·`calendar_contains`·`calendar_start_cursor`·`calendar_moved`·`format_iso_date`·`parse_iso_date`이고 정의는 `src/ui/calendar_date.cpp`다. 요소 문서가 250줄을 넘어 나눴다.

## 문제

- 날짜 계산이 없다. `include`·`src`에 `year_month_day`를 쓰는 곳이 없다. 표준 형만으로는 `2026y/January/31d + months { 1 }`이 2월 31일(`ok()` 거짓)이 되어 앱마다 말일 접기를 짓는다.
- 주 시작 요일과 월·요일 이름의 출처가 없다. 지금 지역 질의는 두 플랫폼 모두 글꼴용 언어 태그 하나다. Win32는 `GetUserDefaultLocaleName`([win32_fonts.cpp](../src/win32/win32_fonts.cpp) 70~76행), Android는 `AConfiguration`의 `read_language`([android_app.cpp](../src/android/android_app.cpp) 152~156행)다. 기본 문구는 "OS 언어는 읽지 않는다"([default_text.h](../src/ui/default_text.h) 13행).
- R5 i18n이 날짜 문구를 자리 값 문장으로 바꿨다. 첫 요일은 앱이 `user_first_weekday`로 넣는다. 날짜 꼴의 OS 기본은 넣지 않는다([흡수 문서](i18n-adoption-design.md)).

## 공개 형식

```cpp
// include/luil/ui/calendar_date.h
struct calendar_bounds
{
    std::optional<std::chrono::year_month_day> minimum {};  // 비면 0001-01-01
    std::optional<std::chrono::year_month_day> maximum {};  // 비면 9999-12-31
};
enum class calendar_move
{
    previous_day, next_day, previous_week, next_week,
    previous_month, next_month, previous_year, next_year,
    month_start, month_end,
};
// 6주 x 7일. 첫 칸은 그 달 1일과 같거나 그 앞의 가장 가까운 `first_day`다.
[[nodiscard]] std::array<std::chrono::year_month_day, 42> calendar_month_grid(std::chrono::year_month month, std::chrono::weekday first_day) noexcept;
// 달을 더하고 날을 그 달 말일로 접는다. 해는 12달이다.
[[nodiscard]] std::chrono::year_month_day add_months_clamped(std::chrono::year_month_day day, int months) noexcept;
[[nodiscard]] std::chrono::year_month_day clamp_date(std::chrono::year_month_day day, const calendar_bounds& bounds) noexcept;
[[nodiscard]] bool calendar_contains(const calendar_bounds& bounds, std::chrono::year_month_day day) noexcept;
// 값, 없으면 오늘, 없으면 범위의 최소, 그것도 없으면 1970-01-01. 범위로 접는다.
[[nodiscard]] std::chrono::year_month_day calendar_start_cursor(std::optional<std::chrono::year_month_day> value, std::optional<std::chrono::year_month_day> today, const calendar_bounds& bounds) noexcept;
[[nodiscard]] std::chrono::year_month_day calendar_moved(std::chrono::year_month_day cursor, calendar_move move, const calendar_bounds& bounds) noexcept;
[[nodiscard]] std::u8string format_iso_date(std::chrono::year_month_day day);  // "2026-11-03"
[[nodiscard]] std::optional<std::chrono::year_month_day> parse_iso_date(std::u8string_view text) noexcept;

// src/ui/default_text.h 끝 (비공개)
[[nodiscard]] std::u8string_view date_field_name() noexcept;  // 날짜
[[nodiscard]] std::u8string_view date_field_placeholder() noexcept;  // 날짜 선택
[[nodiscard]] std::u8string date_field_text(std::chrono::year_month_day day);  // "2026년 11월 3일"
[[nodiscard]] std::u8string_view calendar_name() noexcept;  // 달력
[[nodiscard]] std::u8string_view calendar_previous_month() noexcept;  // 이전 달
[[nodiscard]] std::u8string_view calendar_next_month() noexcept;  // 다음 달
[[nodiscard]] std::u8string calendar_month_title(std::chrono::year_month month);  // "2026년 11월"
[[nodiscard]] std::u8string_view calendar_weekday_short(std::chrono::weekday day) noexcept;  // 일·월·화·수·목·금·토
[[nodiscard]] std::u8string calendar_day_name(std::chrono::year_month_day day);  // "2026년 11월 3일 화요일"
[[nodiscard]] std::u8string_view calendar_today() noexcept;  // 오늘
[[nodiscard]] std::u8string_view calendar_unavailable() noexcept;  // 고를 수 없음
```

## 규칙

**범위와 틀린 값.** 날짜는 1년부터 9999년까지다. `bounds`가 비면 그 끝이고, 최소가 최대보다 늦으면 최대를 최소로 본다. `ok()`가 아닌 `value`·`today`는 없는 것으로 보고, `ok()`가 아닌 커서는 요소가 `calendar_start_cursor`로 바꿔 그린다. 범위 밖 값은 그대로 두되(앱 상태다) 커서는 범위로 접는다. 순수 함수에서는 `clamp_date`가 `ok()`가 아닌 날을 범위의 최소로, `calendar_moved`가 그런 커서를 최소에서 세고, `calendar_month_grid`가 틀린 달을 1970년 1월로, 틀린 요일을 일요일로 본다(구현 중 정함).

**격자.** `calendar_month_grid`의 첫 칸은 1일에서 `(1일의 요일 - first_day)`일 앞이다. 1일이 `first_day`면 첫 줄 첫 칸이 1일이다. 6주로 고정해 높이가 달마다 같다. 계산은 `sys_days` 산술이라 해와 달 경계를 따로 다루지 않는다. 0001년 1월의 앞 칸은 범위 밖이라 요소가 고를 수 없게 그린다.

**이동.** `calendar_moved`는 일·주를 `sys_days` 산술, 달·해를 `add_months_clamped`, 처음·끝을 그 달 1일과 `year_month_day_last`로 짓고 결과를 `clamp_date`로 접는다. 1월 31일의 다음 달은 2월 28일(윤년이면 29일), 2월 29일의 다음 해는 2월 28일이다. 같은 날로 돌아오면 요소가 빈 목록을 낸다.

**오늘.** 라이브러리는 시계를 읽지 않는다. 앱이 `calendar_config::today`로 준다. 자정이 지나면 앱이 다음 frame에 바꾼다. test는 고정 날을 주고 데모는 시작할 때 Win32 `localtime_s`, Android `localtime_r`로 한 번 읽는다(데모 몫). 시간대(`zoned_time`·`current_zone`)는 쓰지 않는다.

**ISO 글.** `format_iso_date`는 네 자리 해, 두 자리 달·날을 숫자 글자로 직접 짓는다. 날 칸의 owner와 test, 앱 저장에 쓰는 글이고 화면 글이 아니다. `parse_iso_date`는 정확히 그 꼴만 읽고 `ok()`와 1~9999년을 검사한다. 공백·한 자리 달·2월 30일은 `std::nullopt`다.

**기본 문구.** 한국어이고 낱말마다 함수 하나다(기본 문구 규칙). 숫자는 별점 값 글처럼 숫자 글자를 직접 지어 로케일과 서식 함수에 달리지 않는다(default_text.h 41~44행). 달·날 앞에 0을 붙이지 않는다("11월 3일"). 요일 이름은 한 글자이고 날 이름에는 "화요일"처럼 온 이름을 쓴다. 앱은 `calendar_text`와 `date_field_config::format`으로 덮는다. 날 이름 뒤 꼬리(", 오늘"·", 고를 수 없음")의 순서는 요소가 정하고 낱말은 `calendar_text::today`·`unavailable`로 덮는다.

## 결정

**1. 값 형.** 질문: 표준 `year_month_day`를 쓰는가, 자체 형을 두는가.

- 길 a: `std::chrono::year_month_day`·`year_month`·`weekday`. 되는 것: 앱의 날짜와 곧바로 맞물리고 요일·`sys_days` 산술이 표준이다. 두 툴체인에 헤더가 있다(NDK 27.3 libc++ `__chrono/year_month_day.h`). 대가: 달 더하기가 `ok()` 거짓을 낼 수 있어 접는 helper가 필요하다. 그 helper가 이 헤더다.
- 길 b: `struct calendar_day { int year; int month; int day; }`. 되는 것: 표준 라이브러리 지원 폭에 매이지 않는다. 대가: 요일·윤년·일 수 산술을 다시 짓고 앱이 형을 옮긴다.
- 권장: a. 대가: 위. 권장대로 진행.

**2. 지역의 출처.** 질문: 주 시작 요일과 월·요일 이름을 OS에서 읽는가.

- 길 a: config와 한국어 기본. OS를 읽지 않는다. `first_day` 기본은 일요일이다(한국어 기본 문구와 같은 관례). 되는 것: 기본 문구 규칙(default_text.h 13행)과 같고 두 플랫폼 코드가 없다. R5 i18n이 몸통을 표로 바꿀 때 호출부가 그대로다. 대가: 월요일로 시작하는 지역의 앱은 `first_day`를 스스로 준다.
- 길 b: 공개 지역 질의를 더한다. Win32는 `GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, ...)`(0이 월요일, 6이 일요일)로 읽는다. 지금 유일한 지역 질의(win32_fonts.cpp 73행) 곁이다. Android는 JNI로 framework의 `java.util.Calendar.getInstance().getFirstDayOfWeek()`(1이 일요일)를 부른다. 기존 framework 클래스 호출이라 Java 소스는 0줄이다(`FindClass` 선례 android_app.cpp 575행). 되는 것: 사용자의 지역을 따른다. 대가: `platform_services*`와 `ui_platform.h`는 이번 라운드 줄 4의 것이고, OS 언어를 읽지 않는다는 규칙과 갈린다. R5에서 G6-locale b로 첫 요일만 질의하고 달·요일 이름은 표 문장으로 닫았다([흡수 문서](i18n-adoption-design.md)).
- 권장: a. 대가: 위. R5에서 G6-locale b로 앱이 `user_first_weekday`의 값을 넣는 길을 더했다. 기본값은 그대로 일요일이다. 권장대로 진행.

**3. 닫힌 칸의 기본 글.** 질문: 값을 어떤 꼴로 보이는가.

- 길 a: "2026년 11월 3일". 되는 것: 한국어 기본 문구와 같은 말이고 접근 값과 같다. 대가: 칸 폭이 ISO보다 넓다.
- 길 b: "2026. 11. 3.". 되는 것: 짧다. 대가: 화면 읽기가 숫자와 마침표로 읽는다.
- 길 c: ISO "2026-11-03". 되는 것: 지역에 매이지 않는다. 대가: 한국어 기본 문구와 결이 다르다.
- 권장: a. 대가: 위. 앱은 `format`으로 바꾼다. 권장대로 진행.

## 플랫폼

**Win32·Android.** 순수 코어 코드라 플랫폼 코드가 없다. Java 소스는 0줄이고 JNI도 unsupported도 없다. R5의 첫 요일 질의는 Win32 함수 하나와 Android JNI 호출 하나(`java.util.Calendar`)다. `DateFormatSymbols`는 넣지 않는다.

## 다른 노드와의 약속

- **R5 i18n.** R5 i18n이 날짜 문구를 자리 값 문장으로 바꿨다. 첫 요일은 앱이 `user_first_weekday`로 넣는다. 날짜 꼴의 OS 기본은 넣지 않는다. `calendar_text`·`format`·`first_day` 칸과 호출부는 그대로다([흡수 문서](i18n-adoption-design.md)).
- **default_text.** 열한 함수를 끝에 더한다. 같은 줄 뒤 teaching_tip도 끝에 두 함수(`teaching_tip_name`·`teaching_tip_close`)를 붙여 병합은 기계적이다. 한글은 `default_text.cpp`에만 둔다(`luil_default_text` 검사).
- **markup(줄 2)·auto_update(줄 4).** 겹치는 파일이 없다.

## 확인

- `tests/calendar_date_tests.cpp`(새 파일, `luil_core_tests`). 윤년(1900 아님, 2000·2024 맞음, 2100 아님), 2월 말일, 1월 31일에서 한 달은 2월 28·29일, 2월 29일에서 한 해는 2월 28일, 12월에서 1월로 해 넘김, 일곱 `first_day`마다 42칸의 첫 칸 요일과 1일 자리, 1일이 `first_day`인 달, 범위 접기, 최소가 최대보다 늦은 범위, 시작 커서의 차례, `ok()`가 아닌 입력, 0001-01-01 앞과 9999-12-31 뒤에서 멈춤, `calendar_moved` 열 가지, ISO 왕복과 틀린 글(`2026-02-30`, `2026-1-3`, 앞뒤 공백).
- `tests/default_text_tests.cpp`에 열한 함수의 글, 달·날 앞 0 없음, 요일 일곱.
- 사람 확인은 없다. 화면 글의 확인은 [날짜 고르기](date-picker-design.md)에 있다.

## 범위 밖

- 시각과 시간대, 음력과 다른 달력, 주 번호.
- OS 날짜 서식(범위 C). 첫 요일 질의는 [흡수 문서](i18n-adoption-design.md).
- 날짜 글 읽기(쳐 넣는 칸). 날짜 칸은 읽기 전용이다.

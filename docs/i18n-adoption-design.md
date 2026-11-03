# 라이브러리 문구의 표 흡수와 지역 질의

[문자열 표](i18n-design.md)의 첫 소비자로 라이브러리 기본 문구를 표로 옮긴다. `default_text.h`의 선언과 호출부는 그대로이고 `src/ui/default_text.cpp`의 몸통만 표 조회가 된다. 편집 메뉴의 두 벌, 시스템 강조색 라벨, 강조색 이름 25개도 같은 표 규칙으로 옮긴다. OS 지역 값은 주 첫 요일 하나만 공개 질의로 읽는다. 새 공개 형식은 [platform_services.h](../include/luil/app/platform_services.h) 끝의 `user_first_weekday`(결정 2를 고르면 `user_ui_languages`도)와 [ui_theme.h](../include/luil/theme/ui_theme.h)의 `accent_label`이다. 표 문서가 250줄을 넘어 나눴다.

## 문제

- 기본 문구 46개가 한국어 리터럴이다([default_text.cpp](../src/ui/default_text.cpp)). 문장을 짓는 함수 일곱은 어순과 숫자 잇기를 몸통에 들고, 낱말 셋(`severity_name`, `theme_picker_label`, `calendar_weekday_short`)은 enum 값마다 글을 고른다.
- 편집 메뉴만 OS 언어를 따른다. `make_text_edit_menu`가 `bool korean`으로 두 벌을 고르고([text_edit_menu.cpp](../src/host/text_edit_menu.cpp) 21~31행), Android 호출부가 `user_ui_language().starts_with("ko")`로 정한다([android_app.cpp](../src/android/android_app.cpp) 1286행). 정책 a의 알려진 어긋남이다([default-text-design.md](default-text-design.md) 90·151행).
- 시스템 강조색 라벨 "시스템"이 [ui_theme.cpp](../src/theme/ui_theme.cpp) 376행에 박혀 있다. `src/theme`은 `src/ui`의 비공개 헤더를 include할 수 없는 아래 층이라 R2가 옮기지 못했다(default-text-design.md 80행).
- 강조색 이름 25개는 [accents.json](../assets/accents.json)의 한국어 `label`이다. 테마 고르기가 툴팁이자 접근 이름으로 쓴다([theme_picker_element.cpp](../src/ui/theme_picker_element.cpp) 72행). 소비자는 `LUIL_ACCENT_CATALOG`로 카탈로그를 통째 바꿀 뿐 언어별 이름을 줄 길이 없다(default-text-design.md 81행).
- R4 달력 문서가 주 시작 요일과 날짜 형식의 OS 기본을 R5에 넘겼다([calendar-date-design.md](calendar-date-design.md) 77·93·106행, [date-picker-design.md](date-picker-design.md) 248행). 확정 B 밖이고 G6-locale b가 첫 요일 하나로 좁혔다.
- 한글 검사(`luil_default_text`)는 `src/ui`·`include/luil/ui`만 보고 `default_text.cpp`를 통째로 건너뛴다([verify_default_text.cmake](../cmake/verify_default_text.cmake) 13·53행). 편집 메뉴와 시스템 라벨의 한글은 검사 밖에 있다.

## 공개 형식

```cpp
// include/luil/app/platform_services.h 끝 (luil, 줄 4)
// 사용자 지역 설정의 주 첫 요일이다. OS가 답하지 않으면 빈 값이다. 어느 thread에서나 부른다.
//  - 라이브러리는 이 값을 스스로 쓰지 않는다. `calendar_config::first_day`의 기본은 일요일 그대로이고 앱이 넣는다.
//  - 부를 때마다 OS에 묻는다. frame마다 부르지 말고 시작할 때와 앞으로 올 때(`app_lifecycle::foreground`) 읽어 둔다.
[[nodiscard]] std::optional<std::chrono::weekday> user_first_weekday();

// 결정 2를 고르면 곁에 둔다. 사용자가 고른 표시 언어의 BCP 47 태그다. 앞이 먼저다. 읽지 못하면 빈 목록이다.
[[nodiscard]] std::vector<std::u8string> user_ui_languages();

// include/luil/theme/ui_theme.h, accent_exists 곁 (결정 4)
// 키 컬러의 이름을 라이브러리 언어(`set_text_languages`)로 고른다. 카탈로그에 그 언어가 없으면 `label`이다.
[[nodiscard]] std::u8string_view accent_label(const accent_definition& accent);
```

```cpp
// 앱: 시작할 때 읽어 두고 달력을 지을 때 넣는다
first_weekday_ = luil::user_first_weekday();
calendar.first_day = first_weekday_.value_or(std::chrono::Sunday);

// src/ui/default_text.cpp: 선언과 호출부는 그대로다
std::u8string_view info_bar_close() noexcept
{
    return library_strings().text(key::info_bar_close);
}
```

```json
// assets/accents.json의 한 항목. 글 하나도 그대로 받는다 (모든 언어가 그 글)
{ "id": "blue", "label": { "ko": "블루", "en": "Blue" }, "swatch": "#a2cfff", "dark": { … }, "light": { … } }
```

`library_strings()`는 비공개 `src/text/library_text.h`가 내는 `string_lookup`이다. `set_text_languages`가 정한 라이브러리 표의 언어 차례(원자 값)를 읽으므로 `noexcept` 함수에서 부른다.

## 규칙

**키 표.** 키는 함수 이름이다(확정, default-text-design.md 174행). 라이브러리 표의 키는 83개다.

| 함수 | 키 | 자리 값 |
| --- | --- | --- |
| 낱말 36개(`info_bar_close` … `teaching_tip_close`) | 함수 이름 | 없음 |
| `severity_name(severity)` | `severity_name_info`·`_success`·`_warning`·`_error` | 없음 |
| `theme_picker_label(preference)` | `theme_picker_label_light`·`_system`·`_dark` | 없음 |
| `calendar_weekday_short(day)` | `calendar_weekday_short_sunday` … `_saturday` | 없음 |
| `split_button_arrow_name(body)` | 같은 이름 | `{label}` |
| `rating_value_name(value, count)` | 같은 이름 | `{value}`(지은 글 "3.5")·`{maximum}` |
| `ink_canvas_summary(strokes)` | 같은 이름, 범주 객체 | `{count}` |
| `color_picker_plane_value(s, v)` | 같은 이름 | `{saturation}`·`{value}` |
| `date_field_text(day)` | 같은 이름 | `{year}`·`{month}`·`{day}`·`{month_name}` |
| `calendar_month_title(month)` | 같은 이름 | `{year}`·`{month}`·`{month_name}` |
| `calendar_day_name(day)` | 같은 이름 | 날짜 넷과 `{weekday}` |
| 날짜 문장이 쓰는 낱말 묶음 | `calendar_month_name_january` … `_december`, `calendar_weekday_long_sunday` … `_saturday` | 없음 |
| 편집 메뉴 여섯 | `text_edit_menu_copy`·`_cut`·`_paste`·`_select_all`·`_open_link`·`_copy_link` | 없음 |
| 시스템 강조색 | `system_accent_label` | 없음 |

- enum 값을 받는 함수는 `<함수>_<값 이름>`이다. 낱말 묶음은 함수가 아니고 `default_text.cpp` 안에서만 찾는다. 이름은 `<요소>_<부품>` 규칙이다.
- 한국어 글은 지금과 바이트까지 같다. 그래서 기존 기대값이 그대로다. 날짜는 ko `{year}년 {month}월 {day}일`, en `{month_name} {day}, {year}`다. 날 이름은 ko `{year}년 {month}월 {day}일 {weekday}`(요일은 긴 이름 "화요일"), en `{weekday}, {month_name} {day}, {year}`다. 기본 언어 ko 파일이 `arguments`로 `month_name`을 밝혀 en이 쓰게 한다.
- 틀린 날(`ok()`가 거짓)의 날 이름은 요일 없는 `date_field_text` 문장이다(지금 "2026년 2월 30일"). 1~12 밖의 달은 `month_name`이 숫자 글자다.
- 숫자는 지금처럼 `append_number`로 숫자 글자를 직접 짓는다. 별점·색 판 값 글의 숫자 모양과 자릿점은 범위 C라 그대로다([default_text.h](../src/ui/default_text.h) 44·59행). 언어별 문장만 표로 간다.
- 영어 글은 저장소의 `en.json`에 새로 쓴다. 편집 메뉴 여섯은 지금의 영어(`Copy` … `Copy link`) 그대로다. 요일 짧은 이름은 두 글자(`Su`, `Mo`)다. 날 칸 32 논리 픽셀 안에 든다.

**편집 메뉴(결정 1).** `make_text_edit_menu`의 `bool korean`을 `const string_lookup& strings`로 바꾼다(`src/host` 비공개, 호출 한 곳과 [text_edit_menu_tests.cpp](../tests/text_edit_menu_tests.cpp)). Android 호출부가 고르는 선호 목록은 비공개 순수 함수 `text_edit_menu_languages(앱 목록, OS 태그)`가 낸다. 앱이 `set_text_languages`로 정했으면 그 목록이고, 정하지 않았으면 OS 태그와 그 뒤의 `en`이다. 끝의 `en`은 지금처럼 한국어가 아닌 OS에서 영어로 물러서게 한다. Win32에는 라이브러리가 짓는 편집 메뉴가 없다.

**시스템 강조색 라벨.** 표가 `src/text`에 서므로 `make_system_accent`(ui_theme.cpp 360~392행)가 `system_accent_label`을 라이브러리 언어로 찾는다. 층이 뒤집히지 않는다(default-text-design.md 80행이 기다린 조건). `system_accent()`는 부를 때마다 합성하므로 언어를 바꾸면 다음 조회부터 새 이름이다.

**강조색 이름(결정 4).** `generate_accents.cmake`가 `label`에 글과 언어 맵을 둘 다 받는다. 맵에는 `ko`가 있어야 하고 `accent_definition::label`은 그 `ko` 글이다(지금 값 그대로). 생성기는 언어별 이름을 따로 낸다. `accent_label`은 라이브러리 선호 목록으로 그 항목의 언어를 고르고 없으면 `label`이다. 시스템 항목은 `label`이 이미 고른 언어다. 테마 고르기(theme_picker_element.cpp 72행)가 이것을 쓴다. 저장소 카탈로그는 25개 모두에 `en`을 더한다. 맵의 태그는 문자열 표의 태그 꼴이고 틀리면 빌드가 선다.

**접근 이름 조각(결정 5).** 정보 막대([info_bar_element.cpp](../src/ui/info_bar_element.cpp) 253~263행)와 msg_box([message_box.cpp](../src/ui/message_box.cpp) 22~32행)는 심각도 낱말, 제목, 본문을 공백으로 잇는다. 그대로 둔다.

**첫 요일 질의(결정 3).**

- Windows: `GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, …)`. 0이 월요일, 6이 일요일이라 `weekday { (값 + 1) % 7 }`이다. 사용자가 지역 설정에서 바꾼 값을 읽는다. 실패하거나 0~6 밖이면 빈 값이다.
- Android: `java.util.Calendar.getInstance().getFirstDayOfWeek()`. 1이 일요일, 7이 토요일이라 `weekday { 값 - 1 }`이다. 1~7 밖이면 빈 값이다.
- 날짜 꼴의 OS 기본(`GetDateFormatEx`, `DateFormatSymbols`)은 넣지 않는다(확정 G6-locale b). 날짜 꼴은 위의 자리 값 문장이고 앱은 `date_field_config::format`·`calendar_text`로 덮는다.

**OS 표시 언어 질의(결정 2를 고를 때).** Windows는 `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, …)`의 목록이다. 지금 글꼴이 읽는 `GetUserDefaultLocaleName`([win32_fonts.cpp](../src/win32/win32_fonts.cpp) 70~80행)은 지역 서식의 이름이라 쓰지 않는다. Android는 호스트가 `AConfiguration`에서 읽어 둔 태그 하나다(android_app.cpp 152~167·546행).

**한글 검사를 넓힌다.** 흡수 뒤 `src`·`include`의 코드 줄 한글은 `text_area_element.cpp` 547행의 측정 견본 하나뿐이다(이 문서를 쓰며 주석 밖 줄을 센 값). 검사 목록을 `src`와 `include` 전체로 넓히고 `default_text.cpp`의 파일 예외를 뺀다. 한국어는 `assets/strings/ko.json`과 `accents.json`에만 산다.

## 결정

확정(2026-11-04): G6-locale b. 공개 질의 하나로 주 첫 요일만 OS에서 읽는다. 기본값은 일요일 그대로이고 앱이 결과를 달력 설정에 넣는다. 날짜 꼴의 OS 기본은 넣지 않고 R4 달력 문서의 약속을 고쳐 닫는다.

확정(2026-11-02): G6 정책 a. 기본 문구는 한국어다. 함수 이름이 키이고 호출부는 그대로다(default-text-design.md 174행). 편집 메뉴 표의 언어는 이 문서가 정한다. 오른쪽에서 왼쪽 쓰기의 화면 전환 방향은 화면 전환 노드의 후속이다(결정 조사).

**1. 편집 메뉴의 언어.** 질문: 표로 옮긴 편집 메뉴는 어느 언어로 서는가. 영어 폰에서 보이는 차이가 있다.

- 길 a: 앱이 언어를 정했으면 그 목록, 정하지 않았으면 지금처럼 OS 언어(끝에 en). 되는 것: 언어를 정하지 않는 앱의 화면이 지금과 같다. 언어를 정한 앱은 편집 메뉴와 나머지 글이 한 언어다. 대가: 정하지 않은 앱에서는 편집 메뉴만 OS를 따르는 예외가 남는다. 기본 문구가 OS를 읽지 않는다는 규칙의 예외가 그대로다.
- 길 b: 늘 라이브러리 언어(기본 한국어). 되는 것: 예외가 없다. 한 화면이 늘 한 언어다. 대가: 언어를 정하지 않은 앱의 영어 폰 사용자가 보던 Copy·Paste가 복사·붙여넣기로 바뀐다.
- 길 c: 늘 OS 언어. 되는 것: 지금과 같다. 대가: 영어로 정한 앱도 한국어 폰에서 편집 메뉴만 한국어다.
- 권장: a. 대가: 위의 남는 예외다. 승인 필요.

**2. OS 표시 언어의 공개 질의.** 질문: 앱이 언어를 정할 근거인 OS 표시 언어를 luil이 내주는가. 확정의 "질의 하나"는 지역 서식 값(첫 요일, 날짜 꼴)의 이야기였고, 이 질의는 언어 고르기의 근거라 따로 묻는다.

- 길 a: `user_ui_languages()`를 더한다. Windows는 표시 언어 목록, Android는 호스트가 이미 읽는 태그다. 되는 것: OS 언어를 따르는 앱이 `set_text_languages(luil::user_ui_languages())` 한 줄로 선다. Android 앱이 JNI를 짜지 않는다. 기본은 그대로 한국어다. 대가: 공개 함수 하나와 Windows 호출 하나가 늘고, OS 지역 질의가 둘이 된다.
- 길 b: 글꼴용 값(`user_ui_language`)을 그대로 공개한다. 되는 것: 새 플랫폼 코드가 없다. 대가: Windows에서는 표시 언어가 아니라 지역 서식 이름이라 영어 표시·한국 지역인 PC에서 ko가 나온다.
- 길 c: 공개하지 않는다. 되는 것: 확정의 "질의 하나"를 글자 그대로 지킨다. 대가: OS 언어를 따르려는 앱은 Windows에서 OS 함수를, Android에서 JNI(`Locale.getDefault`)를 스스로 짠다.
- 권장: a. 대가: 위의 함수와 둘째 질의다. 승인 필요.

**3. 첫 요일 질의의 자리와 꼴.** 질문: 공개 질의 하나를 어디에 어떤 꼴로 두는가.

- 길 a: `platform_services.h` 끝의 함수, 답은 `std::optional<std::chrono::weekday>`. 되는 것: 플랫폼 도구 묶음에 들고 줄 4의 파일이다. 앱이 부를 때만 OS를 읽어 test가 PC마다 갈리지 않는다. 대가: 그 헤더의 다른 함수와 달리 `platform_result`가 아니다. 값 하나를 묻는 질의라 `url_scheme_allowed`처럼 값으로 답한다.
- 길 b: `ui_platform` 칸(호스트가 시작 때 읽어 싣는다). 되는 것: 앱이 함수를 부르지 않는다. 대가: 호스트가 늘 OS를 읽고, Android Activity가 다시 설 때 거둬야 한다. `ui_platform.h`를 고친다.
- 길 c: `string_table.h`에 두고 호스트 갈고리로 채운다(글꼴 registry 꼴). 대가: core에 플랫폼 갈고리가 하나 는다.
- 권장: a. 대가: 위의 답 꼴이다. 권장대로 진행.

**4. 강조색 이름 25개.** 질문: 카탈로그의 이름을 언어마다 다르게 하는가.

- 길 a: `label`에 언어 맵을 받고 `accent_label`로 고른다(위 규칙). 되는 것: 영어로 정한 앱의 테마 고르기가 모두 영어다. 소비자 카탈로그가 자기 언어를 함께 싣는다. 글 하나인 옛 카탈로그도 그대로 빌드된다. 대가: 생성기와 그 test, 25개 항목, 공개 함수 하나가 바뀐다.
- 길 b: 그대로 둔다. 되는 것: 바뀌는 것이 없다. 대가: 영어 앱에서 강조색 이름만 "블루"로 남는다. 앱이 고칠 길이 카탈로그를 한 언어로 통째 바꾸는 것뿐이다.
- 길 c: 문자열 표의 키(`accent_blue`)로 옮긴다. 대가: 카탈로그의 id가 표의 키 목록에 묶여 소비자가 카탈로그를 바꾸면 라이브러리 표까지 바꾼다.
- 권장: a. 대가: 위의 생성기 고침이다. 권장대로 진행.

**5. 접근 이름 조각 잇기.** 질문: 정보 막대와 msg_box가 공백으로 잇는 조각을 자리 값 문장으로 바꾸는가(default-text-design.md 161행).

- 길 a: 그대로 둔다. 되는 것: 조각은 낱말과 앱 글이고 어순 문장이 아니다. 화면 읽기는 조각을 차례로 읽는다. 대가: 쉼표나 쌍점으로 잇는 언어 관례는 따르지 않는다.
- 길 b: 키 하나(`{severity} {title}`)로 바꾼다. 되는 것: 언어마다 잇는 꼴을 고른다. 대가: 빈 조각을 빼는 규칙이 문장마다 갈려 키가 조합 수만큼 는다.
- 권장: a. 대가: 위의 관례다. 권장대로 진행.

## 플랫폼

**Win32.** 첫 요일과 표시 언어는 [platform_services_win32.cpp](../src/win32/platform_services_win32.cpp)에 둔다. 값을 바꾸는 순수 함수(0~6을 요일로)는 `luil_tests`가 친다. 표시 언어를 바꾸면 다시 로그인해야 한다. 지역 설정의 첫 요일은 다음 질의부터 바뀐다. `win32_window.cpp`는 고치지 않는다.

**Android.** Java 소스는 0줄이다. 첫 요일은 기존 framework 클래스 `java.util.Calendar`를 JNI로 부른다(`FindClass` 선례 android_app.cpp 575행). JNI 흐름은 비공개 함수 `read_first_weekday(JNIEnv&)` 하나에 두고 [platform_services_android.cpp](../src/android/platform_services_android.cpp)가 `jni_thread_scope`와 `jni_local_frame`(java_vm.h)으로 부른다. 그래서 어느 thread에서나 된다. `jni_failed`는 nullptr 검사 앞에 둔다(`luil_jni_order`). 실패 지점마다 예외를 거두는지는 가짜 JNIEnv test가 친다(android_settings_tests.cpp와 같은 길). 표시 언어는 JNI 없이 `AConfiguration` 값이다. Android의 luil에는 접근성 계층이 없다. 폰에 보이는 바뀜은 편집 메뉴, 날짜 칸, 달력, 테마 카드의 글이다. unsupported인 부분은 없다.

## 다른 노드와의 약속

**R4 문서 손질 계획.** 확정대로 고쳐 닫는다. 이 줄의 구현 커밋이 아래 줄만 고친다.

| 위치 | 고칠 내용 |
| --- | --- |
| calendar-date-design.md 9·93행 | "R5 i18n이 날짜 문구를 자리 값 문장으로 바꿨다. 첫 요일은 앱이 `user_first_weekday`로 넣는다. 날짜 꼴의 OS 기본은 넣지 않는다" |
| calendar-date-design.md 77·78·88행 | 길 b의 `DateFormatSymbols`와 "R5에 넘긴다"를 "G6-locale b로 첫 요일만 질의, 이름은 표 문장"으로 닫는다 |
| calendar-date-design.md 106행, date-picker-design.md 248행 | "OS 날짜 서식(범위 C). 첫 요일 질의는 [흡수 문서](i18n-adoption-design.md)" |
| date-picker-design.md 221행, default-text-design.md 208·209행 | "지역 서식은 R5"를 "언어별 문장은 R5 표, 숫자·날짜 서식은 범위 C"로, 편집 메뉴·강조색·시스템 라벨은 "R5가 옮겼다"로 |
| default-text-design.md 80~82·90·151행 | 세 판정을 "옮겼다"로, 언어 규칙을 `set_text_languages`로 |
| [calendar_element.h](../include/luil/ui/calendar_element.h) 56행 주석 | "기본은 일요일이다. OS를 읽지 않는다. 앱은 `user_first_weekday`의 값을 넣는다" |
| rating-design.md 262행 | "언어별 문장은 R5 표, 숫자 서식은 범위 C" |
| page-transition-design.md 218행 | 오른쪽에서 왼쪽 쓰기의 쪽은 i18n이 아니라 화면 전환 노드의 후속 칸이다. 지금 칸은 forward·backward뿐이고 backward는 확대 쪽까지 바꾸므로([page_transition.h](../include/luil/ui/page_transition.h) 31·67행) 쪽만 뒤집는 칸이 따로 든다 |

**닫는 약속.** 고칠 글은 없고 이 문서로 닫는다.

- [message-box-design.md](message-box-design.md) 223행: 표준 단추 넷은 `message_box_*_label` 키로 흡수했고 호출부는 그대로다.
- [ink-design.md](ink-design.md) 248행: 획 수 요약은 범주 객체다(ko `획 {count}개`, en one·other).
- [undo-stack-design.md](undo-stack-design.md) 187·223행: 걸음 이름을 잇는 줄은 앱 몫 그대로다. 앱은 자기 표에 `"undo_step": "실행 취소: {name}"`을 두고 `format`으로 짓는다. 라이브러리는 문장을 두지 않는다.
- [inspector-design.md](inspector-design.md) 238행, [auto-update-design.md](auto-update-design.md) 159행, [rotate-gesture-design.md](rotate-gesture-design.md) 173행: 옮길 글이 없다.

**같은 라운드의 다른 줄.**

- **줄 1.** `calendar_element.h` 주석 한 줄과 `theme_picker_element.cpp` 72행 한 줄을 줄 4가 고친다. 이번 라운드에 줄 1의 차트는 두 파일을 열지 않는다. `default_text.h`·`.cpp`는 줄 4가 몸통을 바꾼다. 다른 줄이 낱말을 더하면 함수는 끝에, 키는 `ko.json`·`en.json`에 함께 더한다. 차트는 자기 표를 쓴다([문자열 표](i18n-design.md)의 약속).
- **줄 2.** 닿는 파일이 없다. hot_reload는 표를 다시 읽지 않는다.
- **줄 3.** R5 노드가 없다. 위의 page-transition 문서 한 줄은 문서만 고친다.
- **고치는 줄 4 파일.** `platform_services.h`, `platform_services_win32.cpp`, `platform_services_android.cpp`, `android_app.cpp`(1286행 호출), `src/host/text_edit_menu.*`, `src/theme/ui_theme.cpp`, `ui_theme.h`(선언 한 줄), `generate_accents.cmake`, `assets/accents.json`, `verify_default_text.cmake`, `src/ui/default_text.cpp`다.

## 확인

자동 test.

- [tests/default_text_tests.cpp](../tests/default_text_tests.cpp): 기존 단언(한국어 기본)이 바뀌지 않고 통과해야 한다. 덧붙임: en으로 바꾸는 guard 아래 낱말 몇, `Save options`, `November 3, 2026`, `Tuesday, November 3, 2026`, `November 2026`, `Su`, `1 stroke`·`2 strokes`·`0 strokes`, `3.5 / 5`, 색 판 값 글. ko로 되돌린 뒤 같은 글. 키가 빠지면 `default_text.cpp`가 컴파일되지 않는다(생성 enum).
- `tests/text_edit_menu_tests.cpp`: ko·en 표로 지은 메뉴의 단추 글, `text_edit_menu_languages`가 앱 목록이 있으면 그 목록이고 없으면 OS 태그와 `en`인지(fr OS는 en으로 물러섬).
- `tests/theme_picker_tests.cpp`·`accessibility_tests.cpp`: 기본은 그대로("테마", 25개 이름). en에서 "Theme", 강조색 툴팁 "Blue", 시스템 항목 "System".
- `tests/accent_catalog_tests.cmake`: 언어 맵을 받고 `ko` 없는 맵, 틀린 태그를 막는다. 글 하나인 항목도 받는다.
- `tests/platform_services_tests.cpp`(`luil_tests`, Windows): `user_first_weekday`가 비었거나 `ok()`이고 같은 PC의 `GetLocaleInfoEx`와 맞는지, 0~6 바꿈 함수. 결정 2면 `user_ui_languages`의 태그 꼴. 값 자체는 단언하지 않는다(PC마다 다르다).
- Android 기기(`luil_core_tests`): `read_first_weekday`의 가짜 JNIEnv test가 실패 지점마다 빈 값과 예외 거둠을 친다. 1~7 바꿈.
- `luil_default_text`(넓힌 목록), `luil_jni_order`, `luil_source_style`, 전체 CTest.

데모. 라이브러리 커밋 뒤의 연결 커밋이다.

- 데스크톱 갤러리 페이지의 언어 고르기(`gallery/language`, 시스템·한국어·English)가 지금은 값만 쥔다([gallery_page.cpp](../examples/demo/gallery_page.cpp) 400~409행). 이것을 `set_text_languages`에 잇는다. 시스템은 결정 2면 `user_ui_languages()`, 아니면 빈 목록이다. 테마 페이지 카드 제목은 `accent_label`을 쓴다.
- 팝업 페이지에 체크 `date-os-first-day`("OS 첫 요일")와 label `date-first-day`("주 첫 요일: …")를 둔다. 기본은 꺼져 일요일이라 기존 UIA 확인이 그대로다.
- 모바일 데모 기기 페이지에 label `device-first-weekday`와 `device-ui-language`를 둔다.
- 격리 데스크톱 luil-uia(UIA3)로 English를 Select한 뒤 되읽는다. 테마 묶음 이름 "Theme"와 카드 "Light"·"System"·"Dark", 강조색 이름 "Blue", 날짜 칸(`popups/date-field`)의 placeholder "Select date", 인라인 달력(`popups/calendar`) 격자 이름 "November 2026" 꼴. 한국어로 돌려 "테마"·"날짜 선택"을 다시 읽는다. 체크를 켜면 달력 첫 칸 이름의 요일이 이 PC의 첫 요일과 같은지 본다.

이 PC 흔적. 설계 단계라 실행하지 않았다. 아래는 코드에서 센 값이다.

| 항목 | 값 |
| --- | --- |
| 기본 문구 함수 | 46개(`text_or_default` 뺌) |
| 라이브러리 표 키 | 83개 |
| 흡수 뒤 코드 줄 한글 | `text_area_element.cpp` 547행 하나 |

사람 확인: 영어 글 교정(원어민 눈), 실기기에서 시스템 언어를 바꾼 뒤 Activity가 다시 설 때 편집 메뉴 글(설정 앱을 건드리는 일이라 사람이), 실기기 지역 설정의 첫 요일을 바꾼 값이 `user_first_weekday`에 오는지(Android 14의 지역 환경설정이 `Calendar`에 닿는지는 확인 전), Windows 지역 설정의 첫 요일을 바꾼 뒤 데모 label, 영어 날짜 글과 강조색 이름이 칸·툴팁에서 잘리지 않는지.

## 범위 밖

- 날짜·숫자 꼴의 OS 서식(범위 C). 확정 G6-locale b가 첫 요일 하나로 좁혔다.
- 대체 글꼴의 언어를 앱 언어에 맞추기([문자열 표](i18n-design.md)의 범위 밖).
- 오른쪽에서 왼쪽 쓰기의 화면 전환 쪽. 화면 전환 노드의 후속 칸이다.
- 접근 이름 조각의 언어별 잇기(결정 5).
- 앱이 주는 기존 칸에 기본값을 켜는 일(default-text-design.md 결정 4 그대로).

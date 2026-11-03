# 문자열 표

앱과 라이브러리가 화면 글을 언어마다 다른 문장으로 고르게 한다. 언어마다 JSON 파일 하나를 두고 빌드 때 생성기가 C++ 헤더로 바꾼다. 실행 중에는 그 정적 표에서 고른 언어의 문장을 찾고 자리 값과 복수형을 채운다. 공개 형식은 새 헤더 `include/luil/text/string_table.h`의 `string_table`·`string_lookup`·`select_strings`·`set_text_languages`와 설치되는 CMake 함수 `luil_add_string_table`이다. 라이브러리 기본 문구([default_text.h](../src/ui/default_text.h))가 이 표의 첫 소비자다. 기본 문구와 편집 메뉴·강조색 이름을 표로 옮기는 일, OS 지역 질의는 [라이브러리 문구의 표 흡수와 지역 질의](i18n-adoption-design.md)로 나눴다.

## 문제

- 언어를 고를 길이 없다. 라이브러리 기본 문구는 늘 한국어이고([default-text-design.md](default-text-design.md) 90행), 영어 앱은 칸마다 글을 채운다(같은 문서 62~66행의 줌 단추 예).
- 편집 메뉴만 `bool korean`으로 두 벌을 들고 OS 언어를 따른다([text_edit_menu.cpp](../src/host/text_edit_menu.cpp) 24~31행, [android_app.cpp](../src/android/android_app.cpp) 1286행). 한 화면에서 편집 메뉴만 영어가 된다.
- 어순이 코드에 박혀 있다. 분할 버튼 이름은 본체 이름 뒤에 " 옵션"을 잇고, 날짜 문구는 숫자 사이에 "년 "·"월 "·"일"을 잇는다([default_text.cpp](../src/ui/default_text.cpp) 33~41·65~70·285~293행). 어순이 다른 언어는 함수 몸통을 다시 짜야 한다.
- 수가 끼는 문장에 복수형이 없다. `ink_canvas_summary`는 "획 3개"를 짓는다(173~179행). 영어는 "1 stroke"와 "2 strokes"가 갈린다.
- 앱이 쓸 표가 없다. 앱마다 표와 고르기를 따로 짓는다. core 밖에 서는 선택 라이브러리(줄 1의 차트)는 비공개 `default_text`를 쓸 수 없다.

## 공개 형식

```cpp
// include/luil/text/string_table.h (luil_core). 정의는 src/text/string_table.cpp
namespace luil {
    // CLDR 복수 범주다. 1단계 규칙이 내는 것은 one과 other뿐이다 (결정 3).
    enum class plural_category : std::uint8_t { zero, one, two, few, many, other };
    // 언어의 복수 규칙이다. JSON 머리의 "plural" 값과 같은 이름이다.
    enum class plural_rule : std::uint8_t
    {
        other,  // 늘 other다 (ko·ja·zh 같은 언어)
        one,    // 절댓값이 1이면 one, 아니면 other다 (en 같은 언어)
    };
    [[nodiscard]] plural_category plural_category_for(plural_rule rule, std::int64_t count) noexcept;

    // 문장 하나다. 범주 차례로 둔다. 복수형이 아니면 other 칸만 찬다.
    struct string_message
    {
        std::array<std::u8string_view, 6> forms {};
    };
    struct string_table_language
    {
        std::u8string_view tag {};  // BCP 47 (ko, en, pt-BR)
        plural_rule plural { plural_rule::other };
        std::span<const string_message> messages {};  // 키 차례
    };
    // 생성 헤더가 정적 저장소에 짓는 표다. 첫 언어가 기본 언어다. 모든 언어의 키 차례가 같다.
    struct string_table
    {
        std::span<const std::u8string_view> keys {};
        std::span<const string_table_language> languages {};
    };

    // 자리 값 하나다. 수는 십진 숫자 글자로 짓는다 (자릿점 없음, 음수는 '-').
    struct string_argument
    {
        std::u8string_view name {};
        std::variant<std::u8string_view, std::int64_t> value {};
    };

    // 표 하나와 고른 언어다. 값이라 앱이 상태로 쥐고 언어가 바뀌면 다시 고른다.
    class string_lookup
    {
    public:
        string_lookup() noexcept = default;
        string_lookup(const string_table& table, std::size_t language) noexcept;
        [[nodiscard]] std::u8string_view tag() const noexcept;
        // 자리 값을 채우지 않은 문장이다. 복수형이면 other 꼴이다. 범위 밖 키는 빈 글이다. view는 정적 저장소다.
        [[nodiscard]] std::u8string_view text(std::size_t key) const noexcept;
        // 자리 값을 채운 문장이다. 복수형이면 `count` 인자(정수)로 꼴을 고른다.
        [[nodiscard]] std::u8string format(std::size_t key, std::span<const string_argument> arguments) const;
        // 이름으로 키를 찾는다. 실행 중에 이름만 아는 쪽(마크업 따위)이 쓴다.
        [[nodiscard]] std::optional<std::size_t> find(std::u8string_view name) const noexcept;

    private:
        const string_table* table_ { nullptr };
        std::size_t language_ { 0 };
    };

    // 선호 목록의 차례로 표의 언어를 고른다. 없으면 기본 언어다 (규칙 절).
    [[nodiscard]] string_lookup select_strings(const string_table& table, std::span<const std::u8string> preferences) noexcept;

    // 라이브러리 기본 문구가 따를 언어의 선호 목록이다 (결정 4). 어느 thread에서나 부른다. 그 뒤에 짓는 요소부터 바뀐다.
    void set_text_languages(std::span<const std::u8string> preferences);
    // 앱이 정한 목록이다. 정한 적이 없으면 빈 값이다.
    [[nodiscard]] std::optional<std::vector<std::u8string>> text_languages();
}
```

원본과 생성 헤더, CMake 함수다.

```json
// strings/en.json. 파일 이름이 언어 태그다
{
    "language": "en",
    "plural": "one",
    "strings": {
        "save": "Save",
        "files_selected": { "one": "{count} file selected", "other": "{count} files selected" }
    }
}
```

```cpp
// <my_app 빌드 폴더>/generated/app_strings.h. 생성 파일이다. CMake 함수(cmake/string_table.cmake, 설치본에도 있다)가 짓는다:
//   luil_add_string_table(my_app NAME app_strings DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/strings" BASE en)
namespace app_strings {
    enum class key : std::size_t { files_selected, save };
    inline constexpr luil::string_table table { /* 키 이름, en(기본)·ko 언어 */ };
    [[nodiscard]] inline std::u8string_view text(const luil::string_lookup& strings, key value) noexcept;
    [[nodiscard]] inline std::u8string format(const luil::string_lookup& strings, key value, std::span<const luil::string_argument> arguments);
}

// 앱
strings_ = luil::select_strings(app_strings::table, luil::text_languages().value_or(std::vector<std::u8string> {}));
const std::array<luil::string_argument, 1> count { { { u8"count", std::int64_t { 3 } } } };
label.text = app_strings::format(strings_, app_strings::key::files_selected, count);  // "3 files selected"
```

## 규칙

**원본 파일.**

- 폴더 하나에 언어마다 `<태그>.json` 하나다(결정 1). `language`는 파일 이름과 같다. 태그는 두세 글자 언어에 `-`로 이은 부분(`Hant`, `TW`, `419`)이다.
- `plural`은 `other`·`one` 가운데 하나이고 빠지면 `other`다(결정 3). 기본 언어 파일만 `arguments`(키마다 이름 목록)를 더 둘 수 있다. 다른 언어가 기본 언어 문장에 없는 이름을 쓸 때 밝힌다(흡수 문서의 날짜 문장).
- 키는 `[a-z][a-z0-9_]*`다. 마크업의 이름 꼴 안에 든다([markup-binding-design.md](markup-binding-design.md) 127행). C++ 예약어(`delete`, `default` 따위)는 생성 헤더의 enum 이름이 되므로 막는다.
- 값은 글이거나 범주 객체(`{"one": ..., "other": ...}`)다. 글자는 UTF-8 그대로다. 따옴표·역슬래시·줄 바꿈·탭은 생성기가 C++ 글로 이스케이프한다([accent 생성기](../cmake/generate_accents.cmake)와 같다).

**생성기 검사.** 틀리면 빌드가 선다. 잘못된 글이 조용히 화면에 나가는 것보다 낫다(accent·스타일 생성기와 같은 방침, [asset-pipeline.md](concepts/asset-pipeline.md)).

- 기본 언어 파일이 있다. 모든 언어의 키 집합이 기본 언어와 같다. 빠진 키와 남는 키를 언어와 이름으로 알린다.
- 한 키는 모든 언어에서 꼴이 같다(글이거나 범주 객체). 범주 객체는 그 언어 규칙의 범주를 모두 갖고 다른 범주는 갖지 않는다. other 규칙은 `other` 하나, one 규칙은 `one`·`other`다.
- 자리 값은 `{이름}`이고 `{{`·`}}`는 글자다(마크업 바인딩과 같은 꼴). 짝 없는 괄호와 이름 꼴 밖의 글은 막는다.
- 한 키에서 언어마다 쓴 이름은 기본 언어가 쓴 이름(밝힌 `arguments`가 있으면 그 목록) 안에 든다. 꼴 하나는 그 일부만 써도 된다(영어 one 꼴 "a file"). 범주 객체는 모든 언어에서 `count`를 받는다.
- 겹친 키는 잡지 못한다. CMake의 JSON 읽기가 받아 주는 글이다([asset-pipeline.md](concepts/asset-pipeline.md) 63행).
- 생성 헤더의 언어 차례는 기본 언어 다음에 태그 글자 차례다. 같은 원본이면 같은 헤더다.

**조회.**

- 표는 정적 저장소다. `text`가 돌려준 view는 언어를 바꿔도 산다. default_text.h 18행의 약속(돌려주는 view는 정적 저장소)이 그대로 선다.
- `format`은 이름마다 인자에서 찾는다. 없는 이름은 `{이름}`을 그대로 남겨 개발자가 화면에서 보게 한다. 수는 십진 숫자 글자다. 자릿점·숫자 모양·소수 서식은 넣지 않는다(범위 C). 소수가 끼는 글(별점 "3.5")은 부르는 쪽이 지어 글 인자로 준다.
- 복수 범주는 `count` 인자로 고른다. `count`가 없거나 글이면 other다. 음수는 절댓값으로 본다(CLDR의 n). 고른 범주의 꼴이 비면 other 꼴이다.
- `select_strings`는 대소문자와 `_`·`-`를 가리지 않는다. 선호 목록의 태그마다 그대로 찾고, 끝 부분을 하나씩 떼어(`zh-Hant-TW`, `zh-Hant`, `zh`) 찾은 뒤 다음 태그로 간다. 아무것도 없으면 기본 언어다.

**언어 상태(결정 4).**

- 라이브러리 기본 문구는 `set_text_languages`의 목록으로 라이브러리 표에서 고른다. 정한 적이 없거나 빈 목록이면 한국어다(G6 정책 a).
- 프로세스에 하나다. `set_ui_platform`([ui_platform.h](../include/luil/ui/ui_platform.h) 70행)과 `set_system_accent`([ui_theme.h](../include/luil/theme/ui_theme.h))처럼 앱·호스트가 정하고 요소는 읽기만 한다. 고른 언어 차례는 원자 값이라 요소가 어느 thread에서 읽어도 된다. 목록은 mutex로 지킨다.
- 바꾼 뒤 짓는 요소부터 새 언어다. tree는 frame마다 다시 서므로 다음 frame에 화면이 바뀐다. 이미 지은 요소의 글은 그대로다.
- 앱 표는 앱이 고른다. 언어를 바꾸는 앱 메시지에서 `set_text_languages`와 `select_strings`를 함께 부르면 두 표가 같은 언어가 된다(앱 표에 그 언어가 있을 때).

**라이브러리 표.**

- 원본은 `assets/strings/ko.json`·`en.json`이고 기본 언어는 `ko`다. 키는 `default_text` 함수 이름이다([default-text-design.md](default-text-design.md) 174행. 키 목록은 흡수 문서).
- 생성 헤더는 내부 표라 설치하지 않는 자리(`LUIL_GENERATED_PRIVATE_INCLUDE_DIRECTORY`, [CMakeLists.txt](../CMakeLists.txt) 73~76행)에 둔다. `luil_core`만 include한다.
- `LUIL_STRINGS` 캐시 변수(폴더, 기본 `assets/strings`)로 소비자가 통째로 바꾼다(`LUIL_STYLE` 선례, 68~71행, 결정 5). 키 집합은 저장소의 `assets/strings/ko.json`과 같아야 하고 기본 언어는 `ko` 그대로다. 언어 파일은 더해도 된다. `ja.json`을 더하면 라이브러리 기본 문구에 일본어가 선다.

**앱 표.**

- `luil_add_string_table(<target> NAME <이름 공간> DIRECTORY <폴더> BASE <태그>)`가 같은 생성기를 그 target에 건다. 생성 헤더는 `<target 빌드 폴더>/generated/<NAME>.h`이고 target의 PRIVATE include에 더한다. 언어 파일은 configure 때 폴더에서 모으므로(`CONFIGURE_DEPENDS`) 파일을 더하면 다시 구성된다.
- 설치본에서도 쓴다. `install.cmake`가 `string_table.cmake`·`generate_strings.cmake`를 `generate_notices.cmake` 곁에 싣고(60~66행) `luil-config.cmake.in`이 include한다.
- 앱 표와 라이브러리 표는 따로 선다. 앱 키는 라이브러리 키와 겹쳐도 된다.

배율·테마·고대비·움직임 줄이기와 입력·초점에는 닿지 않는다. 글이 길어질 때는 각 요소의 지금 규칙(말줄임, 줄 바꿈)을 따른다([default-text-design.md](default-text-design.md) 94행).

## 결정

확정(2026-11-02, R4 뒤 다시 맞음을 확인): G6 정책 a와 범위 B. 기본 문구는 한국어이고 `default_text` 한 헤더에 모인다. 공개 표 API는 조회·자리 값·복수형의 최소형이고 빌드 때 JSON에서 헤더를 짓는다. 숫자·날짜·목록 서식(C)은 넣지 않는다.

확정(2026-11-04): N6 a. hot_reload는 스타일·마크업 파일만 다시 읽는다. 문자열 표는 빌드 때 헤더라 다시 읽지 않는다.

**1. 원본 파일의 꼴.** 질문: 언어별 문장을 어떤 파일에 적는가.

- 길 a: 언어마다 파일 하나(`<태그>.json`). 되는 것: 언어를 더하는 일이 파일 하나를 더하는 일이다. 번역하는 사람이 한 파일만 연다. 소비자가 라이브러리 표에 언어를 더할 때 저장소 파일을 고치지 않는다. 대가: 키 일치를 파일 사이에서 맞춘다. 생성기가 언어마다 키를 견준다.
- 길 b: 파일 하나에 키마다 언어 맵(`"save": {"ko": "저장", "en": "Save"}`). 되는 것: 빠진 번역이 한 줄에서 보인다. 대가: 언어를 더하면 모든 항목을 고친다. 소비자가 언어를 더하려면 파일을 통째로 베낀다.
- 권장: a. 대가: 위의 견주기다. 검사는 어느 길이든 같은 생성기 몫이다. 권장대로 진행.

**2. 자리 값과 복수형의 표기.** 질문: 문장 안의 값과 수에 따른 꼴을 어떻게 적는가.

- 길 a: `{이름}`과 범주 객체(위의 예). 되는 것: 마크업 바인딩의 `{이름}`·`{{`·`}}`와 같은 꼴이다. 생성기가 범주와 이름을 JSON 구조로 검사한다. 읽기가 작다. 대가: 한 문장 안에 수 둘의 복수형을 섞지 못한다. 그런 문장은 앱이 둘로 나눠 잇는다.
- 길 b: ICU MessageFormat 한 문장(`{count, plural, one {...} other {...}}`). 되는 것: 다른 도구가 아는 꼴이고 수 여럿을 섞는다. 대가: 중첩 문법의 읽기와 검사를 새로 짓는다. 1단계 크기를 넘는다.
- 길 c: 범주마다 키(`files_selected_one`). 되는 것: 모든 값이 글이다. 대가: 키 이름과 범주가 섞여 키가 함수 이름이라는 약속이 흐려진다.
- 권장: a. 대가: 위의 섞기다. 권장대로 진행.

**3. 1단계 복수 규칙.** 질문: 어떤 언어의 규칙을 처음에 넣는가.

- 길 a: 두 갈래. `other`(ko·ja·zh 같은 한 꼴 언어)와 `one`(절댓값 1이면 one, en 같은 언어). 언어 파일의 `plural`이 고른다. 되는 것: 작고 틀릴 자리가 적다. 라이브러리 두 언어를 덮는다. 앱이 더하는 언어도 둘 가운데 맞는 쪽을 고른다(de·es·it는 one). 대가: fr·pt(0과 1이 one)와 ru·pl·ar(셋 이상의 꼴)는 맞는 갈래가 없다. 그 언어 앱은 one으로 어림하거나 수를 문장 밖에 둔다.
- 길 b: a에 CLDR 정수 규칙 다섯 갈래(0·1 one, 동슬라브, 폴란드, 체코, 아랍)를 더한다. 되는 것: 유럽과 중동의 주요 언어가 바로 선다. 대가: 손으로 옮긴 규칙이 틀리면 조용히 어색한 문장이 나온다. CLDR 견본 값으로 갈래마다 test를 둬야 한다. ICU 표는 쓸 수 없다(줄 나눔과 같은 까닭, [line-break-design.md](line-break-design.md) 125행).
- 길 c: 앱이 언어마다 규칙 함수를 준다. 되는 것: 어느 언어든 선다. 대가: 생성 헤더의 표가 정적 값이 아니게 되고 등록 API가 는다.
- 권장: a. `plural_rule`은 enum이라 b의 갈래는 나중에 값만 더하면 되고 원본 꼴은 그대로다. 대가: 위의 빈 언어다. 권장대로 진행.

**4. 언어 상태의 자리.** 질문: 라이브러리 기본 문구가 어느 언어인지를 어디에 두는가.

- 길 a: 프로세스 전역의 선호 목록(`set_text_languages`). 되는 것: `default_text` 함수가 인자 없이 언어를 안다. 확정된 "호출부 그대로"(default-text-design.md 174행)를 지킨다. frame 밖에서 짓는 Android 편집 메뉴도 같은 값을 본다. 대가: "상태는 앱 것"에 전역 하나가 는다. 시스템 강조색·플랫폼 값과 같은 꼴이라 새 종류의 전역은 아니다. test는 바꾼 언어를 되돌린다.
- 길 b: `ui_frame` 칸. 되는 것: 상태가 frame에 실린다. 대가: 요소 생성자는 frame을 받지 않는다. 기본 문구 46개의 호출부와 요소마다 문맥 전달을 고친다. 확정과 어긋난다.
- 길 c: 요소 config마다 언어 칸. 대가: 칸이 요소 수만큼 늘고 앱이 모두 채운다.
- 권장: a. 대가: 위의 전역 하나다. 권장대로 진행.

**5. 라이브러리 표와 앱 표.** 질문: 앱의 문장과 라이브러리 문장을 한 표에 두는가.

- 길 a: 따로 둔다. 앱 표는 설치되는 CMake 함수로 앱이 짓고, 라이브러리 표는 `LUIL_STRINGS` 폴더로 통째 바꾼다. 되는 것: 앱 키가 라이브러리 키 검사에 걸리지 않는다. 차트 같은 선택 라이브러리도 같은 함수로 자기 표를 짓는다. 소비자가 라이브러리 문구에 언어를 더할 길이 있다. 대가: 공개 CMake 함수 하나와 캐시 변수 하나가 는다. 앱은 표 둘을 같은 목록으로 고른다.
- 길 b: 앱 표만 둔다. 라이브러리 표는 저장소의 두 언어로 고정이다. 되는 것: 캐시 변수가 없다. 대가: 일본어 앱은 라이브러리 기본 문구를 칸마다 채운다.
- 길 c: 앱 키를 라이브러리 표에 합친다. 대가: 앱 키가 `luil_core`에 구워져 앱마다 luil을 다시 빌드한다. 키 이름이 겹친다.
- 권장: a. 대가: 위의 함수와 변수다. 권장대로 진행.

**6. 마크업의 `{t:key}`.** 질문: 마크업 속성 값에서 표를 바로 찾는 꼴을 이번에 여는가. 마크업 바인딩은 `{` 안의 `:`를 이 노드를 위해 비워 두었다(markup-binding-design.md 127·229행, [ui_markup_vocabulary.cpp](../src/ui/ui_markup_vocabulary.cpp) 499행).

- 길 a: 연다. 줄 2가 `ui_markup_context` 끝에 조회 함수 칸(`std::function<std::optional<std::u8string_view>(std::u8string_view key)> text`)을 더하고 `t:` 접두를 읽는다. 줄 4는 `string_lookup::find`·`text`를 낸다. 자리 값이 없는 문장만 받는다. 되는 것: 다시 읽은 마크업이 C++ 고침 없이 빌드한 표의 키를 쓴다. 대가: 줄 2의 파일 셋(`ui_markup.h`, 어휘, 펼치기)과 그 test가 이번 라운드에 는다. 줄 2의 이번 노드는 hot_reload라 마크업 일이 따로 얹힌다.
- 길 b: 미룬다. `:`는 그대로 비워 두고 앱이 고른 문장을 값 표에 넣는다(지금 방식, 229행). 되는 것: 줄 사이 겹침이 없다. 마크업을 쓰는 앱은 이미 이 길로 번역 글을 넣는다. 대가: 마크업에 새 키를 쓰려면 앱 C++에 값 한 줄을 더한다.
- 권장: b. 연결 꼴은 위 a로 약속해 두고 마크업 후속에서 연다. 대가: 위의 한 줄이다. 승인 필요.

## 플랫폼

**Win32.** 표와 조회는 `luil_core`의 코드라 플랫폼 코드가 없다. 생성은 빌드 기계의 CMake가 한다. 언어를 정하는 것은 앱이다. OS 표시 언어를 읽는 질의는 [흡수 문서](i18n-adoption-design.md)의 결정 2다.

**Android.** Java 소스는 0줄이고 표는 JNI를 부르지 않는다. 생성 헤더가 `.so` 안에 들어가므로 APK assets가 필요 없다(codicon 글꼴을 싣는 것과 같은 까닭). 시스템 언어를 바꾸면 프로세스는 남고 Activity만 다시 선다. 호스트는 그때 OS 언어를 다시 읽고([android_app.cpp](../src/android/android_app.cpp) 546~547행) 앱의 `on_started`를 다시 부른다(676행). OS 언어를 따르는 앱은 그 자리에서 목록을 다시 정한다. 정한 목록은 프로세스에 남는다. Android의 luil에는 접근성 계층이 없어 접근 이름은 쓰이지 않는다. 폰에 보이는 바뀜은 화면 글(줌 단추, 테마 카드, 날짜 칸, 편집 메뉴)이다. unsupported인 부분은 없다.

## 다른 노드와의 약속

- **흡수와 지역 질의(같은 줄, 같은 라운드).** [그 문서](i18n-adoption-design.md)가 이 표의 첫 소비자다. 라이브러리 커밋은 이 문서의 표·생성기 커밋 다음에 쌓는다.
- **hot_reload(줄 2).** 확정 N6 a대로 문자열 표를 다시 읽지 않는다. 언어 바꿈은 다시 읽기가 아니라 앱 메시지다. 결정 6이 b면 다시 읽은 마크업의 번역 글도 앱의 값 표에서 온다. 줄 4는 hot_reload에 플랫폼 질의를 내지 않는다. Android 앱 폴더 경로는 앱이 `android_main`에서 읽는다(확정 N6-android a).
- **markup(줄 2 파일).** 결정 6이 a면 위의 칸 하나와 `t:` 읽기를 줄 2가 하고, 데모 연결 커밋이 앱의 조회 함수를 잇는다. b면 `ui_markup_vocabulary.cpp` 499행의 주석과 [markup-binding-design.md](markup-binding-design.md) 127·229·251행을 "마크업 후속"으로 고친다(줄 4의 문서 커밋, 코드 주석은 줄 2가 다음에 그 파일을 열 때).
- **chart(줄 1, core 밖 선택 라이브러리).** 차트는 비공개 `default_text`를 쓰지 않는다. 자기 원본(`ko.json`·`en.json`)을 두고 `luil_add_string_table`로 자기 target에 표를 짓는다. 언어는 라이브러리와 같은 선호 목록(`text_languages()`, 비었으면 빈 목록)으로 `select_strings`가 고른다. 키는 `<부품>` 낱말 이름(`chart_name`, `chart_summary`)이고 요약 같은 수 문장은 범주 객체다. 눈금 숫자는 숫자 서식이 없으므로(범위 C) 차트의 규칙대로 짓는다. 두 줄은 같은 기점에서 나란히 가므로, 줄 1은 R5 안에서 이 헤더를 기다리지 않고 낱말마다 함수인 비공개 파일(default_text 꼴, 한국어)로 시작한다. 통합 때 이 줄의 라이브러리 커밋 뒤에 그 몸통을 차트 표 조회로 바꾸는 커밋 하나를 쌓는다. 한글 검사를 넓히는 커밋(흡수 문서)은 그 뒤다. 차트 소스 폴더는 그 검사의 목록에 더한다.
- **공용 파일의 자리.** 모두 끝에 덧붙이고 다른 줄의 자리를 고치지 않는다.

| 파일 | 줄 4가 더하는 자리 |
| --- | --- |
| [CMakeLists.txt](../CMakeLists.txt) | 76행(`LUIL_GENERATED_STYLE_KEYS`) 뒤에 `LUIL_STRINGS`·`LUIL_GENERATED_STRINGS`. 446행(`generate_notices.cmake` include) 뒤에 `string_table.cmake` include |
| [src/CMakeLists.txt](../src/CMakeLists.txt) | 55행(키 표 명령) 뒤에 생성 명령, 75~79행 `generated` 묶음 끝, 93~113행 `text` 묶음 끝 |
| [tests/CMakeLists.txt](../tests/CMakeLists.txt) | `luil_core_tests` 목록 끝, 462~464행(`luil_style`) 뒤에 `luil_strings`, 시험 표의 `luil_add_string_table` 한 줄 |
| `cmake/install.cmake`, `cmake/luil-config.cmake.in` | 60~66행 `install(FILES)` 끝 두 파일, `generate_notices.cmake` include 뒤 한 줄 |
| [include/luil/luil.h](../include/luil/luil.h) | `text` 묶음(17~25행)의 글자 차례 자리 |

`CMakePresets.json`은 고치지 않는다. 표는 선택 기능이 아니라 늘 선다. 데모는 라이브러리 커밋과 나눠 연결 커밋으로 내고 통합 때 다른 줄의 데모 변경 뒤에 직렬로 쌓는다.

## 확인

자동 test.

- `tests/string_table_tests.cpp`(새 파일, `luil_core_tests`, Windows와 Android 기기). 시험 표 `tests/strings/en.json`·`ko.json`·`fr.json`을 `luil_add_string_table`로 지어 쓴다(공개 함수를 저장소 안에서 먼저 쓴다). 고르기: `en-US`가 `en`, `zh-Hant-TW`가 `zh-Hant`, `zh` 차례, 대소문자와 `_`, 목록의 둘째 태그, 빈 목록과 모르는 태그는 기본 언어. 조회: 범위 밖 키는 빈 글, 복수형 키의 `text`는 other 꼴, `find`. 채우기: 이름 둘, 같은 이름 두 번, 빠진 인자는 `{이름}` 그대로, `{{`·`}}`, 수 0·음수·`INT64_MIN`. 복수: other 규칙은 늘 other, one 규칙은 1과 -1이 one, 0·2·1000000은 other, 빈 one 꼴은 other로 물러섬.
- `set_text_languages`를 바꾸는 test는 끝에서 되돌리는 guard를 쓴다. 전역이라 다른 test에 새지 않게 한다. 정하지 않은 상태와 빈 목록이 모두 한국어인지 본다.
- `tests/string_table_tests.cmake`(새 파일, `luil_strings`, `cmake -P`, [accent_catalog_tests.cmake](../tests/accent_catalog_tests.cmake) 방식). 막는 원본: 깨진 JSON, 파일 이름과 `language`가 다름, 모르는 `plural`, 언어 사이 키 빠짐과 남음, 꼴이 다름, 범주 빠짐과 남는 범주, 짝 없는 괄호, 밝히지 않은 자리 값 이름, `count` 없는 범주 객체, 예약어 키, 기본 언어 파일 없음. 받는 원본: 따옴표·역슬래시·줄 바꿈·탭이 든 글이 이스케이프된 헤더, 같은 원본이 같은 헤더인지.
- `tests/install_consumer.cmake`: 설치본에서 `luil_add_string_table`이 서고 그 헤더가 컴파일되는지.
- `luil_core_portability`, `luil_umbrella_header`, `luil_source_style`, 전체 CTest.

데모와 사람 확인은 [흡수 문서](i18n-adoption-design.md)의 확인 절이다. 표만으로는 화면에 보이는 것이 없다.

## 범위 밖

- 숫자·날짜·목록 서식, 통화, 상대 시간(범위 C).
- 실행 중 표 읽기(파일, APK assets)와 표 다시 읽기(확정 N6 a).
- 오른쪽에서 왼쪽으로 쓰는 언어의 글 방향과 거울 배치. 화면 전환의 쪽은 화면 전환 노드의 후속이다(흡수 문서).
- 대체 글꼴의 언어를 앱 언어에 맞추기. 대체 글꼴은 OS 언어로 고른다([font_registry.h](../src/host/font_registry.h) 105~115행). 앱 언어가 OS와 다르면 한자 자형이 OS 언어를 따른다.
- 성·격에 따른 꼴, 조사 고르기(이/가). 문장은 자리 값 뒤에 조사가 오지 않게 쓴다.
- 1단계 밖의 복수 규칙(결정 3의 b)과 수 여럿을 섞는 문장.
- 마크업 `{t:key}`(결정 6이 b일 때).

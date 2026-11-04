# 실행 중 스타일 읽기

앱이 스타일 JSON 글을 실행 중에 읽어 `ui_style` 값을 얻는다. 빌드 때 `LUIL_STYLE`로 굽는 파일과 같은 스키마, 같은 판정, 같은 대신 값이다. 같은 파일은 굽든 읽든 같은 `ui_style`이 된다. 키 목록은 데이터 파일 하나([style_keys.cmake](../cmake/style_keys.cmake))에만 있고, 생성기가 그것으로 기본값 헤더와 함께 파서가 쓰는 키 표 헤더를 낸다(G4 a). 파서는 `luil_core`에 있고 nlohmann을 안쪽에서만 쓴다(G5 a). 공개 형식은 [ui_style_json.h](../include/luil/theme/ui_style_json.h)의 `parse_ui_style`·`style_parse_result`·`style_error`·`describe`다. 플랫폼을 모르는 순수 함수이고 파일을 읽지 않는다. 범위는 기존 스키마의 실행 중 로드와 오류 보고다. 선택자·상속은 범위 밖이다.

## 문제

앱이 실행 중에 스타일을 파일에서 바꾸려면 파서를 직접 짠다.

- 실행 중에 스타일을 바꾸는 자리는 이미 있다. 앱이 `ui_style`을 세워 `ui_frame::style`([app_host.h](../include/luil/app/app_host.h) 362~367행)과 `make_arranged_tree`의 `style`에 싣는다. 호스트는 `frame->style`을 `frame_state.style`로 옮기고(`win32_window.cpp` 2046행, `android_app.cpp` 1690행) `frame_style`·`frame_palette`([frame_state.cpp](../src/host/frame_state.cpp) 16~33행)가 읽는다. 없는 것은 JSON 글을 그 값으로 바꾸는 길이다.
- 스키마 규칙이 모두 [generate_style.cmake](../cmake/generate_style.cmake) 안에 있다. 키와 필드의 짝(70~127행), 빠져도 되는 중립 역할 아홉과 대신할 키(`neutral_optional_fields`, 101~111행), 허용 값 목록 일곱 묶음(220~239행), 컴포넌트 칸(242~245행), 뿌리 키(310행)다. 앱이 손으로 파서를 쓰면 값 95개(테마마다 34, `tones` 10, `metrics` 10, `tabBar` 5, `popup` 1, `text` 1)와 이 규칙을 통째로 다시 적고, 키가 늘 때마다 어긋난다.
- 앱에는 JSON 파서도 마땅치 않다. nlohmann은 `luil` target에만 이어지고(`src/CMakeLists.txt` 634~639행) `theme/`가 사는 `luil_core`에는 없다(599~610행). core test(`luil_core_tests`)는 Android 기기에서도 돌지만 nlohmann을 쓰는 core 코드는 없다.
- R1 [구문 강조](syntax-highlight-design.md) 223행이 "표를 데이터로 빼면 여섯 행이 그대로 옮겨 가고 실행 중 로더는 같은 대신 값을 읽어야 한다"고, [줄 나눔](line-break-design.md) 137·164행이 "G4가 정해지면 `text` 절에 정책 키를 더한다"고 미뤄 두었다. 이 문서가 앞의 것을 닫는다(결정 1). 뒤의 것은 결정 5(길 b)대로 [줄 나눔 정책 스타일 키](wrap-policy-key-design.md)가 이 문서의 키 표 위에서 닫는다.

## 공개 형식

```cpp
// include/luil/theme/ui_style_json.h (ui_style.h를 include한다. nlohmann 타입을 내지 않는다)
enum class style_error_kind
{
    syntax,         // JSON 문법이 깨졌다. `offset`이 바이트 자리다
    not_object,     // 뿌리나 칸(`dark`, `dark.caption`, `tabBar` …)이 객체가 아니다
    missing_key,
    unknown_key,
    duplicate_key,  // 한 객체에 같은 키가 두 번이다
    wrong_type,     // 숫자 자리에 글, 이름 자리에 숫자
    invalid_color,  // `#rrggbb`·`#rrggbbaa`가 아니다
    out_of_range,
    invalid_value,  // 허용 값 목록에 없는 이름
    too_deep,       // 중첩이 8단을 넘는다. 파싱 전에 막는다
};

struct style_error
{
    style_error_kind kind { style_error_kind::syntax };
    std::u8string path {};    // 오류가 난 객체의 자리다. 생성기와 같은 글이다("dark", "dark.caption", "metrics.fontWeights"). 뿌리는 "root"다
    std::u8string key {};     // 문제의 키다. 키가 없는 오류(not_object, syntax)는 비어 있다
    std::u8string value {};   // 받은 값의 글이다(색·숫자·이름). 64바이트에서 자른다
    std::size_t offset { 0 }; // syntax일 때만 쓴다
    [[nodiscard]] bool operator==(const style_error&) const = default;
};

// 생성기의 오류 글과 같은 한 줄이다("Unknown key in dark: windowBackgrund"). 개발자용 영문 진단이다.
[[nodiscard]] std::u8string describe(const style_error& error);

struct style_parse_result
{
    std::optional<ui_style> style {};   // 오류가 하나라도 있으면 비어 있다
    std::vector<style_error> errors {}; // style이 있으면 비어 있다
    [[nodiscard]] bool ok() const noexcept { return style.has_value(); }
};

// 스타일 JSON 글(UTF-8)을 읽는다. 파일을 읽지 않는다. 전역 상태가 없어 어느 thread에서나 부른다.
[[nodiscard]] style_parse_result parse_ui_style(std::u8string_view json);
```

```cpp
// 앱의 logic thread. 실패하면 쥔 포인터를 바꾸지 않는다.
auto parsed { luil::parse_ui_style(text) };
if (parsed.ok())
    style_ = std::make_shared<const luil::ui_style>(std::move(*parsed.style));
else
    report(parsed.errors);  // describe(error)로 한 줄씩
frame->style = style_;                                                             // 그리기·popup 모서리·한 줄 도우미
frame->tree = make_tree(luil::make_arranged_tree(std::move(root), slot, scale, {}, nullptr, style_.get()));  // 배치
```

키 표는 데이터 파일과 그것에서 나는 비공개 헤더다. 데이터 파일은 지금 생성기 안의 `set()` 목록을 글자 그대로 옮긴 것이다.

```cmake
# cmake/style_keys.cmake: set()만 있다. 생성기와 style_tests.cmake가 include한다.
set(neutral_fields
    "windowBackground|window_background"
    "surfaceBackground|surface_background"
    ...)
set(neutral_optional_fields
    "raisedBackground|raised_background|noticeBackground"
    ...
    "syntaxFunction|syntax_function|primaryForeground")
set(tone_range 0 1)
set(metric_range 0 1000)
set(root_required_keys dark light tones metrics)
```

```cpp
// <생성 디렉터리>/private/luil/generated/ui_style_keys.h: 생성 파일이다. ui_style_json.cpp와 그 시험만 include한다. 설치하지 않는다.
namespace luil::generated::style_keys {
    struct color_row { std::string_view key; ui_color neutral_color_palette::* field; ui_color neutral_color_palette::* fallback; };
    inline constexpr color_row neutral[] {
        { "windowBackground", &neutral_color_palette::window_background, nullptr },
        { "raisedBackground", &neutral_color_palette::raised_background, &neutral_color_palette::notice_background },
        // … 28행. caption(6행)·tones(10행, 0~1)·metrics(4행, 0~1000)도 같은 꼴의 표다
    };
    template <class Enum> struct enum_name { std::string_view name; Enum value; };
    inline constexpr enum_name<tab_close_visibility> tab_close_visibility_names[] {
        { "always", tab_close_visibility::always }, { "selectedOrHovered", tab_close_visibility::selected_or_hovered } };
    template <class Section, class Enum> struct enum_row { std::string_view key; Enum Section::* field; std::span<const enum_name<Enum>> names; };
    // 컴포넌트 칸은 (JSON 키, ui_style의 칸, 행 묶음)이다. 행마다 enum 형이 달라 tuple이다.
    //  - 집합체의 형 추론(CTAD)에 기대지 않으려고 만드는 함수(make_component·make_enum_row)로 짓는다. 칸의 형은
    //    `decltype(ui_style::tab_bar)` 별칭이라 생성기가 struct 이름을 알 필요가 없다.
    using tab_bar_section = decltype(ui_style::tab_bar);
    inline constexpr std::tuple components {
        make_component("tabBar", &ui_style::tab_bar
            , make_enum_row("variant", &tab_bar_section::variant, tab_bar_variant_names) /* … */),
        make_component("popup", &ui_style::popup /* … */), make_component("text", &ui_style::text /* … */) };
}
```

## 규칙

**같은 파일은 같은 값이다.** 파서의 판정은 생성기와 같다. 받는 파일과 막는 파일이 같고, 받은 파일의 값은 `LUIL_STYLE`로 그 파일을 구웠을 때의 `default_style`과 `operator==`로 같다.

- 뿌리에는 `dark`·`light`·`tones`·`metrics`가 꼭 있고 `tabBar`·`popup`·`text`는 빠져도 된다. 그 밖의 키는 막는다. 키 컬러와 테마 선호는 스타일이 아니라 사용자 설정이라(`ui_style.h` 173~175행) `accent` 같은 키도 낯선 키다.
- `dark`·`light`는 `neutral_fields` 19개와 `caption` 객체(6개)가 꼭 있다. `neutral_optional_fields` 아홉은 빠지면 **같은 테마**의 대신할 키 값을 쓴다. 구문 색 여섯은 `primaryForeground`다. 대신할 키는 늘 꼭 있는 키여야 하고 생성기가 그것을 검사한다.
- 색은 `#rrggbb`·`#rrggbbaa`(대소문자 무관)이고 `0xAARRGGBB`가 된다. 숫자는 JSON 숫자여야 하고(글 `"0.5"`는 막는다) `tone_range`·`metric_range` 안이다. 숫자는 **값으로** 본다(결정 8). CMake는 읽은 숫자를 다시 적어 생성기에 넘기므로(정수는 그대로, 실수는 `%.17g`) 지수 표기(`1e1` → `10.0`)는 두 쪽이 받고, 생성기 정규식에 지수 꼴을 더해 아주 작은 실수(`0.00001` → `1.0000000000000001e-05`)도 받는다. 음수는 범위 밖이다. `-0` 정수는 두 쪽 모두 0으로 와서 받고, `-0.0` 실수는 생성기가 부호 붙은 글로 받아 막고 파서는 `signbit`으로 막는다. 셋 다 사례 표에 있다.
- 컴포넌트 칸과 `metrics.fontWeights`는 빠지면 **struct의 기본값**이다. 칸 안에서 빠진 키도 그 필드의 struct 기본값이다. `default_ui_style()`(이 빌드가 구운 값)이 아니다. 생성기가 그렇게 하므로, `LUIL_STYLE`로 `tabBar.variant`를 `connected`로 구운 앱이 `tabBar` 없는 파일을 읽으면 `underline`이 된다. 앱이 내장값 위에 고쳐 쓰려면 파일에 그 칸을 적는다.
- 값은 enum 이름(camelCase)과 `palette_role` 이름이고 허용 목록 밖은 막는다. 이름과 C++ 값의 짝(`selectedOrHovered` → `selected_or_hovered`)은 생성기가 헤더를 낼 때 짓는다. 실행 중 snake_case 변환은 없다.

**전체를 받거나 전체를 막는다.** 오류가 하나라도 있으면 `style`이 비고 쥔 스타일은 부르는 쪽이 그대로 둔다(결정 3). 오류는 끝까지 모은다. 문법 오류와 `too_deep`은 그 하나만 남기고 멈춘다(앞서 센 겹친 키도 지운다). 순서는 키 표 순서(뿌리, dark, light, tones, metrics, fontWeights, 컴포넌트 칸)이고, 한 객체 안에서는 낯선 키(JSON에 적힌 순서), 빠진 키, 값 오류 순이다. 생성기도 같은 순서로 보고 첫 오류에서 멈춘다(생성기의 낯선 키는 CMake가 정렬한 키 순서다). nlohmann 예외는 `ui_style_json.cpp` 안에서 값으로 바뀐다. 예외를 값으로 바꾸는 자리는 `http_body.cpp`(192행)와 여기 둘이다. 같은 키가 두 번이면 막는다(nlohmann은 뒤의 것을 남겨 조용히 지나간다).

**엄한 쪽은 파서다.** 구현 때 재 보니 CMake의 `string(JSON)`(jsoncpp)은 겹친 키를 뒤의 것으로 남겨 조용히 지나가고, 그 밖에도 주석(`//`·`/* */`), 끝 쉼표, 뿌리 뒤의 글, `+1`, 앞자리 0(`01`)을 받는다. 생성기에 같은 검사를 더하려면 CMake로 JSON 토큰을 다시 읽어야 해서 더하지 않았다. 이 묶음(겹친 키와 jsoncpp의 너그러운 문법)이 "같은 판정"의 **유일한 예외**다. 파서는 RFC 8259 그대로 막고, 생성기가 받는 표준 JSON 파일은 파서도 받는다. 이 묶음은 사례 표가 아니라 C++ 시험에만 있다(사례 표의 고침은 두 쪽이 다시 적어 너그러운 글이 남지 않는다).

**키 표가 하나다.**

- 생성기는 [style_keys.cmake](../cmake/style_keys.cmake)를 include해 두 헤더를 낸다. 기본값 헤더(`luil/generated/ui_style.h`)는 지금과 같은 지정 초기화이고, 키 표 헤더는 위의 꼴이다. `add_custom_command`가 둘이다(결정 11). 기본값 헤더는 `LUIL_STYLE`·생성기·데이터 파일에, 키 표 헤더(`-DKEYS_OUTPUT_FILE`)는 생성기·데이터 파일에만 기댄다. 그래서 앱이 스타일 파일을 바꿔도 키 표와 파서는 다시 돌지 않는다. 생성기는 `-DKEYS_FILE`로 다른 데이터 파일을 받는다(시험이 대신할 키가 잘못된 표를 넣는다).
- 행 순서는 struct 선언 순서다. 기본값 헤더가 지정 초기화라 순서가 어긋나면 컴파일이 막힌다(MSVC는 오류, clang은 `-Wreorder-init-list`가 저장소의 `-Werror`로 오류다). 행이 **빠진** 것은 컴파일이 잡지 못한다. 그 필드는 조용히 0이 된다. 그래서 core test가 표의 필드 자리를 잰다(확인).
- 허용 값이 모두 헤더에 C++ 값으로 적히므로 목록의 오타(`palette_roles`에 없는 역할)가 생성 때가 아니라 컴파일 때 드러난다. 지금은 쓰인 값만 적혀 오타가 숨는다.
- 새 중립 역할은 struct 필드와 데이터 파일 한 행, `assets/style.json` 한 줄이다. 새 컴포넌트 칸은 [탭 막대 스타일](tab-bar-style-design.md) 47행의 모양에 데이터 파일의 `component_sections`·필드 묶음 한 벌이고, 파서는 tuple을 돌아 손대지 않는다. 섹션의 새 **꼴**(새 중첩 객체 같은 것)만 생성기와 파서를 함께 고친다.
- `compose_palette`·`high_contrast_palette_for`의 구문 색 여섯 줄은 손으로 쓰는 합성이라 표 밖이다(`// 구문 색` 묶음 그대로).

**두 자리에 같은 값을 준다.** 읽은 스타일은 `ui_frame::style`(팔레트·popup 모서리·한 줄 줄바꿈)과 tree마다의 `make_arranged_tree(..., style)`(치수·컴포넌트 칸)에 같은 포인터로 준다. 주 창 tree뿐 아니라 앱이 따로 배치하는 popup·보조 창 tree도 같다. 한쪽만 주면 잰 폭과 그린 글이 어긋난다([탭 막대 스타일](tab-bar-style-design.md) 25행). `text.lineBreak`는 호스트가 `ui_frame::style`에서 읽어 `set_single_line_break`에 건다(`win32_window.cpp` 2048행, `android_app.cpp` 1692행).

**고대비가 세다.** 읽은 스타일도 고대비에서는 쓰이지 않는다. `color_palette_for`와 `frame_palette`가 고대비면 스타일을 보지 않는다(`ui_style.cpp` 44행, `frame_state.cpp` 25행). 파서는 이 규칙에 닿지 않는다. 치수(`metrics`)와 컴포넌트 칸은 고대비에서도 앱의 값이다. 지금과 같다.

배율·초점·접근성·움직임 줄이기는 닿지 않는다. 치수는 논리 픽셀 그대로이고 배율은 그리기가 곱한다.

## 결정

**1. 키 표 데이터 파일의 꼴.** 질문: 키 목록을 어떤 파일로 빼는가.

- 길 a: `cmake/style_keys.cmake`. 지금의 `set()` 목록을 글자 그대로 옮기고 생성기와 `style_tests.cmake`가 include한다. 되는 것: 생성기의 반복문이 그대로이고 diff가 목록 이동뿐이다. 구문 여섯 행이 바이트까지 같게 옮겨 간다. 대가: CMake 문법이라 다른 도구가 읽으려면 CMake를 거친다.
- 길 b: `assets/style_keys.json`. 되는 것: 구조가 있는 데이터이고 다른 도구도 읽는다. 대가: 행마다 `string(JSON)` 호출로 생성기 반복문을 다시 쓴다. C++는 어차피 생성 헤더를 읽으므로 JSON의 이점이 쓰이지 않는다.
- 길 c: 목록을 생성기 안에 두고 헤더만 하나 더 낸다. 되는 것: 파일이 늘지 않는다. 대가: `style_tests.cmake`가 키 목록을 볼 길이 없어 시험이 키를 다시 적는다.
- 권장: a. 대가: 위의 CMake 문법이다. 권장대로 진행.

**2. 키 표 헤더의 꼴과 자리.** 질문: 파서가 쓰는 표를 어떤 C++로 내고 어디에 두는가.

- 길 a: 구간별 member pointer 표와 enum 이름 짝(위의 꼴). 파서는 손으로 쓰고 표를 돈다. 되는 것: 형이 맞지 않는 행은 컴파일이 막는다. 표가 데이터라 test가 표를 직접 잰다. 대가: 섹션의 꼴(테마, 숫자 묶음, enum 칸)은 생성기와 파서가 따로 안다.
- 길 b: 생성기가 파서 함수 자체를 C++로 낸다. 되는 것: 섹션 꼴까지 한 곳이다. 대가: 오류 수집·nlohmann 순회 코드를 CMake 문자열로 짓게 되어 읽기와 고치기가 가장 어렵고, 형 오류가 생성 파일 안에서 난다.
- 길 c: 오프셋·종류의 평면 표(`offsetof`와 형 태그). 되는 것: 표가 한 배열이다. 대가: 형 검사를 잃고 `reinterpret_cast`로 쓴다.
- 자리는 설치되지 않는 생성 디렉터리(`private/`)로 `luil_core` PRIVATE include에만 둔다. 지금의 생성 헤더 자리는 `cmake/install.cmake`가 통째로 설치해 내부 표가 공개 면처럼 보이게 된다. umbrella 검사(`verify_umbrella_header.cmake`)는 소스 `include/`만 보므로 어느 자리든 걸리지 않는다.
- 권장: a와 비공개 자리. 대가: 섹션 꼴이 두 벌이다. 꼴이 바뀌는 일은 드물고 사례 표가 잡는다. 권장대로 진행.

**3. 오류 정책.** 질문: 잘못된 글을 받으면 무엇을 돌려주는가.

- 길 a: 전체 거부와 오류 모두 모으기. `style`이 비고 부르는 쪽이 쥔 값을 둔다. 되는 것: 생성기와 받는 파일이 같다. 고칠 곳을 한 번에 다 본다(hot_reload의 편집 고리). 대가: 생성기는 첫 오류에서 멈추므로 오류 목록의 일치는 "생성기의 첫 오류가 목록에 있다"로만 잰다.
- 길 b: 첫 오류에서 멈춤. 되는 것: 생성기와 오류까지 같다. 대가: 오타 셋을 고치는 데 세 번 읽는다.
- 길 c: 부분 적용. 바탕 스타일 위에 맞는 키만 얹는다. 되는 것: 오타 하나로 화면 전체가 이전에 머물지 않는다. 대가: 생성기가 막는 파일을 실행 중에는 받아 "같은 파일은 같은 값"이 깨지고, 반쯤 적용된 색이 섞여 대비가 무너질 수 있다. API에 바탕 인자가 는다.
- 권장: a. 대가: 위의 느슨한 일치 판정이다. 권장대로 진행.

**4. 엄격함.** 질문: 낯선 키와 빠진 키를 생성기만큼 엄하게 보는가.

- 길 a: 생성기와 같다(위의 규칙). 되는 것: 오타가 "바꿨는데 안 바뀐다"로 숨지 않는다. 대가: 새 판 라이브러리용 파일(새 키가 든 것)을 옛 판 앱이 읽으면 막는다.
- 길 b: 낯선 키는 경고로 받고 건너뛴다. 되는 것: 앞 판 호환. 대가: 오타가 경고로만 남고 생성기와 받는 파일이 달라진다.
- 길 c: 빠진 꼭 있는 키를 내장 스타일로 채운다(덮어쓰기 파일). 되는 것: 몇 줄짜리 파일로 바꾼다. 대가: 같은 파일이 빌드 때는 막히고 실행 중에는 받는다. 원하는 앱은 JSON을 미리 합쳐 넘기면 된다.
- 권장: a. 대가: 위의 앞 판 비호환이다. 생성기의 선택 키 대신 값이 뒤 판 호환(옛 파일을 새 판이 받음)을 이미 맡는다. 권장대로 진행.

**5. 줄 나눔 정책의 스타일 키.** 질문: uax14가 미룬 앱 전체 기본 정책 키(`text` 절의 `single_line_break` 옆)를 이번에 더하는가.

- 사실 고침: [줄 나눔](line-break-design.md) 136행의 "배치 문맥은 `ui_metrics`만 나른다"는 반만 맞다. `arrange_context::style`은 있다(`ui_element.h` 699행). 그러나 `preferred_size`가 받는 `measure_context`(649~653행)와 `arrange_context::measure()`가 `style`을 버리고, `draw_context`도 `metrics`만 든다(773행). label·markdown·여러 줄 칸·`text_block_style`은 `line_break`를 구체 값(`hangul_words`)으로 들어 "적지 않음"을 나타낼 값이 없다.
- 길 a: 이번에도 두지 않는다. 되는 것: 공개 config와 `ui_element.h` 문맥을 건드리지 않아 이 노드가 스타일 파일 안에 머문다. 키 표가 한 곳이 되어 나중의 추가가 struct 필드와 한 행이다. 대가: 앱 전체를 `unicode`로 바꾸려는 앱은 컨트롤마다 적는다. G4의 근거 넷째가 이번 라운드에 쓰이지 않는다.
- 길 b: 지금 더하고 잇는다. `text_style::wrap_policy`(JSON `text.wrapPolicy`, `hangulWords`·`unicode`), 세 config(`label_config`·`markdown_config`·`text_area_config`)와 `text_block_style`의 `line_break`를 `std::optional`로(탭 막대 칸의 선례), `measure_context`·`draw_context`에 스타일을 싣는다. 되는 것: 앱 전체 기본을 파일에서 고른다. 대가: 공개 구조체 넷과 문맥 둘이 바뀌어 읽는 코드(`config.line_break == …`)가 깨지고, `measure_context`를 손으로 짓는 열 곳(test·데모)을 고친다. rich_edit(R3)와 약속한 "정책은 `text_layout_request::line_break`로만" 경계는 그대로이지만 그 앞단이 넓게 바뀐다.
- 길 c: 키와 필드만 두고 잇기는 나중. 대가: 읽어도 아무 일도 하지 않는 키라 "바꿨는데 안 바뀐다"를 라이브러리가 만든다.
- 권장: a. 다시 미루는 까닭은 위의 대가가 스타일 읽기와 무관한 배치·그리기 문맥 변경이기 때문이다. 앱 전체 기본이 필요한 첫 소비자(같은 줄 R4 markup이 서식 글의 기본을 앱에서 받을 때 등)가 서면 길 b를 그 노드에서 한다. 키 이름과 값은 위대로 예약한다. **승인: 길 b (2026-11-02, 권장 a와 다름). 구현은 [docs/wrap-policy-key-design.md](wrap-policy-key-design.md).** 이 노드는 키 표 데이터 파일·생성기·실행 중 파서·시험까지 하고 `text.wrapPolicy` 키는 더하지 않는다. 그 노드는 데이터 파일의 `text_fields`에 한 행을 더하고 생성기·파서는 고치지 않는다. 그 노드가 그대로 더했다(공유 사례 표 45개에서 48개).

**6. Android 앱 묶음 파일.** 질문: APK assets를 읽는 길을 이 노드가 더하는가.

- 길 a: 범위 밖. 파서는 글만 받고 파일은 부르는 쪽이 읽는다. 되는 것: Android 전용 작업이 없다. 앱 전용 폴더의 파일은 표준 파일 입출력으로 읽는다. 대가: APK assets에 넣은 스타일은 앱이 `AAssetManager`(NDK C API)로 직접 읽는다. luil은 그 관리자를 앱에 내지 않는다.
- 길 b: 플랫폼 도구에 묶음 파일 읽기를 더한다(`GameActivity::assetManager`, GameActivity.h 135행). 되는 것: 데모와 앱이 한 줄로 읽는다. 대가: `platform_services.h`는 같은 라운드 줄 4(인자 넘기기)가 고치는 파일이고, Win32 쪽 짝(실행 파일 옆 자리)의 뜻을 따로 정해야 한다.
- 권장: a. hot_reload(R5)가 감시와 함께 다시 본다. 권장대로 진행.

**7. 두 시험을 맞추는 법.** 질문: 생성기 시험과 C++ 파서 시험이 같은 입력에 같은 판정을 내는지 어떻게 잠그는가.

- 길 a: 공유 사례 표 `tests/style_cases.json`. 사례마다 내장 파일에 가할 고침(`set`·`remove`와 JSON 경로), 받음 여부, 막을 때 생성기의 첫 오류(`kind`, `path`, `key`)를 적는다. `style_tests.cmake`가 `string(JSON SET|REMOVE)`로 고쳐 생성기를 돌리고, C++ 시험이 nlohmann으로 같은 고침을 가해 파서를 돌린다. 값 단언은 각자 한다(생성기는 헤더의 글, 파서는 필드). 되는 것: 사례가 한 벌이라 한쪽에만 더한 사례가 생기지 않는다. 대가: CMake 시험이 사례 표를 도는 틀(약 60줄)을 새로 진다.
- 길 b: C++ 시험이 같은 고침을 손으로 옮기고 주석으로 짝짓는다. 되는 것: 틀이 없다. 대가: 지금 사례 40여 개가 두 벌이 되고 어긋나도 아무것도 실패하지 않는다.
- 권장: a. 지금 `style_tests.cmake`의 받음·막음 사례를 표로 옮기고 내장 파일의 글 단언(26~49행)은 그대로 둔다. 대가: 위의 틀이다. 권장대로 진행.
- 구현한 행의 꼴: `{ "name", "text"(파일 대신 쓸 글) 또는 "edits": [{ "op": "set"|"remove", "path": [...], "value": "<JSON 글>" }], "accept", "error": { "kind", "path", "key", "message" }, "expect": [{ "path", "color"|"number"|"name"|"default": true }] }`. 막는 사례의 `message`는 생성기의 오류 글이다. 생성기 시험은 그 글이 생성기 출력에 있는지(줄 접기를 펴서) 보고, C++ 시험은 `describe`가 같은 글인지 본다(결정 9). `expect`의 경로는 두 쪽이 데이터 파일과 키 표로 필드를 찾고, `default`는 생성기에서는 필드가 적히지 않았는지, 파서에서는 struct 기본값인지다. 사례는 45개다.

아래 결정 8~12는 구현하며 정했다(문서에 없던 것).

**8. 숫자를 보는 법.** 질문: 생성기는 CMake가 다시 적은 숫자 글을 정규식으로 보는데, 파서는 무엇을 보는가.

- 길 a(고름): 값으로 보고 부호를 따로 본다(`signbit`). 생성기 정규식에 지수 꼴(`e[-+]숫자`)을 더한다. 되는 것: `1e1`·`2.5e-1`·`-0`·`0.00001`·`-0.0`에서 두 쪽이 같은 판정이다. 대가: 생성기의 범위 비교가 지수 글을 읽어야 한다(CMake의 `if(LESS)`는 읽는다, 재 보았다).
- 길 b: 파서가 SAX의 원래 글(lexeme)을 정규식으로 본다. 대가: 생성기는 원래 글이 아니라 다시 적은 글을 보므로 `1e2`(생성기는 `100.0`으로 받는다)에서 어긋난다.
- 정규식을 넓히기 전 생성기는 0.0001보다 작은 양수를 지수 글로 받아 막았다. 파서와 맞추려고 받는 쪽으로 바꿨다. 앱이 이미 가진 파일 중 막히던 것이 받히게 될 뿐 받던 것이 막히지 않는다.

**9. 오류 글을 두 쪽에서 글자까지 같게.** 질문: `describe`가 "생성기의 글과 같은" 한 줄이려면 무엇을 맞추는가.

- 고름: 생성기의 글을 종류마다 한 꼴로 고르고(`describe`의 꼴과 같다) 사례 표의 `message`가 두 쪽을 잰다. 생성기에 형 검사(`Expected a string at dark.windowBackground, got NUMBER`, 전에는 "잘못된 색"이나 "잘못된 값"으로 보였다)와 칸이 객체인지의 검사(`dark must be an object, got ARRAY`, 전에는 CMake의 낯선 오류였다)를 더하고, 허용 값 목록을 `, `로 잇고(전에는 CMake 리스트의 `;`), 뿌리가 객체가 아니면 `root must be an object, got ARRAY`, JSON이 깨졌으면 `The style file is not valid JSON: …`이다. 받는 파일과 막는 파일은 바뀌지 않는다(결정 8의 작은 양수만 받는 쪽으로 바뀐다).
- `style_error::value`는 wrong_type·not_object에서 받은 값의 JSON 형 이름(`STRING`·`NUMBER`·`OBJECT`·`ARRAY`·`BOOLEAN`·`NULL`, CMake의 이름)이다. 숫자 값은 생성기가 보는 글(정수 그대로, 실수 유효 17자리와 `.0`)로 적어 `out_of_range`의 글이 같다.
- syntax·duplicate_key·too_deep은 생성기에 짝이 없는 글이다(`Invalid JSON at byte N`, `Duplicate key in dark: windowBackground`, `JSON nested deeper than 8 levels`).

**10. 파서의 모양.** 질문: nlohmann로 겹친 키·깊이·문법 자리를 어떻게 얻는가.

- 고름: SAX로 한 번 훑어 문법 오류(자리와 함께), 8단 넘는 중첩, 겹친 키를 보고, 그 뒤 `ordered_json`으로 값을 짓는다. 값 객체는 낯선 키를 JSON 순서로 알리려고 키 순서를 지키는 `ordered_json`이다(기본 `json`은 키를 정렬한다). SAX는 문법 오류를 콜백으로 알려 예외 없이 끝난다. 깊이는 SAX가 그 자리에서 멈춰 값을 짓기 전에 막는다(`http_body.cpp`의 미리 세기와 같은 뜻이고, net 층의 함수는 core에서 부르지 않는다).
- `offset`은 0부터 센 바이트 자리다(nlohmann의 1부터 센 `byte`에서 1을 뺀다). 글이 도중에 끝났으면 글 길이다.
- 다른 길: 문법만 nlohmann에 맡기고 겹친 키를 놓친다(설계의 판정을 어긴다), 자기 토큰 읽기(G5 a를 어긴다).

**11. 생성 명령 둘.** 질문: 두 헤더를 한 `add_custom_command`의 두 OUTPUT으로 내는가.

- 고름: 명령 둘. 키 표 헤더는 데이터 파일·생성기에만 기대고 늘 다시 쓴다. 한 명령에 두면 LUIL_STYLE만 바뀌어도 키 표가 다시 쓰이고(파서가 다시 컴파일된다), 글이 같다고 건너뛰면 출력이 입력보다 낡아 보여 빌드마다 명령이 다시 돈다.

**12. 데모의 묶음.** 질문: 데스크톱·모바일 데모의 견본과 상태 줄을 어디에 두는가.

- 고름: 견본 짓기·적용·상태 줄을 `examples/demo/style_sheet_sample.h` 하나에 두고 두 데모와 page test가 함께 쓴다(`line_break_sample.h`의 선례). 내장 파일 글은 데모 빌드가 `LUIL_STYLE`을 `embed_binary.cmake`로 실어 페이지 생성자에 넘긴다. 페이지가 기호를 직접 부르지 않아 page test가 시험 실행 파일에 실은 글을 넘긴다.
- 견본 "창 바탕 바꿈"은 어두운 창 바탕(`#1a1b26`), 밝은 창 바탕(`#e8eaf2`), `controlCornerRadius`(8)를 바꾼다. 상태 줄은 어두운 창 바탕만 말한다. "낯선 키"는 `dark` 첫 줄에 `windowBackgrund`를 **더해** 오류가 하나다(이름을 바꾸면 빠진 키까지 둘이다).

## 플랫폼

**Win32.** `src/win32`를 고치지 않는다. 호스트는 이미 `frame->style`을 옮기고 한 줄 줄바꿈을 건다(`win32_window.cpp` 2046~2048행). 고대비는 시스템 색 그대로다.

**Android.** Java 소스는 0줄이고 JNI도 부르지 않는다. 파서는 core라 그대로 돌고 `luil_core_tests`가 기기에서 돈다. 호스트 쪽도 같은 자리가 있다(`android_app.cpp` 1690~1692행). 앱 묶음 파일 읽기는 범위 밖이다(결정 6). unsupported인 부분은 없다.

**빌드(G5 a).** `luil_core`에 `PRIVATE "$<BUILD_INTERFACE:luil_nlohmann_json>"`를 `luil_options` 옆에 더한다. `luil_core`는 정적 라이브러리라 PRIVATE 의존도 export의 링크 목록에 남으므로 감싸야 `install(EXPORT)`가 "export set에 없는 target"으로 서지 않는다(`src/CMakeLists.txt` 640~642행 주석의 웹뷰 SDK와 같은 까닭). 설치본은 이미 nlohmann 헤더를 include 루트에 복사하고(`cmake/install.cmake` 26행) 이식성 검사는 플랫폼 헤더와 `win32`·`net` 경로만 막아(`verify_core_portability.cmake` 27~30행) 둘 다 고치지 않는다. `luil_core_tests`에 nlohmann(BUILD_INTERFACE)과 비공개 생성 include(`<생성 디렉터리>/private`)를 PRIVATE로 잇는다. 공개 헤더는 그대로다(nlohmann 형이 없다). 시험은 사례 표를 읽고 고치며 키 표를 직접 잰다. 내장 파일(`LUIL_STYLE`), 사례의 바탕(`assets/style.json`), 사례 표는 `embed_binary.cmake`로 시험 실행 파일에 싣는다. 문서는 [빌드 체계](concepts/build-system.md) 16행과 [nlohmann_json.cmake](../cmake/dependencies/nlohmann_json.cmake) 머리 주석(지금은 HTTP 본문만 적는다)을 고친다.

## 다른 노드와의 약속

- **hot_reload(R5, 같은 줄)**: 이 파서와 [파일 감시](file-watch-design.md)(234행)를 잇는다. 약속은 다섯이다. (1) `parse_ui_style`은 순수 함수라 감시 thread에서 불러도 되고 결과만 `post_app_message`로 넘긴다. (2) 파일 하나가 스타일 하나다. 다시 읽기는 파일 전체를 읽어 다시 파싱하는 것이고 합치기나 증분이 없다. (3) 실패하면 `style`이 비고 쥔 포인터를 바꾸지 않는다. 화면은 이전 스타일로 남는다. (4) 반쯤 쓴 파일은 `syntax`로 막힌다. 쓰는 쪽은 `replace_file_atomically`를 쓴다. (5) 오류는 `path`·`key`와 `describe` 한 줄이라 화면 띠나 로그에 그대로 쓴다. 성공하면 새 `shared_ptr`을 두 자리에 준다.
- **markup(R4, 같은 줄)**: core의 nlohmann 링크를 이 노드가 세우고 markup은 소스만 더한다. 오류는 같은 꼴(종류 enum, 점으로 이은 자리, `describe`의 영문 한 줄)을 따르면 좋지만 형은 따로다. 결정 5의 길 b가 markup에서 필요해지면 그 노드가 진다.
- **syntax(R1, 같은 줄)**: 223행의 셋을 지킨다. 구문 여섯 행은 데이터 파일에서도 연속한 여섯 행이고 struct 선언 순서와 사례 표의 빠진 키 사례가 그대로 옮겨 간다. 실행 중 로더는 같은 대신 값(`primaryForeground`)을 표에서 읽는다. 이 약속은 이 문서로 닫는다.
- **uax14(R1, 같은 줄)**: 결정 5는 길 b다. [줄 나눔](line-break-design.md) 137행(결정 3의 "G4가 정해지면 스타일 키가 config 기본값을 채우는 길을 그때 더한다")과 164행(style_sheet 약속)의 미룸은 [줄 나눔 정책 스타일 키](wrap-policy-key-design.md)가 닫는다. 이 노드는 그 문서가 기댈 키 표·생성기·파서를 세운다. line-break-design.md는 이 노드가 고치지 않는다.
- **같은 라운드 다른 줄**: 이번 라운드에 `generate_style.cmake`·`assets/style.json`·`ui_style.h`·`style_tests.cmake`는 이 노드만 고친다. 줄 1의 msg_box가 심각도 바탕 톤을 더하게 되면 이 구조 변경이 들어간 뒤 데이터 파일에 한 행을 더하고 생성기는 고치지 않는다. inspector는 draw_context::palette만 읽어 이 노드의 파일과 겹치지 않는다. 앞으로 info_bar·color_picker가 역할을 더하면 위의 "새 중립 역할" 규칙이다. chart는 core 팔레트에 역할을 더하지 않는다([차트](chart-design.md) 결정 E2).
- **같은 줄 앞 노드(undo_stack)**: 목록 파일 셋에 줄을 더하는 것만 겹친다. `src/CMakeLists.txt`(이 노드는 `theme` 필터의 소스 줄, `luil_core` 링크 한 줄, 생성 명령), `tests/CMakeLists.txt`, `include/luil/luil.h`다. 모두 기계적 병합이다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(데이터 파일, 생성기, 헤더, 파서, test, 문서)을 먼저 내고 데모 연결 커밋은 통합 때 직렬로 쌓는다. 고친 데모 파일은 `examples/demo/theme_page.*`·`style_sheet_sample.h`(새 파일, 두 데모가 함께 쓴다)·`common.h`(kind 66·target 16. 같은 라운드 msg_box가 65를 먼저 써서 통합 때 66으로 옮겼다)·`text_targets.h`, `examples/demo_main.cpp`, `examples/CMakeLists.txt`(내장 파일 싣기)와 `examples/mobile_demo/theme_page.*`·`app.*`·`assets.h`·`common.h`(kind 46. 같은 까닭으로 45에서 옮겼다)·`mobile_demo.cmake`, 그 데모를 읽는 `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`·`tests/CMakeLists.txt`의 한 줄(모바일 테마 페이지)이다.
  - 설계와 달라진 점: 데스크톱 데모의 주 tree는 `make_arranged_tree`가 아니라 `root_element`에 자식을 손으로 배치한다(`common.cpp`의 배치는 검색 popup의 것이다). 그래서 "배치에 스타일을 넘기는 한 줄"이 아니라 `make_frame`에서 캡션과 본문(셸)의 `arrange_context`에 `.style`을 싣는다. 모바일은 앱 바와 본문이다. 데모가 따로 짓는 popup·보조 창 tree(명령 팔레트, 검색 메뉴, 배경 효과 견본 창)에는 싣지 않았다. 견본이 글 크기와 컴포넌트 칸을 바꾸지 않아 그 tree의 배치가 달라지지 않는다.
- 구현 때 함께 고치는 문서: [테마](concepts/theming.md)의 스타일 절(실행 시점 길에 파일 읽기 한 갈래, 94행의 "생성 script의 키 목록" → 데이터 파일), [자산 파이프라인](concepts/asset-pipeline.md)의 스타일 절, [탭 막대 스타일](tab-bar-style-design.md) 47행, `ui_style.h` 177~179행의 주석. 색인(`docs/README.md`)은 통합 때 붙인다.

## 확인

자동 test.

- `tests/ui_style_json_tests.cpp`(`luil_core_tests`, Windows와 Android 기기):
  - 내장 파일: `LUIL_STYLE`이 가리킨 파일을 읽은 값이 `default_ui_style()`과 `operator==`로 같다. 기기에는 소스 트리가 없어 파일을 `cmake/embed_binary.cmake`로 시험 실행 파일에 싣는다(사례 표도 같다);
  - 표의 자리: 구간마다 행의 필드 주소(객체 주소와의 차)가 선언 순서로 늘고 빈틈 없이 이어져 그 struct(또는 `caption` 앞까지)를 덮는지. 행이 빠지거나 순서가 어긋나면 실패한다. 대신할 필드가 꼭 있는 행을 가리키는지;
  - 사례 표: 사례마다 받음 여부가 적힌 대로이고, 막을 때 적힌 첫 오류가 오류 목록에 있는지. 받을 때 적힌 값이 필드에 있는지(구문 색이 빠지면 같은 테마의 본문색, `raisedBackground`가 빠지면 `noticeBackground` 값 등);
  - 오류 모으기: 오류 둘(낯선 키와 짧은 색)이 표 순서로 둘 다 오는지. 문법 오류의 `offset`, 깊은 중첩의 `too_deep`, 겹친 키의 `duplicate_key`, 빈 객체와 배열 뿌리. `describe`가 종류마다 생성기의 글과 같은지;
  - 빠진 컴포넌트 칸은 struct 기본값(`tab_bar_style {}`)이지 내장값이 아닌지. 고대비에서는 읽은 스타일의 `dark.window_background`가 `color_palette_for`에 나오지 않는지.
- `tests/style_tests.cmake`(`luil_style`, Windows): 사례 표를 돌고, 키 표 헤더에 데이터 파일의 행이 모두 있는지(`"syntaxFunction"`과 `&neutral_color_palette::syntax_function`, 대신할 필드), 대신할 키가 꼭 있는 키가 아니면 생성이 실패하는지.
- `tests/style_raster_tests.cpp`: 읽은 스타일(어두운 창 바탕과 `controlCornerRadius`를 바꾼 글)을 `frame_palette`와 `make_arranged_tree(style)`에 주면 바탕 픽셀과 버튼 모서리가 바뀌는지. 한쪽(배치)만 주면 모서리가 그대로인지.
- `luil_umbrella_header`(우산 헤더에 새 헤더), `luil_core_portability`, `luil_source_style`, `luil_installed_consumer`(설치본이 BUILD_INTERFACE로 감싼 링크 뒤에도 서는지).

데모. 상태를 label로 그려 `luil-uia`로 되읽는다.

- 데스크톱 테마 페이지에 "스타일 파일" 묶음을 둔다. 여러 줄 칸 `theme-style-json`(내장 파일 글을 실은 채 시작한다. 데모 빌드가 같은 파일을 싣는다), 단추 `theme-style-apply`·`theme-style-reset`과 견본 단추 셋(어두운 창 바탕 바꿈, 낯선 키 `windowBackgrund`, 닫는 괄호 없음)이다. label `theme-style-status`가 "적용됨 · 오류 0 · dark.windowBackground #1a1b26" 또는 "거부 · 오류 1 · Unknown key in dark: windowBackgrund · 이전 스타일 유지"를 말한다. 적용은 `frame->style`과 주 tree 배치에 같은 포인터를 준다.
  - 구현한 이름: 칸 `theme-style-json`, 단추 `theme-style-apply`("적용")·`theme-style-reset`("내장값으로")·`theme-style-sample-background`("견본: 창 바탕 바꿈")·`theme-style-sample-unknown`("견본: 낯선 키")·`theme-style-sample-broken`("견본: 괄호 없음"), label `theme-style-status`. 견본은 칸에 글을 싣고 곧바로 읽는다. 처음과 `theme-style-reset` 뒤의 label은 "내장값 · dark.windowBackground #1e1e1e"다. 문법 오류의 label은 "거부 · 오류 1 · Invalid JSON at byte N · 이전 스타일 유지"다.
- 모바일 데모 테마 페이지에 같은 묶음을 둔다(여러 줄 칸 대신 견본 단추만). 단추는 견본 셋과 `theme-style-reset`이고 label은 폭에서 줄을 바꾼다(세 줄까지).
- UIA(격리 데스크톱): 견본을 Invoke해 label을 읽고, 거부 뒤에도 앞서 적용한 값이 label에 남는지, `theme-style-json`에 SetValue로 글을 넣고 적용하면 label이 그 값을 말하는지, `theme-style-reset` 뒤 내장값인지 읽는다. 모바일 데모(Windows 창)도 같은 label을 읽는다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.

사람 확인.

- 적용한 스타일의 밝은·어두운 테마 대비와 고대비에서 스타일이 무시되는지 눈으로 본다.
- 오류 글이 고칠 곳(자리와 키)을 바로 가리키는지.
- Android 실기기에서 견본 적용 뒤 창 바탕·모서리가 바뀌는지.

## 범위 밖

- 선택자·상속·상태별 값(G4 c). 요소가 앱에서 색을 받지 않는다는 [테마](concepts/theming.md) 69행의 원칙과 부딪힌다.
- 파일 읽기, 감시, 다시 읽기의 디바운스(R5 hot_reload). APK assets 읽기(결정 6).
- 스타일을 JSON으로 쓰는 직렬화, 덮어쓰기 파일(결정 4의 길 c), 스키마 판 번호.
- 줄 나눔 정책 키(`text.wrapPolicy`). 결정 5는 길 b이고 [줄 나눔 정책 스타일 키](wrap-policy-key-design.md)가 이 문서의 키 표 위에 더했다.
- R1에서 넘어온 controller 한계, 메뉴 폭의 글꼴 배율, `android_settings.cpp`의 `local_frame` 복제. 사용자에게 따로 묻는다.

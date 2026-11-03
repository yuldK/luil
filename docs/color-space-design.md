# 색 공간과 16진 색

색 고르기가 쓰는 색 계산을 순수 함수로 낸다. HSV 값 형 `hsv_color`, sRGB 8비트 `ui_color`와의 변환, 16진 글의 검사·쓰기·거르기다. 공개 형식은 [color_space.h](../include/luil/ui/color_space.h)의 `hsv_color`·`clamp_hsv`·`color_from_hsv`·`hsv_from_color`·`hex_color_problem`·`hex_color_check`·`check_hex_color`·`format_hex_color`·`hex_color_filter`다.

요소와 popup은 [색 고르기](color-picker-design.md)에 있다. 글 검사와 거르기의 꼴은 [숫자 칸 검증](number-input-design.md)을 따른다. 색 고르기 문서가 250줄을 넘어 이 부분을 나눴다.

## 문제

- 색 계산이 없다. [ui_theme.h](../include/luil/theme/ui_theme.h)에는 `ui_color`(0xAARRGGBB, 11행), `make_ui_color`(22행), `with_alpha`(30행), `relative_luminance`(40행), `is_dark_background`(42행)뿐이다. HSV 변환과 공개 16진 함수가 없다. 16진은 비공개로 셋이 각자 짓는다. [ui_style_json.cpp](../src/theme/ui_style_json.cpp) `read_color`(303행)는 스타일 파일의 `#RRGGBB`·`#RRGGBBAA`(알파 뒤)만 엄격히 읽고, [inspector_overlay.cpp](../src/host/inspector_overlay.cpp) `append_hex`(98행)는 검사기 팔레트에 `#AARRGGBB` 아홉 자를 대문자로 쓰며, [style_sheet_sample.h](../examples/demo/style_sheet_sample.h) `style_color_text`(137행)는 데모에 소문자 `#rrggbb[aa]`를 쓴다.
- 지각 균일 공간은 비공개다. OKLab 변환(`oklab_from`, `linear_rgb_from`)은 [ui_theme.cpp](../src/theme/ui_theme.cpp) 50~130행의 익명 이름공간에 있고 강조색 합성(367행)만 쓴다.
- 앱이 색을 글로 받으면 검사를 앱마다 짓는다. 숫자 칸은 `check_number_input`과 `number_input_filter`를 갖지만([number_input.h](../include/luil/ui/number_input.h) 54·68행) 색 글에는 짝이 없다.

## 공개 형식

```cpp
// include/luil/ui/color_space.h
struct hsv_color
{
    float hue { 0.0f };  // 도, [0, 360]. 360은 0과 같은 색이다
    float saturation { 0.0f };  // [0, 1]
    float value { 0.0f };  // 명도, [0, 1]
    float alpha { 1.0f };  // [0, 1]
    [[nodiscard]] bool operator==(const hsv_color&) const = default;
};

[[nodiscard]] hsv_color clamp_hsv(hsv_color color) noexcept;  // 범위로 자른다. NaN은 0, alpha의 NaN은 1이다
[[nodiscard]] ui_color color_from_hsv(hsv_color color) noexcept;  // 채널마다 lround(x * 255)
// 채도가 0이면 색상을, 명도가 0이면 색상과 채도를 keep에서 가져온다 (결정 1).
[[nodiscard]] hsv_color hsv_from_color(ui_color color, const hsv_color& keep = {}) noexcept;

enum class hex_color_problem { none, empty, not_hex, wrong_length };
struct hex_color_check
{
    hex_color_problem problem { hex_color_problem::empty };
    std::optional<ui_color> color {};  // 읽었으면 있다
    [[nodiscard]] bool valid() const noexcept { return problem == hex_color_problem::none; }
};
[[nodiscard]] hex_color_check check_hex_color(std::u8string_view text, bool alpha);
[[nodiscard]] std::u8string format_hex_color(ui_color color, bool alpha);  // "#3366FF", 알파면 "#3366FF80"
[[nodiscard]] text_insert_filter hex_color_filter();  // apply_text_edit의 거르기
```

## 규칙

**변환.** 식은 sRGB 부호 값 위의 표준 HSV다. 선형 빛으로 바꾸지 않는다.

- `color_from_hsv`는 먼저 `clamp_hsv`로 자르고 색상 360을 0으로 접는다. 채널마다 `lround(x * 255)`로 8비트에 담는다. 알파도 같다. 식 안의 계산은 `double`이라 왕복의 가운데 값에서 반올림이 어긋나지 않는다(구현에서 정했다).
- `hsv_from_color`는 8비트 색에서 실수 넷을 짓는다. 채도가 0(회색)이면 색상이 정해지지 않아 `keep.hue`를, 명도가 0(검정)이면 색상과 채도가 정해지지 않아 `keep.hue`·`keep.saturation`을 쓴다. 앱은 지금 상태를 `keep`으로 넘겨 16진으로 회색을 넣어도 색상 막대가 0으로 튀지 않게 한다.
- 왕복 약속은 한 방향이다. 모든 `ui_color`에서 `color_from_hsv(hsv_from_color(c, k)) == c`다. 반대 방향(HSV에서 8비트를 거쳐 HSV)은 양자화로 어긋난다. 그래서 상태는 HSV로 들고 8비트는 내보낼 때만 짓는다(결정 1).

**16진 읽기.** `check_hex_color(text, alpha)`는 문제 하나와 읽은 색을 돌려준다.

- 앞뒤 공백을 무시하고 앞의 `#`는 있어도 없어도 된다. 전각 숫자·전각 영문·전각 `#`는 ASCII로 읽는다(IME 전각 확정).
- 자릿수는 3·6이고, `alpha`가 참이면 4·8도 받는다. 3·4자리는 자리마다 두 번 늘린다(`#abc`는 `#AABBCC`). 알파 자리가 없으면 불투명(FF)이다. 순서는 `#RRGGBBAA`다(결정 2).
- 빈 글은 `empty`, 16진 밖 글자가 있으면 `not_hex`, 자릿수가 맞지 않으면 `wrong_length`다. `alpha`가 거짓일 때 8자리는 `wrong_length`다.
- 대소문자를 가리지 않는다. 오류 글의 문구는 앱의 말이다(숫자 칸과 같다).

**16진 쓰기와 거르기.**

- `format_hex_color`는 `#`와 대문자 여섯 자리, `alpha`가 참이면 여덟 자리다. 같은 색은 늘 같은 글이라 test와 UIA 되읽기가 비교할 수 있다.
- `hex_color_filter`는 넣는 조각에서 `0-9`, `a-f`, `A-F`, `#`만 남기고 전각은 ASCII로 바꾼다. 조각만 보므로 "##12"처럼 이어 붙인 틀린 글은 검사가 잡는다. 붙여넣은 "색: #12 34 zz"는 "#1234"다.

## 결정

**1. 색 모델과 값 형.** 질문: 앱이 어떤 값을 들고 어느 공간에서 고르는가.

- 길 a: HSV를 `hsv_color`(실수 넷)로 앱이 들고 8비트는 `color_from_hsv`로 낸다. 되는 것: 판·막대가 HSV 축 그대로라 그리기가 그라디언트 둘이다. 회색·검정으로 끌어도 색상과 채도가 상태에 남는다. 16진·RGB와 일대일로 오간다. 대가: 앱 상태가 `ui_color`가 아니라 실수 넷이고 저장할 때 바꾼다. HSV는 지각 균일이 아니라 같은 명도라도 밝기가 달라 보인다.
- 길 b: `ui_color` 하나만 든다. 되는 것: 앱 상태가 가장 작다. 대가: 채도 0이나 명도 0에서 끄는 순간 색상이 사라지고, 8비트 왕복 오차로 손잡이가 흔들린다.
- 길 c: OKLCH로 고른다. 되는 것: 강조색 합성과 같은 지각 균일 공간이다. 대가: sRGB 밖 색을 자르는 규칙이 필요하고, 익명 OKLab 변환을 공개하며 팔레트 합성 파일(ui_theme.cpp)을 고친다. 판을 픽셀마다 계산해 그려야 한다.
- 권장: a. 대가: 위의 실수 넷과 지각 균일이 아닌 점이다. 승인: 길 a (2026-11-03).

**2. 알파와 8자리 순서.** 질문: 알파를 어떻게 켜고 8자리를 어느 순서로 쓰는가.

- 길 a: `alpha` 인자(색 고르기 설정, 기본 끔)이고 8자리는 `#RRGGBBAA`. 되는 것: CSS와 같은 순서라 웹에서 복사한 색이 그대로 읽히고, 스타일 파일의 `#RRGGBBAA`(`read_color`)와도 같은 순서다. 끄면 막대도 8자리도 없다. 대가: 코드의 `ui_color` 리터럴(0xAARRGGBB)과 순서가 다르고, 검사기 표시(`append_hex`)는 `#AARRGGBB`라 어긋난다.
- 길 b: `#AARRGGBB`. 되는 것: `ui_color`와 검사기 표시(`append_hex`)와 같은 순서다. 대가: 웹과 스타일 파일(`read_color`)의 8자리 색이 다른 색으로 읽힌다.
- 길 c: 늘 알파. 되는 것: 설정이 없다. 대가: 투명을 다루지 않는 앱에 막대와 체커가 선다.
- 권장: a. 대가: 위의 리터럴 순서와 검사기 표시와의 어긋남이다. 권장대로 진행.

## 플랫폼

플랫폼 독립 코드다(`src/ui/color_space.cpp`). OS 색 대화나 지역 설정을 부르지 않는다. Win32와 Android가 같은 결과를 낸다. Android는 Java 소스 0줄이고 JNI도 unsupported도 없다. 경고를 오류로 보는 빌드라 `float`과 정수 사이 변환은 `static_cast`로 쓴다.

## 다른 노드와의 약속

- **color_picker(같은 문서 짝).** 요소는 이 함수만으로 색을 짓는다. 판과 막대의 값은 `hsv_color`이고 견본 칸과 16진 확정은 `hsv_from_color(색, 지금 값)`이다.
- **강조색 합성.** `ui_theme.cpp`를 고치지 않는다. 결정 1이 c가 되면 그 파일의 OKLab 변환을 공개해야 해서 팔레트 합성(`compose_palette`)과 같은 파일을 고친다.
- **숫자 칸.** `number_input.h`를 고치지 않는다. RGB 숫자 칸이 필요한 앱은 `number_input_range { 0, 255 }`로 둔다.
- **스타일 읽기·검사기·데모.** 새 `check_hex_color`는 스타일 읽기(`style_error_kind::invalid_color`를 내는 비공개 `read_color`)를 대신하지 않는다. 스타일 파일은 `#`가 꼭 있고 6·8자리만 받으며 오류를 `style_error`로 모은다. 검사기 `append_hex`와 데모 `style_color_text`의 쓰기도 이번에 고치지 않는다.

## 확인

- `tests/color_space_tests.cpp`(새 파일, `luil_core_tests`). 채널마다 5 걸음 격자와 0·255에서 `color_from_hsv(hsv_from_color(c, k)) == c`. 회색·검정에서 `keep`의 색상·채도 유지. 색상 360과 0이 같은 색. `clamp_hsv`의 NaN과 범위 밖. `check_hex_color`의 `#abc`·`abc`·`#AABBCC`·`  #aabbcc\n`·8자리·4자리·전각, 알파 끔의 8자리 `wrong_length`, `#12G`의 `not_hex`, 빈 글의 `empty`. `format_hex_color`의 대문자와 알파 순서. `hex_color_filter`의 조각 거르기.
- 사람 확인은 없다. 화면에 닿는 확인은 색 고르기 문서의 몫이다.

## 범위 밖

- OKLCH·HSL·Lab 판과 넓은 색역(Display P3), 색 이름(`red`), `rgb()` 꼴 글.
- 선형 빛 보간. 판이 sRGB 부호 값에서 HSV와 같게 그려지는 것으로 충분하다.

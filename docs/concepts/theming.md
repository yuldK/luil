# 테마와 글꼴

앱은 [`appearance_settings`](../../include/luil/theme/appearance.h)로 테마 선호와 accent id를, `font_settings`로 UI·고정폭 글꼴 가족을 게시한다. 좁은 범위는 `appearance_overrides`와 `font_overrides`의 optional 값으로 덮어쓴다.

`theme_preference`는 `system`, `light`, `dark`다. `apply_overrides()`는 지정된 항목만 대체한다. accent override의 빈 문자열은 미지정으로 취급하지만, font override의 빈 문자열은 내장 기본 글꼴을 명시적으로 선택한다. accent id를 모르는 경우에도 저장된 문자열은 보존하고 표시 계층은 기본 `mint`로 물러선다. font family가 비어 있거나 설치되지 않아도 설정 값은 보존하고 font resolver가 내장 글꼴을 선택한다.

## 팔레트

`resolve_color_theme()`는 high contrast, system의 OS 선호, 명시적 light/dark 순서로 실제 테마를 고른다. `accent_definition`은 하나의 대표색과 dark/light별 네 역할을 제공한다.

| 역할 | 용도 |
| --- | --- |
| `accent` | 채움·테두리·정상 상태 |
| `hover` | 강조 요소의 hover |
| `soft` | 배지·선택 행 같은 옅은 바탕 |
| `emphasis_foreground` | 강조 바탕 위 글자 |

`compose_palette()`는 중립 색(`neutral_color_palette`)과 이 네 역할, 그리고 `accent_tones`를 한 `ui_color_palette`로 접는다. 의미를 색 하나에만 싣지 않고 disabled, warning, error, divider 같은 별도 역할을 둔다. `color_palette_for()`는 그 위의 편의 함수로, 테마에 맞는 중립 색과 accent 정의의 테마별 역할을 골라 넘긴다. high contrast에서는 accent도 중립 색도 무시하고 OS 시스템 색을 `high_contrast_palette_for()`로 사용한다.

accent 4역할에서 파생한 역할은 팔레트에 따로 있다. element가 알파를 발명하지 않도록 합성이 tone을 얹어 만든 색이다.

| 파생 역할 | 얹는 자리 | 양 (`accent_tones`) |
| --- | --- | --- |
| `accent_pressed` | `accent` | `pressed` |
| `soft_button_background`·`soft_button_hover_background` | `soft`·`hover` | `soft_button`·`soft_button_hover` |
| `active_toggle_background` | `soft` | `active_toggle` |
| `selection_background` (+ `selection_foreground`) | `soft` | `selection` |
| `row_selection_background` | `soft` | `row_selection` |
| `drop_target_background` | `accent` | `drop_target` |
| `danger_button_background`·`_hover_`·`_pressed_` | `error_accent` | `danger_button`·`_hover`·`_pressed` |

high contrast는 이 파생 역할도 알파 없이 highlight 짝으로 접는다. `row_selection_background`만은 알파 0으로 접어 채움을 없앤다 — 행의 글자는 앱이 그려 highlight 글자로 바꿀 수 없으므로 온전한 highlight를 깔면 글이 사라지고, 왼쪽 표식(`accent`)이 어느 행인지 남긴다. `selection_foreground`는 텍스트 입력이 고른 구간의 글자에 쓰는 짝이라 high contrast에서만 본문 글자와 달라진다.

accent 카탈로그는 `assets/accents.json`에서 빌드 시 생성된다. `system` accent는 OS key color를 실행 시점에 `make_system_accent()`로 합성하고 `set_system_accent()`의 atomic 값으로 교체한다. 정적 `accent_catalog()`에는 넣지 않고 `system_accent()`가 optional 값으로 반환한다. `accent_for()`는 값으로 반환하므로 정적 카탈로그나 실행 시점 합성 결과의 수명이 호출자에게 새지 않는다.

## 스타일 — 앱이 정하는 중립 색과 치수

키 컬러는 사용자 설정이지만 바탕·표면·글자·구분선 같은 **중립 색**은 앱의 것이다. [`ui_style`](../../include/luil/theme/ui_style.h)이 그 자리다: 두 테마의 `neutral_color_palette`, `accent_tones`, 그리고 그리기 안에 박혀 있던 치수 `ui_metrics`(본문·작은 글자 크기, 컨트롤·행 모서리)다.

내장값은 `assets/style.json`에서 빌드 시 생성되며 `default_ui_style()`이 돌려준다. 앱이 손대는 길은 둘이다.

- **빌드 시점** — 같은 스키마의 JSON을 `LUIL_STYLE`로 준다 ([자산 파이프라인](asset-pipeline.md)). 앱 브랜딩처럼 고정된 값이면 이것으로 충분하다.
- **실행 시점** — `ui_style` 값을 세워 `ui_frame::style`에 `shared_ptr`로 싣는다. 비어 있으면 내장 스타일이다. 값을 계산해서 만드는 앱(OS 색에서 중립 색을 뽑는 경우 등)은 `compose_palette()`가 같은 규칙으로 팔레트를 접어 주므로 그리기 코드가 그것을 구별하지 못한다.

```cpp
auto style { std::make_shared<luil::ui_style>(luil::default_ui_style()) };
style->dark.window_background = luil::make_ui_color(26, 27, 38);
style->dark.surface_background = luil::make_ui_color(36, 40, 59);
style->metrics.control_corner_radius = 6.0f;
frame->style = style; // 매 frame 같은 포인터를 실으면 된다
```

표시 계층은 `frame_palette()`로 스타일·테마·accent에서 팔레트 하나를 만들고, 그리기와 창 테두리(DWM)와 웹뷰의 바닥색이 그 하나를 본다. 창 요소의 명암은 테마 이름이 아니라 `is_dark_background(window_background)`가 정한다 — 앱이 "dark"에 밝은 바탕을 두어도 OS 그림이 바탕을 따라간다. high contrast는 스타일을 무시한다. 접근성 설정은 앱의 스타일보다 세다.

`ui_metrics`는 `draw_context::metrics`로 element에 닿는다. config에 이미 열린 값(`label_config::font_size`·`button_config::corner_radius`)과 앱이 tree를 짓는 산수에 쓰는 배치 상수(`check_row_height` 같은 `inline constexpr`)는 여기 담지 않는다. 텍스트 입력의 글꼴 크기도 담지 않는다 — 포인터를 caret으로 옮기는 계산이 그리기 밖에서 같은 글꼴을 재기 때문이다.

## 색은 역할로 말한다

element 설정은 구체 색을 담지 않는다. [`button_visual_role`](../../include/luil/ui/button_element.h), [`text_button_visual`](../../include/luil/ui/dialog_elements.h), [`badge_tone`](../../include/luil/ui/badge_element.h), [`toast_severity`](../../include/luil/ui/toast_element.h), [`label_color_role`](../../include/luil/ui/label_element.h)는 모두 "이 자리가 무엇을 뜻하는가"만 담고, 그 뜻을 색으로 바꾸는 일은 그리기 시점에 팔레트가 한다. 그래서 테마를 바꾸면 element를 하나도 손대지 않고 화면 전체가 따라온다.

element가 `ui_color`를 앱에서 받는 자리는 라이브러리 어디에도 없다. 색을 그대로 받으면 그 색은 dark에서 고른 값 그대로 light에 남고, 고대비에서는 사용자가 OS에서 고른 색을 그 element 하나만 무시한다. 접근성 설정을 element가 뒤엎는 자리가 되는 것이라 열지 않는다.

이 원칙의 예외는 팔레트 자체다. `ui_style`은 색을 받지만 element 하나의 색이 아니라 **역할 전체의 값**이고, 테마마다 따로 있으며, 고대비는 그것을 무시한다. 그래서 element가 색을 받을 때 생기던 세 문제(테마를 따라오지 못함·역할 사이의 어긋남·접근성 설정을 뒤엎음)가 그대로 열리지 않는다.

## 선택자 — 하나뿐인 escape hatch

역할로 이름 붙일 수 없는 모양이 필요할 때만 **팔레트 선택자**를 받는다. 형태는 언제나 `std::function<ui_color(const ui_color_palette&)>`이고 비어 있으면 "역할이 정한 색 그대로"다. 색이 아니라 **고르는 방법**을 받으므로 고르는 시점은 여전히 그리기이고, 테마 전환과 고대비가 그대로 성립한다.

| 자리 | 무엇을 고르는가 |
| --- | --- |
| [`panel_config::background`](../../include/luil/ui/panel_element.h) | panel의 바탕 (기본은 `surface_background`) |
| [`button_config`](../../include/luil/ui/button_element.h)의 다섯 슬롯 | 아이콘 버튼의 쉼·hover·눌림 바탕과 글리프 색 |
| [`text_button_config::fill`·`label_color`](../../include/luil/ui/dialog_elements.h) | 글자 버튼의 채움과 라벨 색 |
| [`modal_host_config::scrim_background`](../../include/luil/ui/modal_host_element.h) | modal scrim의 색 (주면 `scrim_opacity`는 쓰이지 않는다) |

선택자를 준 자리는 상태에 따라 색을 흔들지 않는다. 색 하나를 받았으므로 hover·눌림에도 그 색이 그대로 남는다. 상태마다 다른 색을 고를 수 있는 것은 상태별 슬롯이 팔레트에 이미 있는 아이콘 버튼뿐이다.

## 선택자 대신 역할을 세워야 할 때

같은 선택자가 여러 화면·여러 앱에서 되풀이되면 그것은 팔레트에 역할이 하나 빠져 있다는 신호다. 특히 다음은 선택자가 아니라 역할의 일이다.

- 뜻이 있는 색: 되돌릴 수 없는 동작은 `button_visual_role::danger`·`text_button_visual::danger`가 `error_accent`로 말한다. 앱마다 붉은색을 고르면 한 화면의 두 붉은색이 갈린다.
- 고대비에서 접히는 방식을 정해야 하는 색: `high_contrast_palette_for()`가 역할마다 어떤 시스템 색으로 접을지 정한다. 선택자로 고른 색에는 그 자리가 없다.
- 비활성 표시: element마다 알파를 발명하지 않고 `disabled_foreground` 하나를 쓴다.

역할이 늘면 `ui_color_palette`와 합성 둘(`compose_palette`·`high_contrast_palette_for`)이 함께 늘어난다. 중립 역할이면 `neutral_color_palette`와 `assets/style.json`, 생성 script의 키 목록도 늘어난다. 그것을 같이 정할 수 없는 색이라면 그것은 아직 역할이 아니다.

## 글꼴 fallback

공개 `font_resolver::family(name)`는 글꼴 가족 이름을 미리 보기용 `SkTypeface*`로 바꾸는 인터페이스다. 빈 이름은 기본 글꼴이며 찾지 못한 이름은 null을 반환한다. draw 호출 동안 포인터가 유효하도록 구현이 수명을 유지해야 한다.

내부 [`win32_fonts`](../../src/win32/win32_fonts.h)는 설정된 UI·코드 글꼴과 글리프 fallback을 관리한다. 그리기와 입력 측정이 같은 글꼴을 사용하도록 registry를 공유한다. `fallback_typeface(codepoint)`는 사용자 UI 언어를 사용해 DirectWrite에 대체 글꼴을 요청하고 `sk_sp<SkTypeface>`를 반환한다. code point별 cache는 4096개 상한에 닿으면 비운다. `WM_FONTCHANGE`에서는 글꼴 cache와 관리자를 무효화하고 다음 조회에서 다시 만든다. 이미 반환한 `sk_sp`의 typeface는 계속 살아 있다.

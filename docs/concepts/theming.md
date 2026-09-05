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

`color_palette_for()`는 중립 UI 색과 이 네 역할을 합성한다. 의미를 색 하나에만 싣지 않고 disabled, warning, error, divider 같은 별도 역할을 둔다. high contrast에서는 accent를 무시하고 OS 시스템 색을 `high_contrast_palette_for()`로 사용한다.

accent 카탈로그는 `assets/accents.json`에서 빌드 시 생성된다. `system` accent는 OS key color를 실행 시점에 `make_system_accent()`로 합성하고 `set_system_accent()`의 atomic 값으로 교체한다. 정적 `accent_catalog()`에는 넣지 않고 `system_accent()`가 optional 값으로 반환한다. `accent_for()`는 값으로 반환하므로 정적 카탈로그나 실행 시점 합성 결과의 수명이 호출자에게 새지 않는다.

## 글꼴 fallback

공개 `font_resolver::family(name)`는 글꼴 가족 이름을 미리 보기용 `SkTypeface*`로 바꾸는 인터페이스다. 빈 이름은 기본 글꼴이며 찾지 못한 이름은 null을 반환한다. draw 호출 동안 포인터가 유효하도록 구현이 수명을 유지해야 한다.

내부 [`win32_fonts`](../../src/win32/win32_fonts.h)는 설정된 UI·코드 글꼴과 글리프 fallback을 관리한다. 그리기와 입력 측정이 같은 글꼴을 사용하도록 registry를 공유한다. `fallback_typeface(codepoint)`는 사용자 UI 언어를 사용해 DirectWrite에 대체 글꼴을 요청하고 `sk_sp<SkTypeface>`를 반환한다. code point별 cache는 4096개 상한에 닿으면 비운다. `WM_FONTCHANGE`에서는 글꼴 cache와 관리자를 무효화하고 다음 조회에서 다시 만든다. 이미 반환한 `sk_sp`의 typeface는 계속 살아 있다.

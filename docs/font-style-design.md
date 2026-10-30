# 글꼴 굵기와 기울임

글에 굵기(100~900, 사이 값 포함)와 기울임을 지정하고, 그 굵기의 진짜 face를 찾아 그린다. 공개 형식은 [font_style.h](../include/luil/text/font_style.h)의 `font_weight`·`font_slant`·`font_style`이다. 글을 놓는 자리마다 이 값을 받는다: 직접 그리는 글의 `text_font`, `label_config`, 컴포넌트 글의 `ui_metrics::font_weights`, 직접 짓는 `SkFont`의 `apply_font_style`.

## 문제

- 굵기는 `bool bold` 하나였다. 켜면 `SkFont::setEmbolden`(획 덧칠)이라 Light·Medium·SemiBold 같은 중간 굵기를 표현할 수 없었다. 덧칠은 세기가 글자 크기로만 정해져 한 단계뿐이고 작은 글씨에서 획이 뭉친다.
- 기울임은 `setSkewX(-0.2)`로 똑바른 글자를 눕힐 뿐이었다.
- 글꼴 등록부의 모든 조회(가족 해석, 기본 UI 글꼴, 글자별 대체)가 `SkFontStyle::Normal()`이었다. Segoe UI Semibold·Bold가 깔려 있어도 쓰지 못했다.
- label에는 굵기 칸이 없었고, 버튼·탭·메뉴 같은 컴포넌트는 `context.ui_typeface`로 글꼴을 직접 지어 앱이 굵기를 바꿀 길이 없었다.
- 앱 글꼴(`load_typeface_file`)은 face 하나였다. 굵기별 파일을 한 가족으로 묶거나 가변 글꼴의 굵기 축을 쓸 수 없었다.

## 규칙

굵기·기울임은 face를 고르는 값이다. 고른 face가 다 내지 못한 몫만 덧칠·눕힘으로 메운다.

1. **face가 어디서 왔는가**로 물을 곳이 갈린다 (`style_typeface`).
   - 시스템 가족: 등록부가 시스템 관리자에서 얻어 내준 face다. 관리자에 (가족, 굵기, 기울임)으로 다시 묻는다.
   - 등록한 앱 가족(`register_font_family`): 그 가족의 face 중 기울임이 맞고 굵기가 가장 가까운 것이다. 같은 거리면 regular 이상의 요청은 더 굵은 쪽, 그 아래는 더 가는 쪽이다.
   - 그 밖(파일·바이트로 읽은 face, codicon): 그 face 그대로다. 시스템에 같은 이름의 가족이 깔려 있어도 묻지 않는다. 이름으로 찾으면 앱이 실어 온 글꼴이 조용히 사용자 PC의 다른 판으로 바뀐다.
2. **가변 글꼴**: 고른 face에 `wght` 축이 있으면 요청한 굵기(축 범위로 자른 값)로 clone한다. 다른 축은 지금 자리를 잇는다. 350 같은 사이 값도 그대로 선다.
3. **메움**: `synthetic_bold_threshold`(semibold) 이상을 요청했는데 face가 그보다 가늘면 덧칠한다. 기울임을 요청했는데 face가 똑바로 서 있으면 눕힌다. 브라우저의 `font-synthesis`와 같은 기준이다.
   - 관리자가 이미 흉내 낸 face(DirectWrite의 simulation)는 다시 메우지 않는다.
   - 문턱 아래(medium 등)는 메우지 않는다. 덧칠은 Bold 하나만 흉내 내므로 medium을 덧칠하면 요청보다 굵어진다.
4. **기본 모양**(regular·upright)은 받은 face를 그대로 쓴다는 뜻이다. 자물쇠를 쥐지 않아 매 frame 그리기의 대부분이 이 길로 지나간다. Bold 파일을 읽은 앱 글꼴은 기본 모양이어도 굵다.
5. **대체 글꼴도 같은 굵기**다. 고른 글꼴에 없는 글자(Segoe UI 속 한글)의 대체 face를 (글자, 굵기, 기울임)으로 고르고, 메움은 대체 face에 맞춰 다시 건다. 바탕의 메움을 그대로 이으면 바탕이 진짜 Bold일 때 가는 대체 face가 덧칠을 못 받고, 바탕이 덧칠일 때 굵은 대체 face가 두 번 굵어진다.
   - 대체 해석기(`font_fallback`)는 `for_styled_codepoint`로 모양을 받는다. 기본 몸통은 모양 없이 `for_codepoint`를 부르므로 앱이 지은 해석기도 그대로 빌드된다. 플랫폼의 등록부 해석기가 덮어쓴다.
   - 그리기는 바탕 `SkFont`에서 모양을 읽는다(`font_style_of`). face의 굵기(가변 글꼴은 축 자리)에 덧칠·눕힘을 더한 값이다.

## cache

- 굵기를 바꾼 face는 (바탕 face id, 굵기, 기울임)으로 담는다. clone을 매번 새로 만들면 typeface id가 frame마다 바뀌어 배치 cache가 한 번도 맞지 않는다. 항목은 바탕 face를 함께 쥐어 그 id가 다른 face에 다시 쓰이지 않게 한다. 상한은 1024개이고 넘으면 통째로 비운다.
- 시스템 출처 표도 face를 쥔다. 굵기를 바꿔 얻은 face와 대체 face도 출처를 남겨, 범위 강조가 그 face를 다시 바꿀 때 같은 가족에 묻는다.
- `clear_font_caches`(글꼴 설치·삭제, 기본 UI 글꼴 목록 변경, 앱 가족 등록)가 둘 다 비운다. 이미 건네진 face는 `sk_sp`라 살아 있다.
- 자물쇠 순서는 registry → 선호 목록, registry → 앱 가족이다. 앱 가족 등록은 자기 자물쇠를 놓은 뒤에 cache를 비운다.

## 쓰는 자리

| 자리 | 값 |
| --- | --- |
| 직접 그리는 글 | `text_font::weight`·`slant`. `make_text_font`와 `measure_text_block`이 같은 face로 그리고 잰다 |
| label | `label_config::weight`·`slant`. 한 줄 폭(`preferred_size`)도 그 굵기로 잰다 |
| 범위 강조 | markdown·여러 줄 칸의 `bold`/`italic`은 진짜 Bold·Italic face다. 굵기는 바탕보다 가늘어지지 않는다 |
| 컴포넌트 | `ui_metrics::font_weights` (아래) |
| 직접 짓는 `SkFont` | `apply_font_style(font, style)`. 크기·품질은 그대로 두고 face와 메움만 바꾼다 |
| 측정 | `measure_context::text_width(text, size, style)`, `styled_text_measurer(style)`, 메뉴 popup의 `menu_popup_config::weight` |

범위 강조는 `bool`로 둔다. "이 구간을 강조한다"는 뜻이라 구간마다 임의 굵기를 주는 것은 이번 범위에서 뺐다.

배치가 없는 여러 줄 칸의 길(shaping을 끈 구성)은 caret·조각 자리를 보통 글꼴로 잰다. 진짜 Bold face는 폭이 달라 다음 조각과 겹치므로 그 길만 덧칠을 남긴다.

### 컴포넌트 굵기

`ui_metrics::font_weights`(`ui_font_weights`)는 config에 자리가 없는 글의 굵기다. `draw_context::metrics`와 `measure_context::metrics`로 그리기와 배치에 함께 닿아, 잰 폭과 그린 글이 같은 굵기다.

| 칸 | 글 | 기본값 |
| --- | --- | --- |
| `body_text` | 목록 행·메뉴·탭·드롭다운·체크 라벨·표 칸·toast | regular |
| `small_text` | 머리행 제목·tooltip·끌기 ghost | regular |
| `button_text` | 글 단추 | regular |
| `selected_tab` | 고른 탭의 이름 | regular |
| `selected_choice` | 선택지 묶음의 고른 칸 | semibold |
| `title` | dialog·app bar·caption 줄·묶음 틀의 제목 | regular |

- 고른 선택지는 전에 덧칠로 굵혔다. 진짜 face는 덧칠보다 넓어 칸 폭을 `body_text`와 `selected_choice` 중 넓은 쪽으로 잰다. 고른 칸이 바뀌어도 칸이 들썩이지 않는다.
- 탭의 자리는 이름 폭으로 정하지 않아 `selected_tab`을 바꿔도 탭이 움직이지 않는다. 좁은 탭에서는 이름이 먼저 줄어든다.
- 필드 이름에 `small`을 쓰지 않는다. Windows 헤더(rpcndr.h)가 그 낱말을 `char`로 바꾼다.

스타일 JSON에서는 `metrics.fontWeights`이고 값은 굵기의 이름이다. 칸도 묶음도 빠져도 된다. 빠진 칸은 struct의 기본값이라 옛 JSON이 그대로 빌드된다. 모르는 이름·숫자 값·낯선 키는 생성 단계에서 막는다.

```json
"metrics": {
    "bodyFontSize": 12,
    "smallFontSize": 11,
    "controlCornerRadius": 3,
    "rowCornerRadius": 4,
    "fontWeights": {
        "bodyText": "regular",
        "selectedTab": "semibold",
        "title": "bold"
    }
}
```

이름은 `thin`·`extraLight`·`light`·`regular`·`medium`·`semibold`·`bold`·`extraBold`·`black`이다.

## 앱 글꼴 가족

```cpp
luil::register_font_family(u8"Pretendard", {
    luil::load_typeface_file(u8"assets/Pretendard-Regular.otf"),
    luil::load_typeface_file(u8"assets/Pretendard-SemiBold.otf"),
    luil::load_typeface_file(u8"assets/Pretendard-Bold.otf"),
});
luil::set_ui_typeface_families({ u8"Pretendard", u8"Segoe UI" });
```

- 등록한 이름은 설치된 가족처럼 쓰인다. 기본 UI 글꼴 목록, 설정의 글꼴 가족(`font_settings`), 설치 목록(`installed_font_families`)에 나온다. 같은 이름이면 설치된 가족보다 앞선다.
- 가변 글꼴 face 하나만 넣어도 `wght` 축으로 모든 굵기를 낸다.
- 같은 이름을 다시 등록하면 face 목록을 바꾼다. nullptr face는 버리고 남은 것이 없으면 등록을 지운다. `unregister_font_family`가 지운다.
- 등록과 해제는 글꼴 cache를 비운다. 앱 시작 전에 하는 것이 맞고, 이미 서 있는 화면은 다음 배치부터 새 가족이다.

## 다음 글꼴 처리

`font_style`은 글꼴 등록부·측정·그리기가 같은 face를 찾는 한 길이다. 폭(`wdth`), 자간, OpenType 기능(숫자 고정폭 `tnum` 같은 것)은 이 값에 칸을 늘려 같은 길을 탄다. 자간과 OpenType 기능은 HarfBuzz 배치·측정·caret이 모두 따라야 해 따로 설계한다.

## 검증

- `tests/font_style_tests.cpp`(core, 빈 글꼴 관리자): 굵기 값 자르기, 메움 문턱, 출처를 모르는 face를 그대로 두고 메우기, 기본 모양으로 되돌리면 메움을 지우기, face 없는 글꼴, 넣은 측정기가 굵기보다 앞서기.
- `tests/font_style_native_tests.cpp`(Windows 실제 글꼴): Segoe UI의 Bold·Semibold·Italic이 진짜 face이고 같은 요청이 같은 face인지, 파일로 읽은 face가 시스템 가족으로 바뀌지 않고 덧칠되는지, `SegUIVar.ttf`의 `wght` 축 clone과 범위 자르기, 앱 가족의 이름 해석·Bold face·기본 UI 글꼴·해제, 굵은 글 속 한글의 Bold 대체 face, `make_text_font`와 굵기 측정이 같은 face인지, 굵기 cache의 상한과 비우기.
- `tests/text_block_native_tests.cpp`: light·regular·semibold·bold italic 글의 배치 폭과 그린 폭이 같은지.
- `tests/style_tests.cmake`: `metrics.fontWeights`의 생성, 빠진 묶음·칸, 모르는 이름·숫자 값·낯선 키·문자열 묶음을 막는지.
- `tests/ui_style_tests.cpp`: 컴포넌트 굵기의 기본값과 내장 JSON이 같은지.

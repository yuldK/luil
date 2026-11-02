# 스타일 JSON의 키 표다. `set()`만 있다 (docs/style-sheet-design.md).
# 생성기(generate_style.cmake)가 이것으로 기본값 헤더와 실행 중 파서가 쓰는 키 표 헤더를 함께 내고,
# 생성기 시험(tests/style_tests.cmake)도 같은 목록을 읽는다. 키 목록은 여기 한 곳에만 있다.
#  - 행 순서는 struct 선언 순서다. 기본값 헤더가 지정 초기화라 어긋나면 컴파일이 막고, 빠진 행은
#    core test(ui_style_json_tests.cpp)가 표의 필드 자리로 잡는다.
#  - 새 중립 역할은 struct 필드와 여기 한 행, `assets/style.json` 한 줄이다.

# JSON 키와 C++ 필드의 짝이다. 순서가 곧 지정 초기화의 순서라 struct 선언과 같아야 한다.
set(caption_fields
    "background|background"
    "foreground|foreground"
    "buttonHoverBackground|button_hover_background"
    "buttonHoverForeground|button_hover_foreground"
    "closeButtonHoverBackground|close_button_hover_background"
    "closeButtonHoverForeground|close_button_hover_foreground")
set(neutral_fields
    "windowBackground|window_background"
    "surfaceBackground|surface_background"
    "primaryForeground|primary_foreground"
    "secondaryForeground|secondary_foreground"
    "disabledForeground|disabled_foreground"
    "divider|divider"
    "inputBackground|input_background"
    "inputBorder|input_border"
    "controlBorder|control_border"
    "groupBorder|group_border"
    "warningAccent|warning_accent"
    "errorAccent|error_accent"
    "buttonHoverBackground|button_hover_background"
    "buttonHoverForeground|button_hover_foreground"
    "buttonPressedBackground|button_pressed_background"
    "tooltipBackground|tooltip_background"
    "tooltipBorder|tooltip_border"
    "contentShadow|content_shadow"
    "noticeBackground|notice_background")
# 나중에 생긴 중립 역할이다. 앱이 LUIL_STYLE로 준 옛 JSON이 그대로 빌드되어야 하므로 빠져도 된다.
#  - 짝은 `key|field|대신할 key`다. 빠지면 같은 테마의 그 key 값을 쓴다 — 옛 JSON은 역할이 갈리기 전과 같은 색으로 그린다.
#  - 대신할 key는 `neutral_fields`의 키여야 한다. 생성기가 검사한다.
#  - struct에서는 `neutral_fields`의 뒤, `caption`의 앞에 이 순서로 선다.
set(neutral_optional_fields
    "raisedBackground|raised_background|noticeBackground"
    "thumbBackground|thumb_background|buttonPressedBackground"
    "backdropBackground|backdrop_background|windowBackground"
    # 구문 색이다. 옛 JSON은 본문색으로 그려 구문 색 없이 읽힌다.
    "syntaxKeyword|syntax_keyword|primaryForeground"
    "syntaxString|syntax_string|primaryForeground"
    "syntaxComment|syntax_comment|primaryForeground"
    "syntaxNumber|syntax_number|primaryForeground"
    "syntaxType|syntax_type|primaryForeground"
    "syntaxFunction|syntax_function|primaryForeground")
set(tone_fields
    "pressed|pressed"
    "softButton|soft_button"
    "softButtonHover|soft_button_hover"
    "activeToggle|active_toggle"
    "selection|selection"
    "rowSelection|row_selection"
    "dropTarget|drop_target"
    "dangerButton|danger_button"
    "dangerButtonHover|danger_button_hover"
    "dangerButtonPressed|danger_button_pressed")
set(metric_fields
    "bodyFontSize|body_font_size"
    "smallFontSize|small_font_size"
    "controlCornerRadius|control_corner_radius"
    "rowCornerRadius|row_corner_radius")
# tone은 알파라 0~1이고, 치수는 논리 픽셀이라 위가 열려 있다 (상한은 실수 방지용이다).
set(tone_range 0 1)
set(metric_range 0 1000)

# 컴포넌트 스타일(`tabBar`·`popup`·`text`)은 **빠져도 된다** — 앱이 LUIL_STYLE로 준 옛 JSON이 그대로 빌드되어야 하고,
# 빠진 칸은 struct의 기본값(처음부터의 모양)이다. 낯선 키와 모르는 값은 여기서도 막는다.
#  - 짝은 `key|field|enum type|허용 값들`이고 허용 값은 `,`로 적는다. JSON 값은 camelCase, C++ 값은 snake_case다.
#  - 한 enum 형은 어느 칸에서든 같은 허용 값 목록을 쓴다. 생성기가 형마다 이름 표 하나를 낸다.
set(palette_roles "windowBackground,surfaceBackground,inputBackground,tooltipBackground,noticeBackground,captionBackground,buttonHoverBackground,accentSoft")
set(tab_bar_fields
    "variant|variant|tab_bar_variant|underline,connected,flat"
    "closeVisibility|close_visibility|tab_close_visibility|always,selectedOrHovered"
    "indicatorPlacement|indicator_placement|tab_indicator_placement|iconCorner,closeSlot"
    "barBackground|bar_background|palette_role|${palette_roles}"
    "selectedBackground|selected_background|palette_role|${palette_roles}")
set(popup_fields
    "corner|corner|popup_corner|system,round,roundSmall,square")
set(text_fields
    "lineBreak|line_break|single_line_break|space,symbol,paragraphs"
    "wrapPolicy|wrap_policy|line_break_policy|hangulWords,unicode")
# `metrics.fontWeights`다 (`ui_font_weights`). 값은 굵기의 이름이고 빠진 칸은 struct의 기본값이다.
set(font_weight_names "thin,extraLight,light,regular,medium,semibold,bold,extraBold,black")
set(font_weight_fields
    "bodyText|body_text|font_weight|${font_weight_names}"
    "smallText|small_text|font_weight|${font_weight_names}"
    "buttonText|button_text|font_weight|${font_weight_names}"
    "selectedTab|selected_tab|font_weight|${font_weight_names}"
    "selectedChoice|selected_choice|font_weight|${font_weight_names}"
    "title|title|font_weight|${font_weight_names}")

# 컴포넌트 칸의 JSON 키와 C++ 필드, 그 필드 표다. 순서가 곧 지정 초기화의 순서라 struct 선언과 같아야 한다.
set(component_sections
    "tabBar|tab_bar|tab_bar_fields"
    "popup|popup|popup_fields"
    "text|text|text_fields")

# 뿌리에 꼭 있는 키다. 컴포넌트 칸은 빠져도 되고, 그 밖의 키는 막는다.
set(root_required_keys dark light tones metrics)

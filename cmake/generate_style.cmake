# 스타일 정의(assets/style.json)를 C++ 상수(`luil::generated::default_style`)로 바꾼다.
# 두 테마의 중립 색, accent tone, 치수가 한 파일에 있다 — `ui_style` 하나가 곧 그 파일이다.
#
# 생성물은 `ui_style`의 지정 초기화라 키가 하나라도 빠지거나 낯선 키가 있으면 여기서
# 막는다. 색은 `#rrggbb` 또는 `#rrggbbaa`다 — 중립 색에는 알파가 실린 역할
# (구분선·보조 글자)이 있어 accent 카탈로그와 달리 알파를 받는다.
if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required.")
endif()
file(READ "${INPUT_FILE}" style_json)
string(JSON root_type ERROR_VARIABLE type_error TYPE "${style_json}")
if(type_error OR NOT root_type STREQUAL "OBJECT")
    message(FATAL_ERROR "The style file must be a JSON object: ${INPUT_FILE}\n${type_error}")
endif()

# 객체의 키가 기대한 목록과 정확히 같은지 본다.
# 낯선 키는 오타일 가능성이 커서 조용히 지나가면 "바꿨는데 안 바뀐다"가 된다.
function(luil_style_expect_keys object_json path expected_keys)
    string(JSON count LENGTH "${object_json}")
    set(seen "")
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE 0 ${last})
            string(JSON key MEMBER "${object_json}" ${index})
            if(NOT key IN_LIST expected_keys)
                message(FATAL_ERROR "Unknown key in ${path}: ${key}")
            endif()
            list(APPEND seen "${key}")
        endforeach()
    endif()
    foreach(key IN LISTS expected_keys)
        if(NOT key IN_LIST seen)
            message(FATAL_ERROR "Missing key in ${path}: ${key}")
        endif()
    endforeach()
endfunction()

# `#rrggbb`·`#rrggbbaa`를 ARGB 상수로 바꾼다. 알파를 생략하면 불투명이다.
function(luil_style_color output_variable path color_text)
    if(color_text MATCHES "^#([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])$")
        set(rgb "${CMAKE_MATCH_1}")
        set(alpha "FF")
    elseif(color_text MATCHES "^#([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])$")
        set(rgb "${CMAKE_MATCH_1}")
        set(alpha "${CMAKE_MATCH_2}")
    else()
        message(FATAL_ERROR "Invalid color at ${path}: ${color_text} (expected #rrggbb or #rrggbbaa)")
    endif()
    string(TOUPPER "${alpha}${rgb}" digits)
    set(${output_variable} "0x${digits}u" PARENT_SCOPE)
endfunction()

# 0 이상의 실수를 float 리터럴로 바꾼다.
function(luil_style_number output_variable path number_text minimum maximum)
    if(NOT number_text MATCHES "^[0-9]+(\\.[0-9]+)?$")
        message(FATAL_ERROR "Invalid number at ${path}: ${number_text}")
    endif()
    if(number_text LESS minimum OR number_text GREATER maximum)
        message(FATAL_ERROR "Number out of range at ${path}: ${number_text} (expected ${minimum}..${maximum})")
    endif()
    if(number_text MATCHES "\\.")
        set(${output_variable} "${number_text}f" PARENT_SCOPE)
    else()
        set(${output_variable} "${number_text}.0f" PARENT_SCOPE)
    endif()
endfunction()

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

function(luil_style_keys_of output_variable field_pairs)
    set(keys "")
    foreach(pair IN LISTS field_pairs)
        string(REGEX REPLACE "\\|.*$" "" key "${pair}")
        list(APPEND keys "${key}")
    endforeach()
    set(${output_variable} "${keys}" PARENT_SCOPE)
endfunction()

# 짝은 `key|field`다 — `;`로 가르면 CMake 리스트가 짝을 두 원소로 풀어 버린다.
function(luil_style_split_pair pair key_variable field_variable)
    string(REGEX REPLACE "\\|.*$" "" key "${pair}")
    string(REGEX REPLACE "^.*\\|" "" field "${pair}")
    set(${key_variable} "${key}" PARENT_SCOPE)
    set(${field_variable} "${field}" PARENT_SCOPE)
endfunction()

# 한 테마의 중립 색을 `neutral_color_palette` 지정 초기화로 적는다.
function(luil_style_neutral output_variable theme_key)
    string(JSON theme_json ERROR_VARIABLE theme_error GET "${style_json}" "${theme_key}")
    if(theme_error)
        message(FATAL_ERROR "The style file has no ${theme_key} palette: ${theme_error}")
    endif()
    luil_style_keys_of(neutral_keys "${neutral_fields}")
    list(APPEND neutral_keys "caption")
    luil_style_expect_keys("${theme_json}" "${theme_key}" "${neutral_keys}")

    set(text "        .${theme_key} = {\n")
    foreach(pair IN LISTS neutral_fields)
        luil_style_split_pair("${pair}" key field)
        string(JSON color_text GET "${theme_json}" "${key}")
        luil_style_color(color "${theme_key}.${key}" "${color_text}")
        string(APPEND text "            .${field} = ${color},\n")
    endforeach()

    string(JSON caption_json GET "${theme_json}" "caption")
    luil_style_keys_of(caption_keys "${caption_fields}")
    luil_style_expect_keys("${caption_json}" "${theme_key}.caption" "${caption_keys}")
    string(APPEND text "            .caption = {\n")
    foreach(pair IN LISTS caption_fields)
        luil_style_split_pair("${pair}" key field)
        string(JSON color_text GET "${caption_json}" "${key}")
        luil_style_color(color "${theme_key}.caption.${key}" "${color_text}")
        string(APPEND text "                .${field} = ${color},\n")
    endforeach()
    string(APPEND text "            },\n        },\n")
    set(${output_variable} "${text}" PARENT_SCOPE)
endfunction()

# 숫자 객체(tones·metrics)를 지정 초기화로 적는다.
function(luil_style_numbers output_variable object_key field_pairs minimum maximum)
    string(JSON object_json ERROR_VARIABLE object_error GET "${style_json}" "${object_key}")
    if(object_error)
        message(FATAL_ERROR "The style file has no ${object_key} object: ${object_error}")
    endif()
    luil_style_keys_of(keys "${field_pairs}")
    luil_style_expect_keys("${object_json}" "${object_key}" "${keys}")
    set(text "        .${object_key} = {\n")
    foreach(pair IN LISTS field_pairs)
        luil_style_split_pair("${pair}" key field)
        # GET은 문자열 "0.5"와 숫자 0.5를 같은 글로 돌려주므로 형을 따로 본다.
        string(JSON number_type TYPE "${object_json}" "${key}")
        if(NOT number_type STREQUAL "NUMBER")
            message(FATAL_ERROR "Expected a number at ${object_key}.${key}, got ${number_type}")
        endif()
        string(JSON number_text GET "${object_json}" "${key}")
        luil_style_number(number "${object_key}.${key}" "${number_text}" ${minimum} ${maximum})
        string(APPEND text "            .${field} = ${number},\n")
    endforeach()
    string(APPEND text "        },\n")
    set(${output_variable} "${text}" PARENT_SCOPE)
endfunction()

# 컴포넌트 스타일(`tabBar`·`popup`·`text`)은 **빠져도 된다** — 앱이 LUIL_STYLE로 준 옛 JSON이 그대로 빌드되어야 하고,
# 빠진 칸은 struct의 기본값(처음부터의 모양)이다. 낯선 키와 모르는 값은 여기서도 막는다.
#  - 짝은 `key|field|enum type|허용 값들`이고 허용 값은 `,`로 적는다. JSON 값은 camelCase, C++ 값은 snake_case다.
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
    "lineBreak|line_break|single_line_break|space,symbol,paragraphs")

# 컴포넌트 칸의 JSON 키와 C++ 필드, 그 필드 표다. 순서가 곧 지정 초기화의 순서라 struct 선언과 같아야 한다.
set(component_sections
    "tabBar|tab_bar|tab_bar_fields"
    "popup|popup|popup_fields"
    "text|text|text_fields")

# camelCase를 snake_case로 바꾼다 (`selectedOrHovered` → `selected_or_hovered`).
function(luil_style_snake_case output_variable text)
    string(REGEX REPLACE "([A-Z])" "_\\1" snake "${text}")
    string(TOLOWER "${snake}" snake)
    set(${output_variable} "${snake}" PARENT_SCOPE)
endfunction()

# 컴포넌트 칸 하나를 지정 초기화로 적는다. 칸이 없으면 아무것도 적지 않는다 (struct의 기본값).
function(luil_style_component output_variable json_key cpp_field field_specs)
    string(JSON section_type ERROR_VARIABLE missing TYPE "${style_json}" "${json_key}")
    if(missing)
        set(${output_variable} "" PARENT_SCOPE)
        return()
    endif()
    if(NOT section_type STREQUAL "OBJECT")
        message(FATAL_ERROR "${json_key} must be an object, got ${section_type}")
    endif()
    string(JSON section_json GET "${style_json}" "${json_key}")
    set(known_keys "")
    foreach(spec IN LISTS field_specs)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 key)
        list(APPEND known_keys "${key}")
    endforeach()
    string(JSON count LENGTH "${section_json}")
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE 0 ${last})
            string(JSON key MEMBER "${section_json}" ${index})
            if(NOT key IN_LIST known_keys)
                message(FATAL_ERROR "Unknown key in ${json_key}: ${key}")
            endif()
        endforeach()
    endif()
    # 지정 초기화는 선언 순서라야 하므로 JSON 순서가 아니라 표의 순서로 적는다.
    set(text "        .${cpp_field} = {\n")
    foreach(spec IN LISTS field_specs)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 key)
        list(GET parts 1 field)
        list(GET parts 2 enum_type)
        list(GET parts 3 allowed)
        string(JSON value ERROR_VARIABLE absent GET "${section_json}" "${key}")
        if(absent)
            continue()
        endif()
        string(REPLACE "," ";" allowed "${allowed}")
        if(NOT value IN_LIST allowed)
            message(FATAL_ERROR "Invalid value at ${json_key}.${key}: ${value} (expected one of ${allowed})")
        endif()
        luil_style_snake_case(enum_value "${value}")
        string(APPEND text "            .${field} = ${enum_type}::${enum_value},\n")
    endforeach()
    string(APPEND text "        },\n")
    set(${output_variable} "${text}" PARENT_SCOPE)
endfunction()

set(root_keys "dark;light;tones;metrics")
set(components_text "")
foreach(section IN LISTS component_sections)
    string(REPLACE "|" ";" parts "${section}")
    list(GET parts 0 json_key)
    list(GET parts 1 cpp_field)
    list(GET parts 2 fields_variable)
    string(JSON probe ERROR_VARIABLE absent GET "${style_json}" "${json_key}")
    if(NOT absent)
        list(APPEND root_keys "${json_key}")
    endif()
    luil_style_component(section_text "${json_key}" "${cpp_field}" "${${fields_variable}}")
    string(APPEND components_text "${section_text}")
endforeach()
luil_style_expect_keys("${style_json}" "root" "${root_keys}")
luil_style_neutral(dark_text "dark")
luil_style_neutral(light_text "light")
# tone은 알파라 0~1이고, 치수는 논리 픽셀이라 위가 열려 있다 (상한은 실수 방지용이다).
luil_style_numbers(tones_text "tones" "${tone_fields}" 0 1)
luil_style_numbers(metrics_text "metrics" "${metric_fields}" 0 1000)

set(output_text "#pragma once\n\n")
string(APPEND output_text "// 스타일 JSON에서 생성된 파일이다. 직접 고치지 않는다.\n")
string(APPEND output_text "// 원본: ${INPUT_FILE}\n\n")
string(APPEND output_text "#include \"luil/theme/ui_style.h\"\n\n")
string(APPEND output_text "namespace luil::generated {\n")
string(APPEND output_text "    inline constexpr ui_style default_style {\n")
string(APPEND output_text "${dark_text}${light_text}${tones_text}${metrics_text}${components_text}")
string(APPEND output_text "    };\n} // namespace luil::generated\n")

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${output_text}")

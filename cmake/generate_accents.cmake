# 키 컬러 정의(assets/accents.json)를 C++ 값 표로 바꾼다.
# 런타임 JSON 파싱을 두지 않으려고 빌드 시점에 내장하며, 형식 오류는 여기서 빌드를 세운다.
#  - 잘못된 색이 조용히 화면에 나가는 것보다 낫다.
if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required.")
endif()

file(READ "${INPUT_FILE}" accents_json)
if(NOT DEFINED DEFAULT_ACCENT_ID)
    set(DEFAULT_ACCENT_ID "mint")
endif()
string(JSON catalog_type ERROR_VARIABLE type_error TYPE "${accents_json}")
if(type_error OR NOT catalog_type STREQUAL "ARRAY")
    message(FATAL_ERROR "Accent catalog must be a JSON array: ${INPUT_FILE}")
endif()
string(JSON accent_count ERROR_VARIABLE json_error LENGTH "${accents_json}")
if(json_error)
    message(FATAL_ERROR "Accent catalog is not valid JSON: ${INPUT_FILE}\n${json_error}")
endif()
if(accent_count EQUAL 0)
    message(FATAL_ERROR "Accent catalog is empty: ${INPUT_FILE}")
endif()

# `#rrggbb`를 불투명 ARGB 상수로 바꾼다.
# 다른 표기는 받지 않는다.
function(luil_accent_color output_variable accent_index role_path color_text)
    if(NOT color_text MATCHES "^#[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]$")
        message(FATAL_ERROR "Accent ${accent_index} has an invalid ${role_path} color: ${color_text} (expected #rrggbb)")
    endif()
    string(SUBSTRING "${color_text}" 1 6 color_digits)
    string(TOUPPER "${color_digits}" color_digits)
    set(${output_variable} "0xFF${color_digits}u" PARENT_SCOPE)
endfunction()

# 한 테마의 4역할을 accent, accentHover, accentSoft, accentEmphasisFg 순으로 읽는다.
function(luil_accent_role_set output_variable accent_index entry_json theme_key)
    set(role_values)
    foreach(role_key accent accentHover accentSoft accentEmphasisFg)
        string(JSON role_text ERROR_VARIABLE role_error GET "${entry_json}" "${theme_key}" "${role_key}")
        if(role_error)
            message(FATAL_ERROR "Accent ${accent_index} is missing ${theme_key}.${role_key}: ${role_error}")
        endif()
        luil_accent_color(role_color ${accent_index} "${theme_key}.${role_key}" "${role_text}")
        list(APPEND role_values "${role_color}")
    endforeach()
    list(JOIN role_values ", " joined_roles)
    set(${output_variable} "${joined_roles}" PARENT_SCOPE)
endfunction()

set(output_text "#pragma once\n\n#include <cstdint>\n\n")
string(APPEND output_text "// accent 카탈로그 JSON에서 생성된 파일이다. 직접 고치지 않는다.\n")
string(APPEND output_text "namespace luil::generated {\n")
string(APPEND output_text "    struct accent_entry\n    {\n")
string(APPEND output_text "        const char8_t* id;\n")
string(APPEND output_text "        const char8_t* label;\n")
string(APPEND output_text "        // 설정의 색 동그라미다. 테마와 무관한 대표색이다.\n")
string(APPEND output_text "        std::uint32_t swatch;\n")
string(APPEND output_text "        // accent, accent_hover, accent_soft, accent_emphasis_fg 순이다.\n")
string(APPEND output_text "        std::uint32_t dark[4];\n")
string(APPEND output_text "        std::uint32_t light[4];\n")
string(APPEND output_text "    };\n\n")
string(APPEND output_text "    inline constexpr accent_entry accents[] {\n")

math(EXPR last_accent_index "${accent_count} - 1")
set(seen_identifiers)
foreach(accent_index RANGE 0 ${last_accent_index})
    string(JSON entry_json GET "${accents_json}" ${accent_index})

    string(JSON accent_id ERROR_VARIABLE id_error GET "${entry_json}" "id")
    if(id_error OR accent_id STREQUAL "")
        message(FATAL_ERROR "Accent ${accent_index} has no id: ${id_error}")
    endif()
    if(NOT accent_id MATCHES "^[a-z0-9-]+$")
        message(FATAL_ERROR "Accent id must be lower case ASCII with hyphens: ${accent_id}")
    endif()
    # 실행 시점 항목(OS accent)의 예약 id다.
    # 정적 표에 두면 한 id에 임자가 둘이 된다.
    if(accent_id STREQUAL "system")
        message(FATAL_ERROR "The accent id is reserved for the runtime entry: system")
    endif()
    if(accent_id IN_LIST seen_identifiers)
        message(FATAL_ERROR "Duplicate accent id: ${accent_id}")
    endif()
    list(APPEND seen_identifiers "${accent_id}")

    string(JSON accent_label ERROR_VARIABLE label_error GET "${entry_json}" "label")
    if(label_error OR accent_label STREQUAL "")
        message(FATAL_ERROR "Accent ${accent_id} has no label: ${label_error}")
    endif()

    string(JSON swatch_text ERROR_VARIABLE swatch_error GET "${entry_json}" "swatch")
    if(swatch_error)
        message(FATAL_ERROR "Accent ${accent_id} has no swatch: ${swatch_error}")
    endif()
    luil_accent_color(swatch_color ${accent_index} "swatch" "${swatch_text}")

    luil_accent_role_set(dark_roles ${accent_index} "${entry_json}" "dark")
    luil_accent_role_set(light_roles ${accent_index} "${entry_json}" "light")

    # 표시 이름을 C++ 문자열로 안전하게 내보낸다.
    string(REPLACE "\\" "\\\\" accent_label "${accent_label}")
    string(REPLACE "\"" "\\\"" accent_label "${accent_label}")
    string(REPLACE "\n" "\\n" accent_label "${accent_label}")
    string(REPLACE "\r" "\\r" accent_label "${accent_label}")
    string(REPLACE "\t" "\\t" accent_label "${accent_label}")
    string(APPEND output_text
        "        { u8\"${accent_id}\", u8\"${accent_label}\", ${swatch_color}, { ${dark_roles} }, { ${light_roles} } },\n")
endforeach()

# 앱 설정이 모르는 id를 담고 있을 때 물러설 기본 색이다.
# 없으면 오류 처리한다.
if(NOT DEFAULT_ACCENT_ID IN_LIST seen_identifiers)
    message(FATAL_ERROR "The accent catalog must contain the default id: ${DEFAULT_ACCENT_ID}")
endif()

string(APPEND output_text "    };\n} // namespace luil::generated\n")
get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}" "${output_text}")

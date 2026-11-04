cmake_minimum_required(VERSION 4.2)

file(READ "${SOURCE_DIR}/assets/accents.json" catalog)
string(JSON entry GET "${catalog}" 0)
string(JSON entry SET "${entry}" id "\"brand\"")
string(JSON entry SET "${entry}" label "\"Brand \\\"Blue\\\"\\nLine\\tTwo\\\\End\"")
file(MAKE_DIRECTORY "${BINARY_DIR}/accent-test")
set(input "${BINARY_DIR}/accent-test/accent.json")
set(output "${BINARY_DIR}/accent-test/accents.h")
file(WRITE "${input}" "[${entry}]")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}"
    "-DOUTPUT_FILE=${output}" -DDEFAULT_ACCENT_ID=brand
    -P "${SOURCE_DIR}/cmake/generate_accents.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Custom catalog generation failed")
endif()
file(READ "${output}" header)
string(FIND "${header}" [=[u8"Brand \"Blue\"\nLine\tTwo\\End"]=] label_position)
if(label_position EQUAL -1)
    message(FATAL_ERROR "Custom label was not escaped correctly")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}"
    "-DOUTPUT_FILE=${output}" -DDEFAULT_ACCENT_ID=missing
    -P "${SOURCE_DIR}/cmake/generate_accents.cmake" RESULT_VARIABLE result
    OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "Missing default accent was accepted")
endif()
file(WRITE "${input}" "{}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}"
    "-DOUTPUT_FILE=${output}" -P "${SOURCE_DIR}/cmake/generate_accents.cmake"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "Non-array catalog was accepted")
endif()

# 언어 맵이다. `ko`가 `label`이고 나머지는 태그 차례로 언어별 이름 칸에 든다.
string(JSON entry SET "${entry}" label [=[{ "pt-BR": "Marca", "ko": "브랜드", "en": "Brand \"One\"" }]=])
file(WRITE "${input}" "[${entry}]")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}"
    "-DOUTPUT_FILE=${output}" -DDEFAULT_ACCENT_ID=brand
    -P "${SOURCE_DIR}/cmake/generate_accents.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Language map generation failed")
endif()
file(READ "${output}" header)
string(FIND "${header}" [=[{ u8"brand", u8"브랜드", ]=] label_position)
if(label_position EQUAL -1)
    message(FATAL_ERROR "The ko text of a language map is not the label")
endif()
string(FIND "${header}" [=[, 0u, 2u },]=] count_position)
if(count_position EQUAL -1)
    message(FATAL_ERROR "The entry does not count its two other languages")
endif()
string(FIND "${header}" [=[{ u8"en", u8"Brand \"One\"" },]=] english_position)
string(FIND "${header}" [=[{ u8"pt-BR", u8"Marca" },]=] portuguese_position)
if(english_position EQUAL -1 OR portuguese_position EQUAL -1 OR portuguese_position LESS english_position)
    message(FATAL_ERROR "The language names are not escaped or not in tag order")
endif()
string(FIND "${header}" [=[{ u8"ko", ]=] korean_position)
if(NOT korean_position EQUAL -1)
    message(FATAL_ERROR "The ko text must live only in the label")
endif()

# 맵이 아니고 글도 아닌 이름, `ko` 없는 맵, 틀린 태그, 빈 글, 글이 아닌 값을 막는다.
foreach(rejected
    [=[3]=]
    [=[""]=]
    [=[{}]=]
    [=[{ "en": "Brand" }]=]
    [=[{ "ko": "브랜드", "EN": "Brand" }]=]
    [=[{ "ko": "브랜드", "en_US": "Brand" }]=]
    [=[{ "ko": "브랜드", "en": "" }]=]
    [=[{ "ko": "" }]=]
    [=[{ "ko": 3 }]=]
    [=[{ "ko": "브랜드", "en": { "one": "Brand" } }]=]
    [=[{ "ko": "브랜드", "en-us": "a", "en-US": "b" }]=])
    string(JSON rejected_entry SET "${entry}" label "${rejected}")
    file(WRITE "${input}" "[${rejected_entry}]")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}"
        "-DOUTPUT_FILE=${output}" -DDEFAULT_ACCENT_ID=brand
        -P "${SOURCE_DIR}/cmake/generate_accents.cmake" RESULT_VARIABLE result
        OUTPUT_QUIET ERROR_QUIET)
    if(result EQUAL 0)
        message(FATAL_ERROR "An invalid label was accepted: ${rejected}")
    endif()
endforeach()

# 저장소 카탈로그는 25개 모두 영어 이름을 든다.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${SOURCE_DIR}/assets/accents.json"
    "-DOUTPUT_FILE=${output}" -P "${SOURCE_DIR}/cmake/generate_accents.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The repository catalog failed to generate")
endif()
file(READ "${output}" header)
string(REGEX MATCHALL "\{ u8\"en\", " english_names "${header}")
list(LENGTH english_names english_count)
if(NOT english_count EQUAL 25)
    message(FATAL_ERROR "The repository catalog has ${english_count} English names, not 25")
endif()

# 요소의 .cpp와 공개 헤더가 한글 문자열을 직접 들이지 않았는지 본다.
#
# 라이브러리 요소가 스스로 놓는 글의 기본 문구는 `src/ui/default_text.cpp` 한곳에 모은다
# (docs/default-text-design.md). 요소마다 한국어를 박으면 R5의 i18n이 모을 자리가 다시 흩어지는데
# 컴파일러는 그것을 잡지 못한다. 다음 노드가 헤더 밖에 한국어를 박는 것을 여기서 막는다.
#
# 한글은 `//` 주석 안에서만 허용한다. 기본 문구를 설명하는 한글이 주석에 정당하게 든다.
# 코드 줄에 한글이 있으면 문자열이든 문자든 위반이다. 식별자는 ASCII뿐이라 다른 경우가 없다.
if(NOT DEFINED SOURCE_DIRECTORY OR NOT DEFINED INCLUDE_DIRECTORY)
    message(FATAL_ERROR "SOURCE_DIRECTORY and INCLUDE_DIRECTORY are required.")
endif()

set(checked_directories
    "${SOURCE_DIRECTORY}/ui"
    "${INCLUDE_DIRECTORY}/luil/ui")

# 한글(음절·자모·호환 자모)의 UTF-8 바이트열이다.
# CMake의 정규식은 바이트 단위라 `[가-힣]`로 적으면 `·`나 `…` 같은 다른 비ASCII 글자까지 잡는다.
# 그래서 선행 바이트를 string(ASCII)로 만들어 한글의 범위만 고른다.
#   음절 U+AC00..D7A3: EA B0..BF xx, EB xx xx, EC xx xx, ED 80..9E xx
#   자모 U+1100..11FF: E1 84..87 xx. 호환 자모 U+3130..318F: E3 84..86 xx
string(ASCII 234 byte_ea)
string(ASCII 235 byte_eb)
string(ASCII 236 byte_ec)
string(ASCII 237 byte_ed)
string(ASCII 225 byte_e1)
string(ASCII 227 byte_e3)
string(ASCII 128 byte_80)
string(ASCII 132 byte_84)
string(ASCII 134 byte_86)
string(ASCII 135 byte_87)
string(ASCII 158 byte_9e)
string(ASCII 176 byte_b0)
string(ASCII 191 byte_bf)
set(hangul_lead "(${byte_ea}[${byte_b0}-${byte_bf}]|[${byte_eb}${byte_ec}]|${byte_ed}[${byte_80}-${byte_9e}]|${byte_e1}[${byte_84}-${byte_87}]|${byte_e3}[${byte_84}-${byte_86}])")
# 글자 하나다 (선행 바이트 둘 또는 하나에 이어지는 바이트까지).
set(hangul_char "${hangul_lead}[${byte_80}-${byte_bf}]")

# 파일 이름마다 허용하는 리터럴이다. 리터럴을 그대로 적으므로 둘째 리터럴이 늘면 위반으로 남는다.
#  - `text_area_element.cpp`: 글자 높이를 재는 견본이다. 화면에 나오는 글이 아니라 한글 글자 높이를 재는 용도다.
set(allowed_text_area_element.cpp "u8\"Wg 0${hangul_char}\"")

set(violations "")
set(checked_count 0)
foreach(directory IN LISTS checked_directories)
    if(NOT IS_DIRECTORY "${directory}")
        message(FATAL_ERROR "A checked directory is missing: ${directory}")
    endif()
    file(GLOB_RECURSE files "${directory}/*.h" "${directory}/*.cpp")
    foreach(file IN LISTS files)
        get_filename_component(name "${file}" NAME)
        # 기본 문구가 사는 유일한 자리다.
        if(name STREQUAL "default_text.cpp")
            continue()
        endif()
        math(EXPR checked_count "${checked_count} + 1")
        file(READ "${file}" text)
        # 줄 단위로 찾기 전에 목록을 깨는 글자를 무해한 글자로 바꾼다 (`;`와 `[`·`]`는 CMake 목록의 구분과 묶음이다).
        string(REPLACE "\r" "" code "${text}")
        string(REPLACE ";" " " code "${code}")
        string(REPLACE "[" "(" code "${code}")
        string(REPLACE "]" ")" code "${code}")
        string(REPLACE "\\" "/" code "${code}")
        string(REGEX REPLACE "//[^\n]*" "" code "${code}")
        string(REGEX MATCHALL "[^\n]*${hangul_lead}[^\n]*" lines "${code}")
        foreach(line IN LISTS lines)
            set(remaining "${line}")
            if(DEFINED allowed_${name})
                string(REGEX REPLACE "${allowed_${name}}" "" remaining "${remaining}")
            endif()
            if(remaining MATCHES "${hangul_lead}")
                file(RELATIVE_PATH relative "${SOURCE_DIRECTORY}/.." "${file}")
                string(STRIP "${line}" stripped)
                list(APPEND violations "${relative}: ${stripped}")
            endif()
        endforeach()
    endforeach()
endforeach()

list(LENGTH violations violation_count)
if(violation_count GREATER 0)
    string(REPLACE ";" "\n  " violation_text "${violations}")
    message(FATAL_ERROR
        "Hangul text is written outside src/ui/default_text.cpp.\n"
        "Add a function to src/ui/default_text.h and call it (docs/default-text-design.md):\n  ${violation_text}")
endif()

message(STATUS "Default text check passed: ${checked_count} file(s)")

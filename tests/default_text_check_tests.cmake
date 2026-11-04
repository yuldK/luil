cmake_minimum_required(VERSION 4.2)

# 한글 검사 스크립트를 작은 폴더에 돌려 본다 (cmake/verify_default_text.cmake).
#  - 저장소가 깨끗할 때는 그 검사가 위반을 잡는지 알 수 없다. 선택 라이브러리가 쓰는 두 선택 인자도 여기서 친다.
set(root "${BINARY_DIR}/default-text-check")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/src/ui" "${root}/include/luil" "${root}/other")
file(WRITE "${root}/src/ui/clean.cpp" "// 주석의 한글은 된다.\nconst char8_t* const text { u8\"plain\" };  // 줄 끝 주석도 된다.\n")
file(WRITE "${root}/include/luil/clean.h" "// 헤더 주석의 한글도 된다.\n")
file(WRITE "${root}/other/clean.h" "// 다른 폴더다.\n")

# 넘긴 인자 하나로 검사를 돌려 기대와 다르면 세운다. 따옴표로 넘겨 인자의 `;`가 목록 그대로 간다.
function(expect_check what expected)
    set(option "-DNO_OPTION=1")
    if(ARGC GREATER 2)
        set(option "${ARGV2}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIRECTORY=${root}/src"
        "-DINCLUDE_DIRECTORY=${root}/include"
        "${option}"
        -P "${CMAKE_DIRECTORY}/verify_default_text.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(expected STREQUAL "pass" AND NOT result EQUAL 0)
        message(FATAL_ERROR "${what}: the check should pass\n${output}")
    endif()
    if(expected STREQUAL "fail" AND result EQUAL 0)
        message(FATAL_ERROR "${what}: the check should fail")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

expect_check("Clean folders" pass)

# 기본 목록은 두 폴더 전체다. 깊은 폴더도 `ui` 밖의 폴더도 본다.
file(WRITE "${root}/src/host/deep/words.cpp" "const char8_t* const text { u8\"글\" };\n")
expect_check("A Hangul literal outside src/ui" fail)
string(FIND "${last_output}" "src/host/deep/words.cpp" position)
if(position EQUAL -1)
    message(FATAL_ERROR "The violation does not name its file:\n${last_output}")
endif()

# 파일 예외는 이름 하나다. 다른 이름은 그대로 위반이다.
expect_check("The text file" pass "-DTEXT_FILE=words.cpp")
expect_check("Another file name" fail "-DTEXT_FILE=other.cpp")
file(REMOVE "${root}/src/host/deep/words.cpp")

file(WRITE "${root}/include/luil/deep/words.h" "inline const char8_t* const text { u8\"글\" };\n")
expect_check("A Hangul literal in a public header" fail)

# 폴더 목록을 넘기면 그 폴더만 본다.
expect_check("Only the source folder" pass "-DCHECKED_DIRECTORIES=${root}/src")
expect_check("Two listed folders" pass "-DCHECKED_DIRECTORIES=${root}/src;${root}/other")
expect_check("A listed folder with a violation" fail "-DCHECKED_DIRECTORIES=${root}/other;${root}/include")
file(REMOVE "${root}/include/luil/deep/words.h")

# 없는 폴더는 지나치지 않는다. 오타로 검사가 비면 통과처럼 보인다.
expect_check("A missing folder" fail "-DCHECKED_DIRECTORIES=${root}/missing")

# 글자 높이를 재는 견본 하나만 허용한다. 둘째 리터럴은 위반이다.
file(WRITE "${root}/src/ui/text_area_element.cpp" "const auto sample { measure(u8\"Wg 0한\") };\n")
expect_check("The measuring sample" pass)
file(APPEND "${root}/src/ui/text_area_element.cpp" "const auto other { measure(u8\"글\") };\n")
expect_check("A second literal beside the sample" fail)

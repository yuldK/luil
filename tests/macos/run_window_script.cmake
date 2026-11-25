# AppKit host를 창으로 띄워 입력 대본(LUIL_MACOS_INPUT_SCRIPT)을 돌리고 stderr 줄을 본다 (tests/CMakeLists.txt의 macOS 창 test).
#
#   cmake -DEXECUTABLE=<앱 실행 파일> -DSCRIPT=<대본> [-DARGUMENTS=<목록>] [-DEXPECT=<정규식 목록>] [-DONCE=<정규식 목록>]
#         [-DABSENT=<정규식 목록>] -P run_window_script.cmake
#
#  - 목록은 `%%`로 잇는다 (명령 줄의 `;`는 인자를 가른다).
#
#  - EXPECT의 정규식은 출력에 **차례대로** 나와야 한다 (CTest의 PASS_REGULAR_EXPRESSION은 하나만 맞아도 통과라 쓰지 않는다).
#  - ONCE의 정규식은 출력에 정확히 한 번 나와야 하고, ABSENT의 정규식은 나오지 않아야 한다.
#  - 대본의 마지막 줄은 `quit`이다. 앱이 60초 안에 끝나지 않으면 실패다.
cmake_minimum_required(VERSION 3.25)

if(NOT EXECUTABLE OR NOT SCRIPT)
    message(FATAL_ERROR "run_window_script.cmake needs EXECUTABLE and SCRIPT.")
endif()
# 목록은 `%%`로 이어 온다 (tests/CMakeLists.txt의 luil_add_window_script_test).
foreach(name IN ITEMS ARGUMENTS EXPECT ONCE ABSENT)
    string(REPLACE "%%" ";" ${name} "${${name}}")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LUIL_MACOS_INPUT_SCRIPT=${SCRIPT}" "${EXECUTABLE}" ${ARGUMENTS}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result
    TIMEOUT 60)
message("${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The app ended with ${result}.")
endif()

set(rest "${output}")
foreach(pattern IN LISTS EXPECT)
    string(REGEX MATCH "${pattern}" found "${rest}")
    if(found STREQUAL "")
        message(FATAL_ERROR "Expected, in order: ${pattern}")
    endif()
    string(FIND "${rest}" "${found}" position)
    string(LENGTH "${found}" length)
    math(EXPR position "${position} + ${length}")
    string(SUBSTRING "${rest}" ${position} -1 rest)
endforeach()

foreach(pattern IN LISTS ONCE)
    string(REGEX MATCHALL "${pattern}" found "${output}")
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected exactly once (${count} times): ${pattern}")
    endif()
endforeach()

foreach(pattern IN LISTS ABSENT)
    string(REGEX MATCH "${pattern}" found "${output}")
    if(NOT found STREQUAL "")
        message(FATAL_ERROR "Not expected: ${found}")
    endif()
endforeach()

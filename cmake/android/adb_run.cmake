# 크로스 컴파일한 test 실행 파일을 Android 기기에서 돌린다.
#
# CTest의 CROSSCOMPILING_EMULATOR로 쓴다 — CTest와 Catch2의 test 발견이 실행 파일을
# 부르는 자리마다 이 script를 앞에 세운다 (docs/android-port-plan.md 결정 5).
#   cmake -DADB=<adb> -DDEVICE_DIRECTORY=<dir> -P adb_run.cmake <executable> [args...]
#
#  - 실행 파일은 바뀌었을 때만 올린다 (`adb push --sync`). test마다 이 script가 불리므로
#    매번 올리면 수백 MB가 오간다.
#  - 기기 쪽 표준 출력과 오류, 종료 코드를 그대로 돌려준다. Catch2의 발견은 그 출력을
#    읽고, CTest는 종료 코드로 성패를 가른다.
#  - 기기는 adb가 고른다. 여럿이면 ANDROID_SERIAL 환경 변수로 정한다.
if(NOT DEFINED ADB OR NOT DEFINED DEVICE_DIRECTORY)
    message(FATAL_ERROR "ADB and DEVICE_DIRECTORY are required.")
endif()

# -P 다음이 이 script이고 그다음부터가 실행 파일과 그 인자다.
set(executable_index -1)
math(EXPR last_index "${CMAKE_ARGC} - 1")
foreach(index RANGE 1 ${last_index})
    if(CMAKE_ARGV${index} STREQUAL "-P")
        math(EXPR executable_index "${index} + 2")
        break()
    endif()
endforeach()
if(executable_index LESS 0 OR executable_index GREATER last_index)
    message(FATAL_ERROR "No executable was given after the script.")
endif()

set(executable "${CMAKE_ARGV${executable_index}}")
get_filename_component(executable_name "${executable}" NAME)
# 구성마다 기기 쪽 자리를 나눈다 (다중 구성 빌드의 Debug·Release 디렉터리 이름).
# 한 자리를 같이 쓰면 `--sync`가 더 오래된 다른 구성의 실행 파일을 올리지 않는다.
get_filename_component(executable_directory "${executable}" DIRECTORY)
get_filename_component(configuration_name "${executable_directory}" NAME)
string(APPEND DEVICE_DIRECTORY "/${configuration_name}")
set(device_executable "${DEVICE_DIRECTORY}/${executable_name}")

execute_process(
    COMMAND "${ADB}" shell mkdir -p "${DEVICE_DIRECTORY}"
    RESULT_VARIABLE mkdir_result
    OUTPUT_QUIET)
if(NOT mkdir_result EQUAL 0)
    message(FATAL_ERROR "Could not reach the device with ${ADB} (exit ${mkdir_result}).")
endif()
execute_process(
    COMMAND "${ADB}" push --sync "${executable}" "${device_executable}"
    RESULT_VARIABLE push_result
    OUTPUT_QUIET)
if(NOT push_result EQUAL 0)
    message(FATAL_ERROR "Could not push ${executable} (exit ${push_result}).")
endif()

# 기기의 sh가 받을 한 줄을 만든다. 인자마다 작은따옴표로 감싸고, 안의 작은따옴표는
# 닫고-이스케이프-다시 열기로 옮긴다. Catch2 test 이름에는 공백·괄호·따옴표가 있다.
#
# 결과를 파일로 쓰라는 인자(`--out <path>`, `-o <path>`, `--out=<path>`)는 호스트
# 경로라 기기가 쓸 수 없다. 기기 쪽 파일로 바꿔 실행하고 끝나면 호스트로 당겨 온다 —
# Catch2의 test 발견이 목록을 그렇게 받는다.
set(command_line "cd '${DEVICE_DIRECTORY}' && chmod 755 './${executable_name}' && './${executable_name}'")
set(host_output "")
set(device_output "${DEVICE_DIRECTORY}/${executable_name}.out")
set(next_is_output FALSE)
math(EXPR first_argument "${executable_index} + 1")
if(first_argument LESS_EQUAL last_index)
    foreach(index RANGE ${first_argument} ${last_index})
        set(argument "${CMAKE_ARGV${index}}")
        if(next_is_output)
            set(host_output "${argument}")
            set(argument "${device_output}")
            set(next_is_output FALSE)
        elseif(argument STREQUAL "--out" OR argument STREQUAL "-o")
            set(next_is_output TRUE)
        elseif(argument MATCHES "^--out=(.*)$")
            set(host_output "${CMAKE_MATCH_1}")
            set(argument "--out=${device_output}")
        endif()
        string(REPLACE "'" "'\\''" argument "${argument}")
        string(APPEND command_line " '${argument}'")
    endforeach()
endif()

execute_process(
    COMMAND "${ADB}" shell "${command_line}"
    RESULT_VARIABLE run_result)
if(host_output)
    execute_process(
        COMMAND "${ADB}" pull "${device_output}" "${host_output}"
        RESULT_VARIABLE pull_result
        OUTPUT_QUIET)
    execute_process(COMMAND "${ADB}" shell rm -f "${device_output}" OUTPUT_QUIET ERROR_QUIET)
    if(NOT pull_result EQUAL 0 AND run_result EQUAL 0)
        message(FATAL_ERROR "Could not pull ${device_output} to ${host_output} (exit ${pull_result}).")
    endif()
endif()
if(NOT run_result EQUAL 0)
    # 기기 쪽 종료 코드를 그대로 낼 수는 없다 (cmake -P는 실패를 1로 끝낸다).
    # 출력은 이미 흘렀으므로 원인은 그 앞에 있다.
    message(FATAL_ERROR "${executable_name} exited with ${run_result} on the device.")
endif()

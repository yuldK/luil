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

# 파일 하나를 C++ 바이트 배열로 옮긴다.
#   cmake -DINPUT_FILE=<file> -DOUTPUT_FILE=<cpp> -DSYMBOL=<name> -P embed_binary.cmake
#
# 실행 파일 resource가 없는 플랫폼(Android, 나중에 iOS)이 codicon 글꼴을 라이브러리 안에
# 싣는 길이다. 소비자가 APK assets 같은 자리에 파일을 따로 넣는 단계가 생기지 않는다.
if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT_FILE, OUTPUT_FILE and SYMBOL are required.")
endif()
if(NOT SYMBOL MATCHES "^[a-z_][a-z0-9_]*$")
    message(FATAL_ERROR "SYMBOL must be a snake_case identifier: ${SYMBOL}")
endif()

file(READ "${INPUT_FILE}" content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
if(byte_count EQUAL 0)
    message(FATAL_ERROR "The input file is empty: ${INPUT_FILE}")
endif()

# 두 자리씩 `0x..,`로 바꾸고, 줄마다 16바이트로 끊는다.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")

get_filename_component(input_name "${INPUT_FILE}" NAME)
file(WRITE "${OUTPUT_FILE}.tmp"
    "// 생성 파일이다. 손으로 고치지 않는다 (cmake/embed_binary.cmake, ${input_name}).\n"
    "#include <cstddef>\n"
    "#include <cstdint>\n"
    "\n"
    "extern const std::uint8_t ${SYMBOL}[];\n"
    "extern const std::size_t ${SYMBOL}_size;\n"
    "\n"
    "alignas(16) const std::uint8_t ${SYMBOL}[] {\n"
    "    ${bytes}\n"
    "};\n"
    "const std::size_t ${SYMBOL}_size { ${byte_count} };\n")
# 내용이 같으면 파일을 건드리지 않는다. 다시 컴파일할 이유가 없다.
file(COPY_FILE "${OUTPUT_FILE}.tmp" "${OUTPUT_FILE}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT_FILE}.tmp")
